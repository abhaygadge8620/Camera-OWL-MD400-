/*
 * @file camera_iface.c
 * @brief OWL-MD400 Ethernet command interface implementation (TCP 8088).
 */

#include "camera_iface.h"
#include "camera_mqtt.h"
#include "camera_params.h"
#include "api2_mcast_win.h"
#include "mqtt.h"
#include "tracker.h"
#include "../platform_compat.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define OWL_SOCK(fd_) (fd_)
extern char **environ;
static int owl_close_socket(int fd)
{
    return close(fd);
}
static int owl_net_init(void)
{
    return 0;
}
static int owl_socket_wouldblock(void)
{
    return ((errno == EWOULDBLOCK) || (errno == EAGAIN) || (errno == EINPROGRESS)) ? 1 : 0;
}

/* ---- Local constants ---- */
#define OWL_HDR_BYTE          (0xCCu)
#define OWL_RW_READ_ASCII     (0x52u) /* 'R' */
#define OWL_RW_WRITE_ASCII    (0x57u) /* 'W' */

#define OWL_TX_MAX_DATA       (1024u)
#define OWL_TX_MAX_PACKET     (1u + 1u + 1u + 2u + 2u + OWL_TX_MAX_DATA + 2u)
#define MIN_RESP_LEN          (1u + 1u + 1u + 2u + 2u)
#define OWL_LAST_FAIL_MAX     (512u)

static owl_cam_t *g_tracker_cam = NULL;
static int g_udp_mcast_enabled = 0;
static uint8_t g_udp_camera_id = 1u;
static uint32_t *g_udp_seq_counter = NULL;
static uint8_t g_last_fail_frame[OWL_LAST_FAIL_MAX];
static size_t g_last_fail_len = 0u;
static uint8_t g_last_fail_status = 0xFFu;
static owl_iface_t g_last_fail_iface = OWL_IFACE_CONTROL;
static uint16_t g_last_fail_addr = 0u;
static int g_last_fail_valid = 0;

static int g_vlc_started = 0;
static pid_t g_vlc_pid = -1;
#define VLC_EXE_PATH "vlc"
static void owl_kill_processes_by_name(const char *exe_name)
{
    (void)exe_name;
}

static int owl_resolve_vlc_path(const char *configured_path, char *resolved_path, size_t resolved_path_sz)
{
    const char *path_env;
    char path_copy[2048];
    char *saveptr = NULL;
    char *dir;

    if ((resolved_path == NULL) || (resolved_path_sz == 0u)) {
        return -1;
    }

    resolved_path[0] = '\0';

    if ((configured_path != NULL) && (configured_path[0] != '\0') &&
        (access(configured_path, X_OK) == 0)) {
        const int n = snprintf(resolved_path, resolved_path_sz, "%s", configured_path);
        return ((n > 0) && ((size_t)n < resolved_path_sz)) ? 0 : -1;
    }

    path_env = getenv("PATH");
    if (path_env == NULL) {
        path_env = "";
    }

    (void)snprintf(path_copy, sizeof(path_copy), "%s", path_env);
    dir = strtok_r(path_copy, ":", &saveptr);
    while (dir != NULL) {
        char candidate[PATH_MAX];
        const int n = snprintf(candidate, sizeof(candidate), "%s/%s", dir, "vlc");
        if ((n > 0) && ((size_t)n < sizeof(candidate)) && (access(candidate, X_OK) == 0)) {
            const int out_n = snprintf(resolved_path, resolved_path_sz, "%s", candidate);
            return ((out_n > 0) && ((size_t)out_n < resolved_path_sz)) ? 0 : -1;
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }

    if (access(VLC_EXE_PATH, X_OK) == 0) {
        const int n = snprintf(resolved_path, resolved_path_sz, "%s", VLC_EXE_PATH);
        return ((n > 0) && ((size_t)n < resolved_path_sz)) ? 0 : -1;
    }

    return -1;
}








static void capture_failed_frame(const uint8_t *rx, size_t rx_len)
{
    if ((rx == NULL) || (rx_len == 0u)) {
        return;
    }

    const size_t n = (rx_len <= OWL_LAST_FAIL_MAX) ? rx_len : OWL_LAST_FAIL_MAX;
    (void)memcpy(g_last_fail_frame, rx, n);
    g_last_fail_len = n;

    if (rx_len > 1u) {
        g_last_fail_status = rx[1];
    } else {
        g_last_fail_status = 0xFFu;
    }

    if (rx_len > 4u) {
        g_last_fail_iface = (owl_iface_t)rx[2];
        g_last_fail_addr = (uint16_t)(((uint16_t)rx[3] << 8) | (uint16_t)rx[4]);
    } else {
        g_last_fail_iface = OWL_IFACE_CONTROL;
        g_last_fail_addr = 0u;
    }

    g_last_fail_valid = 1;
}

static void pub_kv(const char *root, const char *leaf, const char *val)
{
    if ((root == NULL) || (leaf == NULL) || (val == NULL)) {
        return;
    }

    char topic[256];
    int n = snprintf(topic, sizeof(topic), "%s/TELEM/%s", root, leaf);
    if ((n < 0) || ((size_t)n >= sizeof(topic))) {
        return;
    }

    (void)mqtt_publish(topic, (const void *)val, strlen(val));

    if ((g_udp_mcast_enabled != 0) && (g_udp_seq_counter != NULL)) {
        (void)api2_send_tlm1((*g_udp_seq_counter)++, g_udp_camera_id, topic, val);
    }
}

/* --- helpers: endian-safe readers --- */
static uint16_t rd_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static float rd_f32_le(const uint8_t *p)
{
    uint32_t u = ((uint32_t)p[0])
               | ((uint32_t)p[1] << 8)
               | ((uint32_t)p[2] << 16)
               | ((uint32_t)p[3] << 24);
    float f = 0.0f;
    (void)memcpy(&f, &u, sizeof(f));
    return f;
}

static double rd_f64_le(const uint8_t *p)
{
    uint64_t u = ((uint64_t)p[0])
               | ((uint64_t)p[1] << 8)
               | ((uint64_t)p[2] << 16)
               | ((uint64_t)p[3] << 24)
               | ((uint64_t)p[4] << 32)
               | ((uint64_t)p[5] << 40)
               | ((uint64_t)p[6] << 48)
               | ((uint64_t)p[7] << 56);
    double d = 0.0;
    (void)memcpy(&d, &u, sizeof(d));
    return d;
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

    /* Tracker register reads can transiently time out right after camera startup. */
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

/* ---- Helpers ---- */
static uint16_t crc16_xmodem(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0x0000u;
    size_t i = 0U;

    while (i < len) {
        crc ^= (uint16_t)((uint16_t)buf[i] << 8);
        for (int b = 0; b < 8; ++b) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc <<= 1;
            }
        }
        ++i;
    }

    return crc;
}

static int tcp_send_all(int fd, const uint8_t *buf, size_t len)
{
    size_t sent = 0U;

    while (sent < len) {
        const size_t rem = len - sent;
        const int chunk = (rem > (size_t)INT_MAX) ? INT_MAX : (int)rem;
        const int n = (int)send(OWL_SOCK(fd), (const char *)(buf + sent), chunk, 0);
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }

    return 0;
}

static int tcp_wait_readable(int fd, int timeout_ms)
{
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(OWL_SOCK(fd), &rfds);

    if (timeout_ms < 0) {
        const int sel = select(fd + 1, &rfds, NULL, NULL, NULL);
        return (sel > 0) ? 0 : -1;
    }

    struct timeval tv;
    tv.tv_sec = (timeout_ms / 1000);
    tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
    {
        const int sel = select(fd + 1, &rfds, NULL, NULL, &tv);
        return (sel > 0) ? 0 : -1;
    }
}

static int tcp_recv_exact(int fd, uint8_t *buf, size_t need, int timeout_ms)
{
    size_t got = 0u;

    while (got < need) {
        const size_t rem = need - got;
        const int chunk = (rem > (size_t)INT_MAX) ? INT_MAX : (int)rem;

        if (tcp_wait_readable(fd, timeout_ms) != 0) {
            return -1;
        }

        {
            const int n = (int)recv(OWL_SOCK(fd), (char *)&buf[got], chunk, 0);
            if (n <= 0) {
                return -1;
            }
            got += (size_t)n;
        }
    }

    return 0;
}

static int tcp_recv_frame(int fd, uint8_t *buf, size_t maxlen, int timeout_ms)
{
    size_t total_len;

    if ((buf == NULL) || (maxlen < 9u)) {
        return -1;
    }

    if (tcp_recv_exact(fd, buf, 7u, timeout_ms) != 0) {
        return -1;
    }

    total_len = (size_t)7u + (size_t)(((uint16_t)buf[5] << 8) | (uint16_t)buf[6]) + (size_t)2u;
    if (total_len > maxlen) {
        return -2;
    }

    if (tcp_recv_exact(fd, &buf[7], total_len - 7u, timeout_ms) != 0) {
        return -1;
    }

    return (int)total_len;
}

static int pack_packet(uint8_t *dst, size_t dst_cap,
                       uint8_t rw_ascii, owl_iface_t iface, uint16_t addr,
                       const uint8_t *data, uint16_t msl,
                       size_t *out_len)
{
    if ((dst == NULL) || (out_len == NULL)) {
        return OWL_ERR_ARG;
    }
    if ((rw_ascii != OWL_RW_READ_ASCII) && (rw_ascii != OWL_RW_WRITE_ASCII)) {
        return OWL_ERR_ARG;
    }
    if ((rw_ascii == OWL_RW_READ_ASCII) && (msl != 0u)) {
        return OWL_ERR_ARG;
    }
    if ((rw_ascii == OWL_RW_WRITE_ASCII) && (data == NULL) && (msl != 0u)) {
        return OWL_ERR_ARG;
    }
    if (msl > OWL_TX_MAX_DATA) {
        return OWL_ERR_ARG;
    }

    {
        const size_t need = (size_t)1u + 1u + 1u + 2u + 2u + (size_t)msl + 2u;
        if (need > dst_cap) {
            return OWL_ERR_ARG;
        }
    }

    size_t o = 0U;
    dst[o++] = (uint8_t)OWL_HDR_BYTE;
    dst[o++] = rw_ascii;
    dst[o++] = (uint8_t)iface;
    dst[o++] = (uint8_t)((addr >> 8) & 0xFFu);
    dst[o++] = (uint8_t)(addr & 0xFFu);
    dst[o++] = (uint8_t)((msl >> 8) & 0xFFu);
    dst[o++] = (uint8_t)(msl & 0xFFu);

    if (msl > 0u) {
        (void)memcpy(&dst[o], data, (size_t)msl);
        o += (size_t)msl;
    }

    {
        const uint16_t crc = crc16_xmodem(dst, o);
        dst[o++] = (uint8_t)((crc >> 8) & 0xFFu);
        dst[o++] = (uint8_t)(crc & 0xFFu);
    }

    *out_len = o;
    return OWL_OK;
}

