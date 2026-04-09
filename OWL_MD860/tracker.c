/*
 * @file tracker.c
 * @brief Stateful MQTT listener for MD-400 tracker (Table 10).
 */

#include "tracker.h"
#include "camera_iface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api2_mcast_win.h"
#include "mqtt.h"

#define TSM_MAX_TOPIC_LEN   (192U)
#define TSM_MAX_PAYLOAD_LEN (64U)

static char s_root[TSM_MAX_TOPIC_LEN] = "OWL/TRACKER";
static uint32_t s_refresh_ms = 1000U;
static uint32_t s_last_refresh = 0U;

static tracker_state_t s_state;
static tracker_state_t s_last_published;
static owl_cam_t *g_tracker_cam = NULL;

static int s_udp_mcast_enabled = 0;
static uint8_t s_udp_camera_id = 1u;
static uint32_t *s_udp_seq_counter = NULL;

static size_t tsm_bounded_len(const char *s, size_t maxlen);
static void make_topic(char *out, size_t out_sz, const char *leaf1, const char *leaf2);
static void publish_kv(const char *branch, const char *name, const char *val);
static void publish_leaf_u(const char *path, unsigned v);
static void force_publish_all(void);
static int parse_u8_dec(const char *p, uint8_t *out);
static int parse_u16_dec(const char *p, uint16_t *out);
static void tsm_rx_handler(const char *topic, const char *payload, size_t len);
static void on_set_word(const char *word, const char *payload);

static int tracker_cam_is_valid(uint8_t cam);
static int tracker_coord_mode_is_valid(uint8_t mode);
static int tracker_detect_mode_is_valid(uint8_t mode);
static int tracker_auto_mode_is_valid(uint8_t mode);
static int tracker_disable_mask_is_valid(uint8_t disable_mask);

static size_t tsm_bounded_len(const char *s, size_t maxlen)
{
    size_t i = 0U;
    if (s == NULL) {
        return 0U;
    }
    while ((i < maxlen) && (s[i] != '\0')) {
        i++;
    }
    return i;
}

void tracker_bind_cam(void *cam_handle)
{
    g_tracker_cam = (owl_cam_t *)cam_handle;
}

static int tracker_cam_is_valid(uint8_t cam)
{
    return (cam == OWL_TRACK_CAM_THERMAL) ||
           (cam == OWL_TRACK_CAM_DAY1) ||
           (cam == OWL_TRACK_CAM_DAY2);
}

static int tracker_coord_mode_is_valid(uint8_t mode)
{
    return (mode == OWL_TRK_COORD_START) || (mode == OWL_TRK_COORD_STOP);
}

static int tracker_detect_mode_is_valid(uint8_t mode)
{
    return (mode >= OWL_TRK_DETECT_DRONE) && (mode <= OWL_TRK_DETECT_FIGHTERJET);
}

static int tracker_auto_mode_is_valid(uint8_t mode)
{
    return (mode == OWL_TRK_AUTO_NONE) ||
           (mode == OWL_TRK_AUTO_DRONE) ||
           (mode == OWL_TRK_AUTO_BIRD) ||
           (mode == OWL_TRK_AUTO_FIGHTERJET) ||
           (mode == OWL_TRK_AUTO_ALL);
}

static int tracker_disable_mask_is_valid(uint8_t disable_mask)
{
    const uint8_t valid_bits = (uint8_t)(OWL_TRK_DISABLE_THERMAL |
                                         OWL_TRK_DISABLE_DAY1 |
                                         OWL_TRK_DISABLE_DAY2);

    return ((disable_mask & valid_bits) != 0u) &&
           ((disable_mask & (uint8_t)(~valid_bits)) == 0u);
}

int camera_iface_read_tracker(tracker_state_t *out_state)
{
    int rc;

    if ((out_state == NULL) || (g_tracker_cam == NULL)) {
        return OWL_ERR_ARG;
    }

    rc = owl_tracker_read_all(out_state);
    if (rc == OWL_OK) {
        return OWL_OK;
    }

    if ((rc == OWL_ERR_IO) || (rc == OWL_ERR_PROTO)) {
        rc = owl_tracker_read_all(out_state);
        if (rc == OWL_OK) {
            return OWL_OK;
        }
        if ((rc == OWL_ERR_IO) || (rc == OWL_ERR_PROTO)) {
            rc = owl_tracker_read_all(out_state);
        }
    }

    return rc;
}

