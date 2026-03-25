/*
 * @file camera_params.h
 * @brief Camera parameter structures and validation helpers.
 * @author Ishan Anant Karve
 * @version 1.0
 * @date 2025-09-01
 *
 * Defines strongly-typed camera configuration and validation utilities.
 * Compliant with MISRA C:2012 and ISO C11.
 * Copyright (c) 2025
 */

#ifndef CAMERA_PARAMS_H
#define CAMERA_PARAMS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CAM_NAME_MAX_LEN          (32U)
#define CAM_MODEL_MAX_LEN         (32U)
#define CAM_SERIAL_MAX_LEN        (32U)
#define CAM_COLORSPACE_MAX_LEN    (16U)
#define CAM_IP_MAX_LEN            (64U)
#define CAM_TOPIC_ROOT_MAX_LEN    (32U)

/* All sizes are unsigned. Use fixed-width integers for portability. */
typedef struct
{
    /* Identity */
    char      name[CAM_NAME_MAX_LEN];
    char      model[CAM_MODEL_MAX_LEN];
    char      serial[CAM_SERIAL_MAX_LEN];
    char      camera_ip[CAM_IP_MAX_LEN];

    /* Video timing/meta (telemetry only; no frame handling here) */
    uint16_t  width;          /* pixels */
    uint16_t  height;         /* pixels */
    uint16_t  fps;            /* frames per second */

    /* Imaging controls */
    uint32_t  exposure_us;    /* microseconds */
    uint16_t  gain_x10;       /* gain * 10 to avoid float (e.g., 13 => 1.3x) */
    uint16_t  wb_r;           /* white-balance red channel */
    uint16_t  wb_g;           /* white-balance green channel */
    uint16_t  wb_b;           /* white-balance blue channel */
    char      colorspace[CAM_COLORSPACE_MAX_LEN]; /* e.g., "RAW12", "YUV", "RGB" */

    /* Network/MQTT (Ethernet only) */
    char      mqtt_broker[CAM_IP_MAX_LEN];   /* host or ip */
    uint16_t  mqtt_port;                      /* typically 1883 */
    char      mqtt_client_id[CAM_NAME_MAX_LEN];
    char      root_topic[CAM_TOPIC_ROOT_MAX_LEN]; /* must be "OWL" per requirement */
    bool      mqtt_retained;                  /* retain publishes? */

    /* UDP multicast mirror transport (Linux-only API2) */
    bool      udp_mcast_enabled;
    char      udp_mcast_group_ip[32];
    uint16_t  udp_mcast_port;
    char      udp_mcast_iface_ip[32];
    uint8_t   udp_mcast_ttl;
    uint8_t   udp_mcast_loopback;
    uint8_t   udp_camera_id;

} camera_params_t;

/**
 * @brief Clamp/validate camera params to safe ranges.
 * @param[in,out] p Pointer to params; fields may be clamped.
 * @return bool true if structurally valid, false if unusable.
 */
bool camera_params_validate(camera_params_t *p);

#endif /* CAMERA_PARAMS_H */
