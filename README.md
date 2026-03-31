# Camera-OWL-MD400-

Linux C11 application that bridges a UART-based WCS button panel to an OWL-MD400 / OWL-MD860 camera over Ethernet, publishes telemetry over MQTT and optional UDP multicast, keeps panel LEDs synchronized, and launches VLC for RTSP viewing.

## Overview

The Linux port preserves the existing project architecture:

- `main.c` remains the application entry point.
- `OWL_MD860/` still contains the camera protocol, telemetry, tracker, multicast, MQTT, CRC, and INI logic.
- `UART API/` still contains the UART transport, frame protocol, and panel ID/config parsing.
- `led_status_router.*` and `camera_command_router.*` remain separate routing layers.

The Windows-only transport code has been replaced with Linux/POSIX implementations:

- `Sleep()` -> monotonic `nanosleep()` wrapper
- Win32 console handler -> POSIX signal handling
- Winsock -> POSIX sockets/select/close/errno
- Win32 COM port access -> `termios` serial access on `/dev/ttyUSB*`, `/dev/ttyACM*`, `/dev/ttyS*`

## Folder Structure

- `main.c`: main runtime loop and command handling
- `platform_compat.c`, `platform_compat.h`: minimal timing/signal/string portability helpers
- `led_status_router.c`, `led_status_router.h`: LED state routing back to UART panel
- `camera_command_router.c`, `camera_command_router.h`: camera command routing helpers
- `Makefile`: Linux build for the full application
- `README.md`: Linux setup/build/run guide
- `OWL_MD860/`: camera Ethernet control, telemetry, multicast, MQTT, tracker, parsing helpers
- `UART API/`: UART transport, UART protocol, UART config parsing, sample simulator scripts, component Makefile
- `MD400_ICD_23_12_2025.pdf`: protocol reference document

## Linux Dependencies

### Ubuntu / Debian

Install the required packages:

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libpaho-mqtt-dev vlc python3
```

Notes:

- `build-essential` provides `gcc`, `make`, and standard development tools.
- `pkg-config` is used by the Makefile when `paho-mqtt3c.pc` is available.
- `libpaho-mqtt-dev` provides `MQTTClient.h` and `libpaho-mqtt3c`.
- `vlc` is optional if you do not need local RTSP viewing, but the stream-switch feature expects it.

### Fedora / RHEL / Rocky / AlmaLinux

```bash
sudo dnf install -y gcc make pkgconf-pkg-config paho-c-devel vlc python3
```

### Arch Linux

```bash
sudo pacman -S --needed base-devel pkgconf paho-mqtt-c vlc python
```

If your distro does not package Eclipse Paho C, install it from source and make sure `MQTTClient.h` and `libpaho-mqtt3c` are visible to the compiler/linker.

## Build

From the project root:

```bash
cd Camera-OWL-MD400-
make clean
make
```

Expected output:

- `bin/wcsbtncam`

The component Makefile under `UART API/` now builds a reusable static archive because that folder does not contain a standalone `main()` entry point:

```bash
cd "UART API"
make clean
make
```

Expected output:

- `UART API/libuart_bridge.a`

## Run

From the project root:

```bash
cd Camera-OWL-MD400-
./bin/wcsbtncam
```

The application loads:

- `UART API/config.ini`
- `OWL_MD860/config.ini`

so run it from the project root unless you change the config paths in code.

## Serial Device Setup

### Find the serial device name

Common Linux serial device names:

- `/dev/ttyUSB0`
- `/dev/ttyUSB1`
- `/dev/ttyACM0`
- `/dev/ttyS0`

Useful commands:

```bash
ls /dev/ttyUSB* /dev/ttyACM* /dev/ttyS* 2>/dev/null
```

```bash
dmesg | tail -n 50
```

```bash
udevadm info -q property -n /dev/ttyUSB0
```

### Set serial permissions

Check the device owner/group:

```bash
ls -l /dev/ttyUSB0
```

Most systems use the `dialout` group. Add your user and re-login:

```bash
sudo usermod -aG dialout "$USER"
```

For systems using `uucp` instead:

```bash
sudo usermod -aG uucp "$USER"
```

Temporary quick test:

```bash
sudo chmod a+rw /dev/ttyUSB0
```

Use that only for bring-up, not as a permanent fix.

## Configuration

### UART API/config.ini

Default Linux example:

```ini
[UART]
device=/dev/ttyUSB0
baud=115200
read_timeout_ms=20
```

Update `device=` to match your actual port.

### OWL_MD860/config.ini

Review and adjust at least:

```ini
[mqtt]
broker       = 127.0.0.1
port         = 1883
client_id    = owl-cam-01
root_topic   = OWL
retained     = 0

[camera]
name         = OWL-MD860
ip           = 192.168.8.238

