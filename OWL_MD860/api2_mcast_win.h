#ifndef API2_MCAST_WIN_H
#define API2_MCAST_WIN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define API2_MAX_JSON 2048u

typedef enum
{
    API2_CH_CAMERA = 0,
    API2_CH_JOYSTICK = 1,
    API2_CH_BUTTON_LED = 2,
    API2_CH_COUNT = 3
} api2_channel_t;

typedef struct
{
    char group_ip[32];
    uint16_t port;
    uint8_t enabled;
} api2_channel_cfg_t;

/*
 * Full old camera telemetry content preserved.
 * This matches the old CAM1 payload meaning, but now carried in JSON.
 */
typedef struct
{
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

typedef struct
{
    char iface_ip[32];
    uint8_t ttl;
    uint8_t loopback;

    api2_channel_cfg_t camera;
    api2_channel_cfg_t joystick;
    api2_channel_cfg_t button_led;

    uint8_t camera_id;
} api2_cfg_t;

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                   */
/* -------------------------------------------------------------------------- */

int api2_init(const api2_cfg_t* cfg);
void api2_shutdown(void);

/* -------------------------------------------------------------------------- */
/* Generic JSON send / receive                                                 */
/* -------------------------------------------------------------------------- */

/*
 * Send raw JSON string on a particular multicast channel.
 * Returns bytes sent on success, negative on error.
 */
int api2_send_json_string(api2_channel_t ch, const char* json_str);

/*
 * Receive raw JSON string from a particular multicast channel.
 * Returns:
 *   >0 : number of bytes received
 *    0 : no packet available (non-blocking)
 *   <0 : error
 */
int api2_recv_json_string(api2_channel_t ch, char* buf, size_t buf_sz);

/* -------------------------------------------------------------------------- */
/* Camera telemetry JSON (full old camera telemetry content preserved)         */
/* -------------------------------------------------------------------------- */

/*
 * Sends a JSON message of type "camera_telem" on camera multicast channel.
 */
int api2_send_cam1(
        uint32_t seq,
        uint8_t camera_id,
        const api2_cam1_status_t* st);

/*
 * Polls camera multicast channel and parses JSON type "camera_telem".
 * Returns:
 *   1 : parsed one valid camera_telem packet
 *   0 : no packet or packet is not camera_telem
 *  <0 : error / invalid packet
 */
int api2_poll_cam1(
        uint32_t* seq_out,
        uint8_t* camera_id_out,
        api2_cam1_status_t* st_out);

/* -------------------------------------------------------------------------- */
/* Joystick JSON                                                               */
/* -------------------------------------------------------------------------- */

/*
 * Receives raw joystick JSON string from joystick multicast channel.
 * This is useful if another process publishes joystick JSON.
 */
int api2_poll_joystick_json(char* buf, size_t buf_sz);

/* -------------------------------------------------------------------------- */
/* Button / LED JSON                                                           */
/* -------------------------------------------------------------------------- */

/*
 * Sends button/LED JSON on button_led multicast channel.
 * Example JSON:
 * {
 *   "type":"button_led",
 *   "seq":10,
 *   "name":"DAY",
 *   "value":1,
 *   "direction":"tx",
 *   "source":"camera_telem"
 * }
 */
int api2_send_button_led_json(
        uint32_t seq,
        const char* name,
        uint8_t value,
        const char* direction,
        const char* source);

/*
 * Receives raw button_led JSON string from button_led multicast channel.
 */
int api2_poll_button_led_json(char* buf, size_t buf_sz);

/* -------------------------------------------------------------------------- */
/* Helpers for loading config values into api2_cfg_t                           */
/* -------------------------------------------------------------------------- */

void api2_cfg_set_defaults(api2_cfg_t* cfg);

#ifdef __cplusplus
}
#endif

#endif /* API2_MCAST_WIN_H */