// clinglite — implementation
// Copyright (c) Elias Bachaalany
// SPDX-License-Identifier: MIT
// All LLVM/Cling headers are confined to this file.

#include <clinglite/clinglite.h>

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/Value.h"
#include "cling/MetaProcessor/MetaProcessor.h"
#include "cling/Utils/Output.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Sema/Sema.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Serialization/ASTWriter.h"
#include "clang/Serialization/InMemoryModuleCache.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"

#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

namespace clinglite {

// ── Helpers ─────────────────────────────────────────────────────────────────

static ExecResult mapResult(cling::Interpreter::CompilationResult cr) {
    switch (cr) {
    case cling::Interpreter::kSuccess:         return ExecResult::Success;
    case cling::Interpreter::kMoreInputExpected: return ExecResult::MoreInputExpected;
    default:                                   return ExecResult::Failure;
    }
}

// ── CallbackOStream ─────────────────────────────────────────────────────────
// Custom raw_ostream that forwards output to an OutputCallback.

class CallbackOStream : public llvm::raw_ostream {
public:
    explicit CallbackOStream(OutputCallback cb)
        : llvm::raw_ostream(/*unbuffered=*/true), m_cb(std::move(cb)) {}

    ~CallbackOStream() override { flush(); }

    void setCb(OutputCallback cb) { m_cb = std::move(cb); }
    bool hasCb() const { return !!m_cb; }

private:
    void write_impl(const char* Ptr, size_t Size) override {
        if (m_cb && Size > 0)
            m_cb(std::string(Ptr, Size));
    }

    uint64_t current_pos() const override { return 0; }

    OutputCallback m_cb;
};

// ── Environment::Impl ───────────────────────────────────────────────────────

struct Environment::Impl {
    llvm::llvm_shutdown_obj shutdownTrigger;
    bool crashRecovery = false;

    static bool s_initialized;
};

bool Environment::Impl::s_initialized = false;

Environment::Environment(const char* argv0) : m_impl(std::make_unique<Impl>()) {
    if (Impl::s_initialized)
        throw std::logic_error("Only one clinglite::Environment instance allowed");
    Impl::s_initialized = true;

    if (argv0)
        llvm::sys::PrintStackTraceOnErrorSignal(argv0);

    llvm::CrashRecoveryContext::Enable();
    m_impl->crashRecovery = true;

#ifdef _WIN32
    llvm::sys::DisableSystemDialogsOnCrash();
#endif
}

Environment::~Environment() {
    Impl::s_initialized = false;
}

bool Environment::crashRecoveryEnabled() const {
    return m_impl->crashRecovery;
}

std::string Environment::version() {
    return cling::Interpreter::getVersion();
}

// ── Value::Impl ─────────────────────────────────────────────────────────────

struct Value::Impl {
    cling::Value val;

    Impl() = default;
    Impl(const cling::Value& v) : val(v) {}
    Impl(cling::Value&& v) : val(std::move(v)) {}
};

Value::Value() : m_impl(std::make_unique<Impl>()) {}

Value::~Value() = default;

Value::Value(Value&& other) noexcept = default;
Value& Value::operator=(Value&& other) noexcept = default;

Value::Value(const Value& other)
    : m_impl(std::make_unique<Impl>(other.m_impl->val)) {}

Value& Value::operator=(const Value& other) {
    if (this != &other)
        m_impl = std::make_unique<Impl>(other.m_impl->val);
    return *this;
}

bool Value::isValid() const { return m_impl->val.isValid(); }
bool Value::hasValue() const { return m_impl->val.hasValue(); }

int64_t Value::asInt() const { return m_impl->val.castAs<int64_t>(); }
uint64_t Value::asUInt() const { return m_impl->val.castAs<uint64_t>(); }
double Value::asDouble() const { return m_impl->val.castAs<double>(); }
void* Value::asPtr() const { return m_impl->val.castAs<void*>(); }

std::string Value::toString() const {
    std::string buf;
    llvm::raw_string_ostream os(buf);
    m_impl->val.print(os);
    return buf;
}

void* Value::nativeHandle() { return &m_impl->val; }
const void* Value::nativeHandle() const { return &m_impl->val; }

// ── Interpreter::Impl ───────────────────────────────────────────────────────

struct Interpreter::Impl {
    // Storage for argv strings — must outlive cling::Interpreter
    std::vector<std::string> argStorage;
    std::vector<const char*> argv;

    std::unique_ptr<cling::Interpreter> interp;
    std::unique_ptr<CallbackOStream> outStream;
    std::unique_ptr<CallbackOStream> errStream;
    std::unique_ptr<cling::MetaProcessor> meta;

    std::string resolvedPchPath; // path passed to -include-pch (cached)

    OutputCallback outputCb;
    OutputCallback errorCb;

