// #include <stdint.h>
// #include <stdbool.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <signal.h>
// #include <pthread.h>

// #include "platform_compat.h"
// #include "UART API/led_status_router.h"
// #include "OWL_MD860/camera_actions.h"
// #include "OWL_MD860/camera_iface.h"
// #include "OWL_MD860/camera_mqtt.h"
// #include "OWL_MD860/camera_params.h"
// #include "OWL_MD860/ini_parser.h"
// #include "OWL_MD860/api2_mcast_win.h"
// #include "OWL_MD860/button_led_rx.h"
// #include "OWL_MD860/joystick_rx.h"
// #include "OWL_MD860/mqtt.h"
// #include "UART API/config_ini.h"
// #include "UART API/button_mcast.h"
// #include "UART API/uart_protocol.h"
// #include "UART API/uart_win.h"

// static volatile sig_atomic_t g_running = 1;
// static unsigned long g_loop_count = 0UL;
// static tracker_state_t g_last_tracker_state;
// static int g_last_tracker_valid = 0;

// /* New global runtime state */
// static int g_tracker_mode_on = 0;
// static uint8_t g_selected_view_cam = 0u;

// #define MAIN_LOG(fmt, ...) \
//     do { printf("[MAIN] " fmt "\n", __VA_ARGS__); } while (0)

// #define MAIN_LOG0(msg) \
//     do { printf("[MAIN] %s\n", msg); } while (0)

// #define OWL_TRACKER_ADDR         0x0001u
// #define OWL_TRACKER_CAM_THERMAL  0x01u
// #define OWL_TRACKER_CAM_DAY1     0x02u
// #define OWL_TRACKER_CAM_DAY2     0x03u
// #define OWL_TRACKER_START        0x01u
// #define OWL_TRACKER_STOP         0x02u
// #define TELEMETRY_LOSS_LED_OFF_MS  500u

// static void get_tracker_center_for_cam(uint8_t cam_id, uint16_t *x_out, uint16_t *y_out)
// {
//     uint16_t x = 960u;
//     uint16_t y = 540u;

//     if ((x_out == NULL) || (y_out == NULL)) {
//         return;
//     }

//     if (cam_id == OWL_TRACK_CAM_THERMAL) {
//         x = 640u;
//         y = 360u;
//     }

//     *x_out = x;
//     *y_out = y;
// }

// static void log_camera_response(const char *op, int rc)
// {
//     uint8_t frame[64];
//     size_t frame_len = sizeof(frame);
//     uint8_t status = 0xFFu;
//     owl_iface_t iface = OWL_IFACE_CONTROL;
//     uint16_t addr = 0u;
//     int got;
//     char hex[64 * 3 + 1];
//     size_t i;
//     size_t pos = 0u;

//     if (op == NULL) {
//         op = "camera_cmd";
//     }

//     if (rc == OWL_OK) {
//         MAIN_LOG("%s rc=%d (ack ok)", op, rc);
//         return;
//     }

//     got = camera_iface_get_last_failed_frame(frame, &frame_len, &status, &iface, &addr);
//     if (got <= 0) {
//         MAIN_LOG("%s rc=%d (no failed frame cached)", op, rc);
//         return;
//     }

//     for (i = 0u; i < frame_len; ++i) {
//         const int n = snprintf(&hex[pos], sizeof(hex) - pos, "%02X%s",
//                                (unsigned)frame[i],
//                                (i + 1u < frame_len) ? " " : "");
//         if (n <= 0) {
//             break;
//         }
//         pos += (size_t)n;
//         if (pos >= sizeof(hex)) {
//             pos = sizeof(hex) - 1u;
//             break;
//         }
//     }
//     hex[pos] = '\0';

//     MAIN_LOG("%s rc=%d status=0x%02X iface=0x%02X addr=0x%04X frame_len=%u frame=%s",
//              op,
//              rc,
//              (unsigned)status,
//              (unsigned)iface,
//              (unsigned)addr,
//              (unsigned)frame_len,
//              hex);
// }

// static int main_tracker_set_coord(owl_cam_t *cam,
//                                   owl_iface_t iface,
//                                   uint8_t cam_id,
//                                   uint16_t x,
//                                   uint16_t y,
//                                   uint8_t start_stop)
// {
//     uint8_t payload[6];

//     if (cam == NULL) {
//         return OWL_ERR_ARG;
//     }
//     if ((cam_id != OWL_TRACKER_CAM_THERMAL) &&
//         (cam_id != OWL_TRACKER_CAM_DAY1) &&
//         (cam_id != OWL_TRACKER_CAM_DAY2)) {
//         return OWL_ERR_ARG;
//     }
//     if ((start_stop != OWL_TRACKER_START) && (start_stop != OWL_TRACKER_STOP)) {
//         return OWL_ERR_ARG;
//     }

//     payload[0] = cam_id;
//     payload[1] = (uint8_t)(x >> 8);
//     payload[2] = (uint8_t)(x & 0xFFu);
//     payload[3] = (uint8_t)(y >> 8);
//     payload[4] = (uint8_t)(y & 0xFFu);
//     payload[5] = start_stop;

//     return owl_cam_write(cam, iface, OWL_TRACKER_ADDR, payload, sizeof(payload));
// }

// static int parse_u16_string(const char *s, uint16_t *out)
// {
//     char *endp = NULL;
//     unsigned long v;

//     if ((s == NULL) || (out == NULL)) {
//         MAIN_LOG0("parse_u16_string invalid input");
//         return -1;
//     }

//     v = strtoul(s, &endp, 10);
//     if ((endp == s) || (*endp != '\0') || (v > 65535UL)) {
//         MAIN_LOG("parse_u16_string failed for value='%s'", s);
//         return -1;
//     }

//     *out = (uint16_t)v;
//     MAIN_LOG("parse_u16_string ok value='%s' parsed=%u", s, (unsigned)*out);
//     return 0;
// }

// static void force_sw_lrf_led_off(led_status_router_t *led_router,
//                                  unsigned long loop_count,
//                                  const char *reason)
// {
//     int rc;

//     if (led_router == NULL) {
//         return;
//     }

//     rc = led_status_router_set_led(led_router, "SW_LRF", 0u);
//     if (rc < 0) {
//         MAIN_LOG("loop=%lu sw_lrf off failed reason=%s rc=%d",
//                  loop_count,
//                  (reason != NULL) ? reason : "unknown",
//                  rc);
//     } else if (rc > 0) {
//         MAIN_LOG("loop=%lu sw_lrf off sent reason=%s",
//                  loop_count,
//                  (reason != NULL) ? reason : "unknown");
//     }
// }

// static int parse_bool_string(const char *s, bool *out)
// {
//     if ((s == NULL) || (out == NULL)) {
//         MAIN_LOG0("parse_bool_string invalid input");
//         return -1;
//     }

//     if ((strcmp(s, "1") == 0) ||
//         (platform_strcasecmp(s, "true") == 0) ||
//         (platform_strcasecmp(s, "yes") == 0) ||
//         (platform_strcasecmp(s, "on") == 0)) {
//         *out = true;
//         MAIN_LOG("parse_bool_string value='%s' parsed=true", s);
//         return 0;
//     }
//     if ((strcmp(s, "0") == 0) ||
//         (platform_strcasecmp(s, "false") == 0) ||
//         (platform_strcasecmp(s, "no") == 0) ||
//         (platform_strcasecmp(s, "off") == 0)) {
//         *out = false;
//         MAIN_LOG("parse_bool_string value='%s' parsed=false", s);
//         return 0;
//     }

//     MAIN_LOG("parse_bool_string unrecognized value='%s'", s);
//     return -1;
// }

// static int load_camera_settings(const char *path, camera_params_t *params, uint16_t *tcp_port_out, bool *telemetry_enable_out)
// {
//     char value[64];

//     if ((path == NULL) || (params == NULL) || (tcp_port_out == NULL) || (telemetry_enable_out == NULL)) {
//         MAIN_LOG0("load_camera_settings invalid input");
//         return -1;
//     }

//     MAIN_LOG("loading camera settings from '%s'", path);
//     memset(params, 0, sizeof(*params));
//     *tcp_port_out = OWL_TCP_PORT;
//     *telemetry_enable_out = false;