static int parse_resp_and_copy(const uint8_t *rx, size_t rx_len,
                               uint8_t expected_rw,
                               owl_status_t *status_out,
                               uint8_t *data_out, size_t *data_io)
{
    (void)expected_rw;

    if ((rx == NULL) || (rx_len < MIN_RESP_LEN) || (status_out == NULL)) {
        capture_failed_frame(rx, rx_len);
        return OWL_ERR_PROTO;
    }
    if (rx[0] != (uint8_t)OWL_HDR_BYTE) {
        capture_failed_frame(rx, rx_len);
        return OWL_ERR_PROTO;
    }

    const uint8_t status = rx[1];
    const uint16_t msl = (uint16_t)(((uint16_t)rx[5] << 8) | (uint16_t)rx[6]);

    if (rx_len < (size_t)(7u + msl + 2u)) {
        capture_failed_frame(rx, rx_len);
        return OWL_ERR_PROTO;
    }

    {
        const size_t without_crc = (size_t)7u + (size_t)msl;
        const uint16_t crc_rx = (uint16_t)(((uint16_t)rx[without_crc] << 8) | (uint16_t)rx[without_crc + 1u]);
        const uint16_t crc_cal = crc16_xmodem(rx, without_crc);
        if (crc_rx != crc_cal) {
            capture_failed_frame(rx, rx_len);
            return OWL_ERR_PROTO;
        }
    }

    if ((data_out != NULL) && (data_io != NULL)) {
        const size_t out_cap = *data_io;
        const size_t to_copy = ((size_t)msl <= out_cap) ? (size_t)msl : out_cap;
        (void)memcpy(data_out, &rx[7], to_copy);
        *data_io = to_copy;
    }

    *status_out = (owl_status_t)status;
    if (status != (uint8_t)OWL_ST_OK) {
        capture_failed_frame(rx, rx_len);
        return OWL_ERR_STATUS;
    }

    return OWL_OK;
}

static int owl_write_u8(owl_cam_t *cam, owl_iface_t iface, uint16_t addr, uint8_t value)
{
    return owl_cam_write(cam, iface, addr, &value, 1u);
}

static int owl_read_u8(owl_cam_t *cam, owl_iface_t iface, uint16_t addr, uint8_t *value)
{
    uint8_t buf[1] = {0};
    size_t len = sizeof(buf);
    int rc;

    if ((cam == NULL) || (value == NULL)) {
        return OWL_ERR_ARG;
    }

    rc = owl_cam_read(cam, iface, addr, buf, &len);
    if (rc != OWL_OK) {
        return rc;
    }
    if (len < 1u) {
        return OWL_ERR_PROTO;
    }

    *value = buf[0];
    return OWL_OK;
}

static void log_hex_frame(const char *prefix, const uint8_t *buf, size_t len)
{
    (void)prefix;
    (void)buf;
    (void)len;
}

static int owl_zoom_mode_is_valid(uint8_t zoom_mode)
{
    return (zoom_mode == OWL_ZOOM_MODE_CONTINUOUS) ||
           (zoom_mode == OWL_ZOOM_MODE_MANUAL);
}

static int owl_zoom_direction_is_valid(uint8_t direction)
{
    return (direction == OWL_ZOOM_IN) ||
           (direction == OWL_ZOOM_OUT);
}

static int owl_focus_direction_is_valid(uint8_t direction)
{
    return (direction == OWL_FOCUS_FAR) ||
           (direction == OWL_FOCUS_NEAR);
}

static int owl_speed_in_range(uint8_t speed, uint8_t min_value, uint8_t max_value)
{
    return (speed >= min_value) && (speed <= max_value);
}

static int owl_day_lowlight_lens_args_are_valid(uint8_t mode, uint8_t speed)
{
    /* ICD Table 10: only continuous zoom/focus is currently supported, speed 1..7. */
    return (mode == OWL_ZOOM_MODE_CONTINUOUS) &&
           (owl_speed_in_range(speed, OWL_DAY_LOWLIGHT_SPEED_MIN, OWL_DAY_LOWLIGHT_SPEED_MAX) != 0);
}

static int owl_day_normal_lens_args_are_valid(uint8_t mode, uint8_t speed)
{
    /* ICD Table 12: continuous uses speed 1..7, manual uses 0. */
    if (mode == OWL_ZOOM_MODE_CONTINUOUS) {
        return owl_speed_in_range(speed, OWL_DAY_NORMAL_SPEED_MIN, OWL_DAY_NORMAL_SPEED_MAX);
    }

    if (mode == OWL_ZOOM_MODE_MANUAL) {
        return (speed == OWL_DAY_NORMAL_MANUAL_STEP);
    }

    return 0;
}

static int owl_cam_lens_zoom_write(owl_cam_t *cam, owl_iface_t iface,
                                   uint8_t zoom_mode, uint8_t direction, uint8_t third_byte)
{
    uint8_t payload[3];

    if (cam == NULL) {
        return OWL_ERR_ARG;
    }
    if (owl_zoom_mode_is_valid(zoom_mode) == 0) {
        return OWL_ERR_ARG;
    }
    if (owl_zoom_direction_is_valid(direction) == 0) {
        return OWL_ERR_ARG;
    }

    payload[0] = zoom_mode;
    payload[1] = direction;
    payload[2] = third_byte;
    return owl_cam_write(cam, iface, OWL_ADDR_ZOOM_CTRL, payload, 3u);
}

static int owl_cam_lens_focus_write(owl_cam_t *cam, owl_iface_t iface,
                                    uint8_t focus_mode, uint8_t direction, uint8_t third_byte)
{
    uint8_t payload[3];

    if (cam == NULL) {
        return OWL_ERR_ARG;
    }
    if (owl_zoom_mode_is_valid(focus_mode) == 0) {
        return OWL_ERR_ARG;
    }
    if (owl_focus_direction_is_valid(direction) == 0) {
        return OWL_ERR_ARG;
    }

    payload[0] = focus_mode;
    payload[1] = direction;
    payload[2] = third_byte;
    return owl_cam_write(cam, iface, OWL_ADDR_FOCUS_CTRL, payload, 3u);
}

static int owl_cam_lens_stop(owl_cam_t *cam, owl_iface_t iface, uint8_t stop_mode)
{
    if (cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((stop_mode != OWL_LENS_STOP_ZOOM) &&
        (stop_mode != OWL_LENS_STOP_FOCUS)) {
        return OWL_ERR_ARG;
    }

    return owl_write_u8(cam, iface, OWL_ADDR_ZOOM_STOP, stop_mode);
}

/* ---- Public API ---- */

void camera_iface_bind_cam(owl_cam_t *cam)
{
    g_tracker_cam = cam;
}

int owl_cam_open(owl_cam_t *cam, const char *ip4_str, uint16_t tcp_port)
{
    if ((cam == NULL) || (ip4_str == NULL)) {
        return OWL_ERR_ARG;
    }
    if (owl_net_init() != 0) {
        return OWL_ERR_SOCK;
    }

    (void)memset(cam, 0, sizeof(*cam));
    cam->tcp_fd = -1;

    struct sockaddr_in sa;
    (void)memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)((tcp_port == 0u) ? OWL_TCP_PORT : tcp_port));

    if (inet_pton(AF_INET, ip4_str, &sa.sin_addr) != 1) {
        return OWL_ERR_ARG;
    }

    {
        const int fd = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd < 0) {
            return OWL_ERR_SOCK;
        }

        if (connect(OWL_SOCK(fd), (const struct sockaddr *)&sa, sizeof(sa)) != 0) {
            (void)owl_close_socket(fd);
            return OWL_ERR_SOCK;
        }

        cam->tcp_fd = fd;
    }

    cam->cam_ipv4_be = sa.sin_addr.s_addr;
    cam->tcp_port_be = sa.sin_port;
    camera_iface_bind_cam(cam);
    return OWL_OK;
}

void owl_cam_close(owl_cam_t *cam)
{
    if ((cam != NULL) && (cam->tcp_fd >= 0)) {
        (void)owl_close_socket(cam->tcp_fd);
        cam->tcp_fd = -1;
    }
}

int owl_cam_read(owl_cam_t *cam, owl_iface_t iface, uint16_t addr,
                 uint8_t *data_out, size_t *data_out_len)
{
    if ((cam == NULL) || (cam->tcp_fd < 0)) {
        return OWL_ERR_ARG;
    }
    if ((data_out == NULL) || (data_out_len == NULL)) {
        return OWL_ERR_ARG;
    }

    uint8_t tx[9];
    size_t tx_len = 0U;
    int rc = pack_packet(tx, sizeof(tx), OWL_RW_READ_ASCII, iface, addr, NULL, 0u, &tx_len);
    if (rc != OWL_OK) {
        return rc;
    }

    if (tcp_send_all(cam->tcp_fd, tx, tx_len) != 0) {
        return OWL_ERR_IO;
    }

    uint8_t rx[OWL_TX_MAX_PACKET];
    const int n = tcp_recv_frame(cam->tcp_fd, rx, sizeof(rx), 1000);
    if (n <= 0) {
        return (n == -2) ? OWL_ERR_PROTO : OWL_ERR_IO;
    }

    owl_status_t st = OWL_ST_FAILED;
    size_t io = *data_out_len;
    rc = parse_resp_and_copy(rx, (size_t)n, OWL_RW_READ_ASCII, &st, data_out, &io);
    *data_out_len = io;
    return rc;
}

int owl_cam_write(owl_cam_t *cam, owl_iface_t iface, uint16_t addr,
                  const uint8_t *data, size_t len)
{
    if ((cam == NULL) || (cam->tcp_fd < 0)) {
        return OWL_ERR_ARG;
    }
    if ((data == NULL) && (len > 0u)) {
        return OWL_ERR_ARG;
    }
    if (len > OWL_TX_MAX_DATA) {
        return OWL_ERR_ARG;
    }

    uint8_t tx[OWL_TX_MAX_PACKET];
    size_t tx_len = 0U;
    int rc = pack_packet(tx, sizeof(tx), OWL_RW_WRITE_ASCII, iface, addr, data, (uint16_t)len, &tx_len);
    if (rc != OWL_OK) {
        return rc;
    }

    log_hex_frame("TX", tx, tx_len);

    if (tcp_send_all(cam->tcp_fd, tx, tx_len) != 0) {
        return OWL_ERR_IO;
    }

    uint8_t rx[OWL_TX_MAX_PACKET];
    const int n = tcp_recv_frame(cam->tcp_fd, rx, sizeof(rx), 1000);
    if (n <= 0) {
        return (n == -2) ? OWL_ERR_PROTO : OWL_ERR_IO;
    }
    log_hex_frame("RX", rx, (size_t)n);

    owl_status_t st = OWL_ST_FAILED;
    uint8_t ignore[8];
    size_t io = sizeof(ignore);
    return parse_resp_and_copy(rx, (size_t)n, OWL_RW_WRITE_ASCII, &st, ignore, &io);
}

int owl_cam_liveliness(owl_cam_t *cam, bool *alive)
{
    if ((cam == NULL) || (alive == NULL)) {
        return OWL_ERR_ARG;
    }

    uint8_t buf[8] = {0};
    size_t len = sizeof(buf);
    const int rc = owl_cam_read(cam, OWL_IFACE_CONTROL, OWL_ADDR_LIVELINESS, buf, &len);
    if (rc != OWL_OK) {
        *alive = false;
        return rc;
    }

    *alive = (len > 0u) && (buf[0] == 0x01u);
    return OWL_OK;
}

int owl_cam_set_telemetry(owl_cam_t *cam, bool enable)
{
    if (cam == NULL) {
        return OWL_ERR_ARG;
    }

    return owl_write_u8(cam, OWL_IFACE_CONTROL, OWL_ADDR_TELEMETRY_DATA,
                        (enable != false) ? 0x01u : 0x02u);
}