    bool valid = false;

    void buildArgv(const Options& opts) {
        // argv[0]
        if (!opts.args.empty())
            argStorage.push_back(opts.args[0]);
        else
            argStorage.push_back("clinglite");

        // Include paths as -I flags
        for (const auto& p : opts.includePaths)
            argStorage.push_back("-I" + p);

        // Compiler flags
        for (const auto& f : opts.compilerFlags)
            argStorage.push_back(f);

        // Remaining args (skip argv[0])
        for (size_t i = 1; i < opts.args.size(); ++i)
            argStorage.push_back(opts.args[i]);

        // NOTE: argv pointer array is built in init() after PCH handling,
        // since PCH may add more entries to argStorage.
    }

    void buildArgvPointers() {
        argv.clear();
        argv.reserve(argStorage.size());
        for (const auto& s : argStorage)
            argv.push_back(s.c_str());
    }

    std::string llvmDirStorage; // keeps llvmdir string alive for cling

    void init(const Options& opts) {
        buildArgv(opts);
        llvmDirStorage = opts.llvmDir;

        // Compile-time fallback: use the build-tree path baked in at configure
#ifdef CLINGLITE_DEFAULT_CLING_DIR
        if (llvmDirStorage.empty())
            llvmDirStorage = CLINGLITE_DEFAULT_CLING_DIR;
#endif

        // ── Runtime headers: source-first mode ─────────────────────────
        if (!llvmDirStorage.empty()) {
            argStorage.push_back("-I" + llvmDirStorage + "/include");
        }

        // Cling source headers (RuntimeUniverse.h etc.) may live outside
        // the build tree when building from source.
#ifdef CLINGLITE_DEFAULT_CLING_SRC_INCLUDE
        argStorage.push_back("-I" CLINGLITE_DEFAULT_CLING_SRC_INCLUDE);
#endif

        // Point Clang at the resource dir (built-in headers like stddef.h)
        // so it doesn't need to resolve it relative to the binary path.
#ifdef CLINGLITE_DEFAULT_RESOURCE_DIR
        argStorage.push_back("-resource-dir");
        argStorage.push_back(CLINGLITE_DEFAULT_RESOURCE_DIR);
#endif

        // ── PCH handling (file-based) ────────────────────────────────────
        if (!opts.pchPath.empty()) {
            resolvedPchPath = opts.pchPath;
            argStorage.push_back("-include-pch");
            argStorage.push_back(resolvedPchPath);
        }

        // Build pointer array (after all argStorage entries are finalized)
        buildArgvPointers();

        int argc = static_cast<int>(argv.size());
        const char* llvmdir = llvmDirStorage.empty() ? nullptr : llvmDirStorage.c_str();
        interp = std::make_unique<cling::Interpreter>(argc, argv.data(), llvmdir);
        valid = interp->isValid();

        if (valid) {
            // Create MetaProcessor with appropriate output stream
            outStream = std::make_unique<CallbackOStream>(outputCb);
            meta = std::make_unique<cling::MetaProcessor>(
                *interp, outStream->hasCb()
                             ? static_cast<llvm::raw_ostream&>(*outStream)
                             : llvm::outs());
        }
    }

    void rebuildMeta() {
        if (!valid) return;
        outStream = std::make_unique<CallbackOStream>(outputCb);
        meta = std::make_unique<cling::MetaProcessor>(
            *interp, outStream->hasCb()
                         ? static_cast<llvm::raw_ostream&>(*outStream)
                         : llvm::outs());
    }
};

Interpreter::Interpreter(const Options& opts)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->init(opts);
}

Interpreter::~Interpreter() = default;

bool Interpreter::isValid() const { return m_impl->valid; }

// ── Code execution ──────────────────────────────────────────────────────────

ExecResult Interpreter::execute(const std::string& code) {
    if (!m_impl->valid) return ExecResult::Failure;
    auto cr = m_impl->interp->process(code);
    return mapResult(cr);
}

ExecResult Interpreter::execute(const std::string& code, Value& result) {
    if (!m_impl->valid) return ExecResult::Failure;
    cling::Value cv;
    auto cr = m_impl->interp->process(code, &cv);
    if (cr == cling::Interpreter::kSuccess)
        result.m_impl->val = std::move(cv);
    return mapResult(cr);
}

ExecResult Interpreter::executeSafe(const std::string& code,
                                    int* crashCode) {
    if (!m_impl->valid) return ExecResult::Failure;

    ExecResult res = ExecResult::Failure;
    llvm::CrashRecoveryContext CRC;
    bool survived = CRC.RunSafely([&]() {
        res = execute(code);
    });

    if (!survived) {
        if (crashCode) *crashCode = CRC.RetCode;
        return ExecResult::Failure;
    }
    if (crashCode) *crashCode = 0;
    return res;
}

