#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../OWL_MD860/api2_mcast_win.h"
#include "../platform_compat.h"
#include "button_mcast.h"
#include "config_ini.h"
#include "ini_parser.h"
#include "uart_protocol.h"
#include "uart_win.h"

static volatile sig_atomic_t g_uart_app_running = 1;

static const char *uart_app_find_config_path(void)
{
    static const char *const candidates[] = {
        "config.ini",
        "UART API/config.ini",
        "./UART API/config.ini",
        "../UART API/config.ini"
    };
    size_t i;

    for (i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i) {
        if (access(candidates[i], R_OK) == 0) {
            return candidates[i];
        }
    }

    return "config.ini";
}

typedef struct
{
    const Config *cfg;
    uart_t *uart;
    pthread_t thread;
    volatile int running;
    int started;
    uint8_t led_seen[256];
    uint8_t led_values[256];
} uart_led_rx_ctx_t;

static int uart_app_parse_u16_string(const char *s, uint16_t *out)
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

static int uart_app_parse_bool_string(const char *s, bool *out)
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

static int uart_app_load_api2_settings(const char *path, api2_cfg_t *cfg)
{
    char value[64];

    if ((path == NULL) || (cfg == NULL)) {
        return -1;
    }

    api2_cfg_set_defaults(cfg);
    cfg->camera.enabled = 0u;
    cfg->joystick.enabled = 0u;

    if (ini_get_value(path, "mcast_common", "iface_ip", value, sizeof(value)) == 0) {
        (void)strncpy(cfg->iface_ip, value, sizeof(cfg->iface_ip) - 1u);
        cfg->iface_ip[sizeof(cfg->iface_ip) - 1u] = '\0';
    }
    if (ini_get_value(path, "mcast_common", "ttl", value, sizeof(value)) == 0) {
        uint16_t parsed = 0u;
        if (uart_app_parse_u16_string(value, &parsed) == 0) {
            cfg->ttl = (uint8_t)parsed;
        }
    }
    if (ini_get_value(path, "mcast_common", "loopback", value, sizeof(value)) == 0) {
        bool loopback = false;
        if (uart_app_parse_bool_string(value, &loopback) == 0) {
            cfg->loopback = loopback ? 1u : 0u;
        }
    }
    if (ini_get_value(path, "mcast_button_led", "enabled", value, sizeof(value)) == 0) {
        bool enabled = false;
        if (uart_app_parse_bool_string(value, &enabled) == 0) {
            cfg->button_led.enabled = enabled ? 1u : 0u;
        }
    }
    if (ini_get_value(path, "mcast_button_led", "group_ip", value, sizeof(value)) == 0) {
        (void)strncpy(cfg->button_led.group_ip, value, sizeof(cfg->button_led.group_ip) - 1u);
        cfg->button_led.group_ip[sizeof(cfg->button_led.group_ip) - 1u] = '\0';
    }
    if (ini_get_value(path, "mcast_button_led", "port", value, sizeof(value)) == 0) {
        (void)uart_app_parse_u16_string(value, &cfg->button_led.port);
    }

    return 0;
}

static const char *uart_led_find_key_value(const char *json_str, const char *key)
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

