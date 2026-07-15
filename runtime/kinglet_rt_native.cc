#include "runtime/kinglet_rt_internal.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <dirent.h>
#include <termios.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace {

std::vector<std::string> g_program_args;

void write_formatted(std::ostream &out, int32_t argc, const kl_h *args) {
  auto write_arg = [&out](kl_h arg) {
    if (kl_is_kind(arg, KlKind::String)) {
      const char *str_data = nullptr;
      int32_t str_len = 0;
      if (kl_string_view(arg, &str_data, &str_len) && str_data != nullptr) {
        out.write(str_data, str_len);
        return;
      }
    }
    out << kl_value_text(arg);
  };

  if (argc > 0) {
    const char *fmt_data = nullptr;
    int32_t fmt_len = 0;
    if (kl_string_view(args[0], &fmt_data, &fmt_len)) {
      const std::string fmt(fmt_data, static_cast<std::size_t>(fmt_len));
      int32_t val_idx = 1;
      for (std::size_t pos = 0; pos < fmt.size(); ++pos) {
        if (pos + 1 < fmt.size() && fmt[pos] == '{' && fmt[pos + 1] == '}') {
          if (val_idx < argc) {
            write_arg(args[val_idx++]);
          } else {
            out << "{}";
          }
          ++pos;
        } else {
          out << fmt[pos];
        }
      }
      return;
    }
  }
  for (int32_t i = 0; i < argc; ++i) {
    write_arg(args[i]);
  }
}

#if defined(__unix__) || defined(__APPLE__)
struct TermiosGuard {
  termios old{};
  bool active = false;

  void disable_echo() {
    if (!isatty(STDIN_FILENO)) {
      return;
    }
    termios current{};
    if (tcgetattr(STDIN_FILENO, &current) != 0) {
      return;
    }
    old = current;
    current.c_lflag &= static_cast<unsigned long>(~ECHO);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &current) == 0) {
      active = true;
    }
  }

  void restore() {
    if (active) {
      tcsetattr(STDIN_FILENO, TCSANOW, &old);
      active = false;
    }
  }

  ~TermiosGuard() { restore(); }
};
#endif

} // namespace

