#include "button_mcast.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../OWL_MD860/api2_mcast_win.h"
#include "../platform_compat.h"

typedef struct
{
    char name[32];
    uint8_t id;
    uint8_t value;
    uint8_t valid;
} control_mcast_state_t;

static pthread_mutex_t g_button_mcast_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_button_mcast_thread;
static int g_button_mcast_thread_started = 0;
static int g_button_mcast_initialized = 0;
static volatile int g_button_mcast_running = 0;
static uint32_t g_button_mcast_period_ms = 250u;
static uint32_t g_button_mcast_seq = 1u;
static control_mcast_state_t g_button_states[BUTTON_COUNT];
static control_mcast_state_t g_switch_states[SWITCH_COUNT];
static control_mcast_state_t g_knob_states[KNOB_COUNT];

static int update_control_state(control_mcast_state_t *states, size_t state_count, uint8_t id, uint8_t value)
{
    size_t i;

    if (states == NULL) {
        return 0;
    }

    for (i = 0u; i < state_count; ++i) {
        if ((states[i].valid != 0u) && (states[i].id == id)) {
            states[i].value = value;
            return 1;
        }
    }

    return 0;
}

static int append_json_text(char *buf, size_t buf_sz, size_t *pos, const char *text)
{
    int n;

    if ((buf == NULL) || (pos == NULL) || (text == NULL) || (*pos >= buf_sz)) {
        return -1;
    }

    n = snprintf(buf + *pos, buf_sz - *pos, "%s", text);
    if ((n < 0) || ((size_t)n >= (buf_sz - *pos))) {
        return -1;
    }

    *pos += (size_t)n;
    return 0;
}

static int append_json_control_section(char *buf,
                                       size_t buf_sz,
                                       size_t *pos,
                                       const char *section_name,
                                       const control_mcast_state_t *states,
                                       size_t state_count)
{
    size_t i;
    int first = 1;

    if ((buf == NULL) || (pos == NULL) || (section_name == NULL) || (states == NULL)) {
        return -1;
    }

    if (append_json_text(buf, buf_sz, pos, "\"") != 0 ||
        append_json_text(buf, buf_sz, pos, section_name) != 0 ||
        append_json_text(buf, buf_sz, pos, "\":{") != 0) {
        return -1;
    }

    for (i = 0u; i < state_count; ++i) {
        int n;

        if (states[i].valid == 0u) {
            continue;
        }

        n = snprintf(buf + *pos,
                     buf_sz - *pos,
                     "%s\"%s\":{\"id\":%u,\"value\":%u}",
                     (first != 0) ? "" : ",",
                     states[i].name,
                     (unsigned)states[i].id,
                     (unsigned)states[i].value);
        if ((n < 0) || ((size_t)n >= (buf_sz - *pos))) {
            return -1;
        }

        *pos += (size_t)n;
        first = 0;
    }

    return append_json_text(buf, buf_sz, pos, "}");
}

static int build_panel_snapshot_json(char *buf, size_t buf_sz, uint32_t seq)
{
    size_t pos = 0u;
    int n;
    const uint64_t heartbeat_ms = platform_monotonic_ms();
    const unsigned long heartbeat_sec = (unsigned long)(heartbeat_ms / 1000u);
    const unsigned long heartbeat_millis = (unsigned long)(heartbeat_ms % 1000u);

    if ((buf == NULL) || (buf_sz == 0u)) {
        return -1;
    }

    n = snprintf(buf,
                 buf_sz,
                 "{\"type\":\"button_led_snapshot\",\"channel\":\"button_led\",\"seq\":%lu,\"direction\":\"rx\",\"source\":\"uart_button\",\"heartbeat\":%lu.%03lu,",
                 (unsigned long)seq,
                 heartbeat_sec,
                 heartbeat_millis);
    if ((n < 0) || ((size_t)n >= buf_sz)) {
        return -1;
    }

    pos = (size_t)n;

    if (append_json_control_section(buf, buf_sz, &pos, "buttons", g_button_states, BUTTON_COUNT) != 0 ||
        append_json_text(buf, buf_sz, &pos, ",") != 0 ||
        append_json_control_section(buf, buf_sz, &pos, "switches", g_switch_states, SWITCH_COUNT) != 0 ||
        append_json_text(buf, buf_sz, &pos, ",") != 0 ||
        append_json_control_section(buf, buf_sz, &pos, "knobs", g_knob_states, KNOB_COUNT) != 0 ||
        append_json_text(buf, buf_sz, &pos, "}") != 0) {
        return -1;
    }

    return 0;
}

