#include "button_led_rx.h"

#include "api2_mcast_win.h"
#include "ini_parser.h"
#include "tracker.h"
#include "../platform_compat.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint8_t seen;
    uint8_t value;
} button_led_state_t;

static pthread_t g_button_led_rx_thread;
static int g_button_led_rx_thread_started = 0;
static volatile int g_button_led_rx_running = 0;
static owl_cam_t *g_button_led_rx_cam = NULL;
static pthread_mutex_t *g_button_led_rx_cam_lock = NULL;
static uint8_t *g_button_led_selected_cam_ptr = NULL;
static button_led_state_t g_button_led_states[256];
static uint32_t g_button_led_tx_seq = 1u;

#define BUTTON_LED_TX_SOURCE "owl_md860"

static int button_led_apply_action(const char *name, uint8_t value);
static int button_led_switch_camera(uint8_t cam_id);

static void button_led_get_tracker_center_for_cam(uint8_t cam_id, uint16_t *x_out, uint16_t *y_out)
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

static int button_led_name_equals(const char *lhs, const char *rhs)
{
    return (platform_strcasecmp(lhs, rhs) == 0) ? 1 : 0;
}

static int button_led_name_is_tracked(const char *name)
{
    if (name == NULL) {
        return 0;
    }

    return (button_led_name_equals(name, "OPTICS_RESET") != 0) ||
           (button_led_name_equals(name, "LRF_RESET") != 0) ||
           (button_led_name_equals(name, "DROP") != 0) ||
           (button_led_name_equals(name, "DAY") != 0) ||
           (button_led_name_equals(name, "LOW_LIGHT") != 0) ||
           (button_led_name_equals(name, "THERMAL") != 0) ||
           (button_led_name_equals(name, "LRF") != 0) ||
           (button_led_name_equals(name, "MODE") != 0) ||
           (button_led_name_equals(name, "FREQUENCY") != 0) ||
           (button_led_name_equals(name, "GBUTTON2") != 0) ||
           (button_led_name_equals(name, "GBUTTON_2") != 0);
}

static const char *button_led_find_key_value(const char *json_str, const char *key)
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

static int button_led_find_named_object(const char *json_str,
                                        const char *key,
                                        const char **obj_start_out,
                                        const char **obj_end_out)
{
    char pattern[64];
    const char *match;
    const char *start;
    const char *p;
    int depth = 0;
    int n;

    if ((json_str == NULL) || (key == NULL) || (obj_start_out == NULL) || (obj_end_out == NULL)) {
        return -1;
    }

    n = snprintf(pattern, sizeof(pattern), "\"%s\":{", key);
    if ((n < 0) || ((size_t)n >= sizeof(pattern))) {
        return -1;
    }

    match = strstr(json_str, pattern);
    if (match == NULL) {
        return -1;
    }

    start = match + (size_t)n - 1u;
    for (p = start; *p != '\0'; ++p) {
        if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0) {
                *obj_start_out = start;
                *obj_end_out = p;
                return 0;
            }
        }
    }

    return -1;
}

