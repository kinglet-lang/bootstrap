#include "vm/vm.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace kinglet {

namespace {

std::string encode_map_key(const Value &key) {
  if (key.type == ValueType::String) return "s:" + key.string_val();
  if (key.type == ValueType::Int) return "i:" + std::to_string(key.as_int);
  return std::string();
}

// Shorthands to avoid verbose casting in hot path.
inline HeapString *as_string(Value &v) {
  return static_cast<HeapString *>(v.heap.ptr);
}
inline const HeapString *as_string(const Value &v) {
  return static_cast<const HeapString *>(v.heap.ptr);
}
inline HeapStruct *as_struct(Value &v) {
  return static_cast<HeapStruct *>(v.heap.ptr);
}
inline const HeapStruct *as_struct(const Value &v) {
  return static_cast<const HeapStruct *>(v.heap.ptr);
}
inline HeapArray *as_array(Value &v) {
  return static_cast<HeapArray *>(v.heap.ptr);
}
inline const HeapArray *as_array(const Value &v) {
  return static_cast<const HeapArray *>(v.heap.ptr);
}
inline HeapEnum *as_enum(Value &v) {
  return static_cast<HeapEnum *>(v.heap.ptr);
}
inline const HeapEnum *as_enum(const Value &v) {
  return static_cast<const HeapEnum *>(v.heap.ptr);
}
inline HeapMap *as_map(Value &v) {
  return static_cast<HeapMap *>(v.heap.ptr);
}
inline const HeapMap *as_map(const Value &v) {
  return static_cast<const HeapMap *>(v.heap.ptr);
}

} // namespace

