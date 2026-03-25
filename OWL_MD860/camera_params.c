/*
 * @file camera_params.c
 * @brief Implementation of camera parameter validation helpers.
 * @author Ishan
 * @version 1.0
 * @date 2025-09-01
 * Compliant with MISRA C:2012 and ISO C11.
 */

#include "camera_params.h"
#include <string.h>

bool camera_params_validate(camera_params_t *p)
{
    if (p == NULL) {
        return false;
    }

    /* Root topic must be set to OWL (per requirement). Enforce if empty/wrong. */
    if ((p->root_topic[0] == '\0') || (strcmp(p->root_topic, "OWL") != 0)) {
        (void)strncpy(p->root_topic, "OWL", (size_t)CAM_TOPIC_ROOT_MAX_LEN);
        p->root_topic[CAM_TOPIC_ROOT_MAX_LEN - 1U] = '\0';
    }

    /* Sanity ranges (defensive; clamp where feasible) */
    if (p->width == 0U)  { p->width  = 1920U; }
    if (p->height == 0U) { p->height = 1080U; }
    if (p->fps == 0U)    { p->fps    = 25U; }

    if (p->exposure_us == 0U) { p->exposure_us = 10000U; }     /* 10 ms */
    if (p->gain_x10 == 0U)    { p->gain_x10    = 10U; }        /* 1.0x */
    /* white balance defaults */
    if (p->wb_r == 0U) { p->wb_r = 512U; }
    if (p->wb_g == 0U) { p->wb_g = 512U; }
    if (p->wb_b == 0U) { p->wb_b = 512U; }

    if (p->mqtt_port == 0U)   { p->mqtt_port = 1883U; }
    if (p->camera_ip[0] == '\0') {
        (void)strncpy(p->camera_ip, "192.168.8.238", (size_t)CAM_IP_MAX_LEN);
        p->camera_ip[CAM_IP_MAX_LEN - 1U] = '\0';
    }
    if (p->mqtt_client_id[0] == '\0') {
        (void)strncpy(p->mqtt_client_id, "owl-cam-client", (size_t)CAM_NAME_MAX_LEN);
        p->mqtt_client_id[CAM_NAME_MAX_LEN - 1U] = '\0';
    }

    if (p->udp_mcast_group_ip[0] == '\0') {
        (void)strncpy(p->udp_mcast_group_ip, "239.255.10.10", sizeof(p->udp_mcast_group_ip));
        p->udp_mcast_group_ip[sizeof(p->udp_mcast_group_ip) - 1U] = '\0';
    }
    if (p->udp_mcast_port == 0U) {
        p->udp_mcast_port = 5000U;
    }
    if (p->udp_mcast_iface_ip[0] == '\0') {
        (void)strncpy(p->udp_mcast_iface_ip, "0.0.0.0", sizeof(p->udp_mcast_iface_ip));
        p->udp_mcast_iface_ip[sizeof(p->udp_mcast_iface_ip) - 1U] = '\0';
    }
    if (p->udp_mcast_ttl == 0U) {
        p->udp_mcast_ttl = 1U;
    }
    if (p->udp_mcast_loopback > 1U) {
        p->udp_mcast_loopback = 1U;
    }
    if (p->udp_camera_id == 0U) {
        p->udp_camera_id = 1U;
    }

    /* Strings already bounded by fixed arrays. Ensure NUL termination. */
    p->name[CAM_NAME_MAX_LEN - 1U]               = '\0';
    p->model[CAM_MODEL_MAX_LEN - 1U]             = '\0';
    p->serial[CAM_SERIAL_MAX_LEN - 1U]           = '\0';
    p->colorspace[CAM_COLORSPACE_MAX_LEN - 1U]   = '\0';
    p->mqtt_broker[CAM_IP_MAX_LEN - 1U]          = '\0';
    p->camera_ip[CAM_IP_MAX_LEN - 1U]            = '\0';
    p->root_topic[CAM_TOPIC_ROOT_MAX_LEN - 1U]   = '\0';
    p->udp_mcast_group_ip[sizeof(p->udp_mcast_group_ip) - 1U] = '\0';
    p->udp_mcast_iface_ip[sizeof(p->udp_mcast_iface_ip) - 1U] = '\0';

    return true;
}
