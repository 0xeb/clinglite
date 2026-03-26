# clinglite plugin system — discovery, dispatch generation.
#
# Orchestrates extension plugins:
#   1. Scans plugins/<name>/CMakeLists.txt for opt-in plugins
#   2. Generates plugin dispatch .cpp (setupAll + pluginNames)
#   3. Wires plugin sources into the clinglite library target

include("${CMAKE_CURRENT_SOURCE_DIR}/common/clinglite_plugin_helpers.cmake")

# ── Accumulators (reset, then plugins append via CACHE INTERNAL) ──────────
set(CLINGLITE_PLUGIN_PCH_FLAGS "" CACHE INTERNAL "")
set(CLINGLITE_PLUGIN_PCH_HEADERS "" CACHE INTERNAL "")
set(CLINGLITE_PLUGIN_SOURCES "" CACHE INTERNAL "")
set(CLINGLITE_PLUGIN_SETUP_FUNCTIONS "" CACHE INTERNAL "")
set(CLINGLITE_PLUGIN_SETUP_HEADERS "" CACHE INTERNAL "")
set(CLINGLITE_PLUGIN_NAMES "" CACHE INTERNAL "")

# ── Plugin discovery (gated by CLINGLITE_BUILD_PLUGINS) ──────────────────
if(CLINGLITE_BUILD_PLUGINS)
    # Auto-enable platform plugins
    if(UNIX AND NOT APPLE AND NOT DEFINED PLUGIN_LINUX)
        set(PLUGIN_LINUX ON CACHE BOOL "Auto-enabled on Linux" FORCE)
    endif()
    if(WIN32 AND NOT DEFINED PLUGIN_WINSDK)
        set(PLUGIN_WINSDK ON CACHE BOOL "Auto-enabled on Windows" FORCE)
    endif()

    # Scan plugins/<name>/CMakeLists.txt, skipping common and template.
    set(_skip_dirs "common" "template")
    file(GLOB _plugin_dirs "${CMAKE_CURRENT_SOURCE_DIR}/*/CMakeLists.txt")
    foreach(_plugin_cmake ${_plugin_dirs})
        get_filename_component(_plugin_dir "${_plugin_cmake}" DIRECTORY)
        get_filename_component(_plugin_name "${_plugin_dir}" NAME)
        if(_plugin_name IN_LIST _skip_dirs)
            continue()
        endif()
        string(TOUPPER "${_plugin_name}" _PLUGIN_NAME)
        if(DEFINED PLUGIN_${_PLUGIN_NAME}_SRC_DIR AND NOT DEFINED PLUGIN_${_PLUGIN_NAME})
            set(PLUGIN_${_PLUGIN_NAME} ON CACHE BOOL
                "Auto-enabled via PLUGIN_${_PLUGIN_NAME}_SRC_DIR" FORCE)
        endif()
        if(PLUGIN_${_PLUGIN_NAME})
            message(STATUS "clinglite plugin: ${_plugin_name}")
            add_subdirectory("${_plugin_dir}"
                "${CMAKE_CURRENT_BINARY_DIR}/plugins/${_plugin_name}")
        endif()
    endforeach()
endif()

# ── Generate plugin dispatch .cpp ─────────────────────────────────────────
clinglite_generate_plugin_dispatch(CLINGLITE "clinglite::plugins"
    "${CMAKE_CURRENT_BINARY_DIR}/plugin_dispatch.cpp")

# ── Wire into clinglite library target ────────────────────────────────────
target_sources(clinglite PRIVATE
    ${CLINGLITE_PLUGIN_SOURCES}
    "${CMAKE_CURRENT_BINARY_DIR}/plugin_dispatch.cpp")
target_include_directories(clinglite PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/common")
