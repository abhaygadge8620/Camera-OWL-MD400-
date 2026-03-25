# Camera-OWL-MD400-

Windows C application that connects a WCS button panel over UART to an OWL-MD400 / OWL-MD860 camera over Ethernet TCP, keeps panel LEDs synchronized, publishes telemetry over MQTT and optional UDP multicast, and switches local VLC streams for the selected camera view.

## Overview

The project acts as a bridge between:

- A UART-based WCS control panel
- The OWL camera Ethernet control interface
- Camera telemetry
- Panel LEDs
- MQTT publishing
- Optional UDP multicast publishing
- VLC RTSP stream viewing

The main application entry point is [`main.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/main.c). Most project behavior is implemented there. The camera protocol implementation lives under [`OWL_MD860`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860). UART parsing and UART TX live under [`UART API`](c:/WCSBTNCAM/Camera-OWL-MD400-/UART%20API).

## Project Structure

- [`main.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/main.c)
  Main runtime loop. Reads UART frames, calls camera functions, updates LEDs, processes telemetry, handles reconnect, manages VLC.
- [`led_status_router.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/led_status_router.c)
  Maps logical button names to UART LED IDs and sends LED ON/OFF frames.
- [`led_status_router.h`](c:/WCSBTNCAM/Camera-OWL-MD400-/led_status_router.h)
  LED router interface and per-button cached state.
- [`camera_command_router.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/camera_command_router.c)
  Older routing logic / helper code. Not the main active control path for the current runtime.
- [`OWL_MD860/camera_iface.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/camera_iface.c)
  Low-level camera TCP protocol, telemetry receive, tracker helpers, RTSP helpers, VLC switching.
- [`OWL_MD860/camera_iface.h`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/camera_iface.h)
  Public camera API used by `main.c`.
- [`OWL_MD860/camera_mqtt.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/camera_mqtt.c)
  MQTT connect and publish helpers.
- [`OWL_MD860/api2_mcast_win.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/api2_mcast_win.c)
  UDP multicast publisher support.
- [`UART API/config_ini.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/UART%20API/config_ini.c)
  Parses UART panel config and ID mappings.
- [`UART API/uart_protocol.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/UART%20API/uart_protocol.c)
  UART frame encode/decode and TX helper `uart_send_control(...)`.
- [`UART API/uart_win.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/UART%20API/uart_win.c)
  Windows COM port open/read/write helpers.
- [`UART API/config.ini`](c:/WCSBTNCAM/Camera-OWL-MD400-/UART%20API/config.ini)
  UART port settings and panel ID mapping.
- [`OWL_MD860/config.ini`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/config.ini)
  Camera IP, MQTT config, UDP multicast config.
- [`Makefile`](c:/WCSBTNCAM/Camera-OWL-MD400-/Makefile)
  GCC build definition for Windows.
- [`MD400_ICD_23_12_2025.pdf`](c:/WCSBTNCAM/Camera-OWL-MD400-/MD400_ICD_23_12_2025.pdf)
  Protocol/reference document for the camera.

## Runtime Flow

At startup, the application:

1. Loads UART config from [`UART API/config.ini`](c:/WCSBTNCAM/Camera-OWL-MD400-/UART%20API/config.ini).
2. Loads camera, MQTT, and UDP multicast settings from [`OWL_MD860/config.ini`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/config.ini).
3. Connects MQTT and publishes initial config if possible.
4. Starts UDP multicast publishing support if enabled.
5. Opens the UART COM port.
6. Opens the camera TCP connection and checks liveliness.
7. Enables or disables telemetry on the camera.
8. Reads and caches RTSP URLs from the camera.
9. Initializes LED routing state.
10. Opens the telemetry UDP receiver if telemetry is enabled.

Then the app enters the main loop:

- Read one UART frame
- Decode `id` and `value`
- Try each command handler in order
- Execute the first matching handler
- Poll telemetry
- Publish telemetry to MQTT
- Update LEDs from telemetry
- Recover camera connection automatically after `OWL_ERR_IO`

On shutdown, the app:

- Closes telemetry
- Stops UDP multicast
- Closes MQTT
- Stops VLC
- Closes UART
- Sends tracker STOP if tracker was active
- Closes the camera connection

## Main Runtime State

