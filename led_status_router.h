#ifndef LED_STATUS_ROUTER_H
#define LED_STATUS_ROUTER_H

#include <stdint.h>

#include "OWL_MD860/camera_iface.h"
#include "UART API/config_ini.h"
#include "UART API/uart_win.h"

typedef struct
{
    const Config *cfg;
    uart_t *uart;

    int day_state;
    int low_light_state;
    int thermal_state;
    int drop_state;
    int sw_lrf_state;
    int lrf_reset_state;
    int optics_reset_state;
} led_status_router_t;

void led_status_router_init(led_status_router_t *ctx, const Config *cfg, uart_t *uart);
int led_status_router_update_from_telem(led_status_router_t *ctx, const owl_telem_frame_t *telem);
int led_status_router_set_led(led_status_router_t *ctx, const char *button_name, uint8_t on);
int led_status_router_pulse_reset_led(led_status_router_t *ctx, const char *button_name);
int led_status_router_clear_reset_led(led_status_router_t *ctx, const char *button_name);

#endif
