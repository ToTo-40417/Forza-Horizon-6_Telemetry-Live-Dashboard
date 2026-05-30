#!/usr/bin/env python3
"""Send synthetic Forza Horizon 6 Data Out UDP packets for ESP32 UI testing."""

from __future__ import annotations

import argparse
import math
import socket
import struct
import time


PACKET_SIZE = 324


def put_i32(buf: bytearray, offset: int, value: int) -> int:
    struct.pack_into("<i", buf, offset, value)
    return offset + 4


def put_u32(buf: bytearray, offset: int, value: int) -> int:
    struct.pack_into("<I", buf, offset, value)
    return offset + 4


def put_f32(buf: bytearray, offset: int, value: float) -> int:
    struct.pack_into("<f", buf, offset, value)
    return offset + 4


def build_packet(t: float, timestamp_ms: int) -> bytes:
    b = bytearray(PACKET_SIZE)
    o = 0
    speed_mps = 18.0 + 42.0 * (0.5 + 0.5 * math.sin(t * 0.43))
    rpm = 900.0 + 6200.0 * (0.5 + 0.5 * math.sin(t * 1.9))
    throttle = int(150 + 105 * max(0.0, math.sin(t * 1.2)))
    brake = int(90 * max(0.0, math.sin(t * 0.63 + 2.2)))
    gear = max(1, min(8, int(speed_mps * 3.6 / 38) + 1))
    steer = int(92 * math.sin(t * 0.9))
    boost = max(0.0, 12.0 * math.sin(t * 1.4))

    o = put_i32(b, o, 1)
    o = put_u32(b, o, timestamp_ms)
    for value in (7600.0, 850.0, rpm):
        o = put_f32(b, o, value)
    for value in (math.sin(t) * 3.0, 0.4 * math.sin(t * 0.7), 1.8 * math.cos(t * 0.8)):
        o = put_f32(b, o, value)
    for value in (0.0, 0.0, speed_mps):
        o = put_f32(b, o, value)
    for value in (0.05 * math.sin(t), 0.12 * math.sin(t * 0.5), 0.04 * math.cos(t)):
        o = put_f32(b, o, value)
    for value in (math.sin(t * 0.22), math.sin(t * 0.17) * 0.04, math.sin(t * 0.31) * 0.08):
        o = put_f32(b, o, value)
    for i in range(4):
        o = put_f32(b, o, 0.38 + 0.12 * math.sin(t * 1.1 + i))
    for i in range(4):
        o = put_f32(b, o, 0.08 + 0.28 * max(0.0, math.sin(t * 1.3 + i * 0.8)))
    for i in range(4):
        o = put_f32(b, o, speed_mps * 2.7 + math.sin(t + i))
    for i in range(4):
        o = put_i32(b, o, 1 if math.sin(t * 0.9 + i) > 0.92 else 0)
    for _ in range(4):
        o = put_i32(b, o, 0)
    for i in range(4):
        o = put_f32(b, o, 0.12 * max(0.0, math.sin(t * 2 + i)))
    for i in range(4):
        o = put_f32(b, o, 0.05 * math.sin(t * 0.8 + i))
    for i in range(4):
        o = put_f32(b, o, 0.08 + 0.18 * max(0.0, math.sin(t * 1.4 + i)))
    for i in range(4):
        o = put_f32(b, o, 0.06 + 0.02 * math.sin(t + i))
    for value in (4521, 6, 832, 2, 6):
        o = put_i32(b, o, value)
    o = put_u32(b, o, 12)
    o = put_f32(b, o, 0.0)
    o = put_f32(b, o, 0.0)
    for value in (1200.0 * math.sin(t * 0.05), 0.0, 1200.0 * math.cos(t * 0.05)):
        o = put_f32(b, o, value)
    for value in (speed_mps, 310000.0 * throttle / 255.0, 520.0 * throttle / 255.0):
        o = put_f32(b, o, value)
    for i in range(4):
        o = put_f32(b, o, 74.0 + 13.0 * max(0.0, math.sin(t * 0.7 + i)))
    for value in (boost, 0.72, timestamp_ms * speed_mps / 1000.0, 82.41, 86.28, 24.0 + t % 70.0, t):
        o = put_f32(b, o, value)
    struct.pack_into("<H", b, o, int(t // 90) % 5)
    o += 2
    for value in (1, throttle, brake, 0, 0, gear):
        struct.pack_into("<B", b, o, value)
        o += 1
    struct.pack_into("<b", b, o, steer)
    o += 1
    struct.pack_into("<b", b, o, int(40 * math.sin(t * 0.4)))
    o += 1
    struct.pack_into("<b", b, o, int(30 * math.sin(t * 0.6)))
    return bytes(b)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=7777)
    parser.add_argument("--hz", type=float, default=60.0)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    start = time.monotonic()
    interval = 1.0 / args.hz
    print(f"Sending FH6-like packets to {args.host}:{args.port} at {args.hz:g} Hz")
    while True:
        now = time.monotonic() - start
        packet = build_packet(now, int(now * 1000) & 0xFFFFFFFF)
        sock.sendto(packet, (args.host, args.port))
        time.sleep(interval)


if __name__ == "__main__":
    main()
