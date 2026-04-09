#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../platform_compat.h"
#include "api2_mcast_win.h"
#include "button_led_rx.h"
#include "camera_iface.h"
#include "camera_mqtt.h"
#include "camera_params.h"
#include "ini_parser.h"
#include "joystick_rx.h"
#include "mqtt.h"

static volatile sig_atomic_t g_owl_main_running = 1;

static int owl_main_parse_u16_string(const char *s, uint16_t *out)
{
    char *endp = NULL;
    unsigned long v;

    if ((s == NULL) || (out == NULL)) {
        return -1;
    }

    v = strtoul(s, &endp, 10);
    if ((endp == s) || (*endp != '\0') || (v > 65535UL)) {
        return -1;
    }

    *out = (uint16_t)v;
    return 0;
}

static int owl_main_parse_bool_string(const char *s, bool *out)
{
    if ((s == NULL) || (out == NULL)) {
        return -1;
    }

    if ((strcmp(s, "1") == 0) ||
        (platform_strcasecmp(s, "true") == 0) ||
        (platform_strcasecmp(s, "yes") == 0) ||
        (platform_strcasecmp(s, "on") == 0)) {
        *out = true;
        return 0;
    }
    if ((strcmp(s, "0") == 0) ||
        (platform_strcasecmp(s, "false") == 0) ||
        (platform_strcasecmp(s, "no") == 0) ||
        (platform_strcasecmp(s, "off") == 0)) {
        *out = false;
        return 0;
    }

    return -1;
}

static uint8_t owl_main_load_button_led_id(const char *path, const char *name, uint8_t default_id)
{
    char value[64];
    uint16_t parsed = 0u;

    if ((path == NULL) || (name == NULL)) {
        return default_id;
    }

    if (ini_get_value(path, "BUTTON_LED_IDS", name, value, sizeof(value)) != 0) {
        return default_id;
    }
    if (owl_main_parse_u16_string(value, &parsed) != 0) {
        return default_id;
    }

    return (uint8_t)parsed;
}

static void owl_main_publish_button_led(uint32_t *seq_counter,
                                        uint8_t led_id,
                                        const char *name,
                                        uint8_t value)
{
    if ((seq_counter == NULL) || (name == NULL)) {
        return;
    }

    (void)api2_send_button_led_state_json((*seq_counter)++,
                                          led_id,
                                          name,
                                          value,
                                          "tx",
                                          "camera_telem");
}

static void owl_main_force_sw_lrf_status_led(uint32_t *seq_counter,
                                             uint8_t led_id,
                                             int *last_value,
                                             uint8_t value)
{
    const int desired_value = (value != 0u) ? 1 : 0;

    if ((seq_counter == NULL) || (last_value == NULL)) {
        return;
    }
    if (*last_value == desired_value) {
        return;
    }

    owl_main_publish_button_led(seq_counter, led_id, "SW_LRF_Status_LED", (uint8_t)desired_value);
    *last_value = desired_value;
}

static int owl_main_publish_camera_telem_full(uint32_t *seq_counter,
                                              uint8_t camera_id,
                                              const owl_telem_frame_t *fr)
{
    api2_cam1_status_t st;

    if ((seq_counter == NULL) || (fr == NULL)) {
        return -1;
    }

    (void)memset(&st, 0, sizeof(st));

    /* No direct "mode" field exists in owl_telem_frame_t, so leave it at 0. */
    st.mode = 0u;
    st.tracker_mode = fr->tracker_mode;
    st.thermal_nuc_status = fr->thermal_nuc_status;
    st.lrf_range = fr->lrf_range;
    st.day_ret_x = fr->day_ret_x;
    st.day_ret_y = fr->day_ret_y;
    st.thermal_ret_x = fr->thermal_ret_x;
    st.thermal_ret_y = fr->thermal_ret_y;
    st.tracker_ret_x = fr->tracker_ret_x;
    st.tracker_ret_y = fr->tracker_ret_y;
    st.tracker_bb_w = fr->tracker_bb_w;
    st.tracker_bb_h = fr->tracker_bb_h;
    st.sys_temp = fr->sys_temp;
    st.alt_m = fr->alt;
    st.sat = fr->sat;
    st.gps_qos = fr->gpsQOS;
    st.pwr_day_on = fr->pwr_day_on;
    st.pwr_thermal_on = fr->pwr_thermal_on;
    st.pwr_plcb_on = fr->pwr_plcb_on;
    st.pwr_lrf_on = fr->pwr_lrf_on;
    st.pwr_overall = fr->pwr_overall;

    return api2_send_cam1((*seq_counter)++, camera_id, &st);
}