[udp_multicast]
enabled      = 1
group_ip     = 239.255.10.10
port         = 5000
iface_ip     = 0.0.0.0
ttl          = 1
loopback     = 1
camera_id    = 1
```

Notes:

- `camera.ip` must match the actual camera IP on your Linux network.
- `udp_multicast.iface_ip=0.0.0.0` is acceptable for the default interface selection on Linux.
- If multicast must leave a specific interface, set `iface_ip` to that interface's IPv4 address.

## MQTT Notes

The build links against the Paho C synchronous client library:

- library: `paho-mqtt3c`
- header: `MQTTClient.h`

The root Makefile tries `pkg-config --cflags --libs paho-mqtt3c` first and falls back to `-lpaho-mqtt3c` if no `.pc` file is present.

## VLC / RTSP Notes

The application still supports RTSP stream switching through VLC.

Linux behavior:

- The default executable name is `vlc`.
- The app looks for VLC in the configured path first, then on `PATH`.
- On Linux, the app stops the VLC process that it launched itself before starting the next stream.

Behavior difference from Windows:

- The old Windows code attempted to kill any `vlc.exe` process globally.
- The Linux port intentionally stops only the VLC child process started by this application.
- This avoids killing unrelated user VLC sessions.

## Important Linux Commands

Build full application:

```bash
make
```

Run full application:

```bash
./bin/wcsbtncam
```

Build UART component library only:

```bash
make -C "UART API"
```

Check MQTT library visibility:

```bash
pkg-config --cflags --libs paho-mqtt3c
```

Check serial permissions:

```bash
ls -l /dev/ttyUSB0
id
```

## Troubleshooting

### Permission denied on serial port

Symptoms:

- `uart_open` fails immediately
- `Permission denied` from the OS

Actions:

- Verify the configured device path exists.
- Add your user to `dialout` or `uucp`.
- Re-login after changing groups.
- Confirm the device is not already opened by another process.

Useful commands:

```bash
ls -l /dev/ttyUSB0
fuser /dev/ttyUSB0
```

### Missing MQTT library

Symptoms:

- compile fails on `MQTTClient.h`
- link fails on `-lpaho-mqtt3c`

Actions:

- Install `libpaho-mqtt-dev` or your distro equivalent.
- Verify `pkg-config --libs paho-mqtt3c` works.
- If installed from source, export `PKG_CONFIG_PATH`, `CFLAGS`, or `LDFLAGS` accordingly.

### Socket issues

Symptoms:

- camera TCP connect fails
- telemetry receive never starts
- multicast send/init fails

Actions:

- Verify camera IP reachability with `ping`.
- Confirm TCP port `8088` is reachable from the Linux machine.
- Confirm telemetry UDP and multicast ports are not blocked by firewall rules.
- If multicast is interface-sensitive, set `iface_ip` to the correct NIC address.

Useful commands:

```bash
ip addr
ip route
ping 192.168.8.238
ss -lun
```

### No camera connection

Symptoms:

- `owl_cam_open` fails
- liveliness check fails

Actions:

- Re-check `OWL_MD860/config.ini` camera IP.
- Verify the camera and Linux host are on the same network.
- Confirm the camera control service is active.
- Test reachability from Linux with `nc -vz <camera-ip> 8088` if `netcat` is installed.

### No UART data

Symptoms:

- repeated UART idle logs
- no decoded frames

Actions:

- Verify the Linux serial device name.
- Confirm baud rate matches the panel MCU.
- Confirm signal wiring and USB-UART adapter health.
- Use the Python simulator scripts with Linux device names for loopback testing.

### VLC does not start

Symptoms:

- stream switch command succeeds but no player appears

Actions:

- Confirm `vlc` is installed and runnable from the shell.
- If VLC is outside `PATH`, pass an explicit VLC path through the existing call site or change the `VLC_EXE_PATH` default.
- Test the RTSP URL manually in VLC.

## Manual Steps Required On The Linux Machine

1. Install build dependencies and the Paho MQTT C library.
2. Install VLC if RTSP viewing is required.
3. Update `UART API/config.ini` with the correct `/dev/tty*` path.
4. Update `OWL_MD860/config.ini` with the correct camera IP, broker, and multicast settings.
5. Add your user to the serial-port group such as `dialout`.
6. Re-login after the group change.
7. Build with `make` and run `./bin/wcsbtncam` from the project root.

## Porting Notes

The following functionality is preserved on Linux:

- camera TCP control
- telemetry UDP receive
- UDP multicast transmit/receive helpers
- MQTT publishing
- tracker command flow
- LED routing
- UART frame protocol
- INI/config parsing

Practical Linux-equivalent behavior where exact Windows semantics were not retained:

- Console shutdown now uses POSIX signals instead of Win32 console events.
- VLC process management is scoped to the process started by this app instead of terminating unrelated system-wide VLC processes.
- The UART API subdirectory now builds a static library instead of a standalone `.exe`, because that submodule has no local program entry point.