extern "C" {

void kl_set_program_args(int32_t argc, const char **argv) {
  g_program_args.clear();
  if (argv == nullptr || argc <= 1) {
    return;
  }
  // Skip argv[0] (executable path); kinglet forwards only args after the .kl file.
  g_program_args.reserve(static_cast<std::size_t>(argc - 1));
  for (int32_t i = 1; i < argc; ++i) {
    if (argv[i] != nullptr) {
      g_program_args.emplace_back(argv[i]);
    }
  }
}

kl_h kl_native_out(int32_t argc, const kl_h *args) {
  write_formatted(std::cout, argc, args);
  return 0;
}

kl_h kl_native_out_ln(int32_t argc, const kl_h *args) {
  write_formatted(std::cout, argc, args);
  std::cout << '\n';
  return 0;
}

kl_h kl_native_err(int32_t argc, const kl_h *args) {
  write_formatted(std::cerr, argc, args);
  return 0;
}

kl_h kl_native_err_ln(int32_t argc, const kl_h *args) {
  write_formatted(std::cerr, argc, args);
  std::cerr << '\n';
  return 0;
}

// Explicit stdout/stderr flush exposed to Kinglet as io::out.flush() /
// io::err.flush(). Plain io::out / io::err are block-buffered for throughput;
// callers force a sync point here when they need output visible immediately
// (e.g. before a blocking read on another channel, or progress reporting).
kl_h kl_native_out_flush(void) {
  std::cout.flush();
  return 0;
}

kl_h kl_native_err_flush(void) {
  std::cerr.flush();
  return 0;
}

kl_h kl_native_in(int32_t argc, const kl_h *args, int32_t secret) {
  for (int32_t i = 0; i < argc; ++i) {
    const char *data = nullptr;
    int32_t len = 0;
    if (kl_string_view(args[i], &data, &len) && len > 0) {
      std::cout.write(data, len);
      std::cout << std::flush;
    }
  }
#if defined(__unix__) || defined(__APPLE__)
  TermiosGuard guard;
  if (secret) {
    guard.disable_echo();
  }
#endif
  std::string line;
  if (!std::getline(std::cin, line)) {
    return kl_null_value();
  }
#if defined(__unix__) || defined(__APPLE__)
  if (secret) {
    guard.restore();
    std::cout << '\n';
  }
#endif
  return kl_string_new(line.data(), static_cast<int32_t>(line.size()));
}

kl_h kl_native_fs_read(kl_h path) {
  const char *data = nullptr;
  int32_t len = 0;
  if (!kl_string_view(path, &data, &len)) {
    return kl_null_value();
  }
  std::ifstream file(std::string(data, static_cast<std::size_t>(len)), std::ios::binary);
  if (!file) {
    return kl_null_value();
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (file.bad()) {
    return kl_null_value();
  }
  const std::string contents = buffer.str();
  return kl_string_new(contents.data(), static_cast<int32_t>(contents.size()));
}

kl_h kl_native_fs_write(kl_h path, kl_h content) {
  const char *path_data = nullptr;
  int32_t path_len = 0;
  const char *content_data = nullptr;
  int32_t content_len = 0;
  if (!kl_string_view(path, &path_data, &path_len) ||
      !kl_string_view(content, &content_data, &content_len)) {
    return 0;
  }
  std::ofstream file(std::string(path_data, static_cast<std::size_t>(path_len)),
                     std::ios::binary | std::ios::trunc);
  if (file) {
    file.write(content_data, content_len);
  }
  return 0;
}

kl_h kl_native_fs_listdir(kl_h path) {
  const char *data = nullptr;
  int32_t len = 0;
  if (!kl_string_view(path, &data, &len)) {
    return 0;
  }
  const std::string dir(data, static_cast<std::size_t>(len));
  std::vector<kl_h> entries;
#if defined(__unix__) || defined(__APPLE__)
  DIR *handle = opendir(dir.c_str());
  if (handle == nullptr) {
    return 0;
  }
  while (struct dirent *entry = readdir(handle)) {
    const char *name = entry->d_name;
    // Skip the synthetic "." and ".." entries; everything else (files,
    // subdirectories, dotfiles) is returned so the caller owns the policy.
    if (name[0] == '.' && name[1] == '\0')
      continue;
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
      continue;
    entries.push_back(kl_string_new(name, static_cast<int32_t>(std::strlen(name))));
  }
  closedir(handle);
#elif defined(_WIN32)
  // FindFirstFile requires a wildcard suffix to enumerate a directory.
  std::string pattern = dir;
  if (!pattern.empty() && pattern.back() != '\\' && pattern.back() != '/') {
    pattern += '\\';
  }
  pattern += '*';

  WIN32_FIND_DATAA ffd;
  HANDLE hFind = FindFirstFileA(pattern.c_str(), &ffd);
  if (hFind == INVALID_HANDLE_VALUE) {
    return 0;
  }
  do {
    const char *name = ffd.cFileName;
    // Skip "." and ".." (same semantics as the POSIX implementation).
    if (name[0] == '.' && name[1] == '\0')
      continue;
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
      continue;
    entries.push_back(kl_string_new(name, static_cast<int32_t>(std::strlen(name))));
  } while (FindNextFileA(hFind, &ffd));
  FindClose(hFind);
#endif
  return kl_array_new(static_cast<int32_t>(entries.size()), entries.data());
}

// --- Public API (ADR 0027) ---

kl_h kl_native_fs_exists(kl_h path) {
  const char *data = nullptr;
  int32_t len = 0;
  if (!kl_string_view(path, &data, &len)) {
    return kl_from_int(0);
  }
  const std::string p(data, static_cast<std::size_t>(len));
  std::error_code ec;
  return kl_from_int(std::filesystem::exists(p, ec) ? 1 : 0);
}

kl_h kl_native_fs_readtext(kl_h path) {
  // Same semantics as kl_native_fs_read but under the public name.
  return kl_native_fs_read(path);
}

kl_h kl_native_fs_writetext(kl_h path, kl_h content) {
  // Same semantics as kl_native_fs_write but under the public name.
  return kl_native_fs_write(path, content);
}

kl_h kl_native_fs_read_bytes(kl_h path) {
  const char *data = nullptr;
  int32_t len = 0;
  if (!kl_string_view(path, &data, &len)) {
    return kl_null_value();
  }
  std::ifstream file(std::string(data, static_cast<std::size_t>(len)), std::ios::binary);
  if (!file) {
    return kl_null_value();
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (file.bad()) {
    return kl_null_value();
  }
  const std::string contents = buffer.str();
  // Build a byte[] array: each byte becomes a kl_h int element.
  std::vector<kl_h> bytes;
  bytes.reserve(contents.size());
  for (unsigned char c : contents) {
    bytes.push_back(kl_from_int(static_cast<int64_t>(c)));
  }
  return kl_array_new(static_cast<int32_t>(bytes.size()), bytes.data());
}

kl_h kl_native_fs_write_bytes(kl_h path, kl_h data) {
  const char *path_data = nullptr;
  int32_t path_len = 0;
  if (!kl_string_view(path, &path_data, &path_len)) {
    return 0;
  }
  // Extract bytes from the byte[] array.
  std::string contents;
  int32_t arr_len = kl_array_len(data);
  if (arr_len > 0) {
    contents.reserve(static_cast<std::size_t>(arr_len));
    for (int32_t i = 0; i < arr_len; ++i) {
      kl_h elem = kl_array_get(data, i);
      contents.push_back(static_cast<char>(kl_to_int(elem) & 0xFF));
    }
  }
  std::ofstream file(std::string(path_data, static_cast<std::size_t>(path_len)),
                     std::ios::binary | std::ios::trunc);
  if (file) {
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  }
  return 0;
}

kl_h kl_native_sys_args(void) {
  std::vector<kl_h> elements;
  elements.reserve(g_program_args.size());
  for (const std::string &arg : g_program_args) {
    elements.push_back(kl_string_new(arg.data(), static_cast<int32_t>(arg.size())));
  }
  return kl_array_new(static_cast<int32_t>(elements.size()), elements.data());
}

kl_h kl_invoke_native(kl_h callee, int32_t argc, const kl_h *args) {
  const int64_t tag = kl_to_int(callee);
  if (tag >= 0) {
    return 0;
  }
  const int fn = static_cast<int>(-tag - 1);
  switch (fn) {
  case 0:
    return kl_native_out(argc, args);
  case 1:
    return kl_native_out_ln(argc, args);
  case 2:
    return kl_native_err(argc, args);
  case 3:
    return kl_native_err_ln(argc, args);
  case 4:
    return kl_native_in(argc, args, 0);
  case 5:
    return kl_native_in(argc, args, 1);
  case 6:
    return argc == 1 ? kl_native_fs_read(args[0]) : 0;
  case 7:
    return argc == 2 ? kl_native_fs_write(args[0], args[1]) : 0;
  case 8:
    return kl_native_sys_args();
  case 9:
    return kl_native_out_flush();
  case 10:
    return kl_native_err_flush();
  case 11:
    return argc == 1 ? kl_native_fs_exists(args[0]) : 0;
  case 12:
    return argc == 1 ? kl_native_fs_readtext(args[0]) : 0;
  case 13:
    return argc == 2 ? kl_native_fs_writetext(args[0], args[1]) : 0;
  case 14:
    return argc == 1 ? kl_native_fs_read_bytes(args[0]) : 0;
  case 15:
    return argc == 2 ? kl_native_fs_write_bytes(args[0], args[1]) : 0;
  default:
    return 0;
  }
}

} // extern "C"