VmResult Vm::run(const Chunk &chunk, const std::vector<std::string> &args) {
  stack_.clear();
  frames_.clear();
  program_args_ = args;
  frames_.push_back(CallFrame{.chunk = &chunk, .ip = 0, .locals = {}});

  while (!frames_.empty()) {
    CallFrame &frame = frames_.back();
    const std::vector<Instruction> &instructions = frame.chunk->instructions();
    const std::vector<Value> &constants = frame.chunk->constants();

    if (frame.ip >= instructions.size()) {
      return runtime_error("Instruction pointer out of range.");
    }

    const Instruction &instruction = instructions[frame.ip];
    ++frame.ip;

    switch (instruction.op) {
    case OpCode::Constant:
      if (instruction.operand < 0 ||
          static_cast<std::size_t>(instruction.operand) >= constants.size()) {
        return runtime_error("Constant index out of range.");
      }
      push(constants[static_cast<std::size_t>(instruction.operand)]);
      break;
    case OpCode::Null:
      push(Value::null_value());
      break;
    case OpCode::True:
      push(Value::bool_value(true));
      break;
    case OpCode::False:
      push(Value::bool_value(false));
      break;
    case OpCode::Add:
    case OpCode::Subtract:
    case OpCode::Multiply:
    case OpCode::Divide:
    case OpCode::Modulo: {
      if (instruction.op == OpCode::Add && stack_.size() >= 2 &&
          (stack_[stack_.size() - 1].type == ValueType::String ||
           stack_[stack_.size() - 2].type == ValueType::String)) {
        Value right = pop();
        Value left = pop();
        auto to_str = [](const Value &v) -> std::string {
          if (v.type == ValueType::String) return v.string_val();
          if (v.type == ValueType::Int) return std::to_string(v.as_int);
          if (v.type == ValueType::Double)
            return std::to_string(v.as_double_storage);
          if (v.type == ValueType::Bool)
            return v.as_bool ? "true" : "false";
          if (v.type == ValueType::Null) return "null";
          return std::string();
        };
        push(Value::string_value(to_str(left) + to_str(right)));
        break;
      }
      std::string error;
      if (!binary_numeric(instruction.op, &error)) {
        return runtime_error(std::move(error));
      }
      break;
    }
    case OpCode::BitNot: {
      if (stack_.empty() || stack_.back().type != ValueType::Int) {
        return runtime_error("Operand must be an integer.");
      }
      Value value = pop();
      push(Value::int_value(~value.as_int));
      break;
    }
    case OpCode::BitAnd:
    case OpCode::BitOr:
    case OpCode::BitXor:
    case OpCode::Shl:
    case OpCode::Shr: {
      if (stack_.size() < 2) return runtime_error("Stack underflow.");
      if (stack_[stack_.size() - 1].type != ValueType::Int ||
          stack_[stack_.size() - 2].type != ValueType::Int) {
        return runtime_error("Bitwise operands must be integers.");
      }
      Value right = pop();
      Value left = pop();
      int64_t l = left.as_int;
      int64_t r = right.as_int;
      int64_t result = 0;
      switch (instruction.op) {
      case OpCode::BitAnd:
        result = l & r;
        break;
      case OpCode::BitOr:
        result = l | r;
        break;
      case OpCode::BitXor:
        result = l ^ r;
        break;
      case OpCode::Shl:
      case OpCode::Shr:
        if (r < 0 || r >= 64) {
          result = 0;
        } else if (instruction.op == OpCode::Shl) {
          result = static_cast<int64_t>(static_cast<uint64_t>(l) << r);
        } else {
          result = static_cast<int64_t>(static_cast<uint64_t>(l) >> r);
        }
        break;
      default:
        break;
      }
      push(Value::int_value(result));
      break;
    }
    case OpCode::Negate: {
      if (stack_.empty() || !stack_.back().is_number())
        return runtime_error("Operand must be numeric.");
      Value value = pop();
      if (value.type == ValueType::Int)
        push(Value::int_value(-value.as_int));
      else
        push(Value::double_value(-value.as_double_storage));
      break;
    }
    case OpCode::Not: {
      if (stack_.empty()) return runtime_error("Stack underflow.");
      Value value = pop();
      bool truthy = false;
      switch (value.type) {
      case ValueType::Bool:
        truthy = value.as_bool;
        break;
      case ValueType::Null:
        truthy = false;
        break;
      case ValueType::Int:
        truthy = value.as_int != 0;
        break;
      case ValueType::Double:
        truthy = value.as_double_storage != 0.0;
        break;
      case ValueType::String:
        truthy = !value.string_val().empty();
        break;
      case ValueType::Char:
      case ValueType::Function:
      case ValueType::Struct:
      case ValueType::Enum:
      case ValueType::Array:
      case ValueType::Map:
      case ValueType::NativeFunction:
        truthy = true;
        break;
      }
      push(Value::bool_value(!truthy));
      break;
    }
    case OpCode::LoadLocal:
      if (instruction.operand < 0 ||
          static_cast<std::size_t>(instruction.operand) >= frame.locals.size())
        return runtime_error("Local slot out of range.");
      push(frame.locals[static_cast<std::size_t>(instruction.operand)]);
      break;
    case OpCode::StoreLocal:
      if (stack_.empty()) return runtime_error("Stack underflow.");
      if (instruction.operand < 0) return runtime_error("Invalid local slot.");
      if (static_cast<std::size_t>(instruction.operand) >=
          frame.locals.size()) {
        frame.locals.resize(
            static_cast<std::size_t>(instruction.operand) + 1,
            Value::null_value());
      }
      frame.locals[static_cast<std::size_t>(instruction.operand)] =
          stack_.back();
      break;
    case OpCode::Pop:
      if (stack_.empty()) return runtime_error("Stack underflow.");
      pop();
      break;
    case OpCode::Dup:
      if (stack_.empty()) return runtime_error("Stack underflow.");
      stack_.push_back(stack_.back());
      break;
    case OpCode::CastTo: {
      Value src = pop();
      const int kind = instruction.operand;
      const int kCastErrorIdx = 0;
      auto cast_err_empty = [&]() {
        return Value::enum_value(kCastErrorIdx, 0);
      };
      auto cast_err_with = [&](int variant, const std::string &payload) {
        std::vector<Value> p;
        p.push_back(Value::string_value(payload));
        return Value::enum_value_with_payload(kCastErrorIdx, variant,
                                               std::move(p));
      };
      if (kind == 0) { // int
        if (src.type == ValueType::Int) {
          push(src);
        } else if (src.type == ValueType::Double) {
          push(Value::int_value(
              static_cast<int64_t>(src.as_double_storage)));
        } else if (src.type == ValueType::Char) {
          push(Value::int_value(src.as_int));
        } else if (src.type == ValueType::Enum) {
          push(Value::int_value(static_cast<int64_t>(src.enum_variant_idx)));
        } else if (src.type == ValueType::String) {
          const std::string &s = src.string_val();
          if (s.empty()) {
            push(cast_err_empty());
          } else {
            char *end = nullptr;
            errno = 0;
            long long v = std::strtoll(s.c_str(), &end, 10);
            if (end == s.c_str() || *end != '\0') {
              push(cast_err_with(1, s));
            } else if (errno == ERANGE) {
              push(cast_err_with(2, s));
            } else {
              push(Value::int_value(static_cast<int64_t>(v)));
            }
          }
        } else {
          return runtime_error(
              "Cast to int requires int, float, char, or string operand.");
        }
      } else if (kind == 1) { // float
        if (src.type == ValueType::Double) {
          push(src);
        } else if (src.type == ValueType::Int) {
          push(Value::double_value(
              static_cast<double>(src.as_int)));
        } else if (src.type == ValueType::Char) {
          push(Value::double_value(
              static_cast<double>(src.as_int)));
        } else if (src.type == ValueType::String) {
          const std::string &s = src.string_val();
          if (s.empty()) {
            push(cast_err_empty());
          } else {
            char *end = nullptr;
            errno = 0;
            double v = std::strtod(s.c_str(), &end);
            if (end == s.c_str() || *end != '\0') {
              push(cast_err_with(1, s));
            } else if (errno == ERANGE) {
              push(cast_err_with(2, s));
            } else {
              push(Value::double_value(v));
            }
          }
        } else {
          return runtime_error(
              "Cast to float requires int, float, or string operand.");
        }
      } else if (kind == 2) { // string
        if (src.type == ValueType::String) {
          push(src);
        } else if (src.type == ValueType::Int ||
                   src.type == ValueType::Double) {
          std::ostringstream oss;
          oss << src;
          push(Value::string_value(oss.str()));
        } else if (src.type == ValueType::Char) {
          push(Value::string_value(
              std::string(1, static_cast<char>(src.as_int))));
        } else {
          return runtime_error(
              "Cast to string requires int, float, char, or string operand.");
        }
      } else if (kind == 3) { // char
        if (src.type == ValueType::Char) {
          push(src);
        } else if (src.type == ValueType::Int) {
          push(Value::char_value(
              static_cast<int8_t>(src.as_int & 0xFF)));
        } else if (src.type == ValueType::String) {
          const std::string &s = src.string_val();
          if (s.empty()) {
            push(cast_err_empty());
          } else {
            push(Value::char_value(
                static_cast<int8_t>(
                    static_cast<unsigned char>(s[0]))));
          }
        } else {
          return runtime_error(
              "Cast to char requires int, char, or string operand.");
        }
      } else {
        return runtime_error("Unknown CastTo target kind.");
      }
      break;
    }
    case OpCode::FloatToBits: {
      const Value src = pop();
      if (src.type != ValueType::Double) {
        return runtime_error("FloatToBits requires a float operand.");
      }
      int64_t bits;
      std::memcpy(&bits, &src.as_double_storage, sizeof(bits));
      push(Value::int_value(bits));
      break;
    }
    case OpCode::Call: {
      const uint32_t arg_count =
          static_cast<uint32_t>(instruction.operand);
      if (stack_.size() < arg_count + 1)
        return runtime_error("Stack underflow for function call.");
      Value callee = pop();
      if (callee.type == ValueType::NativeFunction) {
        std::vector<Value> args(arg_count);
        for (uint32_t i = 0; i < arg_count; ++i) {
          args[arg_count - 1 - i] = pop();
        }
        switch (callee.native_fn) {
        case NativeFn::IoOut:
        case NativeFn::IoOutLine:
          if (!args.empty() && args[0].type == ValueType::String) {
            const std::string &fmt = args[0].string_val();
            std::size_t val_idx = 1;
            for (std::size_t pos = 0; pos < fmt.size(); ++pos) {
              if (pos + 1 < fmt.size() && fmt[pos] == '{' &&
                  fmt[pos + 1] == '}') {
                if (val_idx < args.size()) {
                  std::cout << args[val_idx++];
                } else {
                  std::cout << "{}";
                }
                ++pos;
              } else {
                std::cout << fmt[pos];
              }
            }
          } else {
            for (const Value &arg : args) {
              std::cout << arg;
            }
          }
          if (callee.native_fn == NativeFn::IoOutLine) std::cout << '\n';
          std::cout << std::flush;
          push(Value::null_value());
          break;
        case NativeFn::IoErr:
        case NativeFn::IoErrLine:
          if (!args.empty() && args[0].type == ValueType::String) {
            const std::string &fmt = args[0].string_val();
            std::size_t val_idx = 1;
            for (std::size_t pos = 0; pos < fmt.size(); ++pos) {
              if (pos + 1 < fmt.size() && fmt[pos] == '{' &&
                  fmt[pos + 1] == '}') {
                if (val_idx < args.size()) {
                  std::cerr << args[val_idx++];
                } else {
                  std::cerr << "{}";
                }
                ++pos;
              } else {
                std::cerr << fmt[pos];
              }
            }
          } else {
            for (const Value &arg : args) {
              std::cerr << arg;
            }
          }
          if (callee.native_fn == NativeFn::IoErrLine) std::cerr << '\n';
          std::cerr << std::flush;
          push(Value::null_value());
          break;
        case NativeFn::IoIn:
        case NativeFn::IoInSecret:
          for (const Value &arg : args) {
            if (arg.type == ValueType::String) {
              std::cout << arg.string_val() << std::flush;
            }
          }
          if (callee.native_fn == NativeFn::IoInSecret) disable_echo();
          {
            std::string line;
            if (!std::getline(std::cin, line)) {
              push(Value::null_value());
            } else {
              push(Value::string_value(std::move(line)));
            }
          }
          if (callee.native_fn == NativeFn::IoInSecret) {
            restore_echo();
            std::cout << '\n';
          }
          break;
        case NativeFn::FsRead: {
          if (args.size() != 1 || args[0].type != ValueType::String) {
            push(Value::null_value());
            break;
          }
          std::ifstream file(args[0].string_val(), std::ios::binary);
          if (!file) {
            push(Value::null_value());
            break;
          }
          std::ostringstream buffer;
          buffer << file.rdbuf();
          if (file.bad()) {
            push(Value::null_value());
            break;
          }
          push(Value::string_value(buffer.str()));
          break;
        }
        case NativeFn::FsWrite: {
          if (args.size() == 2 && args[0].type == ValueType::String &&
              args[1].type == ValueType::String) {
            std::ofstream file(args[0].string_val(),
                               std::ios::binary | std::ios::trunc);
            if (file) {
              file.write(args[1].string_val().data(),
                         static_cast<std::streamsize>(
                             args[1].string_val().size()));
            }
          }
          push(Value::null_value());
          break;
        }
        case NativeFn::SysArgs: {
          std::vector<Value> elements;
          elements.reserve(program_args_.size());
          for (const std::string &arg : program_args_) {
            elements.push_back(Value::string_value(arg));
          }
          push(Value::array_value(std::move(elements)));
          break;
        }
        }
        break;
      }
      if (callee.type != ValueType::Function) {
        return runtime_error("Attempted to call a non-function value.");
      }
      int func_idx = callee.function_idx;
      const auto &functions = frame.chunk->functions();
      if (func_idx < 0 ||
          static_cast<std::size_t>(func_idx) >= functions.size()) {
        return runtime_error("Invalid function index.");
      }
      const FunctionInfo &info =
          functions[static_cast<std::size_t>(func_idx)];
      if (static_cast<int>(arg_count) != info.param_count) {
        return runtime_error("Expected " + std::to_string(info.param_count) +
                             " arguments but got " +
                             std::to_string(arg_count) + ".");
      }
      CallFrame new_frame;
      new_frame.chunk = frame.chunk;
      new_frame.ip = info.entry;
      new_frame.locals.resize(arg_count);
      for (uint32_t i = 0; i < arg_count; ++i) {
        new_frame.locals[arg_count - 1 - i] = pop();
      }
      frames_.push_back(std::move(new_frame));
      break;
    }
    case OpCode::Return: {
      Value result = stack_.empty() ? Value::null_value() : pop();
      frames_.pop_back();
      if (frames_.empty()) {
        return VmResult{.ok = true, .value = result, .error = ""};
      }
      push(result);
      break;
    }
    case OpCode::Jmp:
      frame.ip += static_cast<std::size_t>(instruction.operand);
      break;
    case OpCode::JmpIfErr: {
      if (stack_.empty()) return runtime_error("Stack underflow.");
      const Value &top = stack_.back();
      bool is_err = top.type == ValueType::Null ||
                    (top.type == ValueType::Enum && top.enum_type_idx == 0);
      if (is_err) frame.ip += static_cast<std::size_t>(instruction.operand);
      break;
    }
    case OpCode::JmpFalse: {
      if (stack_.empty()) return runtime_error("Stack underflow.");
      Value condition = pop();
      bool truthy = false;
      switch (condition.type) {
      case ValueType::Bool:
        truthy = condition.as_bool;
        break;
      case ValueType::Null:
        truthy = false;
        break;
      case ValueType::Int:
        truthy = condition.as_int != 0;
        break;
      case ValueType::Double:
        truthy = condition.as_double_storage != 0.0;
        break;
      case ValueType::String:
        truthy = !condition.string_val().empty();
        break;
      case ValueType::Char:
      case ValueType::Function:
      case ValueType::Struct:
      case ValueType::Enum:
      case ValueType::Array:
      case ValueType::Map:
      case ValueType::NativeFunction:
        truthy = true;
        break;
      }
      if (!truthy) frame.ip += static_cast<std::size_t>(instruction.operand);
      break;
    }
    case OpCode::Eq:
    case OpCode::Neq:
    case OpCode::Lt:
    case OpCode::Gt:
    case OpCode::Le:
    case OpCode::Ge: {
      if (stack_.size() < 2) return runtime_error("Stack underflow.");
      Value right = pop();
      Value left = pop();
      bool result = false;
      if (instruction.op == OpCode::Eq) {
        if (left.type != right.type) {
          result = false;
        } else if (left.type == ValueType::Enum) {
          result = left.enum_type_idx == right.enum_type_idx &&
                   left.enum_variant_idx == right.enum_variant_idx;
        } else if (left.type == ValueType::String) {
          result = left.string_val() == right.string_val();
        } else if (left.type == ValueType::Array) {
          result = left.heap.ptr == right.heap.ptr;
        } else {
          result = left.as_int == right.as_int &&
                   left.as_double_storage == right.as_double_storage &&
                   left.as_bool == right.as_bool;
        }
      } else if (instruction.op == OpCode::Neq) {
        if (left.type != right.type) {
          result = true;
        } else if (left.type == ValueType::Enum) {
          result = left.enum_type_idx != right.enum_type_idx ||
                   left.enum_variant_idx != right.enum_variant_idx;
        } else if (left.type == ValueType::String) {
          result = left.string_val() != right.string_val();
        } else if (left.type == ValueType::Array) {
          result = left.heap.ptr != right.heap.ptr;
        } else {
          result = !(left.as_int == right.as_int &&
                     left.as_double_storage == right.as_double_storage &&
                     left.as_bool == right.as_bool);
        }
      } else {
        if (left.type == ValueType::String &&
            right.type == ValueType::String) {
          switch (instruction.op) {
          case OpCode::Lt:
            result = left.string_val() < right.string_val();
            break;
          case OpCode::Gt:
            result = left.string_val() > right.string_val();
            break;
          case OpCode::Le:
            result = left.string_val() <= right.string_val();
            break;
          case OpCode::Ge:
            result = left.string_val() >= right.string_val();
            break;
          default:
            break;
          }
        } else if (!left.is_number() || !right.is_number()) {
          return runtime_error("Comparison operands must be numeric.");
        } else {
          double lhs = left.as_double();
          double rhs = right.as_double();
          switch (instruction.op) {
          case OpCode::Lt:
            result = lhs < rhs;
            break;
          case OpCode::Gt:
            result = lhs > rhs;
            break;
          case OpCode::Le:
            result = lhs <= rhs;
            break;
          case OpCode::Ge:
            result = lhs >= rhs;
            break;
          default:
            break;
          }
        }
      }
      push(Value::bool_value(result));
      break;
    }
    case OpCode::NativeOut:
    case OpCode::NativeOutLn: {
      const uint32_t arg_count =
          static_cast<uint32_t>(instruction.operand);
      if (stack_.size() < arg_count)
        return runtime_error("Stack underflow for io::out.");
      std::vector<Value> args(arg_count);
      for (uint32_t i = 0; i < arg_count; ++i) {
        args[arg_count - 1 - i] = pop();
      }
      if (!args.empty() && args[0].type == ValueType::String) {
        const std::string &fmt = args[0].string_val();
        std::size_t val_idx = 1;
        for (std::size_t pos = 0; pos < fmt.size(); ++pos) {
          if (pos + 1 < fmt.size() && fmt[pos] == '{' &&
              fmt[pos + 1] == '}') {
            if (val_idx < args.size()) {
              std::cout << args[val_idx];
              ++val_idx;
            } else {
              std::cout << "{}";
            }
            ++pos;
          } else {
            std::cout << fmt[pos];
          }
        }
      } else {
        for (const Value &arg : args) {
          std::cout << arg;
        }
      }
      if (instruction.op == OpCode::NativeOutLn) std::cout << '\n';
      std::cout << std::flush;
      push(Value::null_value());
      break;
    }
    case OpCode::NativeErr:
    case OpCode::NativeErrLn: {
      const uint32_t arg_count =
          static_cast<uint32_t>(instruction.operand);
      if (stack_.size() < arg_count)
        return runtime_error("Stack underflow for io::err.");
      std::vector<Value> args(arg_count);
      for (uint32_t i = 0; i < arg_count; ++i) {
        args[arg_count - 1 - i] = pop();
      }
      if (!args.empty() && args[0].type == ValueType::String) {
        const std::string &fmt = args[0].string_val();
        std::size_t val_idx = 1;
        for (std::size_t pos = 0; pos < fmt.size(); ++pos) {
          if (pos + 1 < fmt.size() && fmt[pos] == '{' &&
              fmt[pos + 1] == '}') {
            if (val_idx < args.size()) {
              std::cerr << args[val_idx];
              ++val_idx;
            } else {
              std::cerr << "{}";
            }
            ++pos;
          } else {
            std::cerr << fmt[pos];
          }
        }
      } else {
        for (const Value &arg : args) {
          std::cerr << arg;
        }
      }
      if (instruction.op == OpCode::NativeErrLn) std::cerr << '\n';
      std::cerr << std::flush;
      push(Value::null_value());
      break;
    }
    case OpCode::NativeIn:
    case OpCode::NativeInSecret: {
      const uint32_t argc = static_cast<uint32_t>(instruction.operand);
      for (uint32_t i = 0; i < argc; ++i) {
        if (stack_.empty())
          return runtime_error("Stack underflow for io::in.");
        Value prompt = pop();
        if (prompt.type == ValueType::String)
          std::cout << prompt.string_val() << std::flush;
      }
      if (instruction.op == OpCode::NativeInSecret) disable_echo();
      std::string line;
      if (!std::getline(std::cin, line)) {
        push(Value::null_value());
      } else {
        push(Value::string_value(std::move(line)));
      }
      if (instruction.op == OpCode::NativeInSecret) {
        restore_echo();
        std::cout << '\n';
      }
      break;
    }
    case OpCode::NativeFsRead: {
      const uint32_t arg_count =
          static_cast<uint32_t>(instruction.operand);
      if (arg_count != 1)
        return runtime_error("fs::__read expects exactly one argument.");
      if (stack_.empty())
        return runtime_error("Stack underflow for fs::__read.");
      Value path = pop();
      if (path.type != ValueType::String)
        return runtime_error("fs::__read expects a string path.");
      std::ifstream file(path.string_val(), std::ios::binary);
      if (!file) {
        push(Value::null_value());
        break;
      }
      std::ostringstream buffer;
      buffer << file.rdbuf();
      if (file.bad()) {
        push(Value::null_value());
        break;
      }
      push(Value::string_value(buffer.str()));
      break;
    }
    case OpCode::NativeFsWrite: {
      const uint32_t arg_count =
          static_cast<uint32_t>(instruction.operand);
      if (arg_count != 2)
        return runtime_error("fs::__write expects exactly two arguments.");
      if (stack_.size() < 2)
        return runtime_error("Stack underflow for fs::__write.");
      Value content = pop();
      Value path = pop();
      if (path.type != ValueType::String)
        return runtime_error("fs::__write expects a string path.");
      if (content.type != ValueType::String)
        return runtime_error("fs::__write expects string content.");
      std::ofstream file(path.string_val(),
                         std::ios::binary | std::ios::trunc);
      if (file) {
        file.write(content.string_val().data(),
                   static_cast<std::streamsize>(
                       content.string_val().size()));
      }
      push(Value::null_value());
      break;
    }
    case OpCode::NativeSysArgs: {
      const uint32_t arg_count =
          static_cast<uint32_t>(instruction.operand);
      if (arg_count != 0)
        return runtime_error("sys::args expects no arguments.");
      std::vector<Value> elements;
      elements.reserve(program_args_.size());
      for (const std::string &arg : program_args_) {
        elements.push_back(Value::string_value(arg));
      }
      push(Value::array_value(std::move(elements)));
      break;
    }
    case OpCode::StructNew: {
      int type_idx = instruction.operand >> 16;
      int field_count = instruction.operand & 0xFFFF;
      if (static_cast<int>(stack_.size()) < field_count)
        return runtime_error("Stack underflow for struct creation.");
      std::vector<Value> fields(static_cast<std::size_t>(field_count));
      for (int i = field_count - 1; i >= 0; --i) {
        fields[static_cast<std::size_t>(i)] = pop();
      }
      push(Value::struct_value(type_idx, std::move(fields)));
      break;
    }
    case OpCode::FieldGet: {
      if (stack_.empty())
        return runtime_error("Stack underflow for field access.");
      Value obj = pop();
      if (obj.type != ValueType::Struct || !obj.heap)
        return runtime_error("Cannot access field on non-struct value.");
      const std::string &field_name =
          constants[static_cast<std::size_t>(instruction.operand)].string_val();
      int type_idx = as_struct(obj)->type_index;
      const auto &meta =
          frame.chunk->struct_metas()[static_cast<std::size_t>(type_idx)];
      int field_idx = -1;
      for (int i = 0; i < static_cast<int>(meta.field_names.size()); ++i) {
        if (meta.field_names[static_cast<std::size_t>(i)] == field_name) {
          field_idx = i;
          break;
        }
      }
      if (field_idx < 0 ||
          static_cast<std::size_t>(field_idx) >=
              as_struct(obj)->fields.size()) {
        return runtime_error("Unknown field '" + field_name + "'.");
      }
      push(as_struct(obj)->fields[static_cast<std::size_t>(field_idx)]);
      break;
    }
    case OpCode::FieldSet: {
      if (stack_.size() < 2)
        return runtime_error("Stack underflow for field assignment.");
      Value value = pop();
      Value obj = pop();
      if (obj.type != ValueType::Struct || !obj.heap)
        return runtime_error("Cannot set field on non-struct value.");
      const std::string &field_name =
          constants[static_cast<std::size_t>(instruction.operand)].string_val();
      int type_idx = as_struct(obj)->type_index;
      const auto &meta =
          frame.chunk->struct_metas()[static_cast<std::size_t>(type_idx)];
      int field_idx = -1;
      for (int i = 0; i < static_cast<int>(meta.field_names.size()); ++i) {
        if (meta.field_names[static_cast<std::size_t>(i)] == field_name) {
          field_idx = i;
          break;
        }
      }
      if (field_idx < 0 ||
          static_cast<std::size_t>(field_idx) >=
              as_struct(obj)->fields.size()) {
        return runtime_error("Unknown field '" + field_name + "'.");
      }
      as_struct(obj)->fields[static_cast<std::size_t>(field_idx)] = value;
      push(obj);
      break;
    }
    case OpCode::EnumVariant: {
      int type_idx = instruction.operand >> 16;
      int variant_idx = instruction.operand & 0xFFFF;
      push(Value::enum_value(type_idx, variant_idx));
      break;
    }
    case OpCode::EnumVariantPayload: {
      int type_idx = instruction.operand >> 16;
      int variant_idx = instruction.operand & 0xFFFF;
      const auto &meta =
          chunk.enum_metas()[static_cast<std::size_t>(type_idx)];
      int param_count =
          meta.variant_param_counts[static_cast<std::size_t>(variant_idx)];
      std::vector<Value> payload(static_cast<std::size_t>(param_count));
      for (int i = param_count - 1; i >= 0; --i) {
        payload[static_cast<std::size_t>(i)] = pop();
      }
      push(Value::enum_value_with_payload(type_idx, variant_idx,
                                           std::move(payload)));
      break;
    }
    case OpCode::EnumPayloadGet: {
      if (stack_.size() < 1)
        return runtime_error("Stack underflow for enum payload get.");
      Value enum_val = pop();
      if (enum_val.type != ValueType::Enum)
        return runtime_error("EnumPayloadGet: value is not an enum.");
      if (!enum_val.heap)
        return runtime_error(
            "EnumPayloadGet: enum variant has no payload.");
      int payload_idx = instruction.operand;
      auto &payload = as_enum(enum_val)->payload;
      if (payload_idx < 0 ||
          static_cast<std::size_t>(payload_idx) >= payload.size()) {
        return runtime_error("EnumPayloadGet: payload index out of bounds.");
      }
      push(payload[static_cast<std::size_t>(payload_idx)]);
      break;
    }
    case OpCode::ArrayNew: {
      const uint32_t element_count =
          static_cast<uint32_t>(instruction.operand);
      if (stack_.size() < element_count)
        return runtime_error("Stack underflow for array creation.");
      std::vector<Value> elements(element_count);
      for (uint32_t i = 0; i < element_count; ++i) {
        elements[element_count - 1 - i] = pop();
      }
      push(Value::array_value(std::move(elements)));
      break;
    }
    case OpCode::IndexGet: {
      if (stack_.size() < 2)
        return runtime_error("Stack underflow for array indexing.");
      Value index = pop();
      Value object = pop();
      if (object.type == ValueType::Map) {
        if (!object.heap)
          return runtime_error("Cannot index null map.");
        auto it = as_map(object)->entries.find(encode_map_key(index));
        if (it == as_map(object)->entries.end()) {
          push(Value::null_value());
        } else {
          push(it->second.value);
        }
        break;
      }
      if (object.type == ValueType::String) {
        if (index.type != ValueType::Int)
          return runtime_error("String index must be an integer.");
        if (index.as_int < 0 ||
            static_cast<std::size_t>(index.as_int) >=
                object.string_val().size()) {
          return runtime_error("String index out of bounds.");
        }
        push(Value::char_value(static_cast<int8_t>(
            static_cast<unsigned char>(
                object.string_val()[
                    static_cast<std::size_t>(index.as_int)]))));
        break;
      }
      if (object.type != ValueType::Array || !object.heap)
        return runtime_error("Cannot index non-array value.");
      if (index.type != ValueType::Int)
        return runtime_error("Array index must be an integer.");
      if (index.as_int < 0 ||
          static_cast<std::size_t>(index.as_int) >=
              as_array(object)->elements.size()) {
        return runtime_error("Array index out of bounds.");
      }
      push(as_array(object)->elements[static_cast<std::size_t>(index.as_int)]);
      break;
    }
    case OpCode::IndexSet: {
      if (stack_.size() < 3)
        return runtime_error("Stack underflow for array assignment.");
      Value value = pop();
      Value index = pop();
      Value array = pop();
      if (array.type == ValueType::Map) {
        if (!array.heap)
          return runtime_error("Cannot assign on null map.");
        std::string ek = encode_map_key(index);
        if (as_map(array)->entries.find(ek) ==
            as_map(array)->entries.end()) {
          as_map(array)->order.push_back(ek);
        }
        as_map(array)->entries[ek] = MapEntry{index, value};
        push(value);
        break;
      }
      if (array.type != ValueType::Array || !array.heap)
        return runtime_error("Cannot assign indexed value on non-array value.");
      if (index.type != ValueType::Int)
        return runtime_error("Array index must be an integer.");
      if (index.as_int < 0 ||
          static_cast<std::size_t>(index.as_int) >=
              as_array(array)->elements.size()) {
        return runtime_error("Array index out of bounds.");
      }
      as_array(array)->elements[static_cast<std::size_t>(index.as_int)] =
          std::move(value);
      push(value);
      break;
    }
    case OpCode::ArrayLen: {
      Value obj = pop();
      if (obj.type == ValueType::String) {
        push(Value::int_value(
            static_cast<int64_t>(obj.string_val().size())));
        break;
      }
      if (obj.type == ValueType::Map && obj.heap) {
        push(Value::int_value(
            static_cast<int64_t>(as_map(obj)->order.size())));
        break;
      }
      if (obj.type != ValueType::Array || !obj.heap)
        return runtime_error("Cannot call len() on non-array value.");
      push(Value::int_value(
          static_cast<int64_t>(as_array(obj)->elements.size())));
      break;
    }
    case OpCode::ArrayPush: {
      Value value = pop();
      Value array = pop();
      if (array.type != ValueType::Array || !array.heap)
        return runtime_error("Cannot call push() on non-array value.");
      as_array(array)->elements.push_back(std::move(value));
      push(Value::null_value());
      break;
    }
    case OpCode::ArrayResize: {
      Value default_value = pop();
      Value count = pop();
      Value array = pop();
      if (array.type != ValueType::Array || !array.heap)
        return runtime_error("Cannot call resize() on non-array value.");
      if (count.type != ValueType::Int)
        return runtime_error("resize() count must be an integer.");
      if (count.as_int < 0)
        return runtime_error("resize() count must be non-negative.");
      as_array(array)->elements.resize(
          static_cast<std::size_t>(count.as_int), default_value);
      push(Value::null_value());
      break;
    }
    case OpCode::ArrayPop: {
      Value array = pop();
      if (array.type != ValueType::Array || !array.heap)
        return runtime_error("Cannot call pop() on non-array value.");
      if (as_array(array)->elements.empty())
        return runtime_error("Cannot pop from empty array.");
      Value last = as_array(array)->elements.back();
      as_array(array)->elements.pop_back();
      push(last);
      break;
    }
    case OpCode::ArrayRemove: {
      Value index = pop();
      Value array = pop();
      if (array.type == ValueType::Map) {
        if (!array.heap)
          return runtime_error("Cannot call remove() on null map.");
        std::string ek = encode_map_key(index);
        auto it = as_map(array)->entries.find(ek);
        if (it != as_map(array)->entries.end()) {
          as_map(array)->entries.erase(it);
          auto &order = as_map(array)->order;
          order.erase(
              std::remove(order.begin(), order.end(), ek), order.end());
        }
        push(Value::null_value());
        break;
      }
      if (array.type != ValueType::Array || !array.heap)
        return runtime_error("Cannot call remove() on non-array value.");
      if (index.type != ValueType::Int)
        return runtime_error("remove() index must be an integer.");
      auto idx = index.as_int;
      if (idx < 0 ||
          static_cast<std::size_t>(idx) >=
              as_array(array)->elements.size()) {
        return runtime_error("remove() index out of bounds.");
      }
      Value removed =
          as_array(array)->elements[static_cast<std::size_t>(idx)];
      as_array(array)->elements.erase(
          as_array(array)->elements.begin() + idx);
      push(removed);
      break;
    }
    case OpCode::ArrayContains: {
      Value needle = pop();
      Value obj = pop();
      if (obj.type == ValueType::String) {
        if (needle.type != ValueType::String)
          return runtime_error("contains() argument must be a string.");
        push(Value::bool_value(
            obj.string_val().find(needle.string_val()) !=
            std::string::npos));
        break;
      }
      if (obj.type != ValueType::Array || !obj.heap)
        return runtime_error("Cannot call contains() on non-array value.");
      bool found = false;
      for (const auto &elem : as_array(obj)->elements) {
        if (elem.type == needle.type) {
          if (elem.type == ValueType::Int &&
              elem.as_int == needle.as_int) {
            found = true;
            break;
          }
          if (elem.type == ValueType::String &&
              elem.string_val() == needle.string_val()) {
            found = true;
            break;
          }
          if (elem.type == ValueType::Bool &&
              elem.as_bool == needle.as_bool) {
            found = true;
            break;
          }
          if (elem.type == ValueType::Double &&
              elem.as_double_storage == needle.as_double_storage) {
            found = true;
            break;
          }
        }
      }
      push(Value::bool_value(found));
      break;
    }
    case OpCode::ArrayClear: {
      Value array = pop();
      if (array.type != ValueType::Array || !array.heap)
        return runtime_error("Cannot call clear() on non-array value.");
      as_array(array)->elements.clear();
      push(Value::null_value());
      break;
    }
    case OpCode::ArrayInsert: {
      Value value = pop();
      Value index = pop();
      Value array = pop();
      if (array.type != ValueType::Array || !array.heap)
        return runtime_error("Cannot call insert() on non-array value.");
      if (index.type != ValueType::Int)
        return runtime_error("insert() index must be an integer.");
      auto idx = index.as_int;
      auto &elems = as_array(array)->elements;
      if (idx < 0 || static_cast<std::size_t>(idx) > elems.size())
        return runtime_error("insert() index out of bounds.");
      if (value.type == ValueType::Array && value.heap) {
        elems.insert(elems.begin() + idx,
                     as_array(value)->elements.begin(),
                     as_array(value)->elements.end());
      } else {
        elems.insert(elems.begin() + idx, std::move(value));
      }
      push(Value::null_value());
      break;
    }
    case OpCode::ArrayIndexOf: {
      Value needle = pop();
      Value obj = pop();
      if (obj.type == ValueType::String) {
        if (needle.type != ValueType::String)
          return runtime_error("index_of() argument must be a string.");
        auto pos = obj.string_val().find(needle.string_val());
        push(Value::int_value(pos == std::string::npos
                                  ? -1
                                  : static_cast<int64_t>(pos)));
        break;
      }
      if (obj.type != ValueType::Array || !obj.heap)
        return runtime_error("Cannot call index_of() on non-array value.");
      int64_t found = -1;
      for (std::size_t i = 0; i < as_array(obj)->elements.size(); ++i) {
        const auto &elem = as_array(obj)->elements[i];
        if (elem.type == needle.type) {
          bool match = false;
          if (elem.type == ValueType::Int &&
              elem.as_int == needle.as_int)
            match = true;
          if (elem.type == ValueType::String &&
              elem.string_val() == needle.string_val())
            match = true;
          if (elem.type == ValueType::Bool &&
              elem.as_bool == needle.as_bool)
            match = true;
          if (elem.type == ValueType::Double &&
              elem.as_double_storage == needle.as_double_storage)
            match = true;
          if (match) {
            found = static_cast<int64_t>(i);
            break;
          }
        }
      }
      push(Value::int_value(found));
      break;
    }
    case OpCode::ArraySlice: {
      Value end_val = pop();
      Value start_val = pop();
      Value obj = pop();
      if (start_val.type != ValueType::Int ||
          end_val.type != ValueType::Int)
        return runtime_error("slice() arguments must be integers.");
      auto start = start_val.as_int;
      auto end = end_val.as_int;
      if (obj.type == ValueType::String) {
        auto len = static_cast<int64_t>(obj.string_val().size());
        if (start < 0) start = 0;
        if (end > len) end = len;
        if (start >= end) {
          push(Value::string_value(""));
          break;
        }
        push(Value::string_value(obj.string_val().substr(
            static_cast<std::size_t>(start),
            static_cast<std::size_t>(end - start))));
        break;
      }
      if (obj.type != ValueType::Array || !obj.heap)
        return runtime_error("Cannot call slice() on non-array value.");
      auto &elems = as_array(obj)->elements;
      if (start < 0) start = 0;
      if (end > static_cast<int64_t>(elems.size()))
        end = static_cast<int64_t>(elems.size());
      if (start >= end) {
        push(Value::array_value({}));
        break;
      }
      std::vector<Value> slice_elems(elems.begin() + start,
                                      elems.begin() + end);
      push(Value::array_value(std::move(slice_elems)));
      break;
    }
    case OpCode::ArrayReverse: {
      Value array = pop();
      if (array.type != ValueType::Array || !array.heap)
        return runtime_error("Cannot call reverse() on non-array value.");
      std::reverse(as_array(array)->elements.begin(),
                   as_array(array)->elements.end());
      push(Value::null_value());
      break;
    }
    case OpCode::MapNew: {
      const uint32_t pair_count =
          static_cast<uint32_t>(instruction.operand);
      if (stack_.size() < pair_count * 2)
        return runtime_error("Stack underflow for map literal.");
      Value map = Value::map_value();
      std::vector<std::pair<Value, Value>> pairs(pair_count);
      for (uint32_t i = 0; i < pair_count; ++i) {
        Value v = pop();
        Value k = pop();
        pairs[pair_count - 1 - i] = {std::move(k), std::move(v)};
      }
      for (auto &[k, v] : pairs) {
        std::string ek = encode_map_key(k);
        if (as_map(map)->entries.find(ek) ==
            as_map(map)->entries.end()) {
          as_map(map)->order.push_back(ek);
        }
        as_map(map)->entries[ek] = MapEntry{std::move(k), std::move(v)};
      }
      push(std::move(map));
      break;
    }
    case OpCode::MapGet: {
      Value key = pop();
      Value map = pop();
      if (map.type != ValueType::Map || !map.heap)
        return runtime_error("Cannot index non-map value.");
      auto it =
          as_map(map)->entries.find(encode_map_key(key));
      if (it == as_map(map)->entries.end()) {
        push(Value::null_value());
      } else {
        push(it->second.value);
      }
      break;
    }
    case OpCode::MapSet: {
      Value value = pop();
      Value key = pop();
      Value map = pop();
      if (map.type != ValueType::Map || !map.heap)
        return runtime_error("Cannot assign on non-map value.");
      std::string ek = encode_map_key(key);
      if (as_map(map)->entries.find(ek) ==
          as_map(map)->entries.end()) {
        as_map(map)->order.push_back(ek);
      }
      as_map(map)->entries[ek] = MapEntry{std::move(key), value};
      push(Value::null_value());
      break;
    }
    case OpCode::MapHas: {
      Value key = pop();
      Value map = pop();
      if (map.type != ValueType::Map || !map.heap)
        return runtime_error("Cannot call has() on non-map value.");
      bool found =
          as_map(map)->entries.count(encode_map_key(key)) != 0;
      push(Value::bool_value(found));
      break;
    }
    case OpCode::MapRemove: {
      Value key = pop();
      Value map = pop();
      if (map.type != ValueType::Map || !map.heap)
        return runtime_error("Cannot call remove() on non-map value.");
      std::string ek = encode_map_key(key);
      auto it = as_map(map)->entries.find(ek);
      if (it != as_map(map)->entries.end()) {
        as_map(map)->entries.erase(it);
        auto &order = as_map(map)->order;
        order.erase(
            std::remove(order.begin(), order.end(), ek), order.end());
      }
      push(Value::null_value());
      break;
    }
    case OpCode::MapKeys: {
      Value map = pop();
      if (map.type != ValueType::Map || !map.heap)
        return runtime_error("Cannot call keys() on non-map value.");
      std::vector<Value> keys;
      keys.reserve(as_map(map)->order.size());
      for (const std::string &ek : as_map(map)->order) {
        keys.push_back(as_map(map)->entries.at(ek).key);
      }
      push(Value::array_value(std::move(keys)));
      break;
    }
    case OpCode::MapLen: {
      Value map = pop();
      if (map.type != ValueType::Map || !map.heap)
        return runtime_error("Cannot call len() on non-map value.");
      push(Value::int_value(
          static_cast<int64_t>(as_map(map)->order.size())));
      break;
    }
    case OpCode::PushHandler: {
      std::size_t catch_pc =
          frame.ip + static_cast<std::size_t>(instruction.operand);
      handler_stack_.push_back(catch_pc);
      break;
    }
    case OpCode::PopHandler: {
      if (handler_stack_.empty())
        return runtime_error("PopHandler with empty handler stack.");
      handler_stack_.pop_back();
      break;
    }
    case OpCode::PropagateErr: {
      if (stack_.empty()) return runtime_error("Stack underflow.");
      const Value &top = stack_.back();
      bool is_err = top.type == ValueType::Null ||
                    (top.type == ValueType::Enum && top.enum_type_idx == 0);
      if (is_err) {
        if (handler_stack_.empty()) {
          pop();
          push(Value::null_value());
          return VmResult{
              .ok = true, .value = Value::null_value(), .error = ""};
        }
        frame.ip = handler_stack_.back();
      }
      break;
    }
    case OpCode::StringStartsWith: {
      Value prefix = pop();
      Value str = pop();
      if (str.type != ValueType::String ||
          prefix.type != ValueType::String)
        return runtime_error("starts_with() requires string arguments.");
      bool result = str.string_val().size() >=
                        prefix.string_val().size() &&
                    str.string_val().compare(
                        0, prefix.string_val().size(),
                        prefix.string_val()) == 0;
      push(Value::bool_value(result));
      break;
    }
    case OpCode::StringEndsWith: {
      Value suffix = pop();
      Value str = pop();
      if (str.type != ValueType::String ||
          suffix.type != ValueType::String)
        return runtime_error("ends_with() requires string arguments.");
      bool result = str.string_val().size() >=
                        suffix.string_val().size() &&
                    str.string_val().compare(
                        str.string_val().size() -
                            suffix.string_val().size(),
                        suffix.string_val().size(),
                        suffix.string_val()) == 0;
      push(Value::bool_value(result));
      break;
    }
    case OpCode::StringReplace: {
      Value new_str = pop();
      Value old_str = pop();
      Value str = pop();
      if (str.type != ValueType::String ||
          old_str.type != ValueType::String ||
          new_str.type != ValueType::String)
        return runtime_error("replace() requires string arguments.");
      std::string result = str.string_val();
      if (!old_str.string_val().empty()) {
        std::size_t pos = 0;
        while ((pos = result.find(old_str.string_val(), pos)) !=
               std::string::npos) {
          result.replace(pos, old_str.string_val().size(),
                         new_str.string_val());
          pos += new_str.string_val().size();
        }
      }
      push(Value::string_value(std::move(result)));
      break;
    }
    case OpCode::StringSplit: {
      Value delim = pop();
      Value str = pop();
      if (str.type != ValueType::String ||
          delim.type != ValueType::String)
        return runtime_error("split() requires string arguments.");
      std::vector<Value> parts;
      if (delim.string_val().empty()) {
        for (char c : str.string_val()) {
          parts.push_back(Value::string_value(std::string(1, c)));
        }
      } else {
        std::size_t start = 0;
        std::size_t pos;
        while ((pos = str.string_val().find(
                    delim.string_val(), start)) != std::string::npos) {
          parts.push_back(Value::string_value(
              str.string_val().substr(start, pos - start)));
          start = pos + delim.string_val().size();
        }
        parts.push_back(
            Value::string_value(str.string_val().substr(start)));
      }
      push(Value::array_value(std::move(parts)));
      break;
    }
    case OpCode::StringTrim: {
      Value str = pop();
      if (str.type != ValueType::String)
        return runtime_error("trim() requires a string.");
      auto &s = str.string_val();
      auto start = s.find_first_not_of(" \t\n\r");
      if (start == std::string::npos) {
        push(Value::string_value(""));
      } else {
        auto end = s.find_last_not_of(" \t\n\r");
        push(Value::string_value(s.substr(start, end - start + 1)));
      }
      break;
    }
    case OpCode::StringToUpper: {
      Value str = pop();
      if (str.type != ValueType::String)
        return runtime_error("to_upper() requires a string.");
      std::string result = str.string_val();
      for (auto &c : result)
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
      push(Value::string_value(std::move(result)));
      break;
    }
    case OpCode::StringToLower: {
      Value str = pop();
      if (str.type != ValueType::String)
        return runtime_error("to_lower() requires a string.");
      std::string result = str.string_val();
      for (auto &c : result)
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
      push(Value::string_value(std::move(result)));
      break;
    }
    }
  }

  return VmResult{.ok = true, .value = Value::null_value(), .error = ""};
}