ExecResult Interpreter::executeSafe(const std::string& code, Value& result,
                                    int* crashCode) {
    if (!m_impl->valid) return ExecResult::Failure;

    ExecResult res = ExecResult::Failure;
    llvm::CrashRecoveryContext CRC;
    bool survived = CRC.RunSafely([&]() {
        res = execute(code, result);
    });

    if (!survived) {
        if (crashCode) *crashCode = CRC.RetCode;
        return ExecResult::Failure;
    }
    if (crashCode) *crashCode = 0;
    return res;
}

// ── Headers & includes ──────────────────────────────────────────────────────

void Interpreter::addIncludePath(const std::string& path) {
    if (m_impl->valid)
        m_impl->interp->AddIncludePath(path);
}

bool Interpreter::includeHeader(const std::string& header) {
    if (!m_impl->valid) return false;
    std::string code = "#include <" + header + ">";
    return m_impl->interp->process(code) == cling::Interpreter::kSuccess;
}

// ── File loading ────────────────────────────────────────────────────────────

ExecResult Interpreter::loadFile(const std::string& path,
                                 std::string* errbuf) {
    if (!m_impl->valid) {
        if (errbuf) *errbuf = "interpreter not valid";
        return ExecResult::Failure;
    }

    auto bufOrErr = llvm::MemoryBuffer::getFile(path);
    if (!bufOrErr) {
        if (errbuf) *errbuf = bufOrErr.getError().message();
        return ExecResult::Failure;
    }

    std::string content = (*bufOrErr)->getBuffer().str();
    auto cr = m_impl->interp->process(content);
    return mapResult(cr);
}

ExecResult Interpreter::declareFile(const std::string& path,
                                    std::string* errbuf) {
    if (!m_impl->valid) {
        if (errbuf) *errbuf = "interpreter not valid";
        return ExecResult::Failure;
    }

    // Route through processLine(".L path") — same as Cling's CLI .L command.
    // This auto-unloads any previous version of the same file, loads via
    // #include + DeclareInternal, and tracks file→transaction mapping so
    // repeated loads of the same script Just Work.
    ExecResult res;
    processLine(".L " + path, &res);
    if (res != ExecResult::Success && errbuf)
        *errbuf = "failed to load file: " + path;
    return res;
}

ExecResult Interpreter::loadFile(const std::string& path,
                                 const std::string& ns,
                                 std::string* errbuf) {
    if (!m_impl->valid) {
        if (errbuf) *errbuf = "interpreter not valid";
        return ExecResult::Failure;
    }

    auto bufOrErr = llvm::MemoryBuffer::getFile(path);
    if (!bufOrErr) {
        if (errbuf) *errbuf = bufOrErr.getError().message();
        return ExecResult::Failure;
    }

    std::string wrapped = "namespace " + ns + " {\n"
                        + (*bufOrErr)->getBuffer().str()
                        + "\n}";
    auto cr = m_impl->interp->process(wrapped);
    return mapResult(cr);
}

// ── Virtual filesystem ──────────────────────────────────────────────────────

void Interpreter::addVirtualFile(const std::string& path,
                                 const std::string& content) {
    if (!m_impl->valid) return;

    auto& CI = *m_impl->interp->getCI();
    auto& FM = CI.getFileManager();
    auto& SM = CI.getSourceManager();

    auto FERef = FM.getVirtualFileRef(path, content.size(), /*ModTime=*/0);
    auto Buf = llvm::MemoryBuffer::getMemBufferCopy(content, path);
    SM.overrideFileContents(FERef, std::move(Buf));
}

// ── Libraries ───────────────────────────────────────────────────────────────

ExecResult Interpreter::loadLibrary(const std::string& path) {
    if (!m_impl->valid) return ExecResult::Failure;
    auto cr = m_impl->interp->loadLibrary(path);
    return mapResult(cr);
}

// ── REPL / MetaProcessor ────────────────────────────────────────────────────

int Interpreter::processLine(const std::string& line,
                             ExecResult* result,
                             int* crashCode) {
    if (!m_impl->valid) {
        if (result) *result = ExecResult::Failure;
        return 0;
    }

    int indent = 0;
    ExecResult res = ExecResult::Success;

    llvm::CrashRecoveryContext CRC;
    bool survived = CRC.RunSafely([&]() {
        cling::Interpreter::CompilationResult cr;
        indent = m_impl->meta->process(line, cr);
        res = mapResult(cr);
    });

    if (!survived) {
        if (crashCode) *crashCode = CRC.RetCode;
        if (result) *result = ExecResult::Failure;
        indent = 0;
        return indent;
    }

    if (crashCode) *crashCode = 0;
    if (result) *result = res;
    return indent;
}

