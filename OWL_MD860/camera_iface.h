#ifndef CAMERA_IFACE_H
#define CAMERA_IFACE_H

/*
 * @file camera_iface.h
 * @brief OWL-MD400 Ethernet command interface (TCP 8088; CRC-16/XMODEM).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "tracker.h"
#include "camera_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Public constants ---- */

#define OWL_TCP_PORT               (8088U)
#define OWL_UDP_TELEM_PORT         (8005U)
#define OWL_TELEM_FRAME_SIZE       (168U)

/* Interfaces */
typedef enum
{
    OWL_IFACE_CONTROL        = 0x00u,
    OWL_IFACE_THERMAL        = 0x01u,
    OWL_IFACE_DAY            = 0x02u, /* Backward-compatible day interface ID */
    OWL_IFACE_LRF            = 0x03u,
    OWL_IFACE_TRACKER        = 0x07u,
    OWL_IFACE_DAY_LOWLIGHT   = 0x09u, /* Day1 */
    OWL_IFACE_DAY_NORMAL     = 0x0Fu  /* Day2 */
} owl_iface_t;

/* Control addresses */
#define OWL_ADDR_LIVELINESS             (0x0001u)
#define OWL_ADDR_TELEMETRY_DATA         (0x0004u)
#define OWL_ADDR_GLOBAL_RESTART         (0x000Bu)
#define OWL_ADDR_RTSP_URL               (0x000Au)
#define OWL_ADDR_DAY_POWER              (0x0019u)
#define OWL_ADDR_THERMAL_POWER          (0x001Au)
#define OWL_ADDR_LRF_POWER              (0x001Cu)
#define OWL_ADDR_OSD                    (0x002Eu)
#define OWL_ADDR_PIP                    (0x002Fu)
#define OWL_ADDR_PIP_LOCATION           (0x0030u)

/* LRF addresses */
#define OWL_ADDR_LRF_RANGE_MEAS         (0x0001u)
#define OWL_ADDR_LRF_CONTINUOUS_RANGE   (0x0002u)
#define OWL_ADDR_LRF_BREAK_CMD          (0x0003u)
#define OWL_ADDR_LRF_ALIGN_POINTER      (0x0007u)
#define OWL_ADDR_LRF_TARGET_RANGE       (0x0008u)

/* Common lens addresses */
#define OWL_ADDR_ZOOM_CTRL              (0x0100u)
#define OWL_ADDR_FOCUS_CTRL             (0x0101u)
#define OWL_ADDR_ZOOM_STOP              (0x0102u)

/* Restart values */
#define OWL_RESTART_PAYLOAD             (0x01u)
#define OWL_RESTART_SERVICE             (0x02u)
#define OWL_RESTART_SHUTDOWN            (0x03u)
#define OWL_RESTART_REBOOT              OWL_RESTART_PAYLOAD

/* Power values */
#define OWL_POWER_ON                    (0x01u)
#define OWL_POWER_OFF                   (0x02u)
#define OWL_POWER_REBOOT                (0x03u)

/* PIP values */
#define OWL_PIP_ON                      (0x01u)
#define OWL_PIP_OFF                     (0x02u)

/* OSD values */
#define OWL_OSD_ON                      (0x01u)
#define OWL_OSD_OFF                     (0x02u)

/* PIP location values */
#define OWL_PIP_LOC_TOP_LEFT            (0x01u)
#define OWL_PIP_LOC_TOP_RIGHT           (0x02u)
#define OWL_PIP_LOC_BOTTOM_LEFT         (0x03u)
#define OWL_PIP_LOC_BOTTOM_RIGHT        (0x04u)

/* Zoom/focus values */
#define OWL_ZOOM_MODE_CONTINUOUS        (0x01u)
#define OWL_ZOOM_MODE_MANUAL            (0x02u)
#define OWL_ZOOM_IN                     (0x01u)
#define OWL_ZOOM_OUT                    (0x02u)
#define OWL_FOCUS_FAR                   (0x01u)
#define OWL_FOCUS_NEAR                  (0x02u)
#define OWL_LENS_STOP_ZOOM              (0x01u)
#define OWL_LENS_STOP_FOCUS             (0x02u)
#define OWL_THERMAL_LENS_RESERVED       (0x01u)
#define OWL_DAY_LOWLIGHT_SPEED_MIN      (0x01u)
#define OWL_DAY_LOWLIGHT_SPEED_MAX      (0x07u)
#define OWL_DAY_NORMAL_SPEED_MIN        (0x01u)
#define OWL_DAY_NORMAL_SPEED_MAX        (0x07u)
#define OWL_DAY_NORMAL_MANUAL_STEP      (0x00u)

