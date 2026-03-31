#include "api2_mcast_win.h"

/*
 * If your system installs cJSON as <cjson/cJSON.h>, change this include line
 * to:
 *   #include <cjson/cJSON.h>
 */
#include "cJSON.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#define API2_SOCK(fd_)              (fd_)
#define API2_INVALID_SOCK           (-1)
#define API2_RX_PACKET_MAX          API2_MAX_JSON

typedef struct
{
    int tx_sock;
    int rx_sock;
    int active;
    struct sockaddr_in tx_addr;
} api2_channel_state_t;

typedef struct
{
    int initialized;
    char iface_ip[32];
    uint8_t ttl;
    uint8_t loopback;
    uint8_t camera_id;

    api2_channel_state_t ch[API2_CH_COUNT];
} api2_state_t;

static api2_state_t g_api2;

/* -------------------------------------------------------------------------- */
/* Local helpers                                                               */
/* -------------------------------------------------------------------------- */

static int api2_net_init(void)
{
    return 0;
}

static int api2_sock_close(int fd)
{
    return close(fd);
}

static int api2_set_nonblock(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int api2_is_wouldblock(void)
{
    return ((errno == EWOULDBLOCK) || (errno == EAGAIN)) ? 1 : 0;
}

static int api2_parse_ipv4(const char* ip, struct in_addr* out)
{
    if ((ip == NULL) || (out == NULL)) {
        return -1;
    }

    return (inet_pton(AF_INET, ip, out) == 1) ? 0 : -1;
}

static const api2_channel_cfg_t* api2_get_cfg_ch(
        const api2_cfg_t* cfg,
        api2_channel_t ch)
{
    switch (ch) {
        case API2_CH_CAMERA:     return &cfg->camera;
        case API2_CH_JOYSTICK:   return &cfg->joystick;
        case API2_CH_BUTTON_LED: return &cfg->button_led;
        default:                 return NULL;
    }
}

static const char* api2_channel_name(api2_channel_t ch)
{
    switch (ch) {
        case API2_CH_CAMERA:     return "camera";
        case API2_CH_JOYSTICK:   return "joystick";
        case API2_CH_BUTTON_LED: return "button_led";
        default:                 return "unknown";
    }
}

static int api2_open_tx_socket(
        api2_channel_t ch,
        const api2_cfg_t* cfg)
{
    int fd;
    struct in_addr iface_addr;
    unsigned char ttl;
    unsigned char loop;
    const api2_channel_cfg_t* ccfg;

    ccfg = api2_get_cfg_ch(cfg, ch);
    if (ccfg == NULL) {
        return -1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    ttl = cfg->ttl;
    loop = (unsigned char)((cfg->loopback != 0u) ? 1u : 0u);

    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) != 0) {
        (void)api2_sock_close(fd);
        return -1;
    }

    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) != 0) {
        (void)api2_sock_close(fd);
        return -1;
    }

    if ((cfg->iface_ip[0] != '\0') && (strcmp(cfg->iface_ip, "0.0.0.0") != 0)) {
        if (api2_parse_ipv4(cfg->iface_ip, &iface_addr) != 0) {
            (void)api2_sock_close(fd);
            return -1;
        }

        if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF,
                       &iface_addr, sizeof(iface_addr)) != 0) {
            (void)api2_sock_close(fd);
            return -1;
        }
    }

    memset(&g_api2.ch[ch].tx_addr, 0, sizeof(g_api2.ch[ch].tx_addr));
    g_api2.ch[ch].tx_addr.sin_family = AF_INET;
    g_api2.ch[ch].tx_addr.sin_port = htons(ccfg->port);

    if (api2_parse_ipv4(ccfg->group_ip, &g_api2.ch[ch].tx_addr.sin_addr) != 0) {
        (void)api2_sock_close(fd);
        return -1;
    }

    g_api2.ch[ch].tx_sock = fd;
    return 0;
}

