# UART API Bridge

Windows C11 UART bridge for WCS button interface.

## UART Protocol

Frame format is fixed 5 bytes:

1. `START` = `0xAA`
2. `ID`
3. `VALUE`
4. `CRC` = `ID ^ VALUE`
5. `END` = `0x55`

This protocol is implemented in `uart_protocol.c/.h` and used by both RX parser and TX sender.

## Runtime Flow (Current)

### Startup Flow

1. `main()` calls `config_load("config.ini", &cfg)`
2. `config_load()` parses INI sections and builds ID->name maps
3. `main()` calls `uart_open(&uart, cfg.uart_device, cfg.uart_baud, cfg.uart_read_timeout_ms)`
4. Main loop starts

### Main Loop Flow

Loop runs continuously with `Sleep(2)`:

1. **Periodic LED status TX**
   - every `LED_TX_PERIOD_MS` (currently `700 ms`)
   - `update_led_states(led_state, step)` updates test pattern values
   - `send_next_led_state(&uart, &cfg, led_state, led_index)` sends one LED from `[LED_IDS]` (round-robin)

2. **UART RX parse**
   - `uart_read_and_parse(&uart, &rx_id, &rx_value)`
   - If a complete valid frame is decoded, `handle_rx_frame(...)` is called
   - If parser fails, `uart_get_last_parse_error()` is printed

3. **RX classification and action (`handle_rx_frame`)**
   - `is_button_id(id)`:
     - print `RX BUTTON: ...`
     - call `mirror_button_to_led(...)`
   - `is_switch_id(id)`:
     - print only (`RX SWITCH: ...`)
   - `is_knob_id(id)`:
     - print only (`RX KNOB: ...`)
   - `is_led_id(id)`:
     - print only (`RX LED: ...`)
   - `is_button_led_id(id)`:
     - print only (`RX BUTTON_LED: ...`)
   - otherwise print `RX UNKNOWN`

### Button Mirror Flow (Only for Push Buttons)

`mirror_button_to_led(uart, cfg, button_id, value)`:

1. Resolve button name from `button_id` using `get_button_name_by_id()`
2. Build lookup key `<BUTTON_NAME>_LED`
3. Resolve mapped LED ID from `[BUTTON_LED_IDS]` via `config_get_button_led_id_by_button_name()`
4. Send one TX frame with same value using `uart_send_control(uart, led_id, value)`
5. Print `TX BUTTON_LED: ...`

If mapping is missing, warning is printed.  
If UART send fails, error is printed.

## Function Responsibilities

- `main.c`
  - Application loop and behavior policy
  - RX frame classification and logging
  - Button->button_led mirroring
  - Periodic LED status sender

- `config_ini.c/.h`
  - Parse `config.ini`
  - Store IDs in `Config`
  - Build ID->name maps
  - Lookup helpers such as:
    - `get_button_name_by_id`
    - `get_switch_name_by_id`
    - `get_knob_name_by_id`
    - `get_led_name_by_id`
    - `get_button_led_name_by_id`
    - `config_get_button_led_id_by_button_name`

- `uart_protocol.c/.h`
  - Frame build/decode and CRC
  - Byte-stream parser state machine
  - `uart_send_control()` and `uart_read_and_parse()`

- `uart_win.c/.h`
  - Windows COM open/close/read/write wrappers

## Config Sections Used

- `[UART]`: COM port + baud + timeout
- `[INPUT_BUTTON_IDS]`: incoming push button IDs
- `[INPUT_SWITCH_IDS]`: incoming switch IDs
- `[KNOB_IDS]`: incoming knob IDs
- `[LED_IDS]`: normal LED/status IDs (also used by periodic TX)
- `[BUTTON_LED_IDS]`: output LED IDs used for button mirror response
- `[MODE_VALUES]`, `[FREQUENCY_VALUES]`: knob value maps

## Build

```sh
make
```

or

```sh
gcc -std=c11 -O2 -Wall -Wextra -I. main.c config_ini.c uart_win.c uart_protocol.c -o uart_bridge.exe -lkernel32
```

## Run

```sh
./uart_bridge.exe
```

## Test Tools

- `sim_uart_mcu_tx.py`: sends button/switch/knob frames to C app, also prints received frames
- `sim_uart_mcu_rx.py`: receives and decodes frames sent by C app

## Clean

```sh
make clean
```
