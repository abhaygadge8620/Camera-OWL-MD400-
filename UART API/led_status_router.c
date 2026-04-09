#include "led_status_router.h"

#include <stdio.h>
#include <string.h>

#include "uart_protocol.h"

static int resolve_button_led_id(const led_status_router_t *ctx, const char *button_name, uint8_t *led_id_out)
{
    if ((ctx == NULL) || (ctx->cfg == NULL) || (button_name == NULL) || (led_id_out == NULL)) {
        return -1;
    }
    if (strcmp(button_name, "SW_LRF") == 0) {
        return config_get_button_led_id(ctx->cfg, "SW_LRF_LED", led_id_out);
    }
    return config_get_button_led_id_by_button_name(ctx->cfg, button_name, led_id_out);
}

static int *state_slot_for_button(led_status_router_t *ctx, const char *button_name)
{
    if ((ctx == NULL) || (button_name == NULL)) {
        return NULL;
    }
    if (strcmp(button_name, "DAY") == 0) {
        return &ctx->day_state;
    }
    if (strcmp(button_name, "LOW_LIGHT") == 0) {
        return &ctx->low_light_state;
    }
    if (strcmp(button_name, "THERMAL") == 0) {
        return &ctx->thermal_state;
    }
    if (strcmp(button_name, "DROP") == 0) {
        return &ctx->drop_state;
    }
    if (strcmp(button_name, "SW_LRF") == 0) {
        return &ctx->sw_lrf_state;
    }
    if (strcmp(button_name, "LRF_RESET") == 0) {
        return &ctx->lrf_reset_state;
    }
    if (strcmp(button_name, "OPTICS_RESET") == 0) {
        return &ctx->optics_reset_state;
    }
    return NULL;
}

static int send_led_value(led_status_router_t *ctx, const char *button_name, uint8_t value)
{
    uint8_t led_id = 0u;
    int rc;

    if ((ctx == NULL) || (ctx->uart == NULL)) {
        return -1;
    }

    rc = resolve_button_led_id(ctx, button_name, &led_id);
    if (rc != 0) {
        printf("[LED] unresolved mapping button=%s rc=%d\n", button_name, rc);
        return -2;
    }

    rc = uart_send_control(ctx->uart, led_id, value);
    printf("[LED] TX button=%s led_id=%u value=%u rc=%d\n",
           button_name, (unsigned)led_id, (unsigned)value, rc);
    if (rc != 0) {
        return -3;
    }
    return 0;
}

void led_status_router_init(led_status_router_t *ctx, const Config *cfg, uart_t *uart)
{
    if (ctx == NULL) {
        return;
    }
    ctx->cfg = cfg;
    ctx->uart = uart;
    ctx->day_state = -1;
    ctx->low_light_state = -1;
    ctx->thermal_state = -1;
    ctx->drop_state = -1;
    ctx->sw_lrf_state = -1;
    ctx->lrf_reset_state = 0;
    ctx->optics_reset_state = 0;
}

int led_status_router_set_led(led_status_router_t *ctx, const char *button_name, uint8_t on)
{
    int *slot;
    int desired;
    int rc;

    if ((ctx == NULL) || (button_name == NULL)) {
        return -1;
    }

    slot = state_slot_for_button(ctx, button_name);
    if (slot == NULL) {
        printf("[LED] unknown button name for set_led: %s\n", button_name);
        return -2;
    }

    desired = (on != 0u) ? 1 : 0;
    if (*slot == desired) {
        return 0;
    }

    rc = send_led_value(ctx, button_name, (uint8_t)desired);
    if (rc != 0) {
        return rc;
    }

    *slot = desired;
    return 1;
}

int led_status_router_pulse_reset_led(led_status_router_t *ctx, const char *button_name)
{
    if ((button_name == NULL) ||
        ((strcmp(button_name, "LRF_RESET") != 0) && (strcmp(button_name, "OPTICS_RESET") != 0))) {
        printf("[LED] pulse_reset unsupported button=%s\n", (button_name != NULL) ? button_name : "(null)");
        return -1;
    }
    printf("[LED] pulse_reset button=%s -> ON\n", button_name);
    return led_status_router_set_led(ctx, button_name, 1u);
}

int led_status_router_clear_reset_led(led_status_router_t *ctx, const char *button_name)
{
    if ((button_name == NULL) ||
        ((strcmp(button_name, "LRF_RESET") != 0) && (strcmp(button_name, "OPTICS_RESET") != 0))) {
        printf("[LED] clear_reset unsupported button=%s\n", (button_name != NULL) ? button_name : "(null)");
        return -1;
    }
    printf("[LED] clear_reset button=%s -> OFF\n", button_name);
    return led_status_router_set_led(ctx, button_name, 0u);
}

int led_status_router_update_from_telem(led_status_router_t *ctx, const owl_telem_frame_t *telem)
{
    int rc_lrf;
    const int lrf_target = (telem != NULL && telem->pwr_lrf_on != 0u) ? 1 : 0;

    if ((ctx == NULL) || (telem == NULL)) {
        return -1;
    }

    if (ctx->sw_lrf_state != lrf_target) {
        printf("[LED] telem change lrf=%u overall=%u\n",
               (unsigned)telem->pwr_lrf_on,
               (unsigned)telem->pwr_overall);
    }

    rc_lrf = led_status_router_set_led(ctx, "SW_LRF", (telem->pwr_lrf_on != 0u) ? 1u : 0u);

    if (rc_lrf < 0) {
        printf("[LED] update_from_telem error lrf_rc=%d\n", rc_lrf);
        return -2;
    }

    if (rc_lrf > 0) {
        return 1;
    }
    return 0;
}
