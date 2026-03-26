#!/usr/bin/env python3
"""Generate a .def file from a static library using dumpbin.

Usage: gen_exports_def.py <static_lib> <output_def>

Extracts all public symbols from the static library's linker member
table and writes a EXPORTS .def file. This works even when .obj files
are LTCG bitcode (which CMake's __create_def cannot parse).
"""
import re
import subprocess
import sys
from pathlib import Path

SKIP_PREFIXES = (
    ".", "__real@", "__xmm@", "__imp_", "__NULL_IMPORT",
    "__IMPORT_DESCRIPTOR", "\x7fNULL_THUNK_DATA",
    "_vscprintf", "_vsprintf", "_vsnprintf", "_sprintf", "_snprintf",
    "_wcscmp", "_wmemcmp",
)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <static_lib> <output_def>", file=sys.stderr)
        return 1

    lib_path = Path(sys.argv[1])
    def_path = Path(sys.argv[2])

    if not lib_path.exists():
        print(f"Error: {lib_path} not found", file=sys.stderr)
        return 1

    # Run dumpbin to list all public symbols from the first linker member
    result = subprocess.run(
        ["dumpbin", "/LINKERMEMBER:1", str(lib_path)],
        capture_output=True, text=True)

    if result.returncode != 0:
        print(f"dumpbin failed: {result.stderr}", file=sys.stderr)
        return 1

    # Parse: lines look like "    00000010  ?func@ns@@..."
    # After the "public symbols" header, each line has hex offset + symbol
    symbols = []
    in_symbols = False
    for line in result.stdout.splitlines():
        stripped = line.strip()
        if "public symbols" in line.lower():
            in_symbols = True
            continue
        if not in_symbols:
            continue
        if not stripped:
            continue
        # Lines: "  count public symbols" or "  hex_offset  symbol_name"
        parts = stripped.split(None, 1)
        if len(parts) == 2 and len(parts[0]) >= 4:
            sym = parts[1]
            if any(sym.startswith(p) for p in SKIP_PREFIXES):
                continue
            symbols.append(sym)

    # Write .def file
    with open(def_path, "w") as f:
        f.write("EXPORTS\n")
        for sym in sorted(set(symbols)):
            f.write(f"    {sym}\n")

    print(f"Generated {def_path}: {len(symbols)} symbols")
    return 0


if __name__ == "__main__":
    sys.exit(main())
