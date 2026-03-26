# clinglite

Embed a full C++ interpreter in your application with a single header.

clinglite wraps [Cling](https://github.com/root-project/cling) — the interactive C++ interpreter developed at [CERN](https://root.cern/) for the [ROOT](https://root.cern/) data analysis framework — behind a clean API. Your code includes `<clinglite/clinglite.h>`, links `libclinglite`, and gets a C++ interpreter powered by Clang 20. No LLVM headers leak into your build.

```cpp
#include <clinglite/clinglite.h>

int main() {
    clinglite::Environment env("myapp");

    clinglite::Options opts;
    opts.args = {"myapp"};
    clinglite::Interpreter interp(opts);

    interp.execute("#include <cmath>");

    clinglite::Value result;
    interp.execute("std::sqrt(2.0)", result);
    printf("sqrt(2) = %f\n", result.asDouble());  // 1.414214
}
```

## What you can do

- **Execute C++ at runtime** — compile and run arbitrary code, capture typed results (int, double, pointer)
- **Crash recovery** — `executeSafe()` catches SIGSEGV/SEH and keeps the interpreter alive
- **Pre-compiled headers** — generate and load PCH files for instant startup with large header sets
- **Code completion** — token-aware completions with prefix matching for REPL interfaces
- **Script execution** — load `.cpp` files with entrypoint dispatch, namespace isolation, and argc/argv injection
- **Undo/rollback** — unload declarations, revert to a previous interpreter state
- **Virtual filesystem** — register in-memory headers that `#include` can find
- **Symbol lookup** — resolve JIT'd or library symbols by mangled name
- **Multiple interpreters** — run independent sessions from a single process
- **Output capture** — redirect interpreter output and diagnostics through callbacks
- **Plugin system** — extend the interpreter with platform-specific headers, libraries, and PCH contributions

## Plugins

clinglite includes a plugin system for extending the interpreter with platform-specific headers, libraries, and PCH contributions. Plugins hook into initialization and can preload headers, load shared libraries, and inject code.

| Plugin | Description | Auto-enabled |
|--------|-------------|--------------|
| **linux** | Preloads POSIX/ELF headers (`dlfcn.h`, `elf.h`, `link.h`, ...) | Linux |
| **winsdk** | Preloads Windows SDK headers (`windows.h`, `tlhelp32.h`, ...) | Windows |
| **template** | Skeleton for creating new plugins | — |

Downstream consumers (like [idacpp](https://github.com/0xeb/idacpp)) can register their own plugins using the same CMake helpers. See [plugins/template/](plugins/template/) for a walkthrough.

## Design

- **Zero header leakage** — no LLVM or Cling types in your include path
- **Source-only** — build on the machine where you use it, typically via `add_subdirectory(clinglite)`

## Quick start

```bash
git clone --recurse-submodules --shallow-submodules https://github.com/0xeb/clinglite.git
cd clinglite
python scripts/build_cling.py -j $(nproc)    # build LLVM/Cling (~30-90 min, one-time)
python scripts/build_clinglite.py             # build clinglite
./build-ninja-static/examples/basic           # run
```

## Documentation

| Document | Contents |
|----------|----------|
| [BUILDING.md](BUILDING.md) | Prerequisites, build scripts, CMake presets, platform support |
| [API.md](API.md) | Full API reference: Environment, Value, Interpreter, Session, ScriptRunner |
| [examples/](examples/) | Runnable programs: basic usage, crash recovery, REPL, PCH, value types |

## Acknowledgments

clinglite is built on [Cling](https://github.com/root-project/cling), created by the [ROOT](https://root.cern/) team at [CERN](https://home.cern/) — originally by Vassil Vassilev and Axel Naumann. Cling is part of the ROOT data analysis framework used in high-energy physics.

## Limitations

- **Source-only / non-relocatable** — built artifacts expect the local source/build tree at compile time
- **Single-threaded** — each `Interpreter` must be used from one thread
- **Monotonic memory** — JIT memory grows over the interpreter's lifetime
- **Static link size** — ~180 MB (full LLVM/Clang/Cling stack)

## License

MIT License. Copyright (c) Elias Bachaalany. See [LICENSE](LICENSE).
