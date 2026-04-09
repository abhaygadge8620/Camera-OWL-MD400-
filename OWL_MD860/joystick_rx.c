#include "joystick_rx.h"

#include "api2_mcast_win.h"
#include "tracker.h"
#include "../platform_compat.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct
{
    float axis_x;
    float axis_y;
    float thumb_a;
    float thumb_b;
    float thumb_c;
    uint8_t button_1;
    uint8_t button_2;
    uint8_t button_3;
    uint8_t button_4;
    uint8_t button_5;
    uint8_t trigger_up;
    uint8_t trigger_down;
    uint8_t reserved_1;
    uint8_t reserved_2;
    uint8_t reserved_3;
    uint8_t reserved_4;
    int8_t hat_0_x;
    int8_t hat_0_y;
    uint8_t connected;
    double heartbeat;
} joystick_state_t;

static pthread_t g_joystick_rx_thread;
static int g_joystick_rx_thread_started = 0;
static volatile int g_joystick_rx_running = 0;
static owl_cam_t *g_joystick_rx_cam = NULL;
static pthread_mutex_t *g_joystick_rx_cam_lock = NULL;
static const uint8_t *g_joystick_selected_cam_ptr = NULL;
static uint8_t g_joystick_active_cam = 0u;
static uint32_t g_joystick_button_led_seq = 1u;

#define JOYSTICK_HAT_TRACKER_STEP_PX (10u)
#define JOYSTICK_AUTO_TRACK_ON_MODE  (OWL_TRK_AUTO_ALL)
#define JOYSTICK_AUTO_TRACK_ON_SENS  (5u)
#define JOYSTICK_AUTO_TRACK_OFF_MODE (OWL_TRK_AUTO_NONE)
#define JOYSTICK_AUTO_TRACK_OFF_SENS (1u)
#define JOYSTICK_DAY_TRACKER_MAX_X       (1919u)
#define JOYSTICK_DAY_TRACKER_MAX_Y       (1079u)
#define JOYSTICK_THERMAL_TRACKER_MAX_X   (1279u)
#define JOYSTICK_THERMAL_TRACKER_MAX_Y   (719u)
#define JOYSTICK_LRF_LED_ID              (76u)
#define JOYSTICK_BUTTON_LED_SOURCE       "owl_md860_joystick"

static void joystick_publish_lrf_led(uint8_t value)
{
    if (api2_send_button_led_state_json(g_joystick_button_led_seq,
                                        JOYSTICK_LRF_LED_ID,
                                        "LRF_LED",
                                        value,
                                        "tx",
                                        JOYSTICK_BUTTON_LED_SOURCE) >= 0) {
        g_joystick_button_led_seq++;
    }
}

static float joystick_absf(float value)
{
    return (value < 0.0f) ? -value : value;
}

static const char *joystick_find_key_value(const char *json_str, const char *key)
{
    char pattern[64];
    const char *match;
    int n;

    if ((json_str == NULL) || (key == NULL)) {
        return NULL;
    }

    n = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if ((n < 0) || ((size_t)n >= sizeof(pattern))) {
        return NULL;
    }

    match = strstr(json_str, pattern);
    if (match == NULL) {
        return NULL;
    }

    return match + (size_t)n;
}

static const char *joystick_find_key_value_in_section(const char *json_str, const char *section_key, const char *key)
{
    char section_pattern[64];
    const char *section_start;
    const char *section_end;
    int n;

    if ((json_str == NULL) || (section_key == NULL) || (key == NULL)) {
        return NULL;
    }

    n = snprintf(section_pattern, sizeof(section_pattern), "\"%s\":", section_key);
    if ((n < 0) || ((size_t)n >= sizeof(section_pattern))) {
        return NULL;
    }

    section_start = strstr(json_str, section_pattern);
    if (section_start == NULL) {
        return NULL;
    }

    section_end = strchr(section_start, '}');
    if (section_end == NULL) {
        return NULL;
    }

    {
        char key_pattern[64];
        const char *match;

        n = snprintf(key_pattern, sizeof(key_pattern), "\"%s\":", key);
        if ((n < 0) || ((size_t)n >= sizeof(key_pattern))) {
            return NULL;
        }

        match = strstr(section_start, key_pattern);
        if ((match == NULL) || (match >= section_end)) {
            return NULL;
        }

        return match + (size_t)n;
    }
}

