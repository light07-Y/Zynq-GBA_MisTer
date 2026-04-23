#!/usr/bin/env python3
"""
Auto RTC calibrator for Zynq-GBA serial console.

It periodically sends:
  rtc sync <unix> <uncert_s>
  rtc status

Unix time is taken from host UTC clock (time.time()).
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
import sys
import time
from typing import Any, List


BJ_TZ = dt.timezone(dt.timedelta(hours=8), name="UTC+08")


def utc_now_text() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%SZ")


def bj_now_text() -> str:
    return dt.datetime.now(BJ_TZ).strftime("%Y-%m-%d %H:%M:%S %z")


def unix_to_utc_text(unix_s: int) -> str:
    try:
        return dt.datetime.fromtimestamp(unix_s, tz=dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%SZ")
    except (OverflowError, OSError, ValueError):
        return "invalid"


def unix_to_bj_text(unix_s: int) -> str:
    try:
        return dt.datetime.fromtimestamp(unix_s, tz=dt.timezone.utc).astimezone(BJ_TZ).strftime(
            "%Y-%m-%d %H:%M:%S %z"
        )
    except (OverflowError, OSError, ValueError):
        return "invalid"


def read_serial_lines(ser: Any, timeout_s: float) -> List[str]:
    """Read available lines up to timeout."""
    lines: List[str] = []
    end_time = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < end_time:
        raw = ser.readline()
        if raw:
            text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            if text:
                lines.append(text)
        else:
            # Avoid busy-loop on short timeout.
            time.sleep(0.02)
    return lines


def send_cmd(ser: Any, cmd: str, read_timeout_s: float) -> List[str]:
    ser.write((cmd + "\r\n").encode("utf-8"))
    ser.flush()
    return read_serial_lines(ser, read_timeout_s)


def print_lines(tag: str, lines: List[str]) -> None:
    for line in lines:
        print(f"{tag} {line}")


def print_status_time_decode(lines: List[str]) -> None:
    pattern = re.compile(r"\b(current|est|last_cal_unix)=([0-9]+)\b")
    fields = {}

    for line in lines:
        for key, value_text in pattern.findall(line):
            try:
                fields[key] = int(value_text)
            except ValueError:
                continue

    if not fields:
        return

    print("  [BJ] decoded unix fields:")
    for key in ("current", "est", "last_cal_unix"):
        if key in fields:
            unix_s = fields[key]
            print(
                f"    {key}={unix_s} | UTC={unix_to_utc_text(unix_s)} | "
                f"BJ={unix_to_bj_text(unix_s)}"
            )


def one_sync(ser: Any, uncert_s: int, read_timeout_s: float, with_status: bool) -> None:
    unix_now = int(time.time())
    cmd = f"rtc sync {unix_now} {uncert_s}"
    print(
        f"[UTC {utc_now_text()} | BJ {bj_now_text()}] -> {cmd} "
        f"(BJ target {unix_to_bj_text(unix_now)})"
    )
    out = send_cmd(ser, cmd, read_timeout_s)
    print_lines("  ", out)

    if with_status:
        print(f"[UTC {utc_now_text()} | BJ {bj_now_text()}] -> rtc status")
        status_out = send_cmd(ser, "rtc status", read_timeout_s)
        print_lines("  ", status_out)
        print_status_time_decode(status_out)


def list_available_ports() -> None:
    try:
        from serial.tools import list_ports
    except ImportError:
        print(
            "Missing dependency: pyserial\n"
            "Install with: pip install pyserial",
            file=sys.stderr,
        )
        raise SystemExit(2)

    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return
    print("Available serial ports:")
    for p in ports:
        desc = p.description or ""
        hwid = p.hwid or ""
        print(f"  {p.device}  {desc}  {hwid}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Automatically calibrate target RTC through serial console."
    )
    parser.add_argument("--port", help="Serial port, e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument(
        "--interval",
        type=float,
        default=600.0,
        help="Calibration interval seconds (default: 600)",
    )
    parser.add_argument(
        "--uncert",
        type=int,
        default=1,
        help="uncert_s for 'rtc sync <unix> <uncert_s>' (default: 1)",
    )
    parser.add_argument(
        "--read-timeout",
        type=float,
        default=1.2,
        help="Seconds to collect command output after each send (default: 1.2)",
    )
    parser.add_argument(
        "--startup-wait",
        type=float,
        default=1.0,
        help="Wait seconds after opening serial before first command (default: 1.0)",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="Run one calibration only, then exit",
    )
    parser.add_argument(
        "--no-status",
        action="store_true",
        help="Do not send 'rtc status' after each sync",
    )
    parser.add_argument(
        "--list-ports",
        action="store_true",
        help="List local serial ports and exit",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.list_ports:
        list_available_ports()
        return 0

    if not args.port:
        print("Error: --port is required unless --list-ports is used.", file=sys.stderr)
        return 2

    if args.interval <= 0:
        print("Error: --interval must be > 0.", file=sys.stderr)
        return 2
    if args.uncert < 0:
        print("Error: --uncert must be >= 0.", file=sys.stderr)
        return 2
    if args.interval < 300:
        print(
            "Warning: interval < 300s. Firmware drift learning is configured to update"
            " only when sync interval >= 300s.",
            file=sys.stderr,
        )

    print(
        f"Opening {args.port} @ {args.baud} baud, interval={args.interval}s, "
        f"uncert={args.uncert}s"
    )

    try:
        import serial
    except ImportError:
        print(
            "Missing dependency: pyserial\n"
            "Install with: pip install pyserial",
            file=sys.stderr,
        )
        return 2

    try:
        with serial.Serial(
            port=args.port,
            baudrate=args.baud,
            timeout=0.12,
            write_timeout=1.0,
        ) as ser:
            # Clean stale bytes so logs are from this run.
            ser.reset_input_buffer()
            ser.reset_output_buffer()

            if args.startup_wait > 0:
                time.sleep(args.startup_wait)

            # Nudge prompt once, then drain.
            ser.write(b"\r\n")
            ser.flush()
            _ = read_serial_lines(ser, 0.5)

            next_tick = time.monotonic()
            while True:
                one_sync(
                    ser=ser,
                    uncert_s=args.uncert,
                    read_timeout_s=args.read_timeout,
                    with_status=not args.no_status,
                )
                if args.once:
                    break

                next_tick += args.interval
                sleep_s = next_tick - time.monotonic()
                if sleep_s > 0:
                    time.sleep(sleep_s)
                else:
                    # If delayed, avoid drift by resyncing schedule from now.
                    next_tick = time.monotonic()
    except KeyboardInterrupt:
        print("\nStopped by user.")
        return 0
    except serial.SerialException as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
