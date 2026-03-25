// #include "camera_actions.h"

// #include <stddef.h>
// #include <stdint.h>
// #include <stdio.h>

// static int read_power_state(owl_cam_t *cam, uint16_t addr, uint8_t *state_out)
// {
//     uint8_t payload[4] = {0u};
//     size_t payload_len = sizeof(payload);
//     int rc;

//     if ((cam == NULL) || (state_out == NULL)) {
//         return OWL_ERR_ARG;
//     }

//     rc = owl_cam_read(cam, OWL_CTRL_IFACE, addr, payload, &payload_len);
//     if ((rc != OWL_OK) || (payload_len == 0u)) {
//         return (rc != OWL_OK) ? rc : OWL_ERR_IO;
//     }

//     *state_out = payload[0];
//     return OWL_OK;
// }

// int camera_action_day_on(owl_cam_t *cam)
// {
//     uint8_t state = 0u;
//     int rc;
//     if (cam == NULL) { return OWL_ERR_ARG; }

//     rc = read_power_state(cam, OWL_ADDR_DAY_POWER, &state);
//     if ((rc == OWL_OK) && (state == OWL_POWER_ON)) {
//         fprintf(stderr, "[CAM_ACTION] DAY power already ON, skipping command\n");
//         return OWL_OK;
//     }

//     return owl_cam_day_power(cam, OWL_POWER_ON);
// }

// int camera_action_day_off(owl_cam_t *cam)
// {
//     uint8_t state = 0u;
//     int rc;
//     if (cam == NULL) { return OWL_ERR_ARG; }

//     rc = read_power_state(cam, OWL_ADDR_DAY_POWER, &state);
//     if ((rc == OWL_OK) && (state == OWL_POWER_OFF)) {
//         fprintf(stderr, "[CAM_ACTION] DAY power already OFF, skipping command\n");
//         return OWL_OK;
//     }

//     return owl_cam_day_power(cam, OWL_POWER_OFF);
// }
// int camera_action_day_reboot(owl_cam_t *cam) { return owl_cam_day_power(cam, OWL_POWER_REBOOT); }
// int camera_action_select_day(owl_cam_t *cam)
// {
//     int rc;
//     if (cam == NULL) {
//         return OWL_ERR_ARG;
//     }
//     rc = owl_cam_select_day(cam);
//     fprintf(stderr,
//             "[CAM_ACTION] select day mode via TrkCamSwitch iface=0x%02X addr=0x%04X cam_id=0x%02X rc=%d\n",
//             (unsigned)OWL_IFACE_TRACKER,
//             (unsigned)OWL_ADDR_TRACKER_CAM_SWITCH,
//             (unsigned)OWL_TRACK_CAM_DAY1,
//             rc);
//     return rc;
// }

// int camera_action_select_low_light(owl_cam_t *cam)
// {
//     int rc;
//     if (cam == NULL) {
//         return OWL_ERR_ARG;
//     }
//     rc = owl_cam_select_low_light(cam);
//     fprintf(stderr,
//             "[CAM_ACTION] select low-light mode via TrkCamSwitch iface=0x%02X addr=0x%04X cam_id=0x%02X rc=%d\n",
//             (unsigned)OWL_IFACE_TRACKER,
//             (unsigned)OWL_ADDR_TRACKER_CAM_SWITCH,
//             (unsigned)OWL_TRACK_CAM_LOW_LIGHT,
//             rc);
//     return rc;
// }

// int camera_action_thermal_on(owl_cam_t *cam)
// {
//     uint8_t state = 0u;
//     int rc;
//     if (cam == NULL) { return OWL_ERR_ARG; }

//     rc = read_power_state(cam, OWL_ADDR_THERMAL_POWER, &state);
//     if ((rc == OWL_OK) && (state == OWL_POWER_ON)) {
//         fprintf(stderr, "[CAM_ACTION] THERMAL power already ON, skipping command\n");
//         return OWL_OK;
//     }

//     return owl_cam_thermal_power(cam, OWL_POWER_ON);
// }

// int camera_action_thermal_off(owl_cam_t *cam)
// {
//     uint8_t state = 0u;
//     int rc;
//     if (cam == NULL) { return OWL_ERR_ARG; }

//     rc = read_power_state(cam, OWL_ADDR_THERMAL_POWER, &state);
//     if ((rc == OWL_OK) && (state == OWL_POWER_OFF)) {
//         fprintf(stderr, "[CAM_ACTION] THERMAL power already OFF, skipping command\n");
//         return OWL_OK;
//     }