void Interpreter::cancelContinuation() {
    if (!m_impl->valid) return;
    // Reset by processing an empty line or reinitializing MetaProcessor
    m_impl->rebuildMeta();
}

// ── Symbol lookup ────────────────────────────────────────────────────────────

void* Interpreter::getSymbolAddress(const std::string& mangledName,
                                    bool* fromJIT) {
    if (!m_impl->valid) return nullptr;
    return m_impl->interp->getAddressOfGlobal(mangledName, fromJIT);
}

// ── Code completion ─────────────────────────────────────────────────────────

std::vector<std::string> Interpreter::codeComplete(const std::string& line,
                                                   size_t& cursor) {
    std::vector<std::string> completions;
    if (!m_impl->valid) return completions;
    m_impl->interp->codeComplete(line, cursor, completions);
    return completions;
}

CompletionResult Interpreter::codeCompleteWithContext(const std::string& line,
                                                     size_t cursor) {
    CompletionResult result;
    if (!m_impl->valid) return result;

    // Clamp cursor to line length
    if (cursor > line.size())
        cursor = line.size();

    // Get raw completions from Cling
    std::vector<std::string> raw;
    size_t pos = cursor;
    m_impl->interp->codeComplete(line, pos, raw);

    // Compute prefix by walking backwards from cursor through identifier chars
    // (mirrors Clang's getCodeCompletionFilter() behavior)
    size_t start = cursor;
    while (start > 0) {
        char c = line[start - 1];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            --start;
        else
            break;
    }

    result.prefix = line.substr(start, cursor - start);
    result.matchStart = start;
    result.matchEnd = cursor;

    // Filter completions to those starting with prefix
    // (as Cling's UserInterface.cpp does with CC.Prefix)
    for (const auto& c : raw) {
        if (result.prefix.empty() ||
            (c.size() >= result.prefix.size() &&
             c.compare(0, result.prefix.size(), result.prefix) == 0))
            result.completions.push_back(c);
    }

    return result;
}

// ── Undo / rollback ─────────────────────────────────────────────────────────

unsigned Interpreter::undo(unsigned n) {
    if (!m_impl->valid || n == 0) return 0;
    unsigned undone = 0;
    for (unsigned i = 0; i < n; ++i) {
        if (undoableCount() == 0) break;
        // Wrap unload in CrashRecoveryContext: without the DeclUnloader
        // patch (patches/0003-*), unloading transactions that contain
        // implicit template instantiations with UsingShadowDecls can crash.
        llvm::CrashRecoveryContext CRC;
        bool survived = CRC.RunSafely([&]() {
            m_impl->interp->unload(1);
        });
        if (!survived) break;
        ++undone;
    }
    return undone;
}

unsigned Interpreter::undoableCount() const {
    if (!m_impl->valid) return 0;
    unsigned count = 0;
    const auto* t = m_impl->interp->getFirstTransaction();
    if (!t) return 0;
    // Skip the first (runtime init) transaction, count active ones
    t = t->getNext();
    while (t) {
        auto state = t->getState();
        if (state == cling::Transaction::kCompleted ||
            state == cling::Transaction::kCommitted)
            ++count;
        t = t->getNext();
    }
    return count;
}

// ── Output capture ──────────────────────────────────────────────────────────

void Interpreter::setOutputCallback(OutputCallback cb) {
    m_impl->outputCb = std::move(cb);
    if (!m_impl->valid) return;
    m_impl->rebuildMeta();
    // Also redirect cling::outs() so Value::dump() uses the callback.
    // Without this, value printing (e.g. "1+1" → "(int) 2") bypasses
    // the MetaProcessor's stream and goes to std::cout.
    cling::setOuts(m_impl->outStream->hasCb()
                       ? m_impl->outStream.get() : nullptr);
}

OutputCallback Interpreter::getErrorCallback() const {
    return m_impl->errorCb;
}

void Interpreter::setErrorCallback(OutputCallback cb) {
    bool hasCb = !!cb;
    m_impl->errorCb = std::move(cb);
    if (!m_impl->valid) return;

    if (hasCb) {
        // Install a callback-based stream as the diagnostic output.
        // Use Cling's replaceDiagnosticConsumer (sets the target on the
        // FilteringDiagConsumer, preserving Cling's diagnostic filtering).
        auto* cbStream = new CallbackOStream(m_impl->errorCb);
        m_impl->errStream.reset(cbStream);

        auto& CI = *m_impl->interp->getCI();
        auto& DE = CI.getDiagnostics();
        auto* printer = new clang::TextDiagnosticPrinter(
            *cbStream, &DE.getDiagnosticOptions(), /*OwnsStream=*/false);
        printer->BeginSourceFile(CI.getLangOpts(), &CI.getPreprocessor());
        m_impl->interp->replaceDiagnosticConsumer(printer, /*Own=*/true);
    } else {
        // Restore default: remove replacement, diagnostics go to original consumer
        m_impl->interp->replaceDiagnosticConsumer(nullptr);
        m_impl->errStream.reset();
    }
}

