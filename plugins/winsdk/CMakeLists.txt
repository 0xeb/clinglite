# winsdk — extension plugin for clinglite.
#
# Preloads Windows SDK headers (windows.h, etc.) into the REPL so users
# can reference Win32 types and APIs.
#
# Enable: -DPLUGIN_WINSDK=ON
# Only meaningful on Windows (MSVC/MinGW) builds.

include("${CMAKE_CURRENT_SOURCE_DIR}/../common/clinglite_plugin_helpers.cmake")

if(NOT WIN32)
    message(STATUS "winsdk plugin: skipped (not a Windows build)")
    return()
endif()

message(STATUS "winsdk plugin: enabled")

# ── PCH bridge ───────────────────────────────────────────────────────────
# Undo poison macros, then include Windows SDK headers.
clinglite_plugin_generate_pch_bridge(
    "${CMAKE_CURRENT_BINARY_DIR}/winsdk_pch_bridge.h"
    UNDEFS snprintf sprintf getenv
    HEADERS
        windows.h
        winsock2.h
        ws2tcpip.h
        shlwapi.h
        dbghelp.h
        tlhelp32.h
        psapi.h
        winternl.h)

clinglite_plugin_extend_pch(
    FLAGS "-I${CMAKE_CURRENT_BINARY_DIR}"
    HEADERS winsdk_pch_bridge.h)

# ── Setup source ─────────────────────────────────────────────────────────
add_library(winsdk_plugin_init OBJECT
    "${CMAKE_CURRENT_SOURCE_DIR}/winsdk_plugin_setup.cpp")
# Include clinglite headers without linking (avoids circular dep since
# this OBJECT library's objects are added to clinglite via target_sources)
target_include_directories(winsdk_plugin_init PRIVATE
    $<TARGET_PROPERTY:clinglite,INTERFACE_INCLUDE_DIRECTORIES>)

if(MSVC)
    target_compile_options(winsdk_plugin_init PRIVATE /EHsc)
endif()

# ── Register ─────────────────────────────────────────────────────────────
clinglite_register_plugin(winsdk
    winsdk_plugin_setup
    "${CMAKE_CURRENT_SOURCE_DIR}/winsdk_plugin_setup.h"
    "$<TARGET_OBJECTS:winsdk_plugin_init>")
