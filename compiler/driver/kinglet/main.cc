// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/kinglet/cli_driver.h"
#include "driver/pipeline/pipeline.h"
#include "frontend/ast/ast.h"
#include "frontend/checker/type_checker.h"
#include "backend/compiler/compiler.h"
#ifdef KINGLET_HAVE_LLVM
#include "backend/codegen/llvm/kir_to_llvm.h"
#endif
#include "ir/kir.h"
#include "frontend/module/module_loader.h"
#include "frontend/lexer/scanner.h"
#include "frontend/lexer/token.h"
#include "frontend/parser/parser.h"
#include "backend/vm/value.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef KINGLET_HAVE_EMBEDDED_COMPILER
namespace kinglet {
extern const unsigned char kEmbeddedCompilerData[];
extern const std::size_t kEmbeddedCompilerSize;
extern const char kEmbeddedCompilerHash[];
} // namespace kinglet
#endif

namespace {

#ifdef KINGLET_HAVE_EMBEDDED_COMPILER
// `kinglet selfhost <args...>`: materialise the embedded native self-host
// compiler in a content-addressed temp path and exec it.
int run_embedded_compiler(int argc, char **argv) {
#if defined(_WIN32)
  (void)argc;
  (void)argv;
  std::cerr << "kinglet: selfhost embedding is not supported on Windows\n";
  return 78;
#else
  namespace fs = std::filesystem;
  const fs::path cache_path =
      fs::temp_directory_path() /
      (std::string("kinglet-selfhost-") + kinglet::kEmbeddedCompilerHash);
  const std::string cache = cache_path.string();

  std::error_code ec;
  const bool fresh = fs::exists(cache_path, ec) &&
                     fs::file_size(cache_path, ec) == kinglet::kEmbeddedCompilerSize;
  if (!fresh) {
    const fs::path tmp_path = cache + ".tmp." + std::to_string(::getpid());
    {
      std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
      if (!out) {
        std::cerr << "kinglet: cannot write embedded compiler to " << tmp_path << "\n";
        return 74;
      }
      out.write(reinterpret_cast<const char *>(kinglet::kEmbeddedCompilerData),
                static_cast<std::streamsize>(kinglet::kEmbeddedCompilerSize));
      if (!out) {
        std::cerr << "kinglet: short write for embedded compiler\n";
        return 74;
      }
    }
    ::chmod(tmp_path.c_str(), 0755);
    fs::rename(tmp_path, cache_path, ec);
    if (ec && !fs::exists(cache_path)) {
      std::cerr << "kinglet: cannot install embedded compiler: " << ec.message() << "\n";
      fs::remove(tmp_path, ec);
      return 74;
    }
  }

  std::vector<char *> exec_argv;
  exec_argv.push_back(const_cast<char *>(cache.c_str()));
  for (int i = 2; i < argc; ++i) {
    exec_argv.push_back(argv[i]);
  }
  exec_argv.push_back(nullptr);
  ::execv(cache.c_str(), exec_argv.data());
  std::cerr << "kinglet: exec failed for " << cache << "\n";
  return 71;
#endif
}
#endif // KINGLET_HAVE_EMBEDDED_COMPILER

enum class Mode {
  Run,
  Tokens,
  Ast,
  Check,
  Ir,
  Native,
};

#ifdef KINGLET_HAVE_LLVM
std::string resolve_rt_lib(const char *argv0);

int run_native_executable(const kinglet::KirModule &kir, const std::vector<std::string> &program_args,
                          const char *argv0) {
  const std::filesystem::path tmp_dir = std::filesystem::temp_directory_path();
  std::string exe_template = (tmp_dir / "kinglet-run-XXXXXX").string();
  std::vector<char> tpl(exe_template.begin(), exe_template.end());
  tpl.push_back('\0');
#if defined(_WIN32)
  (void)kir;
  (void)program_args;
  (void)argv0;
  std::cerr << "kinglet: native execution is not supported on Windows\n";
  return 78;
#else
  const int fd = mkstemp(tpl.data());
  if (fd < 0) {
    std::cerr << "kinglet: failed to create temporary executable path\n";
    return 74;
  }
  close(fd);
  const std::string exe_path(tpl.data());
  std::filesystem::remove(exe_path);

  kinglet::NativeCompileOptions native_options;
  const kinglet::NativeCompileResult native = kinglet::KirToLlvm::compile_executable(
      kir, exe_path, resolve_rt_lib(argv0), native_options);
  if (!native.ok) {
    std::cerr << "kinglet: native compile failed: " << native.error << '\n';
    return 78;
  }

  const pid_t child = fork();
  if (child < 0) {
    std::cerr << "kinglet: failed to spawn native executable\n";
    std::filesystem::remove(exe_path);
    return 71;
  }
  if (child == 0) {
    std::vector<char *> exec_argv;
    exec_argv.push_back(tpl.data());
    for (const std::string &arg : program_args) {
      exec_argv.push_back(const_cast<char *>(arg.c_str()));
    }
    exec_argv.push_back(nullptr);
    execv(exe_path.c_str(), exec_argv.data());
    _exit(127);
  }

  int status = 0;
  if (waitpid(child, &status, 0) < 0) {
    std::filesystem::remove(exe_path);
    return 71;
  }
  std::filesystem::remove(exe_path);
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return 70;
#endif
}
#endif

#ifdef KINGLET_HAVE_LLVM
std::string resolve_rt_lib(const char *argv0) {
  const std::filesystem::path dir = std::filesystem::absolute(argv0).parent_path();
  for (const char *rel : { "libkinglet_rt.a", "obj/runtime/libkinglet_rt.a",
                           "kinglet_rt.lib", "obj/runtime/kinglet_rt.lib" }) {
    const std::filesystem::path candidate = dir / rel;
    if (std::filesystem::exists(candidate)) {
      return candidate.string();
    }
  }
  return (dir / "obj/runtime/kinglet_rt.lib").string();
}

// FNV-1a content hash of this compiler binary; mixed into object cache
// stamps so a rebuilt compiler invalidates previously cached objects.
std::string compiler_identity(const char *argv0) {
  std::ifstream file(std::filesystem::absolute(argv0), std::ios::binary);
  uint64_t hash = 1469598103934665603ull;
  char buffer[65536];
  while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
    const std::streamsize n = file.gcount();
    for (std::streamsize i = 0; i < n; ++i) {
      hash ^= static_cast<unsigned char>(buffer[i]);
      hash *= 1099511628211ull;
    }
  }
  std::ostringstream out;
  out << std::hex << hash;
  return out.str();
}
#endif