//     if (ini_get_value(path, "camera", "name", value, sizeof(value)) == 0) {
//         (void)strncpy(params->name, value, sizeof(params->name) - 1u);
//         params->name[sizeof(params->name) - 1u] = '\0';
//         MAIN_LOG("camera.name='%s'", params->name);
//     }
//     if (ini_get_value(path, "camera", "ip", value, sizeof(value)) == 0) {
//         (void)strncpy(params->camera_ip, value, sizeof(params->camera_ip) - 1u);
//         params->camera_ip[sizeof(params->camera_ip) - 1u] = '\0';
//         MAIN_LOG("camera.ip='%s'", params->camera_ip);
//     } else {
//         MAIN_LOG0("camera.ip not found, default will be used by validation");
//     }
//     if (ini_get_value(path, "camera", "tcp_port", value, sizeof(value)) == 0) {
//         (void)parse_u16_string(value, tcp_port_out);
//         MAIN_LOG("camera.tcp_port=%u", (unsigned)*tcp_port_out);
//     } else {
//         MAIN_LOG("camera.tcp_port not found, using default=%u", (unsigned)*tcp_port_out);
//     }
//     if (ini_get_value(path, "mqtt", "broker", value, sizeof(value)) == 0) {
//         (void)strncpy(params->mqtt_broker, value, sizeof(params->mqtt_broker) - 1u);
//         params->mqtt_broker[sizeof(params->mqtt_broker) - 1u] = '\0';
//         MAIN_LOG("mqtt.broker='%s'", params->mqtt_broker);
//     } else {
//         MAIN_LOG0("mqtt.broker not found");
//     }
//     if (ini_get_value(path, "mqtt", "port", value, sizeof(value)) == 0) {
//         (void)parse_u16_string(value, &params->mqtt_port);
//         MAIN_LOG("mqtt.port=%u", (unsigned)params->mqtt_port);
//     }
//     if (ini_get_value(path, "mqtt", "client_id", value, sizeof(value)) == 0) {
//         (void)strncpy(params->mqtt_client_id, value, sizeof(params->mqtt_client_id) - 1u);
//         params->mqtt_client_id[sizeof(params->mqtt_client_id) - 1u] = '\0';
//         MAIN_LOG("mqtt.client_id='%s'", params->mqtt_client_id);
//     } else {
//         MAIN_LOG0("mqtt.client_id not found");
//     }
//     if (ini_get_value(path, "mqtt", "root_topic", value, sizeof(value)) == 0) {
//         (void)strncpy(params->root_topic, value, sizeof(params->root_topic) - 1u);
//         params->root_topic[sizeof(params->root_topic) - 1u] = '\0';
//         MAIN_LOG("mqtt.root_topic='%s'", params->root_topic);
//     } else {
//         MAIN_LOG0("mqtt.root_topic not found");
//     }
//     if (ini_get_value(path, "mqtt", "retained", value, sizeof(value)) == 0) {
//         (void)parse_bool_string(value, &params->mqtt_retained);
//         MAIN_LOG("mqtt.retained=%d", params->mqtt_retained ? 1 : 0);
//     }
//     if ((ini_get_value(path, "mcast_camera", "enabled", value, sizeof(value)) == 0) ||
//         (ini_get_value(path, "udp_multicast", "enabled", value, sizeof(value)) == 0)) {
//         (void)parse_bool_string(value, &params->udp_mcast_enabled);
//         MAIN_LOG("multicast.camera.enabled=%d", params->udp_mcast_enabled ? 1 : 0);
//     }
//     if ((ini_get_value(path, "mcast_camera", "group_ip", value, sizeof(value)) == 0) ||
//         (ini_get_value(path, "udp_multicast", "group_ip", value, sizeof(value)) == 0)) {
//         (void)strncpy(params->udp_mcast_group_ip, value, sizeof(params->udp_mcast_group_ip) - 1u);
//         params->udp_mcast_group_ip[sizeof(params->udp_mcast_group_ip) - 1u] = '\0';
//         MAIN_LOG("multicast.camera.group_ip='%s'", params->udp_mcast_group_ip);
//     }
//     if ((ini_get_value(path, "mcast_camera", "port", value, sizeof(value)) == 0) ||
//         (ini_get_value(path, "udp_multicast", "port", value, sizeof(value)) == 0)) {
//         (void)parse_u16_string(value, &params->udp_mcast_port);
//         MAIN_LOG("multicast.camera.port=%u", (unsigned)params->udp_mcast_port);
//     }
//     if ((ini_get_value(path, "mcast_common", "iface_ip", value, sizeof(value)) == 0) ||
//         (ini_get_value(path, "udp_multicast", "iface_ip", value, sizeof(value)) == 0)) {
//         (void)strncpy(params->udp_mcast_iface_ip, value, sizeof(params->udp_mcast_iface_ip) - 1u);
//         params->udp_mcast_iface_ip[sizeof(params->udp_mcast_iface_ip) - 1u] = '\0';
//         MAIN_LOG("multicast.common.iface_ip='%s'", params->udp_mcast_iface_ip);
//     }
//     if ((ini_get_value(path, "mcast_common", "ttl", value, sizeof(value)) == 0) ||
//         (ini_get_value(path, "udp_multicast", "ttl", value, sizeof(value)) == 0)) {
//         uint16_t parsed = 0u;
//         if (parse_u16_string(value, &parsed) == 0) {
//             params->udp_mcast_ttl = (uint8_t)parsed;
//         }
//         MAIN_LOG("multicast.common.ttl=%u", (unsigned)params->udp_mcast_ttl);
//     }
//     if ((ini_get_value(path, "mcast_common", "loopback", value, sizeof(value)) == 0) ||
//         (ini_get_value(path, "udp_multicast", "loopback", value, sizeof(value)) == 0)) {
//         bool loopback = false;
//         if (parse_bool_string(value, &loopback) == 0) {
//             params->udp_mcast_loopback = loopback ? 1u : 0u;
//         }
//         MAIN_LOG("multicast.common.loopback=%u", (unsigned)params->udp_mcast_loopback);
//     }
//     if ((ini_get_value(path, "mcast_common", "camera_id", value, sizeof(value)) == 0) ||
//         (ini_get_value(path, "udp_multicast", "camera_id", value, sizeof(value)) == 0)) {
//         uint16_t parsed = 0u;
//         if (parse_u16_string(value, &parsed) == 0) {
//             params->udp_camera_id = (uint8_t)parsed;
//         }
//         MAIN_LOG("multicast.common.camera_id=%u", (unsigned)params->udp_camera_id);
//     }
//     if (ini_get_value(path, "camera", "telemetry_enabled", value, sizeof(value)) == 0) {
//         (void)parse_bool_string(value, telemetry_enable_out);
//     } else if ((ini_get_value(path, "mcast_camera", "enabled", value, sizeof(value)) == 0) ||
//                (ini_get_value(path, "udp_multicast", "enabled", value, sizeof(value)) == 0)) {
//         (void)parse_bool_string(value, telemetry_enable_out);
//     }

//     if (!camera_params_validate(params)) {
//         MAIN_LOG0("camera_params_validate failed");
//         return -1;
//     }

//     MAIN_LOG("camera settings validated: ip=%s mqtt_broker=%s root_topic=%s",
//              params->camera_ip,
//              params->mqtt_broker,
//              params->root_topic);
//     return 0;
// }

// static int load_api2_settings(const char *path, api2_cfg_t *cfg)
// {
//     char value[64];

//     if ((path == NULL) || (cfg == NULL)) {
//         MAIN_LOG0("load_api2_settings invalid input");
//         return -1;
//     }

//     api2_cfg_set_defaults(cfg);

//     if (ini_get_value(path, "mcast_common", "iface_ip", value, sizeof(value)) == 0) {
//         (void)strncpy(cfg->iface_ip, value, sizeof(cfg->iface_ip) - 1u);
//         cfg->iface_ip[sizeof(cfg->iface_ip) - 1u] = '\0';
//         MAIN_LOG("api2.iface_ip='%s'", cfg->iface_ip);
//     }
//     if (ini_get_value(path, "mcast_common", "ttl", value, sizeof(value)) == 0) {
//         uint16_t parsed = 0u;
//         if (parse_u16_string(value, &parsed) == 0) {
//             cfg->ttl = (uint8_t)parsed;
//         }
//         MAIN_LOG("api2.ttl=%u", (unsigned)cfg->ttl);
//     }
//     if (ini_get_value(path, "mcast_common", "loopback", value, sizeof(value)) == 0) {
//         bool loopback = false;
//         if (parse_bool_string(value, &loopback) == 0) {
//             cfg->loopback = loopback ? 1u : 0u;
//         }
//         MAIN_LOG("api2.loopback=%u", (unsigned)cfg->loopback);
//     }
//     if (ini_get_value(path, "mcast_common", "camera_id", value, sizeof(value)) == 0) {
//         uint16_t parsed = 0u;
//         if (parse_u16_string(value, &parsed) == 0) {
//             cfg->camera_id = (uint8_t)parsed;
//         }
//         MAIN_LOG("api2.camera_id=%u", (unsigned)cfg->camera_id);
//     }