int owl_cam_restart(owl_cam_t *cam, uint8_t mode)
{
    if (cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((mode != OWL_RESTART_PAYLOAD) &&
        (mode != OWL_RESTART_SERVICE) &&
        (mode != OWL_RESTART_SHUTDOWN)) {
        return OWL_ERR_ARG;
    }

    return owl_write_u8(cam, OWL_IFACE_CONTROL, OWL_ADDR_GLOBAL_RESTART, mode);
}

int owl_cam_set_pip(owl_cam_t *cam, bool enable)
{
    if (cam == NULL) {
        return OWL_ERR_ARG;
    }

    return owl_write_u8(cam, OWL_IFACE_CONTROL, OWL_ADDR_PIP,
                        (enable != false) ? OWL_PIP_ON : OWL_PIP_OFF);
}

int owl_cam_get_pip(owl_cam_t *cam, bool *enabled)
{
    uint8_t value;
    int rc;

    if ((cam == NULL) || (enabled == NULL)) {
        return OWL_ERR_ARG;
    }

    rc = owl_read_u8(cam, OWL_IFACE_CONTROL, OWL_ADDR_PIP, &value);
    if (rc != OWL_OK) {
        return rc;
    }
    if ((value != OWL_PIP_ON) && (value != OWL_PIP_OFF)) {
        return OWL_ERR_PROTO;
    }

    *enabled = (value == OWL_PIP_ON);
    return OWL_OK;
}

int owl_cam_set_pip_location(owl_cam_t *cam, uint8_t location)
{
    if (cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((location < OWL_PIP_LOC_TOP_LEFT) ||
        (location > OWL_PIP_LOC_BOTTOM_RIGHT)) {
        return OWL_ERR_ARG;
    }

    return owl_write_u8(cam, OWL_IFACE_CONTROL, OWL_ADDR_PIP_LOCATION, location);
}

int owl_cam_get_pip_location(owl_cam_t *cam, uint8_t *location)
{
    int rc;

    if ((cam == NULL) || (location == NULL)) {
        return OWL_ERR_ARG;
    }

    rc = owl_read_u8(cam, OWL_IFACE_CONTROL, OWL_ADDR_PIP_LOCATION, location);
    if (rc != OWL_OK) {
        return rc;
    }
    if ((*location < OWL_PIP_LOC_TOP_LEFT) ||
        (*location > OWL_PIP_LOC_BOTTOM_RIGHT)) {
        return OWL_ERR_PROTO;
    }

    return OWL_OK;
}

int owl_cam_day_power(owl_cam_t *cam, uint8_t mode)
{
    if (cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((mode != OWL_POWER_ON) &&
        (mode != OWL_POWER_OFF) &&
        (mode != OWL_POWER_REBOOT)) {
        return OWL_ERR_ARG;
    }

    return owl_write_u8(cam, OWL_IFACE_CONTROL, OWL_ADDR_DAY_POWER, mode);
}

int owl_cam_thermal_power(owl_cam_t *cam, uint8_t mode)
{
    if (cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((mode != OWL_POWER_ON) &&
        (mode != OWL_POWER_OFF) &&
        (mode != OWL_POWER_REBOOT)) {
        return OWL_ERR_ARG;
    }

    return owl_write_u8(cam, OWL_IFACE_CONTROL, OWL_ADDR_THERMAL_POWER, mode);
}

int owl_cam_select_thermal(owl_cam_t *cam, bool power_off_day)
{
    int rc = owl_cam_thermal_power(cam, OWL_POWER_ON);
    if (rc != OWL_OK) {
        return rc;
    }

    if (power_off_day) {
        rc = owl_cam_day_power(cam, OWL_POWER_OFF);
        if (rc != OWL_OK) {
            return rc;
        }
    }

    return OWL_OK;
}

int owl_cam_select_day_lowlight(owl_cam_t *cam, bool power_off_thermal)
{
    int rc = owl_cam_day_power(cam, OWL_POWER_ON);
    if (rc != OWL_OK) {
        return rc;
    }

    if (power_off_thermal) {
        rc = owl_cam_thermal_power(cam, OWL_POWER_OFF);
        if (rc != OWL_OK) {
            return rc;
        }
    }

    return OWL_OK;
}

int owl_cam_select_day_normal(owl_cam_t *cam, bool power_off_thermal)
{
    int rc = owl_cam_day_power(cam, OWL_POWER_ON);
    if (rc != OWL_OK) {
        return rc;
    }

    if (power_off_thermal) {
        rc = owl_cam_thermal_power(cam, OWL_POWER_OFF);
        if (rc != OWL_OK) {
            return rc;
        }
    }

    return OWL_OK;
}

int owl_cam_tracker_switch_camera(owl_cam_t *cam, uint8_t cam_id)
{
    if (cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((cam_id != OWL_TRACK_CAM_THERMAL) &&
        (cam_id != OWL_TRACK_CAM_DAY1) &&
        (cam_id != OWL_TRACK_CAM_DAY2)) {
        return OWL_ERR_ARG;
    }

    return owl_write_u8(cam, OWL_IFACE_TRACKER, OWL_ADDR_TRK_CAM_SWITCH, cam_id);
}

int owl_cam_lrf_single_measure(owl_cam_t *cam, uint8_t mode)
{
    if (cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((mode != OWL_LRF_SMM) &&
        (mode != OWL_LRF_CH1) &&
        (mode != OWL_LRF_CH2)) {
        return OWL_ERR_ARG;
    }

    return owl_write_u8(cam, OWL_IFACE_LRF, OWL_ADDR_LRF_RANGE_MEAS, mode);
}

int owl_cam_lrf_set_frequency(owl_cam_t *cam, uint8_t freq_mode)
{
    if (cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((freq_mode < OWL_LRF_FREQ_1HZ) ||
        (freq_mode > OWL_LRF_FREQ_200HZ)) {
        return OWL_ERR_ARG;
    }

    return owl_write_u8(cam, OWL_IFACE_LRF, OWL_ADDR_LRF_CONTINUOUS_RANGE, freq_mode);
}

int owl_cam_lrf_stop(owl_cam_t *cam)
{
    if (cam == NULL) {
        return OWL_ERR_ARG;
    }

    return owl_write_u8(cam, OWL_IFACE_LRF, OWL_ADDR_LRF_BREAK_CMD, 0x01u);
}

int owl_cam_lrf_align_pointer(owl_cam_t *cam, bool enable)
{
    if (cam == NULL) {
        return OWL_ERR_ARG;
    }

    return owl_write_u8(cam, OWL_IFACE_LRF, OWL_ADDR_LRF_ALIGN_POINTER,
                        (enable != false) ? 0x01u : 0x02u);
}

int owl_cam_lrf_read_target_range(owl_cam_t *cam, uint16_t *out_range_m)
{
    if ((cam == NULL) || (out_range_m == NULL)) {
        return OWL_ERR_ARG;
    }

    uint8_t buf[8] = {0};
    size_t len = sizeof(buf);
    const int rc = owl_cam_read(cam, OWL_IFACE_LRF, OWL_ADDR_LRF_TARGET_RANGE, buf, &len);
    if (rc != OWL_OK) {
        return rc;
    }
    if (len < 2u) {
        return OWL_ERR_PROTO;
    }

    *out_range_m = rd_u16_be(buf);
    return OWL_OK;
}

int owl_cam_day_lowlight_zoom(owl_cam_t *cam, uint8_t zoom_mode, uint8_t direction, uint8_t speed)
{
    if (owl_day_lowlight_lens_args_are_valid(zoom_mode, speed) == 0) {
        return OWL_ERR_ARG;
    }

    return owl_cam_lens_zoom_write(cam, OWL_IFACE_DAY_LOWLIGHT, zoom_mode, direction, speed);
}

int owl_cam_day_lowlight_zoom_stop(owl_cam_t *cam)
{
    return owl_cam_lens_stop(cam, OWL_IFACE_DAY_LOWLIGHT, OWL_LENS_STOP_ZOOM);
}

int owl_cam_day_lowlight_focus(owl_cam_t *cam, uint8_t focus_mode, uint8_t direction, uint8_t speed)
{
    if (owl_day_lowlight_lens_args_are_valid(focus_mode, speed) == 0) {
        return OWL_ERR_ARG;
    }

    return owl_cam_lens_focus_write(cam, OWL_IFACE_DAY_LOWLIGHT, focus_mode, direction, speed);
}

int owl_cam_day_lowlight_focus_stop(owl_cam_t *cam)
{
    return owl_cam_lens_stop(cam, OWL_IFACE_DAY_LOWLIGHT, OWL_LENS_STOP_FOCUS);
}

int owl_cam_day_normal_zoom(owl_cam_t *cam, uint8_t zoom_mode, uint8_t direction, uint8_t speed)
{
    if (owl_day_normal_lens_args_are_valid(zoom_mode, speed) == 0) {
        return OWL_ERR_ARG;
    }

    return owl_cam_lens_zoom_write(cam, OWL_IFACE_DAY_NORMAL, zoom_mode, direction, speed);
}

int owl_cam_day_normal_zoom_stop(owl_cam_t *cam)
{
    return owl_cam_lens_stop(cam, OWL_IFACE_DAY_NORMAL, OWL_LENS_STOP_ZOOM);
}

int owl_cam_day_normal_focus(owl_cam_t *cam, uint8_t focus_mode, uint8_t direction, uint8_t speed)
{
    if (owl_day_normal_lens_args_are_valid(focus_mode, speed) == 0) {
        return OWL_ERR_ARG;
    }

    return owl_cam_lens_focus_write(cam, OWL_IFACE_DAY_NORMAL, focus_mode, direction, speed);
}

int owl_cam_day_normal_focus_stop(owl_cam_t *cam)
{
    return owl_cam_lens_stop(cam, OWL_IFACE_DAY_NORMAL, OWL_LENS_STOP_FOCUS);
}

int owl_cam_day_zoom(owl_cam_t *cam, uint8_t zoom_mode, uint8_t direction)
{
    return owl_cam_lens_zoom_write(cam, OWL_IFACE_DAY, zoom_mode, direction, OWL_THERMAL_LENS_RESERVED);
}

int owl_cam_day_zoom_stop(owl_cam_t *cam)
{
    return owl_cam_lens_stop(cam, OWL_IFACE_DAY, OWL_LENS_STOP_ZOOM);
}

int owl_cam_day_focus(owl_cam_t *cam, uint8_t focus_mode, uint8_t direction)
{
    return owl_cam_lens_focus_write(cam, OWL_IFACE_DAY, focus_mode, direction, OWL_THERMAL_LENS_RESERVED);
}

int owl_cam_day_focus_stop(owl_cam_t *cam)
{
    return owl_cam_lens_stop(cam, OWL_IFACE_DAY, OWL_LENS_STOP_FOCUS);
}

int owl_cam_thermal_zoom(owl_cam_t *cam, uint8_t zoom_mode, uint8_t direction)
{
    return owl_cam_lens_zoom_write(cam, OWL_IFACE_THERMAL, zoom_mode, direction, OWL_THERMAL_LENS_RESERVED);
}

int owl_cam_thermal_zoom_stop(owl_cam_t *cam)
{
    return owl_cam_lens_stop(cam, OWL_IFACE_THERMAL, OWL_LENS_STOP_ZOOM);
}

int owl_cam_thermal_focus(owl_cam_t *cam, uint8_t focus_mode, uint8_t direction)
{
    return owl_cam_lens_focus_write(cam, OWL_IFACE_THERMAL, focus_mode, direction, OWL_THERMAL_LENS_RESERVED);
}

int owl_cam_thermal_focus_stop(owl_cam_t *cam)
{
    return owl_cam_lens_stop(cam, OWL_IFACE_THERMAL, OWL_LENS_STOP_FOCUS);
}

/* telemetry */

int camera_telem_open(owl_telem_t *ctx, const char *bind_ip, uint16_t port)
{
    if (ctx == NULL) {
        return -1;
    }
    if (owl_net_init() != 0) {
        return -2;
    }

    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->udp_fd = -1;

    const uint16_t use_port = (port == 0U) ? OWL_UDP_TELEM_PORT : port;

    const int fd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        return -2;
    }

    int yes = 1;
    (void)setsockopt(OWL_SOCK(fd), SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, (socklen_t)sizeof(yes));

    struct sockaddr_in sa;
    (void)memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(use_port);
    sa.sin_addr.s_addr = (bind_ip != NULL) ? inet_addr(bind_ip) : htonl(INADDR_ANY);

    if (bind(OWL_SOCK(fd), (const struct sockaddr *)&sa, sizeof(sa)) != 0) {
        (void)owl_close_socket(fd);
        return -3;
    }

    ctx->udp_fd = fd;
    ctx->port = use_port;
    return 0;
}

void camera_telem_close(owl_telem_t *ctx)
{
    if ((ctx != NULL) && (ctx->udp_fd >= 0)) {
        (void)owl_close_socket(ctx->udp_fd);
        ctx->udp_fd = -1;
    }
}

static int parse_telem(const uint8_t *buf, size_t len, owl_telem_frame_t *out)
{
    if ((buf == NULL) || (out == NULL)) {
        return -1;
    }

    const uint8_t *p = buf;
    size_t payload_len = 0U;

    if ((len >= (size_t)175U) && (buf[0] == 0xCCu)) {
        p = &buf[7];
        payload_len = (size_t)168U;
    } else if (len >= (size_t)168U) {
        p = buf;
        payload_len = (size_t)168U;
    } else {
        return -1;
    }

    if ((payload_len != (size_t)168U) || ((size_t)(p - buf) + payload_len > len)) {
        return -1;
    }

    size_t o = 0U;

    out->version = p[o];
    o += 1U;

    o += 10U;

    out->lrf_range = rd_u16_be(&p[o]);
    o += 5U;

    out->thermal_ret_x = rd_u16_be(&p[o]);
    o += 2U;
    out->thermal_ret_y = rd_u16_be(&p[o]);
    o += 2U;
    out->thermal_fov_code = p[o];
    o += 1U;
    out->thermal_fov_deg = rd_f32_le(&p[o]);
    o += 4U;
    out->thermal_nuc_status = p[o];
    o += 1U;
    o += 10U;

    out->day_ret_x = rd_u16_be(&p[o]);
    o += 2U;
    out->day_ret_y = rd_u16_be(&p[o]);
    o += 2U;
    out->day_fov_code = p[o];
    o += 1U;
    out->day_fov_deg = rd_f32_le(&p[o]);
    o += 4U;
    o += 11U;

    o += 20U; /* Day2 block ignored in struct */

    out->tracker_mode = p[o];
    o += 1U;
    out->tracker_ret_x = rd_u16_be(&p[o]);
    o += 2U;
    out->tracker_ret_y = rd_u16_be(&p[o]);
    o += 2U;
    out->tracker_bb_w = rd_u16_be(&p[o]);
    o += 2U;
    out->tracker_bb_h = rd_u16_be(&p[o]);
    o += 2U;
    out->tracker_pan_err = rd_u16_be(&p[o]);
    o += 2U;
    out->tracker_tilt_err = rd_u16_be(&p[o]);
    o += 2U;
    o += 7U;

    o += 20U;

    out->sys_temp = rd_u16_be(&p[o]);
    o += 2U;

    out->lat = rd_f64_le(&p[o]);
    o += 8U;
    out->NSdir = p[o];
    o += 1U;
    out->lon = rd_f64_le(&p[o]);
    o += 8U;
    out->EWdir = p[o];
    o += 1U;
    out->alt = rd_f32_le(&p[o]);
    o += 4U;
    out->sat = p[o];
    o += 1U;
    out->gpsQOS = p[o];
    o += 1U;
    o += 21U;

    out->pwr_day_on     = (p[o + 0U] == 0x01u) ? 1U : 0U;
    out->pwr_thermal_on = (p[o + 1U] == 0x01u) ? 1U : 0U;
    out->pwr_plcb_on    = (p[o + 2U] == 0x01u) ? 1U : 0U;
    out->pwr_lrf_on     = (p[o + 3U] == 0x01u) ? 1U : 0U;
    out->pwr_overall    = p[o + 4U];

    return 0;
}

int camera_telem_recv(owl_telem_t *ctx, owl_telem_frame_t *out, int timeout_ms)
{
    static uint32_t s_rx_ok = 0U;
    static uint32_t s_rx_bad = 0U;

    if ((ctx == NULL) || (out == NULL) || (ctx->udp_fd < 0)) {
        return -1;
    }

    if (timeout_ms != 0) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(OWL_SOCK(ctx->udp_fd), &rfds);

        struct timeval tv;
        const struct timeval *ptv = NULL;

        if (timeout_ms > 0) {
            tv.tv_sec = (timeout_ms / 1000);
            tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
            ptv = &tv;
        }

        {
            const int sel = select(ctx->udp_fd + 1, &rfds, NULL, NULL, ptv);
            if (sel == 0) {
                return 0;
            }
            if (sel < 0) {
                return -2;
            }
        }
    }

    uint8_t buf[2048];
    const int n = (int)recvfrom(OWL_SOCK(ctx->udp_fd), (char *)buf, (int)sizeof(buf), 0, NULL, NULL);
    if (n < 0) {
        if (owl_socket_wouldblock() != 0) {
            return 0;
        }
        return -3;
    }

    if (n < 168) {
        s_rx_bad++;
        return -4;
    }

    if (parse_telem(buf, (size_t)n, out) == 0) {
        s_rx_ok++;
        return 1;
    }

    s_rx_bad++;
    return -5;
}

void camera_telem_publish(const camera_params_t *cam, const owl_telem_frame_t *fr)
{
    if ((cam == NULL) || (fr == NULL)) {
        return;
    }

    const char *root = (cam->root_topic[0] != '\0') ? cam->root_topic : "OWL";
    char tmp[64];

    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->version);
    pub_kv(root, "VERSION", tmp);

    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->lrf_range);
    pub_kv(root, "LRF_RANGE", tmp);

    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->thermal_ret_x);
    pub_kv(root, "THERMAL/RETICLE_X", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->thermal_ret_y);
    pub_kv(root, "THERMAL/RETICLE_Y", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->thermal_fov_code);
    pub_kv(root, "THERMAL/FOV_CODE", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%.4f", (double)fr->thermal_fov_deg);
    pub_kv(root, "THERMAL/FOV_DEG", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->thermal_nuc_status);
    pub_kv(root, "THERMAL/NUC", tmp);

    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->day_ret_x);
    pub_kv(root, "DAY/RETICLE_X", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->day_ret_y);
    pub_kv(root, "DAY/RETICLE_Y", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->day_fov_code);
    pub_kv(root, "DAY/FOV_CODE", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%.4f", (double)fr->day_fov_deg);
    pub_kv(root, "DAY/FOV_DEG", tmp);

    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_mode);
    pub_kv(root, "TRACKER/MODE", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_ret_x);
    pub_kv(root, "TRACKER/RETICLE_X", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_ret_y);
    pub_kv(root, "TRACKER/RETICLE_Y", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_bb_h);
    pub_kv(root, "TRACKER/BB_H", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_bb_w);
    pub_kv(root, "TRACKER/BB_W", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_pan_err);
    pub_kv(root, "TRACKER/PAN_ERR", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_tilt_err);
    pub_kv(root, "TRACKER/TILT_ERR", tmp);

    {
        float tC = ((float)fr->sys_temp) / 100.0f;
        (void)snprintf(tmp, sizeof(tmp), "%.2f", (double)tC);
        pub_kv(root, "TEMP/SYSTEM_C", tmp);
    }

    {
        const char ns = (char)fr->NSdir;
        const char ew = (char)fr->EWdir;
        double lat = fr->lat;
        double lon = fr->lon;

        if (ns == 'S') {
            lat = -lat;
        }
        if (ew == 'W') {
            lon = -lon;
        }

        (void)snprintf(tmp, sizeof(tmp), "%.8f", lat);
        pub_kv(root, "GPS/LAT", tmp);
        (void)snprintf(tmp, sizeof(tmp), "%.8f", lon);
        pub_kv(root, "GPS/LON", tmp);

        tmp[0] = (ns == 'S' || ns == 'N') ? ns : '?';
        tmp[1] = '\0';
        pub_kv(root, "GPS/NS", tmp);

        tmp[0] = (ew == 'E' || ew == 'W') ? ew : '?';
        tmp[1] = '\0';
        pub_kv(root, "GPS/EW", tmp);
    }

    (void)snprintf(tmp, sizeof(tmp), "%.2f", (double)fr->alt);
    pub_kv(root, "GPS/ALT_M", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->sat);
    pub_kv(root, "GPS/SATS", tmp);
    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->gpsQOS);
    pub_kv(root, "GPS/QOS", tmp);

    pub_kv(root, "POWER/DAY",     (fr->pwr_day_on     != 0U) ? "true" : "false");
    pub_kv(root, "POWER/THERMAL", (fr->pwr_thermal_on != 0U) ? "true" : "false");
    pub_kv(root, "POWER/PLCB",    (fr->pwr_plcb_on    != 0U) ? "true" : "false");
    pub_kv(root, "POWER/LRF",     (fr->pwr_lrf_on     != 0U) ? "true" : "false");

    (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->pwr_overall);
    pub_kv(root, "POWER/OVERALL", tmp);
}

void camera_iface_set_udp_mcast(bool enabled, uint8_t camera_id, uint32_t *seq_counter)
{
    g_udp_mcast_enabled = (enabled != false) ? 1 : 0;
    g_udp_camera_id = camera_id;
    g_udp_seq_counter = seq_counter;
}

int camera_iface_get_last_failed_frame(uint8_t *out_frame, size_t *inout_len,
                                       uint8_t *out_status, owl_iface_t *out_iface, uint16_t *out_addr)
{
    if ((out_frame == NULL) || (inout_len == NULL)) {
        return OWL_ERR_ARG;
    }
    if (g_last_fail_valid == 0) {
        return 0;
    }

    const size_t cap = *inout_len;
    const size_t n = (g_last_fail_len <= cap) ? g_last_fail_len : cap;
    (void)memcpy(out_frame, g_last_fail_frame, n);
    *inout_len = n;

    if (out_status != NULL) {
        *out_status = g_last_fail_status;
    }
    if (out_iface != NULL) {
        *out_iface = g_last_fail_iface;
    }
    if (out_addr != NULL) {
        *out_addr = g_last_fail_addr;
    }

    return 1;
}

/* ---- Tracker writers/readers ---- */

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








// /*
//  * @file camera_iface.c
//  * @brief OWL MD-860 Ethernet command interface implementation (TCP 8088).
//  * @details
//  *  - Packet: 0xCC | 'R'/'W' | IFACE | ADDR[15:8] | ADDR[7:0] | MSL[15:8] | MSL[7:0] | DATA... | CRC16(XMODEM)[15:8] [7:0]
//  *  - CRC: CRC-16/XMODEM (poly 0x1021, init 0x0000, no reflect, no xorout).
//  *  - Responses validated: header 0xCC, status byte checked against Table 3.
//  *  References: field layout & R/W values???turn2file11???ANALINEAR Software_Command_Protocol 1.pdf???L128-L136???; status???turn2file3???ANALINEAR Software_Command_Protocol 1.pdf???L75-L101???.
//  */

// #include "camera_iface.h"
// #include "camera_mqtt.h"
// #include "camera_params.h"
// #include "api2_mcast_win.h"
// #include "mqtt.h"
// #include "tracker.h"

// #include <string.h>   /* memset, memcpy */
// #include <stdio.h>
// #include <limits.h>
// #include <errno.h>

// #if !defined(_WIN32)
// #error This project is Windows-only.
// #endif

// #include <winsock2.h>
// #include <ws2tcpip.h>
// #define OWL_SOCK(fd_) ((SOCKET)(uintptr_t)(fd_))
// static int owl_close_socket(int fd) { return (int)closesocket(OWL_SOCK(fd)); }
// static int owl_net_init(void)
// {
//     static int inited = 0;
//     if (inited == 0) {
//         WSADATA wsa;
//         if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
//             return -1;
//         }
//         inited = 1;
//     }
//     return 0;
// }


// /* ---- Local constants ---- */
// #define OWL_HDR_BYTE          (0xCCu)
// #define OWL_RW_READ_ASCII     (0x52u) /* 'R'???turn2file11???ANALINEAR Software_Command_Protocol 1.pdf???L130-L136??? */
// #define OWL_RW_WRITE_ASCII    (0x57u) /* 'W' */

// #define OWL_TX_MAX_DATA       (1024u) /* ICD maximum data payload???turn2file11???ANALINEAR Software_Command_Protocol 1.pdf???L158-L160??? */
// #define OWL_TX_MAX_PACKET     (1u + 1u + 1u + 2u + 2u + OWL_TX_MAX_DATA + 2u)

// #define MIN_RESP_LEN          (1u + 1u + 1u + 2u + 2u) /* header..MSL plus CRC may follow */

// #define IFACE_TRACKER           ((owl_iface_t)0x07u)  /* Tracker/PT command space */

// /* Simple binding so adapter calls can reach the active camera */
// static owl_cam_t *g_tracker_cam = NULL;
// static int g_udp_mcast_enabled = 0;
// static uint8_t g_udp_camera_id = 1u;
// static uint32_t* g_udp_seq_counter = NULL;


// static void pub_kv(const char *root, const char *leaf, const char *val)
// {
//     if ((root == NULL) || (leaf == NULL) || (val == NULL)) {
//         return;
//     }
//     char topic[256];
//     int n = snprintf(topic, sizeof(topic), "%s/TELEM/%s", root, leaf);
//     if ((n < 0) || ((size_t)n >= sizeof(topic))) {
//         return;
//     }
//     (void)mqtt_publish(topic, (const void *)val, strlen(val));
//     if ((g_udp_mcast_enabled != 0) && (g_udp_seq_counter != NULL)) {
//         (void)api2_send_tlm1((*g_udp_seq_counter)++, g_udp_camera_id, topic, val);
//     }
// }


// /* --- helpers: endian-safe readers (no UB) --- */
// static uint16_t rd_u16_be(const uint8_t *p)
// {
//     return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
// }

// /*
// static int16_t rd_s16_be(const uint8_t *p)
// {
//     return (int16_t)rd_u16_be(p);
// }
// */



// static float rd_f32_le(const uint8_t *p)
// {
//     /* Assemble as little-endian 32-bit then memcpy to float */
//     uint32_t u = ((uint32_t)p[0])
//                | ((uint32_t)p[1] << 8)
//                | ((uint32_t)p[2] << 16)
//                | ((uint32_t)p[3] << 24);
//     float f = 0.0f;
//     (void)memcpy(&f, &u, sizeof(f));
//     return f;
// }

// static double rd_f64_le(const uint8_t *p)
// {
//     /* Assemble as little-endian 64-bit then memcpy to double */
//     uint64_t u = ((uint64_t)p[0])
//                | ((uint64_t)p[1] << 8)
//                | ((uint64_t)p[2] << 16)
//                | ((uint64_t)p[3] << 24)
//                | ((uint64_t)p[4] << 32)
//                | ((uint64_t)p[5] << 40)
//                | ((uint64_t)p[6] << 48)
//                | ((uint64_t)p[7] << 56);
//     double d = 0.0;
//     (void)memcpy(&d, &u, sizeof(d));
//     return d;
// }

// int camera_iface_read_tracker(tracker_state_t *out_state)
// {
//     if ((out_state == NULL) || (g_tracker_cam == NULL)) { return OWL_ERR_ARG; }
//     return owl_tracker_read_all(out_state);
// }

// int camera_iface_cmd_w6(uint8_t mode_value)
// {
//     tracker_state_t st;
//     const uint8_t new_mode = (mode_value != 0u) ? 0x01u : 0x02u;
//     int rc = 0;

//     if (g_tracker_cam == NULL) { return OWL_ERR_ARG; }
//     rc = owl_tracker_read_all(&st);
//     if (rc != OWL_OK) { return rc; }

//     return owl_tracker_set_coord(st.coord_cam_id, st.coord_x, st.coord_y, new_mode);
// }

// int camera_iface_cmd_w7(uint8_t disable_mask)
// {
//     /* Table 10 (Tracker Control) doesn???t list a ???disable mask??? word.
//        Likely this maps to a different command or a vendor extension.
//        Leave as NOT IMPLEMENTED to force a clean resync path in the SM. */
//     (void)disable_mask;
//     return OWL_ERR_STATUS;
// }




// /* ---- Helpers (no dynamic allocation) ---- */
// static uint16_t crc16_xmodem(const uint8_t *buf, size_t len)
// {
//     /* XMODEM: poly=0x1021, init=0x0000, MSB-first, no refin/out. */
//     uint16_t crc = 0x0000u;
//     size_t i = 0U;
//     while (i < len) {
//         crc ^= (uint16_t)((uint16_t)buf[i] << 8);
//         for (int b = 0; b < 8; ++b) {
//             if ((crc & 0x8000u) != 0u) {
//                 crc = (uint16_t)((crc << 1) ^ 0x1021u);
//             } else {
//                 crc <<= 1;
//             }
//         }
//         ++i;
//     }
//     return crc;
// }

// static int tcp_send_all(int fd, const uint8_t *buf, size_t len)
// {
//     size_t sent = 0U;
//     while (sent < len) {
//         const size_t rem = len - sent;
//         const int chunk = (rem > (size_t)INT_MAX) ? INT_MAX : (int)rem;
//         const int n = (int)send(OWL_SOCK(fd), (const char *)(buf + sent), chunk, 0);
//         if (n <= 0) {
//             return -1;
//         }
//         sent += (size_t)n;
//     }
//     return 0;
// }

// static int tcp_recv_some(int fd, uint8_t *buf, size_t maxlen, int timeout_ms)
// {
//     /* Basic blocking read with SO_RCVTIMEO; MISRA: simple loop, check errors. */
//     (void)timeout_ms; /* If you want, set SO_RCVTIMEO on socket at open. */
//     const int chunk = (maxlen > (size_t)INT_MAX) ? INT_MAX : (int)maxlen;
//     const int n = (int)recv(OWL_SOCK(fd), (char *)buf, chunk, 0);
//     if (n <= 0) { return -1; }
//     return n;
// }

// static int pack_packet(uint8_t *dst, size_t dst_cap,
//                        uint8_t rw_ascii, owl_iface_t iface, uint16_t addr,
//                        const uint8_t *data, uint16_t msl,
//                        size_t *out_len)
// {
//     if ((dst == NULL) || (out_len == NULL)) { return OWL_ERR_ARG; }
//     if ((rw_ascii != OWL_RW_READ_ASCII) && (rw_ascii != OWL_RW_WRITE_ASCII)) {
//         return OWL_ERR_ARG;
//     }
//     if ((rw_ascii == OWL_RW_READ_ASCII) && (msl != 0u)) {
//         /* ICD: MSL must be 0 for READ???turn2file11???ANALINEAR Software_Command_Protocol 1.pdf???L154-L156??? */
//         return OWL_ERR_ARG;
//     }
//     if ((rw_ascii == OWL_RW_WRITE_ASCII) && (data == NULL) && (msl != 0u)) {
//         return OWL_ERR_ARG;
//     }
//     if (msl > OWL_TX_MAX_DATA) { return OWL_ERR_ARG; }

//     const size_t need = (size_t)1u + 1u + 1u + 2u + 2u + (size_t)msl + 2u;
//     if (need > dst_cap) { return OWL_ERR_ARG; }

//     size_t o = 0U;
//     dst[o++] = (uint8_t)OWL_HDR_BYTE;
//     dst[o++] = rw_ascii;
//     dst[o++] = (uint8_t)iface;
//     dst[o++] = (uint8_t)((addr >> 8) & 0xFFu);
//     dst[o++] = (uint8_t)(addr & 0xFFu);
//     dst[o++] = (uint8_t)((msl >> 8) & 0xFFu);
//     dst[o++] = (uint8_t)(msl & 0xFFu);

//     if (msl > 0u) {
//         (void)memcpy(&dst[o], data, (size_t)msl);
//         o += (size_t)msl;
//     }

//     const uint16_t crc = crc16_xmodem(dst, o);
//     dst[o++] = (uint8_t)((crc >> 8) & 0xFFu);
//     dst[o++] = (uint8_t)(crc & 0xFFu);

//     *out_len = o;
//     return OWL_OK;
// }

// static int parse_resp_and_copy(const uint8_t *rx, size_t rx_len,
//                                uint8_t expected_rw,
//                                owl_status_t *status_out,
//                                uint8_t *data_out, size_t *data_io)
// {
//     if ((rx == NULL) || (rx_len < MIN_RESP_LEN) || (status_out == NULL)) {
//         return OWL_ERR_PROTO;
//     }
//     if (rx[0] != (uint8_t)OWL_HDR_BYTE) { return OWL_ERR_PROTO; }
//     const uint8_t status = rx[1];
//     /* byte[2] = iface (echoed), [3..4]=addr, [5..6]=MSL, then DATA (MSL bytes), then CRC */
//     const uint16_t msl = (uint16_t)(((uint16_t)rx[5] << 8) | (uint16_t)rx[6]);

//     /* Check CRC */
//     if (rx_len < (size_t)(7u + msl + 2u)) { return OWL_ERR_PROTO; }
//     const size_t without_crc = (size_t)7u + (size_t)msl;
//     const uint16_t crc_rx = (uint16_t)(((uint16_t)rx[without_crc] << 8) | (uint16_t)rx[without_crc + 1u]);
//     const uint16_t crc_cal = crc16_xmodem(rx, without_crc);
//     if (crc_rx != crc_cal) { return OWL_ERR_PROTO; }

//     /* For WRITE ack, spec notes 2-byte response data; for READ ack, 2-byte data as well???turn2file3???ANALINEAR Software_Command_Protocol 1.pdf???L52-L56???.
//        We copy whatever MSL indicates (often 0x0002). */
//     if ((data_out != NULL) && (data_io != NULL)) {
//         const size_t out_cap = *data_io;
//         const size_t to_copy = (msl <= out_cap) ? (size_t)msl : out_cap;
//         (void)memcpy(data_out, &rx[7], to_copy);
//         *data_io = to_copy;
//     }

//     (void)expected_rw; /* The ICD echoes iface and addr; R/W isn???t echoed as a field???status indicates OK/ERR. */

//     *status_out = (owl_status_t)status;
//     if (status != (uint8_t)OWL_ST_OK) {
//         return OWL_ERR_STATUS;
//     }
//     return OWL_OK;
// }

// /* ---- Public API ---- */

// void camera_iface_bind_cam(owl_cam_t *cam) { g_tracker_cam = cam; }

// int owl_cam_open(owl_cam_t *cam, const char *ip4_str, uint16_t tcp_port)
// {
//     if ((cam == NULL) || (ip4_str == NULL)) {
//         return OWL_ERR_ARG;
//     }
//     if (owl_net_init() != 0) {
//         return OWL_ERR_SOCK;
//     }
//     (void)memset(cam, 0, sizeof(*cam));
//     cam->tcp_fd = -1;

//     struct sockaddr_in sa;
//     (void)memset(&sa, 0, sizeof(sa));
//     sa.sin_family = AF_INET;
//     sa.sin_port   = htons((uint16_t)((tcp_port == 0u) ? OWL_TCP_PORT : tcp_port));
//     if (inet_pton(AF_INET, ip4_str, &sa.sin_addr) != 1) {
//         return OWL_ERR_ARG;
//     }

//     const int fd = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
//     if (fd < 0) {
//         return OWL_ERR_SOCK;
//     }

//     if (connect(OWL_SOCK(fd), (const struct sockaddr *)&sa, sizeof(sa)) != 0) {
//         (void)owl_close_socket(fd);
//         return OWL_ERR_SOCK;
//     }

//     cam->tcp_fd      = fd;
//     cam->cam_ipv4_be = sa.sin_addr.s_addr;
//     cam->tcp_port_be = sa.sin_port;
//     camera_iface_bind_cam(cam);
//     return OWL_OK;
// }

// void owl_cam_close(owl_cam_t *cam)
// {
//     if ((cam != NULL) && (cam->tcp_fd >= 0)) {
//         (void)owl_close_socket(cam->tcp_fd);
//         cam->tcp_fd = -1;
//     }
// }

// int owl_cam_read(owl_cam_t *cam, owl_iface_t iface, uint16_t addr,
//                   uint8_t *data_out, size_t *data_out_len)
// {
//     if ((cam == NULL) || (cam->tcp_fd < 0)) { return OWL_ERR_ARG; }
//     if ((data_out == NULL) || (data_out_len == NULL)) { return OWL_ERR_ARG; }

//     uint8_t tx[1u + 1u + 1u + 2u + 2u + 2u];
//     size_t tx_len = 0U;
//     int rc = pack_packet(tx, sizeof(tx), OWL_RW_READ_ASCII, iface, addr, NULL, 0u, &tx_len);
//     if (rc != OWL_OK) { return rc; }

//     if (tcp_send_all(cam->tcp_fd, tx, tx_len) != 0) { return OWL_ERR_IO; }

//     uint8_t rx[7u + 2u + 2u + 8u]; /* header..MSL + up to small data + CRC; expand if needed */
//     const int n = tcp_recv_some(cam->tcp_fd, rx, sizeof(rx), 1000);
//     if (n <= 0) { return OWL_ERR_IO; }

//     owl_status_t st = OWL_ST_FAILED;
//     size_t io = *data_out_len;
//     rc = parse_resp_and_copy(rx, (size_t)n, OWL_RW_READ_ASCII, &st, data_out, &io);
//     *data_out_len = io;
//     return rc;
// }

// int owl_cam_write(owl_cam_t *cam, owl_iface_t iface, uint16_t addr,
//                    const uint8_t *data, size_t len)
// {
//     if ((cam == NULL) || (cam->tcp_fd < 0)) { return OWL_ERR_ARG; }
//     if ((data == NULL) && (len > 0u)) { return OWL_ERR_ARG; }
//     if (len > OWL_TX_MAX_DATA) { return OWL_ERR_ARG; }

//     uint8_t tx[OWL_TX_MAX_PACKET];
//     size_t tx_len = 0U;
//     const uint16_t msl = (uint16_t)len;
//     int rc = pack_packet(tx, sizeof(tx), OWL_RW_WRITE_ASCII, iface, addr, data, msl, &tx_len);
//     if (rc != OWL_OK) { return rc; }

//     if (tcp_send_all(cam->tcp_fd, tx, tx_len) != 0) { return OWL_ERR_IO; }

//     uint8_t rx[7u + 2u + 2u + 8u];
//     const int n = tcp_recv_some(cam->tcp_fd, rx, sizeof(rx), 1000);
//     if (n <= 0) { return OWL_ERR_IO; }

//     owl_status_t st = OWL_ST_FAILED;
//     /* For write, response data length is 2 per ICD; we ignore/copy it safely. */
//     uint8_t ignore[4];
//     size_t  io = sizeof(ignore);
//     rc = parse_resp_and_copy(rx, (size_t)n, OWL_RW_WRITE_ASCII, &st, ignore, &io);
//     return rc;
// }

// int owl_cam_liveliness(owl_cam_t *cam, bool *alive)
// {
//     if ((cam == NULL) || (alive == NULL)) { return OWL_ERR_ARG; }
//     uint8_t buf[4] = { 0u };
//     size_t  len = sizeof(buf);
//     const int rc = owl_cam_read(cam, OWL_IFACE_CONTROL, OWL_ADDR_LIVELINESS, buf, &len);
//     if (rc != OWL_OK) { *alive = false; return rc; }
//     /* ICD says RD returns 0x01 if success???turn2file13???ANALINEAR Software_Command_Protocol 1.pdf???L54-L55??? */
//     *alive = (len > 0u) && (buf[0] == 0x01u);
//     return OWL_OK;
// }

// int owl_cam_set_telemetry(owl_cam_t *cam, bool enable)
// {
//     if (cam == NULL) { return OWL_ERR_ARG; }
//     const uint8_t mode = enable ? 0x01u : 0x02u; /* per ICD???turn2file13???ANALINEAR Software_Command_Protocol 1.pdf???L74-L77??? */
//     return owl_cam_write(cam, OWL_IFACE_CONTROL, OWL_ADDR_TELEMETRY_DATA, &mode, 1u);
// }

// int owl_cam_restart(owl_cam_t *cam, uint8_t mode)
// {
//     if (cam == NULL) { return OWL_ERR_ARG; }
//     if ((mode != OWL_RESTART_REBOOT) &&
//         (mode != OWL_RESTART_SERVICE) &&
//         (mode != OWL_RESTART_SHUTDOWN)) {
//         return OWL_ERR_ARG;
//     }
//     return owl_cam_write(cam, OWL_IFACE_CONTROL, OWL_ADDR_GLOBAL_RESTART, &mode, (size_t)1u);
// }

// int owl_cam_day_power(owl_cam_t *cam, uint8_t mode)
// {
//     if (cam == NULL) { return OWL_ERR_ARG; }
//     if ((mode != OWL_POWER_ON) && (mode != OWL_POWER_OFF) && (mode != OWL_POWER_REBOOT)) {
//         return OWL_ERR_ARG;
//     }
//     return owl_cam_write(cam, OWL_IFACE_CONTROL, OWL_ADDR_DAY_POWER, &mode, (size_t)1u);
// }

// int owl_cam_thermal_power(owl_cam_t *cam, uint8_t mode)
// {
//     if (cam == NULL) { return OWL_ERR_ARG; }
//     if ((mode != OWL_POWER_ON) && (mode != OWL_POWER_OFF) && (mode != OWL_POWER_REBOOT)) {
//         return OWL_ERR_ARG;
//     }
//     return owl_cam_write(cam, OWL_IFACE_CONTROL, OWL_ADDR_THERMAL_POWER, &mode, (size_t)1u);
// }

// int owl_cam_lrf_power(owl_cam_t *cam, uint8_t mode)
// {
//     if (cam == NULL) { return OWL_ERR_ARG; }
//     if ((mode != OWL_POWER_OFF) && (mode != OWL_POWER_REBOOT)) {
//         return OWL_ERR_ARG;
//     }
//     return owl_cam_write(cam, OWL_IFACE_CONTROL, OWL_ADDR_LRF_POWER, &mode, (size_t)1u);
// }

// int owl_cam_lrf_set_frequency(owl_cam_t *cam, uint8_t freq_mode)
// {
//     if (cam == NULL) {
//         return OWL_ERR_ARG;
//     }

//     if ((freq_mode < OWL_LRF_FREQ_1HZ) ||
//         (freq_mode > OWL_LRF_FREQ_200HZ)) {
//         return OWL_ERR_ARG;
//     }

//     return owl_cam_write(
//         cam,
//         OWL_IFACE_LRF,
//         OWL_ADDR_LRF_CONTINUOUS_RANGE,
//         &freq_mode,
//         1
//     );
// }

// int owl_cam_lrf_stop(owl_cam_t *cam)
// {
//     uint8_t cmd = 0x01;

//     if (cam == NULL) {
//         return OWL_ERR_ARG;
//     }

//     return owl_cam_write(
//         cam,
//         OWL_IFACE_LRF,
//         OWL_ADDR_LRF_BREAK_CMD,
//         &cmd,
//         1
//     );
// }

// int owl_cam_day_zoom(owl_cam_t *cam, uint8_t zoom_mode, uint8_t direction)
// {
//     uint8_t payload[3];
//     if (cam == NULL) { return OWL_ERR_ARG; }
//     if ((zoom_mode != OWL_ZOOM_MODE_CONTINUOUS) && (zoom_mode != OWL_ZOOM_MODE_MANUAL)) {
//         return OWL_ERR_ARG;
//     }
//     if ((direction != OWL_ZOOM_IN) && (direction != OWL_ZOOM_OUT)) {
//         return OWL_ERR_ARG;
//     }

//     payload[0] = zoom_mode;
//     payload[1] = direction;
//     payload[2] = 0x01u;
//     return owl_cam_write(cam, OWL_IFACE_DAY, OWL_ADDR_ZOOM_CTRL, payload, (size_t)3u);
// }

// int owl_cam_day_zoom_stop(owl_cam_t *cam)
// {
//     const uint8_t payload = 0x01u;
//     if (cam == NULL) { return OWL_ERR_ARG; }
//     return owl_cam_write(cam, OWL_IFACE_DAY, OWL_ADDR_ZOOM_STOP, &payload, (size_t)1u);
// }

// int owl_cam_thermal_zoom(owl_cam_t *cam, uint8_t zoom_mode, uint8_t direction)
// {
//     uint8_t payload[3];
//     if (cam == NULL) { return OWL_ERR_ARG; }
//     if ((zoom_mode != OWL_ZOOM_MODE_CONTINUOUS) && (zoom_mode != OWL_ZOOM_MODE_MANUAL)) {
//         return OWL_ERR_ARG;
//     }
//     if ((direction != OWL_ZOOM_IN) && (direction != OWL_ZOOM_OUT)) {
//         return OWL_ERR_ARG;
//     }

//     payload[0] = zoom_mode;
//     payload[1] = direction;
//     payload[2] = 0x01u;
//     return owl_cam_write(cam, OWL_IFACE_THERMAL, OWL_ADDR_ZOOM_CTRL, payload, (size_t)3u);
// }

// int owl_cam_thermal_zoom_stop(owl_cam_t *cam)
// {
//     const uint8_t payload = 0x01u;
//     if (cam == NULL) { return OWL_ERR_ARG; }
//     return owl_cam_write(cam, OWL_IFACE_THERMAL, OWL_ADDR_ZOOM_STOP, &payload, (size_t)1u);
// }

// int camera_telem_open(owl_telem_t *ctx, const char *bind_ip, uint16_t port)
// {
//     if (ctx == NULL) { return -1; }
//     if (owl_net_init() != 0) { return -2; }
//     (void)memset(ctx, 0, sizeof(*ctx));
//     ctx->udp_fd = -1;

//     const uint16_t use_port = (port == 0U) ? OWL_UDP_TELEM_PORT : port;

//     const int fd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
//     if (fd < 0) { return -2; }

//     /* Allow quick rebind */
//     int yes = 1;
//     (void)setsockopt(OWL_SOCK(fd), SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, (socklen_t)sizeof(yes));

//     struct sockaddr_in sa;
//     (void)memset(&sa, 0, sizeof(sa));
//     sa.sin_family      = AF_INET;
//     sa.sin_port        = htons(use_port);
//     sa.sin_addr.s_addr = (bind_ip != NULL) ? inet_addr(bind_ip) : htonl(INADDR_ANY);
//     if (bind(OWL_SOCK(fd), (const struct sockaddr *)&sa, sizeof(sa)) != 0) {
//         (void)owl_close_socket(fd);
//         return -3;
//     }

//     ctx->udp_fd = fd;
//     ctx->port   = use_port;
//     log_info("Telemetry UDP bound on %u", (unsigned)use_port);
//     return 0;
// }

// void camera_telem_close(owl_telem_t *ctx)
// {
//     if ((ctx != NULL) && (ctx->udp_fd >= 0)) {
//         (void)owl_close_socket(ctx->udp_fd);
//         ctx->udp_fd = -1;
//     }
// }

// /* --- parse one 168-byte payload into struct --- */
// /* Accept either raw 168B payload, or wrapped packet with 7B header:
//    [0]=0xCC,[1..5]=00 00 00 04 00,[6]=ByteCount(0xA8), then 168B payload.
//    Some camera builds append 2 CRC bytes (total 177B).
// */
// static int parse_telem(const uint8_t *buf, size_t len, owl_telem_frame_t *out)
// {
//     if ((buf == NULL) || (out == NULL)) { return -1; }

//     const uint8_t *p    = buf;
//     size_t payload_len = 0U;

//     /* Wrapped stream: [7B header][168B payload][optional trailing bytes/CRC]. */
//     if ((len >= (size_t)175U) && (buf[0] == 0xCCu)) {
//         p = &buf[7];
//         payload_len = (size_t)168U;
//     /* Raw payload stream: first 168B is payload. */
//     } else if (len >= (size_t)168U) {
//         p = buf;
//         payload_len = (size_t)168U;
//     } else {
//         return -1;
//     }

//     if ((payload_len != (size_t)168U) || ((size_t)(p - buf) + payload_len > len)) {
//         return -1;
//     }

//     /* Offsets per 168B payload map:
//        1 (ver) + 10 (PT) + 5(LRF) + 20(Thermal) + 20(Day) + 20(Jaguar)
//        + 20(Tracker) + 20(FactoryRsv) + 2(Temp) + 45(GPS) + 5(Pwr) = 168
//     */
//     size_t o = 0U;

//     /* Version (1) */
//     out->version = p[o]; o += 1U;

//     /* PT (10) reserved */
//     o += 10U;

//     /* LRF (5): 2B big-endian range, 3B reserved */
//     out->lrf_range = rd_u16_be(&p[o]); o += 5U;

//     /* Thermal (20): X(2 BE), Y(2 BE), FOVcode(1), FOVdeg(4 LE float), NUC(1), rsv(10) */
//     out->thermal_ret_x    = rd_u16_be(&p[o]); o += 2U;
//     out->thermal_ret_y    = rd_u16_be(&p[o]); o += 2U;
//     out->thermal_fov_code = p[o];             o += 1U;
//     out->thermal_fov_deg  = rd_f32_le(&p[o]); o += 4U;
//     out->thermal_nuc_status = p[o];           o += 1U;
//     o += 10U;

//     /* Day (20): X(2 BE), Y(2 BE), FOVcode(1), FOVdeg(4 LE float), rsv(11) */
//     out->day_ret_x   = rd_u16_be(&p[o]); o += 2U;
//     out->day_ret_y   = rd_u16_be(&p[o]); o += 2U;
//     out->day_fov_code= p[o];             o += 1U;
//     out->day_fov_deg = rd_f32_le(&p[o]); o += 4U;
//     o += 11U;

//     /* Jaguar (20) reserved */
//     o += 20U;

//     /* Tracker (20): mode(1), X(2), Y(2), BB_W(2), BB_H(2), PAN(2), TILT(2), rsv(7) */
//     out->tracker_mode    = p[o];           o += 1U;
//     out->tracker_ret_x   = rd_u16_be(&p[o]); o += 2U;
//     out->tracker_ret_y   = rd_u16_be(&p[o]); o += 2U;
//     out->tracker_bb_w    = rd_u16_be(&p[o]); o += 2U;
//     out->tracker_bb_h    = rd_u16_be(&p[o]); o += 2U;
//     out->tracker_pan_err = rd_u16_be(&p[o]); o += 2U;
//     out->tracker_tilt_err= rd_u16_be(&p[o]); o += 2U;
//     o += 7U; /* reserved */

//     /* Factory Reserved (20) */
//     o += 20U;

//     /* Temperature (2): hundredths of degree C, BE */
//     out->sys_temp = rd_u16_be(&p[o]); o += 2U;

//     /* GPS (45):
//        lat(8 LE double) + NS(1 ASCII) + lon(8 LE double) + EW(1 ASCII)
//        + alt(4 LE float) + sats(1) + qos(1) + rsv(21)
//     */
//     out->lat   = rd_f64_le(&p[o]); o += 8U;
//     out->NSdir = p[o];             o += 1U;
//     out->lon   = rd_f64_le(&p[o]); o += 8U;
//     out->EWdir = p[o];             o += 1U;
//     out->alt   = rd_f32_le(&p[o]); o += 4U;
//     out->sat   = p[o];             o += 1U;
//     out->gpsQOS= p[o];             o += 1U;
//     o += 21U;

//     /* Power (5): day, thermal, plcb, lrf, overall-watts */
//     out->pwr_day_on     = (p[o + 0U] != 0U) ? 1U : 0U;
//     out->pwr_thermal_on = (p[o + 1U] != 0U) ? 1U : 0U;
//     out->pwr_plcb_on    = (p[o + 2U] != 0U) ? 1U : 0U;
//     out->pwr_lrf_on     = (p[o + 3U] != 0U) ? 1U : 0U;
//     out->pwr_overall    =  p[o + 4U];
//     /* o += 5U;  // not needed */

//     return 0;
// }

// int camera_telem_recv(owl_telem_t *ctx, owl_telem_frame_t *out, int timeout_ms)
// {
//     static uint32_t s_rx_ok = 0U;
//     static uint32_t s_rx_bad = 0U;

//     if ((ctx == NULL) || (out == NULL) || (ctx->udp_fd < 0)) {
//         return -1;
//     }

//     /* Wait with select() if requested */
//     if (timeout_ms != 0) {
//         fd_set rfds;
//         FD_ZERO(&rfds);
//         FD_SET(OWL_SOCK(ctx->udp_fd), &rfds);
//         struct timeval tv;
//         if (timeout_ms > 0) {
//             tv.tv_sec  = (timeout_ms / 1000);
//             tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
//         } else {
//             tv.tv_sec = 0; tv.tv_usec = 0; /* not used on block */
//         }
//         const int sel = (timeout_ms > 0)
//                         ? select(ctx->udp_fd + 1, &rfds, NULL, NULL, &tv)
//                         : select(ctx->udp_fd + 1, &rfds, NULL, NULL, NULL);
//         if (sel == 0) { return 0; }          /* timeout */
//         if (sel < 0)  { return -2; }         /* select error */
//     }

//     uint8_t buf[2048];
//     const int n = (int)recvfrom(OWL_SOCK(ctx->udp_fd), (char *)buf, (int)sizeof(buf), 0, NULL, NULL);
//     if (n < 0) {
//         const int ws_err = WSAGetLastError();
//         if (ws_err == WSAEWOULDBLOCK) { return 0; }
//         return -3;
//     }

//     if (n < 168) {
//         s_rx_bad++;
//         if ((s_rx_bad % 50U) == 1U) {
//             log_warning("Telemetry short frame len=%d", n);
//         }
//         return -4;
//     }

//     if (parse_telem(buf, (size_t)n, out) == 0) {
//         s_rx_ok++;
//         if ((s_rx_ok % 100U) == 1U) {
//             log_info("Telemetry frame parsed len=%d", n);
//         }
//         return 1;
//     }

//     s_rx_bad++;
//     if ((s_rx_bad % 50U) == 1U) {
//         log_warning("Telemetry parse fail len=%d b0=%02X b1=%02X b2=%02X b3=%02X", n,
//                     (unsigned)buf[0], (unsigned)buf[1], (unsigned)buf[2], (unsigned)buf[3]);
//     }
//     return -5;
// }

// void camera_telem_publish(const camera_params_t *cam, const owl_telem_frame_t *fr)
// {
//     if ((cam == NULL) || (fr == NULL)) { return; }

//     const char *root = (cam->root_topic[0] != '\0') ? cam->root_topic : "OWL";

//     char tmp[48];

//     /* Version & LRF */
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->version);
//     pub_kv(root, "VERSION", tmp);

