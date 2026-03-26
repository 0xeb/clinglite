// clinglite — lightweight Cling wrapper library
// Copyright (c) Elias Bachaalany
// SPDX-License-Identifier: MIT
// PIMPL'd C++ API; no LLVM/Cling types in public interface.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <clinglite/clinglite_export.h>
#include <clinglite/msvc_jit_exports.h>

namespace clinglite {

// ── Result of code execution ────────────────────────────────────────────────

enum class ExecResult {
    Success,            // Code compiled and executed OK
    Failure,            // Compilation or execution error
    MoreInputExpected   // Incomplete input (missing closing brace, etc.)
};

// ── Code completion result ──────────────────────────────────────────────────

struct CompletionResult {
    std::vector<std::string> completions;  ///< candidate strings
    std::string prefix;                     ///< typed prefix that matched
    size_t matchStart = 0;                  ///< byte offset where match begins
    size_t matchEnd = 0;                    ///< byte offset where match ends (== cursor)
};

// ── Output callback type ────────────────────────────────────────────────────

using OutputCallback = std::function<void(const std::string&)>;

// ── Environment ─────────────────────────────────────────────────────────────
//
// Global LLVM/Cling lifecycle manager. Created once, before any Interpreter.
// Owns the LLVM shutdown trigger and crash recovery state.
// Not thread-safe. Only one instance should exist at a time.

class CLINGLITE_API Environment {
public:
    /// Initialize LLVM subsystems. argv0 enables stack trace symbolication.
    /// Enables crash recovery (SEH on Windows), suppresses crash dialogs,
    /// registers signal handlers.
    explicit Environment(const char* argv0 = nullptr);
    ~Environment();

    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;

    /// Whether crash recovery is active.
    bool crashRecoveryEnabled() const;

    /// Cling version string (e.g. "1.4~dev").
    static std::string version();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ── Value ───────────────────────────────────────────────────────────────────
//
// Wraps cling::Value. Type-safe extraction, no LLVM types exposed.

class CLINGLITE_API Value {
public:
    Value();
    ~Value();
    Value(Value&& other) noexcept;
    Value& operator=(Value&& other) noexcept;
    Value(const Value& other);
    Value& operator=(const Value& other);

    bool isValid() const;    // type is known
    bool hasValue() const;   // not void

    int64_t     asInt() const;
    uint64_t    asUInt() const;
    double      asDouble() const;
    void*       asPtr() const;
    std::string toString() const;   // Cling's value printer -> string

    /// Access the underlying cling::Value*.
    void* nativeHandle();
    const void* nativeHandle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    friend class Interpreter;
};

// ── Interpreter options ─────────────────────────────────────────────────────

struct Options {
    std::vector<std::string> args;           // argv[0] + any raw cling flags
    std::vector<std::string> includePaths;   // -I paths
    std::vector<std::string> compilerFlags;  // -D, -std=, -isystem, etc.

    // Optional override for the local Cling/LLVM build tree.
    // When set, Cling locates its resource directory at
    // {llvmDir}/lib/clang/{ver}/ instead of using the baked source-tree
    // default compiled into clinglite.
    // Typically points at the local Cling build tree root.
    std::string llvmDir;

    // Pre-compiled header: loaded at construction time (file-based).
    std::string pchPath;
};

// ── Interpreter ─────────────────────────────────────────────────────────────
//
// Each instance is an independent C++ interpreter session with its own symbol
// table, declarations, and state. Multiple Interpreters can coexist from the
// same Environment.
//
// Not thread-safe. Each Interpreter must be used from a single thread.

class CLINGLITE_API Interpreter {
public:
    explicit Interpreter(const Options& opts);
    ~Interpreter();

    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    /// Whether the interpreter initialized successfully.
    bool isValid() const;

    // ── Code execution ──────────────────────────────────────────────────

    /// Compile and run code.
    ExecResult execute(const std::string& code);

    /// Compile and run code, capturing the result value.
    ExecResult execute(const std::string& code, Value& result);

    /// Execute with crash recovery. On crash, returns Failure and sets
    /// *crashCode to the platform crash code (e.g. SIGSEGV, 0xC0000005).
    ExecResult executeSafe(const std::string& code,
                           int* crashCode = nullptr);

    /// Execute with crash recovery, capturing the result value.
    ExecResult executeSafe(const std::string& code, Value& result,
                           int* crashCode = nullptr);

    // ── Headers & includes ──────────────────────────────────────────────

    /// Add an -I search path.
    void addIncludePath(const std::string& path);

    /// #include <header>. Returns true on success.
    bool includeHeader(const std::string& header);

    // ── File loading ────────────────────────────────────────────────────

    /// Load and execute a .cpp file.
    ExecResult loadFile(const std::string& path,
                        std::string* errbuf = nullptr);

    /// Load a .cpp file via #include + DeclareInternal (same as Cling's .L).
    /// Function definitions, class definitions, and expression statements
    /// all work correctly at file scope.
    ExecResult declareFile(const std::string& path,
                           std::string* errbuf = nullptr);

