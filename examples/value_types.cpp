// value_types.cpp — Extracting different C++ types from JIT results
//
// clinglite::Value wraps cling::Value behind PIMPL. It supports
// type-safe extraction of integers, floats, pointers, and strings.

#include <clinglite/clinglite.h>
#include <cstdio>
#include <cstdlib>

int main() {
    clinglite::Environment env("value_types");

    clinglite::Options opts;
    opts.args = {"value_types"};
    if (const char* dir = getenv("CLING_DIR"))
        opts.llvmDir = dir;
    clinglite::Interpreter interp(opts);

    // Integer
    {
        clinglite::Value v;
        interp.execute("42", v);
        printf("int:    %lld\n", (long long)v.asInt());
    }

    // Unsigned
    {
        clinglite::Value v;
        interp.execute("0xDEADBEEFu", v);
        printf("uint:   0x%llx\n", (unsigned long long)v.asUInt());
    }

    // Double
    {
        clinglite::Value v;
        interp.execute("3.14159", v);
        printf("double: %f\n", v.asDouble());
    }

    // Pointer
    {
        clinglite::Value v;
        interp.execute("(void*)0x1234", v);
        printf("ptr:    %p\n", v.asPtr());
    }

    // String (via toString — Cling's value printer)
    {
        clinglite::Value v;
        interp.execute("\"hello, clinglite\"", v);
        printf("str:    %s\n", v.toString().c_str());
    }

    // Value copy and move semantics
    {
        clinglite::Value a;
        interp.execute("100", a);

        clinglite::Value b = a;           // copy
        clinglite::Value c = std::move(a); // move

        printf("copy:   %lld\n", (long long)b.asInt());
        printf("moved:  %lld\n", (long long)c.asInt());
    }

    return 0;
}