int camera_iface_cmd_w6(uint8_t mode_value)
{
    tracker_state_t st;
    const uint8_t new_mode = (mode_value != 0u) ? OWL_TRK_MODE_ON : OWL_TRK_MODE_OFF;
    int rc;

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }

    rc = owl_tracker_read_all(&st);
    if (rc != OWL_OK) {
        return rc;
    }

    return owl_tracker_set_coord(st.coord_cam_id, st.coord_x, st.coord_y, new_mode);
}

int camera_iface_cmd_w7(uint8_t disable_mask)
{
    tracker_state_t st;
    uint8_t cam_bit = 0u;
    int rc;

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if (tracker_disable_mask_is_valid(disable_mask) == 0) {
        return OWL_ERR_ARG;
    }

    rc = owl_tracker_read_all(&st);
    if (rc != OWL_OK) {
        return rc;
    }

    if (st.coord_cam_id == OWL_TRACK_CAM_THERMAL) {
        cam_bit = OWL_TRK_DISABLE_THERMAL;
    } else if (st.coord_cam_id == OWL_TRACK_CAM_DAY1) {
        cam_bit = OWL_TRK_DISABLE_DAY1;
    } else if (st.coord_cam_id == OWL_TRACK_CAM_DAY2) {
        cam_bit = OWL_TRK_DISABLE_DAY2;
    } else {
        return OWL_ERR_PROTO;
    }

    if ((disable_mask & cam_bit) == 0u) {
        return OWL_OK;
    }

    return owl_tracker_set_coord(st.coord_cam_id, st.coord_x, st.coord_y, OWL_TRK_MODE_OFF);
}

int owl_tracker_mode_on(uint8_t cam, uint16_t x, uint16_t y)
{
    return owl_tracker_set_coord(cam, x, y, OWL_TRK_MODE_ON);
}

int owl_tracker_mode_off(uint8_t cam, uint16_t x, uint16_t y)
{
    return owl_tracker_set_coord(cam, x, y, OWL_TRK_MODE_OFF);
}

int owl_tracker_set_coord(uint8_t cam, uint16_t x, uint16_t y, uint8_t mode)
{
    uint8_t b[6];

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((tracker_cam_is_valid(cam) == 0) || (tracker_coord_mode_is_valid(mode) == 0)) {
        return OWL_ERR_ARG;
    }

    b[0] = cam;
    b[1] = (uint8_t)(x >> 8);
    b[2] = (uint8_t)(x & 0xFFu);
    b[3] = (uint8_t)(y >> 8);
    b[4] = (uint8_t)(y & 0xFFu);
    b[5] = mode;
    return owl_cam_write(g_tracker_cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_SET_COORD, b, 6u);
}

int owl_tracker_set_box(uint8_t cam, uint8_t w, uint8_t h)
{
    uint8_t b[3];

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((tracker_cam_is_valid(cam) == 0) || (w == 0u) || (h == 0u)) {
        return OWL_ERR_ARG;
    }

    b[0] = cam;
    b[1] = w;
    b[2] = h;
    return owl_cam_write(g_tracker_cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_SET_BOX, b, 3u);
}

int owl_tracker_set_mode(uint8_t cam, uint8_t mode)
{
    uint8_t b[2];

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((tracker_cam_is_valid(cam) == 0) || (tracker_detect_mode_is_valid(mode) == 0)) {
        return OWL_ERR_ARG;
    }

    b[0] = cam;
    b[1] = mode;
    return owl_cam_write(g_tracker_cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_SET_MODE, b, 2u);
}

int owl_tracker_set_lock(uint8_t cam, uint8_t mode)
{
    uint8_t b[2];

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if (tracker_cam_is_valid(cam) == 0) {
        return OWL_ERR_ARG;
    }

    b[0] = cam;
    b[1] = mode;
    return owl_cam_write(g_tracker_cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_SET_LOCK, b, 2u);
}

int owl_tracker_set_stab(uint8_t cam, uint8_t mode)
{
    uint8_t b[2];

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if (tracker_cam_is_valid(cam) == 0) {
        return OWL_ERR_ARG;
    }

    b[0] = cam;
    b[1] = mode;
    return owl_cam_write(g_tracker_cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_SET_STAB, b, 2u);
}

