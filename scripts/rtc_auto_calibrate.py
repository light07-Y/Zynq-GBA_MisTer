#!/usr/bin/env python3
"""交互式 RTC 自动校准工具。"""

from __future__ import annotations

import datetime as dt
import re
import time
from typing import Any


BAUD_RATE = 115200
READ_TIMEOUT_S = 1.2
STARTUP_WAIT_S = 1.0
UNCERT_S = 1
BJ_TZ = dt.timezone(dt.timedelta(hours=8), name="UTC+08")


def unix_to_bj_text(unix_s: int) -> str:
    try:
        return dt.datetime.fromtimestamp(unix_s, tz=dt.timezone.utc).astimezone(BJ_TZ).strftime(
            "%Y-%m-%d %H:%M:%S"
        )
    except (OverflowError, OSError, ValueError):
        return "无效时间"


def host_bj_text() -> str:
    return dt.datetime.now(BJ_TZ).strftime("%Y-%m-%d %H:%M:%S")


def wait_any_key() -> None:
    print("\n按任意键退出程序...")
    try:
        import msvcrt

        msvcrt.getch()
    except ImportError:
        input()


def read_serial_lines(ser: Any, timeout_s: float) -> list[str]:
    lines: list[str] = []
    end_time = time.monotonic() + timeout_s
    while time.monotonic() < end_time:
        raw = ser.readline()
        if raw:
            text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            if text:
                lines.append(text)
        else:
            time.sleep(0.02)
    return lines


def send_cmd(ser: Any, cmd: str, read_timeout_s: float = READ_TIMEOUT_S) -> list[str]:
    ser.write((cmd + "\r\n").encode("utf-8"))
    ser.flush()
    return read_serial_lines(ser, read_timeout_s)


def choose_serial_port() -> str | None:
    try:
        from serial.tools import list_ports
    except ImportError:
        print("错误：未安装 pyserial，请先执行：pip install pyserial")
        return None

    ports = list(list_ports.comports())
    if not ports:
        print("未发现可用串口。")
        return None

    print("可用串口：")
    for index, port in enumerate(ports, start=1):
        description = port.description or "无描述"
        print(f"  {index}. {port.device}  {description}")

    while True:
        choice = input("\n请输入要使用的串口编号：").strip()
        try:
            index = int(choice)
        except ValueError:
            print("输入无效，请输入列表中的数字。")
            continue

        if 1 <= index <= len(ports):
            return ports[index - 1].device

        print("编号超出范围，请重新输入。")


def board_bj_text_from_status(lines: list[str]) -> str | None:
    pattern = re.compile(r"\b(?:current|est)=([0-9]+)\b")
    for line in lines:
        match = pattern.search(line)
        if match:
            return unix_to_bj_text(int(match.group(1)))
    return None


def calibrate(port: str) -> int:
    try:
        import serial
    except ImportError:
        print("错误：未安装 pyserial，请先执行：pip install pyserial")
        return 2

    print(f"\n正在打开串口 {port}，波特率 {BAUD_RATE}...")
    try:
        with serial.Serial(port=port, baudrate=BAUD_RATE, timeout=0.12, write_timeout=1.0) as ser:
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            time.sleep(STARTUP_WAIT_S)

            ser.write(b"\r\n")
            ser.flush()
            _ = read_serial_lines(ser, 0.5)

            unix_now = int(time.time())
            print("正在校准开发板 RTC...")
            _ = send_cmd(ser, f"rtc sync {unix_now} {UNCERT_S}")
            status_lines = send_cmd(ser, "rtc status")

            board_bj = board_bj_text_from_status(status_lines)
            print("\n校准完成。")
            print(f"上位机北京时间：{host_bj_text()}")
            if board_bj is not None:
                print(f"开发板本机北京时间：{board_bj}")
            else:
                print("开发板本机北京时间：未能从 rtc status 返回中解析")
    except serial.SerialException as exc:
        print(f"串口错误：{exc}")
        return 1

    return 0


def main() -> int:
    port = choose_serial_port()
    if port is None:
        wait_any_key()
        return 1

    result = calibrate(port)
    wait_any_key()
    return result


if __name__ == "__main__":
    raise SystemExit(main())
