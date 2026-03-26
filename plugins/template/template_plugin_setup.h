// template_plugin_setup.h — rename to <plugin_name>_plugin_setup.h
#pragma once

namespace clinglite { class Interpreter; struct PluginSetupOptions; }

void template_plugin_setup(clinglite::Interpreter& interp, clinglite::PluginSetupOptions& opts);
