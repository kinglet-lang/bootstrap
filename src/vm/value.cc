#include "vm/value.h"

namespace kinglet {

Value Value::int_value(int64_t value) {
  Value result;
  result.type = ValueType::Int;
  result.int_value_storage = value;
  return result;
}

Value Value::double_value(double value) {
  Value result;
  result.type = ValueType::Double;
  result.double_value_storage = value;
  return result;
}

Value Value::bool_value(bool value) {
  Value result;
  result.type = ValueType::Bool;
  result.bool_value_storage = value;
  return result;
}

Value Value::null_value() {
  return Value{};
}

Value Value::string_value(std::string value) {
  Value result;
  result.type = ValueType::String;
  result.string_storage = std::move(value);
  return result;
}

Value Value::function_value(int index) {
  Value result;
  result.type = ValueType::Function;
  result.function_index_storage = index;
  return result;
}

bool Value::is_number() const {
  return type == ValueType::Int || type == ValueType::Double;
}

double Value::as_double() const {
  if (type == ValueType::Int) {
    return static_cast<double>(int_value_storage);
  }
  return double_value_storage;
}

std::ostream &operator<<(std::ostream &out, const Value &value) {
  switch (value.type) {
  case ValueType::Int:
    out << value.int_value_storage;
    break;
  case ValueType::Double:
    out << value.double_value_storage;
    break;
  case ValueType::Bool:
    out << (value.bool_value_storage ? "true" : "false");
    break;
  case ValueType::Null:
    out << "null";
    break;
  case ValueType::String:
    out << value.string_storage;
    break;
  case ValueType::Function:
    out << "<function:" << value.function_index_storage << ">";
    break;
  }
  return out;
}

} // namespace kinglet