int owl_tracker_set_szone(uint8_t cam, uint8_t zone)
{
    uint8_t b[2];

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if (tracker_cam_is_valid(cam) == 0) {
        return OWL_ERR_ARG;
    }

    b[0] = cam;
    b[1] = zone;
    return owl_cam_write(g_tracker_cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_SET_SZONE, b, 2u);
}

int owl_tracker_set_auto(uint8_t cam, uint8_t mode, uint8_t sens)
{
    uint8_t b[3];

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((tracker_cam_is_valid(cam) == 0) || (tracker_auto_mode_is_valid(mode) == 0)) {
        return OWL_ERR_ARG;
    }

    b[0] = cam;
    b[1] = mode;
    b[2] = sens;
    return owl_cam_write(g_tracker_cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_SET_AUTO, b, 3u);
}

int owl_tracker_set_roi(uint8_t cam, uint8_t mode)
{
    uint8_t b[2];

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if (tracker_cam_is_valid(cam) == 0) {
        return OWL_ERR_ARG;
    }

    b[0] = cam;
    b[1] = mode;
    return owl_cam_write(g_tracker_cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_SET_ROI, b, 2u);
}

int owl_tracker_set_bitrate_centi(uint16_t centi_mbps)
{
    uint8_t b[2];

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }

    b[0] = (uint8_t)(centi_mbps >> 8);
    b[1] = (uint8_t)(centi_mbps & 0xFFu);
    return owl_cam_write(g_tracker_cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_SET_BITRATE, b, 2u);
}

int owl_tracker_set_maxfail(uint8_t cam, uint8_t code)
{
    uint8_t b[2];

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if (tracker_cam_is_valid(cam) == 0) {
        return OWL_ERR_ARG;
    }

    b[0] = cam;
    b[1] = code;
    return owl_cam_write(g_tracker_cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_SET_MAXFAIL, b, 2u);
}

int owl_tracker_set_type(uint8_t cam, uint8_t type_code)
{
    uint8_t b[2];

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if (tracker_cam_is_valid(cam) == 0) {
        return OWL_ERR_ARG;
    }

    b[0] = cam;
    b[1] = type_code;
    return owl_cam_write(g_tracker_cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_SET_TYPE, b, 2u);
}

int owl_tracker_set_search_area(uint8_t cam, uint8_t percent)
{
    uint8_t b[2];

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((tracker_cam_is_valid(cam) == 0) || (percent > 100u)) {
        return OWL_ERR_ARG;
    }

    b[0] = cam;
    b[1] = percent;
    return owl_cam_write(g_tracker_cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_SET_SEARCH_AREA, b, 2u);
}