// ── PCH generation ──────────────────────────────────────────────────────────

bool Interpreter::generatePCH(const std::string& outputPath) {
    if (!m_impl->valid) return false;

    auto* clingInterp = m_impl->interp.get();
    auto& CI = *clingInterp->getCI();

    llvm::SmallVector<char, 256 * 1024> pchBuffer;
    llvm::BitstreamWriter stream(pchBuffer);
    clang::InMemoryModuleCache moduleCache;

    clang::ASTWriter writer(stream, pchBuffer, moduleCache, /*Extensions=*/{});
    writer.WriteAST(&clingInterp->getSema(), outputPath,
                    /*WritingModule=*/nullptr, /*isysroot=*/"",
                    /*ShouldCacheASTInMemory=*/false);

    if (pchBuffer.empty())
        return false;

    std::error_code EC;
    llvm::raw_fd_ostream os(outputPath, EC);
    if (EC) return false;
    os.write(pchBuffer.data(), pchBuffer.size());
    return true;
}

// ── Declaration enumeration ──────────────────────────────────────────────────

std::vector<std::string> Interpreter::enumerateDeclarations(
    const std::string& sourceFilter) {
    std::vector<std::string> result;
    if (!m_impl->valid) return result;

    auto& CI = *m_impl->interp->getCI();
    auto& Ctx = CI.getASTContext();
    auto& SM = CI.getSourceManager();

    // Recursive walker: enters LinkageSpecDecl (extern "C"), NamespaceDecl,
    // and collects named declarations that pass the source filter.
    std::function<void(clang::DeclContext*)> walk =
        [&](clang::DeclContext* DC) {
        for (auto* D : DC->decls()) {
            // Recurse into extern "C" { ... } blocks
            if (auto* LSD = llvm::dyn_cast<clang::LinkageSpecDecl>(D)) {
                walk(LSD);
                continue;
            }

            auto* ND = llvm::dyn_cast<clang::NamedDecl>(D);
            if (!ND) continue;

            auto name = ND->getNameAsString();
            if (name.empty()) continue;
            if (ND->isImplicit()) continue;

            // Source location filter
            if (!sourceFilter.empty()) {
                auto loc = SM.getSpellingLoc(ND->getLocation());
                if (loc.isInvalid()) continue;
                auto filename = SM.getFilename(loc);
                if (filename.empty()) continue;
                if (!filename.starts_with(sourceFilter)) continue;
            }

            // Collect useful declaration kinds (skip member functions)
            if (llvm::isa<clang::TypedefDecl>(ND) ||
                llvm::isa<clang::TypeAliasDecl>(ND) ||
                llvm::isa<clang::RecordDecl>(ND) ||
                llvm::isa<clang::EnumDecl>(ND) ||
                llvm::isa<clang::EnumConstantDecl>(ND) ||
                (llvm::isa<clang::FunctionDecl>(ND) &&
                 !llvm::isa<clang::CXXMethodDecl>(ND)) ||
                llvm::isa<clang::VarDecl>(ND)) {
                result.push_back(name);
            }

            // For enums, also collect enumerators
            if (auto* ED = llvm::dyn_cast<clang::EnumDecl>(ND)) {
                for (auto* EC : ED->enumerators()) {
                    auto ename = EC->getNameAsString();
                    if (!ename.empty())
                        result.push_back(std::move(ename));
                }
            }

            // Recurse into namespaces
            if (auto* NSD = llvm::dyn_cast<clang::NamespaceDecl>(ND))
                walk(NSD);
        }
    };

    walk(Ctx.getTranslationUnitDecl());
    return result;
}

std::vector<std::string> Interpreter::enumerateLibraryExports(
    const std::string& libraryPath) {
    std::vector<std::string> result;

    auto bufOrErr = llvm::MemoryBuffer::getFile(libraryPath);
    if (!bufOrErr) return result;

    auto binOrErr = llvm::object::ObjectFile::createObjectFile(
        bufOrErr.get()->getMemBufferRef());
    if (!binOrErr) {
        llvm::consumeError(binOrErr.takeError());
        return result;
    }

    auto& obj = *binOrErr.get();

    // Helper to collect global defined symbols from an iterator range
    auto collectSymbols = [&](auto symRange) {
        for (const auto& sym : symRange) {
            auto flagsOrErr = sym.getFlags();
            if (!flagsOrErr) {
                llvm::consumeError(flagsOrErr.takeError());
                continue;
            }
            if (!(*flagsOrErr & llvm::object::SymbolRef::SF_Global))
                continue;
            if (*flagsOrErr & llvm::object::SymbolRef::SF_Undefined)
                continue;

            auto nameOrErr = sym.getName();
            if (!nameOrErr) {
                llvm::consumeError(nameOrErr.takeError());
                continue;
            }

            auto name = nameOrErr->str();
            if (!name.empty())
                result.push_back(std::move(name));
        }
    };

    // For ELF shared objects, use dynamic symbol table (.dynsym)
    if (auto* elfObj = llvm::dyn_cast<llvm::object::ELFObjectFileBase>(&obj))
        collectSymbols(elfObj->getDynamicSymbolIterators());
    else
        collectSymbols(obj.symbols());

    return result;
}

