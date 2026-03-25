#!/usr/bin/env python3
"""
MCU TX simulator:
Sends UART frames to the PC app to test PC receive/parse path.

Frame format:
  [0xAA][ID][VALUE][CRC][0x55]
  CRC = ID ^ VALUE
"""

import argparse
import configparser
import os
import time
import serial
import threading

START = 0xAA
END = 0x55

WAIT_START = 0
READ_ID = 1
READ_VALUE = 2
READ_CRC = 3
READ_END = 4


def calc_crc(frame_id: int, value: int) -> int:
    return (frame_id ^ value) & 0xFF


def build_frame(frame_id: int, value: int) -> bytes:
    crc = calc_crc(frame_id, value)
    return bytes([START, frame_id & 0xFF, value & 0xFF, crc, END])

class StreamParser:
    def __init__(self) -> None:
        self.state = WAIT_START
        self.frame_id = 0
        self.value = 0
        self.crc = 0

    def feed(self, b: int):
        if self.state == WAIT_START:
            if b == START:
                self.state = READ_ID
            return None

        if self.state == READ_ID:
            self.frame_id = b
            self.state = READ_VALUE
            return None

        if self.state == READ_VALUE:
            self.value = b
            self.state = READ_CRC
            return None

        if self.state == READ_CRC:
            self.crc = b
            self.state = READ_END
            return None

        if self.state == READ_END:
            self.state = WAIT_START
            if b != END:
                return ("error", "END byte mismatch", self.frame_id, self.value)
            if self.crc != calc_crc(self.frame_id, self.value):
                return ("error", "CRC mismatch", self.frame_id, self.value)
            return ("ok", self.frame_id, self.value)

        self.state = WAIT_START
        return ("error", "parser state error", self.frame_id, self.value)


def load_u8_map(cfg: configparser.ConfigParser, section: str):
    out = []
    if section not in cfg:
        return out
    for name, raw in cfg[section].items():
        try:
            val = int(raw, 0) & 0xFF
        except ValueError:
            continue
        out.append((name.upper(), val))
    return out


def build_id_decode_map(cfg_path: str):
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.optionxform = str
    cfg.read(cfg_path, encoding="ascii")

    out = {}
    for section, kind in (
        ("INPUT_BUTTON_IDS", "BUTTON"),
        ("INPUT_SWITCH_IDS", "SWITCH"),
        ("KNOB_IDS", "KNOB"),
        ("LED_IDS", "LED"),
        ("BUTTON_LED_IDS", "BUTTON_LED"),
    ):
        for name, frame_id in load_u8_map(cfg, section):
            out[frame_id] = (kind, name)
    return out


def build_target_tx_sequence(cfg_path: str):
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.optionxform = str
    cfg.read(cfg_path, encoding="ascii")

    buttons = load_u8_map(cfg, "INPUT_BUTTON_IDS")
    button_map = {name: frame_id for name, frame_id in buttons}
    knobs = load_u8_map(cfg, "KNOB_IDS")
    knob_map = {name: frame_id for name, frame_id in knobs}
    target_names = ("LRF_RESET", "DAY", "LOW_LIGHT", "THERMAL")

    seq = []
    for name in target_names:
        frame_id = button_map.get(name)
        if frame_id is None:
            continue
        seq.append(("BUTTON", name, frame_id, 1))
        seq.append(("BUTTON", name, frame_id, 0))

    freq_id = knob_map.get("FREQUENCY")
    if freq_id is not None:
        for value in (1, 4, 10, 20, 100, 200):
            seq.append(("KNOB", "FREQUENCY", freq_id, value))
    return seq


def build_console_targets(cfg_path: str):
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.optionxform = str
    cfg.read(cfg_path, encoding="ascii")

    buttons = load_u8_map(cfg, "INPUT_BUTTON_IDS")
    button_map = {name: frame_id for name, frame_id in buttons}
    out = {}
    for name in ("DAY", "LOW_LIGHT", "THERMAL", "DROP"):
        frame_id = button_map.get(name)
        if frame_id is not None:
            out[frame_id] = name
    return out