//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->lrf_range);
//     pub_kv(root, "LRF_RANGE", tmp);

//     /* Thermal */
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->thermal_ret_x);
//     pub_kv(root, "THERMAL/RETICLE_X", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->thermal_ret_y);
//     pub_kv(root, "THERMAL/RETICLE_Y", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->thermal_fov_code);
//     pub_kv(root, "THERMAL/FOV_CODE", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%.4f", (double)fr->thermal_fov_deg);
//     pub_kv(root, "THERMAL/FOV_DEG", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->thermal_nuc_status);
//     pub_kv(root, "THERMAL/NUC", tmp);

//     /* Day */
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->day_ret_x);
//     pub_kv(root, "DAY/RETICLE_X", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->day_ret_y);
//     pub_kv(root, "DAY/RETICLE_Y", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->day_fov_code);
//     pub_kv(root, "DAY/FOV_CODE", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%.4f", (double)fr->day_fov_deg);
//     pub_kv(root, "DAY/FOV_DEG", tmp);

//     /* Tracker */
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_mode);
//     pub_kv(root, "TRACKER/MODE", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_ret_x);
//     pub_kv(root, "TRACKER/RETICLE_X", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_ret_y);
//     pub_kv(root, "TRACKER/RETICLE_Y", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_bb_h);
//     pub_kv(root, "TRACKER/BB_H", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_bb_w);
//     pub_kv(root, "TRACKER/BB_W", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_pan_err);
//     pub_kv(root, "TRACKER/PAN_ERR", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->tracker_tilt_err);
//     pub_kv(root, "TRACKER/TILT_ERR", tmp);