//     if (ini_get_value(path, "mcast_camera", "enabled", value, sizeof(value)) == 0) {
//         bool enabled = false;
//         if (parse_bool_string(value, &enabled) == 0) {
//             cfg->camera.enabled = enabled ? 1u : 0u;
//         }
//         MAIN_LOG("api2.camera.enabled=%u", (unsigned)cfg->camera.enabled);
//     }
//     if (ini_get_value(path, "mcast_camera", "group_ip", value, sizeof(value)) == 0) {
//         (void)strncpy(cfg->camera.group_ip, value, sizeof(cfg->camera.group_ip) - 1u);
//         cfg->camera.group_ip[sizeof(cfg->camera.group_ip) - 1u] = '\0';
//         MAIN_LOG("api2.camera.group_ip='%s'", cfg->camera.group_ip);
//     }
//     if (ini_get_value(path, "mcast_camera", "port", value, sizeof(value)) == 0) {
//         (void)parse_u16_string(value, &cfg->camera.port);
//         MAIN_LOG("api2.camera.port=%u", (unsigned)cfg->camera.port);
//     }

//     if (ini_get_value(path, "mcast_joystick", "enabled", value, sizeof(value)) == 0) {
//         bool enabled = false;
//         if (parse_bool_string(value, &enabled) == 0) {
//             cfg->joystick.enabled = enabled ? 1u : 0u;
//         }
//         MAIN_LOG("api2.joystick.enabled=%u", (unsigned)cfg->joystick.enabled);
//     }
//     if (ini_get_value(path, "mcast_joystick", "group_ip", value, sizeof(value)) == 0) {
//         (void)strncpy(cfg->joystick.group_ip, value, sizeof(cfg->joystick.group_ip) - 1u);
//         cfg->joystick.group_ip[sizeof(cfg->joystick.group_ip) - 1u] = '\0';
//         MAIN_LOG("api2.joystick.group_ip='%s'", cfg->joystick.group_ip);
//     }
//     if (ini_get_value(path, "mcast_joystick", "port", value, sizeof(value)) == 0) {
//         (void)parse_u16_string(value, &cfg->joystick.port);
//         MAIN_LOG("api2.joystick.port=%u", (unsigned)cfg->joystick.port);
//     }

//     if (ini_get_value(path, "mcast_button_led", "enabled", value, sizeof(value)) == 0) {
//         bool enabled = false;
//         if (parse_bool_string(value, &enabled) == 0) {
//             cfg->button_led.enabled = enabled ? 1u : 0u;
//         }
//         MAIN_LOG("api2.button_led.enabled=%u", (unsigned)cfg->button_led.enabled);
//     }
//     if (ini_get_value(path, "mcast_button_led", "group_ip", value, sizeof(value)) == 0) {
//         (void)strncpy(cfg->button_led.group_ip, value, sizeof(cfg->button_led.group_ip) - 1u);
//         cfg->button_led.group_ip[sizeof(cfg->button_led.group_ip) - 1u] = '\0';
//         MAIN_LOG("api2.button_led.group_ip='%s'", cfg->button_led.group_ip);
//     }
//     if (ini_get_value(path, "mcast_button_led", "port", value, sizeof(value)) == 0) {
//         (void)parse_u16_string(value, &cfg->button_led.port);
//         MAIN_LOG("api2.button_led.port=%u", (unsigned)cfg->button_led.port);
//     }

//     return 0;
// }

// static int recover_camera_connection(owl_cam_t *cam,
//                                      led_status_router_t *led_router,
//                                      const camera_params_t *cam_cfg,
//                                      uint16_t cam_tcp_port,
//                                      bool telemetry_enable,
//                                      unsigned long loop_count,
//                                      const char *reason)
// {
//     bool alive = false;
//     int rc;

//     if ((cam == NULL) || (cam_cfg == NULL) || (reason == NULL)) {
//         return OWL_ERR_ARG;
//     }

//     MAIN_LOG("loop=%lu camera recover start reason=%s", loop_count, reason);
//     owl_cam_close(cam);

//     rc = owl_cam_open(cam, cam_cfg->camera_ip, cam_tcp_port);
//     if (rc != OWL_OK) {
//         MAIN_LOG("loop=%lu camera recover open failed rc=%d", loop_count, rc);
//         force_sw_lrf_led_off(led_router, loop_count, "camera_recover_open_failed");
//         return rc;
//     }

//     rc = owl_cam_liveliness(cam, &alive);
//     if ((rc != OWL_OK) || (!alive)) {
//         MAIN_LOG("loop=%lu camera recover liveliness failed rc=%d alive=%d",
//                  loop_count, rc, alive ? 1 : 0);
//         force_sw_lrf_led_off(led_router, loop_count, "camera_recover_liveliness_failed");
//         return OWL_ERR_IO;
//     }

//     rc = owl_cam_set_telemetry(cam, telemetry_enable);
//     if (rc != OWL_OK) {
//         MAIN_LOG("loop=%lu camera recover telemetry set failed rc=%d", loop_count, rc);
//     }

//     /*
//      * Do not auto-enable tracker after reconnect.
//      * Tracker state is reset to OFF until start command comes again.
//      */
//     g_tracker_mode_on = 0;

//     MAIN_LOG("loop=%lu camera recover done tracker_mode_on=%d", loop_count, g_tracker_mode_on);
//     return OWL_OK;
// }

// static int call_cam_u8_with_recover(int (*fn)(owl_cam_t *, uint8_t),
//                                     owl_cam_t *cam,
//                                     led_status_router_t *led_router,
//                                     uint8_t arg,
//                                     const camera_params_t *cam_cfg,
//                                     uint16_t cam_tcp_port,
//                                     bool telemetry_enable,
//                                     unsigned long loop_count,
//                                     const char *op_name)
// {
//     int rc;

//     if ((fn == NULL) || (cam == NULL) || (cam_cfg == NULL) || (op_name == NULL)) {
//         return OWL_ERR_ARG;
//     }

//     rc = fn(cam, arg);
//     if (rc != OWL_ERR_IO) {
//         return rc;
//     }

//     MAIN_LOG("loop=%lu %s hit OWL_ERR_IO, attempting reconnect", loop_count, op_name);
//     if (recover_camera_connection(cam, led_router, cam_cfg, cam_tcp_port, telemetry_enable, loop_count, op_name) != OWL_OK) {
//         return rc;
//     }
//     return fn(cam, arg);
// }

// static int call_cam_noarg_with_recover(int (*fn)(owl_cam_t *),
//                                        owl_cam_t *cam,
//                                        led_status_router_t *led_router,
//                                        const camera_params_t *cam_cfg,
//                                        uint16_t cam_tcp_port,
//                                        bool telemetry_enable,
//                                        unsigned long loop_count,
//                                        const char *op_name)
// {
//     int rc;

//     if ((fn == NULL) || (cam == NULL) || (cam_cfg == NULL) || (op_name == NULL)) {
//         return OWL_ERR_ARG;
//     }

//     rc = fn(cam);
//     if (rc != OWL_ERR_IO) {
//         return rc;
//     }

//     MAIN_LOG("loop=%lu %s hit OWL_ERR_IO, attempting reconnect", loop_count, op_name);
//     if (recover_camera_connection(cam, led_router, cam_cfg, cam_tcp_port, telemetry_enable, loop_count, op_name) != OWL_OK) {
//         return rc;
//     }
//     return fn(cam);
// }

// static int call_cam_bool_with_recover(int (*fn)(owl_cam_t *, bool),
//                                       owl_cam_t *cam,
//                                       led_status_router_t *led_router,
//                                       bool arg,
//                                       const camera_params_t *cam_cfg,
//                                       uint16_t cam_tcp_port,
//                                       bool telemetry_enable,
//                                       unsigned long loop_count,
//                                       const char *op_name)
// {
//     int rc;

//     if ((fn == NULL) || (cam == NULL) || (cam_cfg == NULL) || (op_name == NULL)) {
//         return OWL_ERR_ARG;
//     }

//     rc = fn(cam, arg);
//     if (rc != OWL_ERR_IO) {
//         return rc;
//     }

//     MAIN_LOG("loop=%lu %s hit OWL_ERR_IO, attempting reconnect", loop_count, op_name);
//     if (recover_camera_connection(cam, led_router, cam_cfg, cam_tcp_port, telemetry_enable, loop_count, op_name) != OWL_OK) {
//         return rc;
//     }
//     return fn(cam, arg);
// }