static int uart_led_copy_string_value(const char *json_str, const char *key, char *out, size_t out_sz)
{
    const char *value_str;
    const char *end_quote;
    size_t len;

    if ((json_str == NULL) || (key == NULL) || (out == NULL) || (out_sz < 2u)) {
        return -1;
    }

    value_str = uart_led_find_key_value(json_str, key);
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

static int uart_led_find_named_object(const char *json_str,
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

static int uart_led_parse_u8_in_range(const char *start, const char *end, const char *key, uint8_t *out_value)
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

static int uart_led_resolve_output_id(const Config *cfg, const char *name, uint8_t *id_out)
{
    if ((cfg == NULL) || (name == NULL) || (id_out == NULL)) {
        return -1;
    }

    if (config_get_button_led_id(cfg, name, id_out) == 0) {
        return 0;
    }
    if (config_get_led_id(cfg, name, id_out) == 0) {
        return 0;
    }

    return -1;
}

static int uart_led_apply_value(uart_led_rx_ctx_t *ctx, const char *name, uint8_t value)
{
    uint8_t output_id = 0u;
    uint8_t normalized = (value != 0u) ? 1u : 0u;

    if ((ctx == NULL) || (ctx->cfg == NULL) || (ctx->uart == NULL) || (name == NULL)) {
        return -1;
    }

    if (uart_led_resolve_output_id(ctx->cfg, name, &output_id) != 0) {
        return 0;
    }

    if ((ctx->led_seen[output_id] != 0u) && (ctx->led_values[output_id] == normalized)) {
        return 0;
    }

    ctx->led_seen[output_id] = 1u;
    ctx->led_values[output_id] = normalized;
    return uart_send_control(ctx->uart, output_id, normalized);
}

static int uart_led_parse_single_json(uart_led_rx_ctx_t *ctx, const char *json_str)
{
    char type[32];
    char name[64];
    const char *value_str;
    char *endptr = NULL;
    long parsed;

    if ((ctx == NULL) || (json_str == NULL)) {
        return -1;
    }

    if (uart_led_copy_string_value(json_str, "type", type, sizeof(type)) != 0) {
        return -1;
    }
    if ((strcmp(type, "button_led") != 0) && (strcmp(type, "button_led_state") != 0)) {
        return -1;
    }
    if (uart_led_copy_string_value(json_str, "name", name, sizeof(name)) != 0) {
        return -1;
    }

    value_str = uart_led_find_key_value(json_str, "value");
    if (value_str == NULL) {
        return -1;
    }

    parsed = strtol(value_str, &endptr, 10);
    if (endptr == value_str) {
        return -1;
    }

    return uart_led_apply_value(ctx, name, (parsed != 0L) ? 1u : 0u);
}

static int uart_led_parse_snapshot_section(uart_led_rx_ctx_t *ctx, const char *json_str, const char *section_name)
{
    const char *section_start;
    const char *section_end;
    const char *p;
    int parsed_any = 0;

    if ((ctx == NULL) || (json_str == NULL) || (section_name == NULL)) {
        return -1;
    }

    if (uart_led_find_named_object(json_str, section_name, &section_start, &section_end) != 0) {
        return -1;
    }

    p = section_start + 1;
    while ((p != NULL) && (p < section_end)) {
        const char *name_start;
        const char *name_end;
        const char *entry_obj_start;
        const char *entry_obj_end;
        char name[64];
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

        if (uart_led_parse_u8_in_range(entry_obj_start, entry_obj_end, "value", &value) == 0) {
            (void)uart_led_apply_value(ctx, name, value);
            parsed_any = 1;
        }

        p = entry_obj_end + 1;
    }

    return (parsed_any != 0) ? 0 : -1;
}

static int uart_led_parse_snapshot_json(uart_led_rx_ctx_t *ctx, const char *json_str)
{
    char type[32];
    int parsed = 0;

    if ((ctx == NULL) || (json_str == NULL)) {
        return -1;
    }

    if (uart_led_copy_string_value(json_str, "type", type, sizeof(type)) != 0) {
        return -1;
    }
    if (strcmp(type, "button_led_snapshot") != 0) {
        return -1;
    }

    if (uart_led_parse_snapshot_section(ctx, json_str, "leds") == 0) {
        parsed = 1;
    }

    return (parsed != 0) ? 0 : -1;
}

static void *uart_led_rx_thread_main(void *arg)
{
    uart_led_rx_ctx_t *ctx = (uart_led_rx_ctx_t *)arg;

    while ((ctx != NULL) && (ctx->running != 0)) {
        char json_buf[API2_MAX_JSON];
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

        if (uart_led_parse_snapshot_json(ctx, json_buf) == 0) {
            continue;
        }

        (void)uart_led_parse_single_json(ctx, json_buf);
    }

    return NULL;
}

static int uart_led_rx_start(uart_led_rx_ctx_t *ctx, const Config *cfg, uart_t *uart)
{
    if ((ctx == NULL) || (cfg == NULL) || (uart == NULL)) {
        return -1;
    }

    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->cfg = cfg;
    ctx->uart = uart;
    ctx->running = 1;

    if (pthread_create(&ctx->thread, NULL, uart_led_rx_thread_main, ctx) != 0) {
        ctx->running = 0;
        return -1;
    }

    ctx->started = 1;
    return 0;
}

static void uart_led_rx_stop(uart_led_rx_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->running = 0;
    if (ctx->started != 0) {
        (void)pthread_join(ctx->thread, NULL);
        ctx->started = 0;
    }
}

int main(void)
{
    Config uart_cfg;
    api2_cfg_t api2_cfg;
    uart_t uart;
    uart_led_rx_ctx_t led_rx_ctx;
    uint8_t id = 0u;
    uint8_t value = 0u;
    int api2_open = 0;
    int rc;
    const char *config_path;

    (void)memset(&uart_cfg, 0, sizeof(uart_cfg));
    (void)memset(&api2_cfg, 0, sizeof(api2_cfg));
    (void)memset(&uart, 0, sizeof(uart));
    (void)memset(&led_rx_ctx, 0, sizeof(led_rx_ctx));
    (void)platform_install_signal_handlers(&g_uart_app_running);

    config_path = uart_app_find_config_path();

    rc = config_load(config_path, &uart_cfg);
    if (rc != 0) {
        fprintf(stderr, "[uart_main] config_load(%s) failed rc=%d\n", config_path, rc);
        return 1;
    }

    rc = uart_app_load_api2_settings(config_path, &api2_cfg);
    if (rc != 0) {
        fprintf(stderr, "[uart_main] failed to load multicast settings from %s\n", config_path);
        return 1;
    }

    rc = api2_init(&api2_cfg);
    if (rc != 0) {
        fprintf(stderr, "[uart_main] warning: api2_init failed rc=%d; continuing without multicast bridge\n", rc);
    } else {
        api2_open = 1;
    }

    rc = uart_open(&uart, uart_cfg.uart_device, uart_cfg.uart_baud, uart_cfg.uart_read_timeout_ms);
    if (rc != 0) {
        fprintf(stderr, "[uart_main] uart_open failed device=%s rc=%d\n", uart_cfg.uart_device, rc);
        api2_shutdown();
        return 1;
    }

    rc = button_mcast_init(&uart_cfg, 250u);
    if (rc != 0) {
        fprintf(stderr, "[uart_main] button_mcast_init failed rc=%d\n", rc);
        uart_close(&uart);
        api2_shutdown();
        return 1;
    }

    rc = button_mcast_start();
    if (rc != 0) {
        fprintf(stderr, "[uart_main] button_mcast_start failed rc=%d\n", rc);
        button_mcast_shutdown();
        uart_close(&uart);
        api2_shutdown();
        return 1;
    }

    rc = uart_led_rx_start(&led_rx_ctx, &uart_cfg, &uart);
    if (rc != 0) {
        fprintf(stderr, "[uart_main] uart_led_rx_start failed rc=%d\n", rc);
        button_mcast_shutdown();
        uart_close(&uart);
        api2_shutdown();
        return 1;
    }

    while (g_uart_app_running != 0) {
        rc = uart_read_and_parse(&uart, &id, &value);
        if (rc == 1) {
            button_mcast_update(id, value);
        } else if (rc < 0) {
            platform_sleep_ms(10u);
        }
    }

    uart_led_rx_stop(&led_rx_ctx);
    button_mcast_shutdown();
    uart_close(&uart);
    if (api2_open != 0) {
        api2_shutdown();
    }

    return 0;
}