bool Vm::push(Value value) {
  stack_.push_back(std::move(value));
  return true;
}

Value Vm::pop() {
  Value value = std::move(stack_.back());
  stack_.pop_back();
  return value;
}

VmResult Vm::runtime_error(std::string message) const {
  return VmResult{
      .ok = false, .value = Value::null_value(), .error = std::move(message)};
}

bool Vm::binary_numeric(OpCode op, std::string *error) {
  if (stack_.size() < 2) {
    *error = "Stack underflow.";
    return false;
  }

  Value right = pop();
  Value left = pop();
  if (!left.is_number() || !right.is_number()) {
    *error = "Operands must be numeric.";
    return false;
  }

  const bool both_int =
      left.type == ValueType::Int && right.type == ValueType::Int;
  if (both_int) {
    switch (op) {
    case OpCode::Add:
      push(Value::int_value(left.as_int + right.as_int));
      return true;
    case OpCode::Subtract:
      push(Value::int_value(left.as_int - right.as_int));
      return true;
    case OpCode::Multiply:
      push(Value::int_value(left.as_int * right.as_int));
      return true;
    case OpCode::Divide:
      if (right.as_int == 0) {
        *error = "Division by zero.";
        return false;
      }
      push(Value::int_value(left.as_int / right.as_int));
      return true;
    case OpCode::Modulo:
      if (right.as_int == 0) {
        *error = "Modulo by zero.";
        return false;
      }
      push(Value::int_value(left.as_int % right.as_int));
      return true;
    default:
      break;
    }
  }

  const double lhs = left.as_double();
  const double rhs = right.as_double();
  switch (op) {
  case OpCode::Add:
    push(Value::double_value(lhs + rhs));
    return true;
  case OpCode::Subtract:
    push(Value::double_value(lhs - rhs));
    return true;
  case OpCode::Multiply:
    push(Value::double_value(lhs * rhs));
    return true;
  case OpCode::Divide:
    push(Value::double_value(lhs / rhs));
    return true;
  case OpCode::Modulo:
    push(Value::double_value(std::fmod(lhs, rhs)));
    return true;
  default:
    *error = "Invalid numeric opcode.";
    return false;
  }
}

void Vm::disable_echo() {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  GetConsoleMode(h, &mode);
  SetConsoleMode(h, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
#else
  struct termios t;
  tcgetattr(STDIN_FILENO, &t);
  t.c_lflag &= ~static_cast<tcflag_t>(ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
#endif
}

void Vm::restore_echo() {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  GetConsoleMode(h, &mode);
  SetConsoleMode(h, mode | ENABLE_ECHO_INPUT);
#else
  struct termios t;
  tcgetattr(STDIN_FILENO, &t);
  t.c_lflag |= ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
#endif
}

} // namespace kinglet
