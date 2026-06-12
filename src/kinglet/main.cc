#include "kinglet/cli_driver.h"
#include "checker/type_checker.h"
#include "compiler/compiler.h"
#ifdef KINGLET_HAVE_LLVM
#include "codegen/llvm/kir_to_llvm.h"
#endif
#include "ir/kir.h"
#include "ir/kir_specialize.h"
#include "ir/kir_typing.h"
#include "module/module_loader.h"
#include "lexer/scanner.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "vm/value.h"
#include "vm/vm.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(KINGLET_HAVE_EMBEDDED_COMPILER) && !defined(_WIN32)
#include <sys/stat.h>
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
// compiler in a content-addressed temp path and exec it (ADR 0015 L5-2).
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
  RunBytecode,
  Tokens,
  Ast,
  Check,
  Bytecode,
  Ir,
  Native,
  SaveBytecode,
  Repl,
};

#ifdef KINGLET_HAVE_LLVM
std::string resolve_rt_lib(const char *argv0) {
  const std::filesystem::path dir = std::filesystem::absolute(argv0).parent_path();
  for (const char *rel : { "libkinglet_rt.a", "obj/runtime/libkinglet_rt.a" }) {
    const std::filesystem::path candidate = dir / rel;
    if (std::filesystem::exists(candidate)) {
      return candidate.string();
    }
  }
  return (dir / "obj/runtime/libkinglet_rt.a").string();
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
  out << "usage: kinglet [--tokens | --ast | --check | --ir | --native <out> [-g] [--obj-cache <dir>] | --backend native -o <out> | --bytecode | --save-bytecode <out.kbc> [--strip-debug] | --run <program.kbc> | --repl] [file.kl]\n"
      << "\n"
      << "Reads Kinglet source from a .kl file, or stdin when file is omitted.\n"
      << "By default, compiles and runs main().\n"
      << "With --run, loads and executes a pre-compiled .kbc file.\n"
      << "With --native -g, emits DWARF debug info from KIR line tables.\n"
      << "With --obj-cache, caches per-module objects by content stamp for incremental rebuilds.\n"
      << "With --save-bytecode --strip-debug, omits debug info for smaller output.\n"
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
  std::string save_bytecode_path;
  std::string native_out_path;
  std::string native_obj_cache_dir;
  std::string run_bytecode_path;
  Mode mode = Mode::Run;
  bool strip_debug = false;
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
    // Once in RunBytecode mode, all remaining args belong to the program.
    if (mode == Mode::RunBytecode) {
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
    if (arg == "--bytecode") {
      mode = Mode::Bytecode;
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
    if (arg == "--save-bytecode") {
      mode = Mode::SaveBytecode;
      // Next argument is the output path
      if (i + 1 < argc) {
        ++i;
        save_bytecode_path = argv[i];
      } else {
        std::cerr << "kinglet: --save-bytecode requires an output path\n";
        return 64;
      }
      continue;
    }
    if (arg == "--repl") {
      mode = Mode::Repl;
      continue;
    }
    if (arg == "--strip-debug") {
      strip_debug = true;
      continue;
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
    if (arg == "--run") {
      mode = Mode::RunBytecode;
      if (i + 1 < argc) {
        ++i;
        run_bytecode_path = argv[i];
      } else {
        std::cerr << "kinglet: --run requires a .kbc file\n";
        return 64;
      }
      continue;
    }
    input_path = std::string(arg);
  }

  // --run mode: load and execute a pre-compiled .kbc file.
  if (mode == Mode::RunBytecode) {
    program_args.clear();
    bool past_run_path = false;
    for (int i = 1; i < argc; ++i) {
      if (!past_run_path) {
        if (std::string_view(argv[i]) == "--run") {
          ++i; // skip the .kbc path too
          past_run_path = true;
        }
        continue;
      }
      program_args.emplace_back(argv[i]);
    }

    std::string error;
    kinglet::Chunk chunk = kinglet::Chunk::deserialize(run_bytecode_path, &error);
    if (!error.empty()) {
      std::cerr << "kinglet: " << error << "\n";
      return 66;
    }

    kinglet::Vm vm;
    kinglet::VmResult result = vm.run(chunk, program_args);
    if (!result.ok) {
      std::cerr << "runtime error: " << result.error << "\n";
      return 70;
    }

    return kinglet::exit_code_from_value(result.value);
  }

  std::string source;
  if (input_path.empty() && mode != Mode::Repl) {
    source = read_stdin();
  } else if (!input_path.empty()) {
    if (!read_file(input_path, &source)) {
      std::cerr << "kinglet: failed to read '" << input_path << "'\n";
      return 66;
    }
  }

  if (mode == Mode::Repl) {
    std::cout << "Kinglet REPL (type 'exit' to quit)\n";
    kinglet::Vm vm;
    while (true) {
      std::cout << "> " << std::flush;
      std::string line;
      if (!std::getline(std::cin, line) || line == "exit") {
        break;
      }
      if (line.empty()) {
        continue;
      }

      // Wrap in a main function if not already wrapped
      std::string wrapped_source;
      if (line.find("int main") == std::string::npos &&
          line.find("fn main") == std::string::npos) {
        std::string expr = line;
        while (!expr.empty() && expr.back() == ';') {
          expr.pop_back();
        }

        const char *return_types[] = {"int", "float", "string", "bool", "void"};
        bool found = false;
        for (const char *rt : return_types) {
          wrapped_source =
              "using io; " + std::string(rt) + " main() => " + expr + ";";
          kinglet::Scanner test_scanner(wrapped_source);
          auto test_tokens = test_scanner.scan_tokens();
          kinglet::Parser test_parser(test_tokens);
          auto test_result = test_parser.parse();
          if (!test_result.errors.empty()) continue;
          kinglet::TypeChecker test_checker;
          auto test_type = test_checker.check(*test_result.program);
          if (test_type.errors.empty()) {
            found = true;
            break;
          }
        }
        if (!found) {
          wrapped_source = "using io; int main() => " + expr + ";";
        }
      } else {
        wrapped_source = line;
      }

      kinglet::Scanner scanner(std::move(wrapped_source));
      const std::vector<kinglet::Token> tokens = scanner.scan_tokens();

      bool had_error = false;
      for (const kinglet::Token &token : tokens) {
        if (token.type == kinglet::TokenType::ERROR) {
          std::cerr << token.line << ':' << token.column << ": lexer error: "
                    << token.lexeme << '\n';
          had_error = true;
        }
      }
      if (had_error) {
        continue;
      }

      kinglet::Parser parser(tokens);
      kinglet::ParseResult result = parser.parse();
      for (const kinglet::ParseError &error : result.errors) {
        std::cerr << error.line << ':' << error.column << ": parse error: "
                  << error.message << '\n';
      }
      if (!result.errors.empty()) {
        continue;
      }

      kinglet::TypeChecker checker;
      kinglet::TypeCheckResult type_result = checker.check(*result.program);
      bool has_type_errors = false;
      for (const kinglet::TypeError &error : type_result.errors) {
        const char *label = error.severity == kinglet::DiagnosticSeverity::Warning ? "warning" : "error";
        std::cerr << error.location.line << ':' << error.location.column
                  << ": " << label << ": " << error.message << '\n';
        if (error.severity == kinglet::DiagnosticSeverity::Error) has_type_errors = true;
      }
      if (has_type_errors) {
        continue;
      }

      kinglet::Compiler compiler;
      kinglet::CompileResult compile_result = compiler.compile(*result.program);
      for (const kinglet::CompileError &error : compile_result.errors) {
        std::cerr << error.location.line << ':' << error.location.column
                  << ": compile error: " << error.message << '\n';
      }
      if (!compile_result.errors.empty()) {
        continue;
      }
      for (const kinglet::CompileWarning &warning : compile_result.warnings) {
        std::cerr << warning.location.line << ':' << warning.location.column
                  << ": warning: " << warning.message << '\n';
      }

      kinglet::VmResult vm_result = vm.run(compile_result.chunk);
      if (!vm_result.ok) {
        std::cerr << "runtime error: " << vm_result.error << '\n';
        continue;
      }

      if (vm_result.value.type != kinglet::ValueType::Null &&
          (vm_result.value.type != kinglet::ValueType::Int ||
           vm_result.value.as_int != 0)) {
        std::cout << vm_result.value << '\n';
      }
    }
    return 0;
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
    // Only abort on type errors for interactive modes (Run, Bytecode).
    // For --save-bytecode, type errors are shown but do not prevent
    // bytecode generation since the compiler does not depend on the
    // type checker output.
    if (mode != Mode::SaveBytecode) {
      return 65;
    }
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

  checker.populate_kir_types(&compile_result.kir);
  infer_kir_types(&compile_result.kir);
  specialize_kir_arithmetic(&compile_result.kir);

  if (mode == Mode::Bytecode) {
    compile_result.chunk.disassemble(std::cout);
    return 0;
  }

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

  if (mode == Mode::SaveBytecode) {
    if (!compile_result.chunk.serialize(save_bytecode_path, strip_debug)) {
      std::cerr << "kinglet: failed to write '" << save_bytecode_path << "'\n";
      return 74;
    }
    return 0;
  }

  kinglet::Vm vm;
  kinglet::VmResult vm_result = vm.run(compile_result.chunk, program_args);
  if (!vm_result.ok) {
    std::cerr << "runtime error: " << vm_result.error << '\n';
    return 70;
  }

  return kinglet::exit_code_from_value(vm_result.value);
}
