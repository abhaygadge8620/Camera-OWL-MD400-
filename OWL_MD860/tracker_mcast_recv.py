#!/usr/bin/env python3
import argparse
import json
import socket
import struct


DEFAULT_GROUP = "239.255.2.4"
DEFAULT_PORT = 50100
DEFAULT_IFACE = "192.168.1.101"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Receive and print OWL_MD860 tracker_telemetry multicast packets.",
    )
    parser.add_argument("--group", default=DEFAULT_GROUP)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--iface", default=DEFAULT_IFACE)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", args.port))
    sock.setsockopt(
        socket.IPPROTO_IP,
        socket.IP_ADD_MEMBERSHIP,
        struct.pack("4s4s", socket.inet_aton(args.group), socket.inet_aton(args.iface)),
    )

    print(f"listening {args.group} {args.port} iface={args.iface}")

    try:
        while True:
            data, addr = sock.recvfrom(65535)
            text = data.decode(errors="replace")
            try:
                obj = json.loads(text)
            except json.JSONDecodeError:
                continue

            if obj.get("type") != "tracker_telemetry":
                continue

            print(addr, json.dumps(obj, separators=(",", ":")))
    except KeyboardInterrupt:
        print("\nstopped")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
