"""clinglite -- Python bindings for the clinglite C++ interpreter library."""

from clinglite._core import (
    # Enums
    ExecResult,
    EntrypointArgStyle,
    # Classes
    Environment,
    Value,
    Options,
    CompletionResult,
    Interpreter,
    Session,
    ScriptRunner,
    EntrypointCandidate,
    FileExecOptions,
    ScriptRunnerStatus,
    # Free functions
    standard_main_candidates,
    cpp_string_literal,
    escape_path,
)

__all__ = [
    "ExecResult",
    "EntrypointArgStyle",
    "Environment",
    "Value",
    "Options",
    "CompletionResult",
    "Interpreter",
    "Session",
    "ScriptRunner",
    "EntrypointCandidate",
    "FileExecOptions",
    "ScriptRunnerStatus",
    "standard_main_candidates",
    "cpp_string_literal",
    "escape_path",
]
