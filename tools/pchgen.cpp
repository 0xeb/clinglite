// clinglite_pchgen — generate pre-compiled header files using clinglite
// Copyright (c) Elias Bachaalany
// SPDX-License-Identifier: MIT
//
// Usage: clinglite_pchgen [options] -o output.pch header1.h header2.h ...
//   -I <path>       Add include path
//   -D <macro>      Add define
//   -std=<std>      C++ standard (default: c++17)
//   -o <path>       Output PCH file
//   --help          Show usage

#include <clinglite/clinglite.h>

#include "cling/Interpreter/Interpreter.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"
#include "clang/Serialization/ASTWriter.h"
#include "clang/Serialization/InMemoryModuleCache.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static void usage(const char* argv0) {
    fprintf(stderr,
        "Usage: %s [options] -o output.pch header1.h header2.h ...\n"
        "  -I <path>       Add include path\n"
        "  -D <macro>      Add define\n"
        "  -std=<std>      C++ standard (default: c++17)\n"
        "  -o <path>       Output PCH file\n"
        "  --help          Show usage\n",
        argv0);
}

int main(int argc, char** argv) {
    std::string outputPath;
    std::vector<std::string> includePaths;
    std::vector<std::string> compilerFlags;
    std::vector<std::string> headers;
    std::string cxxStd = "-std=c++17";

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (arg == "-o" && i + 1 < argc) {
            outputPath = argv[++i];
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
        fprintf(stderr, "Error: -o output.pch is required\n");
        usage(argv[0]);
        return 1;
    }

    if (headers.empty()) {
        fprintf(stderr, "Error: at least one header file is required\n");
        usage(argv[0]);
        return 1;
    }

    // Initialize clinglite
    clinglite::Environment env(argv[0]);

    clinglite::Options opts;
    opts.args = {"clinglite_pchgen", "-noruntime"};
    opts.includePaths = includePaths;
    compilerFlags.push_back(cxxStd);
    opts.compilerFlags = compilerFlags;

    clinglite::Interpreter interp(opts);
    if (!interp.isValid()) {
        fprintf(stderr, "Error: failed to initialize interpreter\n");
        return 1;
    }

    // Pre-include C++ stdlib headers that IDA SDK depends on transitively.
    // macOS libc++ in C++2b mode de-transitivizes headers, so std::is_pod
    // (used by pro.h:2183) won't be found through <memory>/<string> alone.
    static const char* stdPreIncludes[] = {
        "cstdio", "cstdlib", "cstring", "cstdint",
        "string", "vector", "map", "set",
        "memory", "functional", "type_traits",
    };
    for (const char* hdr : stdPreIncludes)
        interp.includeHeader(hdr);

    // Include all requested headers
    int included = 0;
    int skipped = 0;
    for (const auto& hdr : headers) {
        if (interp.includeHeader(hdr)) {
            ++included;
        } else {
            fprintf(stderr, "Warning: failed to include <%s>, skipping\n",
                    hdr.c_str());
            ++skipped;
        }
    }

    fprintf(stderr, "Included %d/%zu headers (%d skipped)\n",
            included, headers.size(), skipped);

    if (included == 0) {
        fprintf(stderr, "Error: no headers could be included\n");
        return 1;
    }

    // Serialize AST to PCH using Clang's ASTWriter
    auto* clingInterp =
        static_cast<cling::Interpreter*>(interp.nativeHandle());
    auto& CI = *clingInterp->getCI();

    llvm::SmallVector<char, 256 * 1024> pchBuffer;
    llvm::BitstreamWriter stream(pchBuffer);
    clang::InMemoryModuleCache moduleCache;

    clang::ASTWriter writer(stream, pchBuffer, moduleCache,
                            /*Extensions=*/{});

    writer.WriteAST(&clingInterp->getSema(), outputPath,
                    /*WritingModule=*/nullptr, /*isysroot=*/"",
                    /*ShouldCacheASTInMemory=*/false);

    if (pchBuffer.empty()) {
        fprintf(stderr, "Error: ASTWriter produced empty output\n");
        return 1;
    }

    // Write PCH to disk
    {
        std::error_code EC;
        llvm::raw_fd_ostream os(outputPath, EC);
        if (EC) {
            fprintf(stderr, "Error: cannot write %s: %s\n",
                    outputPath.c_str(), EC.message().c_str());
            return 1;
        }
        os.write(pchBuffer.data(), pchBuffer.size());
    }

    fprintf(stderr, "PCH written: %s (%zu bytes)\n",
            outputPath.c_str(), pchBuffer.size());

    return 0;
}