/* LRF single measurement modes */
#define OWL_LRF_SMM                     (0x01u)
#define OWL_LRF_CH1                     (0x02u)
#define OWL_LRF_CH2                     (0x03u)

/* LRF continuous measurement rates */
#define OWL_LRF_FREQ_1HZ                (0x01u)
#define OWL_LRF_FREQ_4HZ                (0x02u)
#define OWL_LRF_FREQ_10HZ               (0x03u)
#define OWL_LRF_FREQ_20HZ               (0x04u)
#define OWL_LRF_FREQ_100HZ              (0x05u)
#define OWL_LRF_FREQ_200HZ              (0x06u)

/* Status byte values */
typedef enum
{
    OWL_ST_OK                = 0x00u,
    OWL_ST_TIMEOUT_ERROR     = 0x01u,
    OWL_ST_BAD_HEADER        = 0x02u,
    OWL_ST_CHECKSUM_ERROR    = 0x03u,
    OWL_ST_BYTE_COUNT_ERROR  = 0x04u,
    OWL_ST_BAD_RDWR          = 0x05u,
    OWL_ST_BAD_CMD           = 0x06u,
    OWL_ST_CMD_NOT_SUPPORTED = 0x07u,
    OWL_ST_NOT_VALID         = 0x08u,
    OWL_ST_FAILED            = 0x09u,
    OWL_ST_INVALID_RANGE     = 0x0Au,
    OWL_ST_INVALID_IP        = 0x0Bu,
    OWL_ST_INVALID_SUBNET    = 0x0Cu,
    OWL_ST_OUT_OF_MEMORY     = 0x0Du
} owl_status_t;

/* App-level return codes */
typedef enum
{
    OWL_OK         = 0,
    OWL_ERR_ARG    = -1,
    OWL_ERR_SOCK   = -2,
    OWL_ERR_IO     = -3,
    OWL_ERR_PROTO  = -4,
    OWL_ERR_STATUS = -5
} owl_rc_t;

/* TCP control handle */
typedef struct
{
    int      tcp_fd;
    uint32_t cam_ipv4_be;
    uint16_t tcp_port_be;
} owl_cam_t;

/* Telemetry UDP handle */
typedef struct
{
    int      udp_fd;
    uint16_t port;
} owl_telem_t;

/* Parsed telemetry frame */
typedef struct
{
    uint8_t  version;
    uint8_t  pt_reserved[10];

    uint16_t lrf_range;
    uint8_t  lrf_reserved[3];

    uint16_t thermal_ret_x;
    uint16_t thermal_ret_y;
    uint8_t  thermal_fov_code;
    float    thermal_fov_deg;
    uint8_t  thermal_nuc_status;
    uint8_t  thermal_reserved[10];

    uint16_t day_ret_x;
    uint16_t day_ret_y;
    uint8_t  day_fov_code;
    float    day_fov_deg;
    uint8_t  day_reserved[11];

    uint16_t day2_ret_x;
    uint16_t day2_ret_y;
    uint8_t  day2_fov_code;
    float    day2_fov_deg;
    uint8_t  day2_reserved[11];

    uint8_t  tracker_mode;
    uint16_t tracker_ret_x;
    uint16_t tracker_ret_y;
    uint16_t tracker_bb_w;
    uint16_t tracker_bb_h;
    uint16_t tracker_pan_err;
    uint16_t tracker_tilt_err;
    uint8_t  tracker_cam_id;
    uint8_t  tracker_track_mode;
    uint8_t  tracker_track_type;
    uint8_t  tracker_detection_mode;
    uint8_t  tracker_reserved[3];

    uint8_t  factory_reserved[20];

    uint16_t sys_temp;

    double   lat;
    uint8_t  NSdir;
    double   lon;
    uint8_t  EWdir;
    float    alt;
    uint8_t  sat;
    uint8_t  gpsQOS;
    uint8_t  gps_reserved[21];

    uint8_t  pwr_day_on;
    uint8_t  pwr_thermal_on;
    uint8_t  pwr_plcb_on;
    uint8_t  pwr_lrf_on;
    uint8_t  pwr_overall;
} owl_telem_frame_t;

