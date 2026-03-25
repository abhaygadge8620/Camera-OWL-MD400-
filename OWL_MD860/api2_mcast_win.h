#ifndef API2_MCAST_WIN_H
#define API2_MCAST_WIN_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    char group_ip[32];
    uint16_t port;
    char iface_ip[32];
    uint8_t ttl;
    uint8_t loopback;
} api2_cfg_t;

typedef struct {
    uint8_t  mode;
    uint8_t  tracker_mode;
    uint8_t  thermal_nuc_status;
    uint16_t lrf_range;
    uint16_t day_ret_x;
    uint16_t day_ret_y;
    uint16_t thermal_ret_x;
    uint16_t thermal_ret_y;
    uint16_t tracker_ret_x;
    uint16_t tracker_ret_y;
    uint16_t tracker_bb_w;
    uint16_t tracker_bb_h;
    uint16_t sys_temp;
    float    alt_m;
    uint8_t  sat;
    uint8_t  gps_qos;
    uint8_t  pwr_day_on;
    uint8_t  pwr_thermal_on;
    uint8_t  pwr_plcb_on;
    uint8_t  pwr_lrf_on;
    uint8_t  pwr_overall;
} api2_cam1_status_t;

int api2_init(const api2_cfg_t* cfg);
void api2_shutdown(void);

int api2_send_can_frame(
        uint32_t can_id,
        uint8_t is_ext,
        uint8_t dlc,
        const uint8_t* data);

int api2_poll_led_status(
        uint8_t* led_bits_out,
        uint16_t led_bytes_expected);

int api2_send_tlm1(
        uint32_t seq,
        uint8_t camera_id,
        const char* topic,
        const char* value);

int api2_send_cam1(
        uint32_t seq,
        uint8_t camera_id,
        const api2_cam1_status_t* st);

int api2_send_cmd1(
        uint32_t seq,
        uint8_t camera_id,
        uint8_t cmd_type,
        const uint8_t* arg_buf,
        uint16_t arg_len);

int api2_send_led1(
        const uint8_t* led_bits,
        uint16_t led_bytes);

int api2_poll_cmd1(
        uint32_t* seq_out,
        uint8_t* camera_id_out,
        uint8_t* cmd_type_out,
        uint8_t* arg_buf,
        uint16_t* arg_len_inout);

int api2_poll_cam1(
        uint32_t* seq_out,
        uint8_t* camera_id_out,
        api2_cam1_status_t* st_out);

#endif /* API2_MCAST_WIN_H */