//     return owl_cam_thermal_power(cam, OWL_POWER_OFF);
// }
// int camera_action_thermal_reboot(owl_cam_t *cam) { return owl_cam_thermal_power(cam, OWL_POWER_REBOOT); }
// int camera_action_select_thermal(owl_cam_t *cam)
// {
//     int rc;
//     if (cam == NULL) {
//         return OWL_ERR_ARG;
//     }
//     rc = owl_cam_select_thermal(cam);
//     fprintf(stderr,
//             "[CAM_ACTION] select thermal mode via TrkCamSwitch iface=0x%02X addr=0x%04X cam_id=0x%02X rc=%d\n",
//             (unsigned)OWL_IFACE_TRACKER,
//             (unsigned)OWL_ADDR_TRACKER_CAM_SWITCH,
//             (unsigned)OWL_TRACK_CAM_THERMAL,
//             rc);
//     return rc;
// }

// int camera_action_lrf_on(owl_cam_t *cam)
// {
//     (void)cam;
//     /* ICD control mapping for 0x001C supports OFF(0x02) and REBOOT(0x03), not ON(0x01). */
//     fprintf(stderr, "[CAM_ACTION] LRF ON is not supported by ICD at addr 0x001C\n");
//     return OWL_ERR_ARG;
// }

// int camera_action_lrf_off(owl_cam_t *cam)
// {
//     return owl_cam_lrf_power(cam, OWL_POWER_OFF);
// }

// int camera_action_lrf_reset(owl_cam_t *cam)
// {
//     return owl_cam_lrf_power(cam, OWL_POWER_REBOOT);
// }

// int camera_action_optics_reset(owl_cam_t *cam)
// {
//     int rc_day;
//     int rc_thermal;
//     if (cam == NULL) {
//         return OWL_ERR_ARG;
//     }

//     rc_day = camera_action_day_reboot(cam);
//     rc_thermal = camera_action_thermal_reboot(cam);

//     if (rc_day != OWL_OK) {
//         return rc_day;
//     }
//     return rc_thermal;
// }

// static int diag_read_one(owl_cam_t *cam, const char *label, uint16_t addr)
// {
//     uint8_t payload[8] = {0u};
//     size_t payload_len = sizeof(payload);
//     const int rc = owl_cam_read(cam, OWL_CTRL_IFACE, addr, payload, &payload_len);

//     if (rc == OWL_OK) {
//         fprintf(stderr,
//                 "[DIAG] read %-5s addr=0x%04X rc=%d len=%u value=0x%02X\n",
//                 label,
//                 (unsigned)addr,
//                 rc,
//                 (unsigned)payload_len,
//                 (unsigned)((payload_len > 0u) ? payload[0] : 0u));
//     } else {
//         fprintf(stderr,
//                 "[DIAG] read %-5s addr=0x%04X rc=%d\n",
//                 label,
//                 (unsigned)addr,
//                 rc);
//     }

//     return rc;
// }

// int camera_diag_read_power_controls(owl_cam_t *cam)
// {
//     int rc = OWL_OK;
//     int step_rc;

//     if (cam == NULL) {
//         return OWL_ERR_ARG;
//     }

//     fprintf(stderr, "[DIAG] read power-control registers start\n");

//     step_rc = diag_read_one(cam, "DAY", OWL_ADDR_DAY_POWER);
//     if (step_rc != OWL_OK) {
//         rc = step_rc;
//     }

//     step_rc = diag_read_one(cam, "THERM", OWL_ADDR_THERMAL_POWER);
//     if ((step_rc != OWL_OK) && (rc == OWL_OK)) {
//         rc = step_rc;
//     }

// #if (OWL_HAS_CTRL_LRF_POWER != 0)
//     step_rc = diag_read_one(cam, "LRF", OWL_ADDR_LRF_POWER);
//     if ((step_rc != OWL_OK) && (rc == OWL_OK)) {
//         rc = step_rc;
//     }
// #else
//     fprintf(stderr, "[DIAG] read LRF   skipped: control register not defined in active ICD profile\n");
// #endif

//     fprintf(stderr, "[DIAG] read power-control registers done rc=%d\n", rc);
//     return rc;
// }

// int camera_diag_reboot_day(owl_cam_t *cam)
// {
//     int rc;
//     fprintf(stderr, "[DIAG] reboot DAY start\n");
//     rc = owl_cam_day_power(cam, OWL_POWER_REBOOT);
//     fprintf(stderr, "[DIAG] reboot DAY done rc=%d\n", rc);
//     return rc;
// }

// int camera_diag_reboot_thermal(owl_cam_t *cam)
// {
//     int rc;
//     fprintf(stderr, "[DIAG] reboot THERMAL start\n");
//     rc = owl_cam_thermal_power(cam, OWL_POWER_REBOOT);
//     fprintf(stderr, "[DIAG] reboot THERMAL done rc=%d\n", rc);
//     return rc;
// }

// int camera_diag_reboot_lrf(owl_cam_t *cam)
// {
//     int rc;
//     fprintf(stderr, "[DIAG] reboot LRF start\n");
//     rc = owl_cam_lrf_power(cam, OWL_POWER_REBOOT);
//     fprintf(stderr, "[DIAG] reboot LRF done rc=%d\n", rc);
//     return rc;
// }
