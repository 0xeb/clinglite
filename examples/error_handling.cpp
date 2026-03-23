//
// Demonstrates how to capture Clang diagnostics using Session's evalSnippet
// and evalExpr error strings, plus the raw setErrorCallback API.

#include <clinglite/clinglite.h>
#include <cstdio>
#include <cstdlib>
#include <string>

int main() {
    clinglite::Environment env("error_handling");

    clinglite::Options opts;
    opts.args = {"error_handling"};
    if (const char* dir = getenv("CLING_DIR"))
        opts.llvmDir = dir;
    clinglite::Interpreter interp(opts);
    if (!interp.isValid()) {
        fprintf(stderr, "Failed to initialize interpreter\n");
        return 1;
    }

    clinglite::Session session(&interp);

    // 1. evalSnippet with error capture
    printf("=== evalSnippet error capture ===\n");
    {
        std::string error;
        bool ok = session.evalSnippet("undefined_var;", &error);
        printf("Success: %s\n", ok ? "yes" : "no");
        printf("Error: %s\n\n", error.c_str());
    }

    // 2. evalExpr with error capture
    printf("=== evalExpr error capture ===\n");
    {
        clinglite::Value v;
        std::string error;
        bool ok = session.evalExpr("bad_expr!!!", v, &error);
        printf("Success: %s\n", ok ? "yes" : "no");
        printf("Error: %s\n\n", error.c_str());
    }

    // 3. Crash recovery
    printf("=== Crash recovery ===\n");
    {
        std::string error;
        bool ok = session.evalSnippet("*(int*)0 = 42;", &error);
        printf("Success: %s\n", ok ? "yes" : "no");
        printf("Error: %s\n\n", error.c_str());
    }

    // 4. Interpreter still works
    printf("=== After errors, interpreter works ===\n");
    {
        clinglite::Value v;
        bool ok = session.evalExpr("2 + 2", v);
        printf("2 + 2 = %lld (ok=%s)\n", (long long)v.asInt(),
               ok ? "yes" : "no");
    }

    // 5. Raw setErrorCallback
    printf("\n=== Raw setErrorCallback ===\n");
    {
        std::string captured;
        interp.setErrorCallback([&](const std::string& s) {
            captured += s;
        });

        interp.execute("multiple bad1; bad2;");

        interp.setErrorCallback(nullptr);
        printf("Captured diagnostics:\n%s\n", captured.c_str());
    }

    return 0;
}
