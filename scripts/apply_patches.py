#!/usr/bin/env python3
# Copyright (c) Elias Bachaalany
# SPDX-License-Identifier: MIT
"""Apply required Cling source patches.

Usage: python scripts/apply_patches.py [--all] [--cling-src <path>]

  --all            Also apply optional patches (undo crash fix, etc.)
  --cling-src <p>  Path to cling source directory (default: auto-detect)

Auto-detection order:
  1. --cling-src flag
  2. CLING_SRC_DIR environment variable
  3. external/cling-src (clinglite standalone clone)
  4. ../cling-src (monorepo sibling layout)

Safe to run multiple times — already-applied patches are skipped.
"""

import os
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent
PATCHES_DIR = ROOT_DIR / "patches"


def find_cling_src() -> Path:
    """Locate cling-src directory."""
    # 1. --cling-src flag
    for i, arg in enumerate(sys.argv):
        if arg == "--cling-src" and i + 1 < len(sys.argv):
            return Path(sys.argv[i + 1]).resolve()

    # 2. Environment variable
    env = os.environ.get("CLING_SRC_DIR")
    if env:
        return Path(env).resolve()

    # 3. clinglite standalone: external/cling-src
    candidate = ROOT_DIR / "external" / "cling-src"
    if (candidate / ".git").exists():
        return candidate

    # 4. Monorepo sibling: ../cling-src
    candidate = ROOT_DIR.parent / "cling-src"
    if (candidate / ".git").exists():
        return candidate

    return Path()  # empty — will fail validation


CLING_SRC = find_cling_src()

REQUIRED_PATCHES = [
    ("0001-noruntime-atexit-guard.patch",
     "required: guard atexit interception for clean PCH generation"),
    ("0002-msvc-clang-build-static.patch",
     "required: suppress LLVM/Clang dllexport in static builds"),
    ("0004-redirectable-cling-outs.patch",
     "required: make cling::outs() redirectable for output capture"),
    ("0005-build-userinterface-unconditionally.patch",
     "required: build clingUserInterface without requiring CLING_INCLUDE_TESTS"),
    ("0006-libcling-link-clanginterpreter.patch",
     "required: link clangInterpreter into libcling for ReplCodeCompleter"),
    ("0007-disable-concurrent-compilation.patch",
     "required: prevent CloneToNewContext destroying Modules needed by undo"),
]

OPTIONAL_PATCHES = [
    ("0003-fix-declunloader-usingshadowdecl-crash.patch",
     "optional: fix undo() crash with using-shadow declarations"),
]


def apply_patch(patch: str, label: str) -> None:
    patch_path = PATCHES_DIR / patch
    if not patch_path.exists():
        print(f"  WARNING: {patch} not found in {PATCHES_DIR}")
        return

    # Check if patch can be applied (dry run)
    result = subprocess.run(
        ["git", "-C", str(CLING_SRC), "apply", "--check", str(patch_path)],
        capture_output=True)

    if result.returncode == 0:
        subprocess.run(
            ["git", "-C", str(CLING_SRC), "apply", str(patch_path)],
            check=True)
        print(f"  Applied: {patch} ({label})")
    else:
        print(f"  Skipped: {patch} (already applied or conflict)")


def main() -> int:
    if not (CLING_SRC / ".git").exists():
        print("Error: cling-src directory not found.")
        print("Either:")
        print("  - Pass: --cling-src <path>")
        print("  - Set:  env CLING_SRC_DIR=<path>")
        print("  - Init: git submodule update --init external/cling-src")
        print(f"  (tried: {CLING_SRC})")
        return 1

    apply_all = "--all" in sys.argv

    print("Applying cling-src patches...")
    for patch, label in REQUIRED_PATCHES:
        apply_patch(patch, label)

    if apply_all:
        for patch, label in OPTIONAL_PATCHES:
            apply_patch(patch, label)

    print("\nDone.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
