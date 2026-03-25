#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>
#include "uart_win.h"

enum
{
    UART_FRAME_LEN = 5,
    UART_START_BYTE = 0xAA,
    UART_END_BYTE = 0x55
};

typedef enum
{
    UART_PARSE_WAIT_START = 0,
    UART_PARSE_READ_ID,
    UART_PARSE_READ_VALUE,
    UART_PARSE_READ_CRC,
    UART_PARSE_READ_END
} uart_parse_state_t;

typedef struct
{
    uart_parse_state_t state;
    uint8_t id;
    uint8_t value;
    uint8_t crc;
} uart_stream_parser_t;

uint8_t uart_calc_crc(uint8_t id, uint8_t value);
void uart_build_frame(uint8_t id, uint8_t value, uint8_t frame[UART_FRAME_LEN]);
int uart_decode_frame(const uint8_t frame[UART_FRAME_LEN], uint8_t *id, uint8_t *value);

void uart_stream_parser_init(uart_stream_parser_t *parser);
int uart_stream_parser_feed(uart_stream_parser_t *parser, uint8_t byte, uint8_t *id, uint8_t *value, int *error_code);

int uart_send_control(uart_t *uart, uint8_t id, uint8_t value);
int uart_read_and_parse(uart_t *uart, uint8_t *id, uint8_t *value);
int uart_get_last_parse_error(void);

#endif
