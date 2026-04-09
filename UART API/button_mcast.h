#ifndef BUTTON_MCAST_H
#define BUTTON_MCAST_H

#include <stdint.h>

#include "controls.h"

#ifdef __cplusplus
extern "C" {
#endif

int button_mcast_init(const Config *cfg, uint32_t period_ms);
void button_mcast_shutdown(void);
int button_mcast_start(void);
void button_mcast_stop(void);
void button_mcast_update(uint8_t id, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_MCAST_H */
