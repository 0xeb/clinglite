# clinglite_plugin_helpers.cmake — shared cmake functions for plugin systems.
#
# All accumulator functions are prefix-parameterized so clinglite and
# downstream consumers can reuse the same logic with different accumulator
# namespaces.
#
# Provides:
#   _clinglite_plugin_register_impl()       — parameterized registration
#   _clinglite_plugin_extend_pch_impl()     — parameterized PCH contribution
#   clinglite_register_plugin()             — convenience (CLINGLITE prefix)
#   clinglite_plugin_extend_pch()           — convenience (CLINGLITE prefix)
#   clinglite_plugin_generate_pch_bridge()  — stateless bridge header generator (UNDEFS, DEFINES, HEADERS)
#   clinglite_plugin_make_shared()          — stateless static→shared wrapper
#   clinglite_generate_plugin_dispatch()    — parameterized dispatch .cpp generator

# ── Parameterized registration ─────────────────────────────────────────────
# Accumulates into ${PREFIX}_PLUGIN_* CACHE INTERNAL variables.
#
# Arguments:
#   PREFIX        — variable prefix (e.g. "CLINGLITE" or "MYPROJECT")
#   PLUGIN_NAME   — short name (e.g. "linux")
#   SETUP_FN      — C++ function name (e.g. "linux_plugin_setup")
#   SETUP_HEADER  — absolute path to the header declaring SETUP_FN
#   SETUP_SOURCE  — source/object to compile (path or generator expr)
function(_clinglite_plugin_register_impl PREFIX PLUGIN_NAME SETUP_FN SETUP_HEADER SETUP_SOURCE)
    set(${PREFIX}_PLUGIN_SETUP_FUNCTIONS
        ${${PREFIX}_PLUGIN_SETUP_FUNCTIONS} "${SETUP_FN}" CACHE INTERNAL "")
    set(${PREFIX}_PLUGIN_SETUP_HEADERS
        ${${PREFIX}_PLUGIN_SETUP_HEADERS} "${SETUP_HEADER}" CACHE INTERNAL "")
    set(${PREFIX}_PLUGIN_SOURCES
        ${${PREFIX}_PLUGIN_SOURCES} "${SETUP_SOURCE}" CACHE INTERNAL "")
    set(${PREFIX}_PLUGIN_NAMES
        ${${PREFIX}_PLUGIN_NAMES} "${PLUGIN_NAME}" CACHE INTERNAL "")
endfunction()

# ── Parameterized PCH contribution ─────────────────────────────────────────
# Accumulates into ${PREFIX}_PLUGIN_PCH_* CACHE INTERNAL variables.
#
# Arguments:
#   PREFIX — variable prefix
# Named arguments:
#   FLAGS   — extra -I or -D flags for pchgen
#   HEADERS — header file names to include
function(_clinglite_plugin_extend_pch_impl PREFIX)
    cmake_parse_arguments(ARG "" "" "FLAGS;HEADERS" ${ARGN})
    set(${PREFIX}_PLUGIN_PCH_FLAGS
        ${${PREFIX}_PLUGIN_PCH_FLAGS} ${ARG_FLAGS} CACHE INTERNAL "")
    set(${PREFIX}_PLUGIN_PCH_HEADERS
        ${${PREFIX}_PLUGIN_PCH_HEADERS} ${ARG_HEADERS} CACHE INTERNAL "")
endfunction()

# ── Convenience wrappers (CLINGLITE prefix) ────────────────────────────────

function(clinglite_register_plugin PLUGIN_NAME SETUP_FN SETUP_HEADER SETUP_SOURCE)
    _clinglite_plugin_register_impl(CLINGLITE
        ${PLUGIN_NAME} ${SETUP_FN} ${SETUP_HEADER} ${SETUP_SOURCE})
endfunction()

function(clinglite_plugin_extend_pch)
    _clinglite_plugin_extend_pch_impl(CLINGLITE ${ARGN})
endfunction()

# ── PCH bridge generation (stateless) ──────────────────────────────────────
# Generates a header file that undefs macros, defines macros, then includes
# headers. Common pattern for plugins that layer on top of headers with
# macro poisoning.
#
# Arguments:
#   OUTPUT_FILE — absolute path for the generated header
# Named arguments:
#   UNDEFS  — macro names to #undef (e.g. snprintf sprintf getenv)
#   DEFINES — lines to emit as guarded #define (e.g. "QT_NAMESPACE=QT" "QT_DLL")
#             Each entry becomes: #ifndef NAME / #define NAME [VALUE] / #endif
#   HEADERS — headers to #include <...> after the undefs/defines
function(clinglite_plugin_generate_pch_bridge OUTPUT_FILE)
    cmake_parse_arguments(ARG "" "" "UNDEFS;DEFINES;HEADERS" ${ARGN})
    set(_content "// Auto-generated PCH bridge — do not edit.\n")
    foreach(_undef ${ARG_UNDEFS})
        string(APPEND _content "#undef ${_undef}\n")
    endforeach()
    foreach(_def ${ARG_DEFINES})
        # Split "NAME=VALUE" or just "NAME"
        string(FIND "${_def}" "=" _eq_pos)
        if(_eq_pos GREATER -1)
            string(SUBSTRING "${_def}" 0 ${_eq_pos} _def_name)
            math(EXPR _val_pos "${_eq_pos} + 1")
            string(SUBSTRING "${_def}" ${_val_pos} -1 _def_value)
            string(APPEND _content "#ifndef ${_def_name}\n#define ${_def_name} ${_def_value}\n#endif\n")
        else()
            string(APPEND _content "#ifndef ${_def}\n#define ${_def}\n#endif\n")
        endif()
    endforeach()
    foreach(_hdr ${ARG_HEADERS})
        string(APPEND _content "#include <${_hdr}>\n")
    endforeach()
    file(WRITE "${OUTPUT_FILE}" "${_content}")
