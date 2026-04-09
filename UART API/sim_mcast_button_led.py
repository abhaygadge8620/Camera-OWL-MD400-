#!/usr/bin/env python3
"""
Joystick multicast listener.

This script listens only on the joystick multicast channel and prints only
joystick packets to the console.

Examples:
  python3 sim_mcast_button_led.py listen
  python3 sim_mcast_button_led.py listen --pretty
  python3 sim_mcast_button_led.py listen --config ../OWL_MD860/config.ini
"""

from __future__ import annotations

import argparse
import configparser
import json
import socket
from typing import Dict, Tuple


def load_config(cfg_path: str) -> Tuple[str, int, str]:
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.optionxform = str
    read_files = cfg.read(cfg_path, encoding="ascii")
    if not read_files:
        raise FileNotFoundError(f"unable to read config: {cfg_path}")

    group_ip = cfg.get("mcast_joystick", "group_ip", fallback="239.255.2.2").strip()
    port = cfg.getint("mcast_joystick", "port", fallback=50100)
    iface_ip = cfg.get("mcast_common", "iface_ip", fallback="0.0.0.0").strip()
    return group_ip, port, iface_ip


def make_rx_socket(group_ip: str, port: int, iface_ip: str) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", port))
    if iface_ip and iface_ip != "0.0.0.0":
        mreq = socket.inet_aton(group_ip) + socket.inet_aton(iface_ip)
    else:
        mreq = socket.inet_aton(group_ip) + socket.inet_aton("0.0.0.0")
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    return sock


def is_joystick_payload(payload: Dict[str, object]) -> bool:
    if not isinstance(payload, dict):
        return False

    axes = payload.get("axes")
    thumbwheels = payload.get("thumbwheels")
    buttons = payload.get("buttons")

    return (
        isinstance(axes, dict)
        and isinstance(thumbwheels, dict)
        and isinstance(buttons, dict)
        and ("x" in axes)
        and ("y" in axes)
        and ("thumb_a" in thumbwheels)
        and ("thumb_b" in thumbwheels)
    )


def get_hat(payload: Dict[str, object]) -> Tuple[object, object]:
    hat_0 = payload.get("hat_0")
    if isinstance(hat_0, dict):
        return hat_0.get("x"), hat_0.get("y")

    hats = payload.get("hats")
    if isinstance(hats, dict):
        nested_hat = hats.get("hat_0")
        if isinstance(nested_hat, dict):
            return nested_hat.get("x"), nested_hat.get("y")

    return None, None


def print_pretty_payload(payload: Dict[str, object]) -> None:
    axes = payload.get("axes", {})
    thumbwheels = payload.get("thumbwheels", {})
    buttons = payload.get("buttons", {})
    hat_x, hat_y = get_hat(payload)

    print(
        "parsed "
        f"connected={payload.get('connected')} "
        f"heartbeat={payload.get('heartbeat')} "
        f"axes=({axes.get('x')}, {axes.get('y')}) "
        f"thumb=({thumbwheels.get('thumb_a')}, {thumbwheels.get('thumb_b')}, {thumbwheels.get('thumb_c')}) "
        f"hat=({hat_x}, {hat_y})"
    )
    print(
        "buttons "
        f"b1={buttons.get('button_1')} "
        f"b2={buttons.get('button_2')} "
        f"b3={buttons.get('button_3')} "
        f"b4={buttons.get('button_4')} "
        f"b5={buttons.get('button_5')} "
        f"trig_up={buttons.get('trigger_up')} "
        f"trig_down={buttons.get('trigger_down')}"
    )


def cmd_listen(args: argparse.Namespace) -> int:
    group_ip, port, iface_ip = load_config(args.config)
    sock = make_rx_socket(group_ip, port, iface_ip)
    if args.timeout > 0:
        sock.settimeout(args.timeout)
    else:
        sock.settimeout(None)

    print(f"listening joystick multicast on {group_ip}:{port} via iface {iface_ip}")
    print("printing only joystick packets")
    print("press Ctrl+C to stop")

    try:
        while True:
            try:
                payload, addr = sock.recvfrom(args.max_size)
            except socket.timeout:
                if args.timeout > 0:
                    print("listen timeout")
                    return 0
                continue

            text = payload.decode("utf-8", errors="replace")

            try:
                msg = json.loads(text)
            except json.JSONDecodeError:
                continue

            if not is_joystick_payload(msg):
                continue

            print(f"\nfrom {addr[0]}:{addr[1]}")
            print(text)
            if args.pretty:
                print_pretty_payload(msg)
    except KeyboardInterrupt:
        print("\nstopped")
        return 0
    finally:
        sock.close()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Joystick multicast listener")
    sub = parser.add_subparsers(dest="command", required=True)

    listen = sub.add_parser("listen", help="listen only for joystick multicast packets")
    listen.add_argument("--config", default="../OWL_MD860/config.ini", help="path to config.ini")
    listen.add_argument("--timeout", type=float, default=0.0, help="socket timeout seconds, 0=wait forever")
    listen.add_argument("--max-size", type=int, default=4096, help="max UDP payload size")
    listen.add_argument("--pretty", action="store_true", help="decode joystick fields for easier reading")
    listen.set_defaults(func=cmd_listen)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
