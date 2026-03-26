# Plugin Template

This directory is a skeleton for creating new clinglite plugins.

## Quick Start

1. Copy this directory to `plugins/<your-name>/`
2. Replace all `<PLUGIN_NAME>` with your uppercase name (e.g. `WINSDK`)
3. Replace all `<plugin_name>` with your lowercase name (e.g. `winsdk`)
4. Uncomment and adapt the sections in `CMakeLists.txt`
5. Implement your setup function in `<plugin_name>_plugin_setup.cpp`
6. Enable with `-DPLUGIN_<YOUR_NAME>_SRC_DIR=<path>` (or `-DPLUGIN_<YOUR_NAME>=ON` for plugins without external sources)

## Plugin Anatomy

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Build, PCH bridge, registration |
| `<name>_plugin_setup.h` | Setup function declaration |
| `<name>_plugin_setup.cpp` | Setup function implementation |

## Available Helpers (`common/clinglite_plugin_helpers.cmake`)

| Function | Purpose |
|----------|---------|
| `clinglite_register_plugin(name fn header source)` | Register with dispatch |
| `clinglite_plugin_extend_pch(FLAGS ... HEADERS ...)` | Add to shared PCH |
| `clinglite_plugin_generate_pch_bridge(file UNDEFS ... HEADERS ...)` | Generate bridge header |
| `clinglite_plugin_make_shared(target static_lib LINK_LIBS ...)` | Wrap static lib as shared |

For downstream consumers with separate dispatch, use the
parameterized `_clinglite_plugin_register_impl(PREFIX ...)` and
`_clinglite_plugin_extend_pch_impl(PREFIX ...)` functions with a custom prefix.

## Setup Function Signature

```cpp
void <name>_plugin_setup(clinglite::Interpreter& interp, clinglite::PluginSetupOptions& opts);
```

Use the interpreter to:
- `interp.loadLibrary(path)` — load shared libs for JIT symbol resolution
- `interp.addIncludePath(dir)` — add header search paths
- `interp.execute(code)` — run arbitrary C++ (e.g. `#undef`)
- `interp.includeHeader(name)` — include a header

## Baking Paths

Use CMake compile definitions to bake paths at build time:

```cmake
target_compile_definitions(<name>_plugin_init PRIVATE
    MY_INCLUDE_DIR="${_inc}"
    MY_SHARED_LIB="$<TARGET_FILE:my_shared_target>")
```

These become string literals in C++ that the setup function uses directly.
