#include "api2_mcast_win.h"
#include "crc16_ccitt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#error This project is Windows-only.
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#define API2_SOCK(fd_) ((SOCKET)(uintptr_t)(fd_))
static int api2_net_init(void)
{
    static int inited = 0;
    if (inited == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return -1;
        }
        inited = 1;
    }
    return 0;
}
static int api2_sock_close(int fd) { return (int)closesocket(API2_SOCK(fd)); }
static int api2_set_nonblock(int fd)
{
    u_long mode = 1UL;
    return (int)ioctlsocket(API2_SOCK(fd), (long)FIONBIO, &mode);
}
static int api2_is_wouldblock(void) { return (WSAGetLastError() == WSAEWOULDBLOCK) ? 1 : 0; }

#define API2_MAX_PACKET 512
#define API2_CAM1_PAYLOAD_SIZE 34u

/*
 * Primary multicast packet roles:
 * - CAN1: raw joystick CAN/J1939 frame
 * - CMD1: UART/business-logic action/event packet
 * - CAM1: compact periodic camera state
 * - LED1: LED output/status bitmap
 * - TLM1: telemetry/debug string topic/value
 */

typedef struct {
    int initialized;
    int sock;
    struct sockaddr_in group_addr;
} api2_state_t;

static api2_state_t g_api2 = {0};