// ── Escape hatch ────────────────────────────────────────────────────────────

void* Interpreter::nativeHandle() {
    return m_impl->valid ? m_impl->interp.get() : nullptr;
}

// ── Session::Impl ────────────────────────────────────────────────────────────

struct Session::Impl {
    Interpreter* interp;

    explicit Impl(Interpreter* i) : interp(i) {}

    /// RAII error capture: installs callback, captures text, restores previous on dtor.
    struct ErrorCapture {
        Interpreter* interp;
        OutputCallback previousCb;
        std::string captured;

        explicit ErrorCapture(Interpreter* i) : interp(i) {
            if (interp) {
                previousCb = interp->getErrorCallback();
                interp->setErrorCallback([this](const std::string& s) {
                    captured += s;
                });
            }
        }
        ~ErrorCapture() {
            if (interp)
                interp->setErrorCallback(std::move(previousCb));
        }

        /// Trim trailing whitespace and return captured text.
        std::string text() const {
            std::string msg = captured;
            while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r'
                                    || msg.back() == ' '))
                msg.pop_back();
            return msg;
        }
    };
};

Session::Session(Interpreter* interp)
    : m_impl(std::make_unique<Impl>(interp)) {}

Session::~Session() = default;

bool Session::evalSnippet(const std::string& code, std::string* error) {
    if (!m_impl->interp || !m_impl->interp->isValid()) {
        if (error) *error = "interpreter not valid";
        return false;
    }

    Impl::ErrorCapture ec(m_impl->interp);
    int crashCode = 0;
    auto r = m_impl->interp->executeSafe(code, &crashCode);

    if (crashCode != 0) {
        if (error) {
            char buf[64];
            snprintf(buf, sizeof(buf), "crash (code 0x%08x)", crashCode);
            *error = buf;
        }
        return false;
    }

    if (r != ExecResult::Success) {
        if (error) {
            std::string msg = ec.text();
            *error = msg.empty() ? "compilation error" : msg;
        }
        return false;
    }
    return true;
}

bool Session::evalExpr(const std::string& expr, Value& result,
                       std::string* error) {
    if (!m_impl->interp || !m_impl->interp->isValid()) {
        if (error) *error = "interpreter not valid";
        return false;
    }

    Impl::ErrorCapture ec(m_impl->interp);
    int crashCode = 0;
    auto r = m_impl->interp->executeSafe(expr, result, &crashCode);

    if (crashCode != 0) {
        if (error) {
            char buf[64];
            snprintf(buf, sizeof(buf), "crash (code 0x%08x)", crashCode);
            *error = buf;
        }
        return false;
    }

    if (r != ExecResult::Success) {
        if (error) {
            std::string msg = ec.text();
            *error = msg.empty() ? "evaluation error" : msg;
        }
        return false;
    }
    return true;
}

int Session::processLine(const std::string& line,
                         ExecResult* result, int* crashCode) {
    if (!m_impl->interp || !m_impl->interp->isValid()) {
        if (result) *result = ExecResult::Failure;
        return 0;
    }

    // Intercept .clear command
    if (line == ".clear") {
        clear();
        if (result) *result = ExecResult::Success;
        if (crashCode) *crashCode = 0;
        return 0;
    }

    return m_impl->interp->processLine(line, result, crashCode);
}

void Session::clear() {
    if (!m_impl->interp || !m_impl->interp->isValid())
        return;
    unsigned n = m_impl->interp->undoableCount();
    if (n > 0)
        m_impl->interp->undo(n);
}

Interpreter* Session::interp() const {
    return m_impl->interp;
}

// ── ScriptRunner ────────────────────────────────────────────────────────────

struct ScriptRunner::Impl {
    Session* session;
    std::string namespacePrefix = "clinglite_exec_";
    std::string runtimeNamespace = "clinglite_runtime";
    ScriptRunnerStatus status;
    unsigned execFileCounter = 0;
    unsigned execResultCounter = 0;

    explicit Impl(Session* s) : session(s) {}

