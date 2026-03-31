#ifndef OWL_UART_WIN_H
#define OWL_UART_WIN_H

#include <stdint.h>

typedef struct
{
    int fd;
    uint32_t read_timeout_ms;
} uart_t;

int uart_open(uart_t *uart, const char *device, uint32_t baud, uint32_t read_timeout_ms);
void uart_close(uart_t *uart);
int uart_write(uart_t *uart, const uint8_t *data, uint32_t len);
int uart_read_byte(uart_t *uart, uint8_t *byte_out);

#endif
