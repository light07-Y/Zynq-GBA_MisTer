#!/usr/bin/env python3
"""One-shot repair utility for FatFs UTF-8 LFN settings in this workspace."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_CMAKELISTS = REPO_ROOT / "src/ps/app/src/CMakeLists.txt"
FATFS_CONFIG_SCRIPT = REPO_ROOT / "scripts/fatfs/configure_utf8_lfn.py"

MAIN_BSP_DIR = REPO_ROOT / "src/ps/platform/ps7_cortexa9_0/freertos_ps7_cortexa9_0/bsp"
EXPORT_BSP_DIR = REPO_ROOT / "src/ps/platform/export/platform/sw/freertos_ps7_cortexa9_0"

MAIN_BSP_YAML = MAIN_BSP_DIR / "bsp.yaml"
EXPORT_BSP_YAML = EXPORT_BSP_DIR / "bsp.yaml"
REPO_YAML = REPO_ROOT / "src/ps/_ide/.wsdata/.repo.yaml"

APP_SRC_DIR = REPO_ROOT / "src/ps/app/src"
APP_BUILD_DIR = REPO_ROOT / "src/ps/app/build"


def _run(cmd: list[str], cwd: Path | None = None, required: bool = True) -> int:
    printable = " ".join(cmd)
    print(f"[run] {printable}")
    result = subprocess.run(cmd, cwd=str(cwd) if cwd else None)
    if required and result.returncode != 0:
        print(f"[error] command failed with exit code {result.returncode}")
        raise SystemExit(result.returncode)
    return result.returncode


def _run_empyro(args: list[str], required: bool = True) -> int:
    command = "empyro.bat " + subprocess.list2cmdline(args)
    return _run(["cmd.exe", "/C", command], cwd=REPO_ROOT, required=required)


def _set_xilffs_use_lfn_2_in_bsp_yaml(path: Path) -> str:
    if not path.exists():
        return "not-found"

    text = path.read_text(encoding="utf-8", errors="replace")
    pattern = re.compile(
        r"(XILFFS_use_lfn:\n(?:[ \t].*\n)*?[ \t]+value:\s*)'[^']*'",
        re.MULTILINE,
    )
    match = pattern.search(text)
    if not match:
        return "pattern-not-found"

    current_value_match = re.search(r"'([^']*)'", match.group(0))
    current_value = current_value_match.group(1) if current_value_match else ""

    new_text, count = pattern.subn(r"\g<1>'2'", text, count=1)
    if count == 0:
        return "pattern-not-found"
    if new_text != text:
        path.write_text(new_text, encoding="utf-8", newline="\n")
        return "updated"
    if current_value == "2":
        return "already-ok"
    return "already-ok"


def _ensure_cmake_auto_apply(path: Path) -> bool:
    text = path.read_text(encoding="utf-8", errors="replace")
    original = text

    if (
        "--apply" in text
        and "PS_APP_FATFS_UTF8_LFN_APPLY" in text
        and "set(PS_APP_FATFS_UTF8_LFN_SCRIPT" in text
    ):
        return False

    text = text.replace(
        "find_package(Python3 COMPONENTS Interpreter REQUIRED)\n",
        "find_package(Python3 COMPONENTS Interpreter REQUIRED)\n"
        "set(PS_APP_FATFS_UTF8_LFN_SCRIPT \"${CMAKE_CURRENT_SOURCE_DIR}/../../../../scripts/fatfs/configure_utf8_lfn.py\")\n",
        1,
    )

    text = text.replace(
        '"${CMAKE_CURRENT_SOURCE_DIR}/../../../../scripts/fatfs/configure_utf8_lfn.py"',
        '"${PS_APP_FATFS_UTF8_LFN_SCRIPT}"',
    )
    text = text.replace("--check", "--apply")
    text = text.replace("PS_APP_FATFS_UTF8_LFN_CHECK", "PS_APP_FATFS_UTF8_LFN_APPLY")
    text = text.replace(
        "FatFs BSP is not configured for UTF-8 LFN paths.",
        "Failed to configure FatFs BSP for UTF-8 LFN paths.",
    )

    if text != original:
        path.write_text(text, encoding="utf-8", newline="\n")
        return True
    return False


def repair(args: argparse.Namespace) -> int:
    print(f"[info] repo: {REPO_ROOT}")

    if not args.skip_cmake_fix:
        changed = _ensure_cmake_auto_apply(APP_CMAKELISTS)
        print(f"[info] CMake auto-apply: {'updated' if changed else 'already-ok'}")

    yaml_changes: list[tuple[Path, str]] = []
    for yaml_path in (MAIN_BSP_YAML, EXPORT_BSP_YAML):
        status = _set_xilffs_use_lfn_2_in_bsp_yaml(yaml_path)
        yaml_changes.append((yaml_path, status))
    for yaml_path, status in yaml_changes:
        print(f"[info] bsp.yaml XILFFS_use_lfn=2: {yaml_path} -> {status}")

    if not args.skip_bsp_config:
        if REPO_YAML.exists():
            common = ["-st", "xilffs", "XILFFS_use_lfn:2", "-r", str(REPO_YAML)]
            _run_empyro(["config_bsp", "-d", str(MAIN_BSP_DIR)] + common, required=False)
            if args.try_export_bsp_config:
                _run_empyro(["config_bsp", "-d", str(EXPORT_BSP_DIR)] + common, required=False)
        else:
            print(f"[warn] repo yaml not found, skipped config_bsp: {REPO_YAML}")

    _run([sys.executable, str(FATFS_CONFIG_SCRIPT), "--apply"], cwd=REPO_ROOT)
    _run([sys.executable, str(FATFS_CONFIG_SCRIPT), "--check"], cwd=REPO_ROOT)

    if args.build_bsp:
        _run_empyro(["build_bsp", "-d", str(MAIN_BSP_DIR)], required=False)

    if args.build_app:
        _run_empyro(["build_app", "-s", str(APP_SRC_DIR), "-b", str(APP_BUILD_DIR)])

    print("[done] FatFs UTF-8 LFN repair completed.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Repair recurring FatFs UTF-8 LFN mismatches after BSP regenerations."
    )
    parser.add_argument("--build-bsp", action="store_true", help="Also run empyro.bat build_bsp for the main freertos BSP")
    parser.add_argument("--build-app", action="store_true", help="Also run empyro.bat build_app for src/ps/app")
    parser.add_argument("--try-export-bsp-config", action="store_true", help="Also try config_bsp on export domain (can fail in some setups)")
    parser.add_argument("--skip-cmake-fix", action="store_true", help="Do not touch src/ps/app/src/CMakeLists.txt")
    parser.add_argument("--skip-bsp-config", action="store_true", help="Do not call empyro.bat config_bsp")
    args = parser.parse_args()
    return repair(args)


if __name__ == "__main__":
    raise SystemExit(main())