static int owl_main_publish_tracker_telem(uint32_t *seq_counter,
                                          uint8_t camera_id,
                                          const owl_telem_frame_t *fr)
{
    api2_tracker_telem_t st;

    if ((seq_counter == NULL) || (fr == NULL)) {
        return -1;
    }

    (void)memset(&st, 0, sizeof(st));

    st.version = fr->version;
    (void)memcpy(st.pt_reserved, fr->pt_reserved, sizeof(st.pt_reserved));
    st.lrf_range = fr->lrf_range;
    (void)memcpy(st.lrf_reserved, fr->lrf_reserved, sizeof(st.lrf_reserved));
    st.thermal_ret_x = fr->thermal_ret_x;
    st.thermal_ret_y = fr->thermal_ret_y;
    st.thermal_fov_code = fr->thermal_fov_code;
    st.thermal_fov_deg = fr->thermal_fov_deg;
    st.thermal_nuc_status = fr->thermal_nuc_status;
    (void)memcpy(st.thermal_reserved, fr->thermal_reserved, sizeof(st.thermal_reserved));
    st.day_ret_x = fr->day_ret_x;
    st.day_ret_y = fr->day_ret_y;
    st.day_fov_code = fr->day_fov_code;
    st.day_fov_deg = fr->day_fov_deg;
    (void)memcpy(st.day_reserved, fr->day_reserved, sizeof(st.day_reserved));
    st.day2_ret_x = fr->day2_ret_x;
    st.day2_ret_y = fr->day2_ret_y;
    st.day2_fov_code = fr->day2_fov_code;
    st.day2_fov_deg = fr->day2_fov_deg;
    (void)memcpy(st.day2_reserved, fr->day2_reserved, sizeof(st.day2_reserved));
    st.tracker_enable_mode = fr->tracker_mode;
    st.tracker_ret_x = fr->tracker_ret_x;
    st.tracker_ret_y = fr->tracker_ret_y;
    st.tracker_bb_w = fr->tracker_bb_w;
    st.tracker_bb_h = fr->tracker_bb_h;
    st.tracker_pan_err = fr->tracker_pan_err;
    st.tracker_tilt_err = fr->tracker_tilt_err;
    st.track_cam_id = fr->tracker_cam_id;
    st.track_mode = fr->tracker_track_mode;
    st.track_type = fr->tracker_track_type;
    st.detection_mode = fr->tracker_detection_mode;
    (void)memcpy(st.tracker_reserved, fr->tracker_reserved, sizeof(st.tracker_reserved));
    (void)memcpy(st.factory_reserved, fr->factory_reserved, sizeof(st.factory_reserved));
    st.sys_temp = fr->sys_temp;
    st.lat = fr->lat;
    st.ns_dir = fr->NSdir;
    st.lon = fr->lon;
    st.ew_dir = fr->EWdir;
    st.alt_m = fr->alt;
    st.sat = fr->sat;
    st.gps_qos = fr->gpsQOS;
    (void)memcpy(st.gps_reserved, fr->gps_reserved, sizeof(st.gps_reserved));
    st.pwr_day_on = fr->pwr_day_on;
    st.pwr_thermal_on = fr->pwr_thermal_on;
    st.pwr_plcb_on = fr->pwr_plcb_on;
    st.pwr_lrf_on = fr->pwr_lrf_on;
    st.pwr_overall = fr->pwr_overall;

    return api2_send_tracker_telem((*seq_counter)++, camera_id, &st);
}