static void api2_le16_write(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void api2_le32_write(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint16_t api2_le16_read(const uint8_t* p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t api2_le32_read(const uint8_t* p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void api2_f32_le_write(uint8_t* p, float v)
{
    uint32_t u = 0u;
    (void)memcpy((void*)&u, (const void*)&v, sizeof(u));
    api2_le32_write(p, u);
}

static float api2_f32_le_read(const uint8_t* p)
{
    uint32_t u = api2_le32_read(p);
    float v = 0.0f;
    (void)memcpy((void*)&v, (const void*)&u, sizeof(v));
    return v;
}

static size_t api2_append_crc16(uint8_t* buf, size_t len_before_crc)
{
    uint16_t crc = crc16_ccitt_false(buf, len_before_crc);
    buf[len_before_crc] = (uint8_t)(crc & 0x00FFu);
    buf[len_before_crc + 1u] = (uint8_t)((crc >> 8) & 0x00FFu);
    return len_before_crc + 2u;
}

static int api2_peek_packet(uint8_t* pkt, int* rcv_out)
{
    struct sockaddr_in src_addr;
    socklen_t src_len = (socklen_t)sizeof(src_addr);
    int rcv = (int)recvfrom(API2_SOCK(g_api2.sock),
                            (char*)pkt,
                            API2_MAX_PACKET,
                            MSG_PEEK,
                            (struct sockaddr*)&src_addr,
                            &src_len);

    if (rcv < 0) {
        if (api2_is_wouldblock() != 0) {
            return 0;
        }
        return -1;
    }

    *rcv_out = (int)rcv;
    return 1;
}

static int api2_consume_packet(void)
{
    uint8_t tmp[API2_MAX_PACKET];
    struct sockaddr_in src_addr;
    socklen_t src_len = (socklen_t)sizeof(src_addr);
    int rcv = (int)recvfrom(API2_SOCK(g_api2.sock),
                            (char*)tmp,
                            API2_MAX_PACKET,
                            0,
                            (struct sockaddr*)&src_addr,
                            &src_len);
    if (rcv >= 0) {
        return 1;
    }
    if (api2_is_wouldblock() != 0) {
        return 0;
    }
    return -1;
}

static int api2_parse_ipv4(const char* ip, struct in_addr* out)
{
    if ((ip == NULL) || (out == NULL)) {
        return -1;
    }
    return (inet_pton(AF_INET, ip, out) == 1) ? 0 : -1;
}

static int api2_send_packet(const uint8_t* pkt, size_t pkt_len)
{
    int sent;

    if (!g_api2.initialized) {
        return -1;
    }

    sent = (int)sendto(API2_SOCK(g_api2.sock),
                       (const char*)pkt,
                       (int)pkt_len,
                       0,
                       (const struct sockaddr*)&g_api2.group_addr,
                       (socklen_t)sizeof(g_api2.group_addr));
    return (sent >= 0) ? (int)sent : -1;
}

int api2_init(const api2_cfg_t* cfg)
{
    struct sockaddr_in bind_addr;
    struct ip_mreq mreq;
    struct in_addr iface_addr;
    unsigned char ttl;
    unsigned char loop;
    int reuse = 1;
    if (cfg == NULL) {
        return -3;
    }

    if (g_api2.initialized) {
        return 0;
    }

    memset(&g_api2, 0, sizeof(g_api2));
    g_api2.sock = -1;
    if (api2_net_init() != 0) {
        return -2;
    }

    g_api2.sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (g_api2.sock < 0) {
        return -2;
    }

    if (setsockopt(API2_SOCK(g_api2.sock), SOL_SOCKET, SO_REUSEADDR,
                   (const void*)&reuse, (socklen_t)sizeof(reuse)) != 0) {
        api2_shutdown();
        return -2;
    }

    memset(&g_api2.group_addr, 0, sizeof(g_api2.group_addr));
    g_api2.group_addr.sin_family = AF_INET;
    g_api2.group_addr.sin_port = htons(cfg->port);
    if (api2_parse_ipv4(cfg->group_ip, &g_api2.group_addr.sin_addr) != 0) {
        api2_shutdown();
        return -3;
    }

    ttl = cfg->ttl;
    if (setsockopt(API2_SOCK(g_api2.sock), IPPROTO_IP, IP_MULTICAST_TTL,
                   (const void*)&ttl, (socklen_t)sizeof(ttl)) != 0) {
        api2_shutdown();
        return -3;
    }

    loop = (unsigned char)(cfg->loopback ? 1u : 0u);
    if (setsockopt(API2_SOCK(g_api2.sock), IPPROTO_IP, IP_MULTICAST_LOOP,
                   (const void*)&loop, (socklen_t)sizeof(loop)) != 0) {
        api2_shutdown();
        return -3;
    }

    if (strcmp(cfg->iface_ip, "0.0.0.0") != 0) {
        if (api2_parse_ipv4(cfg->iface_ip, &iface_addr) != 0) {
            api2_shutdown();
            return -3;
        }
        if (setsockopt(API2_SOCK(g_api2.sock), IPPROTO_IP, IP_MULTICAST_IF,
                       (const void*)&iface_addr, (socklen_t)sizeof(iface_addr)) != 0) {
            api2_shutdown();
            return -3;
        }
    }

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(cfg->port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(API2_SOCK(g_api2.sock), (const struct sockaddr*)&bind_addr, (socklen_t)sizeof(bind_addr)) != 0) {
        api2_shutdown();
        return -3;
    }

    if (api2_set_nonblock(g_api2.sock) != 0) {
        api2_shutdown();
        return -3;
    }

    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr = g_api2.group_addr.sin_addr;
    if (strcmp(cfg->iface_ip, "0.0.0.0") == 0) {
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    } else {
        if (api2_parse_ipv4(cfg->iface_ip, &mreq.imr_interface) != 0) {
            api2_shutdown();
            return -4;
        }
    }

    if (setsockopt(API2_SOCK(g_api2.sock), IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   (const void*)&mreq, (socklen_t)sizeof(mreq)) != 0) {
        api2_shutdown();
        return -4;
    }

    g_api2.initialized = 1;
    return 0;
}

void api2_shutdown(void)
{
    if (g_api2.sock >= 0) {
        (void)api2_sock_close(g_api2.sock);
        g_api2.sock = -1;
    }
    memset(&g_api2, 0, sizeof(g_api2));
}

int api2_send_can_frame(
        uint32_t can_id,
        uint8_t is_ext,
        uint8_t dlc,
        const uint8_t* data)
{
    uint8_t pkt[20];
    size_t pkt_len;

    if (!g_api2.initialized) {
        return -1;
    }

    if ((dlc > 0u) && (data == NULL)) {
        return -1;
    }

    if (dlc > 8u) {
        return -1;
    }

    pkt[0] = 'C';
    pkt[1] = 'A';
    pkt[2] = 'N';
    pkt[3] = '1';
    api2_le32_write(&pkt[4], can_id);
    pkt[8] = (uint8_t)((is_ext != 0u) ? 1u : 0u);
    pkt[9] = dlc;
    memset(&pkt[10], 0, 8u);
    if (dlc > 0u) {
        memcpy(&pkt[10], data, dlc);
    }
    pkt_len = api2_append_crc16(pkt, 18u);
    return api2_send_packet(pkt, pkt_len);
}

int api2_send_cmd1(
        uint32_t seq,
        uint8_t camera_id,
        uint8_t cmd_type,
        const uint8_t* arg_buf,
        uint16_t arg_len)
{
    uint8_t pkt[API2_MAX_PACKET];
    size_t off = 0u;

    if ((arg_len > 0u) && (arg_buf == NULL)) {
        return -1;
    }
    if ((size_t)(12u + arg_len + 2u) > API2_MAX_PACKET) {
        return -1;
    }

    pkt[off++] = 'C';
    pkt[off++] = 'M';
    pkt[off++] = 'D';
    pkt[off++] = '1';
    api2_le32_write(&pkt[off], seq);
    off += 4u;
    pkt[off++] = camera_id;
    pkt[off++] = cmd_type;
    api2_le16_write(&pkt[off], arg_len);
    off += 2u;

    if (arg_len > 0u) {
        memcpy(&pkt[off], arg_buf, arg_len);
        off += arg_len;
    }

    return api2_send_packet(pkt, api2_append_crc16(pkt, off));
}

int api2_send_tlm1(
        uint32_t seq,
        uint8_t camera_id,
        const char* topic,
        const char* value)
{
    uint8_t pkt[API2_MAX_PACKET];
    size_t topic_len;
    size_t value_len;
    size_t pkt_len_no_crc;
    size_t off = 0u;

    if ((topic == NULL) || (value == NULL)) {
        return -1;
    }

    topic_len = strlen(topic);
    value_len = strlen(value);
    if ((topic_len > 0xFFFFu) || (value_len > 0xFFFFu)) {
        return -1;
    }

    pkt_len_no_crc = 13u + topic_len + value_len;
    if ((pkt_len_no_crc + 2u) > API2_MAX_PACKET) {
        return -1;
    }

    pkt[off++] = 'T';
    pkt[off++] = 'L';
    pkt[off++] = 'M';
    pkt[off++] = '1';
    api2_le32_write(&pkt[off], seq);
    off += 4u;
    pkt[off++] = camera_id;
    api2_le16_write(&pkt[off], (uint16_t)topic_len);
    off += 2u;
    api2_le16_write(&pkt[off], (uint16_t)value_len);
    off += 2u;
    if (topic_len > 0u) {
        memcpy(&pkt[off], topic, topic_len);
        off += topic_len;
    }
    if (value_len > 0u) {
        memcpy(&pkt[off], value, value_len);
        off += value_len;
    }

    return api2_send_packet(pkt, api2_append_crc16(pkt, off));
}

int api2_send_cam1(
        uint32_t seq,
        uint8_t camera_id,
        const api2_cam1_status_t* st)
{
    uint8_t pkt[64];
    size_t off = 0u;

    if (st == NULL) {
        return -1;
    }

    pkt[off++] = 'C';
    pkt[off++] = 'A';
    pkt[off++] = 'M';
    pkt[off++] = '1';
    api2_le32_write(&pkt[off], seq);
    off += 4u;
    pkt[off++] = camera_id;

    pkt[off++] = st->mode;
    pkt[off++] = st->tracker_mode;
    pkt[off++] = st->thermal_nuc_status;
    api2_le16_write(&pkt[off], st->lrf_range); off += 2u;
    api2_le16_write(&pkt[off], st->day_ret_x); off += 2u;
    api2_le16_write(&pkt[off], st->day_ret_y); off += 2u;
    api2_le16_write(&pkt[off], st->thermal_ret_x); off += 2u;
    api2_le16_write(&pkt[off], st->thermal_ret_y); off += 2u;
    api2_le16_write(&pkt[off], st->tracker_ret_x); off += 2u;
    api2_le16_write(&pkt[off], st->tracker_ret_y); off += 2u;
    api2_le16_write(&pkt[off], st->tracker_bb_w); off += 2u;
    api2_le16_write(&pkt[off], st->tracker_bb_h); off += 2u;
    api2_le16_write(&pkt[off], st->sys_temp); off += 2u;
    api2_f32_le_write(&pkt[off], st->alt_m); off += 4u;
    pkt[off++] = st->sat;
    pkt[off++] = st->gps_qos;
    pkt[off++] = st->pwr_day_on;
    pkt[off++] = st->pwr_thermal_on;
    pkt[off++] = st->pwr_plcb_on;
    pkt[off++] = st->pwr_lrf_on;
    pkt[off++] = st->pwr_overall;

    if (off != (size_t)(9u + API2_CAM1_PAYLOAD_SIZE)) {
        return -1;
    }

    return api2_send_packet(pkt, api2_append_crc16(pkt, off));
}

int api2_send_led1(
        const uint8_t* led_bits,
        uint16_t led_bytes)
{
    uint8_t pkt[API2_MAX_PACKET];
    size_t off = 0u;

    if ((led_bytes > 0u) && (led_bits == NULL)) {
        return -1;
    }
    if ((size_t)(6u + led_bytes + 2u) > API2_MAX_PACKET) {
        return -1;
    }

    pkt[off++] = 'L';
    pkt[off++] = 'E';
    pkt[off++] = 'D';
    pkt[off++] = '1';
    api2_le16_write(&pkt[off], led_bytes);
    off += 2u;

    if (led_bytes > 0u) {
        memcpy(&pkt[off], led_bits, led_bytes);
        off += led_bytes;
    }

    return api2_send_packet(pkt, api2_append_crc16(pkt, off));
}

int api2_poll_led_status(
        uint8_t* led_bits_out,
        uint16_t led_bytes_expected)
{
    static uint8_t pkt[API2_MAX_PACKET];

    if (!g_api2.initialized) {
        return -1;
    }

    if (led_bytes_expected > 0u && led_bits_out == NULL) {
        return -1;
    }

    for (;;) {
        int rcv = 0;
        uint16_t led_bytes;
        uint16_t crc_rx;
        uint16_t crc_calc;
        size_t payload_plus_header_len;
        int st = api2_peek_packet(pkt, &rcv);
        if (st <= 0) {
            return st;
        }

        if (rcv < 6) {
            if (api2_consume_packet() < 0) {
                return -1;
            }
            continue;
        }

        if (pkt[0] != 'L' || pkt[1] != 'E' || pkt[2] != 'D' || pkt[3] != '1') {
            if (api2_consume_packet() < 0) {
                return -1;
            }
            continue;
        }

        led_bytes = api2_le16_read(&pkt[4]);
        payload_plus_header_len = (size_t)(6u + led_bytes);
        if ((size_t)rcv != (payload_plus_header_len + 2u)) {
            if (api2_consume_packet() < 0) {
                return -1;
            }
            continue;
        }

        crc_rx = api2_le16_read(&pkt[(size_t)rcv - 2u]);
        crc_calc = crc16_ccitt_false(pkt, (size_t)rcv - 2u);
        if (crc_rx != crc_calc) {
            if (api2_consume_packet() < 0) {
                return -1;
            }
            continue;
        }

        if (led_bytes != led_bytes_expected) {
            if (api2_consume_packet() < 0) {
                return -1;
            }
            continue;
        }

        if ((led_bytes > 0u) && (led_bits_out != NULL)) {
            memcpy(led_bits_out, &pkt[6], led_bytes);
        }

        if (api2_consume_packet() < 0) {
            return -1;
        }

        return 1;
    }
}

int api2_poll_cam1(
        uint32_t* seq_out,
        uint8_t* camera_id_out,
        api2_cam1_status_t* st_out)
{
    static uint8_t pkt[API2_MAX_PACKET];
    uint16_t crc_rx;
    uint16_t crc_calc;
    int rcv = 0;
    int st;
    const size_t cam1_no_crc = (size_t)(9u + API2_CAM1_PAYLOAD_SIZE);
    const size_t cam1_total = cam1_no_crc + 2u;

    if (!g_api2.initialized) {
        return -1;
    }
    if ((seq_out == NULL) || (camera_id_out == NULL) || (st_out == NULL)) {
        return -1;
    }

    st = api2_peek_packet(pkt, &rcv);
    if (st <= 0) {
        return st;
    }

    if (rcv < 4) {
        return -2;
    }

    if ((pkt[0] != 'C') || (pkt[1] != 'A') || (pkt[2] != 'M') || (pkt[3] != '1')) {
        return 0;
    }

    if ((size_t)rcv != cam1_total) {
        (void)api2_consume_packet();
        return -2;
    }

    crc_rx = api2_le16_read(&pkt[cam1_no_crc]);
    crc_calc = crc16_ccitt_false(pkt, cam1_no_crc);
    if (crc_rx != crc_calc) {
        (void)api2_consume_packet();
        return -2;
    }

    *seq_out = api2_le32_read(&pkt[4]);
    *camera_id_out = pkt[8];
    st_out->mode = pkt[9];
    st_out->tracker_mode = pkt[10];
    st_out->thermal_nuc_status = pkt[11];
    st_out->lrf_range = api2_le16_read(&pkt[12]);
    st_out->day_ret_x = api2_le16_read(&pkt[14]);
    st_out->day_ret_y = api2_le16_read(&pkt[16]);
    st_out->thermal_ret_x = api2_le16_read(&pkt[18]);
    st_out->thermal_ret_y = api2_le16_read(&pkt[20]);
    st_out->tracker_ret_x = api2_le16_read(&pkt[22]);
    st_out->tracker_ret_y = api2_le16_read(&pkt[24]);
    st_out->tracker_bb_w = api2_le16_read(&pkt[26]);
    st_out->tracker_bb_h = api2_le16_read(&pkt[28]);
    st_out->sys_temp = api2_le16_read(&pkt[30]);
    st_out->alt_m = api2_f32_le_read(&pkt[32]);
    st_out->sat = pkt[36];
    st_out->gps_qos = pkt[37];
    st_out->pwr_day_on = pkt[38];
    st_out->pwr_thermal_on = pkt[39];
    st_out->pwr_plcb_on = pkt[40];
    st_out->pwr_lrf_on = pkt[41];
    st_out->pwr_overall = pkt[42];

    if (api2_consume_packet() < 0) {
        return -1;
    }
    return 1;
}

int api2_poll_cmd1(
        uint32_t* seq_out,
        uint8_t* camera_id_out,
        uint8_t* cmd_type_out,
        uint8_t* arg_buf,
        uint16_t* arg_len_inout)
{
    static uint8_t pkt[API2_MAX_PACKET];

    if (!g_api2.initialized) {
        return -1;
    }

    if ((seq_out == NULL) || (camera_id_out == NULL) ||
        (cmd_type_out == NULL) || (arg_len_inout == NULL)) {
        return -1;
    }

    for (;;) {
        int rcv = 0;
        uint16_t arg_len;
        uint16_t crc_rx;
        uint16_t crc_calc;
        int st = api2_peek_packet(pkt, &rcv);
        if (st <= 0) {
            return st;
        }

        if (rcv < 14) {
            if (api2_consume_packet() < 0) {
                return -1;
            }
            continue;
        }

        if (pkt[0] != 'C' || pkt[1] != 'M' || pkt[2] != 'D' || pkt[3] != '1') {
            if (api2_consume_packet() < 0) {
                return -1;
            }
            continue;
        }

        arg_len = api2_le16_read(&pkt[10]);
        if ((size_t)rcv != (size_t)(12u + arg_len + 2u)) {
            if (api2_consume_packet() < 0) {
                return -1;
            }
            continue;
        }

        crc_rx = api2_le16_read(&pkt[(size_t)rcv - 2u]);
        crc_calc = crc16_ccitt_false(pkt, (size_t)rcv - 2u);
        if (crc_rx != crc_calc) {
            if (api2_consume_packet() < 0) {
                return -1;
            }
            continue;
        }

        if (arg_len > *arg_len_inout) {
            if (api2_consume_packet() < 0) {
                return -1;
            }
            return -2;
        }

        *seq_out = api2_le32_read(&pkt[4]);
        *camera_id_out = pkt[8];
        *cmd_type_out = pkt[9];
        if ((arg_len > 0u) && (arg_buf != NULL)) {
            memcpy(arg_buf, &pkt[12], arg_len);
        }
        *arg_len_inout = arg_len;

        if (api2_consume_packet() < 0) {
            return -1;
        }
        return 1;
    }
}
