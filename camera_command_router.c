// #include "camera_command_router.h"

// #include <stdio.h>
// #include <string.h>

// #include "OWL_MD860/camera_actions.h"

// static int map_frequency_to_enum(uint8_t raw_value, uint8_t *out_freq)
// {
//     if (out_freq == NULL) {
//         return -1;
//     }

//     switch (raw_value) {
//     case 1u:   *out_freq = OWL_LRF_FREQ_1HZ;   return 0;
//     case 4u:   *out_freq = OWL_LRF_FREQ_4HZ;   return 0;
//     case 10u:  *out_freq = OWL_LRF_FREQ_10HZ;  return 0;
//     case 20u:  *out_freq = OWL_LRF_FREQ_20HZ;  return 0;
//     case 100u: *out_freq = OWL_LRF_FREQ_100HZ; return 0;
//     case 200u: *out_freq = OWL_LRF_FREQ_200HZ; return 0;
//     default:
//         return -1;
//     }
// }

// static int8_t decode_mode_value(uint8_t value)
// {
//     return (int8_t)value;
// }

// static int handle_button(camera_command_router_t *ctx, const char *name, uint8_t value)
// {
//     int rc;
//     if (strcmp(name, "DAY") == 0) {
//         if (value == 1u) {
//             int rc_power = camera_action_day_on(ctx->cam);
//             int rc_select = camera_action_select_day(ctx->cam);
//             rc = (rc_power < 0) ? rc_power : rc_select;
//             if (rc_select >= 0) {
//                 ctx->selected_view_mode = (int)OWL_TRACK_CAM_DAY1;
//             }
//             printf("[ROUTER] CMD button DAY value=1 -> day_on rc=%d select_day rc=%d final_rc=%d\n",
//                    rc_power, rc_select, rc);
//             return (rc < 0) ? rc : 1;
//         }
//         if (value == 0u) {
//             rc = camera_action_day_off(ctx->cam);
//             printf("[ROUTER] CMD button DAY value=0 -> camera_action_day_off rc=%d\n", rc);
//             return (rc < 0) ? rc : 1;
//         }
//         printf("[ROUTER] button DAY unsupported value=%u -> no-op\n", (unsigned)value);
//         return 0;
//     }
//     if (strcmp(name, "LOW_LIGHT") == 0) {
//         if (value == 1u) {
//             int rc_power = camera_action_day_on(ctx->cam);
//             int rc_select = camera_action_select_low_light(ctx->cam);
//             rc = (rc_power < 0) ? rc_power : rc_select;
//             if (rc_select >= 0) {
//                 ctx->selected_view_mode = (int)OWL_TRACK_CAM_LOW_LIGHT;
//             }
//             printf("[ROUTER] CMD button LOW_LIGHT value=1 -> day_on rc=%d select_low_light rc=%d final_rc=%d\n",
//                    rc_power, rc_select, rc);
//             return (rc < 0) ? rc : 1;
//         }
//         if (value == 0u) {
//             rc = camera_action_day_off(ctx->cam);
//             printf("[ROUTER] CMD button LOW_LIGHT value=0 -> camera_action_day_off rc=%d\n", rc);
//             return (rc < 0) ? rc : 1;
//         }
//         printf("[ROUTER] button LOW_LIGHT unsupported value=%u -> no-op\n", (unsigned)value);
//         return 0;
//     }
//     if (strcmp(name, "THERMAL") == 0) {
//         if (value == 1u) {
//             int rc_power = camera_action_thermal_on(ctx->cam);
//             int rc_select = camera_action_select_thermal(ctx->cam);
//             rc = (rc_power < 0) ? rc_power : rc_select;
//             if (rc_select >= 0) {
//                 ctx->selected_view_mode = (int)OWL_TRACK_CAM_THERMAL;
//             }
//             printf("[ROUTER] CMD button THERMAL value=1 -> thermal_on rc=%d select_thermal rc=%d final_rc=%d\n",
//                    rc_power, rc_select, rc);
//             return (rc < 0) ? rc : 1;
//         }
//         if (value == 0u) {
//             rc = camera_action_thermal_off(ctx->cam);
//             printf("[ROUTER] CMD button THERMAL value=0 -> camera_action_thermal_off rc=%d\n", rc);
//             return (rc < 0) ? rc : 1;
//         }
//         printf("[ROUTER] button THERMAL unsupported value=%u -> no-op\n", (unsigned)value);
//         return 0;
//     }
//     if (value != 1u) {
//         printf("[ROUTER] button=%s value=%u -> no-op\n", name, (unsigned)value);
//         return 0;
//     }