/* rtsp url streaming helper*/
typedef struct
{
    char thermal[256];
    char day[256];
    char low_light[256];
    int  valid;
} owl_rtsp_urls_t;

/* RTSP helpers */
int owl_cam_get_rtsp_url(owl_cam_t *cam, char *out, size_t out_sz);
int owl_cam_parse_rtsp_urls(const char *rtsp_blob, owl_rtsp_urls_t *urls);

/* VLC single-instance control */
int  owl_cam_vlc_switch_stream(const char *vlc_path, const char *rtsp_url);
void owl_cam_vlc_stop(void);

/* Optional direct helpers */
int owl_cam_open_thermal_stream(owl_cam_t *cam, const char *vlc_path);
int owl_cam_open_day_stream(owl_cam_t *cam, const char *vlc_path);
int owl_cam_open_lowlight_stream(owl_cam_t *cam, const char *vlc_path);

/* base transport */
void camera_iface_bind_cam(owl_cam_t *cam);
int  owl_cam_open(owl_cam_t *cam, const char *ip4_str, uint16_t tcp_port);
void owl_cam_close(owl_cam_t *cam);
int  owl_cam_read(owl_cam_t *cam, owl_iface_t iface, uint16_t addr,
                  uint8_t *data_out, size_t *data_out_len);
int  owl_cam_write(owl_cam_t *cam, owl_iface_t iface, uint16_t addr,
                   const uint8_t *data, size_t len);

/* basic control */
int  owl_cam_liveliness(owl_cam_t *cam, bool *alive);
int  owl_cam_set_telemetry(owl_cam_t *cam, bool enable);
int  owl_cam_restart(owl_cam_t *cam, uint8_t mode);
int  owl_cam_set_osd(owl_cam_t *cam, bool enable);
int  owl_cam_set_pip(owl_cam_t *cam, bool enable);
int  owl_cam_get_pip(owl_cam_t *cam, bool *enabled);
int  owl_cam_set_pip_location(owl_cam_t *cam, uint8_t location);
int  owl_cam_get_pip_location(owl_cam_t *cam, uint8_t *location);

/* power */
int  owl_cam_day_power(owl_cam_t *cam, uint8_t mode);
int  owl_cam_thermal_power(owl_cam_t *cam, uint8_t mode);
int  owl_cam_lrf_power(owl_cam_t *cam, uint8_t mode);

/* selection helpers */
int  owl_cam_select_thermal(owl_cam_t *cam, bool power_off_day);
int  owl_cam_select_day_lowlight(owl_cam_t *cam, bool power_off_thermal);
int  owl_cam_select_day_normal(owl_cam_t *cam, bool power_off_thermal);

/* tracker camera switch */
int  owl_cam_tracker_switch_camera(owl_cam_t *cam, uint8_t cam_id);

/* tracker adapter hooks */
int  camera_iface_read_tracker(tracker_state_t *out_state);
int  camera_iface_cmd_w6(uint8_t mode_value);
int  camera_iface_cmd_w7(uint8_t disable_mask);

/* tracker controls */
int  owl_tracker_mode_on(uint8_t cam, uint16_t x, uint16_t y);
int  owl_tracker_mode_off(uint8_t cam, uint16_t x, uint16_t y);
int  owl_tracker_read_all(tracker_state_t *st);
int  owl_tracker_set_coord(uint8_t cam, uint16_t x, uint16_t y, uint8_t mode);
int  owl_tracker_set_box(uint8_t cam, uint8_t w, uint8_t h);
int  owl_tracker_set_mode(uint8_t cam, uint8_t mode);
int  owl_tracker_set_lock(uint8_t cam, uint8_t mode);
int  owl_tracker_set_stab(uint8_t cam, uint8_t mode);
int  owl_tracker_set_szone(uint8_t cam, uint8_t zone);
int  owl_tracker_set_auto(uint8_t cam, uint8_t mode, uint8_t sens);
int  owl_tracker_set_roi(uint8_t cam, uint8_t mode);
int  owl_tracker_set_bitrate_centi(uint16_t centi_mbps);
int  owl_tracker_set_maxfail(uint8_t cam, uint8_t code);
int  owl_tracker_set_type(uint8_t cam, uint8_t type_code);
int  owl_tracker_set_search_area(uint8_t cam, uint8_t percent);
int  owl_tracker_set_confidence(uint8_t cam, uint8_t tenths);