//     /* Temperatures */
//     float tC = ((float)fr->sys_temp) / 100.0f;
//     (void)snprintf(tmp, sizeof(tmp), "%.2f", (double)tC);
//     pub_kv(root, "TEMP/SYSTEM_C", tmp);

//     /* GPS */
//     const char ns = (char)fr->NSdir; /* 'N' or 'S' */
//     const char ew = (char)fr->EWdir; /* 'E' or 'W' */

//     double lat = fr->lat;
//     double lon = fr->lon;
//     if (ns == 'S') { lat = -lat; }
//     if (ew == 'W') { lon = -lon; }

//     (void)snprintf(tmp, sizeof(tmp), "%.8f", lat);
//     pub_kv(root, "GPS/LAT", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%.8f", lon);
//     pub_kv(root, "GPS/LON", tmp);

//     /* Also publish the raw ASCII directions explicitly */
//     tmp[0] = (ns == 'S' || ns == 'N') ? ns : '?'; tmp[1] = '\0';
//     pub_kv(root, "GPS/NS", tmp);
//     tmp[0] = (ew == 'E' || ew == 'W') ? ew : '?'; tmp[1] = '\0';
//     pub_kv(root, "GPS/EW", tmp);

//     (void)snprintf(tmp, sizeof(tmp), "%.2f", (double)fr->alt);
//     pub_kv(root, "GPS/ALT_M", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->sat);
//     pub_kv(root, "GPS/SATS", tmp);
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->gpsQOS);
//     pub_kv(root, "GPS/QOS", tmp);