static int joystick_parse_float_field(const char *json_str, const char *key, float *out_value)
{
    const char *value_str;
    char *endptr = NULL;
    double parsed;

    if ((json_str == NULL) || (key == NULL) || (out_value == NULL)) {
        return -1;
    }

    value_str = joystick_find_key_value(json_str, key);
    if (value_str == NULL) {
        return -1;
    }

    parsed = strtod(value_str, &endptr);
    if (endptr == value_str) {
        return -1;
    }

    *out_value = (float)parsed;
    return 0;
}

static int joystick_parse_float_field_in_section(const char *json_str, const char *section_key,
    const char *key, float *out_value)
{
    const char *value_str;
    char *endptr = NULL;
    double parsed;

    if ((json_str == NULL) || (section_key == NULL) || (key == NULL) || (out_value == NULL)) {
        return -1;
    }

    value_str = joystick_find_key_value_in_section(json_str, section_key, key);
    if (value_str == NULL) {
        return -1;
    }

    parsed = strtod(value_str, &endptr);
    if (endptr == value_str) {
        return -1;
    }

    *out_value = (float)parsed;
    return 0;
}

static int joystick_parse_u8_field(const char *json_str, const char *key, uint8_t *out_value)
{
    const char *value_str;
    char *endptr = NULL;
    long parsed;

    if ((json_str == NULL) || (key == NULL) || (out_value == NULL)) {
        return -1;
    }

    value_str = joystick_find_key_value(json_str, key);
    if (value_str == NULL) {
        return -1;
    }

    parsed = strtol(value_str, &endptr, 10);
    if (endptr == value_str) {
        return -1;
    }
    if (parsed < 0L) {
        parsed = 0L;
    }
    if (parsed > 255L) {
        parsed = 255L;
    }

    *out_value = (uint8_t)parsed;
    return 0;
}

static int joystick_parse_u8_field_in_section(const char *json_str, const char *section_key,
    const char *key, uint8_t *out_value)
{
    const char *value_str;
    char *endptr = NULL;
    long parsed;

    if ((json_str == NULL) || (section_key == NULL) || (key == NULL) || (out_value == NULL)) {
        return -1;
    }

    value_str = joystick_find_key_value_in_section(json_str, section_key, key);
    if (value_str == NULL) {
        return -1;
    }

    parsed = strtol(value_str, &endptr, 10);
    if (endptr == value_str) {
        return -1;
    }
    if (parsed < 0L) {
        parsed = 0L;
    }
    if (parsed > 255L) {
        parsed = 255L;
    }

    *out_value = (uint8_t)parsed;
    return 0;
}

static int joystick_parse_bool_field(const char *json_str, const char *key, uint8_t *out_value)
{
    const char *value_str;

    if ((json_str == NULL) || (key == NULL) || (out_value == NULL)) {
        return -1;
    }

    value_str = joystick_find_key_value(json_str, key);
    if (value_str == NULL) {
        return -1;
    }

    while ((*value_str != '\0') && isspace((unsigned char)*value_str)) {
        ++value_str;
    }

    if (strncmp(value_str, "true", 4u) == 0) {
        *out_value = 1u;
        return 0;
    }
    if (strncmp(value_str, "false", 5u) == 0) {
        *out_value = 0u;
        return 0;
    }

    return -1;
}

static int joystick_parse_i8_field(const char *json_str, const char *key, int8_t *out_value)
{
    const char *value_str;
    char *endptr = NULL;
    long parsed;

    if ((json_str == NULL) || (key == NULL) || (out_value == NULL)) {
        return -1;
    }

    value_str = joystick_find_key_value(json_str, key);
    if (value_str == NULL) {
        return -1;
    }

    parsed = strtol(value_str, &endptr, 10);
    if (endptr == value_str) {
        return -1;
    }
    if (parsed < -128L) {
        parsed = -128L;
    }
    if (parsed > 127L) {
        parsed = 127L;
    }

    *out_value = (int8_t)parsed;
    return 0;
}