/* LRF */
int  owl_cam_lrf_single_measure(owl_cam_t *cam, uint8_t mode);
int  owl_cam_lrf_set_frequency(owl_cam_t *cam, uint8_t freq_mode);
int  owl_cam_lrf_stop(owl_cam_t *cam);
int  owl_cam_lrf_align_pointer(owl_cam_t *cam, bool enable);
int  owl_cam_lrf_read_target_range(owl_cam_t *cam, uint16_t *out_range_m);

/* Day1 low light lens */
int  owl_cam_day_lowlight_zoom(owl_cam_t *cam, uint8_t zoom_mode, uint8_t direction, uint8_t speed);
int  owl_cam_day_lowlight_zoom_stop(owl_cam_t *cam);
int  owl_cam_day_lowlight_focus(owl_cam_t *cam, uint8_t focus_mode, uint8_t direction, uint8_t speed);
int  owl_cam_day_lowlight_focus_stop(owl_cam_t *cam);

/* Day2 normal lens */
int  owl_cam_day_normal_zoom(owl_cam_t *cam, uint8_t zoom_mode, uint8_t direction, uint8_t speed);
int  owl_cam_day_normal_zoom_stop(owl_cam_t *cam);
int  owl_cam_day_normal_focus(owl_cam_t *cam, uint8_t focus_mode, uint8_t direction, uint8_t speed);
int  owl_cam_day_normal_focus_stop(owl_cam_t *cam);

/* Legacy day lens API (single day interface) */
int  owl_cam_day_zoom(owl_cam_t *cam, uint8_t zoom_mode, uint8_t direction);
int  owl_cam_day_zoom_stop(owl_cam_t *cam);
int  owl_cam_day_focus(owl_cam_t *cam, uint8_t focus_mode, uint8_t direction);
int  owl_cam_day_focus_stop(owl_cam_t *cam);

/* Thermal lens */
int  owl_cam_thermal_zoom(owl_cam_t *cam, uint8_t zoom_mode, uint8_t direction);
int  owl_cam_thermal_zoom_stop(owl_cam_t *cam);
int  owl_cam_thermal_focus(owl_cam_t *cam, uint8_t focus_mode, uint8_t direction);
int  owl_cam_thermal_focus_stop(owl_cam_t *cam);

/* telemetry */
int  camera_telem_open(owl_telem_t *ctx, const char *bind_ip, uint16_t port);
void camera_telem_close(owl_telem_t *ctx);
int  camera_telem_recv(owl_telem_t *ctx, owl_telem_frame_t *out, int timeout_ms);
void camera_telem_publish(const camera_params_t *cam, const owl_telem_frame_t *fr);
void camera_iface_set_udp_mcast(bool enabled, uint8_t camera_id, uint32_t *seq_counter);
int  camera_iface_get_last_failed_frame(uint8_t *out_frame, size_t *inout_len,
                                        uint8_t *out_status, owl_iface_t *out_iface, uint16_t *out_addr);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_IFACE_H */








// /*
//  * @file camera_iface.h
//  * @brief OWL MD-860 Ethernet command interface (TCP 8088; CRC-16/X-MODEM).
//  * @author
//  * @version 1.0
//  * @date 2025-09-01
//  *
//  * Implements the packet format described in the MD-860 ICD:
//  *   Header 0xCC, R/W 0x52/0x57, IFACE, ADDR[15:0], MSL, DATA..., CRC-16/XMODEM
//  * Host->Core and Core->Host fields per Table 1/2; status byte checked.
//  * References: Packet fields and status table【turn2file11†ANALINEAR Software_Command_Protocol 1.pdf†L128-L136】【turn2file4†ANALINEAR Software_Command_Protocol 1.pdf†L150-L158】.
//  */

// #ifndef CAMERA_IFACE_H
// #define CAMERA_IFACE_H

// #include <stdint.h>
// #include <stdbool.h>
// #include <stddef.h>

// #include "tracker.h"
// #include "camera_params.h"


