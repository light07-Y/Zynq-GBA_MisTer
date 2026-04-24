#!/usr/bin/env python3
"""Patch or verify the generated Xilinx FatFs BSP for UTF-8 LFN paths."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
FREERTOS_DOMAIN = REPO_ROOT / "src/ps/platform/ps7_cortexa9_0/freertos_ps7_cortexa9_0"
EXPORT_DOMAIN = REPO_ROOT / "src/ps/platform/export/platform/sw/freertos_ps7_cortexa9_0"


def _replace_define(text: str, name: str, value: str) -> tuple[str, bool]:
    pattern = re.compile(rf"^#define\s+{re.escape(name)}\s+.*$", re.MULTILINE)
    replacement = f"#define {name}\t{value}"
    new_text, count = pattern.subn(replacement, text)
    return new_text, count > 0


def _patch_ffconf(path: Path, apply: bool) -> list[str]:
    issues: list[str] = []
    if not path.exists():
        return issues

    text = path.read_text(encoding="utf-8", errors="replace")
    patched = text
    for name, value in (
        ("FF_CODE_PAGE", "437"),
        ("FF_MAX_LFN", "255"),
        ("FF_LFN_UNICODE", "2"),
        ("FF_LFN_BUF", "765"),
        ("FF_SFN_BUF", "34"),
    ):
        patched, found = _replace_define(patched, name, value)
        if not found:
            issues.append(f"{path}: missing #define {name}")

    if apply and patched != text:
        path.write_text(patched, encoding="utf-8", newline="\n")

    check_text = patched if apply else text
    expectations = {
        "FF_CODE_PAGE": "437",
        "FF_MAX_LFN": "255",
        "FF_LFN_UNICODE": "2",
        "FF_LFN_BUF": "765",
        "FF_SFN_BUF": "34",
    }
    for name, value in expectations.items():
        if not re.search(rf"^#define\s+{re.escape(name)}\s+{re.escape(value)}\b", check_text, re.MULTILINE):
            issues.append(f"{path}: expected {name}={value}")
    return issues


def _patch_xilffs_config(path: Path, apply: bool) -> list[str]:
    issues: list[str] = []
    if not path.exists():
        return issues

    text = path.read_text(encoding="utf-8", errors="replace")
    patched = re.sub(r"^/\* #undef FILE_SYSTEM_USE_LFN \*/$", "#define FILE_SYSTEM_USE_LFN 2", text, flags=re.MULTILINE)
    patched = re.sub(r"^#define\s+FILE_SYSTEM_USE_LFN\s+.*$", "#define FILE_SYSTEM_USE_LFN 2", patched, flags=re.MULTILINE)

    if apply and patched != text:
        path.write_text(patched, encoding="utf-8", newline="\n")

    check_text = patched if apply else text
    if not re.search(r"^#define\s+FILE_SYSTEM_USE_LFN\s+2\b", check_text, re.MULTILINE):
        issues.append(f"{path}: expected FILE_SYSTEM_USE_LFN=2")
    return issues


def _candidate_files() -> tuple[list[Path], list[Path]]:
    ffconfs = [
        FREERTOS_DOMAIN / "bsp/include/ffconf.h",
        FREERTOS_DOMAIN / "bsp/libsrc/xilffs/src/include/ffconf.h",
        FREERTOS_DOMAIN / "bsp/libsrc/build_configs/gen_bsp/include/ffconf.h",
        EXPORT_DOMAIN / "include/ffconf.h",
    ]
    xilffs_configs = [
        FREERTOS_DOMAIN / "bsp/include/xilffs_config.h",
        FREERTOS_DOMAIN / "bsp/libsrc/build_configs/gen_bsp/include/xilffs_config.h",
        EXPORT_DOMAIN / "include/xilffs_config.h",
    ]
    return ffconfs, xilffs_configs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true", help="Patch generated BSP files in-place")
    parser.add_argument("--check", action="store_true", help="Only verify generated BSP files")
    args = parser.parse_args()

    if not args.apply and not args.check:
        args.check = True

    ffconfs, xilffs_configs = _candidate_files()
    issues: list[str] = []
    for path in ffconfs:
        issues.extend(_patch_ffconf(path, args.apply))
    for path in xilffs_configs:
        issues.extend(_patch_xilffs_config(path, args.apply))

    if issues:
        for issue in issues:
            print(f"[fatfs-utf8-lfn] {issue}", file=sys.stderr)
        print("[fatfs-utf8-lfn] run: python scripts/fatfs/configure_utf8_lfn.py --apply", file=sys.stderr)
        return 1

    mode = "patched" if args.apply else "verified"
    print(f"[fatfs-utf8-lfn] {mode}: FF_USE_LFN=2 FF_LFN_UNICODE=2 FF_CODE_PAGE=437")
    return 0


if __name__ == "__main__":
    sys.exit(main())