//     if (strcmp(name, "LRF_RESET") == 0) {
//         rc = camera_action_lrf_reset(ctx->cam);
//         printf("[ROUTER] CMD button LRF_RESET -> camera_action_lrf_reset rc=%d\n", rc);
//         if ((rc >= 0) && (ctx->led_router != NULL)) {
//             (void)led_status_router_pulse_reset_led(ctx->led_router, "LRF_RESET");
//         }
//         return (rc < 0) ? rc : 1;
//     }
//     if (strcmp(name, "OPTICS_RESET") == 0) {
//         int rc_day;
//         int rc_thermal;
//         printf("[ROUTER] OPTICS_RESET start\n");
//         rc_day = camera_action_day_reboot(ctx->cam);
//         printf("[ROUTER] OPTICS_RESET subcmd day reboot rc=%d\n", rc_day);
//         rc_thermal = camera_action_thermal_reboot(ctx->cam);
//         printf("[ROUTER] OPTICS_RESET subcmd thermal reboot rc=%d\n", rc_thermal);
//         rc = (rc_day < 0) ? rc_day : rc_thermal;
//         printf("[ROUTER] OPTICS_RESET done rc=%d\n", rc);
//         if ((rc >= 0) && (ctx->led_router != NULL)) {
//             (void)led_status_router_pulse_reset_led(ctx->led_router, "OPTICS_RESET");
//         }
//         return (rc < 0) ? rc : 1;
//     }

//     printf("[ROUTER] known button=%s has no mapped action\n", name);
//     return 0;
// }

// static int handle_switch(camera_command_router_t *ctx, const char *name, uint8_t value)
// {
//     int rc;

//     if (strcmp(name, "LRF_ON") == 0) {
//         if (value == 1u) {
//             rc = camera_action_lrf_on(ctx->cam);
//             printf("[ROUTER] switch=LRF_ON value=1 -> unsupported by camera ICD rc=%d\n", rc);
//             return (rc < 0) ? rc : OWL_ERR_STATUS;
//         }
//         if (value == 0u) {
//             rc = camera_action_lrf_off(ctx->cam);
//             printf("[ROUTER] CMD switch LRF_ON value=0 -> camera_action_lrf_off rc=%d\n", rc);
//             return (rc < 0) ? rc : 1;
//         }
//         printf("[ROUTER] switch LRF_ON unsupported value=%u -> no-op\n", (unsigned)value);
//         return 0;
//     }

//     if (strcmp(name, "OPTICS_ON") == 0) {
//         if (value == 1u) {
//             int rc_day = camera_action_day_on(ctx->cam);
//             int rc_thermal = camera_action_thermal_on(ctx->cam);
//             printf("[ROUTER] switch OPTICS_ON value=1 subcmd day_on rc=%d\n", rc_day);
//             printf("[ROUTER] switch OPTICS_ON value=1 subcmd thermal_on rc=%d\n", rc_thermal);
//             rc = (rc_day < 0) ? rc_day : rc_thermal;
//             printf("[ROUTER] CMD switch OPTICS_ON value=1 -> optics power on rc=%d\n", rc);
//             return (rc < 0) ? rc : 1;
//         }
//         if (value == 0u) {
//             int rc_day = camera_action_day_off(ctx->cam);
//             int rc_thermal = camera_action_thermal_off(ctx->cam);
//             printf("[ROUTER] switch OPTICS_ON value=0 subcmd day_off rc=%d\n", rc_day);
//             printf("[ROUTER] switch OPTICS_ON value=0 subcmd thermal_off rc=%d\n", rc_thermal);
//             rc = (rc_day < 0) ? rc_day : rc_thermal;
//             printf("[ROUTER] CMD switch OPTICS_ON value=0 -> optics power off rc=%d\n", rc);
//             return (rc < 0) ? rc : 1;
//         }
//         printf("[ROUTER] switch OPTICS_ON unsupported value=%u -> no-op\n", (unsigned)value);
//         return 0;
//     }

