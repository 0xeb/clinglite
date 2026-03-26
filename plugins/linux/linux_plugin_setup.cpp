// linux_plugin_setup.cpp — Linux/POSIX extension plugin setup.
//
// Preloads POSIX/Linux system headers so users can reference ELF structures,
// mmap, dlopen, signal handlers, etc.

#include "linux_plugin_setup.h"
#include <clinglite/clinglite.h>
#include <iostream>

void linux_plugin_setup(clinglite::Interpreter& interp, clinglite::PluginSetupOptions& /*opts*/) {
    // Undo macro poisoning (harmless no-op if not poisoned)
    clinglite::undoProhPoisoning(interp);

    // Include POSIX/Linux headers — hits include guards if already in PCH.
    static const char* headers[] = {
        "elf.h",
        "link.h",
        "dlfcn.h",
        "unistd.h",
        "fcntl.h",
        "signal.h",
        "sys/mman.h",
        "sys/stat.h",
        "sys/types.h",
        "sys/wait.h",
        "dirent.h",
    };

    int loaded = 0;
    for (const char* hdr : headers) {
        if (interp.includeHeader(hdr))
            ++loaded;
    }

    std::cerr << "Plugin linux: " << loaded << "/"
              << (sizeof(headers)/sizeof(headers[0])) << " headers loaded\n";
}
