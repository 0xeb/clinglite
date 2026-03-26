// winsdk_plugin_setup.cpp — Windows SDK extension plugin setup.
//
// Preloads Windows SDK headers so users can reference Win32 types
// (HANDLE, HMODULE, IMAGE_NT_HEADERS, etc.).

#include "winsdk_plugin_setup.h"
#include <clinglite/clinglite.h>
#include <iostream>

void winsdk_plugin_setup(clinglite::Interpreter& interp, clinglite::PluginSetupOptions& /*opts*/) {
    // Undo macro poisoning (harmless no-op if not poisoned)
    clinglite::undoProhPoisoning(interp);

    // Include Windows SDK headers — hits include guards if already in PCH.
    static const char* headers[] = {
        "windows.h",
        "winsock2.h",
        "ws2tcpip.h",
        "shlwapi.h",
        "dbghelp.h",
        "tlhelp32.h",
        "psapi.h",
        "winternl.h",
    };

    int loaded = 0;
    for (const char* hdr : headers) {
        if (interp.includeHeader(hdr))
            ++loaded;
    }

    std::cerr << "Plugin winsdk: " << loaded << "/"
              << (sizeof(headers)/sizeof(headers[0])) << " headers loaded\n";
}
