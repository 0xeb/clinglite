// template_plugin_setup.cpp — rename to <plugin_name>_plugin_setup.cpp
#include "template_plugin_setup.h"
#include <clinglite/clinglite.h>

void template_plugin_setup(clinglite::Interpreter& interp, clinglite::PluginSetupOptions& /*opts*/) {
    // Example: load a shared library for JIT symbol resolution
    // interp.loadLibrary("/path/to/lib.so");

    // Example: add an include search path
    // interp.addIncludePath("/path/to/include");

    // Example: include a header
    // interp.includeHeader("mylib/mylib.h");
}
