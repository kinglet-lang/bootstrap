#include "runtime/kinglet_rt_internal.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <iconv.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#endif

namespace {

std::vector<std::string> g_program_args;

kl_h bytes_from_string_bytes(const std::string &bytes) {
  std::vector<kl_h> out;
  out.reserve(bytes.size());
  for (char c : bytes) {
    out.push_back(kl_from_int(static_cast<int64_t>(static_cast<unsigned char>(c))));
  }
  return kl_array_new(static_cast<int32_t>(out.size()), out.data());
}

std::string bytes_array_to_string(kl_h data) {
  std::string out;
  const int32_t len = kl_array_len(data);
  if (len <= 0) {
    return out;
  }
  out.reserve(static_cast<std::size_t>(len));
  for (int32_t i = 0; i < len; ++i) {
    out.push_back(static_cast<char>(kl_to_int(kl_array_get(data, i)) & 0xFF));
  }
  return out;
}

#if defined(__unix__) || defined(__APPLE__)
std::string convert_encoding_iconv(const std::string &input, const char *to_code,
                                   const char *from_code) {
  iconv_t cd = iconv_open(to_code, from_code);
  if (cd == reinterpret_cast<iconv_t>(-1)) {
    return "";
  }

  std::vector<char> output(input.size() * 4 + 16);
  char *in_ptr = const_cast<char *>(input.data());
  std::size_t in_left = input.size();
  char *out_ptr = output.data();
  std::size_t out_left = output.size();

  while (true) {
    const std::size_t rc = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
    if (rc != static_cast<std::size_t>(-1)) {
      break;
    }
    if (errno == E2BIG) {
      const std::size_t used = static_cast<std::size_t>(out_ptr - output.data());
      output.resize(output.size() * 2 + 16);
      out_ptr = output.data() + used;
      out_left = output.size() - used;
      continue;
    }
    iconv_close(cd);
    return "";
  }

  const std::size_t used = static_cast<std::size_t>(out_ptr - output.data());
  iconv_close(cd);
  return std::string(output.data(), used);
}
#endif

#if defined(_WIN32)
std::string convert_windows_codepage_to_utf8(const std::string &input, UINT codepage) {
  if (input.empty()) {
    return "";
  }
  const int wide_len =
      MultiByteToWideChar(codepage, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
  if (wide_len <= 0) {
    return "";
  }
  std::wstring wide(static_cast<std::size_t>(wide_len), L'\0');
  MultiByteToWideChar(codepage, 0, input.data(), static_cast<int>(input.size()), wide.data(),
                      wide_len);
  const int utf8_len =
      WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_len, nullptr, 0, nullptr, nullptr);
  if (utf8_len <= 0) {
    return "";
  }
  std::string out(static_cast<std::size_t>(utf8_len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_len, out.data(), utf8_len, nullptr, nullptr);
  return out;
}

std::string convert_utf8_to_windows_codepage(const std::string &input, UINT codepage) {
  if (input.empty()) {
    return "";
  }
  const int wide_len =
      MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
  if (wide_len <= 0) {
    return "";
  }
  std::wstring wide(static_cast<std::size_t>(wide_len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), wide.data(),
                      wide_len);
  const int out_len =
      WideCharToMultiByte(codepage, 0, wide.data(), wide_len, nullptr, 0, nullptr, nullptr);
  if (out_len <= 0) {
    return "";
  }
  std::string out(static_cast<std::size_t>(out_len), '\0');
  WideCharToMultiByte(codepage, 0, wide.data(), wide_len, out.data(), out_len, nullptr, nullptr);
  return out;
}
#endif

std::string convert_utf8_to_gbk(const std::string &input) {
#if defined(_WIN32)
  return convert_utf8_to_windows_codepage(input, 936);
#elif defined(__unix__) || defined(__APPLE__)
  return convert_encoding_iconv(input, "GBK", "UTF-8");
#else
  return input;
#endif
}

std::string convert_gbk_to_utf8(const std::string &input) {
#if defined(_WIN32)
  return convert_windows_codepage_to_utf8(input, 936);
#elif defined(__unix__) || defined(__APPLE__)
  return convert_encoding_iconv(input, "UTF-8", "GBK");
#else
  return input;
#endif
}

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

// --- File handle API (ADR 0027 D1+D2) ---

kl_h kl_native_fs_open(kl_h path) {
  const char *data = nullptr;
  int32_t len = 0;
  if (!kl_string_view(path, &data, &len)) {
    return kl_null_value();
  }
  const std::string p(data, static_cast<std::size_t>(len));
  std::error_code ec;
  if (!std::filesystem::exists(p, ec)) {
    return kl_null_value();
  }
#if defined(__unix__) || defined(__APPLE__)
  int fd = ::open(p.c_str(), O_RDONLY);
  if (fd < 0) {
    return kl_null_value();
  }
#else
  // Windows: open for read using _open
  int fd = ::_open(p.c_str(), _O_RDONLY | _O_BINARY);
  if (fd < 0) {
    return kl_null_value();
  }
#endif
  auto *f = new KlFile();
  f->fd = static_cast<int64_t>(fd);
  f->write_mode = 0;
  return kl_box_ptr(f);
}

kl_h kl_native_fs_create(kl_h path) {
  const char *data = nullptr;
  int32_t len = 0;
  if (!kl_string_view(path, &data, &len)) {
    return kl_null_value();
  }
  const std::string p(data, static_cast<std::size_t>(len));
#if defined(__unix__) || defined(__APPLE__)
  int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    return kl_null_value();
  }
#else
  int fd = ::_open(p.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0644);
  if (fd < 0) {
    return kl_null_value();
  }
#endif
  auto *f = new KlFile();
  f->fd = static_cast<int64_t>(fd);
  f->write_mode = 1;
  return kl_box_ptr(f);
}

// Returns bytes read (>=0) or 0 on error. Error is not propagated as an
// exception -- callers that need to distinguish EOF from error should check
// the file state via open(). This mirrors the current whole-file fs::read/fs::write behavior.
kl_h kl_native_file_read(kl_h file_val, kl_h buffer_val) {
  // Type/mode guard: wrong kind or wrong mode -> 0 (no bytes read).
  if (!kl_is_kind(file_val, KlKind::File)) {
    return kl_from_int(0);
  }
  auto *f = static_cast<KlFile *>(kl_unbox_ptr(file_val));
  if (f->fd < 0 || f->write_mode != 0) {
    return kl_from_int(0);
  }
  // Read into the byte[] buffer, return number of bytes read.
  int32_t buf_len = kl_array_len(buffer_val);
  if (buf_len <= 0) {
    return kl_from_int(0);
  }
  std::vector<char> raw(static_cast<std::size_t>(buf_len));
#if defined(__unix__) || defined(__APPLE__)
  ssize_t n = ::read(static_cast<int>(f->fd), raw.data(), static_cast<size_t>(buf_len));
#else
  ssize_t n = ::_read(static_cast<int>(f->fd), raw.data(), static_cast<unsigned int>(buf_len));
#endif
  if (n < 0) {
    n = 0;
  }
  // Write bytes back into the kl_h array elements.
  for (ssize_t i = 0; i < n; ++i) {
    kl_index_set(buffer_val, kl_from_int(static_cast<int64_t>(i)),
                 kl_from_int(static_cast<int64_t>(static_cast<unsigned char>(raw[i]))));
  }
  return kl_from_int(static_cast<int64_t>(n));
}

// Returns bytes written (>=0) or 0 on error. Same error convention as read.
kl_h kl_native_file_write(kl_h file_val, kl_h data_val) {
  // Type/mode guard: wrong kind or wrong mode -> 0 (no bytes written).
  if (!kl_is_kind(file_val, KlKind::File)) {
    return kl_from_int(0);
  }
  auto *f = static_cast<KlFile *>(kl_unbox_ptr(file_val));
  if (f->fd < 0 || f->write_mode != 1) {
    return kl_from_int(0);
  }
  // Extract bytes from byte[] and write them.
  int32_t arr_len = kl_array_len(data_val);
  if (arr_len <= 0) {
    return kl_from_int(0);
  }
  std::string contents;
  contents.reserve(static_cast<std::size_t>(arr_len));
  for (int32_t i = 0; i < arr_len; ++i) {
    kl_h elem = kl_array_get(data_val, i);
    contents.push_back(static_cast<char>(kl_to_int(elem) & 0xFF));
  }
#if defined(__unix__) || defined(__APPLE__)
  ssize_t n =
      ::write(static_cast<int>(f->fd), contents.data(), static_cast<size_t>(contents.size()));
#else
  ssize_t n = ::_write(static_cast<int>(f->fd), contents.data(),
                       static_cast<unsigned int>(contents.size()));
#endif
  if (n < 0) {
    n = 0;
  }
  return kl_from_int(static_cast<int64_t>(n));
}

kl_h kl_native_file_size(kl_h file_val) {
  if (!kl_is_kind(file_val, KlKind::File)) {
    return kl_from_int(0);
  }
  auto *f = static_cast<KlFile *>(kl_unbox_ptr(file_val));
  if (f->fd < 0) {
    return kl_from_int(0);
  }
  std::error_code ec;
  // Use lseek to get file size on POSIX, _filelength on Windows.
#if defined(__unix__) || defined(__APPLE__)
  off_t cur = ::lseek(static_cast<int>(f->fd), 0, SEEK_CUR);
  off_t end = ::lseek(static_cast<int>(f->fd), 0, SEEK_END);
  ::lseek(static_cast<int>(f->fd), cur, SEEK_SET);
  return kl_from_int(static_cast<int64_t>(end));
#else
  int64_t sz = ::_filelengthi64(static_cast<int>(f->fd));
  return kl_from_int(sz);
#endif
}

kl_h kl_native_file_sync(kl_h file_val) {
  if (!kl_is_kind(file_val, KlKind::File)) {
    return kl_from_int(0);
  }
  auto *f = static_cast<KlFile *>(kl_unbox_ptr(file_val));
  if (f->fd < 0) {
    return kl_from_int(0);
  }
#if defined(__unix__) || defined(__APPLE__)
  ::fsync(static_cast<int>(f->fd));
#else
  ::_commit(static_cast<int>(f->fd));
#endif
  return kl_from_int(0);
}

kl_h kl_native_file_close(kl_h file_val) {
  if (!kl_is_kind(file_val, KlKind::File)) {
    return kl_from_int(0);
  }
  auto *f = static_cast<KlFile *>(kl_unbox_ptr(file_val));
  if (f->fd >= 0) {
#if defined(__unix__) || defined(__APPLE__)
    ::close(static_cast<int>(f->fd));
#else
    ::_close(static_cast<int>(f->fd));
#endif
    f->fd = -1;
  }
  return kl_from_int(0);
}

kl_h kl_native_file_is_open(kl_h file_val) {
  if (!kl_is_kind(file_val, KlKind::File)) {
    return kl_from_int(0);
  }
  auto *f = static_cast<KlFile *>(kl_unbox_ptr(file_val));
  return kl_from_int(f->fd >= 0 ? 1 : 0);
}

kl_h kl_native_txt_utf8_encode(kl_h text) {
  const char *data = nullptr;
  int32_t len = 0;
  if (!kl_string_view(text, &data, &len)) {
    return kl_array_new(0, nullptr);
  }
  return bytes_from_string_bytes(std::string(data, static_cast<std::size_t>(len)));
}

kl_h kl_native_txt_utf8_decode(kl_h data) {
  const std::string bytes = bytes_array_to_string(data);
  return kl_string_new(bytes.data(), static_cast<int32_t>(bytes.size()));
}

kl_h kl_native_txt_gbk_encode(kl_h text) {
  const char *data = nullptr;
  int32_t len = 0;
  if (!kl_string_view(text, &data, &len)) {
    return kl_array_new(0, nullptr);
  }
  return bytes_from_string_bytes(
      convert_utf8_to_gbk(std::string(data, static_cast<std::size_t>(len))));
}

kl_h kl_native_txt_gbk_decode(kl_h data) {
  const std::string utf8 = convert_gbk_to_utf8(bytes_array_to_string(data));
  return kl_string_new(utf8.data(), static_cast<int32_t>(utf8.size()));
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
  case 13:
    // Reserved: removed public text helper slots.
    return 0;
  case 14:
    return argc == 1 ? kl_native_fs_read_bytes(args[0]) : 0;
  case 15:
    return argc == 2 ? kl_native_fs_write_bytes(args[0], args[1]) : 0;
  case 16:
    return argc == 1 ? kl_native_fs_open(args[0]) : 0;
  case 17:
    return argc == 1 ? kl_native_fs_create(args[0]) : 0;
  case 18:
    return argc == 2 ? kl_native_file_read(args[0], args[1]) : 0;
  case 19:
    return argc == 2 ? kl_native_file_write(args[0], args[1]) : 0;
  case 20:
    return argc == 1 ? kl_native_file_size(args[0]) : 0;
  case 21:
    return argc == 1 ? kl_native_file_sync(args[0]) : 0;
  case 22:
    return argc == 1 ? kl_native_file_close(args[0]) : 0;
  case 23:
    return argc == 1 ? kl_native_file_is_open(args[0]) : 0;
  default:
    return 0;
  }
}

} // extern "C"