// static int update_camera_switch_leds(led_status_router_t *led_router,
//                                      const char *selected_button,
//                                      unsigned long loop_count)
// {
//     int led_rc;

//     if ((led_router == NULL) || (selected_button == NULL)) {
//         return -1;
//     }

//     led_rc = led_status_router_set_led(led_router, "DAY",
//                                        (strcmp(selected_button, "DAY") == 0) ? 1u : 0u);
//     if (led_rc < 0) {
//         MAIN_LOG("loop=%lu led update failed for DAY rc=%d", loop_count, led_rc);
//     }

//     led_rc = led_status_router_set_led(led_router, "LOW_LIGHT",
//                                        (strcmp(selected_button, "LOW_LIGHT") == 0) ? 1u : 0u);
//     if (led_rc < 0) {
//         MAIN_LOG("loop=%lu led update failed for LOW_LIGHT rc=%d", loop_count, led_rc);
//     }

//     led_rc = led_status_router_set_led(led_router, "THERMAL",
//                                        (strcmp(selected_button, "THERMAL") == 0) ? 1u : 0u);
//     if (led_rc < 0) {
//         MAIN_LOG("loop=%lu led update failed for THERMAL rc=%d", loop_count, led_rc);
//     }

//     return 0;
// }

// static int handle_camera_view_switch_command(owl_cam_t *cam,
//                                              led_status_router_t *led_router,
//                                              const camera_params_t *cam_cfg,
//                                              uint16_t cam_tcp_port,
//                                              bool telemetry_enable,
//                                              uint8_t id,
//                                              uint8_t value,
//                                              unsigned long loop_count)
// {
//     uint8_t cam_id = 0u;
//     uint16_t tracker_x = 0u;
//     uint16_t tracker_y = 0u;
//     const char *selected_button = NULL;
//     int rc;

//     if ((cam == NULL) || (led_router == NULL) || (cam_cfg == NULL)) {
//         return -1;
//     }

//     switch (id) {
//     case 11u:
//         cam_id = OWL_TRACK_CAM_DAY2;
//         selected_button = "DAY";
//         break;
//     case 12u:
//         cam_id = OWL_TRACK_CAM_DAY1;
//         selected_button = "LOW_LIGHT";
//         break;
//     case 13u:
//         cam_id = OWL_TRACK_CAM_THERMAL;
//         selected_button = "THERMAL";
//         break;
//     default:
//         return 0;
//     }

//     if ((value != 0u) && (value != 1u)) {
//         MAIN_LOG("loop=%lu view switch unsupported value id=%u value=%u",
//                  loop_count, (unsigned)id, (unsigned)value);
//         return 1;
//     }

//     rc = call_cam_u8_with_recover(owl_cam_tracker_switch_camera,
//                                   cam,
//                                   led_router,
//                                   cam_id,
//                                   cam_cfg,
//                                   cam_tcp_port,
//                                   telemetry_enable,
//                                   loop_count,
//                                   "owl_cam_tracker_switch_camera");
//     if (rc != OWL_OK) {
//         MAIN_LOG("loop=%lu tracker camera switch failed id=%u value=%u button=%s cam_id=%u rc=%d",
//                  loop_count,
//                  (unsigned)id,
//                  (unsigned)value,
//                  selected_button,
//                  (unsigned)cam_id,
//                  rc);
//         return -3;
//     }

//     MAIN_LOG("loop=%lu tracker camera switch ok id=%u value=%u button=%s cam_id=%u",
//              loop_count,
//              (unsigned)id,
//              (unsigned)value,
//              selected_button,
//              (unsigned)cam_id);

//     if (g_tracker_mode_on != 0) {
//         get_tracker_center_for_cam(cam_id, &tracker_x, &tracker_y);
//         rc = main_tracker_set_coord(cam,
//                                     OWL_IFACE_TRACKER,
//                                     cam_id,
//                                     tracker_x,
//                                     tracker_y,
//                                     OWL_TRACKER_START);
//         if (rc == OWL_ERR_IO) {
//             MAIN_LOG("loop=%lu tracker restart on selected camera hit OWL_ERR_IO, reconnecting",
//                      loop_count);
//             if (recover_camera_connection(cam, led_router, cam_cfg, cam_tcp_port, telemetry_enable, loop_count, "tracker_restart_on_view_switch") == OWL_OK) {
//                 rc = main_tracker_set_coord(cam,
//                                             OWL_IFACE_TRACKER,
//                                             cam_id,
//                                             tracker_x,
//                                             tracker_y,
//                                             OWL_TRACKER_START);
//             }
//         }

//         log_camera_response("tracker restart on selected camera", rc);
//         if (rc != OWL_OK) {
//             MAIN_LOG("loop=%lu tracker restart on selected camera failed id=%u value=%u button=%s cam_id=%u x=%u y=%u rc=%d",
//                      loop_count,
//                      (unsigned)id,
//                      (unsigned)value,
//                      selected_button,
//                      (unsigned)cam_id,
//                      (unsigned)tracker_x,
//                      (unsigned)tracker_y,
//                      rc);
//             return -4;
//         }

//         g_last_tracker_state.coord_cam_id = cam_id;
//         g_last_tracker_state.coord_x = tracker_x;
//         g_last_tracker_state.coord_y = tracker_y;
//         g_last_tracker_state.coord_mode = OWL_TRACKER_START;
//         g_last_tracker_valid = 1;

//         MAIN_LOG("loop=%lu tracker restart on selected camera ok button=%s cam_id=%u x=%u y=%u",
//                  loop_count,
//                  selected_button,
//                  (unsigned)cam_id,
//                  (unsigned)tracker_x,
//                  (unsigned)tracker_y);
//     }

//     g_selected_view_cam = cam_id;

//     MAIN_LOG("loop=%lu camera view switch ok id=%u value=%u button=%s selected_cam=%u tracker_mode_on=%d",
//              loop_count,
//              (unsigned)id,
//              (unsigned)value,
//              selected_button,
//              (unsigned)g_selected_view_cam,
//              g_tracker_mode_on);

//     (void)update_camera_switch_leds(led_router, selected_button, loop_count);
//     return 1;
// }

// /*
//  * DROP button (id 14) tracker on/off handling is intentionally disabled.
//  * Keep the old implementation below for reference.
//  */
// #if 0
// static int handle_drop_tracker_command(owl_cam_t *cam,
//                                        led_status_router_t *led_router,
//                                        const camera_params_t *cam_cfg,
//                                        uint16_t cam_tcp_port,
//                                        bool telemetry_enable,
//                                        uint8_t id,
//                                        uint8_t value,
//                                        unsigned long loop_count)
// {
//     const uint8_t tracker_cam = g_selected_view_cam;
//     const uint8_t tracker_mode = (value == 1u) ? OWL_TRK_MODE_ON : OWL_TRK_MODE_OFF;
//     uint16_t tracker_x = 0u;
//     uint16_t tracker_y = 0u;
//     int rc;
//     int led_rc;

//     if ((cam == NULL) || (led_router == NULL) || (cam_cfg == NULL)) {
//         return -1;
//     }

//     if (id != 14u) {
//         return 0;
//     }

//     if ((value != 0u) && (value != 1u)) {
//         MAIN_LOG("loop=%lu drop tracker unsupported value id=%u value=%u",
//                  loop_count, (unsigned)id, (unsigned)value);
//         return 1;
//     }

//     get_tracker_center_for_cam(tracker_cam, &tracker_x, &tracker_y);

//     rc = owl_tracker_set_coord(tracker_cam, tracker_x, tracker_y, tracker_mode);
//     if (rc == OWL_ERR_IO) {
//         MAIN_LOG("loop=%lu drop tracker hit OWL_ERR_IO, reconnecting", loop_count);
//         if (recover_camera_connection(cam, led_router, cam_cfg, cam_tcp_port, telemetry_enable, loop_count, "drop_tracker") == OWL_OK) {
//             rc = owl_tracker_set_coord(tracker_cam, tracker_x, tracker_y, tracker_mode);
//         }
//     }

//     log_camera_response("drop tracker", rc);
//     if (rc != OWL_OK) {
//         MAIN_LOG("loop=%lu drop tracker failed id=%u value=%u cam=%u x=%u y=%u rc=%d",
//                  loop_count,
//                  (unsigned)id,
//                  (unsigned)value,
//                  (unsigned)tracker_cam,
//                  (unsigned)tracker_x,
//                  (unsigned)tracker_y,
//                  rc);
//         return -2;
//     }

//     g_tracker_mode_on = (value == 1u) ? 1 : 0;
//     g_last_tracker_state.coord_cam_id = tracker_cam;
//     g_last_tracker_state.coord_x = tracker_x;
//     g_last_tracker_state.coord_y = tracker_y;
//     g_last_tracker_state.coord_mode = tracker_mode;
//     g_last_tracker_valid = 1;