static int joystick_parse_i8_field_in_section(const char *json_str, const char *section_key,
    const char *key, int8_t *out_value)
{
    const char *value_str;
    char *endptr = NULL;
    long parsed;

    if ((json_str == NULL) || (section_key == NULL) || (key == NULL) || (out_value == NULL)) {
        return -1;
    }

    value_str = joystick_find_key_value_in_section(json_str, section_key, key);
    if (value_str == NULL) {
        return -1;
    }

    parsed = strtol(value_str, &endptr, 10);
    if (endptr == value_str) {
        return -1;
    }
    if (parsed < -128L) {
        parsed = -128L;
    }
    if (parsed > 127L) {
        parsed = 127L;
    }

    *out_value = (int8_t)parsed;
    return 0;
}

static int joystick_parse_i8_field_in_nested_section(const char *json_str,
    const char *outer_section_key,
    const char *inner_section_key,
    const char *key,
    int8_t *out_value)
{
    const char *outer_start;
    const char *outer_end;
    char outer_pattern[64];
    int n;

    if ((json_str == NULL) || (outer_section_key == NULL) || (inner_section_key == NULL) ||
        (key == NULL) || (out_value == NULL)) {
        return -1;
    }

    n = snprintf(outer_pattern, sizeof(outer_pattern), "\"%s\":", outer_section_key);
    if ((n < 0) || ((size_t)n >= sizeof(outer_pattern))) {
        return -1;
    }

    outer_start = strstr(json_str, outer_pattern);
    if (outer_start == NULL) {
        return -1;
    }

    outer_end = strchr(outer_start, '}');
    if (outer_end == NULL) {
        return -1;
    }

    {
        const char *inner_value;
        char inner_pattern[64];
        char *endptr = NULL;
        long parsed;

        n = snprintf(inner_pattern, sizeof(inner_pattern), "\"%s\":", inner_section_key);
        if ((n < 0) || ((size_t)n >= sizeof(inner_pattern))) {
            return -1;
        }

        inner_value = strstr(outer_start, inner_pattern);
        if ((inner_value == NULL) || (inner_value >= outer_end)) {
            return -1;
        }

        inner_value = joystick_find_key_value_in_section(inner_value, inner_section_key, key);
        if (inner_value == NULL) {
            return -1;
        }

        parsed = strtol(inner_value, &endptr, 10);
        if (endptr == inner_value) {
            return -1;
        }
        if (parsed < -128L) {
            parsed = -128L;
        }
        if (parsed > 127L) {
            parsed = 127L;
        }

        *out_value = (int8_t)parsed;
        return 0;
    }
}

static int joystick_parse_double_field(const char *json_str, const char *key, double *out_value)
{
    const char *value_str;
    char *endptr = NULL;

    if ((json_str == NULL) || (key == NULL) || (out_value == NULL)) {
        return -1;
    }

    value_str = joystick_find_key_value(json_str, key);
    if (value_str == NULL) {
        return -1;
    }

    *out_value = strtod(value_str, &endptr);
    if (endptr == value_str) {
        return -1;
    }

    return 0;
}

