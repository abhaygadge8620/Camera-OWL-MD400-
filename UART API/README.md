# UART API

Linux UART transport component for the OWL WCS button-panel interface.

## Purpose

This folder provides the reusable UART pieces used by the main application:

- `config_ini.c/.h`: panel ID and UART config parsing
- `uart_protocol.c/.h`: 5-byte frame build/decode/parser helpers
- `uart_win.c/.h`: Linux `termios` serial transport retained under the existing filename to avoid breaking includes
- `config.ini`: sample Linux serial configuration
- `sim_uart_mcu_rx.py`, `sim_uart_mcu_tx.py`: Python helpers with Linux serial device defaults

## UART Frame Format

Frames remain unchanged:

1. `START = 0xAA`
2. `ID`
3. `VALUE`
4. `CRC = ID ^ VALUE`
5. `END = 0x55`

## Linux Transport Details

The transport layer now uses:

- `open()` / `close()`
- `termios` for 8N1 serial configuration
- `poll()` for read timeout handling
- `/dev/ttyUSB*`, `/dev/ttyACM*`, `/dev/ttyS*` device paths

Supported baud rates include at least:

- `9600`
- `19200`
- `38400`
- `57600`
- `115200`
- `230400` where supported by the platform headers

## Build

This directory does not contain a standalone `main()` program, so the Makefile builds a static archive instead of an executable:

```bash
cd "UART API"
make clean
make
```

Output:

- `libuart_bridge.a`

## Config Example

```ini
[UART]
device=/dev/ttyUSB0
baud=115200
read_timeout_ms=20
```

## Simulator Examples

Transmit-side simulator example:

```bash
python3 sim_uart_mcu_tx.py --port /dev/ttyUSB0
```

Receive-side simulator example:

```bash
python3 sim_uart_mcu_rx.py --port /dev/ttyUSB0
```

## Notes

- The filename `uart_win.c` / `uart_win.h` was kept to avoid breaking the existing include graph.
- The implementation is now Linux/POSIX, not Win32.
- The root application Makefile compiles these sources directly when building `bin/wcsbtncam`.