static void owl_main_publish_shutdown_leds_off(uint32_t *seq_counter, const char *config_path)
{
    const uint8_t lrf_reset_led_id =
        owl_main_load_button_led_id(config_path, "LRF_RESET_LED", 62u);
    const uint8_t optics_reset_led_id =
        owl_main_load_button_led_id(config_path, "OPTICS_RESET_LED", 61u);
    const uint8_t lrf_led_id =
        owl_main_load_button_led_id(config_path, "LRF_LED", 76u);
    const uint8_t sw_lrf_status_led_id =
        owl_main_load_button_led_id(config_path, "SW_LRF_Status_LED", 81u);

    owl_main_publish_button_led(seq_counter, lrf_reset_led_id, "LRF_RESET_LED", 0u);
    owl_main_publish_button_led(seq_counter, optics_reset_led_id, "OPTICS_RESET_LED", 0u);
    owl_main_publish_button_led(seq_counter, lrf_led_id, "LRF_LED", 0u);
    owl_main_publish_button_led(seq_counter, sw_lrf_status_led_id, "SW_LRF_Status_LED", 0u);
}

static int owl_main_load_camera_settings(const char *path,
                                         camera_params_t *params,
                                         uint16_t *tcp_port_out,
                                         bool *telemetry_enable_out)
{
    char value[64];

    if ((path == NULL) || (params == NULL) || (tcp_port_out == NULL) || (telemetry_enable_out == NULL)) {
        return -1;
    }

    (void)memset(params, 0, sizeof(*params));
    *tcp_port_out = OWL_TCP_PORT;
    *telemetry_enable_out = false;

    if (ini_get_value(path, "camera", "name", value, sizeof(value)) == 0) {
        (void)strncpy(params->name, value, sizeof(params->name) - 1u);
        params->name[sizeof(params->name) - 1u] = '\0';
    }
    if (ini_get_value(path, "camera", "ip", value, sizeof(value)) == 0) {
        (void)strncpy(params->camera_ip, value, sizeof(params->camera_ip) - 1u);
        params->camera_ip[sizeof(params->camera_ip) - 1u] = '\0';
    }
    if (ini_get_value(path, "camera", "tcp_port", value, sizeof(value)) == 0) {
        (void)owl_main_parse_u16_string(value, tcp_port_out);
    }
    if (ini_get_value(path, "mqtt", "broker", value, sizeof(value)) == 0) {
        (void)strncpy(params->mqtt_broker, value, sizeof(params->mqtt_broker) - 1u);
        params->mqtt_broker[sizeof(params->mqtt_broker) - 1u] = '\0';
    }
    if (ini_get_value(path, "mqtt", "port", value, sizeof(value)) == 0) {
        (void)owl_main_parse_u16_string(value, &params->mqtt_port);
    }
    if (ini_get_value(path, "mqtt", "client_id", value, sizeof(value)) == 0) {
        (void)strncpy(params->mqtt_client_id, value, sizeof(params->mqtt_client_id) - 1u);
        params->mqtt_client_id[sizeof(params->mqtt_client_id) - 1u] = '\0';
    }
    if (ini_get_value(path, "mqtt", "root_topic", value, sizeof(value)) == 0) {
        (void)strncpy(params->root_topic, value, sizeof(params->root_topic) - 1u);
        params->root_topic[sizeof(params->root_topic) - 1u] = '\0';
    }
    if (ini_get_value(path, "mqtt", "retained", value, sizeof(value)) == 0) {
        (void)owl_main_parse_bool_string(value, &params->mqtt_retained);
    }
    if ((ini_get_value(path, "mcast_camera", "enabled", value, sizeof(value)) == 0) ||
        (ini_get_value(path, "udp_multicast", "enabled", value, sizeof(value)) == 0)) {
        (void)owl_main_parse_bool_string(value, &params->udp_mcast_enabled);
    }
    if ((ini_get_value(path, "mcast_camera", "group_ip", value, sizeof(value)) == 0) ||
        (ini_get_value(path, "udp_multicast", "group_ip", value, sizeof(value)) == 0)) {
        (void)strncpy(params->udp_mcast_group_ip, value, sizeof(params->udp_mcast_group_ip) - 1u);
        params->udp_mcast_group_ip[sizeof(params->udp_mcast_group_ip) - 1u] = '\0';
    }
    if ((ini_get_value(path, "mcast_camera", "port", value, sizeof(value)) == 0) ||
        (ini_get_value(path, "udp_multicast", "port", value, sizeof(value)) == 0)) {
        (void)owl_main_parse_u16_string(value, &params->udp_mcast_port);
    }
    if ((ini_get_value(path, "mcast_common", "iface_ip", value, sizeof(value)) == 0) ||
        (ini_get_value(path, "udp_multicast", "iface_ip", value, sizeof(value)) == 0)) {
        (void)strncpy(params->udp_mcast_iface_ip, value, sizeof(params->udp_mcast_iface_ip) - 1u);
        params->udp_mcast_iface_ip[sizeof(params->udp_mcast_iface_ip) - 1u] = '\0';
    }
    if ((ini_get_value(path, "mcast_common", "ttl", value, sizeof(value)) == 0) ||
        (ini_get_value(path, "udp_multicast", "ttl", value, sizeof(value)) == 0)) {
        uint16_t parsed = 0u;
        if (owl_main_parse_u16_string(value, &parsed) == 0) {
            params->udp_mcast_ttl = (uint8_t)parsed;
        }
    }
    if ((ini_get_value(path, "mcast_common", "loopback", value, sizeof(value)) == 0) ||
        (ini_get_value(path, "udp_multicast", "loopback", value, sizeof(value)) == 0)) {
        bool loopback = false;
        if (owl_main_parse_bool_string(value, &loopback) == 0) {
            params->udp_mcast_loopback = loopback ? 1u : 0u;
        }
    }
    if ((ini_get_value(path, "mcast_common", "camera_id", value, sizeof(value)) == 0) ||
        (ini_get_value(path, "udp_multicast", "camera_id", value, sizeof(value)) == 0)) {
        uint16_t parsed = 0u;
        if (owl_main_parse_u16_string(value, &parsed) == 0) {
            params->udp_camera_id = (uint8_t)parsed;
        }
    }
    if (ini_get_value(path, "camera", "telemetry_enabled", value, sizeof(value)) == 0) {
        (void)owl_main_parse_bool_string(value, telemetry_enable_out);
    } else if ((ini_get_value(path, "mcast_camera", "enabled", value, sizeof(value)) == 0) ||
               (ini_get_value(path, "udp_multicast", "enabled", value, sizeof(value)) == 0)) {
        (void)owl_main_parse_bool_string(value, telemetry_enable_out);
    }

    return camera_params_validate(params) ? 0 : -1;
}