static int joystick_parse_state(const char *json_str, joystick_state_t *state)
{
    if ((json_str == NULL) || (state == NULL)) {
        return -1;
    }

    if (joystick_parse_float_field_in_section(json_str, "axes", "x", &state->axis_x) != 0 ||
        joystick_parse_float_field_in_section(json_str, "axes", "y", &state->axis_y) != 0 ||
        joystick_parse_float_field_in_section(json_str, "thumbwheels", "thumb_a", &state->thumb_a) != 0 ||
        joystick_parse_float_field_in_section(json_str, "thumbwheels", "thumb_b", &state->thumb_b) != 0 ||
        joystick_parse_float_field_in_section(json_str, "thumbwheels", "thumb_c", &state->thumb_c) != 0 ||
        joystick_parse_u8_field_in_section(json_str, "buttons", "button_1", &state->button_1) != 0 ||
        joystick_parse_u8_field_in_section(json_str, "buttons", "button_2", &state->button_2) != 0 ||
        joystick_parse_u8_field_in_section(json_str, "buttons", "button_3", &state->button_3) != 0 ||
        joystick_parse_u8_field_in_section(json_str, "buttons", "button_4", &state->button_4) != 0 ||
        joystick_parse_u8_field_in_section(json_str, "buttons", "button_5", &state->button_5) != 0 ||
        joystick_parse_u8_field_in_section(json_str, "buttons", "trigger_up", &state->trigger_up) != 0 ||
        joystick_parse_u8_field_in_section(json_str, "buttons", "trigger_down", &state->trigger_down) != 0 ||
        ((joystick_parse_u8_field_in_section(json_str, "buttons", "reserved_1", &state->reserved_1) != 0) &&
         (joystick_parse_u8_field_in_section(json_str, "buttons", "button_6", &state->reserved_1) != 0)) ||
        joystick_parse_u8_field_in_section(json_str, "buttons", "reserved_2", &state->reserved_2) != 0 ||
        joystick_parse_u8_field_in_section(json_str, "buttons", "reserved_3", &state->reserved_3) != 0 ||
        joystick_parse_u8_field_in_section(json_str, "buttons", "reserved_4", &state->reserved_4) != 0 ||
        ((joystick_parse_i8_field_in_section(json_str, "hat_0", "x", &state->hat_0_x) != 0) &&
         (joystick_parse_i8_field_in_nested_section(json_str, "hats", "hat_0", "x", &state->hat_0_x) != 0)) ||
        ((joystick_parse_i8_field_in_section(json_str, "hat_0", "y", &state->hat_0_y) != 0) &&
         (joystick_parse_i8_field_in_nested_section(json_str, "hats", "hat_0", "y", &state->hat_0_y) != 0)) ||
        joystick_parse_bool_field(json_str, "connected", &state->connected) != 0 ||
        joystick_parse_double_field(json_str, "heartbeat", &state->heartbeat) != 0) {
        return -1;
    }

    return 0;
}

static uint8_t joystick_get_selected_cam(void)
{
    if (g_joystick_selected_cam_ptr == NULL) {
        return OWL_TRACK_CAM_THERMAL;
    }

    return *g_joystick_selected_cam_ptr;
}

static uint8_t joystick_axis_to_speed(float value, uint8_t max_speed)
{
    const float mag = joystick_absf(value);
    uint8_t speed;

    if (mag < 0.15f) {
        return 0u;
    }

    speed = (uint8_t)(mag * (float)max_speed);
    if (speed == 0u) {
        speed = 1u;
    }
    if (speed > max_speed) {
        speed = max_speed;
    }

    return speed;
}

static const char *joystick_camera_name(uint8_t cam_id)
{
    switch (cam_id) {
        case OWL_TRACK_CAM_DAY1:
            return "DAY1";
        case OWL_TRACK_CAM_DAY2:
            return "DAY2";
        case OWL_TRACK_CAM_THERMAL:
            return "THERMAL";
        default:
            return "UNKNOWN";
    }
}

static void joystick_get_tracker_center_for_cam(uint8_t cam_id, uint16_t *x_out, uint16_t *y_out)
{
    uint16_t x = 960u;
    uint16_t y = 540u;

    if ((x_out == NULL) || (y_out == NULL)) {
        return;
    }

    if (cam_id == OWL_TRACK_CAM_THERMAL) {
        x = 640u;
        y = 360u;
    }

    *x_out = x;
    *y_out = y;
}

static void joystick_get_tracker_limits_for_cam(uint8_t cam_id, uint16_t *max_x_out, uint16_t *max_y_out)
{
    uint16_t max_x = JOYSTICK_DAY_TRACKER_MAX_X;
    uint16_t max_y = JOYSTICK_DAY_TRACKER_MAX_Y;

    if ((max_x_out == NULL) || (max_y_out == NULL)) {
        return;
    }

    if (cam_id == OWL_TRACK_CAM_THERMAL) {
        max_x = JOYSTICK_THERMAL_TRACKER_MAX_X;
        max_y = JOYSTICK_THERMAL_TRACKER_MAX_Y;
    }

    *max_x_out = max_x;
    *max_y_out = max_y;
}

static void joystick_log_action_result(const char *event_name,
                                       uint8_t cam_id,
                                       int rc,
                                       const char *detail)
{
    (void)fprintf((rc == OWL_OK) ? stdout : stderr,
                  "[joystick_action] %s cam=%s%s%s result=%s rc=%d\n",
                  (event_name != NULL) ? event_name : "unknown",
                  joystick_camera_name(cam_id),
                  ((detail != NULL) && (detail[0] != '\0')) ? " " : "",
                  ((detail != NULL) && (detail[0] != '\0')) ? detail : "",
                  (rc == OWL_OK) ? "success" : "fail",
                  rc);
    (void)fflush(stdout);
}

