#ifndef UART_INI_PARSER_H
#define UART_INI_PARSER_H

#include <stddef.h>

#define UART_INI_MAX_LINE_LENGTH 256
#define UART_INI_MAX_KEY_LENGTH 64

int ini_get_value(const char *filename, const char *section, const char *key,
                  char *value_out, size_t max_len);

#endif /* UART_INI_PARSER_H */