static int api2_open_rx_socket(
        api2_channel_t ch,
        const api2_cfg_t* cfg)
{
    int fd;
    int reuse = 1;
    struct sockaddr_in bind_addr;
    struct ip_mreq mreq;
    const api2_channel_cfg_t* ccfg;

    ccfg = api2_get_cfg_ch(cfg, ch);
    if (ccfg == NULL) {
        return -1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        (void)api2_sock_close(fd);
        return -1;
    }

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(ccfg->port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) != 0) {
        (void)api2_sock_close(fd);
        return -1;
    }

    memset(&mreq, 0, sizeof(mreq));

    if (api2_parse_ipv4(ccfg->group_ip, &mreq.imr_multiaddr) != 0) {
        (void)api2_sock_close(fd);
        return -1;
    }

    if ((cfg->iface_ip[0] == '\0') || (strcmp(cfg->iface_ip, "0.0.0.0") == 0)) {
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    } else {
        if (api2_parse_ipv4(cfg->iface_ip, &mreq.imr_interface) != 0) {
            (void)api2_sock_close(fd);
            return -1;
        }
    }

    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &mreq, sizeof(mreq)) != 0) {
        (void)api2_sock_close(fd);
        return -1;
    }

    if (api2_set_nonblock(fd) != 0) {
        (void)api2_sock_close(fd);
        return -1;
    }

    g_api2.ch[ch].rx_sock = fd;
    return 0;
}

static int api2_recv_one_packet(
        api2_channel_t ch,
        char* buf,
        size_t buf_sz)
{
    int rcv;

    if ((buf == NULL) || (buf_sz < 2u)) {
        return -1;
    }

    if ((ch < 0) || (ch >= API2_CH_COUNT)) {
        return -1;
    }

    if (g_api2.ch[ch].active == 0) {
        return -1;
    }

    rcv = (int)recvfrom(API2_SOCK(g_api2.ch[ch].rx_sock),
                        buf,
                        (int)(buf_sz - 1u),
                        0,
                        NULL,
                        NULL);

    if (rcv < 0) {
        if (api2_is_wouldblock() != 0) {
            return 0;
        }
        return -1;
    }

    buf[rcv] = '\0';
    return rcv;
}

static int api2_json_get_u8(
        const cJSON* obj,
        const char* name,
        uint8_t* out)
{
    const cJSON* item;

    if ((obj == NULL) || (name == NULL) || (out == NULL)) {
        return -1;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON*)obj, name);
    if (!cJSON_IsNumber(item)) {
        return -1;
    }

    *out = (uint8_t)(item->valuedouble);
    return 0;
}

static int api2_json_get_u16(
        const cJSON* obj,
        const char* name,
        uint16_t* out)
{
    const cJSON* item;

    if ((obj == NULL) || (name == NULL) || (out == NULL)) {
        return -1;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON*)obj, name);
    if (!cJSON_IsNumber(item)) {
        return -1;
    }

    *out = (uint16_t)(item->valuedouble);
    return 0;
}

static int api2_json_get_u32(
        const cJSON* obj,
        const char* name,
        uint32_t* out)
{
    const cJSON* item;

    if ((obj == NULL) || (name == NULL) || (out == NULL)) {
        return -1;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON*)obj, name);
    if (!cJSON_IsNumber(item)) {
        return -1;
    }

    *out = (uint32_t)(item->valuedouble);
    return 0;
}

static int api2_json_get_f32(
        const cJSON* obj,
        const char* name,
        float* out)
{
    const cJSON* item;

    if ((obj == NULL) || (name == NULL) || (out == NULL)) {
        return -1;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON*)obj, name);
    if (!cJSON_IsNumber(item)) {
        return -1;
    }

    *out = (float)(item->valuedouble);
    return 0;
}

