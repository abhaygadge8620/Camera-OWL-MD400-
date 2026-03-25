#ifndef CONTROLS_H
#define CONTROLS_H

#include <stdint.h>

enum
{
    BUTTON_COUNT = 16,
    BUTTON_LED_COUNT = 17,
    SWITCH_COUNT = 18,
    LED_COUNT = 22,
    KNOB_COUNT = 2,
    MODE_VALUE_COUNT = 8,
    FREQUENCY_VALUE_COUNT = 8
};

typedef struct
{
    char name[32];
    uint8_t id;
    uint8_t valid;
} id_entry_t;

typedef struct
{
    char uart_device[32];
    uint32_t uart_baud;
    uint32_t uart_read_timeout_ms;

    id_entry_t button_ids[BUTTON_COUNT];
    id_entry_t button_led_ids[BUTTON_LED_COUNT];
    id_entry_t switch_ids[SWITCH_COUNT];
    id_entry_t led_ids[LED_COUNT];
    id_entry_t knob_ids[KNOB_COUNT];

    id_entry_t mode_values[MODE_VALUE_COUNT];
    id_entry_t frequency_values[FREQUENCY_VALUE_COUNT];
} Config;

#endif
