#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace kinglet {

enum class ValueType {
  Int,
  Double,
  Bool,
  Char,
  Null,
  String,
  Function,
  Struct,
  Enum,
  Array,
  Map,
  NativeFunction,
};

enum class NativeFn {
  IoOut,
  IoOutLine,
  IoErr,
  IoErrLine,
  IoIn,
  IoInSecret,
  FsRead,
  FsWrite,
  SysArgs,
};

// ── Reference-counted heap infrastructure ──────────────────────────────

struct HeapObj {
  int refcount = 0;
  ValueType tag = ValueType::Null;
};

template <typename T>
struct RcPtr {
  T *ptr = nullptr;

  RcPtr() = default;
  explicit RcPtr(T *p) : ptr(p) {
    if (ptr) ++ptr->refcount;
  }

  RcPtr(const RcPtr &other) : ptr(other.ptr) {
    if (ptr) ++ptr->refcount;
  }

  RcPtr(RcPtr &&other) noexcept : ptr(other.ptr) {
    other.ptr = nullptr;
  }

  RcPtr &operator=(const RcPtr &other) {
    if (this != &other) {
      if (ptr && --ptr->refcount == 0) delete ptr;
      ptr = other.ptr;
      if (ptr) ++ptr->refcount;
    }
    return *this;
  }

  RcPtr &operator=(RcPtr &&other) noexcept {
    if (this != &other) {
      if (ptr && --ptr->refcount == 0) delete ptr;
      ptr = other.ptr;
      other.ptr = nullptr;
    }
    return *this;
  }

  ~RcPtr() {
    if (ptr && --ptr->refcount == 0) delete ptr;
  }

  T *operator->() const { return ptr; }
  T &operator*() const { return *ptr; }
  explicit operator bool() const { return ptr != nullptr; }
  bool operator==(const RcPtr &o) const { return ptr == o.ptr; }
  bool operator!=(const RcPtr &o) const { return ptr != o.ptr; }
};

struct Value;

// ── Value ──────────────────────────────────────────────────────────────

struct Value {
  static Value int_value(int64_t value);
  static Value double_value(double value);
  static Value bool_value(bool value);
  static Value char_value(int8_t value);
  static Value null_value();
  static Value string_value(std::string value);
  static Value function_value(int index);
  static Value native_function_value(NativeFn fn);
  static Value struct_value(int type_index, std::vector<Value> fields);
  static Value enum_value(int type_index, int variant_index);
  static Value enum_value_with_payload(int type_index, int variant_index,
                                        std::vector<Value> payload);
  static Value array_value(std::vector<Value> elements);
  static Value dense_array_value(std::vector<Value> flat, std::vector<int32_t> dims);
  static Value map_value();

  bool is_number() const;
  double as_double() const;

  // Accessor helpers — defined in value.cc where Heap* are complete.
  std::string &string_val();
  const std::string &string_val() const;

  // ── Fields ────────────────────────────────────────────────────────────

  ValueType type = ValueType::Null;
  int64_t as_int = 0;                  // Int, Char
  double as_double_storage = 0.0;      // Double
  bool as_bool = false;                // Bool
  int function_idx = -1;               // Function
  NativeFn native_fn = NativeFn::IoOut; // NativeFunction
  int enum_type_idx = -1;              // Enum (no-payload variant, inline)
  int enum_variant_idx = -1;           // Enum (no-payload variant, inline)
  RcPtr<HeapObj> heap;                 // String, Struct, Array, Enum(payload), Map
};

// ── Heap-allocated value types (defined after Value so they see it complete) ─

struct HeapString final : HeapObj {
  std::string value;
  explicit HeapString(std::string s) : value(std::move(s)) {
    tag = ValueType::String;
  }
};

struct HeapStruct final : HeapObj {
  int type_index;
  std::vector<Value> fields;
  HeapStruct(int ti, std::vector<Value> f)
      : type_index(ti), fields(std::move(f)) {
    tag = ValueType::Struct;
  }
};

struct HeapArray final : HeapObj {
  std::vector<Value> elements;
  std::vector<int32_t> dense_dims;
  explicit HeapArray(std::vector<Value> e) : elements(std::move(e)) {
    tag = ValueType::Array;
  }
};

struct MapEntry {
  Value key;
  Value value;
};

struct HeapEnum final : HeapObj {
  int type_index;
  int variant_index;
  std::vector<Value> payload;
  HeapEnum(int ti, int vi, std::vector<Value> p)
      : type_index(ti), variant_index(vi), payload(std::move(p)) {
    tag = ValueType::Enum;
  }
};

struct HeapMap final : HeapObj {
  std::vector<std::string> order;
  std::unordered_map<std::string, MapEntry> entries;
  HeapMap() { tag = ValueType::Map; }
};

std::ostream &operator<<(std::ostream &out, const Value &value);

// Map main()'s return value to a process exit code (shell-visible 0–255).
int exit_code_from_value(const Value &value);

} // namespace kinglet