// #ifdef __cplusplus
// extern "C" {
// #endif

// /* ---- Public constants from ICD ---- */

// /* TCP control channel (ICD) */
// #define OWL_TCP_PORT               (8088U)   /* ICD TCP client socket port【turn2file11†ANALINEAR Software_Command_Protocol 1.pdf†L74-L79】 */
// #define OWL_UDP_TELEM_PORT         (8005U)   /* Telemetry UDP port (when enabled)【turn2file13†ANALINEAR Software_Command_Protocol 1.pdf†L74-L77】 */
// #define OWL_TELEM_FRAME_SIZE (168U)

// /* Interfaces (CMD first byte)【turn2file11†ANALINEAR Software_Command_Protocol 1.pdf†L141-L148】 */
// typedef enum
// {
//     OWL_IFACE_CONTROL = 0x00u,
//     OWL_IFACE_THERMAL = 0x01u,
//     OWL_IFACE_DAY     = 0x02u,
//     OWL_IFACE_LRF     = 0x03u
// } owl_iface_t;

// /* Selected control addresses (ICD “Control Configuration”) */
// #define OWL_ADDR_LIVELINESS        (0x0001u) /* RD: returns 0x01 if success【turn2file13†ANALINEAR Software_Command_Protocol 1.pdf†L54-L55】 */
// #define OWL_ADDR_TELEMETRY_DATA    (0x0004u) /* WR: 0x01 on, 0x02 off; enables UDP 8005【turn2file13†ANALINEAR Software_Command_Protocol 1.pdf†L74-L87】 */
// #define OWL_ADDR_GLOBAL_RESTART    (0x000Bu) /* WR: 0x01 reboot, 0x02 service restart, 0x03 shutdown */
// #define OWL_ADDR_DAY_POWER         (0x0019u) /* WR: 0x01 on, 0x02 off, 0x03 reboot */
// #define OWL_ADDR_THERMAL_POWER     (0x001Au) /* WR: 0x01 on, 0x02 off, 0x03 reboot */
// #define OWL_ADDR_LRF_POWER         (0x001Cu) /* WR: 0x02 off, 0x03 reboot */
// #define OWL_ADDR_LRF_CONTINUOUS_RANGE   (0x0002u)
// #define OWL_ADDR_LRF_BREAK_CMD          (0x0003u)

// #define OWL_ADDR_ZOOM_CTRL         (0x0100u) /* WR: [mode, direction, 0x01] */
// #define OWL_ADDR_ZOOM_STOP         (0x0102u) /* WR: [0x01] */

// #define OWL_RESTART_REBOOT         (0x01u)
// #define OWL_RESTART_SERVICE        (0x02u)
// #define OWL_RESTART_SHUTDOWN       (0x03u)

// #define OWL_POWER_ON               (0x01u)
// #define OWL_POWER_OFF              (0x02u)
// #define OWL_POWER_REBOOT           (0x03u)

// #define OWL_ZOOM_MODE_CONTINUOUS   (0x01u)
// #define OWL_ZOOM_MODE_MANUAL       (0x02u)
// #define OWL_ZOOM_IN                (0x01u)
// #define OWL_ZOOM_OUT               (0x02u)

// #define OWL_LRF_FREQ_1HZ    (0x01u)
// #define OWL_LRF_FREQ_4HZ    (0x02u)
// #define OWL_LRF_FREQ_10HZ   (0x03u)
// #define OWL_LRF_FREQ_20HZ   (0x04u)
// #define OWL_LRF_FREQ_100HZ  (0x05u)
// #define OWL_LRF_FREQ_200HZ  (0x06u)

// /* Status byte values (Core->Host 2nd byte)【turn2file3†ANALINEAR Software_Command_Protocol 1.pdf†L75-L101】 */
// typedef enum
// {
//     OWL_ST_OK                = 0x00u,
//     OWL_ST_TIMEOUT_ERROR     = 0x01u,
//     OWL_ST_BAD_HEADER        = 0x02u,
//     OWL_ST_CHECKSUM_ERROR    = 0x03u,
//     OWL_ST_BYTE_COUNT_ERROR  = 0x04u,
//     OWL_ST_BAD_RDWR          = 0x05u,
//     OWL_ST_BAD_CMD           = 0x06u,
//     OWL_ST_CMD_NOT_SUPPORTED = 0x07u,
//     OWL_ST_NOT_VALID         = 0x08u,
//     OWL_ST_FAILED            = 0x09u,
//     OWL_ST_INVALID_RANGE     = 0x0Au,
//     OWL_ST_INVALID_IP        = 0x0Bu,
//     OWL_ST_INVALID_SUBNET    = 0x0Cu
// } owl_status_t;