    static bool readTextFile(const std::string& path,
                             std::string* out,
                             std::string* error)
    {
        std::ifstream input(path);
        if (!input) {
            if (error)
                *error = "Failed to open file: " + path;
            return false;
        }
        std::ostringstream buf;
        buf << input.rdbuf();
        if (!input.good() && !input.eof()) {
            if (error)
                *error = "Failed to read file: " + path;
            return false;
        }
        *out = buf.str();
        return true;
    }

    std::string buildExecNamespace() {
        return namespacePrefix + std::to_string(++execFileCounter);
    }

    static std::string buildProbeSnippet(
        const std::string& scope,
        const EntrypointCandidate& candidate)
    {
        return "{ auto __clinglite_probe = static_cast<"
             + candidate.signature + ">("
             + scope + candidate.symbol
             + "); (void)__clinglite_probe; }";
    }

    static std::string buildInvokeExpr(
        const std::string& scope,
        const std::string& rtScope,
        const EntrypointCandidate& candidate)
    {
        std::string expr = scope + candidate.symbol;
        switch (candidate.argStyle) {
        case EntrypointArgStyle::MutableArgv:
            expr += "(" + rtScope + "argc(), " + rtScope + "argv_mutable())";
            break;
        case EntrypointArgStyle::ConstArgv:
            expr += "(" + rtScope + "argc(), " + rtScope + "argv())";
            break;
        case EntrypointArgStyle::NoArgs:
            expr += "()";
            break;
        }
        if (!candidate.returnsValue)
            expr += ";";
        return expr;
    }

    bool probeEntrypoint(const std::string& scope,
                         const EntrypointCandidate& candidate)
    {
        std::string error;
        return session->evalSnippet(buildProbeSnippet(scope, candidate), &error);
    }

    bool invokeEntrypoint(const std::string& scope,
                          const std::string& rtScope,
                          const EntrypointCandidate& candidate,
                          Value* result,
                          std::string* error)
    {
        const std::string invokeExpr = buildInvokeExpr(scope, rtScope, candidate);
        if (candidate.returnsValue) {
            const std::string tempName =
                "__clinglite_entry_result_" + std::to_string(++execResultCounter);
            const std::string assignExpr =
                "auto " + tempName + " = (" + invokeExpr + ");";
            if (!session->evalSnippet(assignExpr, error))
                return false;
            if (result == nullptr)
                return true;
            return session->evalExpr(tempName, *result, error);
        }
        return session->evalSnippet(invokeExpr, error);
    }

    bool compileExecFile(const std::string& file,
                         const FileExecOptions& options,
                         std::string* outNamespace,
                         std::string* error)
    {
        std::string contents;
        if (!readTextFile(file, &contents, error)) {
            status.lastError = error ? *error : "file read error";
            return false;
        }

        const std::string scriptNs = buildExecNamespace();
        std::string wrapped;
        wrapped.reserve(contents.size() + 512 + options.args.size() * 64);
        wrapped += "namespace ";
        wrapped += scriptNs;
        wrapped += " {\nnamespace ";
        wrapped += runtimeNamespace;
        wrapped += " {\ninline int ARGC = ";
        wrapped += std::to_string(options.args.size());
        wrapped += ";\n";
        if (options.args.empty()) {
            wrapped += "inline const char **ARGV = nullptr;\n";
            wrapped += "inline char **ARGV_MUTABLE = nullptr;\n";
        } else {
            wrapped += "inline const char *ARGV_DATA[] = {";
            for (size_t i = 0; i < options.args.size(); ++i) {
                if (i != 0) wrapped += ", ";
                wrapped += cppStringLiteral(options.args[i]);
            }
            wrapped += "};\n";
            wrapped += "inline char *ARGV_MUTABLE_DATA[] = {";
            for (size_t i = 0; i < options.args.size(); ++i) {
                if (i != 0) wrapped += ", ";
                wrapped += "const_cast<char*>(";
                wrapped += cppStringLiteral(options.args[i]);
                wrapped += ")";
            }
            wrapped += "};\n";
            wrapped += "inline const char **ARGV = ARGV_DATA;\n";
            wrapped += "inline char **ARGV_MUTABLE = ARGV_MUTABLE_DATA;\n";
        }
        wrapped += "inline int argc() { return ARGC; }\n";
        wrapped += "inline const char** argv() { return ARGV; }\n";
        wrapped += "inline char** argv_mutable() { return ARGV_MUTABLE; }\n";
        wrapped += "}\n#line 1 ";
        wrapped += cppStringLiteral(file);
        wrapped += "\n";
        wrapped += contents;
        if (wrapped.empty() || wrapped.back() != '\n')
            wrapped.push_back('\n');
        wrapped += "}\n";

        std::string loadError;
        if (!session->evalSnippet(wrapped, &loadError)) {
            if (error)
                *error = loadError.empty() ? "C++ file execution error" : loadError;
            status.lastError = error ? *error : "C++ file execution error";
            return false;
        }

        if (outNamespace)
            *outNamespace = scriptNs;
        status.lastFileNamespace = scriptNs;
        return true;
    }