    /// Load file wrapped in namespace ns { ... }.
    ExecResult loadFile(const std::string& path, const std::string& ns,
                        std::string* errbuf = nullptr);

    // ── Virtual filesystem ──────────────────────────────────────────────

    /// Register an in-memory file. #include will find it by path.
    /// Useful for embedding SDK headers without filesystem dependency.
    void addVirtualFile(const std::string& path,
                        const std::string& content);

    // ── Libraries ───────────────────────────────────────────────────────

    /// Load a DLL/SO for JIT symbol resolution.
    ExecResult loadLibrary(const std::string& path);

    // ── REPL / MetaProcessor ────────────────────────────────────────────

    /// Process one REPL line with crash recovery.
    /// Handles dot commands (.L, .x, .q, .help).
    /// Returns: >0 = continuation indent, 0 = done, <0 = quit.
    int processLine(const std::string& line,
                    ExecResult* result = nullptr,
                    int* crashCode = nullptr);

    /// Reset multiline state.
    void cancelContinuation();

    // ── Symbol lookup ────────────────────────────────────────────────────

    /// Look up a global symbol by its (mangled) name.
    /// Returns its address, or nullptr if not found.
    /// If fromJIT is non-null, *fromJIT indicates whether the symbol
    /// lives in JIT code (true) or a loaded library (false).
    void* getSymbolAddress(const std::string& mangledName,
                           bool* fromJIT = nullptr);

    // ── Code completion ─────────────────────────────────────────────────

    /// Get code completions at cursor position in a partial line.
    /// cursor is in/out: on return, may be adjusted to the start of the
    /// token being completed (for prefix replacement in REPL).
    std::vector<std::string> codeComplete(const std::string& line,
                                          size_t& cursor);

    /// Get code completions with match range information.
    /// Returns completions filtered by prefix, plus the prefix string
    /// and the byte range [matchStart, matchEnd) to replace in line.
    CompletionResult codeCompleteWithContext(const std::string& line,
                                            size_t cursor);

    // ── Undo / rollback ─────────────────────────────────────────────────

    /// Undo the N most recent transactions. Returns how many were
    /// actually undone. The initial runtime transaction cannot be undone.
    unsigned undo(unsigned n = 1);

    /// Number of undoable transactions (excludes runtime init).
    unsigned undoableCount() const;

    // ── Output capture ──────────────────────────────────────────────────

    /// Redirect interpreter output (value printing, etc.).
    void setOutputCallback(OutputCallback cb);

    /// Redirect diagnostics/errors.
    void setErrorCallback(OutputCallback cb);

    /// Get the current error callback (may be null).
    OutputCallback getErrorCallback() const;

    // ── PCH generation ────────────────────────────────────────────────

    /// Serialize the current interpreter state (all included headers) to
    /// a pre-compiled header file. On subsequent runs, load with pchPath.
    /// Returns true on success.
    bool generatePCH(const std::string& outputPath);

    // ── Declaration enumeration ────────────────────────────────────────

    /// Enumerate all named declarations visible in the interpreter.
    /// If sourceFilter is non-empty, only include declarations whose
    /// source file path starts with sourceFilter (e.g., a project include dir).
    /// Returns a list of unqualified identifier names (not sorted).
    std::vector<std::string> enumerateDeclarations(
        const std::string& sourceFilter = "");

    /// Enumerate exported symbol names from a shared library.
    /// Supports ELF (.so) and PE/COFF (.dll) formats via LLVM's Object API.
    /// Returns a list of symbol names (not sorted).
    static std::vector<std::string> enumerateLibraryExports(
        const std::string& libraryPath);

    // ── Escape hatch ────────────────────────────────────────────────────

    /// Raw cling::Interpreter* for advanced use.
    void* nativeHandle();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ── Session ─────────────────────────────────────────────────────────────────
//
// Higher-level wrapper around Interpreter for common use patterns.
// Provides error capture, crash recovery, and state management (.clear).
// Suited for both snippet execution (eval_snippet) and REPL (cli_execute_line).
//
// Not thread-safe. Each Session must be used from a single thread.

class CLINGLITE_API Session {
public:
    /// Create a session bound to an existing interpreter.
    /// The interpreter must outlive the session.
    explicit Session(Interpreter* interp);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    /// Execute a code snippet with error capture and crash recovery.
    /// No auto-wrapping — callers wrap in {} for isolation if desired.
    /// Returns true on success. On failure, *error receives diagnostics.
    bool evalSnippet(const std::string& code, std::string* error = nullptr);

    /// Evaluate an expression, capture result. Error capture + crash recovery.
    /// Returns true on success.
    bool evalExpr(const std::string& expr, Value& result,
                  std::string* error = nullptr);

    /// Process one REPL line (persistent state). Intercepts .clear.
    /// Returns: >0 = continuation indent, 0 = done, <0 = quit.
    int processLine(const std::string& line,
                    ExecResult* result = nullptr, int* crashCode = nullptr);