// /* App-level return codes */
// typedef enum
// {
//     OWL_OK = 0,
//     OWL_ERR_ARG = -1,
//     OWL_ERR_SOCK = -2,
//     OWL_ERR_IO = -3,
//     OWL_ERR_PROTO = -4,
//     OWL_ERR_STATUS = -5
// } owl_rc_t;

// /* Opaque connection handle */
// typedef struct
// {
//     int          tcp_fd;             /* control socket (TCP) */
//     uint32_t     cam_ipv4_be;        /* network-order IPv4 */
//     uint16_t     tcp_port_be;        /* network-order port (8088) */
//     /* Optionally track telemetry UDP socket later */
// } owl_cam_t;

// /* Parsed subset of telemetry fields (units as provided by device/ICD) */
// typedef struct
// {
//     uint8_t  version;

//     /* LRF */
//     uint16_t lrf_range;          /* meters; 0x00FF => 255 m per note */

//     /* Thermal */
//     uint16_t thermal_ret_x;
//     uint16_t thermal_ret_y;
//     uint8_t  thermal_fov_code;
//     float    thermal_fov_deg;    /* LE float degrees */
//     uint8_t  thermal_nuc_status;

//     /* Day */
//     uint16_t day_ret_x;
//     uint16_t day_ret_y;
//     uint8_t  day_fov_code;
//     float    day_fov_deg;        /* LE float degrees */

//     /* Tracker (20 bytes total) */
//     uint8_t  tracker_mode;       /* 0x01 enable, 0x02 disable */
//     uint16_t tracker_ret_x;
//     uint16_t tracker_ret_y;
//     uint16_t tracker_bb_w;       /* width first (per spec) */
//     uint16_t tracker_bb_h;       /* then height */
//     uint16_t tracker_pan_err;
//     uint16_t tracker_tilt_err;

//     /* Temperatures */
//     uint16_t sys_temp;           /* hundredths of °C (e.g., 5393 => 53.93°C) */

//     /* GPS */
//     double   lat;                /* degrees, LE double */
//     uint8_t  NSdir;              /* ASCII 'N' or 'S' */
//     double   lon;                /* degrees, LE double */
//     uint8_t  EWdir;              /* ASCII 'E' or 'W' */
//     float    alt;                /* meters, LE float */
//     uint8_t  sat;                /* number of satellites */
//     uint8_t  gpsQOS;             /* quality indicator */

//     /* Power controls */
//     uint8_t  pwr_day_on;
//     uint8_t  pwr_thermal_on;
//     uint8_t  pwr_plcb_on;
//     uint8_t  pwr_lrf_on;
//     uint8_t  pwr_overall;        /* watts */
// } owl_telem_frame_t;

// /* Opaque listener context */
// typedef struct
// {
//     int      udp_fd;     /* bound UDP socket */
//     uint16_t port;       /* host-order */
// } owl_telem_t;

// /* ---- API ---- */

// /** @brief Open TCP control channel to camera at ip4 dotted string (e.g. "192.168.1.50"). */
// int  owl_cam_open(owl_cam_t *cam, const char *ip4_str, uint16_t tcp_port);

// /** @brief Close TCP channel. Safe to call on a partially-open handle. */
// void owl_cam_close(owl_cam_t *cam);

// /** @brief Low-level READ (R=0x52) per ICD. data_out_len is both in/out. */
// int  owl_cam_read(owl_cam_t *cam, owl_iface_t iface, uint16_t addr,
//                   uint8_t *data_out, size_t *data_out_len);

// /** @brief Low-level WRITE (W=0x57) per ICD. Writes 'len' bytes at (iface, addr). */
// int  owl_cam_write(owl_cam_t *cam, owl_iface_t iface, uint16_t addr,
//                    const uint8_t *data, size_t len);

// /** @brief Perform ICD “Liveliness_Check” (RD 0x0001). Sets *alive=true iff device returns 0x01. */
// int  owl_cam_liveliness(owl_cam_t *cam, bool *alive);