static void *button_mcast_thread_main(void *arg)
{
    (void)arg;

    while (g_button_mcast_running != 0) {
        if (pthread_mutex_lock(&g_button_mcast_lock) == 0) {
            char json[API2_MAX_JSON];

            if (build_panel_snapshot_json(json, sizeof(json), g_button_mcast_seq) == 0) {
                (void)api2_send_json_string(API2_CH_BUTTON_LED, json);
                g_button_mcast_seq++;
            }

            (void)pthread_mutex_unlock(&g_button_mcast_lock);
        }

        platform_sleep_ms(g_button_mcast_period_ms);
    }

    return NULL;
}

int button_mcast_init(const Config *cfg, uint32_t period_ms)
{
    size_t i;

    if (cfg == NULL) {
        return -1;
    }

    if (pthread_mutex_lock(&g_button_mcast_lock) != 0) {
        return -1;
    }

    (void)memset(g_button_states, 0, sizeof(g_button_states));
    (void)memset(g_switch_states, 0, sizeof(g_switch_states));
    (void)memset(g_knob_states, 0, sizeof(g_knob_states));

    for (i = 0u; i < BUTTON_COUNT; ++i) {
        if (cfg->button_ids[i].valid == 0u) {
            continue;
        }

        g_button_states[i].id = cfg->button_ids[i].id;
        g_button_states[i].valid = 1u;
        g_button_states[i].value = 0u;
        (void)strncpy(g_button_states[i].name, cfg->button_ids[i].name, sizeof(g_button_states[i].name) - 1u);
        g_button_states[i].name[sizeof(g_button_states[i].name) - 1u] = '\0';
    }

    for (i = 0u; i < SWITCH_COUNT; ++i) {
        if (cfg->switch_ids[i].valid == 0u) {
            continue;
        }

        g_switch_states[i].id = cfg->switch_ids[i].id;
        g_switch_states[i].valid = 1u;
        g_switch_states[i].value = 0u;
        (void)strncpy(g_switch_states[i].name, cfg->switch_ids[i].name, sizeof(g_switch_states[i].name) - 1u);
        g_switch_states[i].name[sizeof(g_switch_states[i].name) - 1u] = '\0';
    }

    for (i = 0u; i < KNOB_COUNT; ++i) {
        if (cfg->knob_ids[i].valid == 0u) {
            continue;
        }

        g_knob_states[i].id = cfg->knob_ids[i].id;
        g_knob_states[i].valid = 1u;
        g_knob_states[i].value = 0u;
        (void)strncpy(g_knob_states[i].name, cfg->knob_ids[i].name, sizeof(g_knob_states[i].name) - 1u);
        g_knob_states[i].name[sizeof(g_knob_states[i].name) - 1u] = '\0';
    }

    g_button_mcast_period_ms = (period_ms != 0u) ? period_ms : 250u;
    g_button_mcast_seq = 1u;
    g_button_mcast_initialized = 1;

    (void)pthread_mutex_unlock(&g_button_mcast_lock);
    return 0;
}

void button_mcast_shutdown(void)
{
    button_mcast_stop();

    if (pthread_mutex_lock(&g_button_mcast_lock) == 0) {
        (void)memset(g_button_states, 0, sizeof(g_button_states));
        (void)memset(g_switch_states, 0, sizeof(g_switch_states));
        (void)memset(g_knob_states, 0, sizeof(g_knob_states));
        g_button_mcast_initialized = 0;
        g_button_mcast_period_ms = 250u;
        g_button_mcast_seq = 1u;
        (void)pthread_mutex_unlock(&g_button_mcast_lock);
    }
}

int button_mcast_start(void)
{
    if (g_button_mcast_initialized == 0) {
        return -1;
    }
    if (g_button_mcast_thread_started != 0) {
        return 0;
    }

    g_button_mcast_running = 1;
    if (pthread_create(&g_button_mcast_thread, NULL, button_mcast_thread_main, NULL) != 0) {
        g_button_mcast_running = 0;
        return -1;
    }

    g_button_mcast_thread_started = 1;
    return 0;
}

void button_mcast_stop(void)
{
    if (g_button_mcast_thread_started == 0) {
        g_button_mcast_running = 0;
        return;
    }

    g_button_mcast_running = 0;
    (void)pthread_join(g_button_mcast_thread, NULL);
    g_button_mcast_thread_started = 0;
}

void button_mcast_update(uint8_t id, uint8_t value)
{
    if (g_button_mcast_initialized == 0) {
        return;
    }

    if (pthread_mutex_lock(&g_button_mcast_lock) != 0) {
        return;
    }

    if (update_control_state(g_button_states, BUTTON_COUNT, id, value) == 0) {
        if (update_control_state(g_switch_states, SWITCH_COUNT, id, value) == 0) {
            (void)update_control_state(g_knob_states, KNOB_COUNT, id, value);
        }
    }

    (void)pthread_mutex_unlock(&g_button_mcast_lock);
}
