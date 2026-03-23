#!/usr/bin/env python3
# Copyright (c) Elias Bachaalany
# SPDX-License-Identifier: MIT
"""Build Cling/LLVM from source (portable).

Usage: python scripts/build_cling.py [options] [--configure-only] [--build-only] [-j N]

  --llvm-src <path>   Path to llvm-project/llvm (default: auto-detect)
  --cling-src <path>  Path to cling source (default: auto-detect)
  --configure-only    Only run CMake configure, skip build
  --build-only        Only run build, skip configure
  --ninja             Use Ninja generator (requires Developer Command Prompt on Windows)
  -j N                Parallel build jobs (0 = auto)

Auto-detection order for --llvm-src / --cling-src:
  1. CLI flag
  2. LLVM_SRC_DIR / CLING_SRC_DIR environment variable
  3. external/llvm-project/llvm and external/cling-src (clinglite standalone clone)
  4. ../llvm-project/llvm and ../cling-src (monorepo sibling layout)

Handles:
  1. System checks (cmake, compiler)
  2. Applying cling-src patches
  3. CMake configure with LLVM_EXTERNAL_CLING_SOURCE_DIR (no symlink/junction)
  4. Build

On Windows, defaults to the Visual Studio generator (no vcvarsall needed).
Use --ninja to opt-in to Ninja (requires a Developer Command Prompt).
"""

import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT_DIR / "build-cling"


def find_llvm_src() -> Path:
    """Locate LLVM source directory."""
    env = os.environ.get("LLVM_SRC_DIR")
    if env:
        return Path(env).resolve()
    # clinglite standalone: external/llvm-project/llvm
    candidate = ROOT_DIR / "external" / "llvm-project" / "llvm"
    if (candidate / "CMakeLists.txt").exists():
        return candidate
    # Monorepo sibling: ../llvm-project/llvm
    candidate = ROOT_DIR.parent / "llvm-project" / "llvm"
    if (candidate / "CMakeLists.txt").exists():
        return candidate
    return Path()


def find_cling_src() -> Path:
    """Locate cling source directory."""
    env = os.environ.get("CLING_SRC_DIR")
    if env:
        return Path(env).resolve()
    # clinglite standalone: external/cling-src
    candidate = ROOT_DIR / "external" / "cling-src"
    if (candidate / "CMakeLists.txt").exists():
        return candidate
    # Monorepo sibling: ../cling-src
    candidate = ROOT_DIR.parent / "cling-src"
    if (candidate / "CMakeLists.txt").exists():
        return candidate
    return Path()


LLVM_SRC = find_llvm_src()
CLING_SRC = find_cling_src()

MIN_CMAKE_VERSION = (3, 20)


# ── System checks ────────────────────────────────────────────────────────


def check_cmake() -> str:
    """Check that cmake is available and meets minimum version."""
    cmake = shutil.which("cmake")
    if not cmake:
        print("ERROR: cmake not found.")
        print("  Install: https://cmake.org/download/")
        sys.exit(1)

    result = subprocess.run(
        ["cmake", "--version"], capture_output=True, text=True)
    match = re.search(r"cmake version (\d+)\.(\d+)", result.stdout)
    if match:
        major, minor = int(match.group(1)), int(match.group(2))
        if (major, minor) < MIN_CMAKE_VERSION:
            print(f"ERROR: cmake {major}.{minor} found, "
                  f"need >= {MIN_CMAKE_VERSION[0]}.{MIN_CMAKE_VERSION[1]}")
            sys.exit(1)
        print(f"  cmake {major}.{minor} ... OK")
    return cmake


def check_compiler() -> None:
    """Verify a C++ compiler is available."""
    if platform.system() == "Windows":
        if shutil.which("cl"):
            print("  cl.exe (MSVC) ... OK")
        else:
            print("  cl.exe ... NOT FOUND")
            print("  NOTE: If using --ninja, run from a VS Developer Command Prompt")
    else:
        for cc in ("c++", "g++", "clang++"):
            if shutil.which(cc):
                print(f"  {cc} ... OK")
                return
        print("WARNING: No C++ compiler found in PATH")


