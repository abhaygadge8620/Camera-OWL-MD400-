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


def build_full_test_sequence(cfg_path: str):
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.optionxform = str
    cfg.read(cfg_path, encoding="ascii")

    buttons = load_u8_map(cfg, "INPUT_BUTTON_IDS")
    switches = load_u8_map(cfg, "INPUT_SWITCH_IDS")
    knobs = load_u8_map(cfg, "KNOB_IDS")

    mode_vals = load_u8_map(cfg, "MODE_VALUES")
    freq_vals = load_u8_map(cfg, "FREQUENCY_VALUES")

    mode_ids = [x for x in knobs if x[0] == "MODE"]
    freq_ids = [x for x in knobs if x[0] == "FREQUENCY"]

    seq = []

    for name, frame_id in buttons:
        seq.append(("BUTTON", name, frame_id, 1))
        seq.append(("BUTTON", name, frame_id, 0))

    for name, frame_id in switches:
        seq.append(("SWITCH", name, frame_id, 1))
        seq.append(("SWITCH", name, frame_id, 0))

    if mode_ids:
        mode_id = mode_ids[0][1]
        for mode_name, mode_val in mode_vals:
            seq.append(("KNOB", f"MODE:{mode_name}", mode_id, mode_val))

    if freq_ids:
        freq_id = freq_ids[0][1]
        for freq_name, freq_val in freq_vals:
            seq.append(("KNOB", f"FREQUENCY:{freq_name}", freq_id, freq_val))

    return seq


def main() -> int:
    parser = argparse.ArgumentParser(description="UART MCU TX simulator (send to PC app)")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="Serial device (default: /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--period", type=float, default=0.7, help="Seconds between frames")
    parser.add_argument("--config", default="config.ini", help="Path to config.ini")
    parser.add_argument("--send-invalid-every", type=int, default=0,
                        help="Inject invalid CRC frame every N valid frames (0=disabled)")
    args = parser.parse_args()

    pattern = build_full_test_sequence(args.config)
    id_decode_map = build_id_decode_map(args.config)
    if not pattern:
        print(f"No test IDs found in {args.config}.")
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
        print("Console shows both TX and RX frames. Press Ctrl+C to stop.")
        i = 0
        sent_valid = 0
        try:
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
                    bad[3] ^= 0x01  # break CRC
                    ser.write(bad)
                    print(f"TX invalid CRC test frame: {[hex(b) for b in bad]}")

                i += 1
                time.sleep(args.period)
        except KeyboardInterrupt:
            stop_evt.set()
            rx_thread.join(timeout=1.0)
            print("\nStopped.")
            return 0


if __name__ == "__main__":
    raise SystemExit(main())
