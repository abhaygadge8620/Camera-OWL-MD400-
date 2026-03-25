#ifndef CONFIG_INI_H
#define CONFIG_INI_H

#include <stdint.h>
#include "controls.h"

int config_load(const char *path, Config *cfg);

const char *get_button_name_by_id(uint8_t id);
const char *get_button_led_name_by_id(uint8_t id);
const char *get_switch_name_by_id(uint8_t id);
const char *get_led_name_by_id(uint8_t id);
const char *get_knob_name_by_id(uint8_t id);

int config_get_led_id(const Config *cfg, const char *name, uint8_t *id_out);
int config_get_button_led_id(const Config *cfg, const char *name, uint8_t *id_out);
int config_get_button_led_id_by_button_name(const Config *cfg, const char *button_name, uint8_t *id_out);

#endif