def check_submodules() -> None:
    """Verify source directories are found."""
    ok = True
    if not LLVM_SRC.exists():
        print(f"ERROR: LLVM source not found at {LLVM_SRC}")
        print("  Fix: --llvm-src <path>, or set LLVM_SRC_DIR, "
              "or: git submodule update --init --recursive")
        ok = False
    if not CLING_SRC.exists():
        print(f"ERROR: Cling source not found at {CLING_SRC}")
        print("  Fix: --cling-src <path>, or set CLING_SRC_DIR, "
              "or: git submodule update --init --recursive")
        ok = False
    if not ok:
        sys.exit(1)
    print(f"  llvm-src  ... {LLVM_SRC}")
    print(f"  cling-src ... {CLING_SRC}")


def detect_arch() -> str:
    """Return LLVM target triple architecture."""
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        return "X86"
    elif machine in ("aarch64", "arm64"):
        return "AArch64"
    return "X86"


# ── Build steps ──────────────────────────────────────────────────────────


def apply_patches() -> None:
    """Run apply_patches.py."""
    script = ROOT_DIR / "scripts" / "apply_patches.py"
    if script.exists():
        print("\n=== Applying patches ===\n")
        subprocess.run([sys.executable, str(script), "--all"], check=True)


def configure(use_ninja: bool) -> None:
    """Run CMake configure."""
    print("\n=== CMake Configure ===\n")

    cmd = ["cmake"]

    if use_ninja:
        ninja = shutil.which("ninja")
        if not ninja:
            print("ERROR: --ninja requested but ninja not found in PATH")
            print("  pip install ninja")
            sys.exit(1)
        cmd += ["-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}"]
    elif platform.system() == "Windows":
        cmd += ["-G", "Visual Studio 17 2022", "-A", "x64"]

    cmd += [
        "-S", str(LLVM_SRC),
        "-B", str(BUILD_DIR),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DLLVM_ENABLE_PROJECTS=clang",
        f"-DLLVM_TARGETS_TO_BUILD={detect_arch()}",
        "-DLLVM_ENABLE_RTTI=OFF",
        "-DLLVM_ENABLE_EH=OFF",
        "-DLLVM_INCLUDE_TESTS=OFF",
        "-DLLVM_INCLUDE_EXAMPLES=OFF",
        "-DLLVM_INCLUDE_BENCHMARKS=OFF",
        "-DLLVM_INCLUDE_DOCS=OFF",
        "-DLLVM_ENABLE_ZSTD=OFF",
        "-DLLVM_ENABLE_ZLIB=OFF",
        "-DLLVM_EXTERNAL_PROJECTS=cling",
        f"-DLLVM_EXTERNAL_CLING_SOURCE_DIR={CLING_SRC}",
        "-DCMAKE_CXX_STANDARD=17",
    ]
    subprocess.run(cmd, check=True)


def build(jobs: int) -> None:
    """Run CMake build."""
    print("\n=== Building ===\n")
    cmd = ["cmake", "--build", str(BUILD_DIR), "--config", "Release"]
    if jobs:
        cmd += ["--parallel", str(jobs)]
    subprocess.run(cmd, check=True)


# ── Main ─────────────────────────────────────────────────────────────────


def main() -> int:
    import argparse
    parser = argparse.ArgumentParser(
        description="Build Cling/LLVM from source (portable)")
    parser.add_argument("--configure-only", action="store_true",
                        help="Only run CMake configure, skip build")
    parser.add_argument("--build-only", action="store_true",
                        help="Only run build, skip configure")
    parser.add_argument("--ninja", action="store_true",
                        help="Use Ninja instead of VS generator "
                             "(requires Developer Command Prompt on Windows)")
    parser.add_argument("-j", type=int, default=0,
                        help="Parallel build jobs (0 = auto)")
    parser.add_argument("--llvm-src", type=Path, default=None,
                        help="Path to llvm-project/llvm (default: auto-detect)")
    parser.add_argument("--cling-src", type=Path, default=None,
                        help="Path to cling source (default: auto-detect)")
    args = parser.parse_args()

    global LLVM_SRC, CLING_SRC
    if args.llvm_src:
        LLVM_SRC = args.llvm_src.resolve()
    if args.cling_src:
        CLING_SRC = args.cling_src.resolve()

    print("=== System checks ===\n")
    check_cmake()
    check_compiler()
    check_submodules()

    if not args.build_only:
        apply_patches()
        configure(use_ninja=args.ninja)

    if not args.configure_only:
        build(args.j)

    print("\n=== Done ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
