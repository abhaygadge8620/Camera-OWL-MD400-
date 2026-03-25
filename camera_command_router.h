#ifndef CAMERA_COMMAND_ROUTER_H
#define CAMERA_COMMAND_ROUTER_H

#include <stdint.h>

#include "led_status_router.h"
#include "OWL_MD860/camera_iface.h"
#include "UART API/config_ini.h"

typedef struct
{
    owl_cam_t *cam;
    const Config *uart_cfg;
    led_status_router_t *led_router;
    int current_lrf_mode;
    int last_frequency_value;
    int selected_view_mode;
} camera_command_router_t;

void camera_command_router_init(camera_command_router_t *ctx, owl_cam_t *cam, const Config *uart_cfg);
void camera_command_router_set_led_router(camera_command_router_t *ctx, led_status_router_t *led_router);
int camera_command_router_handle(camera_command_router_t *ctx, uint8_t id, uint8_t value);

#endif