//     // led_rc = led_status_router_set_led(led_router, "DROP", (value == 1u) ? 1u : 0u);
//     // if (led_rc < 0) {
//     //     MAIN_LOG("loop=%lu drop led update failed rc=%d", loop_count, led_rc);
//     //     return -3;
//     // }

//     MAIN_LOG("loop=%lu drop tracker ACK ok id=%u value=%u cam=%u x=%u y=%u tracker_mode_on=%d",
//              loop_count,
//              (unsigned)id,
//              (unsigned)value,
//              (unsigned)tracker_cam,
//              (unsigned)tracker_x,
//              (unsigned)tracker_y,
//              g_tracker_mode_on);
//     return 1;
// }
// #endif

// static int handle_optics_reset_command(owl_cam_t *cam,
//                                        led_status_router_t *led_router,
//                                        const camera_params_t *cam_cfg,
//                                        uint16_t cam_tcp_port,
//                                        bool telemetry_enable,
//                                        uint8_t id,
//                                        uint8_t value,
//                                        unsigned long loop_count)
// {
//     int rc;
//     int led_rc;

//     if ((cam == NULL) || (led_router == NULL) || (cam_cfg == NULL)) {
//         return -1;
//     }

//     if (id != 2u) {
//         return 0;
//     }

//     if (value == 1u) {
//         rc = call_cam_u8_with_recover(owl_cam_restart,
//                                       cam,
//                                       led_router,
//                                       OWL_RESTART_SERVICE,
//                                       cam_cfg,
//                                       cam_tcp_port,
//                                       telemetry_enable,
//                                       loop_count,
//                                       "owl_cam_restart");
//         if (rc != OWL_OK) {
//             MAIN_LOG("loop=%lu optics reset failed id=%u value=%u rc=%d",
//                      loop_count, (unsigned)id, (unsigned)value, rc);
//             return -2;
//         }

//         MAIN_LOG("loop=%lu optics reset ACK ok id=%u value=%u",
//                  loop_count, (unsigned)id, (unsigned)value);
//         led_rc = led_status_router_set_led(led_router, "OPTICS_RESET", 1u);
//         if (led_rc < 0) {
//             MAIN_LOG("loop=%lu optics reset led on failed rc=%d", loop_count, led_rc);
//         }
//         return 1;
//     }

//     if (value == 0u) {
//         uint8_t led_id = 0u;
//         rc = config_get_button_led_id_by_button_name(led_router->cfg, "OPTICS_RESET", &led_id);
//         if (rc != 0) {
//             MAIN_LOG("loop=%lu optics reset led mapping failed rc=%d", loop_count, rc);
//             return -3;
//         }
//         rc = uart_send_control(led_router->uart, led_id, 0u);
//         if (rc != 0) {
//             MAIN_LOG("loop=%lu optics reset led off uart send failed led_id=%u rc=%d",
//                      loop_count, (unsigned)led_id, rc);
//             return -4;
//         }
//         MAIN_LOG("loop=%lu optics reset led off sent led_id=%u value=0",
//                  loop_count, (unsigned)led_id);
//         led_rc = led_status_router_set_led(led_router, "OPTICS_RESET", 0u);
//         if (led_rc < 0) {
//             MAIN_LOG("loop=%lu optics reset led state sync failed rc=%d", loop_count, led_rc);
//         }
//         MAIN_LOG("loop=%lu optics reset released id=%u value=%u", loop_count, (unsigned)id, (unsigned)value);
//         return 1;
//     }

//     MAIN_LOG("loop=%lu optics reset unsupported value=%u", loop_count, (unsigned)value);
//     return 1;
// }

// static int handle_lrf_reset_command(owl_cam_t *cam,
//                                     led_status_router_t *led_router,
//                                     const camera_params_t *cam_cfg,
//                                     uint16_t cam_tcp_port,
//                                     bool telemetry_enable,
//                                     uint8_t id,
//                                     uint8_t value,
//                                     unsigned long loop_count)
// {
//     const uint8_t lrf_led_id = 76u;
//     int rc;
//     int led_rc;

//     if ((cam == NULL) || (led_router == NULL) || (cam_cfg == NULL)) {
//         return -1;
//     }

//     if (id != 3u) {
//         return 0;
//     }

//     if ((value != 0u) && (value != 1u)) {
//         return 0;
//     }

//     led_rc = led_status_router_set_led(led_router, "LRF_RESET", 1u);
//     if (led_rc < 0) {
//         MAIN_LOG("loop=%lu lrf reset led on failed rc=%d", loop_count, led_rc);
//     } else {
//         MAIN_LOG("loop=%lu lrf reset led on sent", loop_count);
//     }

//     rc = call_cam_noarg_with_recover(owl_cam_lrf_stop,
//                                      cam,
//                                      led_router,
//                                      cam_cfg,
//                                      cam_tcp_port,
//                                      telemetry_enable,
//                                      loop_count,
//                                      "owl_cam_lrf_stop");
//     if (rc != OWL_OK) {
//         MAIN_LOG("loop=%lu lrf reset failed id=%u value=%u rc=%d",
//                  loop_count, (unsigned)id, (unsigned)value, rc);
//         return -2;
//     }

//     rc = call_cam_bool_with_recover(owl_cam_lrf_align_pointer,
//                                     cam,
//                                     led_router,
//                                     false,
//                                     cam_cfg,
//                                     cam_tcp_port,
//                                     telemetry_enable,
//                                     loop_count,
//                                     "owl_cam_lrf_align_pointer");
//     if (rc != OWL_OK) {
//         MAIN_LOG("loop=%lu lrf reset pointer off failed id=%u value=%u rc=%d",
//                  loop_count, (unsigned)id, (unsigned)value, rc);
//         return -2;
//     }

//     rc = uart_send_control(led_router->uart, lrf_led_id, 0u);
//     if (rc != 0) {
//         MAIN_LOG("loop=%lu lrf reset pointer led off uart send failed led_id=%u rc=%d",
//                  loop_count, (unsigned)lrf_led_id, rc);
//         return -3;
//     }

//     MAIN_LOG("loop=%lu lrf reset ACK ok id=%u value=%u",
//              loop_count, (unsigned)id, (unsigned)value);
//     led_rc = led_status_router_set_led(led_router, "LRF_RESET", 0u);
//     if (led_rc < 0) {
//         MAIN_LOG("loop=%lu lrf reset led off failed rc=%d", loop_count, led_rc);
//     } else {
//         MAIN_LOG("loop=%lu lrf reset led off sent after ACK", loop_count);
//     }
//     return 1;
// }

// static int handle_lrf_frequency_command(owl_cam_t *cam,
//                                         led_status_router_t *led_router,
//                                         const camera_params_t *cam_cfg,
//                                         uint16_t cam_tcp_port,
//                                         bool telemetry_enable,
//                                         uint8_t id,
//                                         uint8_t value,
//                                         unsigned long loop_count)
// {
//     uint8_t freq_mode = 0u;
//     int rc;

//     if ((cam == NULL) || (led_router == NULL) || (cam_cfg == NULL)) {
//         return -1;
//     }

//     if (id != 53u) {
//         return 0;
//     }

//     switch (value) {
//     case 1u:   freq_mode = OWL_LRF_FREQ_1HZ;   break;
//     case 4u:   freq_mode = OWL_LRF_FREQ_4HZ;   break;
//     case 10u:  freq_mode = OWL_LRF_FREQ_10HZ;  break;
//     case 20u:  freq_mode = OWL_LRF_FREQ_20HZ;  break;
//     case 100u: freq_mode = OWL_LRF_FREQ_100HZ; break;
//     case 200u: freq_mode = OWL_LRF_FREQ_200HZ; break;
//     default:
//         MAIN_LOG("loop=%lu lrf frequency unsupported raw value=%u",
//                  loop_count, (unsigned)value);
//         return 1;
//     }

//     rc = call_cam_u8_with_recover(owl_cam_lrf_set_frequency,
//                                   cam,
//                                   led_router,
//                                   freq_mode,
//                                   cam_cfg,
//                                   cam_tcp_port,
//                                   telemetry_enable,
//                                   loop_count,
//                                   "owl_cam_lrf_set_frequency");
//     if (rc != OWL_OK) {
//         MAIN_LOG("loop=%lu lrf frequency set failed raw=%u mode=%u rc=%d",
//                  loop_count, (unsigned)value, (unsigned)freq_mode, rc);
//         return -2;
//     }