Important globals in [`main.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/main.c):

- `g_running`
  Main loop exit flag set by Windows console control handler.
- `g_loop_count`
  Loop counter used in logs.
- `g_tracker_mode_on`
  Current tracker ON/OFF state.
- `g_last_tracker_state`
  Last tracker command used for safe stop at shutdown.
- `g_rtsp_urls`
  Cached RTSP URLs for thermal, day, and low-light streams.
- `g_rtsp_urls_valid`
  Indicates whether RTSP cache is valid.
- `g_selected_view_cam`
  Currently selected camera view used by tracker/view logic.

## Current Active UART Mapping

The active input/LED mapping comes from [`UART API/config.ini`](c:/WCSBTNCAM/Camera-OWL-MD400-/UART%20API/config.ini).

### Button IDs

- `OPTICS_RESET = 2`
- `LRF_RESET = 3`
- `DAY = 11`
- `LOW_LIGHT = 12`
- `THERMAL = 13`
- `DROP = 14`
- `LRF = 75`

### Knob IDs

- `MODE = 52`
- `FREQUENCY = 53`

### LED IDs

- `OPTICS_RESET_LED = 61`
- `LRF_RESET_LED = 62`
- `DAY_LED = 70`
- `LOW_LIGHT_LED = 71`
- `THERMAL_LED = 72`
- `DROP_LED = 73`
- `LRF_LED = 76`
- `SW_LRF_LED = 81`

## Current Active Command Behavior

This section describes the current active behavior in [`main.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/main.c).

### `id 11`: DAY

Handled by `handle_camera_view_switch_command()`.

- Switches tracker camera to day-normal
- Switches VLC stream to the configured RTSP URL
- Turns DAY LED on and LOW_LIGHT / THERMAL LEDs off
- If tracker is already active, restarts tracker centered on the selected camera

### `id 12`: LOW_LIGHT

Handled by `handle_camera_view_switch_command()`.

- Switches tracker camera to day-low-light
- Switches VLC stream
- Updates camera selection LEDs
- Restarts tracker on selected camera if tracker is active

### `id 13`: THERMAL

Handled by `handle_camera_view_switch_command()`.

- Switches tracker camera to thermal
- Switches VLC stream
- Updates camera selection LEDs
- Restarts tracker on selected camera if tracker is active

### `id 14`: DROP

Handled by `handle_drop_tracker_command()`.

- `value 1` starts tracker on the currently selected camera view
- `value 0` stops tracker on the currently selected camera view
- Uses center coordinates for the selected camera
- Updates DROP LED

### `id 2`: OPTICS_RESET

Handled by `handle_optics_reset_command()`.

- `value 1`
  Calls `owl_cam_restart(cam, OWL_RESTART_SERVICE)` and turns `OPTICS_RESET` LED on
- `value 0`
  Sends UART LED OFF command for the mapped `OPTICS_RESET` LED and syncs LED router state

### `id 3`: LRF_RESET

Handled by `handle_lrf_reset_command()`.

- `value 1` or `value 0`
  Both values are accepted by the current code
- Action sequence:
  - Turn `LRF_RESET` LED on
  - Call `owl_cam_lrf_stop()`
  - Call `owl_cam_lrf_align_pointer(cam, false)`
  - Send UART LED OFF for `LRF_LED` `id 76`
  - Turn `LRF_RESET` LED off

### `id 52`: LRF Single Measure Mode

Handled by `handle_lrf_single_measure_mode_command()`.

Raw value mapping:

- `0 -> OWL_LRF_SMM`
- `2 -> OWL_LRF_CH1`
- `1 -> OWL_LRF_CH2`

Calls `owl_cam_lrf_single_measure(...)`.

### `id 53`: LRF Frequency

Handled by `handle_lrf_frequency_command()`.

Raw value mapping:

- `1 -> OWL_LRF_FREQ_1HZ`
- `4 -> OWL_LRF_FREQ_4HZ`
- `10 -> OWL_LRF_FREQ_10HZ`
- `20 -> OWL_LRF_FREQ_20HZ`
- `100 -> OWL_LRF_FREQ_100HZ`
- `200 -> OWL_LRF_FREQ_200HZ`

Calls `owl_cam_lrf_set_frequency(...)`.

### `id 75`: LRF Align Pointer

Handled by `handle_lrf_align_pointer_command()`.

- `value 1`
  Turns pointer ON using `owl_cam_lrf_align_pointer(cam, true)`
- `value 0`
  Turns pointer OFF using `owl_cam_lrf_align_pointer(cam, false)`

After the camera command, it sends the same ON/OFF state to UART LED `76`.

## Telemetry Behavior

Telemetry support is implemented in [`OWL_MD860/camera_iface.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/camera_iface.c) and consumed in [`main.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/main.c).

When telemetry is enabled:

- `camera_telem_open(...)` opens the UDP telemetry receiver
- `camera_telem_recv(...)` reads frames
- `camera_telem_publish(...)` publishes telemetry through MQTT support
- `led_status_router_update_from_telem(...)` keeps `SW_LRF` LED synchronized to LRF power status

If telemetry is lost for more than `500 ms`, the app forces `SW_LRF` LED off for safety.

## LED Routing

LED logic is centralized in [`led_status_router.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/led_status_router.c).

Key points:

- Logical names like `DAY`, `LOW_LIGHT`, `THERMAL`, `DROP`, `SW_LRF`, `LRF_RESET`, `OPTICS_RESET` are mapped to actual UART LED IDs.
- Cached LED state is stored so duplicate UART LED frames are avoided.
- `led_status_router_set_led(...)` is the main ON/OFF helper.
- Some flows also send a direct `uart_send_control(...)` frame when explicit LED behavior is needed immediately.

## Camera Recovery

Automatic reconnect is implemented in `recover_camera_connection()` in [`main.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/main.c).