    /// Undo all user transactions, restoring initial state.
    void clear();

    /// Access the underlying interpreter.
    Interpreter* interp() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ── Script execution types ──────────────────────────────────────────────────

/// How to pass arguments to an entrypoint function.
enum class EntrypointArgStyle {
    MutableArgv,    // int main(int argc, char** argv)
    ConstArgv,      // int main(int argc, const char** argv)
    NoArgs,         // int main() or void main()
};

/// One candidate entrypoint to probe for in a script file.
struct EntrypointCandidate {
    std::string label;       // e.g. "main(int, char**)"
    std::string symbol;      // e.g. "main"
    std::string signature;   // e.g. "int(*)(int, char**)"
    EntrypointArgStyle argStyle = EntrypointArgStyle::NoArgs;
    bool returnsValue = true;
};

/// Options for script file execution.
struct FileExecOptions {
    std::vector<std::string> args;

    /// Entrypoint candidates to probe, in priority order.
    /// Groups are tried in order; within each group, the first matching
    /// candidate is invoked. If empty, the file is loaded without invoking
    /// any function.
    std::vector<std::vector<EntrypointCandidate>> entrypointGroups;

    /// When true and no entrypoint is found, succeed (load-only).
    /// When false and no entrypoint is found, fail with error.
    bool allowNoEntrypoint = true;
};

/// Snapshot of script runner state.
struct ScriptRunnerStatus {
    std::string lastFileExecuted;
    std::string lastFileNamespace;
    std::string lastEntrypointChosen;
    std::string lastError;
};

// ── ScriptRunner ────────────────────────────────────────────────────────────
//
// Loads and executes C++ script files with namespace isolation, argument
// passing, and entrypoint resolution. Each file is wrapped in a unique
// namespace; a runtime sub-namespace provides argc/argv access.
//
// Not thread-safe. Bound to a single Session for its lifetime.

class CLINGLITE_API ScriptRunner {
public:
    /// Create a runner bound to an existing session.
    /// The session (and its interpreter) must outlive the runner.
    explicit ScriptRunner(Session* session);
    ~ScriptRunner();

    ScriptRunner(const ScriptRunner&) = delete;
    ScriptRunner& operator=(const ScriptRunner&) = delete;

    // ── Configuration ────────────────────────────────────────────────

    /// Set the namespace prefix for file executions.
    /// Default: "clinglite_exec_"
    void setNamespacePrefix(const std::string& prefix);

    /// Set the runtime namespace name injected into each file.
    /// Default: "clinglite_runtime"
    void setRuntimeNamespace(const std::string& ns);

    // ── File operations ──────────────────────────────────────────────

    /// Compile a file, optionally wrapped in a namespace.
    /// Does not inject runtime argv or probe for entrypoints.
    bool compileFile(const std::string& file,
                     const char* requestedNamespace = nullptr,
                     std::string* error = nullptr);

    /// Load a file with namespace isolation, inject runtime argv,
    /// and optionally invoke an entrypoint.
    bool execFile(const std::string& file,
                  const FileExecOptions& options = {},
                  Value* result = nullptr,
                  std::string* error = nullptr);

    // ── Status ───────────────────────────────────────────────────────

    /// Return a snapshot of the runner's status.
    ScriptRunnerStatus status() const;

    /// Reset status (preserves configuration).
    void resetStatus();

    /// Access the underlying session.
    Session* session() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ── Free functions ──────────────────────────────────────────────────────────

/// Return the standard main() entrypoint candidates:
/// main(int, char**), main(int, const char**), main(), main(void).
CLINGLITE_API std::vector<EntrypointCandidate> standardMainCandidates();

/// Escape a string for use as a C++ string literal (with quotes).
CLINGLITE_API std::string cppStringLiteral(const std::string& s);

/// Escape backslashes in a path for use in C++ string literals.
CLINGLITE_API std::string escapePath(const std::string& path);

// ── Plugin system ───────────────────────────────────────────────────────────

/// Undo IDA SDK pro.h macro poisoning (#undef snprintf, sprintf, getenv).
/// Harmless no-op if macros are not defined (e.g. PCH already handled it).
inline void undoProhPoisoning(Interpreter& interp) {
    interp.execute("#undef snprintf");
    interp.execute("#undef sprintf");
    interp.execute("#undef getenv");
}

/// Options passed to plugin setup functions. Extensible for future fields.
struct PluginSetupOptions {
    bool hasPch = false;  ///< Whether a precompiled header was loaded
};

namespace plugins {

/// Run setup for all registered extension plugins (auto-generated at build time).
CLINGLITE_API void setupAll(Interpreter& interp, PluginSetupOptions& opts);

/// Return names of all enabled extension plugins (baked at build time).
CLINGLITE_API std::vector<std::string> pluginNames();

} // namespace plugins

} // namespace clinglite