//     if (strcmp(name, "MAIN_POWER_ON") == 0) {
//         printf("[ROUTER] switch MAIN_POWER_ON placeholder -> no-op\n");
//         return 0;
//     }

//     printf("[ROUTER] known switch=%s ignored\n", name);
//     return 0;
// }

// static int handle_knob(camera_command_router_t *ctx, const char *name, uint8_t value)
// {
//     if (strcmp(name, "MODE") == 0) {
//         const int8_t mode_value = decode_mode_value(value);
//         if ((mode_value == -2) || (mode_value == -1) || (mode_value == 0)) {
//             ctx->current_lrf_mode = (int)mode_value;
//             printf("[ROUTER] knob MODE raw=%u decoded=%d stored\n",
//                    (unsigned)value, (int)mode_value);
//             return 0;
//         }
//         printf("[ROUTER] knob MODE unknown raw=%u decoded=%d ignored\n",
//                (unsigned)value, (int)mode_value);
//         return 0;
//     }

//     if (strcmp(name, "FREQUENCY") == 0) {
//         uint8_t mapped_freq = 0u;
//         int rc;
//         if (map_frequency_to_enum(value, &mapped_freq) != 0) {
//             printf("[ROUTER] knob FREQUENCY unsupported raw=%u ignored\n", (unsigned)value);
//             return 0;
//         }
//         rc = owl_cam_lrf_set_frequency(ctx->cam, mapped_freq);
//         if (rc < 0) {
//             printf("[ROUTER] CMD knob FREQUENCY raw=%u map=%u -> owl_cam_lrf_set_frequency rc=%d\n",
//                    (unsigned)value, (unsigned)mapped_freq, rc);
//             return rc;
//         }
//         ctx->last_frequency_value = (int)value;
//         printf("[ROUTER] CMD knob FREQUENCY raw=%u map=%u -> owl_cam_lrf_set_frequency rc=%d\n",
//                (unsigned)value, (unsigned)mapped_freq, rc);
//         return 1;
//     }

//     printf("[ROUTER] known knob=%s ignored\n", name);
//     return 0;
// }

// void camera_command_router_init(camera_command_router_t *ctx, owl_cam_t *cam, const Config *uart_cfg)
// {
//     if (ctx == NULL) {
//         return;
//     }
//     ctx->cam = cam;
//     ctx->uart_cfg = uart_cfg;
//     ctx->led_router = NULL;
//     ctx->current_lrf_mode = 0;
//     ctx->last_frequency_value = -1;
//     ctx->selected_view_mode = -1;
// }

// void camera_command_router_set_led_router(camera_command_router_t *ctx, led_status_router_t *led_router)
// {
//     if (ctx == NULL) {
//         return;
//     }
//     ctx->led_router = led_router;
// }

// int camera_command_router_handle(camera_command_router_t *ctx, uint8_t id, uint8_t value)
// {
//     const char *name;

//     if ((ctx == NULL) || (ctx->cam == NULL) || (ctx->uart_cfg == NULL)) {
//         return -1;
//     }

//     printf("[ROUTER] RX id=%u value=%u\n", (unsigned)id, (unsigned)value);

//     name = get_button_name_by_id(id);
//     if (name != NULL) {
//         printf("[ROUTER] resolved BUTTON name=%s\n", name);
//         return handle_button(ctx, name, value);
//     }

//     name = get_switch_name_by_id(id);
//     if (name != NULL) {
//         printf("[ROUTER] resolved SWITCH name=%s\n", name);
//         return handle_switch(ctx, name, value);
//     }

//     name = get_knob_name_by_id(id);
//     if (name != NULL) {
//         printf("[ROUTER] resolved KNOB name=%s\n", name);
//         return handle_knob(ctx, name, value);
//     }

//     printf("[ROUTER] unknown id=%u value=%u ignored\n", (unsigned)id, (unsigned)value);
//     return 0;
// }