// /** @brief Enable (true) or disable (false) telemetry streaming (UDP 8005, 168 bytes). */
// int  owl_cam_set_telemetry(owl_cam_t *cam, bool enable);

// int  owl_cam_restart(owl_cam_t *cam, uint8_t mode);

// int  owl_cam_day_power(owl_cam_t *cam, uint8_t mode);
// int  owl_cam_thermal_power(owl_cam_t *cam, uint8_t mode);
// int  owl_cam_lrf_power(owl_cam_t *cam, uint8_t mode);
// int  owl_cam_lrf_set_frequency(owl_cam_t *cam, uint8_t freq_mode);
// int  owl_cam_lrf_stop(owl_cam_t *cam);

// int  owl_cam_day_zoom(owl_cam_t *cam, uint8_t zoom_mode, uint8_t direction);
// int  owl_cam_day_zoom_stop(owl_cam_t *cam);

// int  owl_cam_thermal_zoom(owl_cam_t *cam, uint8_t zoom_mode, uint8_t direction);
// int  owl_cam_thermal_zoom_stop(owl_cam_t *cam);

// /**
//  * @brief Open UDP socket and bind to interface/port for telemetry frames.
//  * @param ctx  [out] initialized on success
//  * @param bind_ip dotted-quad or NULL/"0.0.0.0" to bind any
//  * @param port UDP port (use OWL_UDP_TELEM_PORT if 0)
//  * @return 0 on success, negative error code on failure
//  */
// int  camera_telem_open(owl_telem_t *ctx, const char *bind_ip, uint16_t port);

// /**
//  * @brief Close the telemetry socket (safe to call on partially-open ctx).
//  */
// void camera_telem_close(owl_telem_t *ctx);

// /**
//  * @brief Receive one telemetry frame (blocking up to timeout_ms) and parse.
//  * @param timeout_ms  <0 = block; 0 = poll; >0 = wait up to N ms
//  * @return 1 on frame received & parsed, 0 on timeout, negative on error
//  */
// int  camera_telem_recv(owl_telem_t *ctx, owl_telem_frame_t *out, int timeout_ms);

// /**
//  * @brief Publish a parsed telemetry frame to MQTT under <root>/TELEM/<leaf>.
//  * @note  Uses camera_params_t.root_topic or "OWL" if empty.
//  */
// void camera_telem_publish(const camera_params_t *cam, const owl_telem_frame_t *fr);

// void camera_iface_set_udp_mcast(bool enabled, uint8_t camera_id, uint32_t *seq_counter);

// void camera_iface_bind_cam(owl_cam_t *cam);
// /* -------- Tracker read/all and writers (Table 10) -------- */
// int owl_tracker_read_all(tracker_state_t *st);

// int owl_tracker_set_coord(uint8_t cam, uint16_t x, uint16_t y, uint8_t mode);      /* 0x0001 */
// int owl_tracker_set_box(uint8_t cam, uint8_t w, uint8_t h);                         /* 0x0002 */
// int owl_tracker_set_mode(uint8_t cam, uint8_t mode);                                 /* 0x0003 */
// int owl_tracker_set_lock(uint8_t cam, uint8_t mode);                                 /* 0x0004 */
// int owl_tracker_set_stab(uint8_t cam, uint8_t mode);                                 /* 0x0005 */
// int owl_tracker_set_szone(uint8_t cam, uint8_t zone);                                /* 0x0006 */
// int owl_tracker_set_auto(uint8_t cam, uint8_t mode, uint8_t sens);                   /* 0x0008 */
// int owl_tracker_set_roi(uint8_t cam, uint8_t mode);                                  /* 0x0009 */
// int owl_tracker_set_bitrate_centi(uint16_t centi_mbps);                              /* 0x000C */
// int owl_tracker_set_maxfail(uint8_t cam, uint8_t code);                               /* 0x000D */
// int owl_tracker_set_type(uint8_t cam, uint8_t type_code);                             /* 0x000F */
// int owl_tracker_set_search_area(uint8_t cam, uint8_t percent);                        /* 0x0010 */
// int owl_tracker_set_confidence(uint8_t cam, uint8_t tenths);


// #ifdef __cplusplus
// }
// #endif

// #endif /* CAMERA_IFACE_H */
