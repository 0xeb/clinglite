//
// Creates an interpreter, executes C++ code, and captures a result value.
//
// Build: link against libclinglite (see CMakeLists.txt)
// Run:   ./basic

#include <clinglite/clinglite.h>
#include <cstdio>
#include <cstdlib>

int main() {
    // Initialize LLVM subsystems (once, before any Interpreter)
    clinglite::Environment env("basic");
    printf("Cling version: %s\n", clinglite::Environment::version().c_str());

    // Create an interpreter session
    clinglite::Options opts;
    opts.args = {"basic"};
    if (const char* dir = getenv("CLING_DIR"))
        opts.llvmDir = dir;
    clinglite::Interpreter interp(opts);

    // Execute C++ code
    interp.execute("#include <cmath>");
    interp.execute("double x = std::sqrt(2.0);");

    // Capture a result value
    clinglite::Value result;
    interp.execute("x", result);
    printf("sqrt(2) = %f\n", result.asDouble());

    // Variables persist across calls
    interp.execute("int count = 0;");
    interp.execute("for (int i = 0; i < 10; ++i) count += i;");

    clinglite::Value sum;
    interp.execute("count", sum);
    printf("sum(0..9) = %lld\n", (long long)sum.asInt());

    return 0;
}