static int joystick_should_ignore_packet(const char *json_str)
{
    const char *p = json_str;

    if (json_str == NULL) {
        return 1;
    }

    while ((*p != '\0') && isspace((unsigned char)*p)) {
        ++p;
    }

    if (*p != '{') {
        return 1;
    }

    if ((strstr(json_str, "\"channel\":\"button_led\"") != NULL) ||
        (strstr(json_str, "\"type\":\"button_led\"") != NULL) ||
        (strstr(json_str, "\"type\":\"button_led_snapshot\"") != NULL)) {
        return 1;
    }

    return 0;
}

static int joystick_stop_zoom_locked(uint8_t cam_id)
{
    if (g_joystick_rx_cam == NULL) {
        return OWL_ERR_ARG;
    }

    if (cam_id == OWL_TRACK_CAM_DAY1) {
        return owl_cam_day_lowlight_zoom_stop(g_joystick_rx_cam);
    }
    if (cam_id == OWL_TRACK_CAM_DAY2) {
        return owl_cam_day_normal_zoom_stop(g_joystick_rx_cam);
    }
    if (cam_id == OWL_TRACK_CAM_THERMAL) {
        return owl_cam_thermal_zoom_stop(g_joystick_rx_cam);
    }

    return OWL_ERR_ARG;
}

static int joystick_stop_focus_locked(uint8_t cam_id)
{
    if (g_joystick_rx_cam == NULL) {
        return OWL_ERR_ARG;
    }

    if (cam_id == OWL_TRACK_CAM_DAY1) {
        return owl_cam_day_lowlight_focus_stop(g_joystick_rx_cam);
    }
    if (cam_id == OWL_TRACK_CAM_DAY2) {
        return owl_cam_day_normal_focus_stop(g_joystick_rx_cam);
    }
    if (cam_id == OWL_TRACK_CAM_THERMAL) {
        return owl_cam_thermal_focus_stop(g_joystick_rx_cam);
    }

    return OWL_ERR_ARG;
}

static int joystick_apply_zoom_locked(uint8_t cam_id, float axis_value)
{
    const uint8_t direction = (axis_value > 0.0f) ? OWL_ZOOM_IN : OWL_ZOOM_OUT;
    const uint8_t speed = joystick_axis_to_speed(axis_value, 7u);

    if (g_joystick_rx_cam == NULL) {
        return OWL_ERR_ARG;
    }

    if (speed == 0u) {
        return joystick_stop_zoom_locked(cam_id);
    }

    if (cam_id == OWL_TRACK_CAM_DAY1) {
        return owl_cam_day_lowlight_zoom(g_joystick_rx_cam, OWL_ZOOM_MODE_CONTINUOUS, direction, speed);
    }
    if (cam_id == OWL_TRACK_CAM_DAY2) {
        return owl_cam_day_normal_zoom(g_joystick_rx_cam, OWL_ZOOM_MODE_CONTINUOUS, direction, speed);
    }
    if (cam_id == OWL_TRACK_CAM_THERMAL) {
        return owl_cam_thermal_zoom(g_joystick_rx_cam, OWL_ZOOM_MODE_CONTINUOUS, direction);
    }

    return OWL_ERR_ARG;
}

static int joystick_apply_focus_locked(uint8_t cam_id, float axis_value)
{
    const uint8_t direction = (axis_value > 0.0f) ? OWL_FOCUS_FAR : OWL_FOCUS_NEAR;
    const uint8_t speed = joystick_axis_to_speed(axis_value, 7u);

    if (g_joystick_rx_cam == NULL) {
        return OWL_ERR_ARG;
    }

    if (speed == 0u) {
        return joystick_stop_focus_locked(cam_id);
    }

    if (cam_id == OWL_TRACK_CAM_DAY1) {
        return owl_cam_day_lowlight_focus(g_joystick_rx_cam, OWL_ZOOM_MODE_CONTINUOUS, direction, speed);
    }
    if (cam_id == OWL_TRACK_CAM_DAY2) {
        return owl_cam_day_normal_focus(g_joystick_rx_cam, OWL_ZOOM_MODE_CONTINUOUS, direction, speed);
    }
    if (cam_id == OWL_TRACK_CAM_THERMAL) {
        return owl_cam_thermal_focus(g_joystick_rx_cam, OWL_ZOOM_MODE_CONTINUOUS, direction);
    }

    return OWL_ERR_ARG;
}