endfunction()

# ── Shared library from static archive (stateless) ────────────────────────
# Wraps a static lib as a shared lib (WHOLE_ARCHIVE) for JIT loading.
#
# Arguments:
#   TARGET_NAME — name for the shared library target
#   STATIC_LIB  — static library target to wrap
# Named arguments:
#   LINK_LIBS — additional libraries to link
function(clinglite_plugin_make_shared TARGET_NAME STATIC_LIB)
    cmake_parse_arguments(ARG "" "" "LINK_LIBS" ${ARGN})
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_stub.cpp" "")
    if(MSVC)
        # IDA SDK forces /GL + /LTCG, so .obj files are LTCG bitcode and
        # CMake's WINDOWS_EXPORT_ALL_SYMBOLS (which uses __create_def to
        # parse COFF .obj) cannot work. Generate a .def from the static
        # .lib via dumpbin and add it as a source so MSVC links with it.
        set(_gen_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/gen_exports_def.py")
        set(_def "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_all.def")
        add_custom_command(
            OUTPUT "${_def}"
            COMMAND ${Python3_EXECUTABLE} "${_gen_script}"
                "$<TARGET_FILE:${STATIC_LIB}>" "${_def}"
            DEPENDS ${STATIC_LIB} "${_gen_script}"
            COMMENT "Generating ${TARGET_NAME} export definitions"
            VERBATIM)
        add_library(${TARGET_NAME} SHARED
            "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_stub.cpp"
            "${_def}")
    else()
        add_library(${TARGET_NAME} SHARED
            "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_stub.cpp")
    endif()
    target_link_libraries(${TARGET_NAME} PRIVATE
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,${STATIC_LIB}>")
    foreach(_lib ${ARG_LINK_LIBS})
        target_link_libraries(${TARGET_NAME} PRIVATE ${_lib})
    endforeach()
endfunction()

# ── Dispatch generation (parameterized) ────────────────────────────────────
# Generates a plugin_dispatch.cpp from accumulated ${PREFIX}_PLUGIN_* vars.
#
# Arguments:
#   PREFIX     — variable prefix (e.g. "CLINGLITE" or "MYPROJECT")
#   NAMESPACE  — C++ namespace for generated code (e.g. "clinglite::plugins")
#   OUTPUT_FILE — path for the generated .cpp
function(clinglite_generate_plugin_dispatch PREFIX NAMESPACE OUTPUT_FILE)
    set(_dispatch_includes "")
    set(_dispatch_calls "")
    set(_dispatch_names "")
    foreach(_hdr ${${PREFIX}_PLUGIN_SETUP_HEADERS})
        string(APPEND _dispatch_includes "#include \"${_hdr}\"\n")
    endforeach()
    foreach(_fn ${${PREFIX}_PLUGIN_SETUP_FUNCTIONS})
        string(APPEND _dispatch_calls "    ${_fn}(interp, opts);\n")
    endforeach()
    foreach(_name ${${PREFIX}_PLUGIN_NAMES})
        string(APPEND _dispatch_names "        \"${_name}\",\n")
    endforeach()

    # Split namespace for nested namespace syntax (e.g. "a::b" → "namespace a { namespace b {")
    string(REPLACE "::" ";" _ns_parts "${NAMESPACE}")
    set(_ns_open "")
    set(_ns_close "")
    foreach(_part ${_ns_parts})
        string(APPEND _ns_open "namespace ${_part} { ")
        string(APPEND _ns_close "} ")
    endforeach()

    file(WRITE "${OUTPUT_FILE}"
        "// Auto-generated by clinglite plugin system — do not edit.\n"
        "#include <clinglite/clinglite.h>\n"
        "#include <string>\n"
        "#include <vector>\n"
        "${_dispatch_includes}\n"
        "${_ns_open}\n"
        "void setupAll(clinglite::Interpreter& interp, clinglite::PluginSetupOptions& opts) {\n"
        "${_dispatch_calls}"
        "}\n"
        "std::vector<std::string> pluginNames() {\n"
        "    return {\n"
        "${_dispatch_names}"
        "    };\n"
        "}\n"
        "${_ns_close} // ${NAMESPACE}\n"
    )
endfunction()
