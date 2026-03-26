// msvc_jit_exports.h — Force-export MSVC CRT symbols for Cling's JIT
// Copyright (c) Elias Bachaalany
// SPDX-License-Identifier: MIT
//
// When building an executable with /MT (static CRT), essential CRT symbols
// are linked into the .exe but NOT exported. Cling's ORC JIT linker uses
// GetProcAddress on the host process, which only finds exported symbols.
//
// Include this header from any MSVC executable that uses clinglite's JIT
// (interpreter, tests, examples) to export the required symbols.

#pragma once

#if defined(_MSC_VER) && !defined(_WINDLL)
#pragma comment(linker, "/EXPORT:??_7type_info@@6B@,DATA")
#pragma comment(linker, "/EXPORT:??2@YAPEAX_K@Z")           // operator new(size_t)
#pragma comment(linker, "/EXPORT:??3@YAXPEAX@Z")             // operator delete(void*)
#pragma comment(linker, "/EXPORT:??3@YAXPEAX_K@Z")           // operator delete(void*, size_t)
#pragma comment(linker, "/EXPORT:??_U@YAPEAX_K@Z")           // operator new[](size_t)
#pragma comment(linker, "/EXPORT:??_V@YAXPEAX@Z")            // operator delete[](void*)
#pragma comment(linker, "/EXPORT:??_V@YAXPEAX_K@Z")          // operator delete[](void*, size_t)
#pragma comment(linker, "/EXPORT:malloc")
#pragma comment(linker, "/EXPORT:free")
#pragma comment(linker, "/EXPORT:calloc")
#pragma comment(linker, "/EXPORT:realloc")
#pragma comment(linker, "/EXPORT:_purecall")
#pragma comment(linker, "/EXPORT:_CxxThrowException")
#pragma comment(linker, "/EXPORT:__std_terminate")
#endif