int owl_tracker_set_confidence(uint8_t cam, uint8_t tenths)
{
    uint8_t b[2];

    if (g_tracker_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((tracker_cam_is_valid(cam) == 0) || (tenths > 10u)) {
        return OWL_ERR_ARG;
    }

    b[0] = cam;
    b[1] = tenths;
    return owl_cam_write(g_tracker_cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_SET_CONFIDENCE, b, 2u);
}

int owl_tracker_read_all(tracker_state_t *st)
{
    if ((st == NULL) || (g_tracker_cam == NULL)) {
        return OWL_ERR_ARG;
    }

#define RD(_addr, _buf, _len) \
    do { \
        size_t _io = (size_t)(_len); \
        int _rc = owl_cam_read(g_tracker_cam, OWL_IFACE_TRACKER, (_addr), (_buf), &_io); \
        if ((_rc != OWL_OK) || (_io != (size_t)(_len))) { \
            return (_rc != OWL_OK) ? _rc : OWL_ERR_IO; \
        } \
    } while (0)

    {
        uint8_t b[6];
        RD(OWL_ADDR_TRK_SET_COORD, b, 6);
        st->coord_cam_id = b[0];
        st->coord_x = ((uint16_t)b[1] << 8) | b[2];
        st->coord_y = ((uint16_t)b[3] << 8) | b[4];
        st->coord_mode = b[5];
    }

    {
        uint8_t b[3];
        RD(OWL_ADDR_TRK_SET_BOX, b, 3);
        st->box_cam_id = b[0];
        st->box_w = b[1];
        st->box_h = b[2];
    }

    {
        uint8_t b[2];
        RD(OWL_ADDR_TRK_SET_MODE, b, 2);
        st->tmode_cam_id = b[0];
        st->tmode = b[1];
    }

    {
        uint8_t b[2];
        RD(OWL_ADDR_TRK_SET_LOCK, b, 2);
        st->lock_cam_id = b[0];
        st->lock_mode = b[1];
    }

    {
        uint8_t b[2];
        RD(OWL_ADDR_TRK_SET_STAB, b, 2);
        st->stab_cam_id = b[0];
        st->stab_mode = b[1];
    }

    {
        uint8_t b[2];
        RD(OWL_ADDR_TRK_SET_SZONE, b, 2);
        st->szone_cam_id = b[0];
        st->szone_zone = b[1];
    }

    {
        uint8_t b[3];
        RD(OWL_ADDR_TRK_SET_AUTO, b, 3);
        st->ad_cam_id = b[0];
        st->ad_mode = b[1];
        st->ad_sens = b[2];
    }

    {
        uint8_t b[2];
        RD(OWL_ADDR_TRK_SET_ROI, b, 2);
        st->roi_cam_id = b[0];
        st->roi_mode = b[1];
    }

    {
        uint8_t b[2];
        RD(OWL_ADDR_TRK_SET_BITRATE, b, 2);
        st->enc_br_centi = ((uint16_t)b[0] << 8) | b[1];
    }

    {
        uint8_t b[2];
        RD(OWL_ADDR_TRK_SET_MAXFAIL, b, 2);
        st->mfail_cam_id = b[0];
        st->mfail_code = b[1];
    }

    {
        uint8_t b[2];
        RD(OWL_ADDR_TRK_SET_TYPE, b, 2);
        st->type_cam_id = b[0];
        st->type_code = b[1];
    }

    {
        uint8_t b[2];
        RD(OWL_ADDR_TRK_SET_SEARCH_AREA, b, 2);
        st->sarea_cam_id = b[0];
        st->sarea_percent = b[1];
    }

    {
        uint8_t b[2];
        RD(OWL_ADDR_TRK_SET_CONFIDENCE, b, 2);
        st->conf_cam_id = b[0];
        st->conf_tenths = b[1];
    }

#undef RD
    return OWL_OK;
}

static void make_topic(char *out, size_t out_sz, const char *leaf1, const char *leaf2)
{
    size_t pos = 0U;
    size_t n = 0U;
    size_t cap = 0U;

    if ((out == NULL) || (out_sz == 0U) || (leaf1 == NULL)) {
        return;
    }

    out[0] = '\0';
    cap = out_sz - 1U;

    n = tsm_bounded_len(s_root, TSM_MAX_TOPIC_LEN);
    if (n > cap) { n = cap; }
    (void)memcpy(&out[pos], s_root, n);
    pos += n;

    if (pos < cap) { out[pos++] = '/'; }

    n = tsm_bounded_len(leaf1, TSM_MAX_TOPIC_LEN);
    if (n > (cap - pos)) { n = (cap - pos); }
    (void)memcpy(&out[pos], leaf1, n);
    pos += n;

    if (leaf2 != NULL) {
        if (pos < cap) { out[pos++] = '/'; }
        n = tsm_bounded_len(leaf2, TSM_MAX_TOPIC_LEN);
        if (n > (cap - pos)) { n = (cap - pos); }
        (void)memcpy(&out[pos], leaf2, n);
        pos += n;
    }

    out[pos] = '\0';
}

static void publish_kv(const char *branch, const char *name, const char *val)
{
    char topic[TSM_MAX_TOPIC_LEN];

    if ((branch == NULL) || (name == NULL) || (val == NULL)) {
        return;
    }

    make_topic(topic, sizeof(topic), branch, name);
    (void)mqtt_publish(topic, val, strlen(val));
    if ((s_udp_mcast_enabled != 0) && (s_udp_seq_counter != NULL)) {
        (void)api2_send_tlm1((*s_udp_seq_counter)++, s_udp_camera_id, topic, val);
    }
}

void tracker_set_udp_mcast(bool enabled, uint8_t camera_id, uint32_t *seq_counter)
{
    s_udp_mcast_enabled = (enabled != false) ? 1 : 0;
    s_udp_camera_id = camera_id;
    s_udp_seq_counter = seq_counter;
}

static void publish_leaf_u(const char *path, unsigned v)
{
    char buf[16];
    (void)snprintf(buf, sizeof(buf), "%u", v);
    publish_kv("STATE", path, buf);
}

static void force_publish_all(void)
{
    publish_leaf_u("COORD/CAM_ID", s_state.coord_cam_id);
    publish_leaf_u("COORD/X", s_state.coord_x);
    publish_leaf_u("COORD/Y", s_state.coord_y);
    publish_leaf_u("COORD/MODE", s_state.coord_mode);

    publish_leaf_u("BOX/CAM_ID", s_state.box_cam_id);
    publish_leaf_u("BOX/WIDTH", s_state.box_w);
    publish_leaf_u("BOX/HEIGHT", s_state.box_h);

    publish_leaf_u("TRACK_MODE/CAM_ID", s_state.tmode_cam_id);
    publish_leaf_u("TRACK_MODE/MODE", s_state.tmode);

    publish_leaf_u("TARGET_LOCK/CAM_ID", s_state.lock_cam_id);
    publish_leaf_u("TARGET_LOCK/MODE", s_state.lock_mode);

    publish_leaf_u("STABILIZATION/CAM_ID", s_state.stab_cam_id);
    publish_leaf_u("STABILIZATION/MODE", s_state.stab_mode);

    publish_leaf_u("SEL_ZONE/CAM_ID", s_state.szone_cam_id);
    publish_leaf_u("SEL_ZONE/ZONE", s_state.szone_zone);

    publish_leaf_u("AUTO_DETECT/CAM_ID", s_state.ad_cam_id);
    publish_leaf_u("AUTO_DETECT/MODE", s_state.ad_mode);
    publish_leaf_u("AUTO_DETECT/SENS", s_state.ad_sens);

    publish_leaf_u("ROI/CAM_ID", s_state.roi_cam_id);
    publish_leaf_u("ROI/MODE", s_state.roi_mode);

    publish_leaf_u("ENC/BR_centi", s_state.enc_br_centi);

    publish_leaf_u("MAX_FAIL/CAM_ID", s_state.mfail_cam_id);
    publish_leaf_u("MAX_FAIL/COUNT_CODE", s_state.mfail_code);

    publish_leaf_u("TYPE/CAM_ID", s_state.type_cam_id);
    publish_leaf_u("TYPE/CODE", s_state.type_code);

    publish_leaf_u("SEARCH_AREA/CAM_ID", s_state.sarea_cam_id);
    publish_leaf_u("SEARCH_AREA/PERCENT", s_state.sarea_percent);

    publish_leaf_u("CONFIDENCE/CAM_ID", s_state.conf_cam_id);
    publish_leaf_u("CONFIDENCE/TENTHS", s_state.conf_tenths);

    s_last_published = s_state;
}

static int parse_u8_dec(const char *p, uint8_t *out)
{
    unsigned long acc = 0UL;
    bool any = false;

    if ((p == NULL) || (out == NULL)) {
        return -1;
    }

    for (size_t i = 0U; p[i] != '\0'; i++) {
        const char c = p[i];
        if ((c >= '0') && (c <= '9')) {
            any = true;
            acc = (acc * 10UL) + (unsigned long)(c - '0');
            if (acc > 255UL) {
                return -1;
            }
        } else if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n')) {
        } else {
            return -1;
        }
    }

    if (!any) {
        return -1;
    }

    *out = (uint8_t)acc;
    return 0;
}