//     MAIN_LOG("loop=%lu lrf frequency ACK ok raw=%u mode=%u",
//              loop_count, (unsigned)value, (unsigned)freq_mode);
//     return 1;
// }

// static int handle_lrf_single_measure_mode_command(owl_cam_t *cam,
//                                                   led_status_router_t *led_router,
//                                                   const camera_params_t *cam_cfg,
//                                                   uint16_t cam_tcp_port,
//                                                   bool telemetry_enable,
//                                                   uint8_t id,
//                                                   uint8_t value,
//                                                   unsigned long loop_count)
// {
//     uint8_t measure_mode = 0u;
//     const char *mode_name = NULL;
//     int rc;

//     if ((cam == NULL) || (led_router == NULL) || (cam_cfg == NULL)) {
//         return -1;
//     }

//     if (id != 52u) {
//         return 0;
//     }

//     switch (value) {
//     case 0u:
//         measure_mode = OWL_LRF_SMM;
//         mode_name = "SMM";
//         break;
//     case 2u:
//         measure_mode = OWL_LRF_CH1;
//         mode_name = "CH1";
//         break;
//     case 1u:
//         measure_mode = OWL_LRF_CH2;
//         mode_name = "CH2";
//         break;
//     default:
//         MAIN_LOG("loop=%lu lrf single measure unsupported raw value=%u",
//                  loop_count, (unsigned)value);
//         return 1;
//     }

//     rc = call_cam_u8_with_recover(owl_cam_lrf_single_measure,
//                                   cam,
//                                   led_router,
//                                   measure_mode,
//                                   cam_cfg,
//                                   cam_tcp_port,
//                                   telemetry_enable,
//                                   loop_count,
//                                   "owl_cam_lrf_single_measure");
//     if (rc != OWL_OK) {
//         MAIN_LOG("loop=%lu lrf single measure failed raw=%u mode=%u rc=%d",
//                  loop_count, (unsigned)value, (unsigned)measure_mode, rc);
//         return -2;
//     }

//     MAIN_LOG("loop=%lu lrf single measure ACK ok raw=%u mode=%u",
//              loop_count, (unsigned)value, (unsigned)measure_mode);
//     MAIN_LOG("%s mode on", mode_name);
//     return 1;
// }

// static int handle_lrf_align_pointer_command(owl_cam_t *cam,
//                                             led_status_router_t *led_router,
//                                             uart_t *uart,
//                                             const camera_params_t *cam_cfg,
//                                             uint16_t cam_tcp_port,
//                                             bool telemetry_enable,
//                                             uint8_t id,
//                                             uint8_t value,
//                                             unsigned long loop_count)
// {
//     const uint8_t lrf_button_id = 75u;
//     const uint8_t lrf_led_id = 76u;
//     bool enable;
//     int rc;

//     if ((cam == NULL) || (led_router == NULL) || (uart == NULL) || (cam_cfg == NULL)) {
//         return -1;
//     }

//     if (id != lrf_button_id) {
//         return 0;
//     }

//     if ((value != 0u) && (value != 1u)) {
//         MAIN_LOG("loop=%lu lrf align pointer unsupported value=%u",
//                  loop_count, (unsigned)value);
//         return 1;
//     }

//     enable = (value == 1u);
//     rc = call_cam_bool_with_recover(owl_cam_lrf_align_pointer,
//                                     cam,
//                                     led_router,
//                                     enable,
//                                     cam_cfg,
//                                     cam_tcp_port,
//                                     telemetry_enable,
//                                     loop_count,
//                                     "owl_cam_lrf_align_pointer");
//     if (rc != OWL_OK) {
//         MAIN_LOG("loop=%lu lrf align pointer failed id=%u value=%u rc=%d",
//                  loop_count, (unsigned)id, (unsigned)value, rc);
//         return -2;
//     }

//     MAIN_LOG("loop=%lu lrf align pointer ACK ok id=%u value=%u",
//              loop_count, (unsigned)id, (unsigned)value);
//     MAIN_LOG("LRF pointer %s", enable ? "ON" : "OFF");

//     rc = uart_send_control(uart, lrf_led_id, value);
//     if (rc != 0) {
//         MAIN_LOG("loop=%lu lrf align pointer led uart send failed led_id=%u value=%u rc=%d",
//                  loop_count, (unsigned)lrf_led_id, (unsigned)value, rc);
//         return -3;
//     }

//     MAIN_LOG("loop=%lu lrf align pointer led sent led_id=%u value=%u",
//              loop_count, (unsigned)lrf_led_id, (unsigned)value);
//     return 1;
// }

// static int handle_pip_command(owl_cam_t *cam,
//                               led_status_router_t *led_router,
//                               const camera_params_t *cam_cfg,
//                               uint16_t cam_tcp_port,
//                               bool telemetry_enable,
//                               uint8_t id,
//                               uint8_t value,
//                               unsigned long loop_count)
// {
//     uint8_t led_id = 0u;
//     int rc;

//     if ((cam == NULL) || (led_router == NULL) || (led_router->uart == NULL) || (cam_cfg == NULL)) {
//         return -1;
//     }

//     if (id != 83u) {
//         return 0;
//     }

//     if ((value != 0u) && (value != 1u)) {
//         MAIN_LOG("loop=%lu pip unsupported value id=%u value=%u",
//                  loop_count, (unsigned)id, (unsigned)value);
//         return 1;
//     }

//     rc = call_cam_bool_with_recover(owl_cam_set_pip,
//                                     cam,
//                                     led_router,
//                                     (value == 1u),
//                                     cam_cfg,
//                                     cam_tcp_port,
//                                     telemetry_enable,
//                                     loop_count,
//                                     "owl_cam_set_pip");
//     if (rc != OWL_OK) {
//         MAIN_LOG("loop=%lu pip set failed id=%u value=%u rc=%d",
//                  loop_count, (unsigned)id, (unsigned)value, rc);
//         return -2;
//     }

//     rc = config_get_button_led_id(led_router->cfg, "GBUTTON2_LED", &led_id);
//     if (rc != 0) {
//         MAIN_LOG("loop=%lu pip led mapping failed rc=%d", loop_count, rc);
//         return -3;
//     }

//     rc = uart_send_control(led_router->uart, led_id, value);
//     if (rc != 0) {
//         MAIN_LOG("loop=%lu pip led uart send failed led_id=%u value=%u rc=%d",
//                  loop_count, (unsigned)led_id, (unsigned)value, rc);
//         return -4;
//     }

//     MAIN_LOG("loop=%lu pip set ok id=%u value=%u led_id=%u",
//              loop_count, (unsigned)id, (unsigned)value, (unsigned)led_id);
//     return 1;
// }

// int main(void)
// {
//     Config uart_cfg;
//     camera_params_t cam_cfg;
//     uint16_t cam_tcp_port = OWL_TCP_PORT;
//     bool telemetry_enable = false;
//     bool alive = false;
//     uart_t uart;
//     owl_cam_t cam;
//     owl_telem_t telem_ctx;
//     owl_telem_frame_t telem;
//     int telem_open = 0;
//     unsigned long uart_idle_count = 0UL;
//     uint64_t lrf_reset_clear_deadline = 0u;
//     uint64_t optics_reset_clear_deadline = 0u;
//     uint64_t last_telem_tick = 0u;
//     uint64_t last_telem_enable_tick = 0u;
//     uint32_t udp_mcast_seq = 1u;
//     int api2_open = 0;
//     int button_led_rx_open = 0;
//     int joystick_rx_open = 0;
//     led_status_router_t led_router;
//     pthread_mutex_t cam_cmd_lock = PTHREAD_MUTEX_INITIALIZER;
//     uint8_t rx_id = 0u;
//     uint8_t rx_value = 0u;
//     const int run_camera_diag_on_start = 0;
//     int rc;

//     memset(&uart_cfg, 0, sizeof(uart_cfg));
//     memset(&cam_cfg, 0, sizeof(cam_cfg));
//     memset(&uart, 0, sizeof(uart));
//     memset(&cam, 0, sizeof(cam));
//     memset(&telem_ctx, 0, sizeof(telem_ctx));
//     memset(&telem, 0, sizeof(telem));
//     (void)platform_install_signal_handlers(&g_running);

//     rc = config_load("UART API/config.ini", &uart_cfg);
//     if (rc != 0) {
//         MAIN_LOG("error: config_load(UART API/config.ini) rc=%d", rc);
//         return 1;
//     }

//     rc = load_camera_settings("OWL_MD860/config.ini", &cam_cfg, &cam_tcp_port, &telemetry_enable);
//     if (rc != 0) {
//         MAIN_LOG0("error: failed to load camera settings from OWL_MD860/config.ini");
//         return 1;
//     }

