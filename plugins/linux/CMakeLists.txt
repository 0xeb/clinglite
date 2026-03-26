# linux — extension plugin for clinglite.
#
# Preloads POSIX/Linux system headers (elf.h, sys/mman.h, dlfcn.h, etc.)
# into the REPL so users can reference ELF structures and POSIX APIs.
#
# Enable: -DPLUGIN_LINUX=ON
# Only meaningful on Linux builds.

include("${CMAKE_CURRENT_SOURCE_DIR}/../common/clinglite_plugin_helpers.cmake")

if(NOT UNIX OR APPLE)
    message(STATUS "linux plugin: skipped (not a Linux build)")
    return()
endif()

message(STATUS "linux plugin: enabled")

# ── PCH bridge ───────────────────────────────────────────────────────────
# Undo poison macros, then include POSIX/Linux headers.
clinglite_plugin_generate_pch_bridge(
    "${CMAKE_CURRENT_BINARY_DIR}/linux_pch_bridge.h"
    UNDEFS snprintf sprintf getenv
    HEADERS
        elf.h
        link.h
        dlfcn.h
        unistd.h
        fcntl.h
        signal.h
        sys/mman.h
        sys/stat.h
        sys/types.h
        sys/wait.h
        dirent.h)

clinglite_plugin_extend_pch(
    FLAGS "-I${CMAKE_CURRENT_BINARY_DIR}"
    HEADERS linux_pch_bridge.h)

# ── Setup source ─────────────────────────────────────────────────────────
add_library(linux_plugin_init OBJECT
    "${CMAKE_CURRENT_SOURCE_DIR}/linux_plugin_setup.cpp")
# Include clinglite headers without linking (avoids circular dep since
# this OBJECT library's objects are added to clinglite via target_sources)
target_include_directories(linux_plugin_init PRIVATE
    $<TARGET_PROPERTY:clinglite,INTERFACE_INCLUDE_DIRECTORIES>)

# ── Register ─────────────────────────────────────────────────────────────
clinglite_register_plugin(linux
    linux_plugin_setup
    "${CMAKE_CURRENT_SOURCE_DIR}/linux_plugin_setup.h"
    "$<TARGET_OBJECTS:linux_plugin_init>")