static int owl_main_load_api2_settings(const char *path, api2_cfg_t *cfg)
{
    char value[64];

    if ((path == NULL) || (cfg == NULL)) {
        return -1;
    }

    api2_cfg_set_defaults(cfg);

    if (ini_get_value(path, "mcast_common", "iface_ip", value, sizeof(value)) == 0) {
        (void)strncpy(cfg->iface_ip, value, sizeof(cfg->iface_ip) - 1u);
        cfg->iface_ip[sizeof(cfg->iface_ip) - 1u] = '\0';
    }
    if (ini_get_value(path, "mcast_common", "ttl", value, sizeof(value)) == 0) {
        uint16_t parsed = 0u;
        if (owl_main_parse_u16_string(value, &parsed) == 0) {
            cfg->ttl = (uint8_t)parsed;
        }
    }
    if (ini_get_value(path, "mcast_common", "loopback", value, sizeof(value)) == 0) {
        bool loopback = false;
        if (owl_main_parse_bool_string(value, &loopback) == 0) {
            cfg->loopback = loopback ? 1u : 0u;
        }
    }
    if (ini_get_value(path, "mcast_common", "camera_id", value, sizeof(value)) == 0) {
        uint16_t parsed = 0u;
        if (owl_main_parse_u16_string(value, &parsed) == 0) {
            cfg->camera_id = (uint8_t)parsed;
        }
    }

    if (ini_get_value(path, "mcast_camera", "enabled", value, sizeof(value)) == 0) {
        bool enabled = false;
        if (owl_main_parse_bool_string(value, &enabled) == 0) {
            cfg->camera.enabled = enabled ? 1u : 0u;
        }
    }
    if (ini_get_value(path, "mcast_camera", "group_ip", value, sizeof(value)) == 0) {
        (void)strncpy(cfg->camera.group_ip, value, sizeof(cfg->camera.group_ip) - 1u);
        cfg->camera.group_ip[sizeof(cfg->camera.group_ip) - 1u] = '\0';
    }
    if (ini_get_value(path, "mcast_camera", "port", value, sizeof(value)) == 0) {
        (void)owl_main_parse_u16_string(value, &cfg->camera.port);
    }

    if (ini_get_value(path, "mcast_joystick", "enabled", value, sizeof(value)) == 0) {
        bool enabled = false;
        if (owl_main_parse_bool_string(value, &enabled) == 0) {
            cfg->joystick.enabled = enabled ? 1u : 0u;
        }
    }
    if (ini_get_value(path, "mcast_joystick", "group_ip", value, sizeof(value)) == 0) {
        (void)strncpy(cfg->joystick.group_ip, value, sizeof(cfg->joystick.group_ip) - 1u);
        cfg->joystick.group_ip[sizeof(cfg->joystick.group_ip) - 1u] = '\0';
    }
    if (ini_get_value(path, "mcast_joystick", "port", value, sizeof(value)) == 0) {
        (void)owl_main_parse_u16_string(value, &cfg->joystick.port);
    }

    if (ini_get_value(path, "mcast_button_led", "enabled", value, sizeof(value)) == 0) {
        bool enabled = false;
        if (owl_main_parse_bool_string(value, &enabled) == 0) {
            cfg->button_led.enabled = enabled ? 1u : 0u;
        }
    }
    if (ini_get_value(path, "mcast_button_led", "group_ip", value, sizeof(value)) == 0) {
        (void)strncpy(cfg->button_led.group_ip, value, sizeof(cfg->button_led.group_ip) - 1u);
        cfg->button_led.group_ip[sizeof(cfg->button_led.group_ip) - 1u] = '\0';
    }
    if (ini_get_value(path, "mcast_button_led", "port", value, sizeof(value)) == 0) {
        (void)owl_main_parse_u16_string(value, &cfg->button_led.port);
    }

    if (ini_get_value(path, "mcast_tracker_telemetry", "enabled", value, sizeof(value)) == 0) {
        bool enabled = false;
        if (owl_main_parse_bool_string(value, &enabled) == 0) {
            cfg->tracker_telem.enabled = enabled ? 1u : 0u;
        }
    }
    if (ini_get_value(path, "mcast_tracker_telemetry", "group_ip", value, sizeof(value)) == 0) {
        (void)strncpy(cfg->tracker_telem.group_ip, value, sizeof(cfg->tracker_telem.group_ip) - 1u);
        cfg->tracker_telem.group_ip[sizeof(cfg->tracker_telem.group_ip) - 1u] = '\0';
    }
    if (ini_get_value(path, "mcast_tracker_telemetry", "port", value, sizeof(value)) == 0) {
        (void)owl_main_parse_u16_string(value, &cfg->tracker_telem.port);
    }

    return 0;
}