static int joystick_sync_selected_camera_locked(uint8_t cam_id)
{
    int rc;

    if (g_joystick_rx_cam == NULL) {
        return OWL_ERR_ARG;
    }
    if ((cam_id != OWL_TRACK_CAM_DAY1) &&
        (cam_id != OWL_TRACK_CAM_DAY2) &&
        (cam_id != OWL_TRACK_CAM_THERMAL)) {
        return OWL_ERR_ARG;
    }

    if (g_joystick_active_cam == cam_id) {
        return OWL_OK;
    }

    rc = owl_cam_tracker_switch_camera(g_joystick_rx_cam, cam_id);
    if (rc == OWL_OK) {
        g_joystick_active_cam = cam_id;
    }

    return rc;
}

static void joystick_stop_lens_motion_locked(uint8_t cam_id)
{
    (void)joystick_stop_zoom_locked(cam_id);
    (void)joystick_stop_focus_locked(cam_id);
}
static uint16_t clamp_u16(uint16_t v, uint16_t min_v, uint16_t max_v)
{
    if (v < min_v) {
        return min_v;
    }
    if (v > max_v) {
        return max_v;
    }
    return v;
}

static int joystick_apply_hat_tracker_move_locked(uint8_t cam_id, int8_t hat_x, int8_t hat_y)
{
    tracker_state_t st;
    uint16_t new_x;
    uint16_t new_y;
    uint16_t max_x;
    uint16_t max_y;
    int32_t tmp_x;
    int32_t tmp_y;
    int rc;

    if ((g_joystick_rx_cam == NULL) || (cam_id == 0u)) {
        return OWL_ERR_ARG;
    }

    if ((hat_x == 0) && (hat_y == 0)) {
        return OWL_OK;
    }

    rc = owl_tracker_read_all(&st);
    if (rc != OWL_OK) {
        return rc;
    }

    joystick_get_tracker_limits_for_cam(cam_id, &max_x, &max_y);

    tmp_x = (int32_t)st.coord_x;
    tmp_y = (int32_t)st.coord_y;

    if (hat_x < 0) {
        tmp_x -= (int32_t)JOYSTICK_HAT_TRACKER_STEP_PX;
    } else if (hat_x > 0) {
        tmp_x += (int32_t)JOYSTICK_HAT_TRACKER_STEP_PX;
    }

    if (hat_y > 0) {
        tmp_y -= (int32_t)JOYSTICK_HAT_TRACKER_STEP_PX;
    } else if (hat_y < 0) {
        tmp_y += (int32_t)JOYSTICK_HAT_TRACKER_STEP_PX;
    }

    new_x = clamp_u16((uint16_t)((tmp_x < 0) ? 0 : tmp_x), 0u, max_x);
    new_y = clamp_u16((uint16_t)((tmp_y < 0) ? 0 : tmp_y), 0u, max_y);

    return owl_tracker_set_coord(cam_id, new_x, new_y, OWL_TRK_MODE_ON);
}
static void *joystick_rx_thread_main(void *arg)
{
    joystick_state_t prev_state;
    uint8_t prev_selected_cam = 0u;
    uint32_t err_counter = 0u;
    uint32_t parse_fail_counter = 0u;

    (void)arg;
    (void)memset(&prev_state, 0, sizeof(prev_state));

    while (g_joystick_rx_running != 0) {
        char json_buf[API2_MAX_JSON];
        joystick_state_t cur_state;
        uint8_t selected_cam;
        int rc;

        rc = api2_poll_joystick_json(json_buf, sizeof(json_buf));
        if (rc < 0) {
            ++err_counter;
            if ((err_counter == 1u) || ((err_counter % 50u) == 0u)) {
                (void)fprintf(stderr,
                              "[joystick_mcast] recv error rc=%d count=%u\n",
                              rc,
                              (unsigned)err_counter);
            }
            platform_sleep_ms(20u);
            continue;
        }
        if (rc == 0) {
            platform_sleep_ms(10u);
            continue;
        }

        err_counter = 0u;

        if (joystick_should_ignore_packet(json_buf) != 0) {
            continue;
        }

        if (joystick_parse_state(json_buf, &cur_state) != 0) {
            ++parse_fail_counter;
            (void)fprintf(stderr,
                          "[joystick_mcast] parse failed count=%u json=%s\n",
                          (unsigned)parse_fail_counter,
                          json_buf);
            continue;
        }

        selected_cam = joystick_get_selected_cam();

        if (pthread_mutex_lock(g_joystick_rx_cam_lock) != 0) {
            continue;
        }

        if ((selected_cam != prev_selected_cam) || (cur_state.connected == 0u)) {
            if (prev_selected_cam != 0u) {
                rc = joystick_stop_zoom_locked(prev_selected_cam);
                joystick_log_action_result("selected_cam_change/zoom_stop", prev_selected_cam, rc, "");
                rc = joystick_stop_focus_locked(prev_selected_cam);
                joystick_log_action_result("selected_cam_change/focus_stop", prev_selected_cam, rc, "");
            }
            prev_state.thumb_a = 0.0f;
            prev_state.thumb_b = 0.0f;
            prev_state.hat_0_x = 0;
            prev_state.hat_0_y = 0;
            if ((cur_state.connected != 0u) && (selected_cam != 0u)) {
                rc = joystick_sync_selected_camera_locked(selected_cam);
                joystick_log_action_result("selected_cam_change/switch", selected_cam, rc, "");
                if (rc != OWL_OK) {
                    (void)pthread_mutex_unlock(g_joystick_rx_cam_lock);
                    continue;
                }
            }
            prev_selected_cam = selected_cam;
        } else if ((cur_state.connected != 0u) && (selected_cam != 0u)) {
            rc = joystick_sync_selected_camera_locked(selected_cam);
            if (rc != OWL_OK) {
                joystick_log_action_result("selected_cam_sync", selected_cam, rc, "");
                (void)pthread_mutex_unlock(g_joystick_rx_cam_lock);
                continue;
            }
        }

        if ((joystick_absf(cur_state.thumb_a - prev_state.thumb_a) >= 0.10f) ||
            ((joystick_axis_to_speed(cur_state.thumb_a, 7u) == 0u) != (joystick_axis_to_speed(prev_state.thumb_a, 7u) == 0u))) {
            rc = joystick_apply_focus_locked(selected_cam, cur_state.thumb_a);
            if (rc != OWL_OK) {
                char detail[64];
                (void)snprintf(detail, sizeof(detail), "thumb_a=%.3f", (double)cur_state.thumb_a);
                joystick_log_action_result("focus", selected_cam, rc, detail);
            }
            prev_state.thumb_a = cur_state.thumb_a;
        }

        if ((joystick_absf(cur_state.thumb_b - prev_state.thumb_b) >= 0.10f) ||
            ((joystick_axis_to_speed(cur_state.thumb_b, 7u) == 0u) != (joystick_axis_to_speed(prev_state.thumb_b, 7u) == 0u))) {
            rc = joystick_apply_zoom_locked(selected_cam, cur_state.thumb_b);
            if (rc != OWL_OK) {
                char detail[64];
                (void)snprintf(detail, sizeof(detail), "thumb_b=%.3f", (double)cur_state.thumb_b);
                joystick_log_action_result("zoom", selected_cam, rc, detail);
            }
            prev_state.thumb_b = cur_state.thumb_b;
        }

        if ((cur_state.button_4 != prev_state.button_4) &&
            (cur_state.button_4 != 0u) &&
            (selected_cam != 0u)) {
            uint16_t tracker_x = 0u;
            uint16_t tracker_y = 0u;
            char detail[64];

            joystick_get_tracker_center_for_cam(selected_cam, &tracker_x, &tracker_y);
            rc = owl_tracker_mode_on(selected_cam, tracker_x, tracker_y);
            (void)snprintf(detail, sizeof(detail), "pressed center=(%u,%u)",
                           (unsigned)tracker_x, (unsigned)tracker_y);
            joystick_log_action_result("button_4 tracker=on", selected_cam, rc, detail);
        }
        prev_state.button_4 = cur_state.button_4;

        if ((cur_state.button_5 != prev_state.button_5) &&
            (cur_state.button_5 != 0u) &&
            (selected_cam != 0u)) {
            uint16_t tracker_x = 0u;
            uint16_t tracker_y = 0u;
            char detail[64];

            joystick_get_tracker_center_for_cam(selected_cam, &tracker_x, &tracker_y);
            rc = owl_tracker_mode_off(selected_cam, tracker_x, tracker_y);
            (void)snprintf(detail, sizeof(detail), "pressed center=(%u,%u)",
                           (unsigned)tracker_x, (unsigned)tracker_y);
            joystick_log_action_result("button_5 tracker=off", selected_cam, rc, detail);
        }
        prev_state.button_5 = cur_state.button_5;

        if (cur_state.button_2 != prev_state.button_2) {
            rc = owl_cam_lrf_align_pointer(g_joystick_rx_cam, (cur_state.button_2 != 0u));
            if (rc == OWL_OK) {
                joystick_publish_lrf_led((cur_state.button_2 != 0u) ? 1u : 0u);
            }
            joystick_log_action_result((cur_state.button_2 != 0u) ? "button_2 lrf_align=on" : "button_2 lrf_align=off",
                                       selected_cam,
                                       rc,
                                       (cur_state.button_2 != 0u) ? "pressed" : "released");
        }
        prev_state.button_2 = cur_state.button_2;

        if (((cur_state.hat_0_x != prev_state.hat_0_x) ||
             (cur_state.hat_0_y != prev_state.hat_0_y)) &&
            (selected_cam != 0u) &&
            (cur_state.connected != 0u)) {
            char detail[64];
            rc = joystick_apply_hat_tracker_move_locked(selected_cam,
                                                        cur_state.hat_0_x,
                                                        cur_state.hat_0_y);
            (void)snprintf(detail, sizeof(detail), "hat=(%d,%d)",
                           (int)cur_state.hat_0_x, (int)cur_state.hat_0_y);
            joystick_log_action_result("hat_tracker_move", selected_cam, rc, detail);
        }
        prev_state.hat_0_x = cur_state.hat_0_x;
        prev_state.hat_0_y = cur_state.hat_0_y;

        prev_state.connected = cur_state.connected;
        prev_selected_cam = selected_cam;

        (void)pthread_mutex_unlock(g_joystick_rx_cam_lock);
    }

    if ((g_joystick_rx_cam != NULL) && (g_joystick_rx_cam_lock != NULL) &&
        (pthread_mutex_lock(g_joystick_rx_cam_lock) == 0)) {
        joystick_stop_lens_motion_locked(prev_selected_cam);
        (void)pthread_mutex_unlock(g_joystick_rx_cam_lock);
    }

    return NULL;
}

