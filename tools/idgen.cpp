// clinglite_idgen — enumerate declarations and library exports
// Copyright (c) Elias Bachaalany
// SPDX-License-Identifier: MIT
//
// Usage: clinglite_idgen [options] -o output.txt header1.h header2.h ...
//   -I <path>         Add include path
//   -D <macro>        Add define
//   -std=<std>        C++ standard (default: c++17)
//   -o <path>         Output file (one identifier per line)
//   --source-filter   Only include decls from files under this path
//   --libs lib1 lib2  Shared libraries to extract exports from
//   --help            Show usage
//
// Outputs raw unsorted identifiers. Downstream tools (e.g., idacpp_idgen)
// handle application-specific filtering, deduplication, and sorting.

#include <clinglite/clinglite.h>

#include <cstdio>
#include <string>
#include <vector>

static void usage(const char* argv0) {
    fprintf(stderr,
        "Usage: %s [options] -o output.txt header1.h header2.h ...\n"
        "  -I <path>         Add include path\n"
        "  -D <macro>        Add define\n"
        "  -std=<std>        C++ standard (default: c++17)\n"
        "  -o <path>         Output file (one identifier per line)\n"
        "  --source-filter   Only include decls from files under this path\n"
        "  --libs lib1 ...   Shared libraries to extract exports from\n"
        "  --help            Show usage\n",
        argv0);
}

int main(int argc, char** argv) {
    std::string outputPath;
    std::string sourceFilter;
    std::vector<std::string> includePaths;
    std::vector<std::string> compilerFlags;
    std::vector<std::string> headers;
    std::vector<std::string> libs;
    std::string cxxStd = "-std=c++17";

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (arg == "-o" && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (arg == "--source-filter" && i + 1 < argc) {
            sourceFilter = argv[++i];
        } else if (arg == "--libs") {
            // Consume remaining args until next flag or end
            while (i + 1 < argc && argv[i + 1][0] != '-')
                libs.push_back(argv[++i]);
        } else if (arg.substr(0, 2) == "-I") {
            if (arg.size() > 2)
                includePaths.push_back(arg.substr(2));
            else if (i + 1 < argc)
                includePaths.push_back(argv[++i]);
        } else if (arg.substr(0, 2) == "-D") {
            if (arg.size() > 2)
                compilerFlags.push_back(arg);
            else if (i + 1 < argc)
                compilerFlags.push_back(std::string("-D") + argv[++i]);
        } else if (arg.substr(0, 5) == "-std=") {
            cxxStd = arg;
        } else if (arg[0] != '-') {
            headers.push_back(arg);
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            usage(argv[0]);
            return 1;
        }
    }

    if (outputPath.empty()) {
        fprintf(stderr, "Error: -o output.txt is required\n");
        usage(argv[0]);
        return 1;
    }

    if (headers.empty() && libs.empty()) {
        fprintf(stderr, "Error: provide headers and/or --libs\n");
        usage(argv[0]);
        return 1;
    }

    std::vector<std::string> identifiers;

    // ── AST declarations from headers ───────────────────────────────────
    if (!headers.empty()) {
        clinglite::Environment env(argv[0]);

        clinglite::Options opts;
        opts.args = {"clinglite_idgen", "-noruntime"};
        opts.includePaths = includePaths;
        compilerFlags.push_back(cxxStd);
        opts.compilerFlags = compilerFlags;

        clinglite::Interpreter interp(opts);
        if (!interp.isValid()) {
            fprintf(stderr, "Error: failed to initialize interpreter\n");
            return 1;
        }

        int included = 0;
        for (const auto& hdr : headers) {
            if (interp.includeHeader(hdr))
                ++included;
            else
                fprintf(stderr, "Warning: failed to include <%s>\n",
                        hdr.c_str());
        }
        fprintf(stderr, "Included %d/%zu headers\n", included, headers.size());

        auto decls = interp.enumerateDeclarations(sourceFilter);
        fprintf(stderr, "Declarations: %zu identifiers\n", decls.size());
        identifiers.insert(identifiers.end(), decls.begin(), decls.end());
    }

    // ── Library exports ─────────────────────────────────────────────────
    for (const auto& lib : libs) {
        auto exports = clinglite::Interpreter::enumerateLibraryExports(lib);
        fprintf(stderr, "Exports from %s: %zu symbols\n",
                lib.c_str(), exports.size());
        identifiers.insert(identifiers.end(), exports.begin(), exports.end());
    }

    // ── Write output ────────────────────────────────────────────────────
    FILE* out = fopen(outputPath.c_str(), "w");
    if (!out) {
        fprintf(stderr, "Error: cannot open %s for writing\n",
                outputPath.c_str());
        return 1;
    }

    for (const auto& id : identifiers)
        fprintf(out, "%s\n", id.c_str());
    fclose(out);

    fprintf(stderr, "Written %zu identifiers to %s\n",
            identifiers.size(), outputPath.c_str());
    return 0;
}
