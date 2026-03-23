// session.cpp — Session-based REPL example
//
// Demonstrates the recommended way to build a REPL with clinglite::Session.
// Features: evalSnippet, evalExpr, processLine with .clear support.

#include <clinglite/clinglite.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

int main() {
    clinglite::Environment env("session");

    clinglite::Options opts;
    opts.args = {"session"};
    if (const char* dir = getenv("CLING_DIR"))
        opts.llvmDir = dir;
    clinglite::Interpreter interp(opts);
    if (!interp.isValid()) {
        fprintf(stderr, "Failed to initialize interpreter\n");
        return 1;
    }

    clinglite::Session session(&interp);

    printf("clinglite Session REPL (Cling %s)\n",
           clinglite::Environment::version().c_str());
    printf("Type C++ code. Use .q to quit, .clear to reset.\n\n");

    int indent = 0;
    while (true) {
        std::cout << (indent > 0 ? "       > " : "[cling]$ ") << std::flush;

        std::string line;
        if (!std::getline(std::cin, line))
            break;

        int crashCode = 0;
        clinglite::ExecResult res;
        indent = session.processLine(line, &res, &crashCode);

        if (indent < 0)
            break; // .q

        if (crashCode != 0) {
            printf("  [crash caught: 0x%x]\n", crashCode);
            indent = 0;
        }
    }

    return 0;
}
