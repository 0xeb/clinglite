# clinglite Python bindings (nanobind)
# Copyright (c) Elias Bachaalany
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)

# nanobind requires find_package(Python ...) not find_package(Python3 ...)
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)

# Strategy 1: pip-installed nanobind (preferred)
execute_process(
  COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE nanobind_ROOT
  RESULT_VARIABLE _nb_result
)

if(_nb_result EQUAL 0 AND EXISTS "${nanobind_ROOT}")
  message(STATUS "Using pip-installed nanobind: ${nanobind_ROOT}")
  find_package(nanobind CONFIG REQUIRED)
else()
  # Strategy 2: FetchContent fallback
  message(STATUS "nanobind not found via pip, fetching from GitHub...")
  include(FetchContent)
  FetchContent_Declare(nanobind
    GIT_REPOSITORY https://github.com/wjakob/nanobind.git
    GIT_TAG        v2.4.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(nanobind)
endif()

nanobind_add_module(_core
  STABLE_ABI
  NB_STATIC
  src/bind_clinglite.cpp
)

target_link_libraries(_core PRIVATE clinglite::clinglite)

# clinglite is built with -fno-rtti (PRIVATE), but nanobind requires RTTI.
# Since clinglite's public API has no virtual methods (pure PIMPL), linking
# an RTTI-enabled binding against the no-RTTI lib is safe. Force RTTI on
# the binding target as a safety measure.
if(MSVC)
  target_compile_options(_core PRIVATE /GR /EHsc)
else()
  target_compile_options(_core PRIVATE -frtti)
endif()

# Output the .so/.pyd into the clinglite/ package directory for in-tree import
set_target_properties(_core PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/clinglite"
)