int main(void)
{
    camera_params_t cam_cfg;
    api2_cfg_t api2_cfg;
    owl_cam_t cam;
    owl_telem_t telem;
    pthread_mutex_t cam_cmd_lock = PTHREAD_MUTEX_INITIALIZER;
    uint16_t cam_tcp_port = OWL_TCP_PORT;
    bool alive = false;
    bool telemetry_enable = false;
    uint8_t selected_cam = OWL_TRACK_CAM_THERMAL;
    uint8_t sw_lrf_status_led_id = 81u;
    uint32_t udp_mcast_seq = 1u;
    int api2_open = 0;
    int button_led_open = 0;
    int joystick_open = 0;
    int telem_open = 0;
    int last_lrf_led_value = -1;
    unsigned int telem_rx_log_count = 0u;
    unsigned int telem_miss_count = 0u;
    int rc;

    (void)memset(&cam_cfg, 0, sizeof(cam_cfg));
    (void)memset(&api2_cfg, 0, sizeof(api2_cfg));
    (void)memset(&cam, 0, sizeof(cam));
    (void)memset(&telem, 0, sizeof(telem));
    (void)platform_install_signal_handlers(&g_owl_main_running);

    rc = owl_main_load_camera_settings("config.ini", &cam_cfg, &cam_tcp_port, &telemetry_enable);
    if (rc != 0) {
        fprintf(stderr, "[owl_main] failed to load camera settings from config.ini\n");
        return 1;
    }

    rc = owl_main_load_api2_settings("config.ini", &api2_cfg);
    if (rc != 0) {
        fprintf(stderr, "[owl_main] failed to load multicast settings from config.ini\n");
        return 1;
    }
    sw_lrf_status_led_id = owl_main_load_button_led_id("config.ini", "SW_LRF_Status_LED", 81u);

    fprintf(stdout,
            "[owl_main] joystick mcast enabled=%u group=%s port=%u iface=%s ttl=%u loopback=%u\n",
            (unsigned)api2_cfg.joystick.enabled,
            api2_cfg.joystick.group_ip,
            (unsigned)api2_cfg.joystick.port,
            api2_cfg.iface_ip,
            (unsigned)api2_cfg.ttl,
            (unsigned)api2_cfg.loopback);

    rc = owl_cam_open(&cam, cam_cfg.camera_ip, cam_tcp_port);
    if (rc != OWL_OK) {
        fprintf(stderr, "[owl_main] owl_cam_open failed ip=%s port=%u rc=%d\n",
                cam_cfg.camera_ip, (unsigned)cam_tcp_port, rc);
        return 1;
    }

    rc = owl_cam_liveliness(&cam, &alive);
    if ((rc != OWL_OK) || (!alive)) {
        fprintf(stderr, "[owl_main] camera liveliness failed rc=%d alive=%d\n", rc, alive ? 1 : 0);
        owl_cam_close(&cam);
        return 1;
    }

    if (!camera_mqtt_connect(&cam_cfg)) {
        fprintf(stderr, "[owl_main] warning: MQTT connect failed\n");
    } else {
        camera_mqtt_publish_all(&cam_cfg);
    }

    rc = api2_init(&api2_cfg);
    if (rc != 0) {
        fprintf(stderr, "[owl_main] api2_init failed rc=%d\n", rc);
        owl_cam_close(&cam);
        return 1;
    }
    api2_open = 1;
    camera_iface_set_udp_mcast(cam_cfg.udp_mcast_enabled, api2_cfg.camera_id, &udp_mcast_seq);
    camera_mqtt_set_udp_mcast(cam_cfg.udp_mcast_enabled, api2_cfg.camera_id, &udp_mcast_seq);

    rc = owl_cam_set_telemetry(&cam, telemetry_enable);
    if (rc != OWL_OK) {
        fprintf(stderr, "[owl_main] owl_cam_set_telemetry failed rc=%d\n", rc);
    }
    if (telemetry_enable) {
        const char *bind_ip = (cam_cfg.udp_mcast_iface_ip[0] != '\0') ? cam_cfg.udp_mcast_iface_ip : NULL;

        rc = camera_telem_open(&telem, bind_ip, 0u);
        if (rc == OWL_OK) {
            telem_open = 1;
            fprintf(stdout, "[owl_main] camera telemetry receive enabled udp_port=%u bind_ip=%s\n",
                    (unsigned)telem.port,
                    (bind_ip != NULL) ? bind_ip : "0.0.0.0");
        } else {
            fprintf(stderr, "[owl_main] camera_telem_open failed bind_ip=%s rc=%d\n",
                    (bind_ip != NULL) ? bind_ip : "0.0.0.0", rc);
            owl_main_force_sw_lrf_status_led(&udp_mcast_seq,
                                             sw_lrf_status_led_id,
                                             &last_lrf_led_value,
                                             0u);
        }
    }

    rc = button_led_rx_start(&cam, &cam_cmd_lock, &selected_cam);
    if (rc == 0) {
        button_led_open = 1;
    } else {
        fprintf(stderr, "[owl_main] button_led_rx_start failed rc=%d\n", rc);
    }

    rc = joystick_rx_start(&cam, &cam_cmd_lock, &selected_cam);
    if (rc == 0) {
        joystick_open = 1;
    } else {
        fprintf(stderr, "[owl_main] joystick_rx_start failed rc=%d\n", rc);
    }

    while (g_owl_main_running != 0) {
        if (telem_open != 0) {
            owl_telem_frame_t fr;
            const int telem_rc = camera_telem_recv(&telem, &fr, 50);
            //printf("telemetry=%d",telem_rc);

            if (telem_rc > 0) {
                const int new_lrf_led_value = (fr.pwr_lrf_on != 0u) ? 1 : 0;
                const int cam_mcast_rc =
                    owl_main_publish_camera_telem_full(&udp_mcast_seq, api2_cfg.camera_id, &fr);
                const int tracker_mcast_rc =
                    owl_main_publish_tracker_telem(&udp_mcast_seq, api2_cfg.camera_id, &fr);

                telem_miss_count = 0u;
                camera_telem_publish(&cam_cfg, &fr);
                if ((telem_rx_log_count < 10u) || ((telem_rx_log_count % 20u) == 0u)) {
                    fprintf(stdout,
                            "[owl_main] telemetry rx ok lrf=%u tracker=(%u,%u) cam_mcast_rc=%d tracker_mcast_rc=%d\n",
                            (unsigned)fr.lrf_range,
                            (unsigned)fr.tracker_ret_x,
                            (unsigned)fr.tracker_ret_y,
                            cam_mcast_rc,
                            tracker_mcast_rc);
                }
                telem_rx_log_count++;
                owl_main_force_sw_lrf_status_led(&udp_mcast_seq,
                                                 sw_lrf_status_led_id,
                                                 &last_lrf_led_value,
                                                 (uint8_t)new_lrf_led_value);
                continue;
            }
            if (telem_rc == 0) {
                telem_miss_count++;
                if (telem_miss_count >= 20u) {
                    owl_main_force_sw_lrf_status_led(&udp_mcast_seq,
                                                     sw_lrf_status_led_id,
                                                     &last_lrf_led_value,
                                                     0u);
                }
            }
            if ((telem_rc < 0) && (telem_rc != -4) && (telem_rc != -5)) {
                fprintf(stderr, "[owl_main] camera_telem_recv failed rc=%d\n", telem_rc);
                owl_main_force_sw_lrf_status_led(&udp_mcast_seq,
                                                 sw_lrf_status_led_id,
                                                 &last_lrf_led_value,
                                                 0u);
            }
            if ((telem_rc == -4) || (telem_rc == -5)) {
                owl_main_force_sw_lrf_status_led(&udp_mcast_seq,
                                                 sw_lrf_status_led_id,
                                                 &last_lrf_led_value,
                                                 0u);
            }
        } else {
            platform_sleep_ms(50u);
        }
    }

    if (joystick_open != 0) {
        joystick_rx_stop();
    }
    if (button_led_open != 0) {
        button_led_rx_stop();
    }
    if (api2_open != 0) {
        owl_main_publish_shutdown_leds_off(&udp_mcast_seq, "config.ini");
    }
    if (api2_open != 0) {
        api2_shutdown();
    }
    mqtt_close();
    if (telem_open != 0) {
        camera_telem_close(&telem);
    }

    owl_cam_close(&cam);
    return 0;
}
