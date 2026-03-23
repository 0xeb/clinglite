//
// executeSafe() wraps execution in a crash recovery context.
// Null pointer dereferences, illegal instructions, etc. are caught
// and the interpreter remains usable.

#include <clinglite/clinglite.h>
#include <cstdio>
#include <cstdlib>

int main() {
    clinglite::Environment env("crash_recovery");

    clinglite::Options opts;
    opts.args = {"crash_recovery"};
    if (const char* dir = getenv("CLING_DIR"))
        opts.llvmDir = dir;
    clinglite::Interpreter interp(opts);

    // This would normally crash the process
    int crashCode = 0;
    auto r = interp.executeSafe("*(int*)0 = 42;", &crashCode);

    printf("Result: %s, crash code: 0x%x\n",
           r == clinglite::ExecResult::Failure ? "Failure" : "?",
           crashCode);

    // Interpreter is still alive and functional
    clinglite::Value result;
    interp.executeSafe("2 + 2", result);
    printf("After crash: 2 + 2 = %lld\n", (long long)result.asInt());

    return 0;
}