def main() -> int:
    default_cfg = os.path.join(os.path.dirname(__file__), "config.ini")
    parser = argparse.ArgumentParser(description="UART MCU TX simulator (send to PC app)")
    parser.add_argument("--port", default="COM9", help="COM port (default: COM9)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--period", type=float, default=10.0, help="Seconds between frames")
    parser.add_argument("--config", default=default_cfg, help="Path to config.ini")
    parser.add_argument("--mode", choices=("interactive", "pattern"), default="interactive",
                        help="interactive: type id/value in console, pattern: send built-in test pattern")
    parser.add_argument("--send-invalid-every", type=int, default=0,
                        help="Inject invalid CRC frame every N valid frames (0=disabled)")
    args = parser.parse_args()

    pattern = build_target_tx_sequence(args.config)
    console_targets = build_console_targets(args.config)
    id_decode_map = build_id_decode_map(args.config)
    if args.mode == "pattern" and not pattern:
        print(f"Required IDs not found in {args.config} (need LRF_RESET, DAY, LOW_LIGHT, THERMAL, FREQUENCY).")
        return 1
    if args.mode == "interactive" and not console_targets:
        print(f"Required button IDs not found in {args.config} (need DAY, LOW_LIGHT, THERMAL, DROP).")
        return 1

    with serial.Serial(args.port, args.baud, timeout=0.1) as ser:
        stop_evt = threading.Event()
        rx_parser = StreamParser()

        def rx_worker() -> None:
            while not stop_evt.is_set():
                data = ser.read(64)
                if not data:
                    continue
                for x in data:
                    parsed = rx_parser.feed(x)
                    if parsed is None:
                        continue
                    if parsed[0] == "ok":
                        _, frame_id, value = parsed
                        decoded = id_decode_map.get(frame_id)
                        if decoded is None:
                            print(f"RX UNKNOWN: id={frame_id} value={value}")
                        else:
                            kind, name = decoded
                            print(f"RX {kind}: {name} id={frame_id} value={value}")
                    else:
                        _, reason, frame_id, value = parsed
                        print(f"RX invalid: {reason} id={frame_id} value={value}")

        rx_thread = threading.Thread(target=rx_worker, daemon=True)
        rx_thread.start()

        print(f"Opened {args.port} @ {args.baud}")
        print(f"Sending MCU->PC frames from {args.config}.")
        sent_valid = 0
        try:
            if args.mode == "pattern":
                print("Pattern: LRF_RESET, DAY, LOW_LIGHT, THERMAL (ON/OFF) + FREQUENCY (1,4,10,20,100,200)")
                print("Console shows both TX and RX frames. Press Ctrl+C to stop.")
                i = 0
                while True:
                    kind, name, frame_id, value = pattern[i % len(pattern)]
                    frame = bytearray(build_frame(frame_id, value))
                    ser.write(frame)
                    print(
                        f"TX valid: {kind} {name} id={frame_id} value={value} "
                        f"frame={[hex(b) for b in frame]}"
                    )
                    sent_valid += 1

                    if args.send_invalid_every > 0 and (sent_valid % args.send_invalid_every) == 0:
                        bad = bytearray(frame)
                        bad[3] ^= 0x01
                        ser.write(bad)
                        print(f"TX invalid CRC test frame: {[hex(b) for b in bad]}")

                    i += 1
                    time.sleep(args.period)
            else:
                print("Interactive mode.")
                print("Type: <id> <value>")
                print("Allowed IDs from config:")
                for frame_id, name in sorted(console_targets.items()):
                    print(f"  {frame_id} -> {name}")
                print("Allowed values: 0 or 1")
                print("Examples: 11 1, 12 0, 13 1, 14 0")
                print("Type 'q' or 'quit' to stop.")

                while True:
                    line = input("TX> ").strip()
                    if not line:
                        continue
                    if line.lower() in ("q", "quit", "exit"):
                        break

                    parts = line.split()
                    if len(parts) != 2:
                        print("Invalid input. Use: <id> <value>")
                        continue

                    try:
                        frame_id = int(parts[0], 0)
                        value = int(parts[1], 0)
                    except ValueError:
                        print("Invalid numbers. Use integer id and value.")
                        continue

                    if frame_id not in console_targets:
                        print(f"Unsupported id={frame_id}. Allowed ids: {sorted(console_targets)}")
                        continue
                    if value not in (0, 1):
                        print("Unsupported value. Use 0 or 1.")
                        continue

                    name = console_targets[frame_id]
                    frame = bytearray(build_frame(frame_id, value))
                    ser.write(frame)
                    print(
                        f"TX valid: BUTTON {name} id={frame_id} value={value} "
                        f"frame={[hex(b) for b in frame]}"
                    )
                    sent_valid += 1

                    if args.send_invalid_every > 0 and (sent_valid % args.send_invalid_every) == 0:
                        bad = bytearray(frame)
                        bad[3] ^= 0x01
                        ser.write(bad)
                        print(f"TX invalid CRC test frame: {[hex(b) for b in bad]}")
        except KeyboardInterrupt:
            pass
        finally:
            stop_evt.set()
            rx_thread.join(timeout=1.0)
            print("\nStopped.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
