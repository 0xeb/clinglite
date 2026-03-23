# clinglite API Reference

## Environment

Global LLVM/Cling lifecycle manager. Created once, before any Interpreter.

| Method | Description |
|--------|-------------|
| `Environment(argv0)` | Initialize LLVM, enable crash recovery |
| `crashRecoveryEnabled()` | Whether crash recovery is active |
| `version()` | Cling version string (e.g. `"1.4~dev"`) — static |

## Value

Wraps `cling::Value`. Type-safe extraction, no LLVM types exposed.

| Method | Return type | Description |
|--------|-------------|-------------|
| `isValid()` | `bool` | Type is known |
| `hasValue()` | `bool` | Non-void result |
| `asInt()` | `int64_t` | Extract as signed integer |
| `asUInt()` | `uint64_t` | Extract as unsigned integer |
| `asDouble()` | `double` | Extract as floating point |
| `asPtr()` | `void*` | Extract as pointer |
| `toString()` | `std::string` | Cling's value printer output |
| `nativeHandle()` | `void*` | Raw `cling::Value*` escape hatch |

## Interpreter

Independent C++ interpreter session with its own symbol table, declarations, and state.

| Method | Description |
|--------|-------------|
| `Interpreter(opts)` | Create interpreter with given options |
| `isValid()` | Whether the interpreter initialized successfully — check after construction |
| **Code execution** | |
| `execute(code)` | Compile and run C++ code |
| `execute(code, result)` | Execute and capture result value |
| `executeSafe(code, crashCode)` | Execute with crash recovery |
| `executeSafe(code, result, crashCode)` | Execute with crash recovery, capturing result |
| **Headers & includes** | |
| `addIncludePath(path)` | Add `-I` search path |
| `includeHeader(header)` | `#include <header>` |
| **File loading** | |
| `loadFile(path, errbuf)` | Load and execute a `.cpp` file |
| `declareFile(path, errbuf)` | Load via DeclareInternal (Cling's `.L` — supports function defs at file scope) |
| `loadFile(path, ns, errbuf)` | Load file wrapped in `namespace ns { ... }` |
| **Virtual filesystem** | |
| `addVirtualFile(path, content)` | Register an in-memory file for `#include` |
| **Libraries** | |
| `loadLibrary(path)` | Load a DLL/SO for JIT symbol resolution |
| **REPL** | |
| `processLine(line, result, crashCode)` | Process one REPL line with dot commands and crash recovery. Returns: >0 continuation indent, 0 done, <0 quit |
| `cancelContinuation()` | Reset multiline parsing state |
| **Symbol lookup** | |
| `getSymbolAddress(name, fromJIT)` | Look up a global symbol by mangled name |
| **Code completion** | |
| `codeComplete(line, cursor)` | Get completions at cursor. `cursor` is in/out — adjusted to token start on return |
| `codeCompleteWithContext(line, cursor)` | Completions with prefix string and byte range `[matchStart, matchEnd)` for replacement |
| **Undo / rollback** | |
| `undo(n)` | Undo the last N transactions |
| `undoableCount()` | Number of undoable transactions |
| **Output capture** | |
| `setOutputCallback(cb)` | Redirect interpreter output |
| `setErrorCallback(cb)` | Redirect diagnostics |
| `getErrorCallback()` | Get current error callback (may be null) |
| **PCH** | |
| `generatePCH(outputPath)` | Serialize interpreter state to a PCH file |
| **Declaration enumeration** | |
| `enumerateDeclarations(sourceFilter)` | List visible declarations (optionally filtered by source path) |
| `enumerateLibraryExports(libraryPath)` | List exported symbols from a shared library — static method |
| **Escape hatch** | |
| `nativeHandle()` | Raw `cling::Interpreter*` |

## Session

Higher-level wrapper around Interpreter for common use patterns. Provides error capture, crash recovery, and state management.

| Method | Description |
|--------|-------------|
| `Session(interp)` | Create a session bound to an existing interpreter (interpreter must outlive session) |
| `evalSnippet(code, error)` | Execute code with error capture and crash recovery. Returns `true` on success |
| `evalExpr(expr, result, error)` | Evaluate expression, capture result. Returns `true` on success |
| `processLine(line, result, crashCode)` | REPL line processing with `.clear` interception. Returns: >0 continuation, 0 done, <0 quit |
| `clear()` | Undo all user transactions, restoring initial state |
| `interp()` | Access the underlying interpreter |

## ScriptRunner

Loads and executes C++ script files with namespace isolation, argument passing, and entrypoint resolution.

| Method | Description |
|--------|-------------|
| `ScriptRunner(session)` | Create a runner bound to a session (session must outlive runner) |
| `setNamespacePrefix(prefix)` | Set namespace prefix for file executions (default: `"clinglite_exec_"`) |
| `setRuntimeNamespace(ns)` | Set runtime namespace injected into each file (default: `"clinglite_runtime"`) |
| `compileFile(file, ns, error)` | Compile file, optionally wrapped in a namespace. No entrypoint probing |
| `execFile(file, options, result, error)` | Load file with namespace isolation, inject runtime argv, invoke entrypoint |
| `status()` | Snapshot of runner state (`ScriptRunnerStatus`) |
| `resetStatus()` | Clear status (preserves configuration) |
| `session()` | Access the underlying session |

## Types

| Type | Description |
|------|-------------|
| `ExecResult` | Enum: `Success`, `Failure`, `MoreInputExpected` |
| `CompletionResult` | Struct: `completions` (vector), `prefix` (string), `matchStart`/`matchEnd` (byte offsets) |
| `EntrypointCandidate` | Struct: `label`, `symbol`, `signature`, `argStyle`, `returnsValue` |
| `EntrypointArgStyle` | Enum: `MutableArgv` (`int, char**`), `ConstArgv` (`int, const char**`), `NoArgs` |
| `FileExecOptions` | Struct: `args` (vector), `entrypointGroups` (priority-ordered candidates), `allowNoEntrypoint` |
| `ScriptRunnerStatus` | Struct: `lastFileExecuted`, `lastFileNamespace`, `lastEntrypointChosen`, `lastError` |
| `Options` | Struct: `args`, `includePaths`, `compilerFlags`, `llvmDir`, `pchPath` |
| `OutputCallback` | `std::function<void(const std::string&)>` |

## Free functions

| Function | Description |
|----------|-------------|
| `escapePath(path)` | Escape backslashes for C++ string literals |
| `standardMainCandidates()` | Return standard `main()` entrypoint candidates |
| `cppStringLiteral(s)` | Escape and quote a string for use as a C++ string literal |