static int api2_parse_cam1_json(
        const char* json_str,
        uint32_t* seq_out,
        uint8_t* camera_id_out,
        api2_cam1_status_t* st_out)
{
    cJSON* root = NULL;
    cJSON* type = NULL;
    int rc = -1;

    if ((json_str == NULL) ||
        (seq_out == NULL) ||
        (camera_id_out == NULL) ||
        (st_out == NULL)) {
        return -1;
    }

    root = cJSON_Parse(json_str);
    if (root == NULL) {
        return -1;
    }

    type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if ((!cJSON_IsString(type)) || (type->valuestring == NULL)) {
        cJSON_Delete(root);
        return 0;
    }

    if (strcmp(type->valuestring, "camera_telem") != 0) {
        cJSON_Delete(root);
        return 0;
    }

    memset(st_out, 0, sizeof(*st_out));

    if (api2_json_get_u32(root, "seq", seq_out) != 0) {
        cJSON_Delete(root);
        return -1;
    }

    if (api2_json_get_u8(root, "camera_id", camera_id_out) != 0) {
        cJSON_Delete(root);
        return -1;
    }

    if (api2_json_get_u8(root, "mode", &st_out->mode) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u8(root, "tracker_mode", &st_out->tracker_mode) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u8(root, "thermal_nuc_status", &st_out->thermal_nuc_status) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u16(root, "lrf_range", &st_out->lrf_range) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u16(root, "day_ret_x", &st_out->day_ret_x) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u16(root, "day_ret_y", &st_out->day_ret_y) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u16(root, "thermal_ret_x", &st_out->thermal_ret_x) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u16(root, "thermal_ret_y", &st_out->thermal_ret_y) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u16(root, "tracker_ret_x", &st_out->tracker_ret_x) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u16(root, "tracker_ret_y", &st_out->tracker_ret_y) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u16(root, "tracker_bb_w", &st_out->tracker_bb_w) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u16(root, "tracker_bb_h", &st_out->tracker_bb_h) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u16(root, "sys_temp", &st_out->sys_temp) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_f32(root, "alt_m", &st_out->alt_m) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u8(root, "sat", &st_out->sat) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u8(root, "gps_qos", &st_out->gps_qos) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u8(root, "pwr_day_on", &st_out->pwr_day_on) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u8(root, "pwr_thermal_on", &st_out->pwr_thermal_on) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u8(root, "pwr_plcb_on", &st_out->pwr_plcb_on) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u8(root, "pwr_lrf_on", &st_out->pwr_lrf_on) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    if (api2_json_get_u8(root, "pwr_overall", &st_out->pwr_overall) != 0) {
        cJSON_Delete(root);
        return -1;
    }

    rc = 1;
    cJSON_Delete(root);
    return rc;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

void api2_cfg_set_defaults(api2_cfg_t* cfg)
{
    if (cfg == NULL) {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));

    (void)snprintf(cfg->iface_ip, sizeof(cfg->iface_ip), "0.0.0.0");
    cfg->ttl = 1u;
    cfg->loopback = 1u;

    cfg->camera.enabled = 1u;
    (void)snprintf(cfg->camera.group_ip, sizeof(cfg->camera.group_ip), "239.255.10.10");
    cfg->camera.port = 5000u;

    cfg->joystick.enabled = 1u;
    (void)snprintf(cfg->joystick.group_ip, sizeof(cfg->joystick.group_ip), "239.255.2.2");
    cfg->joystick.port = 50100u;

    cfg->button_led.enabled = 1u;
    (void)snprintf(cfg->button_led.group_ip, sizeof(cfg->button_led.group_ip), "239.255.2.3");
    cfg->button_led.port = 50100u;

    cfg->camera_id = 1u;
}

int api2_init(const api2_cfg_t* cfg)
{
    int ch;

    if (cfg == NULL) {
        return -1;
    }

    if (api2_net_init() != 0) {
        return -1;
    }

    memset(&g_api2, 0, sizeof(g_api2));
    for (ch = 0; ch < API2_CH_COUNT; ++ch) {
        g_api2.ch[ch].tx_sock = API2_INVALID_SOCK;
        g_api2.ch[ch].rx_sock = API2_INVALID_SOCK;
    }

    (void)snprintf(g_api2.iface_ip, sizeof(g_api2.iface_ip), "%s", cfg->iface_ip);
    g_api2.ttl = cfg->ttl;
    g_api2.loopback = cfg->loopback;
    g_api2.camera_id = cfg->camera_id;

    for (ch = 0; ch < API2_CH_COUNT; ++ch) {
        const api2_channel_cfg_t* ccfg = api2_get_cfg_ch(cfg, (api2_channel_t)ch);
        if ((ccfg == NULL) || (ccfg->enabled == 0u)) {
            continue;
        }

        if (api2_open_tx_socket((api2_channel_t)ch, cfg) != 0) {
            api2_shutdown();
            return -2;
        }

        if (api2_open_rx_socket((api2_channel_t)ch, cfg) != 0) {
            api2_shutdown();
            return -3;
        }

        g_api2.ch[ch].active = 1;
    }

    g_api2.initialized = 1;
    return 0;
}

void api2_shutdown(void)
{
    int ch;

    for (ch = 0; ch < API2_CH_COUNT; ++ch) {
        if (g_api2.ch[ch].tx_sock != API2_INVALID_SOCK) {
            (void)api2_sock_close(g_api2.ch[ch].tx_sock);
            g_api2.ch[ch].tx_sock = API2_INVALID_SOCK;
        }

        if (g_api2.ch[ch].rx_sock != API2_INVALID_SOCK) {
            (void)api2_sock_close(g_api2.ch[ch].rx_sock);
            g_api2.ch[ch].rx_sock = API2_INVALID_SOCK;
        }

        g_api2.ch[ch].active = 0;
    }

    memset(&g_api2, 0, sizeof(g_api2));
}

int api2_send_json_string(api2_channel_t ch, const char* json_str)
{
    size_t len;
    int sent;

    if ((!g_api2.initialized) || (json_str == NULL)) {
        return -1;
    }

    if ((ch < 0) || (ch >= API2_CH_COUNT)) {
        return -1;
    }

    if (g_api2.ch[ch].active == 0) {
        return -1;
    }

    len = strlen(json_str);
    if (len == 0u) {
        return -1;
    }

    sent = (int)sendto(API2_SOCK(g_api2.ch[ch].tx_sock),
                       json_str,
                       len,
                       0,
                       (const struct sockaddr*)&g_api2.ch[ch].tx_addr,
                       (socklen_t)sizeof(g_api2.ch[ch].tx_addr));

    return (sent >= 0) ? sent : -1;
}

int api2_recv_json_string(api2_channel_t ch, char* buf, size_t buf_sz)
{
    if (!g_api2.initialized) {
        return -1;
    }

    return api2_recv_one_packet(ch, buf, buf_sz);
}

int api2_send_cam1(
        uint32_t seq,
        uint8_t camera_id,
        const api2_cam1_status_t* st)
{
    cJSON* root = NULL;
    char* json = NULL;
    int rc = -1;

    if (st == NULL) {
        return -1;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return -1;
    }

    cJSON_AddStringToObject(root, "type", "camera_telem");
    cJSON_AddStringToObject(root, "channel", api2_channel_name(API2_CH_CAMERA));
    cJSON_AddNumberToObject(root, "seq", (double)seq);
    cJSON_AddNumberToObject(root, "camera_id", (double)camera_id);

    cJSON_AddNumberToObject(root, "mode", (double)st->mode);
    cJSON_AddNumberToObject(root, "tracker_mode", (double)st->tracker_mode);
    cJSON_AddNumberToObject(root, "thermal_nuc_status", (double)st->thermal_nuc_status);
    cJSON_AddNumberToObject(root, "lrf_range", (double)st->lrf_range);
    cJSON_AddNumberToObject(root, "day_ret_x", (double)st->day_ret_x);
    cJSON_AddNumberToObject(root, "day_ret_y", (double)st->day_ret_y);
    cJSON_AddNumberToObject(root, "thermal_ret_x", (double)st->thermal_ret_x);
    cJSON_AddNumberToObject(root, "thermal_ret_y", (double)st->thermal_ret_y);
    cJSON_AddNumberToObject(root, "tracker_ret_x", (double)st->tracker_ret_x);
    cJSON_AddNumberToObject(root, "tracker_ret_y", (double)st->tracker_ret_y);
    cJSON_AddNumberToObject(root, "tracker_bb_w", (double)st->tracker_bb_w);
    cJSON_AddNumberToObject(root, "tracker_bb_h", (double)st->tracker_bb_h);
    cJSON_AddNumberToObject(root, "sys_temp", (double)st->sys_temp);
    cJSON_AddNumberToObject(root, "alt_m", (double)st->alt_m);
    cJSON_AddNumberToObject(root, "sat", (double)st->sat);
    cJSON_AddNumberToObject(root, "gps_qos", (double)st->gps_qos);
    cJSON_AddNumberToObject(root, "pwr_day_on", (double)st->pwr_day_on);
    cJSON_AddNumberToObject(root, "pwr_thermal_on", (double)st->pwr_thermal_on);
    cJSON_AddNumberToObject(root, "pwr_plcb_on", (double)st->pwr_plcb_on);
    cJSON_AddNumberToObject(root, "pwr_lrf_on", (double)st->pwr_lrf_on);
    cJSON_AddNumberToObject(root, "pwr_overall", (double)st->pwr_overall);

    json = cJSON_PrintUnformatted(root);
    if (json != NULL) {
        rc = api2_send_json_string(API2_CH_CAMERA, json);
        cJSON_free(json);
    }

    cJSON_Delete(root);
    return rc;
}

int api2_poll_cam1(
        uint32_t* seq_out,
        uint8_t* camera_id_out,
        api2_cam1_status_t* st_out)
{
    char buf[API2_RX_PACKET_MAX];
    int rcv;

    if ((!g_api2.initialized) ||
        (seq_out == NULL) ||
        (camera_id_out == NULL) ||
        (st_out == NULL)) {
        return -1;
    }

    rcv = api2_recv_one_packet(API2_CH_CAMERA, buf, sizeof(buf));
    if (rcv <= 0) {
        return rcv;
    }

    return api2_parse_cam1_json(buf, seq_out, camera_id_out, st_out);
}

int api2_poll_joystick_json(char* buf, size_t buf_sz)
{
    if (!g_api2.initialized) {
        return -1;
    }

    return api2_recv_one_packet(API2_CH_JOYSTICK, buf, buf_sz);
}

int api2_send_button_led_json(
        uint32_t seq,
        const char* name,
        uint8_t value,
        const char* direction,
        const char* source)
{
    cJSON* root = NULL;
    char* json = NULL;
    int rc = -1;

    if (name == NULL) {
        return -1;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return -1;
    }

    cJSON_AddStringToObject(root, "type", "button_led");
    cJSON_AddStringToObject(root, "channel", api2_channel_name(API2_CH_BUTTON_LED));
    cJSON_AddNumberToObject(root, "seq", (double)seq);
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddNumberToObject(root, "value", (double)value);
    cJSON_AddStringToObject(root, "direction", (direction != NULL) ? direction : "tx");
    cJSON_AddStringToObject(root, "source", (source != NULL) ? source : "wcs");

    json = cJSON_PrintUnformatted(root);
    if (json != NULL) {
        rc = api2_send_json_string(API2_CH_BUTTON_LED, json);
        cJSON_free(json);
    }

    cJSON_Delete(root);
    return rc;
}

int api2_poll_button_led_json(char* buf, size_t buf_sz)
{
    if (!g_api2.initialized) {
        return -1;
    }

    return api2_recv_one_packet(API2_CH_BUTTON_LED, buf, buf_sz);
}