//     /* Power */
//     pub_kv(root, "POWER/DAY",     (fr->pwr_day_on     != 0U) ? "true" : "false");
//     pub_kv(root, "POWER/THERMAL", (fr->pwr_thermal_on != 0U) ? "true" : "false");
//     pub_kv(root, "POWER/PLCB",    (fr->pwr_plcb_on    != 0U) ? "true" : "false");
//     pub_kv(root, "POWER/LRF",     (fr->pwr_lrf_on     != 0U) ? "true" : "false");
//     (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)fr->pwr_overall);
//     pub_kv(root, "POWER/OVERALL", tmp);

// }

// void camera_iface_set_udp_mcast(bool enabled, uint8_t camera_id, uint32_t *seq_counter)
// {
//     g_udp_mcast_enabled = (enabled != false) ? 1 : 0;
//     g_udp_camera_id = camera_id;
//     g_udp_seq_counter = seq_counter;
// }


// /* ---- Writer helpers (byte-accurate to Table 10) ---- */

// int owl_tracker_set_coord(uint8_t cam, uint16_t x, uint16_t y, uint8_t mode)
// {
//     uint8_t b[6];
//     b[0]=cam; b[1]=(uint8_t)(x>>8); b[2]=(uint8_t)(x&0xFFu);
//     b[3]=(uint8_t)(y>>8); b[4]=(uint8_t)(y&0xFFu); b[5]=mode;
//     return owl_cam_write(g_tracker_cam, IFACE_TRACKER, 0x0001u, b, (size_t)6);
// }

