// linux_plugin_setup.h — Linux/POSIX extension plugin for clinglite.
#pragma once

namespace clinglite { class Interpreter; struct PluginSetupOptions; }

/// Preload POSIX/Linux system headers into the interpreter.
void linux_plugin_setup(clinglite::Interpreter& interp, clinglite::PluginSetupOptions& opts);
