#!/usr/bin/env python3
"""
MCU RX simulator:
Reads UART frames from the PC app and prints decoded control names.

Frame format:
  [0xAA][ID][VALUE][CRC][0x55]
  CRC = ID ^ VALUE
"""

import argparse
import configparser
import os
import serial

START = 0xAA
END = 0x55

WAIT_START = 0
READ_ID = 1
READ_VALUE = 2
READ_CRC = 3
READ_END = 4


def calc_crc(frame_id: int, value: int) -> int:
    return (frame_id ^ value) & 0xFF


def load_id_maps(cfg_path: str):
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.optionxform = str
    cfg.read(cfg_path, encoding="ascii")

    id_map = {}
    for section, kind in (
        ("INPUT_BUTTON_IDS", "BUTTON"),
        ("BUTTON_LED_IDS", "BUTTON_LED"),
        ("INPUT_SWITCH_IDS", "SWITCH"),
        ("LED_IDS", "LED"),
        ("KNOB_IDS", "KNOB"),
    ):
        if section not in cfg:
            continue
        for name, raw in cfg[section].items():
            try:
                frame_id = int(raw, 0) & 0xFF
            except ValueError:
                continue
            id_map[frame_id] = (kind, name.upper())
    return id_map


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


def main() -> int:
    default_cfg = os.path.join(os.path.dirname(__file__), "config.ini")
    parser = argparse.ArgumentParser(description="UART MCU RX simulator (read from PC app)")
    parser.add_argument("--port", default="COM9", help="COM port (default: COM9)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--config", default=default_cfg, help="Path to config.ini for ID decoding")
    args = parser.parse_args()

    stream = StreamParser()
    id_map = load_id_maps(args.config)

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        print(f"Opened {args.port} @ {args.baud}")
        print(f"Listening PC->MCU frames using map from {args.config}. Press Ctrl+C to stop.")
        try:
            while True:
                data = ser.read(64)
                if not data:
                    continue
                for x in data:
                    parsed = stream.feed(x)
                    if parsed is None:
                        continue
                    if parsed[0] == "ok":
                        _, frame_id, value = parsed
                        kind_name = id_map.get(frame_id)
                        if kind_name is None:
                            print(f"RX valid: UNKNOWN id={frame_id} value={value}")
                        else:
                            kind, name = kind_name
                            print(f"RX valid: {kind} {name} id={frame_id} value={value}")
                    else:
                        _, reason, frame_id, value = parsed
                        print(f"RX invalid: {reason} id={frame_id} value={value}")
        except KeyboardInterrupt:
            print("\nStopped.")
            return 0


if __name__ == "__main__":
    raise SystemExit(main())