// int owl_tracker_set_box(uint8_t cam, uint8_t w, uint8_t h)
// {
//     uint8_t b[3]; b[0]=cam; b[1]=w; b[2]=h;
//     return owl_cam_write(g_tracker_cam, IFACE_TRACKER, 0x0002u, b, (size_t)3);
// }

// int owl_tracker_set_mode(uint8_t cam, uint8_t mode)
// {
//     uint8_t b[2]; b[0]=cam; b[1]=mode;
//     return owl_cam_write(g_tracker_cam, IFACE_TRACKER, 0x0003u, b, (size_t)2);
// }

// int owl_tracker_set_lock(uint8_t cam, uint8_t mode)
// {
//     uint8_t b[2]; b[0]=cam; b[1]=mode;
//     return owl_cam_write(g_tracker_cam, IFACE_TRACKER, 0x0004u, b, (size_t)2);
// }

// int owl_tracker_set_stab(uint8_t cam, uint8_t mode)
// {
//     uint8_t b[2]; b[0]=cam; b[1]=mode;
//     return owl_cam_write(g_tracker_cam, IFACE_TRACKER, 0x0005u, b, (size_t)2);
// }

// int owl_tracker_set_szone(uint8_t cam, uint8_t zone)
// {
//     uint8_t b[2]; b[0]=cam; b[1]=zone;
//     return owl_cam_write(g_tracker_cam, IFACE_TRACKER, 0x0006u, b, (size_t)2);
// }