When a camera command returns `OWL_ERR_IO`:

- The connection is closed
- TCP is reopened
- Camera liveliness is checked again
- Telemetry setting is restored
- RTSP URLs are refreshed
- Tracker mode is reset to OFF

Wrapper helpers used for this:

- `call_cam_u8_with_recover(...)`
- `call_cam_noarg_with_recover(...)`
- `call_cam_bool_with_recover(...)`

## RTSP and VLC Behavior

RTSP helper code is in [`OWL_MD860/camera_iface.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/camera_iface.c).

The app:

- Reads RTSP URLs from the camera once at startup
- Caches thermal, day, and low-light URLs
- Uses `owl_cam_vlc_switch_stream(...)` to launch or replace the VLC stream on camera view change
- Uses `owl_cam_vlc_stop()` when tracker starts or when the app exits

Default VLC path in [`main.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/main.c):

```text
C:\Program Files\VideoLAN\VLC\vlc.exe
```

## MQTT and UDP Multicast

MQTT helpers are in [`OWL_MD860/camera_mqtt.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/camera_mqtt.c).

The app:

- Connects to the broker from [`OWL_MD860/config.ini`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/config.ini)
- Publishes initial camera config/state
- Publishes telemetry values

UDP multicast helpers are in [`OWL_MD860/api2_mcast_win.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/api2_mcast_win.c).

When enabled, the app initializes multicast publishing using:

- group IP
- interface IP
- port
- TTL
- loopback
- camera ID

from [`OWL_MD860/config.ini`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/config.ini).

## Configuration

### UART Config

File: [`UART API/config.ini`](c:/WCSBTNCAM/Camera-OWL-MD400-/UART%20API/config.ini)

Important fields:

- `device`
- `baud`
- `read_timeout_ms`
- `INPUT_BUTTON_IDS`
- `INPUT_SWITCH_IDS`
- `BUTTON_LED_IDS`
- `KNOB_IDS`

Current UART device default:

```ini
[UART]
device=COM4
baud=115200
read_timeout_ms=0
```

### Camera Config

File: [`OWL_MD860/config.ini`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/config.ini)

Current example values:

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

## Build

The project uses GCC on Windows with the [`Makefile`](c:/WCSBTNCAM/Camera-OWL-MD400-/Makefile).

Linked libraries:

- `ws2_32`
- `kernel32`
- `paho-mqtt3c`

Build command:

```powershell
make
```

Expected output executable:

```text
bin/wcsbtncam.exe
```

The Makefile compiles:

- `main.c`
- `camera_command_router.c`
- `led_status_router.c`
- all `.c` files under `OWL_MD860`
- UART support files under `UART API`

## Run

Typical run flow:

1. Connect the WCS UART panel to the configured COM port.
2. Ensure the camera is reachable at the configured IP.
3. Ensure VLC is installed at the configured path, or update `VLC_EXE_PATH`.
4. Ensure MQTT broker is reachable if MQTT is required.
5. Build the executable.
6. Run `bin/wcsbtncam.exe`.

## Logging

Main application logs use the `MAIN_LOG(...)` macro in [`main.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/main.c).

Logs typically include:

- loop counter
- incoming UART `id` and `value`
- camera ACK/failure
- reconnect attempts
- LED updates
- telemetry LED state changes

This is the primary source for troubleshooting runtime behavior.

## Known Current Notes

- Tracker UART IDs `91`, `92`, and `93` are no longer active in the current code.
- `lrf_reset_clear_deadline` and `optics_reset_clear_deadline` variables exist in the loop, but current active code does not assign future deadlines to them.
- `camera_command_router.c` exists in the build but the active command routing for the current runtime is centered in [`main.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/main.c).

## Primary Files for Future Changes

If you need to change behavior, these are the main files to edit:

- [`main.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/main.c)
  For button-to-action logic and runtime flow
- [`UART API/config.ini`](c:/WCSBTNCAM/Camera-OWL-MD400-/UART%20API/config.ini)
  For panel ID mapping
- [`led_status_router.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/led_status_router.c)
  For LED routing and LED caching
- [`OWL_MD860/camera_iface.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860/camera_iface.c)
  For low-level camera protocol behavior

## Summary

This project is a Windows camera control bridge that:

- reads UART button and knob inputs
- converts them to OWL camera Ethernet commands
- synchronizes panel LEDs
- switches RTSP streams in VLC
- publishes telemetry to MQTT
- optionally publishes over UDP multicast
- reconnects automatically after camera I/O errors

The current active logic is centered in [`main.c`](c:/WCSBTNCAM/Camera-OWL-MD400-/main.c), with camera transport in [`OWL_MD860`](c:/WCSBTNCAM/Camera-OWL-MD400-/OWL_MD860) and UART handling in [`UART API`](c:/WCSBTNCAM/Camera-OWL-MD400-/UART%20API).
