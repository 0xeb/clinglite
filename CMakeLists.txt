# clinglite — lightweight Cling wrapper library
# Copyright (c) Elias Bachaalany
# SPDX-License-Identifier: MIT

# Requires a local Cling/LLVM build tree (from build-cling/, env var, or CMake var).

cmake_minimum_required(VERSION 3.20)
project(clinglite VERSION 0.0.3 LANGUAGES CXX)

# LLVM/Cling libs are built Release (/MD). Force the same CRT for all build
# types so Debug builds can link against them without LNK2038 errors.
if(MSVC)
  cmake_policy(SET CMP0091 NEW)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING
    "MSVC runtime library — forced to /MD to match pre-built LLVM libs")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Locate a Cling build directory ──────────────────────────────────────────
# Priority: 1) -DCLING_BUILD_DIR  2) env CLING_BUILD_DIR  3) local build-cling/
if(NOT CLING_BUILD_DIR)
  if(DEFINED ENV{CLING_BUILD_DIR})
    set(CLING_BUILD_DIR "$ENV{CLING_BUILD_DIR}")
    message(STATUS "Using CLING_BUILD_DIR from environment: ${CLING_BUILD_DIR}")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build-cling/lib"
      AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build-cling/include")
    set(CLING_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build-cling")
    message(STATUS "Using local build tree: ${CLING_BUILD_DIR}")
  else()
    message(FATAL_ERROR
      "CLING_BUILD_DIR not found. Either:\n"
      "  - Build Cling locally into build-cling/\n"
      "  - Set: -DCLING_BUILD_DIR=<path>  (point to existing Cling build)\n"
      "  - Set: env CLING_BUILD_DIR=<path>")
  endif()
endif()

if(NOT IS_ABSOLUTE "${CLING_BUILD_DIR}")
  get_filename_component(CLING_BUILD_DIR "${CLING_BUILD_DIR}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# Validate
foreach(_required lib include)
  if(NOT EXISTS "${CLING_BUILD_DIR}/${_required}")
    message(FATAL_ERROR
      "CLING_BUILD_DIR (${CLING_BUILD_DIR}) missing '${_required}/' — "
      "not a valid Cling build tree.")
  endif()
endforeach()

# ── Detect actual library directory ────────────────────────────────────────
# Single-config generators (Ninja, Make) output directly to lib/.
# Multi-config generators (VS) output to <config>/lib/.
# Probe for whichever layout exists — no hardcoded config names.
set(_CLING_LIB_DIR "${CLING_BUILD_DIR}/lib")
file(GLOB _config_lib_dirs "${CLING_BUILD_DIR}/*/lib")
foreach(_dir ${_config_lib_dirs})
  if(EXISTS "${_dir}/clingInterpreter.lib" OR EXISTS "${_dir}/libclingInterpreter.a")
    set(_CLING_LIB_DIR "${_dir}")
    break()
  endif()
endforeach()
message(STATUS "Cling library dir: ${_CLING_LIB_DIR}")

# ── Source directories ──────────────────────────────────────────────────────
# Primary: external/ submodules (standalone builds)
# Fallback: ../ parent directories (monorepo builds)
if(NOT CLING_SRC_DIR)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/cling-src/CMakeLists.txt")
    set(CLING_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/cling-src")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../cling-src/CMakeLists.txt")
    set(CLING_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cling-src")
  endif()
endif()

if(NOT LLVM_SRC_DIR)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/llvm-project/llvm/CMakeLists.txt")
    set(LLVM_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/llvm-project/llvm")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../llvm-project/llvm/CMakeLists.txt")
    set(LLVM_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../llvm-project/llvm")
  endif()
endif()

if(NOT CLANG_SRC_DIR)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/llvm-project/clang/CMakeLists.txt")
    set(CLANG_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/llvm-project/clang")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../llvm-project/clang/CMakeLists.txt")
    set(CLANG_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../llvm-project/clang")
  endif()
endif()

foreach(_dir CLING_SRC_DIR LLVM_SRC_DIR CLANG_SRC_DIR)
  if(NOT ${_dir} OR NOT EXISTS "${${_dir}}")
    message(FATAL_ERROR
      "${_dir} not found. Either:\n"
      "  - Clone with --recurse-submodules (populates external/)\n"
      "  - Build from the monorepo (../ sibling dirs)\n"
      "  - Pass -D${_dir}=<path> explicitly")
  endif()
endforeach()

# ── Auto-apply required patches to cling-src ──────────────────────────────
# Runs apply_patches.py at configure time. Safe to run multiple times —
# already-applied patches are silently skipped. Fails loudly on errors.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_EXECUTABLE)
  find_program(Python3_EXECUTABLE NAMES python3 python)
endif()
if(Python3_EXECUTABLE)
  set(_patch_script "${CMAKE_CURRENT_SOURCE_DIR}/scripts/apply_patches.py")
  if(EXISTS "${_patch_script}")
    execute_process(
      COMMAND ${Python3_EXECUTABLE} "${_patch_script}"
        --cling-src "${CLING_SRC_DIR}" --all
      RESULT_VARIABLE _patch_result
      OUTPUT_VARIABLE _patch_output
      ERROR_VARIABLE _patch_output)
    if(_patch_result)
      message(FATAL_ERROR "Patch application failed:\n${_patch_output}")
    else()
      message(STATUS "Cling patches: ${_patch_output}")
    endif()
  endif()
endif()

# ── Build type (SHARED or STATIC) ──────────────────────────────────────────
include(GenerateExportHeader)

if(NOT DEFINED CLINGLITE_BUILD_SHARED)
  set(CLINGLITE_BUILD_SHARED OFF)
endif()

set(CLINGLITE_HEADERS
  include/clinglite/clinglite.h
  include/clinglite/msvc_jit_exports.h
)

if(CLINGLITE_BUILD_SHARED)
  add_library(clinglite SHARED src/clinglite.cpp ${CLINGLITE_HEADERS})
else()
  add_library(clinglite STATIC src/clinglite.cpp ${CLINGLITE_HEADERS})
endif()

# Namespace alias: consumers can use clinglite::clinglite
add_library(clinglite::clinglite ALIAS clinglite)

generate_export_header(clinglite
  EXPORT_MACRO_NAME CLINGLITE_API
  EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/clinglite/clinglite_export.h"
)
target_sources(clinglite PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/include/clinglite/clinglite_export.h"
)

# Public: consumers get include/clinglite/ (source) and generated export header
target_include_directories(clinglite PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
)

# Private: LLVM/Cling includes only needed by clinglite.cpp
target_include_directories(clinglite PRIVATE
  ${CLING_SRC_DIR}/include
  ${CLING_BUILD_DIR}/tools/cling/include
  ${CLANG_SRC_DIR}/include
  ${CLING_BUILD_DIR}/tools/clang/include
  ${LLVM_SRC_DIR}/include
  ${CLING_BUILD_DIR}/include
)

target_compile_definitions(clinglite PRIVATE
  __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS __STDC_LIMIT_MACROS
)

# Bake source paths so clinglite can find Cling headers at runtime without
# requiring the user to set CLING_DIR env var or create directory junctions.
# Normalize to forward slashes to avoid C escape sequence issues on Windows.
if(CLING_SRC_DIR)
  file(TO_CMAKE_PATH "${CLING_SRC_DIR}/include" _cling_src_inc)
  target_compile_definitions(clinglite PRIVATE
    CLINGLITE_DEFAULT_CLING_SRC_INCLUDE="${_cling_src_inc}")
endif()
if(CLING_BUILD_DIR)
  file(TO_CMAKE_PATH "${CLING_BUILD_DIR}" _cling_build_dir)
  target_compile_definitions(clinglite PRIVATE
    CLINGLITE_DEFAULT_CLING_DIR="${_cling_build_dir}")
endif()

# ── Compiler flags ──────────────────────────────────────────────────────────
if(MSVC)
  target_compile_options(clinglite PRIVATE /GR- /EHsc)
elseif(MINGW)
  target_compile_options(clinglite PRIVATE -fno-rtti -Wa,-mbig-obj)
else()
  target_compile_options(clinglite PRIVATE -fno-rtti)
endif()

# ── PCH for faster builds ──────────────────────────────────────────────────
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.16)
  target_precompile_headers(clinglite PRIVATE
    <cling/Interpreter/Interpreter.h>
    <cling/Interpreter/Value.h>
    <cling/MetaProcessor/MetaProcessor.h>
    <llvm/Support/CrashRecoveryContext.h>
    <llvm/Support/ManagedStatic.h>
    <llvm/Support/raw_ostream.h>
    <clang/Frontend/CompilerInstance.h>
  )
endif()

# ── Link directories ─────────────────────────────────────────────────────
target_link_directories(clinglite PUBLIC $<BUILD_INTERFACE:${_CLING_LIB_DIR}>)

# ── Collect all libraries ────────────────────────────────────────────────
set(ALL_LIBS
  # Cling
  clingInterpreter clingMetaProcessor clingUserInterface clingUtils

  # Clang
  clangFrontendTool clangFrontend clangDriver clangSerialization clangCodeGen
  clangParse clangSema clangAnalysis clangAST clangASTMatchers clangLex
  clangBasic clangEdit clangRewrite clangRewriteFrontend clangInterpreter
  clangAPINotes clangSupport clangARCMigrate clangStaticAnalyzerFrontend
  clangStaticAnalyzerCheckers clangStaticAnalyzerCore clangCrossTU clangIndex
  clangFormat clangToolingInclusions clangToolingCore clangExtractAPI
  clangInstallAPI

  # LLVM
  LLVMOrcDebugging LLVMOrcJIT LLVMPasses LLVMCoroutines LLVMHipStdPar
  LLVMipo LLVMFrontendOpenMP LLVMFrontendOffloading LLVMFrontendAtomic
  LLVMFrontendDriver LLVMFrontendHLSL LLVMLinker LLVMVectorize LLVMSandboxIR
  LLVMExecutionEngine LLVMJITLink LLVMOrcTargetProcess LLVMOrcShared
  LLVMRuntimeDyld LLVMIRPrinter LLVMInstrumentation LLVMCFGuard
  LLVMAsmPrinter LLVMGlobalISel LLVMSelectionDAG LLVMCodeGen LLVMScalarOpts
  LLVMAggressiveInstCombine LLVMInstCombine LLVMObjCARCOpts LLVMTransformUtils
  LLVMTarget LLVMCGData LLVMBitWriter LLVMAnalysis LLVMProfileData
  LLVMSymbolize LLVMDebugInfoPDB LLVMDebugInfoCodeView LLVMDebugInfoMSF
  LLVMDebugInfoBTF LLVMDebugInfoDWARF LLVMObject LLVMIRReader LLVMBitReader
  LLVMAsmParser LLVMCore LLVMRemarks LLVMBitstreamReader LLVMTextAPI
  LLVMTextAPIBinaryReader LLVMMCParser LLVMMCDisassembler LLVMMC
  LLVMCodeGenTypes LLVMBinaryFormat LLVMTargetParser LLVMLineEditor LLVMOption
  LLVMCoverage LLVMLTO LLVMExtensions LLVMWindowsDriver LLVMTableGen
  LLVMSupport LLVMDemangle
)

# Architecture-specific targets
if(WIN32)
  list(APPEND ALL_LIBS LLVMX86CodeGen LLVMX86AsmParser LLVMX86Desc
    LLVMX86Disassembler LLVMX86Info)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
  list(APPEND ALL_LIBS LLVMAArch64CodeGen LLVMAArch64AsmParser LLVMAArch64Desc
    LLVMAArch64Disassembler LLVMAArch64Info LLVMAArch64Utils)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  list(APPEND ALL_LIBS LLVMX86CodeGen LLVMX86AsmParser LLVMX86Desc
    LLVMX86Disassembler LLVMX86Info)
endif()

# NVPTX (if built)
if(EXISTS "${CLING_BUILD_DIR}/lib/libLLVMNVPTXCodeGen.a"
   OR EXISTS "${CLING_BUILD_DIR}/lib/LLVMNVPTXCodeGen.lib")
  list(APPEND ALL_LIBS LLVMNVPTXCodeGen LLVMNVPTXDesc LLVMNVPTXInfo)
endif()

# ── Link LLVM/Clang/Cling libs ──────────────────────────────────────────
# Shared: all LLVM baked in, consumer just links clinglite.dll/.so
# Static: consumer also needs the LLVM libs (unless fat-static)
if(CLINGLITE_BUILD_SHARED)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    target_link_libraries(clinglite PRIVATE -Wl,--start-group ${ALL_LIBS} -Wl,--end-group)
  else()
    target_link_libraries(clinglite PRIVATE ${ALL_LIBS})
  endif()
else()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    target_link_libraries(clinglite PUBLIC -Wl,--start-group ${ALL_LIBS} -Wl,--end-group)
  else()
    target_link_libraries(clinglite PUBLIC ${ALL_LIBS})
  endif()
endif()

# ── Platform system libraries ──────────────────────────────────────────────
if(WIN32)
  target_link_libraries(clinglite PUBLIC psapi shell32 ole32 uuid advapi32 ws2_32 ntdll version)
elseif(APPLE)
  target_link_libraries(clinglite PUBLIC dl pthread)
  # LLVM on macOS links libedit (for LineEditor) and libxml2 (for clang)
  find_library(EDIT_LIB edit)
  find_library(XML2_LIB xml2)
  find_library(CURSES_LIB curses)
  if(EDIT_LIB)
    target_link_libraries(clinglite PUBLIC ${EDIT_LIB})
  endif()
  if(XML2_LIB)
    target_link_libraries(clinglite PUBLIC ${XML2_LIB})
  endif()
  if(CURSES_LIB)
    target_link_libraries(clinglite PUBLIC ${CURSES_LIB})
  endif()
elseif(UNIX)
  target_link_libraries(clinglite PUBLIC dl pthread)
endif()

# External compression libraries (LLVM depends on these)
find_library(ZSTD_LIB NAMES zstd libzstd HINTS ${CLING_BUILD_DIR}/lib)
find_library(ZLIB_LIB NAMES z zlib HINTS ${CLING_BUILD_DIR}/lib)
if(ZSTD_LIB)
  target_link_libraries(clinglite PUBLIC ${ZSTD_LIB})
endif()
if(ZLIB_LIB)
  target_link_libraries(clinglite PUBLIC ${ZLIB_LIB})
endif()

# ── Fat static library option ──────────────────────────────────────────────
option(CLINGLITE_FAT_STATIC "Merge all LLVM/Cling libs into a single archive" OFF)

if(CLINGLITE_FAT_STATIC AND NOT CLINGLITE_BUILD_SHARED)
  set(_fat_lib "${CMAKE_CURRENT_BINARY_DIR}/libclinglite_fat.a")
  set(_mri "${CMAKE_CURRENT_BINARY_DIR}/fat_static.mri")
  set(_fat_inputs "$<TARGET_FILE:clinglite>")
  foreach(_lib ${ALL_LIBS})
    list(APPEND _fat_inputs "${CLING_BUILD_DIR}/lib/lib${_lib}.a")
  endforeach()

  if(APPLE)
    find_program(CLINGLITE_LIBTOOL NAMES libtool)
    if(NOT CLINGLITE_LIBTOOL)
      message(FATAL_ERROR "fat-static on macOS requires libtool")
    endif()

    add_custom_command(TARGET clinglite POST_BUILD
      COMMAND ${CLINGLITE_LIBTOOL} -static -o ${_fat_lib} ${_fat_inputs}
      COMMENT "Creating fat static library (all-in-one)"
      BYPRODUCTS ${_fat_lib}
      COMMAND_EXPAND_LISTS
    )
  else()
    set(_mri_content "create ${_fat_lib}\n")
    string(APPEND _mri_content "addlib $<TARGET_FILE:clinglite>\n")
    foreach(_lib ${ALL_LIBS})
      string(APPEND _mri_content "addlib ${CLING_BUILD_DIR}/lib/lib${_lib}.a\n")
    endforeach()
    string(APPEND _mri_content "save\nend\n")

    file(GENERATE OUTPUT ${_mri} CONTENT "${_mri_content}")

    add_custom_command(TARGET clinglite POST_BUILD
      COMMAND ${CMAKE_AR} -M < ${_mri}
      COMMENT "Creating fat static library (all-in-one)"
      BYPRODUCTS ${_fat_lib}
    )
  endif()
endif()

# ── PCH generator tool ──────────────────────────────────────────────────────
add_executable(clinglite_pchgen tools/pchgen.cpp)
target_link_libraries(clinglite_pchgen PRIVATE clinglite)
if(CLINGLITE_BUILD_SHARED)
  # pchgen uses LLVM/Clang internals (ASTWriter, BitstreamWriter, Sema, etc.)
  # not exported from clinglite.dll — link the full static lib set directly.
  target_link_directories(clinglite_pchgen PRIVATE ${CLING_BUILD_DIR}/lib)
  target_link_libraries(clinglite_pchgen PRIVATE ${ALL_LIBS})
endif()
target_include_directories(clinglite_pchgen PRIVATE
  ${CLING_SRC_DIR}/include
  ${CLING_BUILD_DIR}/tools/cling/include
  ${CLANG_SRC_DIR}/include
  ${CLING_BUILD_DIR}/tools/clang/include
  ${LLVM_SRC_DIR}/include
  ${CLING_BUILD_DIR}/include
)
target_compile_definitions(clinglite_pchgen PRIVATE
  __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS __STDC_LIMIT_MACROS
)
if(MSVC)
  target_compile_options(clinglite_pchgen PRIVATE /GR- /EHsc)
elseif(MINGW)
  target_compile_options(clinglite_pchgen PRIVATE -fno-rtti -Wa,-mbig-obj)
else()
  target_compile_options(clinglite_pchgen PRIVATE -fno-rtti)
endif()

# ── Identifier generator tool ──────────────────────────────────────────────
add_executable(clinglite_idgen tools/idgen.cpp)
target_link_libraries(clinglite_idgen PRIVATE clinglite)
if(CLINGLITE_BUILD_SHARED)
  target_link_directories(clinglite_idgen PRIVATE ${CLING_BUILD_DIR}/lib)
  target_link_libraries(clinglite_idgen PRIVATE ${ALL_LIBS})
endif()
if(MSVC)
  target_compile_options(clinglite_idgen PRIVATE /EHsc)
endif()

# ── Detect Clang resource directory ─────────────────────────────────────────
if(NOT CLINGLITE_CLANG_BUILTIN_DIR)
  file(GLOB _clang_resource_dirs "${_CLING_LIB_DIR}/clang/*")
  foreach(_dir ${_clang_resource_dirs})
    if(IS_DIRECTORY "${_dir}/include")
      get_filename_component(CLINGLITE_CLANG_RESOURCE_VERSION "${_dir}" NAME)
      set(CLINGLITE_CLANG_BUILTIN_DIR "${_dir}/include")
      break()
    endif()
  endforeach()
endif()

# Allow explicit override: -DCLINGLITE_CLANG_BUILTIN_DIR=... -DCLINGLITE_CLANG_RESOURCE_VERSION=20
if(CLINGLITE_CLANG_BUILTIN_DIR AND NOT CLINGLITE_CLANG_RESOURCE_VERSION)
  # Infer version from path: .../lib/clang/<ver>/include
  get_filename_component(_parent "${CLINGLITE_CLANG_BUILTIN_DIR}" DIRECTORY)
  get_filename_component(CLINGLITE_CLANG_RESOURCE_VERSION "${_parent}" NAME)
endif()

if(NOT CLINGLITE_CLANG_BUILTIN_DIR)
  message(WARNING "Could not find Clang built-in headers in ${CLING_BUILD_DIR}/lib/clang/*/include"
    " (set -DCLINGLITE_CLANG_BUILTIN_DIR=...)")
endif()

# ── Bake Clang resource dir path into clinglite ───────────────────────────
# Cling resolves lib/clang/<ver> relative to the binary. Instead of creating
# symlinks or copying the resource dir into the build tree, we bake the
# original path as a compile definition and pass -resource-dir to Cling.
if(CLINGLITE_CLANG_BUILTIN_DIR AND CLINGLITE_CLANG_RESOURCE_VERSION)
  get_filename_component(_res_dir "${CLINGLITE_CLANG_BUILTIN_DIR}" DIRECTORY)
  file(TO_CMAKE_PATH "${_res_dir}" _res_dir)
  target_compile_definitions(clinglite PRIVATE
    CLINGLITE_DEFAULT_RESOURCE_DIR="${_res_dir}")
  message(STATUS "Clang resource dir: ${_res_dir}")
endif()

# ── Propagate key variables to parent scope ──────────────────────────────
# When included via add_subdirectory(), make discovery results available
# to the consumer so it does not need to re-derive them.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(CLING_BUILD_DIR                 "${CLING_BUILD_DIR}"                 PARENT_SCOPE)
  set(CLING_SRC_DIR                   "${CLING_SRC_DIR}"                   PARENT_SCOPE)
  set(CLINGLITE_CLANG_BUILTIN_DIR     "${CLINGLITE_CLANG_BUILTIN_DIR}"     PARENT_SCOPE)
  set(CLINGLITE_CLANG_RESOURCE_VERSION "${CLINGLITE_CLANG_RESOURCE_VERSION}" PARENT_SCOPE)
  set(CLINGLITE_PLUGIN_PCH_FLAGS      "${CLINGLITE_PLUGIN_PCH_FLAGS}"      PARENT_SCOPE)
  set(CLINGLITE_PLUGIN_PCH_HEADERS    "${CLINGLITE_PLUGIN_PCH_HEADERS}"    PARENT_SCOPE)
endif()

# ── Plugin system ──────────────────────────────────────────────────────────
# Always included — generates dispatch .cpp (empty if no plugins enabled).
# Set CLINGLITE_BUILD_PLUGINS=OFF to skip plugin discovery.
option(CLINGLITE_BUILD_PLUGINS "Build clinglite extension plugins" ON)
add_subdirectory(plugins)

# ── Examples ───────────────────────────────────────────────────────────────
option(CLINGLITE_BUILD_EXAMPLES "Build example programs" ON)
if(CLINGLITE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# ── Python bindings (optional) ──────────────────────────────────────────────
option(CLINGLITE_BUILD_PYTHON "Build Python bindings (nanobind)" OFF)
if(CLINGLITE_BUILD_PYTHON)
  find_package(Python COMPONENTS Interpreter Development.Module QUIET)
  if(Python_FOUND)
    add_subdirectory(python)
  else()
    message(WARNING "CLINGLITE_BUILD_PYTHON=ON but Python dev not found — skipping")
  endif()
endif()
