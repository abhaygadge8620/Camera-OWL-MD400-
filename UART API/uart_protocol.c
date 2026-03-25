#include "uart_protocol.h"

#include <string.h>

static uart_stream_parser_t g_parser;
static int g_parser_initialized = 0;
static int g_last_parse_error = 0;

uint8_t uart_calc_crc(uint8_t id, uint8_t value)
{
    return (uint8_t)(id ^ value);
}

void uart_build_frame(uint8_t id, uint8_t value, uint8_t frame[UART_FRAME_LEN])
{
    frame[0] = UART_START_BYTE;
    frame[1] = id;
    frame[2] = value;
    frame[3] = uart_calc_crc(id, value);
    frame[4] = UART_END_BYTE;
}

int uart_decode_frame(const uint8_t frame[UART_FRAME_LEN], uint8_t *id, uint8_t *value)
{
    if ((frame == NULL) || (id == NULL) || (value == NULL))
    {
        return -10;
    }
    if (frame[0] != UART_START_BYTE)
    {
        return -1;
    }
    if (frame[4] != UART_END_BYTE)
    {
        return -2;
    }
    if (frame[3] != uart_calc_crc(frame[1], frame[2]))
    {
        return -3;
    }
    *id = frame[1];
    *value = frame[2];
    return 0;
}

void uart_stream_parser_init(uart_stream_parser_t *parser)
{
    if (parser != NULL)
    {
        memset(parser, 0, sizeof(*parser));
        parser->state = UART_PARSE_WAIT_START;
    }
}

int uart_stream_parser_feed(uart_stream_parser_t *parser, uint8_t byte, uint8_t *id, uint8_t *value, int *error_code)
{
    if (parser == NULL)
    {
        return -1;
    }

    if (error_code != NULL)
    {
        *error_code = 0;
    }

    switch (parser->state)
    {
    case UART_PARSE_WAIT_START:
        if (byte == UART_START_BYTE)
        {
            parser->state = UART_PARSE_READ_ID;
        }
        break;
    case UART_PARSE_READ_ID:
        parser->id = byte;
        parser->state = UART_PARSE_READ_VALUE;
        break;
    case UART_PARSE_READ_VALUE:
        parser->value = byte;
        parser->state = UART_PARSE_READ_CRC;
        break;
    case UART_PARSE_READ_CRC:
        parser->crc = byte;
        parser->state = UART_PARSE_READ_END;
        break;
    case UART_PARSE_READ_END:
        parser->state = UART_PARSE_WAIT_START;
        if (byte != UART_END_BYTE)
        {
            if (error_code != NULL) *error_code = -2;
            return -1;
        }
        if (parser->crc != uart_calc_crc(parser->id, parser->value))
        {
            if (error_code != NULL) *error_code = -3;
            return -1;
        }
        if (id != NULL) *id = parser->id;
        if (value != NULL) *value = parser->value;
        return 1;
    default:
        parser->state = UART_PARSE_WAIT_START;
        if (error_code != NULL) *error_code = -99;
        return -1;
    }

    return 0;
}

int uart_send_control(uart_t *uart, uint8_t id, uint8_t value)
{
    uint8_t frame[UART_FRAME_LEN];
    uart_build_frame(id, value, frame);
    return uart_write(uart, frame, UART_FRAME_LEN);
}

int uart_read_and_parse(uart_t *uart, uint8_t *id, uint8_t *value)
{
    uint8_t b = 0U;
    int rc;
    int parse_rc;
    int parse_error = 0;
    unsigned int bytes_processed = 0U;

    enum { UART_MAX_BYTES_PER_CALL = 64 };

    if (g_parser_initialized == 0)
    {
        uart_stream_parser_init(&g_parser);
        g_parser_initialized = 1;
    }

    while (bytes_processed < UART_MAX_BYTES_PER_CALL)
    {
        rc = uart_read_byte(uart, &b);
        if (rc <= 0)
        {
            if (bytes_processed == 0U)
            {
                return rc;
            }
            return 0;
        }

        bytes_processed++;
        parse_rc = uart_stream_parser_feed(&g_parser, b, id, value, &parse_error);
        if (parse_rc == -1)
        {
            g_last_parse_error = parse_error;
        }
        if (parse_rc != 0)
        {
            return parse_rc;
        }
    }

    return 0;
}

int uart_get_last_parse_error(void)
{
    return g_last_parse_error;
}
