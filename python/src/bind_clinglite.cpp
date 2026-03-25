// clinglite Python bindings (nanobind)
// Copyright (c) Elias Bachaalany
// SPDX-License-Identifier: MIT

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/function.h>

#include <clinglite/clinglite.h>

namespace nb = nanobind;
using namespace nb::literals;

NB_MODULE(_core, m) {
    m.doc() = "clinglite -- Python bindings for the clinglite C++ interpreter library";

    // ── ExecResult ──────────────────────────────────────────────────────────

    nb::enum_<clinglite::ExecResult>(m, "ExecResult")
        .value("Success", clinglite::ExecResult::Success)
        .value("Failure", clinglite::ExecResult::Failure)
        .value("MoreInputExpected", clinglite::ExecResult::MoreInputExpected);

    // ── EntrypointArgStyle ──────────────────────────────────────────────────

    nb::enum_<clinglite::EntrypointArgStyle>(m, "EntrypointArgStyle")
        .value("MutableArgv", clinglite::EntrypointArgStyle::MutableArgv)
        .value("ConstArgv", clinglite::EntrypointArgStyle::ConstArgv)
        .value("NoArgs", clinglite::EntrypointArgStyle::NoArgs);

    // ── Environment ─────────────────────────────────────────────────────────

    nb::class_<clinglite::Environment>(m, "Environment",
        "Global LLVM/Cling lifecycle manager. Create once before any Interpreter.")
        .def(nb::init<const char *>(), "argv0"_a = nullptr)
        .def("crash_recovery_enabled", &clinglite::Environment::crashRecoveryEnabled,
             "Whether crash recovery is active.")
        .def_static("version", &clinglite::Environment::version,
                    "Cling version string.")
        .def("__enter__", [](clinglite::Environment &self) -> clinglite::Environment & {
            return self;
        }, nb::rv_policy::reference)
        .def("__exit__", [](clinglite::Environment &, nb::args) {});

    // ── Value ───────────────────────────────────────────────────────────────

    nb::class_<clinglite::Value>(m, "Value",
        "Wraps a Cling value with type-safe extraction.")
        .def(nb::init<>())
        .def_prop_ro("valid", &clinglite::Value::isValid,
                     "Whether the type is known.")
        .def_prop_ro("has_value", &clinglite::Value::hasValue,
                     "Whether the value is not void.")
        .def("as_int", &clinglite::Value::asInt, "Extract as signed 64-bit int.")
        .def("as_uint", &clinglite::Value::asUInt, "Extract as unsigned 64-bit int.")
        .def("as_double", &clinglite::Value::asDouble, "Extract as double.")
        .def("as_ptr", [](const clinglite::Value &v) -> uintptr_t {
            return reinterpret_cast<uintptr_t>(v.asPtr());
        }, "Extract as integer address.")
        .def("__repr__", [](const clinglite::Value &v) -> std::string {
            if (!v.isValid()) return "<Value: invalid>";
            if (!v.hasValue()) return "<Value: void>";
            auto s = v.toString();
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
                s.pop_back();
            return "<Value: " + s + ">";
        })
        .def("__str__", [](const clinglite::Value &v) -> std::string {
            auto s = v.toString();
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
                s.pop_back();
            return s;
        })
        .def("__int__", &clinglite::Value::asInt)
        .def("__float__", &clinglite::Value::asDouble)
        .def("__bool__", [](const clinglite::Value &v) -> bool {
            return v.hasValue() && v.asInt() != 0;
        });

    // ── Options ─────────────────────────────────────────────────────────────

    nb::class_<clinglite::Options>(m, "Options", "Interpreter configuration.")
        .def(nb::init<>())
        .def_rw("args", &clinglite::Options::args,
                "argv[0] + any raw cling flags.")
        .def_rw("include_paths", &clinglite::Options::includePaths,
                "-I paths.")
        .def_rw("compiler_flags", &clinglite::Options::compilerFlags,
                "-D, -std=, -isystem, etc.")
        .def_rw("llvm_dir", &clinglite::Options::llvmDir,
                "Override for Cling/LLVM build tree root.")
        .def_rw("pch_path", &clinglite::Options::pchPath,
                "Pre-compiled header file path.");

    // ── CompletionResult ────────────────────────────────────────────────────

    nb::class_<clinglite::CompletionResult>(m, "CompletionResult",
        "Code completion result with context.")
        .def(nb::init<>())
        .def_ro("completions", &clinglite::CompletionResult::completions)
        .def_ro("prefix", &clinglite::CompletionResult::prefix)
        .def_ro("match_start", &clinglite::CompletionResult::matchStart)
        .def_ro("match_end", &clinglite::CompletionResult::matchEnd);

    // ── Interpreter ─────────────────────────────────────────────────────────

    nb::class_<clinglite::Interpreter>(m, "Interpreter",
        "Independent C++ interpreter session.")
        .def(nb::init<const clinglite::Options &>(), "opts"_a)
        .def_prop_ro("valid", &clinglite::Interpreter::isValid,
                     "Whether initialization succeeded.")

        // execute: raises on failure, returns Value or None
        .def("execute", [](clinglite::Interpreter &self, const std::string &code) -> nb::object {
            clinglite::Value result;
            auto r = self.execute(code, result);
            if (r == clinglite::ExecResult::Failure)
                throw std::runtime_error("Execution failed");
            if (r == clinglite::ExecResult::MoreInputExpected)
                throw std::runtime_error("Incomplete input");
            if (result.hasValue())
                return nb::cast(std::move(result));
            return nb::none();
        }, "code"_a, "Execute C++ code. Returns Value or None. Raises on failure.")

        // execute_raw: returns ExecResult (no exception)
        .def("execute_raw", [](clinglite::Interpreter &self, const std::string &code) {
            return self.execute(code);
        }, "code"_a, "Execute C++ code, return ExecResult.")

        // execute_with_value: returns (ExecResult, Value)
        .def("execute_with_value", [](clinglite::Interpreter &self, const std::string &code) {
            clinglite::Value result;
            auto r = self.execute(code, result);
            return std::make_pair(r, std::move(result));
        }, "code"_a, "Execute code, return (ExecResult, Value) tuple.")

        // execute_safe: returns (ExecResult, Value, crash_code)
        .def("execute_safe", [](clinglite::Interpreter &self, const std::string &code) {
            clinglite::Value result;
            int crashCode = 0;
            auto r = self.executeSafe(code, result, &crashCode);
            return nb::make_tuple(r, std::move(result), crashCode);
        }, "code"_a, "Execute with crash recovery. Returns (ExecResult, Value, crash_code).")

        // Headers & includes
        .def("add_include_path", &clinglite::Interpreter::addIncludePath, "path"_a)
        .def("include_header", &clinglite::Interpreter::includeHeader, "header"_a,
             "Include a header. Returns True on success.")

        // File loading (raise on error)
        .def("load_file", [](clinglite::Interpreter &self, const std::string &path) {
            std::string errbuf;
            auto r = self.loadFile(path, &errbuf);
            if (r == clinglite::ExecResult::Failure)
                throw std::runtime_error(errbuf.empty() ? "loadFile failed" : errbuf);
            return r;
        }, "path"_a, "Load and execute a .cpp file. Raises on failure.")

        .def("declare_file", [](clinglite::Interpreter &self, const std::string &path) {
            std::string errbuf;
            auto r = self.declareFile(path, &errbuf);
            if (r == clinglite::ExecResult::Failure)
                throw std::runtime_error(errbuf.empty() ? "declareFile failed" : errbuf);
            return r;
        }, "path"_a, "Load file via #include + DeclareInternal. Raises on failure.")

        .def("load_file_namespaced", [](clinglite::Interpreter &self,
                                         const std::string &path, const std::string &ns) {
            std::string errbuf;
            auto r = self.loadFile(path, ns, &errbuf);
            if (r == clinglite::ExecResult::Failure)
                throw std::runtime_error(errbuf.empty() ? "loadFile failed" : errbuf);
            return r;
        }, "path"_a, "ns"_a, "Load file wrapped in namespace. Raises on failure.")

        // Virtual filesystem
        .def("add_virtual_file", &clinglite::Interpreter::addVirtualFile,
             "path"_a, "content"_a, "Register an in-memory file for #include.")

        // Libraries
        .def("load_library", &clinglite::Interpreter::loadLibrary,
             "path"_a, "Load a DLL/SO for JIT symbol resolution.")

        // REPL
        .def("process_line", [](clinglite::Interpreter &self, const std::string &line) {
            clinglite::ExecResult result;
            int crashCode = 0;
            int indent = self.processLine(line, &result, &crashCode);
            return nb::make_tuple(indent, result, crashCode);
        }, "line"_a, "Process REPL line. Returns (indent, ExecResult, crash_code).")

        .def("cancel_continuation", &clinglite::Interpreter::cancelContinuation,
             "Reset multiline state.")

        // Symbol lookup
        .def("get_symbol_address", [](clinglite::Interpreter &self,
                                       const std::string &name) {
            bool fromJIT = false;
            void *addr = self.getSymbolAddress(name, &fromJIT);
            return nb::make_tuple(reinterpret_cast<uintptr_t>(addr), fromJIT);
        }, "mangled_name"_a, "Look up symbol. Returns (address, from_jit).")

        // Code completion
        .def("code_complete", &clinglite::Interpreter::codeCompleteWithContext,
             "line"_a, "cursor"_a,
             "Get completions with context. Returns CompletionResult.")

        // Undo
        .def("undo", &clinglite::Interpreter::undo, "n"_a = 1,
             "Undo N most recent transactions.")
        .def_prop_ro("undoable_count", &clinglite::Interpreter::undoableCount)

        // Output capture
        .def("set_output_callback", [](clinglite::Interpreter &self, nb::callable cb) {
            self.setOutputCallback([cb](const std::string &s) {
                nb::gil_scoped_acquire guard;
                cb(nb::str(s.c_str(), s.size()));
            });
        }, "callback"_a, "Set output callback.")
        .def("clear_output_callback", [](clinglite::Interpreter &self) {
            self.setOutputCallback(nullptr);
        }, "Clear output callback.")

        .def("set_error_callback", [](clinglite::Interpreter &self, nb::callable cb) {
            self.setErrorCallback([cb](const std::string &s) {
                nb::gil_scoped_acquire guard;
                cb(nb::str(s.c_str(), s.size()));
            });
        }, "callback"_a, "Set error callback.")
        .def("clear_error_callback", [](clinglite::Interpreter &self) {
            self.setErrorCallback(nullptr);
        }, "Clear error callback.")

        // PCH
        .def("generate_pch", &clinglite::Interpreter::generatePCH,
             "output_path"_a, "Serialize state to PCH file.")

        // Declaration enumeration
        .def("enumerate_declarations", &clinglite::Interpreter::enumerateDeclarations,
             "source_filter"_a = "",
             "Enumerate visible declarations.")
        .def_static("enumerate_library_exports",
                    &clinglite::Interpreter::enumerateLibraryExports,
                    "library_path"_a,
                    "Enumerate exported symbols from a shared library.");

    // ── Session ─────────────────────────────────────────────────────────────

    nb::class_<clinglite::Session>(m, "Session",
        "Higher-level wrapper with error capture and crash recovery.")
        .def(nb::init<clinglite::Interpreter *>(), "interp"_a,
             nb::keep_alive<1, 2>())

        .def("eval_snippet", [](clinglite::Session &self, const std::string &code) {
            std::string error;
            bool ok = self.evalSnippet(code, &error);
            if (!ok)
                throw std::runtime_error(error.empty() ? "evalSnippet failed" : error);
        }, "code"_a, "Execute code snippet. Raises on failure.")

        .def("eval_snippet_safe", [](clinglite::Session &self, const std::string &code) {
            std::string error;
            bool ok = self.evalSnippet(code, &error);
            return nb::make_tuple(ok, error);
        }, "code"_a, "Execute code snippet. Returns (success, error_string).")

        .def("eval_expr", [](clinglite::Session &self, const std::string &expr) {
            clinglite::Value result;
            std::string error;
            bool ok = self.evalExpr(expr, result, &error);
            if (!ok)
                throw std::runtime_error(error.empty() ? "evalExpr failed" : error);
            return result;
        }, "expr"_a, "Evaluate expression, return Value. Raises on failure.")

        .def("process_line", [](clinglite::Session &self, const std::string &line) {
            clinglite::ExecResult result;
            int crashCode = 0;
            int indent = self.processLine(line, &result, &crashCode);
            return nb::make_tuple(indent, result, crashCode);
        }, "line"_a, "Process REPL line. Returns (indent, ExecResult, crash_code).")

        .def("clear", &clinglite::Session::clear,
             "Undo all user transactions, restoring initial state.")
        .def_prop_ro("interp", &clinglite::Session::interp,
                     nb::rv_policy::reference);

    // ── EntrypointCandidate ─────────────────────────────────────────────────

    nb::class_<clinglite::EntrypointCandidate>(m, "EntrypointCandidate",
        "Entrypoint probe descriptor for script execution.")
        .def(nb::init<>())
        .def_rw("label", &clinglite::EntrypointCandidate::label)
        .def_rw("symbol", &clinglite::EntrypointCandidate::symbol)
        .def_rw("signature", &clinglite::EntrypointCandidate::signature)
        .def_rw("arg_style", &clinglite::EntrypointCandidate::argStyle)
        .def_rw("returns_value", &clinglite::EntrypointCandidate::returnsValue);

    // ── FileExecOptions ─────────────────────────────────────────────────────

    nb::class_<clinglite::FileExecOptions>(m, "FileExecOptions",
        "Options for script file execution.")
        .def(nb::init<>())
        .def_rw("args", &clinglite::FileExecOptions::args)
        .def_rw("entrypoint_groups", &clinglite::FileExecOptions::entrypointGroups)
        .def_rw("allow_no_entrypoint", &clinglite::FileExecOptions::allowNoEntrypoint);

    // ── ScriptRunnerStatus ──────────────────────────────────────────────────

    nb::class_<clinglite::ScriptRunnerStatus>(m, "ScriptRunnerStatus",
        "Snapshot of script runner state.")
        .def(nb::init<>())
        .def_ro("last_file_executed", &clinglite::ScriptRunnerStatus::lastFileExecuted)
        .def_ro("last_file_namespace", &clinglite::ScriptRunnerStatus::lastFileNamespace)
        .def_ro("last_entrypoint_chosen", &clinglite::ScriptRunnerStatus::lastEntrypointChosen)
        .def_ro("last_error", &clinglite::ScriptRunnerStatus::lastError);

    // ── ScriptRunner ────────────────────────────────────────────────────────

    nb::class_<clinglite::ScriptRunner>(m, "ScriptRunner",
        "Loads and executes C++ script files with namespace isolation.")
        .def(nb::init<clinglite::Session *>(), "session"_a,
             nb::keep_alive<1, 2>())

        .def("set_namespace_prefix", &clinglite::ScriptRunner::setNamespacePrefix,
             "prefix"_a)
        .def("set_runtime_namespace", &clinglite::ScriptRunner::setRuntimeNamespace,
             "ns"_a)

        .def("compile_file", [](clinglite::ScriptRunner &self,
                                 const std::string &file, nb::object ns) {
            std::string error;
            std::string ns_str;
            const char *ns_ptr = nullptr;
            if (!ns.is_none()) {
                ns_str = nb::cast<std::string>(ns);
                ns_ptr = ns_str.c_str();
            }
            bool ok = self.compileFile(file, ns_ptr, &error);
            if (!ok)
                throw std::runtime_error(error.empty() ? "compileFile failed" : error);
        }, "file"_a, "namespace_"_a = nb::none(),
           "Compile a file. Raises on failure.")

        .def("exec_file", [](clinglite::ScriptRunner &self,
                              const std::string &file,
                              const clinglite::FileExecOptions &opts) {
            clinglite::Value result;
            std::string error;
            bool ok = self.execFile(file, opts, &result, &error);
            if (!ok)
                throw std::runtime_error(error.empty() ? "execFile failed" : error);
            return result;
        }, "file"_a, "options"_a = clinglite::FileExecOptions{},
           "Execute a file with namespace isolation. Raises on failure.")

        .def_prop_ro("status", &clinglite::ScriptRunner::status)
        .def("reset_status", &clinglite::ScriptRunner::resetStatus)
        .def_prop_ro("session", &clinglite::ScriptRunner::session,
                     nb::rv_policy::reference);

    // ── Free functions ──────────────────────────────────────────────────────

    m.def("standard_main_candidates", &clinglite::standardMainCandidates,
          "Return standard main() entrypoint candidates.");
    m.def("cpp_string_literal", &clinglite::cppStringLiteral, "s"_a,
          "Escape string for use as a C++ string literal.");
    m.def("escape_path", &clinglite::escapePath, "path"_a,
          "Escape backslashes in a path for C++ string literals.");
}