// int owl_tracker_set_auto(uint8_t cam, uint8_t mode, uint8_t sens)
// {
//     uint8_t b[3]; b[0]=cam; b[1]=mode; b[2]=sens;
//     return owl_cam_write(g_tracker_cam, IFACE_TRACKER, 0x0008u, b, (size_t)3);
// }

// int owl_tracker_set_roi(uint8_t cam, uint8_t mode)
// {
//     uint8_t b[2]; b[0]=cam; b[1]=mode;
//     return owl_cam_write(g_tracker_cam, IFACE_TRACKER, 0x0009u, b, (size_t)2);
// }

// int owl_tracker_set_bitrate_centi(uint16_t centi_mbps)
// {
//     uint8_t b[2]; b[0]=(uint8_t)(centi_mbps>>8); b[1]=(uint8_t)(centi_mbps&0xFFu);
//     return owl_cam_write(g_tracker_cam, IFACE_TRACKER, 0x000Cu, b, (size_t)2);
// }

// int owl_tracker_set_maxfail(uint8_t cam, uint8_t code)
// {
//     uint8_t b[2]; b[0]=cam; b[1]=code;
//     return owl_cam_write(g_tracker_cam, IFACE_TRACKER, 0x000Du, b, (size_t)2);
// }

// int owl_tracker_set_type(uint8_t cam, uint8_t type_code)
// {
//     uint8_t b[2]; b[0]=cam; b[1]=type_code;
//     return owl_cam_write(g_tracker_cam, IFACE_TRACKER, 0x000Fu, b, (size_t)2);
// }

// int owl_tracker_set_search_area(uint8_t cam, uint8_t percent)
// {
//     uint8_t b[2]; b[0]=cam; b[1]=percent;
//     return owl_cam_write(g_tracker_cam, IFACE_TRACKER, 0x0010u, b, (size_t)2);
// }

// int owl_tracker_set_confidence(uint8_t cam, uint8_t tenths)
// {
//     uint8_t b[2]; b[0]=cam; b[1]=tenths;
//     return owl_cam_write(g_tracker_cam, IFACE_TRACKER, 0x0011u, b, (size_t)2);
// }

// /* ---- Read-all: fetch each register, fill structured state ---- */
// int owl_tracker_read_all(tracker_state_t *st)
// {
//     if (st == NULL) { return OWL_ERR_ARG; }
//     /* Helper macro for compact read+length check */
//     #define RD(_addr, _buf, _len)
//         do { size_t _io=(size_t)(_len); int _rc = owl_cam_read(g_tracker_cam, IFACE_TRACKER, (_addr), (_buf), &_io);
//              if ((_rc != OWL_OK) || (_io != (size_t)(_len))) { return (_rc != OWL_OK) ? _rc : OWL_ERR_IO; } } while (0)

//     /* 0x0001: CAM|X(2)|Y(2)|MODE */
//     { uint8_t b[6]; RD(0x0001u, b, 6);
//       st->coord_cam_id=b[0]; st->coord_x=((uint16_t)b[1]<<8)|b[2]; st->coord_y=((uint16_t)b[3]<<8)|b[4]; st->coord_mode=b[5]; }

//     /* 0x0002: CAM|W|H */
//     { uint8_t b[3]; RD(0x0002u, b, 3);
//       st->box_cam_id=b[0]; st->box_w=b[1]; st->box_h=b[2]; }

//     /* 0x0003: CAM|MODE */
//     { uint8_t b[2]; RD(0x0003u, b, 2);
//       st->tmode_cam_id=b[0]; st->tmode=b[1]; }

//     /* 0x0004: CAM|MODE */
//     { uint8_t b[2]; RD(0x0004u, b, 2);
//       st->lock_cam_id=b[0]; st->lock_mode=b[1]; }

//     /* 0x0005: CAM|MODE */
//     { uint8_t b[2]; RD(0x0005u, b, 2);
//       st->stab_cam_id=b[0]; st->stab_mode=b[1]; }

//     /* 0x0006: CAM|ZONE */
//     { uint8_t b[2]; RD(0x0006u, b, 2);
//       st->szone_cam_id=b[0]; st->szone_zone=b[1]; }

//     /* 0x0008: CAM|MODE|SENS */
//     { uint8_t b[3]; RD(0x0008u, b, 3);
//       st->ad_cam_id=b[0]; st->ad_mode=b[1]; st->ad_sens=b[2]; }

//     /* 0x0009: CAM|MODE */
//     { uint8_t b[2]; RD(0x0009u, b, 2);
//       st->roi_cam_id=b[0]; st->roi_mode=b[1]; }

//     /* 0x000C: BR (u16 BE) */
//     { uint8_t b[2]; RD(0x000Cu, b, 2);
//       st->enc_br_centi=((uint16_t)b[0]<<8)|b[1]; }

//     /* 0x000D: CAM|CODE */
//     { uint8_t b[2]; RD(0x000Du, b, 2);
//       st->mfail_cam_id=b[0]; st->mfail_code=b[1]; }

//     /* 0x000F: CAM|TYPE (optional) */
//     { uint8_t b[2]; RD(0x000Fu, b, 2);
//       st->type_cam_id=b[0]; st->type_code=b[1]; }

//     /* 0x0010: CAM|PERCENT (optional) */
//     { uint8_t b[2]; RD(0x0010u, b, 2);
//       st->sarea_cam_id=b[0]; st->sarea_percent=b[1]; }

//     /* 0x0011: CAM|CONF_TENTHS (optional) */
//     { uint8_t b[2]; RD(0x0011u, b, 2);
//       st->conf_cam_id=b[0]; st->conf_tenths=b[1]; }

//     #undef RD
//     return OWL_OK;
// }

void owl_cam_vlc_stop(void)
{
    if (g_vlc_pid > 0) {
        int status = 0;
        int attempts = 0;

        (void)kill(g_vlc_pid, SIGTERM);
        while ((attempts < 30) && (waitpid(g_vlc_pid, &status, WNOHANG) == 0)) {
            platform_sleep_ms(100u);
            attempts++;
        }

        if (waitpid(g_vlc_pid, &status, WNOHANG) == 0) {
            (void)kill(g_vlc_pid, SIGKILL);
            (void)waitpid(g_vlc_pid, &status, 0);
        }

        g_vlc_pid = -1;
    }

    g_vlc_started = 0;
}
int owl_cam_vlc_switch_stream(const char *vlc_path, const char *rtsp_url)
{
    char resolved_vlc_path[PATH_MAX];
    const char *launch_target = VLC_EXE_PATH;

    if ((rtsp_url == NULL) || (rtsp_url[0] == '\0')) {
        return OWL_ERR_ARG;
    }

    /* Stop any running VLC before launching the next stream. */
    owl_cam_vlc_stop();
    platform_sleep_ms(200u);

    if (owl_resolve_vlc_path(vlc_path, resolved_vlc_path, sizeof(resolved_vlc_path)) == 0) {
        launch_target = resolved_vlc_path;
    }

    {
        pid_t child = -1;
        char *const argv[] = {
            (char *)launch_target,
            (char *)rtsp_url,
            NULL
        };

        if (posix_spawnp(&child, launch_target, NULL, NULL, argv, environ) != 0) {
            return OWL_ERR_IO;
        }

        g_vlc_pid = child;
    }

    g_vlc_started = 1;
    return OWL_OK;
}
int owl_cam_get_rtsp_url(owl_cam_t *cam, char *out, size_t out_sz)
{
    uint8_t rx[256];
    size_t len = sizeof(rx);
    int rc;

    if ((cam == NULL) || (out == NULL) || (out_sz == 0u)) {
        return OWL_ERR_ARG;
    }

    rc = owl_cam_read(cam, OWL_IFACE_CONTROL, OWL_ADDR_RTSP_URL, rx, &len);
    if (rc != OWL_OK) {
        return rc;
    }

    if (len >= out_sz) {
        len = out_sz - 1u;
    }

    memcpy(out, rx, len);
    out[len] = '\0';
    return OWL_OK;
}

int owl_cam_parse_rtsp_urls(const char *rtsp_blob, owl_rtsp_urls_t *urls)
{
    char temp[256];
    char *saveptr = NULL;
    char *token;

    if ((rtsp_blob == NULL) || (urls == NULL) || (rtsp_blob[0] == '\0')) {
        return OWL_ERR_ARG;
    }

    memset(urls, 0, sizeof(*urls));

    strncpy(temp, rtsp_blob, sizeof(temp) - 1u);
    temp[sizeof(temp) - 1u] = '\0';

    token = strtok_r(temp, ";", &saveptr);
    if (token == NULL) {
        return OWL_ERR_PROTO;
    }
    strncpy(urls->thermal, token, sizeof(urls->thermal) - 1u);

    token = strtok_r(NULL, ";", &saveptr);
    if (token == NULL) {
        return OWL_ERR_PROTO;
    }
    strncpy(urls->day, token, sizeof(urls->day) - 1u);

    token = strtok_r(NULL, ";", &saveptr);
    if (token == NULL) {
        return OWL_ERR_PROTO;
    }
    strncpy(urls->low_light, token, sizeof(urls->low_light) - 1u);

    urls->valid = 1;
    return OWL_OK;
}
int owl_cam_open_thermal_stream(owl_cam_t *cam, const char *vlc_path)
{
    char blob[256];
    owl_rtsp_urls_t urls;
    int rc;

    rc = owl_cam_get_rtsp_url(cam, blob, sizeof(blob));
    if (rc != OWL_OK) {
        return rc;
    }

    rc = owl_cam_parse_rtsp_urls(blob, &urls);
    if (rc != OWL_OK) {
        return rc;
    }

    return owl_cam_vlc_switch_stream(vlc_path, urls.thermal);
}

int owl_cam_open_day_stream(owl_cam_t *cam, const char *vlc_path)
{
    char blob[256];
    owl_rtsp_urls_t urls;
    int rc;

    rc = owl_cam_get_rtsp_url(cam, blob, sizeof(blob));
    if (rc != OWL_OK) {
        return rc;
    }

    rc = owl_cam_parse_rtsp_urls(blob, &urls);
    if (rc != OWL_OK) {
        return rc;
    }

    return owl_cam_vlc_switch_stream(vlc_path, urls.day);
}

int owl_cam_open_lowlight_stream(owl_cam_t *cam, const char *vlc_path)
{
    char blob[256];
    owl_rtsp_urls_t urls;
    int rc;

    rc = owl_cam_get_rtsp_url(cam, blob, sizeof(blob));
    if (rc != OWL_OK) {
        return rc;
    }

    rc = owl_cam_parse_rtsp_urls(blob, &urls);
    if (rc != OWL_OK) {
        return rc;
    }

    return owl_cam_vlc_switch_stream(vlc_path, urls.low_light);
}
