// repl.cpp — Interactive C++ REPL using clinglite
//
// Supports multiline input (open braces/parens) and dot commands
// (.L file, .x file, .q quit, .help).

#include <clinglite/clinglite.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

int main() {
    clinglite::Environment env("repl");

    clinglite::Options opts;
    opts.args = {"repl"};
    if (const char* dir = getenv("CLING_DIR"))
        opts.llvmDir = dir;
    clinglite::Interpreter interp(opts);

    printf("clinglite REPL (Cling %s)\n",
           clinglite::Environment::version().c_str());
    printf("Type C++ code. Use .q to quit, .help for commands.\n\n");

    bool continuation = false;

    while (true) {
        std::cout << (continuation ? "       > " : "[cling]$ ") << std::flush;

        std::string line;
        if (!std::getline(std::cin, line))
            break;

        clinglite::ExecResult res;
        int crashCode = 0;
        int indent = interp.processLine(line, &res, &crashCode);

        if (indent < 0)
            break; // .q or EOF

        continuation = (indent > 0);

        if (crashCode != 0)
            printf("  [crash caught: 0x%x]\n", crashCode);
    }

    return 0;
}