//     if (!camera_mqtt_connect(&cam_cfg)) {
//         MAIN_LOG0("warning: MQTT connect failed");
//     } else {
//         camera_mqtt_publish_all(&cam_cfg);
//     }

//     if (cam_cfg.udp_mcast_enabled) {
//         api2_cfg_t api2_cfg;

//         if (load_api2_settings("OWL_MD860/config.ini", &api2_cfg) != 0) {
//             MAIN_LOG0("warning: failed to load API2 multicast settings");
//             api2_cfg_set_defaults(&api2_cfg);
//         }

//         rc = api2_init(&api2_cfg);
//         if (rc == 0) {
//             api2_open = 1;
//             camera_iface_set_udp_mcast(true, api2_cfg.camera_id, &udp_mcast_seq);
//             if (button_mcast_init(&uart_cfg, 250u) == 0) {
//                 if (button_mcast_start() != 0) {
//                     MAIN_LOG0("warning: button multicast thread start failed");
//                     button_mcast_shutdown();
//                 }
//             } else {
//                 MAIN_LOG0("warning: button multicast state init failed");
//             }
//         } else {
//             MAIN_LOG("warning: api2_init failed rc=%d", rc);
//         }
//     }

//     rc = uart_open(&uart, uart_cfg.uart_device, uart_cfg.uart_baud, uart_cfg.uart_read_timeout_ms);
//     if (rc != 0) {
//         MAIN_LOG("error: uart_open device=%s baud=%lu timeout=%lu rc=%d",
//                  uart_cfg.uart_device,
//                  (unsigned long)uart_cfg.uart_baud,
//                  (unsigned long)uart_cfg.uart_read_timeout_ms,
//                  rc);
//         return 1;
//     }
//     MAIN_LOG("UART open ok device=%s baud=%lu",
//              uart_cfg.uart_device,
//              (unsigned long)uart_cfg.uart_baud);

//     MAIN_LOG("opening camera connection ip=%s port=%u...", cam_cfg.camera_ip, (unsigned)cam_tcp_port);
//     rc = owl_cam_open(&cam, cam_cfg.camera_ip, cam_tcp_port);
//     if (rc != 0) {
//         MAIN_LOG("error: owl_cam_open ip=%s port=%u rc=%d",
//                  cam_cfg.camera_ip, (unsigned)cam_tcp_port, rc);
//         uart_close(&uart);
//         return 1;
//     }

//     rc = owl_cam_liveliness(&cam, &alive);
//     if ((rc != 0) || (!alive)) {
//         MAIN_LOG("error: camera liveliness check failed rc=%d alive=%d", rc, alive ? 1 : 0);
//         owl_cam_close(&cam);
//         uart_close(&uart);
//         return 1;
//     }

//     rc = owl_cam_set_telemetry(&cam, telemetry_enable);
//     if ((rc == OWL_OK) && telemetry_enable) {
//         last_telem_enable_tick = platform_monotonic_ms();
//     }
//     (void)rc;

//     g_selected_view_cam = OWL_TRACK_CAM_THERMAL;
//     get_tracker_center_for_cam(g_selected_view_cam,
//                                &g_last_tracker_state.coord_x,
//                                &g_last_tracker_state.coord_y);
//     rc = main_tracker_set_coord(&cam,
//                                 OWL_IFACE_TRACKER,
//                                 g_selected_view_cam,
//                                 g_last_tracker_state.coord_x,
//                                 g_last_tracker_state.coord_y,
//                                 OWL_TRACKER_START);
//     log_camera_response("startup tracker", rc);
//     if (rc != OWL_OK) {
//         MAIN_LOG("error: startup tracker failed cam=%u x=%u y=%u rc=%d",
//                  (unsigned)g_selected_view_cam,
//                  (unsigned)g_last_tracker_state.coord_x,
//                  (unsigned)g_last_tracker_state.coord_y,
//                  rc);
//         owl_cam_close(&cam);
//         uart_close(&uart);
//         return 1;
//     }
//     g_tracker_mode_on = 1;
//     g_last_tracker_state.coord_cam_id = g_selected_view_cam;
//     g_last_tracker_state.coord_mode = OWL_TRACKER_START;
//     g_last_tracker_valid = 1;
//     MAIN_LOG("startup tracker ok cam=%u x=%u y=%u",
//              (unsigned)g_last_tracker_state.coord_cam_id,
//              (unsigned)g_last_tracker_state.coord_x,
//              (unsigned)g_last_tracker_state.coord_y);

//     if (run_camera_diag_on_start != 0) {
//         MAIN_LOG0("camera diagnostics startup request ignored; power-control diagnostics are disabled");
//     }

//     led_status_router_init(&led_router, &uart_cfg, &uart);
//     if (telemetry_enable) {
//         rc = camera_telem_open(&telem_ctx, "0.0.0.0", 8005u);
//         if (rc == 0) {
//             telem_open = 1;
//             last_telem_tick = platform_monotonic_ms();
//         } else {
//             force_sw_lrf_led_off(&led_router, g_loop_count, "telemetry_open_failed");
//         }
//     }

//     if (api2_open != 0) {
//         rc = button_led_rx_start(&cam, &cam_cmd_lock, &g_selected_view_cam);
//         if (rc == 0) {
//             button_led_rx_open = 1;
//         } else {
//             MAIN_LOG("warning: button_led_rx_start failed rc=%d", rc);
//         }

//         rc = joystick_rx_start(&cam, &cam_cmd_lock, &g_selected_view_cam);
//         if (rc == 0) {
//             joystick_rx_open = 1;
//         } else {
//             MAIN_LOG("warning: joystick_rx_start failed rc=%d", rc);
//         }
//     }

//     while (g_running != 0) {
//         g_loop_count++;
//         rc = uart_read_and_parse(&uart, &rx_id, &rx_value);
//         printf("%d",rc);
//         printf("loop= %lu uart read id=%u value=%u rc=%d\n", g_loop_count, (unsigned)rx_id, (unsigned)rx_value, rc);

//         if (rc == 1) {
//             int lrf_single_measure_rc;
//             int lrf_freq_rc;
//             int lrf_reset_rc;
//             int optics_reset_rc;
//             int lrf_align_rc;
//             int pip_rc;
//             int switch_rc;

//             uart_idle_count = 0UL;
//             button_mcast_update(rx_id, rx_value);
//             MAIN_LOG("loop=%lu frame decoded id=%u value=%u",
//                      g_loop_count, (unsigned)rx_id, (unsigned)rx_value);

//             /* DROP button (id 14) tracker on/off handling is disabled.
//             drop_tracker_rc = handle_drop_tracker_command(&cam,
//                                                           &led_router,
//                                                           &cam_cfg,
//                                                           cam_tcp_port,
//                                                           telemetry_enable,
//                                                           rx_id,
//                                                           rx_value,
//                                                           g_loop_count);
//             if (drop_tracker_rc < 0) {
//                 MAIN_LOG("loop=%lu drop tracker handler error id=%u value=%u rc=%d",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value, drop_tracker_rc);
//             } else if (drop_tracker_rc == 1) {
//                 MAIN_LOG("loop=%lu drop tracker handler completed id=%u value=%u tracker_mode_on=%d",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value, g_tracker_mode_on);
//             }

//             if (drop_tracker_rc == 1) {
//                 continue;
//             }
//             */

//             (void)pthread_mutex_lock(&cam_cmd_lock);
//             lrf_single_measure_rc = handle_lrf_single_measure_mode_command(&cam, &led_router, &cam_cfg, cam_tcp_port, telemetry_enable, rx_id, rx_value, g_loop_count);
//             (void)pthread_mutex_unlock(&cam_cmd_lock);
//             if (lrf_single_measure_rc < 0) {
//                 MAIN_LOG("loop=%lu lrf single measure handler error id=%u value=%u rc=%d",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value, lrf_single_measure_rc);
//             } else if (lrf_single_measure_rc == 1) {
//                 MAIN_LOG("loop=%lu lrf single measure handler completed id=%u value=%u",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value);
//             }
//             if (lrf_single_measure_rc == 1) {
//                 continue;
//             }

//             (void)pthread_mutex_lock(&cam_cmd_lock);
//             lrf_freq_rc = handle_lrf_frequency_command(&cam, &led_router, &cam_cfg, cam_tcp_port, telemetry_enable, rx_id, rx_value, g_loop_count);
//             (void)pthread_mutex_unlock(&cam_cmd_lock);
//             if (lrf_freq_rc < 0) {
//                 MAIN_LOG("loop=%lu lrf frequency handler error id=%u value=%u rc=%d",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value, lrf_freq_rc);
//             } else if (lrf_freq_rc == 1) {
//                 MAIN_LOG("loop=%lu lrf frequency handler completed id=%u value=%u",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value);
//             }
//             if (lrf_freq_rc == 1) {
//                 continue;
//             }

