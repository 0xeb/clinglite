// pch_generation.cpp — Integration test for clinglite::Interpreter::generatePCH()
//
// Creates an interpreter, includes headers, generates a PCH, then verifies
// that a new interpreter loading that PCH has the declarations available.
//
// Build: link against libclinglite (see CMakeLists.txt)
// Run:   ./pch_generation

#include <clinglite/clinglite.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <io.h>
#define unlink _unlink
#else
#include <unistd.h>
#endif

static void setupRuntime(clinglite::Options& opts) {
    if (const char* dir = getenv("CLING_DIR"))
        opts.llvmDir = dir;
}

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        ++failures;
    } else {
        printf("PASS: %s\n", msg);
    }
}

int main() {
    clinglite::Environment env("pch_generation");
    printf("Testing generatePCH() — Cling %s\n",
           clinglite::Environment::version().c_str());

    std::string pchPathStr =
        (std::filesystem::temp_directory_path() / "clinglite_test.pch").string();
    const char* pchPath = pchPathStr.c_str();

    // ── Phase 1: Generate a PCH with some declarations ──────────────────
    {
        clinglite::Options opts;
        opts.args = {"pchgen", "-noruntime"};
        setupRuntime(opts);

        clinglite::Interpreter interp(opts);
        check(interp.isValid(), "phase 1: interpreter created");

        auto r1 = interp.execute("#include <cmath>");
        check(r1 == clinglite::ExecResult::Success, "phase 1: include <cmath>");

        auto r2 = interp.execute("#include <cstring>");
        check(r2 == clinglite::ExecResult::Success, "phase 1: include <cstring>");

        auto r3 = interp.execute("#include <vector>");
        check(r3 == clinglite::ExecResult::Success, "phase 1: include <vector>");

        bool ok = interp.generatePCH(pchPath);
        check(ok, "phase 1: generatePCH() returned true");

        // Check file exists and is non-empty
        FILE* f = fopen(pchPath, "rb");
        check(f != nullptr, "phase 1: PCH file exists");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fclose(f);
            check(sz > 1024, "phase 1: PCH file is non-trivial size");
            printf("  PCH size: %ld bytes\n", sz);
        }
    }

    // ── Phase 2: Load the PCH in a new interpreter ──────────────────────
    {
        clinglite::Options opts;
        opts.args = {"pchuser"};
        opts.pchPath = pchPath;
        setupRuntime(opts);

        clinglite::Interpreter interp(opts);
        check(interp.isValid(), "phase 2: interpreter with PCH created");

        // Use declarations from the PCH without re-including
        clinglite::Value val;
        auto r1 = interp.execute("std::sqrt(144.0)", val);
        check(r1 == clinglite::ExecResult::Success, "phase 2: std::sqrt from PCH");
        if (val.hasValue())
            check(val.asDouble() == 12.0, "phase 2: sqrt(144) == 12");

        auto r2 = interp.execute("std::strlen(\"hello\")", val);
        check(r2 == clinglite::ExecResult::Success, "phase 2: std::strlen from PCH");
        if (val.hasValue())
            check(val.asInt() == 5, "phase 2: strlen(\"hello\") == 5");

        // Vector should also be available
        auto r3 = interp.execute("std::vector<int> v{1,2,3}; (int)v.size()", val);
        check(r3 == clinglite::ExecResult::Success, "phase 2: std::vector from PCH");
        if (val.hasValue())
            check(val.asInt() == 3, "phase 2: vector size == 3");
    }

    // ── Cleanup ─────────────────────────────────────────────────────────
    unlink(pchPath);

    printf("\n%s (%d failure%s)\n",
           failures == 0 ? "ALL PASSED" : "SOME FAILED",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
