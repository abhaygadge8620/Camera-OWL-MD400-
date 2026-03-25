#define _CRT_SECURE_NO_WARNINGS

#include "uart_win.h"

#include <stdio.h>
#include <string.h>

static int build_device_path(const char *device, char *out_path, uint32_t out_size)
{
    if ((device == NULL) || (out_path == NULL) || (out_size < 8U))
    {
        return -1;
    }

    if (strncmp(device, "\\\\.\\", 4U) == 0)
    {
        strncpy(out_path, device, out_size - 1U);
        out_path[out_size - 1U] = '\0';
    }
    else
    {
        _snprintf(out_path, out_size, "\\\\.\\%s", device);
        out_path[out_size - 1U] = '\0';
    }
    return 0;
}

int uart_open(uart_t *uart, const char *device, uint32_t baud, uint32_t read_timeout_ms)
{
    char path[64];
    DCB dcb;
    COMMTIMEOUTS timeouts;

    if ((uart == NULL) || (device == NULL))
    {
        return -1;
    }

    memset(uart, 0, sizeof(*uart));
    uart->handle = INVALID_HANDLE_VALUE;
    uart->read_timeout_ms = read_timeout_ms;

    if (build_device_path(device, path, sizeof(path)) != 0)
    {
        return -2;
    }

    uart->handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (uart->handle == INVALID_HANDLE_VALUE)
    {
        return -3;
    }

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(uart->handle, &dcb))
    {
        uart_close(uart);
        return -4;
    }

    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    if (!SetCommState(uart->handle, &dcb))
    {
        uart_close(uart);
        return -5;
    }

    memset(&timeouts, 0, sizeof(timeouts));
    if (read_timeout_ms == 0U)
    {
        /* Immediate return: consume whatever bytes are already buffered. */
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = 0U;
        timeouts.ReadTotalTimeoutMultiplier = 0U;
    }
    else
    {
        timeouts.ReadIntervalTimeout = read_timeout_ms;
        timeouts.ReadTotalTimeoutConstant = read_timeout_ms;
        timeouts.ReadTotalTimeoutMultiplier = 0U;
    }
    timeouts.WriteTotalTimeoutConstant = 50U;
    timeouts.WriteTotalTimeoutMultiplier = 1U;
    if (!SetCommTimeouts(uart->handle, &timeouts))
    {
        uart_close(uart);
        return -6;
    }

    (void)SetupComm(uart->handle, 4096U, 4096U);
    (void)PurgeComm(uart->handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return 0;
}

void uart_close(uart_t *uart)
{
    if ((uart != NULL) && (uart->handle != INVALID_HANDLE_VALUE))
    {
        CloseHandle(uart->handle);
        uart->handle = INVALID_HANDLE_VALUE;
    }
}

int uart_write(uart_t *uart, const uint8_t *data, uint32_t len)
{
    DWORD written = 0U;
    if ((uart == NULL) || (uart->handle == INVALID_HANDLE_VALUE) || (data == NULL) || (len == 0U))
    {
        return -1;
    }

    if (!WriteFile(uart->handle, data, (DWORD)len, &written, NULL))
    {
        return -2;
    }
    if (written != (DWORD)len)
    {
        return -3;
    }
    return 0;
}

int uart_read_byte(uart_t *uart, uint8_t *byte_out)
{
    DWORD nread = 0U;
    if ((uart == NULL) || (uart->handle == INVALID_HANDLE_VALUE) || (byte_out == NULL))
    {
        return -1;
    }

    if (!ReadFile(uart->handle, byte_out, 1U, &nread, NULL))
    {
        return -2;
    }
    if (nread == 0U)
    {
        return 0;
    }
    return 1;
}
