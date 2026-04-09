#ifndef JOYSTICK_RX_H
#define JOYSTICK_RX_H

#include <pthread.h>
#include <stdint.h>

#include "camera_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int joystick_rx_start(owl_cam_t *cam, pthread_mutex_t *cam_lock, const uint8_t *selected_cam_ptr);
void joystick_rx_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* JOYSTICK_RX_H */
