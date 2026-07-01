#include "backend/vm/value.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace kinglet {

// ── Accessor helpers ───────────────────────────────────────────────────

std::string &Value::string_val() {
  return static_cast<HeapString *>(heap.ptr)->value;
}
const std::string &Value::string_val() const {
  return static_cast<const HeapString *>(heap.ptr)->value;
}

// ── Factory methods ────────────────────────────────────────────────────

Value Value::int_value(int64_t value) {
  Value result;
  result.type = ValueType::Int;
  result.as_int = value;
  return result;
}

Value Value::double_value(double value) {
  Value result;
  result.type = ValueType::Double;
  result.as_double_storage = value;
  return result;
}

Value Value::bool_value(bool value) {
  Value result;
  result.type = ValueType::Bool;
  result.as_bool = value;
  return result;
}

Value Value::char_value(int8_t value) {
  Value result;
  result.type = ValueType::Char;
  result.as_int = static_cast<int64_t>(value);
  return result;
}

Value Value::null_value() {
  return Value{};
}

Value Value::string_value(std::string value) {
  Value result;
  result.type = ValueType::String;
  result.heap = RcPtr<HeapObj>{new HeapString(std::move(value))};
  return result;
}

Value Value::function_value(int index) {
  Value result;
  result.type = ValueType::Function;
  result.function_idx = index;
  return result;
}

Value Value::native_function_value(NativeFn fn) {
  Value result;
  result.type = ValueType::NativeFunction;
  result.native_fn = fn;
  return result;
}

Value Value::struct_value(int type_index, std::vector<Value> fields) {
  Value result;
  result.type = ValueType::Struct;
  result.heap = RcPtr<HeapObj>{new HeapStruct(type_index, std::move(fields))};
  return result;
}

Value Value::enum_value(int type_index, int variant_index) {
  Value result;
  result.type = ValueType::Enum;
  result.enum_type_idx = type_index;
  result.enum_variant_idx = variant_index;
  return result;
}

Value Value::enum_value_with_payload(int type_index, int variant_index, std::vector<Value> payload) {
  Value result;
  result.type = ValueType::Enum;
  result.enum_type_idx = type_index;
  result.enum_variant_idx = variant_index;
  result.heap = RcPtr<HeapObj>{new HeapEnum(type_index, variant_index, std::move(payload))};
  return result;
}

Value Value::array_value(std::vector<Value> elements) {
  Value result;
  result.type = ValueType::Array;
  result.heap = RcPtr<HeapObj>{new HeapArray(std::move(elements))};
  return result;
}

Value Value::dense_array_value(std::vector<Value> flat, std::vector<int32_t> dims) {
  Value result;
  result.type = ValueType::Array;
  auto *arr = new HeapArray(std::move(flat));
  arr->dense_dims = std::move(dims);
  result.heap = RcPtr<HeapObj>{arr};
  return result;
}

Value Value::map_value() {
  Value result;
  result.type = ValueType::Map;
  result.heap = RcPtr<HeapObj>{new HeapMap()};
  return result;
}

bool Value::is_number() const {
  return type == ValueType::Int || type == ValueType::Double;
}

double Value::as_double() const {
  if (type == ValueType::Int)
    return static_cast<double>(as_int);
  return as_double_storage;
}

int exit_code_from_value(const Value &value) {
  switch (value.type) {
  case ValueType::Int: {
    if (value.as_int < 0)
      return 255;
    if (value.as_int > 255)
      return 255;
    return static_cast<int>(value.as_int);
  }
  case ValueType::Bool:
    return value.as_bool ? 1 : 0;
  case ValueType::Double: {
    auto n = static_cast<int64_t>(value.as_double_storage);
    if (n < 0)
      return 255;
    if (n > 255)
      return 255;
    return static_cast<int>(n);
  }
  default:
    return 0;
  }
}

// ── Printing ───────────────────────────────────────────────────────────

std::ostream &operator<<(std::ostream &out, const Value &value) {
  switch (value.type) {
  case ValueType::Int:
    out << value.as_int;
    break;
  case ValueType::Double:
    out << value.as_double_storage;
    break;
  case ValueType::Bool:
    out << (value.as_bool ? "true" : "false");
    break;
  case ValueType::Char:
    out << static_cast<char>(value.as_int);
    break;
  case ValueType::Null:
    out << "null";
    break;
  case ValueType::String:
    out << value.string_val();
    break;
  case ValueType::Function:
    out << "<function:" << value.function_idx << ">";
    break;
  case ValueType::Struct: {
    auto &s = *static_cast<HeapStruct *>(value.heap.ptr);
    out << "<struct:" << s.type_index << ">";
    break;
  }
  case ValueType::Enum: {
    if (value.heap) {
      auto &e = *static_cast<HeapEnum *>(value.heap.ptr);
      out << "<enum:" << e.type_index << ":" << e.variant_index << ">";
    } else {
      out << "<enum:" << value.enum_type_idx << ":" << value.enum_variant_idx << ">";
    }
    break;
  }
  case ValueType::Array: {
    out << "[";
    if (value.heap) {
      auto &a = *static_cast<HeapArray *>(value.heap.ptr);
      for (std::size_t i = 0; i < a.elements.size(); ++i) {
        if (i > 0)
          out << ", ";
        out << a.elements[i];
      }
    }
    out << "]";
    break;
  }
  case ValueType::Map: {
    out << "{";
    if (value.heap) {
      auto &m = *static_cast<HeapMap *>(value.heap.ptr);
      for (std::size_t i = 0; i < m.order.size(); ++i) {
        if (i > 0)
          out << ", ";
        out << m.entries.at(m.order[i]).key << ": " << m.entries.at(m.order[i]).value;
      }
    }
    out << "}";
    break;
  }
  case ValueType::NativeFunction:
    out << "<native-fn>";
    break;
  }
  return out;
}

} // namespace kinglet
