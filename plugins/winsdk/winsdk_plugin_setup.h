// winsdk_plugin_setup.h — Windows SDK extension plugin for clinglite.
#pragma once

namespace clinglite { class Interpreter; struct PluginSetupOptions; }

/// Preload Windows SDK headers into the interpreter.
void winsdk_plugin_setup(clinglite::Interpreter& interp, clinglite::PluginSetupOptions& opts);