void print_usage(std::ostream &out) {
  out << "usage: kinglet [--tokens | --ast | --check | --ir | --native <out> [-g] [--obj-cache <dir>] | --backend native -o <out>] [file.kl] [args...]\n"
      << "\n"
      << "Reads Kinglet source from a .kl file, or stdin when file is omitted.\n"
      << "By default, compiles to a native executable and runs main() (requires LLVM build).\n"
      << "With --native -g, emits DWARF debug info from KIR line tables.\n"
      << "With --obj-cache, caches per-module objects by content stamp for incremental rebuilds.\n"
      << "With selfhost <args...>, runs the embedded native compiler (if built in).\n";
}

std::string read_stdin() {
  std::ostringstream buffer;
  buffer << std::cin.rdbuf();
  return buffer.str();
}

bool read_file(std::string_view path, std::string *out) {
  std::ifstream file(std::string(path), std::ios::in | std::ios::binary);
  if (!file) {
    return false;
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  *out = buffer.str();
  return true;
}

void print_escaped(std::ostream &out, std::string_view text) {
  for (char c : text) {
    switch (c) {
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      out << c;
      break;
    }
  }
}

void print_token(const kinglet::Token &token) {
  std::cout << token.line << ':' << token.column << ' '
            << kinglet::token_type_name(token.type);

  if (!token.lexeme.empty()) {
    std::cout << " \"";
    print_escaped(std::cout, token.lexeme);
    std::cout << '"';
  }

  switch (token.type) {
  case kinglet::TokenType::INTEGER:
  case kinglet::TokenType::CHAR_LIT:
    std::cout << " = " << token.int_value;
    break;
  case kinglet::TokenType::FLOAT_LIT:
    std::cout << " = " << token.float_value;
    break;
  default:
    break;
  }

  std::cout << '\n';
}

} // namespace