static int parse_u16_dec(const char *p, uint16_t *out)
{
    unsigned long acc = 0UL;
    bool any = false;

    if ((p == NULL) || (out == NULL)) {
        return -1;
    }

    for (size_t i = 0U; p[i] != '\0'; i++) {
        const char c = p[i];
        if ((c >= '0') && (c <= '9')) {
            any = true;
            acc = (acc * 10UL) + (unsigned long)(c - '0');
            if (acc > 65535UL) {
                return -1;
            }
        } else if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n')) {
        } else {
            return -1;
        }
    }

    if (!any) {
        return -1;
    }

    *out = (uint16_t)acc;
    return 0;
}

static void on_set_word(const char *word, const char *payload)
{
    int io_rc = -1;
    tracker_state_t t = s_state;
    uint8_t u8 = 0U;
    uint16_t u16 = 0U;

    if ((word == NULL) || (payload == NULL)) {
        return;
    }

    if ((strcmp(word, "COORD/CAM_ID") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.coord_cam_id = u8; io_rc = owl_tracker_set_coord(t.coord_cam_id, t.coord_x, t.coord_y, t.coord_mode);
    } else if ((strcmp(word, "COORD/X") == 0) && (parse_u16_dec(payload, &u16) == 0)) {
        t.coord_x = u16; io_rc = owl_tracker_set_coord(t.coord_cam_id, t.coord_x, t.coord_y, t.coord_mode);
    } else if ((strcmp(word, "COORD/Y") == 0) && (parse_u16_dec(payload, &u16) == 0)) {
        t.coord_y = u16; io_rc = owl_tracker_set_coord(t.coord_cam_id, t.coord_x, t.coord_y, t.coord_mode);
    } else if ((strcmp(word, "COORD/MODE") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.coord_mode = u8; io_rc = owl_tracker_set_coord(t.coord_cam_id, t.coord_x, t.coord_y, t.coord_mode);
    } else if ((strcmp(word, "BOX/CAM_ID") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.box_cam_id = u8; io_rc = owl_tracker_set_box(t.box_cam_id, t.box_w, t.box_h);
    } else if ((strcmp(word, "BOX/WIDTH") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.box_w = u8; io_rc = owl_tracker_set_box(t.box_cam_id, t.box_w, t.box_h);
    } else if ((strcmp(word, "BOX/HEIGHT") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.box_h = u8; io_rc = owl_tracker_set_box(t.box_cam_id, t.box_w, t.box_h);
    } else if ((strcmp(word, "TRACK_MODE/CAM_ID") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.tmode_cam_id = u8; io_rc = owl_tracker_set_mode(t.tmode_cam_id, t.tmode);
    } else if ((strcmp(word, "TRACK_MODE/MODE") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.tmode = u8; io_rc = owl_tracker_set_mode(t.tmode_cam_id, t.tmode);
    } else if ((strcmp(word, "TARGET_LOCK/CAM_ID") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.lock_cam_id = u8; io_rc = owl_tracker_set_lock(t.lock_cam_id, t.lock_mode);
    } else if ((strcmp(word, "TARGET_LOCK/MODE") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.lock_mode = u8; io_rc = owl_tracker_set_lock(t.lock_cam_id, t.lock_mode);
    } else if ((strcmp(word, "STABILIZATION/CAM_ID") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.stab_cam_id = u8; io_rc = owl_tracker_set_stab(t.stab_cam_id, t.stab_mode);
    } else if ((strcmp(word, "STABILIZATION/MODE") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.stab_mode = u8; io_rc = owl_tracker_set_stab(t.stab_cam_id, t.stab_mode);
    } else if ((strcmp(word, "SEL_ZONE/CAM_ID") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.szone_cam_id = u8; io_rc = owl_tracker_set_szone(t.szone_cam_id, t.szone_zone);
    } else if ((strcmp(word, "SEL_ZONE/ZONE") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.szone_zone = u8; io_rc = owl_tracker_set_szone(t.szone_cam_id, t.szone_zone);
    } else if ((strcmp(word, "AUTO_DETECT/CAM_ID") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.ad_cam_id = u8; io_rc = owl_tracker_set_auto(t.ad_cam_id, t.ad_mode, t.ad_sens);
    } else if ((strcmp(word, "AUTO_DETECT/MODE") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.ad_mode = u8; io_rc = owl_tracker_set_auto(t.ad_cam_id, t.ad_mode, t.ad_sens);
    } else if ((strcmp(word, "AUTO_DETECT/SENS") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.ad_sens = u8; io_rc = owl_tracker_set_auto(t.ad_cam_id, t.ad_mode, t.ad_sens);
    } else if ((strcmp(word, "ROI/CAM_ID") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.roi_cam_id = u8; io_rc = owl_tracker_set_roi(t.roi_cam_id, t.roi_mode);
    } else if ((strcmp(word, "ROI/MODE") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.roi_mode = u8; io_rc = owl_tracker_set_roi(t.roi_cam_id, t.roi_mode);
    } else if ((strcmp(word, "ENC/BR_centi") == 0) && (parse_u16_dec(payload, &u16) == 0)) {
        io_rc = owl_tracker_set_bitrate_centi(u16);
    } else if ((strcmp(word, "MAX_FAIL/CAM_ID") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.mfail_cam_id = u8; io_rc = owl_tracker_set_maxfail(t.mfail_cam_id, t.mfail_code);
    } else if ((strcmp(word, "MAX_FAIL/COUNT_CODE") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.mfail_code = u8; io_rc = owl_tracker_set_maxfail(t.mfail_cam_id, t.mfail_code);
    } else if ((strcmp(word, "TYPE/CAM_ID") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.type_cam_id = u8; io_rc = owl_tracker_set_type(t.type_cam_id, t.type_code);
    } else if ((strcmp(word, "TYPE/CODE") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.type_code = u8; io_rc = owl_tracker_set_type(t.type_cam_id, t.type_code);
    } else if ((strcmp(word, "SEARCH_AREA/CAM_ID") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.sarea_cam_id = u8; io_rc = owl_tracker_set_search_area(t.sarea_cam_id, t.sarea_percent);
    } else if ((strcmp(word, "SEARCH_AREA/PERCENT") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.sarea_percent = u8; io_rc = owl_tracker_set_search_area(t.sarea_cam_id, t.sarea_percent);
    } else if ((strcmp(word, "CONFIDENCE/CAM_ID") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.conf_cam_id = u8; io_rc = owl_tracker_set_confidence(t.conf_cam_id, t.conf_tenths);
    } else if ((strcmp(word, "CONFIDENCE/TENTHS") == 0) && (parse_u8_dec(payload, &u8) == 0)) {
        t.conf_tenths = u8; io_rc = owl_tracker_set_confidence(t.conf_cam_id, t.conf_tenths);
    } else {
        return;
    }

    if (io_rc != 0) {
        log_warning("TRACKER set failed word=%s rc=%d", word, io_rc);
    }

    if (owl_tracker_read_all(&s_state) == 0) {
        force_publish_all();
    }
}

static void tsm_rx_handler(const char *topic, const char *payload, size_t len)
{
    char set_prefix[TSM_MAX_TOPIC_LEN];
    size_t prefix_len = 0U;

    if ((topic == NULL) || (payload == NULL) || (len == 0U)) {
        return;
    }

    make_topic(set_prefix, sizeof(set_prefix), "SET", NULL);
    prefix_len = strlen(set_prefix);

    if (strncmp(topic, set_prefix, prefix_len) == 0) {
        const char *leaf = topic + prefix_len;
        if (*leaf == '/') {
            leaf++;
        }
        if (*leaf != '\0') {
            on_set_word(leaf, payload);
        }
    }
}

tsm_status_t tracker_sm_init(const char *root_topic, uint32_t refresh_period_ms)
{
    char sub_topic[TSM_MAX_TOPIC_LEN];

    if ((root_topic != NULL) && (root_topic[0] != '\0')) {
        (void)snprintf(s_root, sizeof(s_root), "%s", root_topic);
        s_root[sizeof(s_root) - 1U] = '\0';
    }

    s_refresh_ms = (refresh_period_ms != 0U) ? refresh_period_ms : 1000U;
    s_last_refresh = 0U;

    mqtt_set_rx_handler(tsm_rx_handler);
    make_topic(sub_topic, sizeof(sub_topic), "SET", "#");
    (void)mqtt_subscribe(sub_topic);

    if (owl_tracker_read_all(&s_state) != 0) {
        (void)memset(&s_state, 0, sizeof(s_state));
        (void)memset(&s_last_published, 0, sizeof(s_last_published));
        force_publish_all();
        return TSM_ERR_IO;
    }

    (void)memset(&s_last_published, 0, sizeof(s_last_published));
    force_publish_all();
    return TSM_OK;
}

tsm_status_t tracker_sm_poll(uint32_t now_ms)
{
    if ((now_ms - s_last_refresh) >= s_refresh_ms) {
        if (owl_tracker_read_all(&s_state) == 0) {
            /* Periodic authoritative push for continuous multicast/MQTT state output. */
            force_publish_all();
        }
        s_last_refresh = now_ms;
    }

    return TSM_OK;
}

tsm_status_t tracker_sm_get_state(tracker_state_t *out)
{
    if (out == NULL) {
        return TSM_ERR_BAD_ARG;
    }
    *out = s_state;
    return TSM_OK;
}
