#ifndef BUTTON_LED_RX_H
#define BUTTON_LED_RX_H

#include <pthread.h>

#include "camera_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int button_led_rx_start(owl_cam_t *cam, pthread_mutex_t *cam_lock, uint8_t *selected_cam_ptr);
void button_led_rx_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_LED_RX_H */