//             (void)pthread_mutex_lock(&cam_cmd_lock);
//             lrf_reset_rc = handle_lrf_reset_command(&cam, &led_router, &cam_cfg, cam_tcp_port, telemetry_enable, rx_id, rx_value, g_loop_count);
//             (void)pthread_mutex_unlock(&cam_cmd_lock);
//             if (lrf_reset_rc < 0) {
//                 MAIN_LOG("loop=%lu lrf reset handler error id=%u value=%u rc=%d",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value, lrf_reset_rc);
//             } else if (lrf_reset_rc == 1) {
//                 MAIN_LOG("loop=%lu lrf reset handler completed id=%u value=%u",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value);
//             }
//             if (lrf_reset_rc == 1) {
//                 continue;
//             }

//             (void)pthread_mutex_lock(&cam_cmd_lock);
//             optics_reset_rc = handle_optics_reset_command(&cam, &led_router, &cam_cfg, cam_tcp_port, telemetry_enable, rx_id, rx_value, g_loop_count);
//             (void)pthread_mutex_unlock(&cam_cmd_lock);
//             if (optics_reset_rc < 0) {
//                 MAIN_LOG("loop=%lu optics reset handler error id=%u value=%u rc=%d",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value, optics_reset_rc);
//             } else if (optics_reset_rc == 1) {
//                 MAIN_LOG("loop=%lu optics reset handler completed id=%u value=%u",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value);
//             }
//             if (optics_reset_rc == 1) {
//                 continue;
//             }

//             (void)pthread_mutex_lock(&cam_cmd_lock);
//             lrf_align_rc = handle_lrf_align_pointer_command(&cam, &led_router, &uart, &cam_cfg, cam_tcp_port, telemetry_enable, rx_id, rx_value, g_loop_count);
//             (void)pthread_mutex_unlock(&cam_cmd_lock);
//             if (lrf_align_rc < 0) {
//                 MAIN_LOG("loop=%lu lrf align pointer handler error id=%u value=%u rc=%d",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value, lrf_align_rc);
//             } else if (lrf_align_rc == 1) {
//                 MAIN_LOG("loop=%lu lrf align pointer handler completed id=%u value=%u",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value);
//             }
//             if (lrf_align_rc == 1) {
//                 continue;
//             }

//             (void)pthread_mutex_lock(&cam_cmd_lock);
//             pip_rc = handle_pip_command(&cam,
//                                         &led_router,
//                                         &cam_cfg,
//                                         cam_tcp_port,
//                                         telemetry_enable,
//                                         rx_id,
//                                         rx_value,
//                                         g_loop_count);
//             (void)pthread_mutex_unlock(&cam_cmd_lock);
//             if (pip_rc < 0) {
//                 MAIN_LOG("loop=%lu pip handler error id=%u value=%u rc=%d",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value, pip_rc);
//             } else if (pip_rc == 1) {
//                 MAIN_LOG("loop=%lu pip handler completed id=%u value=%u",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value);
//             }
//             if (pip_rc == 1) {
//                 continue;
//             }

//             (void)pthread_mutex_lock(&cam_cmd_lock);
//             switch_rc = handle_camera_view_switch_command(&cam, &led_router, &cam_cfg, cam_tcp_port, telemetry_enable, rx_id, rx_value, g_loop_count);
//             (void)pthread_mutex_unlock(&cam_cmd_lock);
//             if (switch_rc < 0) {
//                 MAIN_LOG("loop=%lu camera view switch handler error id=%u value=%u rc=%d",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value, switch_rc);
//             } else if (switch_rc == 1) {
//                 MAIN_LOG("loop=%lu camera view switch handler completed id=%u value=%u tracker_mode_on=%d",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value, g_tracker_mode_on);
//             } else {
//                 MAIN_LOG("loop=%lu no direct camera view mapping for id=%u value=%u",
//                          g_loop_count, (unsigned)rx_id, (unsigned)rx_value);
//             }
//         } else if (rc == -1) {
//             MAIN_LOG("loop=%lu UART parse error=%d", g_loop_count, uart_get_last_parse_error());
//         } else if (rc == 0) {
//             uart_idle_count++;
//             MAIN_LOG("loop=%lu UART idle count=%lu", g_loop_count, uart_idle_count);
//         } else if (rc < 0) {
//             MAIN_LOG("loop=%lu UART read error rc=%d", g_loop_count, rc);
//             platform_sleep_ms(10u);
//         }
//         else {
//             MAIN_LOG("loop=%lu UART read unknown return code rc=%d", g_loop_count, rc);
//             platform_sleep_ms(10u);
//         }
//         // Sleep briefly to avoid tight loop if needed
//         printf("telem_open= %d",telem_open);
//         if (telem_open != 0) {
//             const uint64_t now = platform_monotonic_ms();
//             if (telemetry_enable && ((last_telem_enable_tick == 0u) ||
//                                      ((now - last_telem_enable_tick) >= 1000u))) {
//                 int set_telem_rc;
//                 (void)pthread_mutex_lock(&cam_cmd_lock);
//                 set_telem_rc = owl_cam_set_telemetry(&cam, true);
//                 (void)pthread_mutex_unlock(&cam_cmd_lock);
//                 if (set_telem_rc != OWL_OK) {
//                     MAIN_LOG("loop=%lu owl_cam_set_telemetry(true) failed rc=%d", g_loop_count, set_telem_rc);
//                 } else {
//                     last_telem_enable_tick = now;
//                 }
//             }
//             const int trc = camera_telem_recv(&telem_ctx, &telem, 500);
//             printf("trc= %d",trc);
//             if (trc > 0) {
//                 last_telem_tick = platform_monotonic_ms();
//                 camera_telem_publish(&cam_cfg, &telem);
//                 const int led_telem_rc = led_status_router_update_from_telem(&led_router, &telem);
//                 if (led_telem_rc < 0) {
//                     MAIN_LOG("loop=%lu telemetry led update failed rc=%d", g_loop_count, led_telem_rc);
//                 }
//             } else {
//                 const uint64_t now = platform_monotonic_ms();
//                 if ((now - last_telem_tick) >= TELEMETRY_LOSS_LED_OFF_MS) {
//                     force_sw_lrf_led_off(&led_router, g_loop_count, "telemetry_timeout");
//                 }
//             }
//         }
        
//         if (lrf_reset_clear_deadline != 0u) {
//             const uint64_t now = platform_monotonic_ms();
//             if (now >= lrf_reset_clear_deadline) {
//                 (void)led_status_router_clear_reset_led(&led_router, "LRF_RESET");
//                 lrf_reset_clear_deadline = 0u;
//             }
//         }
//         if (optics_reset_clear_deadline != 0u) {
//             const uint64_t now = platform_monotonic_ms();
//             if (now >= optics_reset_clear_deadline) {
//                 (void)led_status_router_clear_reset_led(&led_router, "OPTICS_RESET");
//                 optics_reset_clear_deadline = 0u;
//             }
//         }
            
//     } //end of while loop

//     if (telem_open != 0) {
//         camera_telem_close(&telem_ctx);
//     }
//     button_mcast_shutdown();
//     if (joystick_rx_open != 0) {
//         joystick_rx_stop();
//     }
//     if (button_led_rx_open != 0) {
//         button_led_rx_stop();
//     }

//     if (api2_open != 0) {
//         api2_shutdown();
//     }
//     mqtt_close();

//     uart_close(&uart);

//     if ((g_tracker_mode_on != 0) && (g_last_tracker_valid != 0)) {
//         int trk_rc;
//         (void)pthread_mutex_lock(&cam_cmd_lock);
//         trk_rc = main_tracker_set_coord(&cam,
//                                         OWL_IFACE_TRACKER,
//                                         g_last_tracker_state.coord_cam_id,
//                                         g_last_tracker_state.coord_x,
//                                         g_last_tracker_state.coord_y,
//                                         OWL_TRACKER_STOP);
//         (void)pthread_mutex_unlock(&cam_cmd_lock);
//         log_camera_response("shutdown tracker", trk_rc);
//         MAIN_LOG("shutdown tracker mode off cam=%u x=%u y=%u",
//                  (unsigned)g_last_tracker_state.coord_cam_id,
//                  (unsigned)g_last_tracker_state.coord_x,
//                  (unsigned)g_last_tracker_state.coord_y);
//     }

//     owl_cam_close(&cam);
//     return 0;
// }