int main(int argc, char **argv) {
  if (argc >= 2) {
    const int sub_rc = kinglet::run_cli_subcommand(argc, argv);
    if (sub_rc >= 0) {
      return sub_rc;
    }
  }

  if (argc >= 2 && std::string_view(argv[1]) == "selfhost") {
#ifdef KINGLET_HAVE_EMBEDDED_COMPILER
    return run_embedded_compiler(argc, argv);
#else
    std::cerr << "kinglet: selfhost compiler not embedded "
                 "(rebuild with embed_compiler_path=\"/path/to/compiler\")\n";
    return 78;
#endif
  }

  std::string input_path;
  std::string native_out_path;
  std::string native_obj_cache_dir;
  Mode mode = Mode::Run;
  bool native_debug_info = false;
  std::vector<std::string> program_args;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    // Once an input file is set, everything after it is forwarded to the
    // program verbatim (so `kinglet script.kl --foo bar` passes `--foo bar`
    // to sys::args(), not to the interpreter).
    if (!input_path.empty()) {
      program_args.emplace_back(arg);
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      print_usage(std::cout);
      return 0;
    }
    if (arg == "--tokens") {
      mode = Mode::Tokens;
      continue;
    }
    if (arg == "--ast") {
      mode = Mode::Ast;
      continue;
    }
    if (arg == "--ir") {
      mode = Mode::Ir;
      continue;
    }
    if (arg == "--native") {
      mode = Mode::Native;
      if (i + 1 < argc) {
        ++i;
        native_out_path = argv[i];
      } else {
        std::cerr << "kinglet: --native requires an output path\n";
        return 64;
      }
      continue;
    }
    if (arg == "--backend") {
      if (i + 1 < argc && std::string_view(argv[i + 1]) == "native") {
        mode = Mode::Native;
        ++i;
      } else {
        std::cerr << "kinglet: --backend requires 'native'\n";
        return 64;
      }
      continue;
    }
    if (arg == "-o") {
      if (i + 1 < argc) {
        ++i;
        native_out_path = argv[i];
        if (mode != Mode::Native) {
          mode = Mode::Native;
        }
      } else {
        std::cerr << "kinglet: -o requires an output path\n";
        return 64;
      }
      continue;
    }
    if (arg == "--check") {
      mode = Mode::Check;
      continue;
    }
    if (arg == "--strip-debug") {
      std::cerr << "kinglet: --strip-debug is only supported with legacy bytecode tooling (removed)\n";
      return 64;
    }
    if (arg == "-g") {
      native_debug_info = true;
      continue;
    }
    if (arg == "--obj-cache") {
      if (i + 1 < argc) {
        ++i;
        native_obj_cache_dir = argv[i];
      } else {
        std::cerr << "kinglet: --obj-cache requires a directory\n";
        return 64;
      }
      continue;
    }
    if (arg == "--run" || arg == "--save-bytecode" || arg == "--repl" || arg == "--bytecode") {
      std::cerr << "kinglet: " << arg << " removed (VM backend deleted; use --ir or native build)\n";
      return 64;
    }
    input_path = std::string(arg);
  }

  std::string source;
  if (input_path.empty()) {
    source = read_stdin();
  } else if (!read_file(input_path, &source)) {
    std::cerr << "kinglet: failed to read '" << input_path << "'\n";
    return 66;
  }

  kinglet::Scanner scanner(std::move(source));
  const std::vector<kinglet::Token> tokens = scanner.scan_tokens();

  bool had_lexer_error = false;
  for (const kinglet::Token &token : tokens) {
    if (token.type == kinglet::TokenType::ERROR) {
      std::cerr << token.line << ':' << token.column << ": lexer error: "
                << token.lexeme << '\n';
      had_lexer_error = true;
    }
  }

  if (mode == Mode::Tokens) {
    for (const kinglet::Token &token : tokens) {
      print_token(token);
    }
    return had_lexer_error ? 65 : 0;
  }

  if (had_lexer_error) {
    return 65;
  }

  kinglet::Parser parser(tokens);
  kinglet::ParseResult result = parser.parse();
  for (const kinglet::ParseError &error : result.errors) {
    std::cerr << error.line << ':' << error.column << ": parse error: "
              << error.message << '\n';
  }

  if (!result.errors.empty()) {
    return 65;
  }

  if (mode == Mode::Ast) {
    result.program->print(std::cout);
    return 0;
  }

  std::string base_dir = ".";
  if (!input_path.empty()) {
    std::filesystem::path p(input_path);
    if (p.is_absolute()) {
      if (p.has_parent_path()) {
        base_dir = p.parent_path().string();
      }
    } else {
      base_dir = std::filesystem::absolute(p).parent_path().string();
      if (base_dir == ".") base_dir = std::filesystem::current_path().string();
    }
  }
  kinglet::ModuleLoader module_loader(base_dir);
  module_loader.discover_project_root(base_dir);
  if (!input_path.empty()) {
    std::filesystem::path input_p(input_path);
    if (input_p.is_absolute()) {
      module_loader.register_source_file(input_path);
    } else {
      module_loader.register_source_file((std::filesystem::current_path() / input_path).string());
    }
  }

  kinglet::TypeChecker checker;
  checker.set_module_loader(&module_loader);
  // Desugar pipe expressions once, after parse and before any downstream pass,
  // so check() and compile() receive identical already-normalized ASTs without
  // each having to mutate (and cast away const on) the program.
  kinglet::ast::desugar_pipes(*result.program);
  kinglet::TypeCheckResult type_result = checker.check(*result.program);
  bool has_type_errors = false;
  for (const kinglet::TypeError &error : type_result.errors) {
    const char *label = error.severity == kinglet::DiagnosticSeverity::Warning ? "warning" : "error";
    std::cerr << error.location.line << ':' << error.location.column
              << ": " << label << ": " << error.message << '\n';
    if (error.severity == kinglet::DiagnosticSeverity::Error) has_type_errors = true;
  }

  if (has_type_errors) {
    if (mode == Mode::Check) { return 65; }
    return 65;
  }

  if (mode == Mode::Check) { return 0; }

  kinglet::Compiler compiler;
  compiler.set_module_loader(&module_loader);
  if (!input_path.empty()) {
    const std::filesystem::path entry_path =
        std::filesystem::path(input_path).is_absolute()
            ? std::filesystem::path(input_path)
            : std::filesystem::current_path() / input_path;
    compiler.set_entry_source_path(entry_path.string());
  }
  kinglet::CompileResult compile_result = compiler.compile(*result.program);
  for (const kinglet::CompileError &error : compile_result.errors) {
    std::cerr << error.location.line << ':' << error.location.column
              << ": compile error: " << error.message << '\n';
  }

  if (!compile_result.errors.empty()) {
    return 65;
  }

  for (const kinglet::CompileWarning &warning : compile_result.warnings) {
    std::cerr << warning.location.line << ':' << warning.location.column
              << ": warning: " << warning.message << '\n';
  }

  kinglet::prepare_kir(compile_result.kir, checker);

  if (mode == Mode::Ir) {
    std::cout << dump_kir_module(compile_result.kir);
    return 0;
  }

  if (mode == Mode::Native) {
    if (native_out_path.empty()) {
      std::cerr << "kinglet: native output path required (--native <out> or -o <out>)\n";
      return 64;
    }
#ifndef KINGLET_HAVE_LLVM
    std::cerr << "kinglet: native backend not built (rebuild with enable_llvm=true)\n";
    return 78;
#else
    kinglet::NativeCompileOptions native_options;
    native_options.debug_info = native_debug_info;
    if (!native_obj_cache_dir.empty()) {
      native_options.object_cache_dir = native_obj_cache_dir;
      native_options.cache_salt = compiler_identity(argv[0]);
    }
    kinglet::NativeCompileResult native = kinglet::KirToLlvm::compile_executable(
        compile_result.kir, native_out_path, resolve_rt_lib(argv[0]), native_options);
    if (!native.ok) {
      std::cerr << "kinglet: native compile failed: " << native.error << "\n";
      return 78;
    }
    return 0;
#endif
  }

#ifdef KINGLET_HAVE_LLVM
  if (mode == Mode::Run) {
    return run_native_executable(compile_result.kir, program_args, argv[0]);
  }
#endif
  std::cerr << "kinglet: execution requires LLVM native backend (rebuild with enable_llvm=true)\n";
  return 78;
}
