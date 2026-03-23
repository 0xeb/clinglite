#!/usr/bin/env python3
# Copyright (c) Elias Bachaalany
# SPDX-License-Identifier: MIT
"""Build clinglite using CMake presets (portable).

Usage:
  python scripts/build_clinglite.py              # static (VS on Windows, Ninja on Linux)
  python scripts/build_clinglite.py --shared     # shared variant
  python scripts/build_clinglite.py --fat        # fat-static (Linux/macOS, all-in-one)
  python scripts/build_clinglite.py --ninja      # force Ninja on Windows

Assumes build-cling/ already exists (run build_cling.py first).
"""

import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
BUILD_CLING = ROOT_DIR / "build-cling"


def check_prerequisites() -> None:
    if not shutil.which("cmake"):
        print("ERROR: cmake not found")
        sys.exit(1)
    if not BUILD_CLING.exists():
        print(f"ERROR: {BUILD_CLING} not found")
        print("Run: python scripts/build_cling.py")
        sys.exit(1)


def pick_preset(shared: bool, fat: bool, ninja: bool) -> str:
    """Choose the right CMake preset for this platform and flags."""
    if fat:
        return "fat-static"

    is_windows = platform.system() == "Windows"
    use_vs = is_windows and not ninja

    if use_vs:
        return "vs-shared" if shared else "vs-static"
    else:
        return "ninja-shared" if shared else "ninja-static"


def build(preset: str, config: str) -> None:
    """Configure and build clinglite with the given preset."""
    print(f"\n=== Configuring clinglite (preset: {preset}) ===\n")
    subprocess.run(["cmake", "--preset", preset], cwd=str(ROOT_DIR), check=True)

    build_dir_name = {
        "vs-static": "build-vs-static",
        "vs-shared": "build-vs-shared",
        "ninja-static": "build-ninja-static",
        "ninja-shared": "build-ninja-shared",
        "fat-static": "build-fat-static",
    }[preset]
    build_dir = ROOT_DIR / build_dir_name

    print(f"\n=== Building clinglite ({config}) ===\n")
    subprocess.run(
        ["cmake", "--build", str(build_dir), "--config", config],
        check=True,
    )
    print(f"\n=== clinglite ({preset}, {config}) SUCCEEDED ===")


def main() -> int:
    import argparse
    parser = argparse.ArgumentParser(description="Build clinglite")
    parser.add_argument("--shared", action="store_true",
                        help="Build shared (DLL) variant")
    parser.add_argument("--fat", action="store_true",
                        help="Build fat-static (Linux/macOS all-in-one) variant")
    parser.add_argument("--ninja", action="store_true",
                        help="Use Ninja generator even on Windows")
    parser.add_argument("--config", default="Release",
                        help="Build configuration for VS multi-config "
                             "(default: Release)")
    args = parser.parse_args()

    check_prerequisites()

    preset = pick_preset(shared=args.shared, fat=args.fat, ninja=args.ninja)
    build(preset, args.config)

    print("\n=== Done ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
