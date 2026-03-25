/*
 * @file camera_mqtt.h
 * @brief MQTT publish/subscription helpers for camera parameters (Ethernet only).
 * @author Ishan
 * @version 1.0
 * @date 2025-09-01
 * Compliant with MISRA C:2012 and ISO C11.
 */

#ifndef CAMERA_MQTT_H
#define CAMERA_MQTT_H

#include <stdint.h>
#include <stdbool.h>
#include "camera_params.h"

/**
 * @brief Initialize and connect MQTT (Ethernet only).
 * Uses existing mqtt.c/h functions; no dynamic allocation here.
 * @param[in] p Camera params containing broker, port, client id.
 * @return bool true on success.
 */
bool camera_mqtt_connect(const camera_params_t *p);

/**
 * @brief Publish the full camera parameter set under OWL/CAMERA/<leaf>
 * Uses your helper functions for topic consistency.
 * @param[in] p Camera parameters (validated).
 */
void camera_mqtt_publish_all(const camera_params_t *p);

/**
 * @brief Poll the MQTT client (yield) and process incoming config commands.
 * Expected command topics:
 *   OWL/CMD/GET            payload: "ALL" or leaf key (e.g., WIDTH)
 *   OWL/CMD/SET/<KEY>      payload: value (ASCII)
 * Responds on:
 *   OWL/RESP/<KEY>         payload: current value
 * @param[in,out] p Params to update if SET arrives (applied + republished).
 */
void camera_mqtt_poll_and_process(camera_params_t *p);

void camera_mqtt_set_udp_mcast(bool enabled, uint8_t camera_id, uint32_t *seq_counter);

#endif /* CAMERA_MQTT_H */