    bool resolveAndRunEntrypoint(const std::string& scope,
                                 const std::string& rtScope,
                                 const FileExecOptions& options,
                                 Value* result,
                                 std::string* error)
    {
        if (options.entrypointGroups.empty()) {
            status.lastEntrypointChosen = "none";
            return true;
        }

        for (const auto& group : options.entrypointGroups) {
            for (const auto& candidate : group) {
                if (!probeEntrypoint(scope, candidate))
                    continue;
                if (!invokeEntrypoint(scope, rtScope, candidate, result, error))
                    return false;
                status.lastEntrypointChosen = candidate.label;
                return true;
            }
        }

        if (options.allowNoEntrypoint) {
            status.lastEntrypointChosen = "none";
            return true;
        }

        std::string msg = "No matching entrypoint found";
        if (error)
            *error = msg;
        status.lastError = msg;
        return false;
    }
};

ScriptRunner::ScriptRunner(Session* session)
    : m_impl(std::make_unique<Impl>(session)) {}

ScriptRunner::~ScriptRunner() = default;

void ScriptRunner::setNamespacePrefix(const std::string& prefix) {
    m_impl->namespacePrefix = prefix;
}

void ScriptRunner::setRuntimeNamespace(const std::string& ns) {
    m_impl->runtimeNamespace = ns;
}

bool ScriptRunner::compileFile(const std::string& file,
                               const char* requestedNamespace,
                               std::string* error)
{
    if (!m_impl->session || !m_impl->session->interp()) {
        if (error) *error = "interpreter not initialized";
        return false;
    }

    const bool haveNs =
        requestedNamespace != nullptr && requestedNamespace[0] != '\0';

    std::string loadError;
    const ExecResult result = haveNs
        ? m_impl->session->interp()->loadFile(file, requestedNamespace, &loadError)
        : m_impl->session->interp()->declareFile(file, &loadError);

    m_impl->status.lastFileExecuted = file;
    m_impl->status.lastFileNamespace = haveNs ? requestedNamespace : "";
    m_impl->status.lastEntrypointChosen.clear();

    if (result != ExecResult::Success) {
        std::string msg = loadError;
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == ' '))
            msg.pop_back();
        if (msg.empty())
            msg = "C++ file compilation error";
        if (error) *error = msg;
        m_impl->status.lastError = msg;
        return false;
    }

    m_impl->status.lastError.clear();
    return true;
}

bool ScriptRunner::execFile(const std::string& file,
                            const FileExecOptions& options,
                            Value* result,
                            std::string* error)
{
    if (!m_impl->session || !m_impl->session->interp()) {
        if (error) *error = "interpreter not initialized";
        return false;
    }

    m_impl->status.lastFileExecuted = file;
    m_impl->status.lastFileNamespace.clear();

    std::string scriptNs;
    if (!m_impl->compileExecFile(file, options, &scriptNs, error))
        return false;

    const std::string scope = scriptNs + "::";
    const std::string rtScope = scriptNs + "::" + m_impl->runtimeNamespace + "::";

    if (!m_impl->resolveAndRunEntrypoint(scope, rtScope, options, result, error))
        return false;

    m_impl->status.lastError.clear();
    return true;
}

ScriptRunnerStatus ScriptRunner::status() const {
    return m_impl->status;
}

void ScriptRunner::resetStatus() {
    m_impl->status = ScriptRunnerStatus{};
}

Session* ScriptRunner::session() const {
    return m_impl->session;
}

// ── Free functions ──────────────────────────────────────────────────────────

std::vector<EntrypointCandidate> standardMainCandidates() {
    return {
        {"main(int, char**)", "main", "int(*)(int, char**)",
         EntrypointArgStyle::MutableArgv, true},
        {"main(int, const char**)", "main", "int(*)(int, const char**)",
         EntrypointArgStyle::ConstArgv, true},
        {"main()", "main", "int(*)()",
         EntrypointArgStyle::NoArgs, true},
        {"main(void)", "main", "void(*)()",
         EntrypointArgStyle::NoArgs, false},
    };
}

std::string cppStringLiteral(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\0': out += "\\0"; break;
        default:   out.push_back(c); break;
        }
    }
    out.push_back('"');
    return out;
}

std::string escapePath(const std::string& path) {
    std::string result;
    result.reserve(path.size());
    for (char c : path) {
        if (c == '\\')
            result += "\\\\";
        else
            result += c;
    }
    return result;
}

} // namespace clinglite
