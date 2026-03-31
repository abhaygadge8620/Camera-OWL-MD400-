#include "uart_win.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static int uart_lookup_baud(uint32_t baud, speed_t *speed_out)
{
    if (speed_out == NULL) {
        return -1;
    }

    switch (baud) {
    case 9600U:
        *speed_out = B9600;
        return 0;
    case 19200U:
        *speed_out = B19200;
        return 0;
    case 38400U:
        *speed_out = B38400;
        return 0;
    case 57600U:
        *speed_out = B57600;
        return 0;
    case 115200U:
        *speed_out = B115200;
        return 0;
#ifdef B230400
    case 230400U:
        *speed_out = B230400;
        return 0;
#endif
    default:
        return -1;
    }
}

static int uart_configure_fd(int fd, uint32_t baud)
{
    struct termios tio;
    speed_t speed;

    if (uart_lookup_baud(baud, &speed) != 0) {
        return -1;
    }

    if (tcgetattr(fd, &tio) != 0) {
        return -1;
    }

    tio.c_iflag &= (tcflag_t)~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tio.c_oflag &= (tcflag_t)~OPOST;
    tio.c_lflag &= (tcflag_t)~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (cfsetispeed(&tio, speed) != 0) {
        return -1;
    }
    if (cfsetospeed(&tio, speed) != 0) {
        return -1;
    }

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        return -1;
    }

    if (tcflush(fd, TCIOFLUSH) != 0) {
        return -1;
    }

    return 0;
}

int uart_open(uart_t *uart, const char *device, uint32_t baud, uint32_t read_timeout_ms)
{
    int fd;

    if ((uart == NULL) || (device == NULL)) {
        return -1;
    }

    memset(uart, 0, sizeof(*uart));
    uart->fd = -1;
    uart->read_timeout_ms = read_timeout_ms;

    fd = open(device, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        return -3;
    }

    if (uart_configure_fd(fd, baud) != 0) {
        (void)close(fd);
        return -4;
    }

    uart->fd = fd;
    return 0;
}

void uart_close(uart_t *uart)
{
    if ((uart != NULL) && (uart->fd >= 0)) {
        (void)close(uart->fd);
        uart->fd = -1;
    }
}

int uart_write(uart_t *uart, const uint8_t *data, uint32_t len)
{
    size_t total = 0U;

    if ((uart == NULL) || (uart->fd < 0) || (data == NULL) || (len == 0U)) {
        return -1;
    }

    while (total < len) {
        const ssize_t rc = write(uart->fd, data + total, (size_t)(len - total));
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -2;
        }
        if (rc == 0) {
            return -3;
        }
        total += (size_t)rc;
    }

    if (tcdrain(uart->fd) != 0) {
        return -4;
    }

    return 0;
}

int uart_read_byte(uart_t *uart, uint8_t *byte_out)
{
    struct pollfd pfd;
    const int timeout_ms = (int)((uart != NULL) ? uart->read_timeout_ms : 0U);
    ssize_t nread;

    if ((uart == NULL) || (uart->fd < 0) || (byte_out == NULL)) {
        return -1;
    }

    pfd.fd = uart->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    {
        int rc;
        do {
            rc = poll(&pfd, 1, timeout_ms);
        } while ((rc < 0) && (errno == EINTR));

        if (rc < 0) {
            return -2;
        }
        if (rc == 0) {
            return 0;
        }
    }

    nread = read(uart->fd, byte_out, 1U);
    if (nread < 0) {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            return 0;
        }
        return -2;
    }
    if (nread == 0) {
        return 0;
    }

    return 1;
}