int joystick_rx_start(owl_cam_t *cam, pthread_mutex_t *cam_lock, const uint8_t *selected_cam_ptr)
{
    if ((cam == NULL) || (cam_lock == NULL) || (selected_cam_ptr == NULL)) {
        return -1;
    }
    if (g_joystick_rx_thread_started != 0) {
        return 0;
    }

    g_joystick_rx_cam = cam;
    g_joystick_rx_cam_lock = cam_lock;
    g_joystick_selected_cam_ptr = selected_cam_ptr;
    g_joystick_active_cam = 0u;
    g_joystick_button_led_seq = 1u;
    g_joystick_rx_running = 1;

    if (pthread_create(&g_joystick_rx_thread, NULL, joystick_rx_thread_main, NULL) != 0) {
        g_joystick_rx_running = 0;
        g_joystick_rx_cam = NULL;
        g_joystick_rx_cam_lock = NULL;
        g_joystick_selected_cam_ptr = NULL;
        g_joystick_active_cam = 0u;
        return -1;
    }

    g_joystick_rx_thread_started = 1;
    return 0;
}

void joystick_rx_stop(void)
{
    if (g_joystick_rx_thread_started == 0) {
        g_joystick_rx_running = 0;
        g_joystick_rx_cam = NULL;
        g_joystick_rx_cam_lock = NULL;
        g_joystick_selected_cam_ptr = NULL;
        g_joystick_active_cam = 0u;
        return;
    }

    g_joystick_rx_running = 0;
    (void)pthread_join(g_joystick_rx_thread, NULL);
    g_joystick_rx_thread_started = 0;
    g_joystick_rx_cam = NULL;
    g_joystick_rx_cam_lock = NULL;
    g_joystick_selected_cam_ptr = NULL;
    g_joystick_active_cam = 0u;
}