static int button_led_parse_u8_in_range(const char *start,
                                        const char *end,
                                        const char *key,
                                        uint8_t *out_value)
{
    char pattern[32];
    const char *match;
    char *endptr = NULL;
    long parsed;
    int n;

    if ((start == NULL) || (end == NULL) || (key == NULL) || (out_value == NULL) || (start > end)) {
        return -1;
    }

    n = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if ((n < 0) || ((size_t)n >= sizeof(pattern))) {
        return -1;
    }

    match = strstr(start, pattern);
    if ((match == NULL) || (match > end)) {
        return -1;
    }

    match += (size_t)n;
    parsed = strtol(match, &endptr, 10);
    if ((endptr == match) || (endptr > (end + 1))) {
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

static int button_led_copy_string_value(const char *json_str,
                                        const char *key,
                                        char *out,
                                        size_t out_sz)
{
    const char *value_str;
    const char *end_quote;
    size_t len;

    if ((json_str == NULL) || (key == NULL) || (out == NULL) || (out_sz < 2u)) {
        return -1;
    }

    value_str = button_led_find_key_value(json_str, key);
    if ((value_str == NULL) || (*value_str != '"')) {
        return -1;
    }

    value_str++;
    end_quote = strchr(value_str, '"');
    if (end_quote == NULL) {
        return -1;
    }

    len = (size_t)(end_quote - value_str);
    if (len >= out_sz) {
        len = out_sz - 1u;
    }

    (void)memcpy(out, value_str, len);
    out[len] = '\0';
    return 0;
}

static int button_led_map_frequency_value(uint8_t raw_value, uint8_t *freq_mode_out)
{
    if (freq_mode_out == NULL) {
        return -1;
    }

    switch (raw_value) {
        case 1u:
            *freq_mode_out = OWL_LRF_FREQ_1HZ;
            return 0;
        case 4u:
            *freq_mode_out = OWL_LRF_FREQ_4HZ;
            return 0;
        case 10u:
            *freq_mode_out = OWL_LRF_FREQ_10HZ;
            return 0;
        case 20u:
            *freq_mode_out = OWL_LRF_FREQ_20HZ;
            return 0;
        case 100u:
            *freq_mode_out = OWL_LRF_FREQ_100HZ;
            return 0;
        case 200u:
            *freq_mode_out = OWL_LRF_FREQ_200HZ;
            return 0;
        default:
            break;
    }

    return -1;
}

static uint8_t button_led_normalize_mode_value(uint8_t raw_value)
{
    if (raw_value > 127u) {
        return (uint8_t)(256u - (unsigned)raw_value);
    }
    return raw_value;
}

static int button_led_map_mode_value(uint8_t raw_value, uint8_t *mode_out)
{
    const uint8_t normalized_value = button_led_normalize_mode_value(raw_value);

    if (mode_out == NULL) {
        return -1;
    }

    switch (normalized_value) {
        case 0u:
            *mode_out = OWL_LRF_SMM;
            return 0;
        case 1u:
            *mode_out = OWL_LRF_CH1;
            return 0;
        case 2u:
            *mode_out = OWL_LRF_CH2;
            return 0;
        default:
            break;
    }

    return -1;
}

static void button_led_store_mode_value(uint8_t raw_value)
{
    char value_buf[16];
    const uint8_t normalized_value = button_led_normalize_mode_value(raw_value);

    (void)snprintf(value_buf, sizeof(value_buf), "%u", (unsigned)normalized_value);
    if (ini_set_value("config.ini", "runtime", "mode_value", value_buf) != 0) {
        fprintf(stderr, "[button_led_rx] failed to persist runtime.mode_value=%s\n", value_buf);
    }
}

static int button_led_is_own_tx_message(const char *json_str)
{
    char source[32];
    char direction[16];

    if (json_str == NULL) {
        return 0;
    }

    if (button_led_copy_string_value(json_str, "source", source, sizeof(source)) != 0) {
        return 0;
    }
    if (button_led_copy_string_value(json_str, "direction", direction, sizeof(direction)) != 0) {
        return 0;
    }

    if ((strcmp(source, BUTTON_LED_TX_SOURCE) == 0) &&
        (strcmp(direction, "tx") == 0)) {
        return 1;
    }

    return 0;
}

static int button_led_publish_named_led(const char *name, uint8_t value)
{
    int rc;

    if (name == NULL) {
        return -1;
    }

    rc = api2_send_button_led_state_json(g_button_led_tx_seq,
                                         0u,
                                         name,
                                         value,
                                         "tx",
                                         BUTTON_LED_TX_SOURCE);
    if (rc >= 0) {
        g_button_led_tx_seq++;
        return 0;
    }

    return -1;
}

static void button_led_publish_camera_select_leds(const char *selected_name)
{
    const uint8_t day_value =
        (button_led_name_equals(selected_name, "DAY") != 0) ? 1u : 0u;
    const uint8_t low_light_value =
        (button_led_name_equals(selected_name, "LOW_LIGHT") != 0) ? 1u : 0u;
    const uint8_t thermal_value =
        (button_led_name_equals(selected_name, "THERMAL") != 0) ? 1u : 0u;

    (void)button_led_publish_named_led("DAY_LED", day_value);
    (void)button_led_publish_named_led("LOW_LIGHT_LED", low_light_value);
    (void)button_led_publish_named_led("THERMAL_LED", thermal_value);
}

static void button_led_publish_selected_camera_leds(void)
{
    if (g_button_led_selected_cam_ptr == NULL) {
        return;
    }

    if (*g_button_led_selected_cam_ptr == OWL_TRACK_CAM_DAY2) {
        button_led_publish_camera_select_leds("DAY");
    } else if (*g_button_led_selected_cam_ptr == OWL_TRACK_CAM_DAY1) {
        button_led_publish_camera_select_leds("LOW_LIGHT");
    } else if (*g_button_led_selected_cam_ptr == OWL_TRACK_CAM_THERMAL) {
        button_led_publish_camera_select_leds("THERMAL");
    } else {
        button_led_publish_camera_select_leds("");
    }
}

static int button_led_switch_camera(uint8_t cam_id)
{
    int rc;

    if (g_button_led_rx_cam == NULL) {
        return OWL_ERR_ARG;
    }

    rc = owl_cam_tracker_switch_camera(g_button_led_rx_cam, cam_id);
    if (rc != OWL_OK) {
        return rc;
    }

    if (g_button_led_selected_cam_ptr != NULL) {
        *g_button_led_selected_cam_ptr = cam_id;
    }
    /* (void)button_led_publish_named_led("GBUTTON2_LED", 0u); */

    return OWL_OK;
}

static int button_led_handle_state_change(uint8_t id, const char *name, uint8_t value)
{
    int rc;

    if ((name == NULL) || (button_led_name_is_tracked(name) == 0)) {
        return 0;
    }

    if (g_button_led_states[id].seen == 0u) {
        g_button_led_states[id].seen = 1u;
        g_button_led_states[id].value = value;
        return 0;
    }

    if (g_button_led_states[id].value == value) {
        return 0;
    }

    g_button_led_states[id].value = value;
    rc = button_led_apply_action(name, value);
    if (rc != OWL_OK) {
        fprintf(stderr, "[button_led_rx] action failed name=%s id=%u value=%u rc=%d\n",
                name, (unsigned)id, (unsigned)value, rc);
    } else {
        fprintf(stderr, "[button_led_rx] action ok name=%s id=%u value=%u\n",
                name, (unsigned)id, (unsigned)value);
    }

    return rc;
}

static int button_led_parse_single_json(const char *json_str,
                                        uint8_t *id_out,
                                        char *name_out,
                                        size_t name_out_sz,
                                        uint8_t *value_out)
{
    char type[32];
    const char *value_str;

    if ((json_str == NULL) || (id_out == NULL) || (name_out == NULL) || (name_out_sz < 2u) || (value_out == NULL)) {
        return -1;
    }

    if (button_led_copy_string_value(json_str, "type", type, sizeof(type)) != 0) {
        return -1;
    }
    if (strcmp(type, "button_led") != 0) {
        return -1;
    }
    if (button_led_copy_string_value(json_str, "name", name_out, name_out_sz) != 0) {
        return -1;
    }

    value_str = button_led_find_key_value(json_str, "id");
    if (value_str == NULL) {
        return -1;
    }
    {
        char *endptr = NULL;
        long parsed = strtol(value_str, &endptr, 10);
        if (endptr == value_str) {
            return -1;
        }
        if (parsed < 0L) {
            parsed = 0L;
        }
        if (parsed > 255L) {
            parsed = 255L;
        }
        *id_out = (uint8_t)parsed;
    }

    value_str = button_led_find_key_value(json_str, "value");
    if (value_str == NULL) {
        return -1;
    }
    {
        char *endptr = NULL;
        long parsed = strtol(value_str, &endptr, 10);
        if (endptr == value_str) {
            return -1;
        }
        if (parsed < 0L) {
            parsed += 256L;
        }
        if (parsed < 0L) {
            parsed = 0L;
        }
        if (parsed > 255L) {
            parsed = 255L;
        }
        *value_out = (uint8_t)parsed;
    }

    return 0;
}

static int button_led_parse_snapshot_section(const char *json_str, const char *section_name)
{
    const char *section_start;
    const char *section_end;
    const char *p;

    if ((json_str == NULL) || (section_name == NULL)) {
        return -1;
    }

    if (button_led_find_named_object(json_str, section_name, &section_start, &section_end) != 0) {
        return -1;
    }

    p = section_start + 1;
    while ((p != NULL) && (p < section_end)) {
        const char *name_start;
        const char *name_end;
        const char *entry_obj_start;
        const char *entry_obj_end;
        char name[64];
        uint8_t id = 0u;
        uint8_t value = 0u;
        size_t name_len;

        name_start = strchr(p, '"');
        if ((name_start == NULL) || (name_start >= section_end)) {
            break;
        }
        name_end = strchr(name_start + 1, '"');
        if ((name_end == NULL) || (name_end >= section_end)) {
            break;
        }

        name_len = (size_t)(name_end - (name_start + 1));
        if (name_len >= sizeof(name)) {
            name_len = sizeof(name) - 1u;
        }
        (void)memcpy(name, name_start + 1, name_len);
        name[name_len] = '\0';

        entry_obj_start = strchr(name_end, '{');
        if ((entry_obj_start == NULL) || (entry_obj_start >= section_end)) {
            break;
        }

        {
            const char *q;
            int depth = 0;
            entry_obj_end = NULL;
            for (q = entry_obj_start; q <= section_end; ++q) {
                if (*q == '{') {
                    depth++;
                } else if (*q == '}') {
                    depth--;
                    if (depth == 0) {
                        entry_obj_end = q;
                        break;
                    }
                }
            }
        }
        if (entry_obj_end == NULL) {
            break;
        }

        if ((button_led_parse_u8_in_range(entry_obj_start, entry_obj_end, "id", &id) == 0) &&
            (button_led_parse_u8_in_range(entry_obj_start, entry_obj_end, "value", &value) == 0)) {
            (void)button_led_handle_state_change(id, name, value);
        }

        p = entry_obj_end + 1;
    }

    return 0;
}

static int button_led_parse_snapshot_json(const char *json_str)
{
    char type[32];
    int parsed = 0;

    if (json_str == NULL) {
        return -1;
    }

    if (button_led_copy_string_value(json_str, "type", type, sizeof(type)) != 0) {
        return -1;
    }
    if (strcmp(type, "button_led_snapshot") != 0) {
        return -1;
    }

    if (button_led_parse_snapshot_section(json_str, "buttons") == 0) {
        parsed = 1;
    }
    if (button_led_parse_snapshot_section(json_str, "switches") == 0) {
        parsed = 1;
    }
    if (button_led_parse_snapshot_section(json_str, "knobs") == 0) {
        parsed = 1;
    }

    return (parsed != 0) ? 0 : -1;
}

static int button_led_apply_action(const char *name, uint8_t value)
{
    int rc = OWL_OK;
    uint8_t freq_mode = 0u;
    uint8_t mode_value = 0u;

    if ((name == NULL) || (g_button_led_rx_cam == NULL) || (g_button_led_rx_cam_lock == NULL)) {
        return -1;
    }

    if (pthread_mutex_lock(g_button_led_rx_cam_lock) != 0) {
        return -1;
    }

    if (button_led_name_equals(name, "DAY") != 0) {
        rc = button_led_switch_camera(OWL_TRACK_CAM_DAY2);
        if (rc == OWL_OK) {
            button_led_publish_selected_camera_leds();
        }
    } else if (button_led_name_equals(name, "LOW_LIGHT") != 0) {
        rc = button_led_switch_camera(OWL_TRACK_CAM_DAY1);
        if (rc == OWL_OK) {
            button_led_publish_selected_camera_leds();
        }
    } else if (button_led_name_equals(name, "THERMAL") != 0) {
        rc = button_led_switch_camera(OWL_TRACK_CAM_THERMAL);
        if (rc == OWL_OK) {
            button_led_publish_selected_camera_leds();
        }
    } else if (button_led_name_equals(name, "OPTICS_RESET") != 0) {
        if (value == 1u) {
            rc = owl_cam_restart(g_button_led_rx_cam, OWL_RESTART_SERVICE);
            if (rc == OWL_OK) {
                (void)button_led_publish_named_led("OPTICS_RESET_LED", 1u);
            }
        } else {
            (void)button_led_publish_named_led("OPTICS_RESET_LED", 0u);
        }
    } else if (button_led_name_equals(name, "LRF_RESET") != 0) {
        rc = owl_cam_lrf_stop(g_button_led_rx_cam);
        if (rc == OWL_OK) {
            rc = owl_cam_lrf_align_pointer(g_button_led_rx_cam, false);
        }
        if (rc == OWL_OK) {
            (void)button_led_publish_named_led("LRF_RESET_LED", value);
            (void)button_led_publish_named_led("LRF_LED", 0u);
        }
    } else if (button_led_name_equals(name, "LRF") != 0) {
        rc = owl_cam_lrf_align_pointer(g_button_led_rx_cam, (value == 1u));
        if (rc == OWL_OK) {
            (void)button_led_publish_named_led("LRF_LED", value);
        }
    } else if (button_led_name_equals(name, "DROP") != 0) {
        if (g_button_led_selected_cam_ptr == NULL) {
            rc = OWL_ERR_ARG;
        } else {
            rc = OWL_OK;
            if (value == 1u) {
                uint16_t tracker_x = 0u;
                uint16_t tracker_y = 0u;

                button_led_get_tracker_center_for_cam(*g_button_led_selected_cam_ptr,
                                                      &tracker_x,
                                                      &tracker_y);
                rc = owl_tracker_mode_off(*g_button_led_selected_cam_ptr, tracker_x, tracker_y);
            }
        }
        if (rc == OWL_OK) {
            (void)button_led_publish_named_led("DROP_LED", value);
        }
    } else if (button_led_name_equals(name, "MODE") != 0) {
        if (button_led_map_mode_value(value, &mode_value) != 0) {
            rc = OWL_ERR_ARG;
        } else {
            rc = owl_cam_lrf_single_measure(g_button_led_rx_cam, mode_value);
        }
        if (rc == OWL_OK) {
            button_led_store_mode_value(value);
        }
    } else if (button_led_name_equals(name, "FREQUENCY") != 0) {
        if (button_led_map_frequency_value(value, &freq_mode) != 0) {
            rc = OWL_ERR_ARG;
        } else {
            rc = owl_cam_lrf_set_frequency(g_button_led_rx_cam, freq_mode);
        }
    } else if ((button_led_name_equals(name, "GBUTTON2") != 0) ||
               (button_led_name_equals(name, "GBUTTON_2") != 0) ||
               (button_led_name_equals(name, "GBUTTON2_LED") != 0)) {
        rc = owl_cam_set_pip(g_button_led_rx_cam, (value == 1u));
        if ((rc == OWL_OK) &&
            ((button_led_name_equals(name, "GBUTTON2") != 0) ||
             (button_led_name_equals(name, "GBUTTON_2") != 0))) {
            (void)button_led_publish_named_led("GBUTTON2_LED", value);
        }
    }

    (void)pthread_mutex_unlock(g_button_led_rx_cam_lock);
    return rc;
}

static void *button_led_rx_thread_main(void *arg)
{
    (void)arg;

    while (g_button_led_rx_running != 0) {
        char json_buf[API2_MAX_JSON];
        char name[64];
        uint8_t id = 0u;
        uint8_t value = 0u;
        int rc;

        rc = api2_poll_button_led_json(json_buf, sizeof(json_buf));
        if (rc < 0) {
            platform_sleep_ms(20u);
            continue;
        }
        if (rc == 0) {
            platform_sleep_ms(10u);
            continue;
        }

        if (button_led_is_own_tx_message(json_buf) != 0) {
            continue;
        }

        if (button_led_parse_snapshot_json(json_buf) == 0) {
            continue;
        }

        if (button_led_parse_single_json(json_buf, &id, name, sizeof(name), &value) != 0) {
            continue;
        }

        (void)button_led_handle_state_change(id, name, value);
    }

    return NULL;
}

int button_led_rx_start(owl_cam_t *cam, pthread_mutex_t *cam_lock, uint8_t *selected_cam_ptr)
{
    if ((cam == NULL) || (cam_lock == NULL) || (selected_cam_ptr == NULL)) {
        return -1;
    }
    if (g_button_led_rx_thread_started != 0) {
        return 0;
    }

    g_button_led_rx_cam = cam;
    g_button_led_rx_cam_lock = cam_lock;
    g_button_led_selected_cam_ptr = selected_cam_ptr;
    (void)memset(g_button_led_states, 0, sizeof(g_button_led_states));
    g_button_led_rx_running = 1;

    if (pthread_create(&g_button_led_rx_thread, NULL, button_led_rx_thread_main, NULL) != 0) {
        g_button_led_rx_running = 0;
        g_button_led_rx_cam = NULL;
        g_button_led_rx_cam_lock = NULL;
        g_button_led_selected_cam_ptr = NULL;
        return -1;
    }

    g_button_led_rx_thread_started = 1;
    button_led_publish_selected_camera_leds();
    return 0;
}

void button_led_rx_stop(void)
{
    if (g_button_led_rx_thread_started == 0) {
        g_button_led_rx_running = 0;
        g_button_led_rx_cam = NULL;
        g_button_led_rx_cam_lock = NULL;
        g_button_led_selected_cam_ptr = NULL;
        return;
    }

    g_button_led_rx_running = 0;
    (void)pthread_join(g_button_led_rx_thread, NULL);
    g_button_led_rx_thread_started = 0;
    g_button_led_rx_cam = NULL;
    g_button_led_rx_cam_lock = NULL;
    g_button_led_selected_cam_ptr = NULL;
}
