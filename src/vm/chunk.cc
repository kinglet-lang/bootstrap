#include "vm/chunk.h"

#include <cstring>
#include <fstream>
#include <ostream>
#include <string>
#include <utility>

namespace kinglet {

// Forward declaration — defined in the anonymous namespace below.
namespace { bool values_equal(const Value &a, const Value &b); }

uint32_t Chunk::add_constant(Value value) {
  // Deduplication: if an identical constant already exists, reuse its index.
  for (uint32_t i = 0; i < static_cast<uint32_t>(constants_.size()); ++i) {
    const auto &existing = constants_[i];
    if (existing.type != value.type) continue;
    switch (value.type) {
    case ValueType::Int:
      if (existing.as_int == value.as_int) return i;
      break;
    case ValueType::Double:
      if (existing.as_double_storage == value.as_double_storage) return i;
      break;
    case ValueType::Bool:
      if (existing.as_bool == value.as_bool) return i;
      break;
    case ValueType::Char:
      if (existing.as_int == value.as_int) return i;
      break;
    case ValueType::Null:
      return i;
    case ValueType::String:
      if (existing.string_val() == value.string_val()) return i;
      break;
    case ValueType::Function:
      if (existing.function_idx == value.function_idx) return i;
      break;
    case ValueType::Enum: {
      if (existing.enum_type_idx != value.enum_type_idx ||
          existing.enum_variant_idx != value.enum_variant_idx)
        break;
      if (!existing.heap && !value.heap) return i;
      if (!existing.heap || !value.heap) break;
      auto *ep = static_cast<HeapEnum *>(existing.heap.ptr);
      auto *vp = static_cast<HeapEnum *>(value.heap.ptr);
      if (ep->payload.size() != vp->payload.size()) break;
      bool match = true;
      for (std::size_t k = 0; k < ep->payload.size(); ++k) {
        if (!values_equal(ep->payload[k], vp->payload[k])) { match = false; break; }
      }
      if (match) return i;
      break;
    }
    default:
      break;
    }
  }
  constants_.push_back(value);
  return static_cast<uint32_t>(constants_.size() - 1);
}

void Chunk::write(OpCode op, int line, int column) {
  instructions_.push_back(Instruction{
      .op = op,
      .operand = 0,
      .line = line,
      .column = column,
  });
}

void Chunk::write_operand(OpCode op, uint32_t operand, int line, int column) {
  instructions_.push_back(Instruction{
      .op = op,
      .operand = static_cast<int32_t>(operand),
      .line = line,
      .column = column,
  });
}

void Chunk::write_constant(Value value, int line, int column) {
  const uint32_t index = add_constant(value);
  instructions_.push_back(Instruction{
      .op = OpCode::Constant,
      .operand = static_cast<int32_t>(index),
      .line = line,
      .column = column,
  });
}

const std::vector<Value> &Chunk::constants() const {
  return constants_;
}

const std::vector<Instruction> &Chunk::instructions() const {
  return instructions_;
}

int Chunk::add_function(FunctionInfo info) {
  int index = static_cast<int>(functions_.size());
  functions_.push_back(std::move(info));
  return index;
}

const std::vector<FunctionInfo> &Chunk::functions() const {
  return functions_;
}

int Chunk::add_struct_meta(StructMeta meta) {
  int index = static_cast<int>(struct_metas_.size());
  struct_metas_.push_back(std::move(meta));
  return index;
}

int Chunk::add_enum_meta(EnumMeta meta) {
  int index = static_cast<int>(enum_metas_.size());
  enum_metas_.push_back(std::move(meta));
  return index;
}

const std::vector<StructMeta> &Chunk::struct_metas() const {
  return struct_metas_;
}

const std::vector<EnumMeta> &Chunk::enum_metas() const {
  return enum_metas_;
}

void Chunk::disassemble(std::ostream &out) const {
  for (std::size_t i = 0; i < instructions_.size(); ++i) {
    const Instruction &instruction = instructions_[i];
    out << i << "  " << instruction.line << ':' << instruction.column << "  "
        << opcode_name(instruction.op);
    if (instruction.op == OpCode::Constant) {
      out << " #" << instruction.operand << " ("
          << constants_[static_cast<std::size_t>(instruction.operand)] << ")";
    } else if (instruction.op == OpCode::LoadLocal ||
               instruction.op == OpCode::StoreLocal) {
      out << " slot " << instruction.operand;
    } else if (instruction.op == OpCode::Call) {
      out << " args=" << instruction.operand;
    } else if (instruction.op == OpCode::Jmp ||
               instruction.op == OpCode::JmpFalse) {
      out << " +" << instruction.operand;
    } else if (instruction.op == OpCode::JmpIfErr) {
      out << " +" << instruction.operand;
    } else if (instruction.op == OpCode::PushHandler) {
      out << " catch_pc+" << instruction.operand;
    } else if (instruction.op == OpCode::NativeOut) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeOutLn) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeErr) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeErrLn) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeIn) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeInSecret) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeFsRead) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeFsWrite) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeSysArgs) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::ArrayNew) {
      out << " count=" << instruction.operand;
    } else if (instruction.op == OpCode::MapNew) {
      out << " pairs=" << instruction.operand;
    }
    out << '\n';
  }
}

void Chunk::patch_operand(std::size_t index, int32_t operand) {
  instructions_[index].operand = operand;
}

const char *opcode_name(OpCode op) {
  switch (op) {
  case OpCode::Constant:
    return "Constant";
  case OpCode::Null:
    return "Null";
  case OpCode::True:
    return "True";
  case OpCode::False:
    return "False";
  case OpCode::Add:
    return "Add";
  case OpCode::Subtract:
    return "Subtract";
  case OpCode::Multiply:
    return "Multiply";
  case OpCode::Divide:
    return "Divide";
  case OpCode::Modulo:
    return "Modulo";
  case OpCode::Negate:
    return "Negate";
  case OpCode::Not:
    return "Not";
  case OpCode::BitNot:
    return "BitNot";
  case OpCode::BitAnd:
    return "BitAnd";
  case OpCode::BitOr:
    return "BitOr";
  case OpCode::BitXor:
    return "BitXor";
  case OpCode::Shl:
    return "Shl";
  case OpCode::Shr:
    return "Shr";
  case OpCode::LoadLocal:
    return "LoadLocal";
  case OpCode::StoreLocal:
    return "StoreLocal";
  case OpCode::Pop:
    return "Pop";
  case OpCode::Dup:
    return "Dup";
  case OpCode::CastTo:
    return "CastTo";
  case OpCode::FloatToBits:
    return "FloatToBits";
  case OpCode::Call:
    return "Call";
  case OpCode::Return:
    return "Return";
  case OpCode::Jmp:
    return "Jmp";
  case OpCode::JmpFalse:
    return "JmpFalse";
  case OpCode::JmpIfErr:
    return "JmpIfErr";
  case OpCode::Eq:
    return "Eq";
  case OpCode::Neq:
    return "Neq";
  case OpCode::Lt:
    return "Lt";
  case OpCode::Gt:
    return "Gt";
  case OpCode::Le:
    return "Le";
  case OpCode::Ge:
    return "Ge";
  case OpCode::NativeOut:
    return "NativeOut";
  case OpCode::NativeOutLn:
    return "NativeOutLn";
  case OpCode::NativeErr:
    return "NativeErr";
  case OpCode::NativeErrLn:
    return "NativeErrLn";
  case OpCode::NativeIn:
    return "NativeIn";
  case OpCode::NativeInSecret:
    return "NativeInSecret";
  case OpCode::NativeFsRead:
    return "NativeFsRead";
  case OpCode::NativeFsWrite:
    return "NativeFsWrite";
  case OpCode::NativeSysArgs:
    return "NativeSysArgs";
  case OpCode::StructNew:
    return "StructNew";
  case OpCode::FieldGet:
    return "FieldGet";
  case OpCode::FieldSet:
    return "FieldSet";
  case OpCode::EnumVariant:
    return "EnumVariant";
  case OpCode::ArrayNew:
    return "ArrayNew";
  case OpCode::IndexGet:
    return "IndexGet";
  case OpCode::IndexSet:
    return "IndexSet";
  case OpCode::ArrayLen:
    return "ArrayLen";
  case OpCode::ArrayPush:
    return "ArrayPush";
  case OpCode::ArrayResize:
    return "ArrayResize";
  case OpCode::ArrayPop:
    return "ArrayPop";
  case OpCode::ArrayRemove:
    return "ArrayRemove";
  case OpCode::ArrayContains:
    return "ArrayContains";
  case OpCode::ArrayClear:
    return "ArrayClear";
  case OpCode::ArrayInsert:
    return "ArrayInsert";
  case OpCode::ArrayIndexOf:
    return "ArrayIndexOf";
  case OpCode::ArraySlice:
    return "ArraySlice";
  case OpCode::ArrayReverse:
    return "ArrayReverse";
  case OpCode::StringStartsWith:
    return "StringStartsWith";
  case OpCode::StringEndsWith:
    return "StringEndsWith";
  case OpCode::StringReplace:
    return "StringReplace";
  case OpCode::StringSplit:
    return "StringSplit";
  case OpCode::StringTrim:
    return "StringTrim";
  case OpCode::StringToUpper:
    return "StringToUpper";
  case OpCode::StringToLower:
    return "StringToLower";
  case OpCode::EnumVariantPayload:
    return "EnumVariantPayload";
  case OpCode::EnumPayloadGet:
    return "EnumPayloadGet";
  case OpCode::MapNew:
    return "MapNew";
  case OpCode::MapGet:
    return "MapGet";
  case OpCode::MapSet:
    return "MapSet";
  case OpCode::MapHas:
    return "MapHas";
  case OpCode::MapRemove:
    return "MapRemove";
  case OpCode::MapKeys:
    return "MapKeys";
  case OpCode::MapLen:
    return "MapLen";
  case OpCode::PushHandler:
    return "PushHandler";
  case OpCode::PopHandler:
    return "PopHandler";
  case OpCode::PropagateErr:
    return "PropagateErr";
  case OpCode::IsNull:
    return "IsNull";
  }
  return "Unknown";
}

// ---------------------------------------------------------------------------
// Bytecode serialization helpers
// ---------------------------------------------------------------------------

namespace {

// .kbc format constants
constexpr uint32_t kKbcMagic = 0x01424B43; // "KBC\x01" little-endian
constexpr uint32_t kKbcVersion = 2;
constexpr uint32_t kFlagHasDebugInfo = 1;

void write_u8(std::ostream &out, uint8_t v) { out.write(reinterpret_cast<const char *>(&v), 1); }
void write_i8(std::ostream &out, int8_t v) { out.write(reinterpret_cast<const char *>(&v), 1); }
void write_u32(std::ostream &out, uint32_t v) { out.write(reinterpret_cast<const char *>(&v), 4); }
void write_i32(std::ostream &out, int32_t v) { out.write(reinterpret_cast<const char *>(&v), 4); }
void write_u64(std::ostream &out, uint64_t v) { out.write(reinterpret_cast<const char *>(&v), 8); }
void write_i64(std::ostream &out, int64_t v) { out.write(reinterpret_cast<const char *>(&v), 8); }
void write_f64(std::ostream &out, double v) { out.write(reinterpret_cast<const char *>(&v), 8); }
void write_str(std::ostream &out, const std::string &s) {
  write_u32(out, static_cast<uint32_t>(s.size()));
  out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool read_u8(std::istream &in, uint8_t &v) { return in.read(reinterpret_cast<char *>(&v), 1).good(); }
bool read_i8(std::istream &in, int8_t &v) { return in.read(reinterpret_cast<char *>(&v), 1).good(); }
bool read_u32(std::istream &in, uint32_t &v) { return in.read(reinterpret_cast<char *>(&v), 4).good(); }
bool read_i32(std::istream &in, int32_t &v) { return in.read(reinterpret_cast<char *>(&v), 4).good(); }
bool read_u64(std::istream &in, uint64_t &v) { return in.read(reinterpret_cast<char *>(&v), 8).good(); }
bool read_i64(std::istream &in, int64_t &v) { return in.read(reinterpret_cast<char *>(&v), 8).good(); }
bool read_f64(std::istream &in, double &v) { return in.read(reinterpret_cast<char *>(&v), 8).good(); }
bool read_str(std::istream &in, std::string &s) {
  uint32_t len = 0;
  if (!read_u32(in, len)) return false;
  s.resize(len);
  return in.read(s.data(), static_cast<std::streamsize>(len)).good();
}

// --- Sleb128 encoding/decoding for v2 format ---

int encode_sleb128(int32_t value, uint8_t *buf) {
  int count = 0;
  bool more = true;
  while (more) {
    uint8_t byte = static_cast<uint8_t>(value & 0x7F);
    value >>= 7;
    if ((value == 0 && !(byte & 0x40)) || (value == -1 && (byte & 0x40))) {
      more = false;
    } else {
      byte |= 0x80;
    }
    buf[count++] = byte;
  }
  return count;
}

int decode_sleb128(const uint8_t *buf, int32_t *value) {
  int32_t result = 0;
  int shift = 0;
  int count = 0;
  uint8_t byte;
  do {
    byte = buf[count++];
    result |= (int32_t)(byte & 0x7F) << shift;
    shift += 7;
  } while (byte & 0x80);
  if (shift < 32 && (byte & 0x40)) {
    result |= -(1 << shift);
  }
  *value = result;
  return count;
}

// Write Sleb128 to stream.
void write_sleb128(std::ostream &out, int32_t v) {
  uint8_t buf[5];
  int n = encode_sleb128(v, buf);
  out.write(reinterpret_cast<const char *>(buf), n);
}

// Read Sleb128 from stream.
bool read_sleb128(std::istream &in, int32_t &v) {
  uint8_t buf[5];
  int count = 0;
  uint8_t byte;
  do {
    if (!in.read(reinterpret_cast<char *>(&byte), 1).good()) return false;
    buf[count++] = byte;
  } while (byte & 0x80);
  decode_sleb128(buf, &v);
  return true;
}

bool values_equal(const Value &a, const Value &b) {
  if (a.type != b.type) return false;
  switch (a.type) {
  case ValueType::Int:
    return a.as_int == b.as_int;
  case ValueType::Double:
    return a.as_double_storage == b.as_double_storage;
  case ValueType::Bool:
    return a.as_bool == b.as_bool;
  case ValueType::Char:
    return a.as_int == b.as_int;
  case ValueType::Null:
    return true;
  case ValueType::String:
    return a.string_val() == b.string_val();
  case ValueType::Function:
    return a.function_idx == b.function_idx;
  default:
    return false;
  }
}

} // namespace

// ---------------------------------------------------------------------------
// Chunk::serialize
// ---------------------------------------------------------------------------

bool Chunk::serialize(const std::string &path, bool strip_debug) const {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;

  const uint32_t flags = strip_debug ? 0 : kFlagHasDebugInfo;

  // Header
  write_u32(out, kKbcMagic);
  write_u32(out, kKbcVersion);
  write_u32(out, flags);
  write_u32(out, 0); // reserved

  // Constants section
  write_u32(out, static_cast<uint32_t>(constants_.size()));
  for (const auto &c : constants_) {
    write_u8(out, static_cast<uint8_t>(c.type));
    switch (c.type) {
    case ValueType::Int:
      write_i64(out, c.as_int);
      break;
    case ValueType::Double:
      write_f64(out, c.as_double_storage);
      break;
    case ValueType::Bool:
      write_u8(out, c.as_bool ? 1 : 0);
      break;
    case ValueType::Char:
      write_i8(out, static_cast<int8_t>(c.as_int));
      break;
    case ValueType::Null:
      break; // no data
    case ValueType::String:
      write_str(out, c.string_val());
      break;
    case ValueType::Function:
      // Function constant: just the index into the function table.
      write_i32(out, c.function_idx);
      break;
    case ValueType::Enum:
      // Enum constant: type_index + variant_index + payload count + payload values.
      {
        int ti = c.enum_type_idx;
        int vi = c.enum_variant_idx;
        const std::vector<Value> *pl = nullptr;
        if (c.heap) {
          auto *e = static_cast<HeapEnum *>(c.heap.ptr);
          ti = e->type_index;
          vi = e->variant_index;
          pl = &e->payload;
        }
        write_i32(out, static_cast<int32_t>(ti));
        write_i32(out, static_cast<int32_t>(vi));
        uint32_t pc = pl ? static_cast<uint32_t>(pl->size()) : 0;
        write_u32(out, pc);
        if (pl) for (const auto &p : *pl) {
        // Recurse — payload values should be simple types only.
        write_u8(out, static_cast<uint8_t>(p.type));
        switch (p.type) {
        case ValueType::Int:
          write_i64(out, p.as_int);
          break;
        case ValueType::Double:
          write_f64(out, p.as_double_storage);
          break;
        case ValueType::Bool:
          write_u8(out, p.as_bool ? 1 : 0);
          break;
        case ValueType::Char:
          write_i8(out, static_cast<int8_t>(p.as_int));
          break;
        case ValueType::Null:
          break;
        case ValueType::String:
          write_str(out, p.string_val());
          break;
        default:
          break; // skip unsupported payload types
        }
      }
      } /* ti/vi/pl block */
      break;
    default:
      // Runtime-only types (Struct, Array, Map, NativeFunction)
      // should not appear in the constant pool.
      break;
    }
  }

  // Instructions section
  write_u32(out, static_cast<uint32_t>(instructions_.size()));
  for (const auto &inst : instructions_) {
    write_u8(out, static_cast<uint8_t>(inst.op));
    write_sleb128(out, inst.operand);
    if (!strip_debug) {
      write_sleb128(out, inst.line);
      write_sleb128(out, inst.column);
    }
  }

  // Function metadata section
  write_u32(out, static_cast<uint32_t>(functions_.size()));
  for (const auto &fn : functions_) {
    write_str(out, fn.name);
    write_u64(out, static_cast<uint64_t>(fn.entry));
    write_i32(out, static_cast<int32_t>(fn.param_count));
  }

  // Struct metadata section
  write_u32(out, static_cast<uint32_t>(struct_metas_.size()));
  for (const auto &sm : struct_metas_) {
    write_str(out, sm.name);
    write_u32(out, static_cast<uint32_t>(sm.field_names.size()));
    for (const auto &f : sm.field_names) {
      write_str(out, f);
    }
  }

  // Enum metadata section
  write_u32(out, static_cast<uint32_t>(enum_metas_.size()));
  for (const auto &em : enum_metas_) {
    write_str(out, em.name);
    write_u32(out, static_cast<uint32_t>(em.variants.size()));
    for (std::size_t i = 0; i < em.variants.size(); ++i) {
      write_str(out, em.variants[i]);
      write_i32(out, static_cast<int32_t>(em.variant_param_counts[i]));
    }
  }

  return out.good();
}

// ---------------------------------------------------------------------------
// Chunk::deserialize
// ---------------------------------------------------------------------------

Chunk Chunk::deserialize(const std::string &path, std::string *error) {
  Chunk chunk;
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = "Cannot open file: " + path;
    return chunk;
  }

  // Header
  uint32_t magic = 0, version = 0, flags = 0, reserved = 0;
  if (!read_u32(in, magic) || magic != kKbcMagic) {
    if (error) *error = "Invalid .kbc file: bad magic";
    return chunk;
  }
  if (!read_u32(in, version) || (version != 1 && version != 2)) {
    if (error) *error = "Unsupported .kbc version";
    return chunk;
  }
  if (!read_u32(in, flags)) {
    if (error) *error = "Truncated .kbc header";
    return chunk;
  }
  if (!read_u32(in, reserved)) {
    if (error) *error = "Truncated .kbc header";
    return chunk;
  }

  // Constants section
  uint32_t const_count = 0;
  if (!read_u32(in, const_count)) {
    if (error) *error = "Truncated constants count";
    return chunk;
  }
  for (uint32_t i = 0; i < const_count; ++i) {
    uint8_t type_tag = 0;
    if (!read_u8(in, type_tag)) {
      if (error) *error = "Truncated constant type";
      return chunk;
    }
    auto vt = static_cast<ValueType>(type_tag);
    switch (vt) {
    case ValueType::Int: {
      int64_t v = 0;
      if (!read_i64(in, v)) {
        if (error) *error = "Truncated int constant";
        return chunk;
      }
      chunk.add_constant(Value::int_value(v));
      break;
    }
    case ValueType::Double: {
      double v = 0.0;
      if (!read_f64(in, v)) {
        if (error) *error = "Truncated double constant";
        return chunk;
      }
      chunk.add_constant(Value::double_value(v));
      break;
    }
    case ValueType::Bool: {
      uint8_t v = 0;
      if (!read_u8(in, v)) {
        if (error) *error = "Truncated bool constant";
        return chunk;
      }
      chunk.add_constant(Value::bool_value(v != 0));
      break;
    }
    case ValueType::Char: {
      int8_t v = 0;
      if (!read_i8(in, v)) {
        if (error) *error = "Truncated char constant";
        return chunk;
      }
      chunk.add_constant(Value::char_value(v));
      break;
    }
    case ValueType::Null:
      chunk.add_constant(Value::null_value());
      break;
    case ValueType::String: {
      std::string v;
      if (!read_str(in, v)) {
        if (error) *error = "Truncated string constant";
        return chunk;
      }
      chunk.add_constant(Value::string_value(std::move(v)));
      break;
    }
    case ValueType::Function: {
      int32_t idx = -1;
      if (!read_i32(in, idx)) {
        if (error) *error = "Truncated function constant";
        return chunk;
      }
      chunk.add_constant(Value::function_value(idx));
      break;
    }
    case ValueType::Enum: {
      int32_t type_idx = -1, var_idx = -1;
      uint32_t payload_count = 0;
      if (!read_i32(in, type_idx) || !read_i32(in, var_idx) || !read_u32(in, payload_count)) {
        if (error) *error = "Truncated enum constant";
        return chunk;
      }
      if (payload_count == 0) {
        chunk.add_constant(Value::enum_value(type_idx, var_idx));
      } else {
        std::vector<Value> payload;
        for (uint32_t j = 0; j < payload_count; ++j) {
          uint8_t pt = 0;
          if (!read_u8(in, pt)) {
            if (error) *error = "Truncated enum payload type";
            return chunk;
          }
          auto pvt = static_cast<ValueType>(pt);
          switch (pvt) {
          case ValueType::Int: {
            int64_t v = 0;
            if (!read_i64(in, v)) {
              if (error) *error = "Truncated enum payload int";
              return chunk;
            }
            payload.push_back(Value::int_value(v));
            break;
          }
          case ValueType::Double: {
            double v = 0.0;
            if (!read_f64(in, v)) {
              if (error) *error = "Truncated enum payload double";
              return chunk;
            }
            payload.push_back(Value::double_value(v));
            break;
          }
          case ValueType::Bool: {
            uint8_t v = 0;
            if (!read_u8(in, v)) {
              if (error) *error = "Truncated enum payload bool";
              return chunk;
            }
            payload.push_back(Value::bool_value(v != 0));
            break;
          }
          case ValueType::Char: {
            int8_t v = 0;
            if (!read_i8(in, v)) {
              if (error) *error = "Truncated enum payload char";
              return chunk;
            }
            payload.push_back(Value::char_value(v));
            break;
          }
          case ValueType::Null:
            payload.push_back(Value::null_value());
            break;
          case ValueType::String: {
            std::string v;
            if (!read_str(in, v)) {
              if (error) *error = "Truncated enum payload string";
              return chunk;
            }
            payload.push_back(Value::string_value(std::move(v)));
            break;
          }
          default:
            if (error) *error = "Unsupported enum payload type in .kbc";
            return chunk;
          }
        }
        chunk.add_constant(Value::enum_value_with_payload(type_idx, var_idx, std::move(payload)));
      }
      break;
    }
    default:
      if (error) *error = "Unsupported constant type in .kbc";
      return chunk;
    }
  }

  // Instructions section
  const bool has_debug = (flags & kFlagHasDebugInfo) != 0;
  const bool use_sleb128 = (version >= 2);
  uint32_t inst_count = 0;
  if (!read_u32(in, inst_count)) {
    if (error) *error = "Truncated instructions count";
    return chunk;
  }
  for (uint32_t i = 0; i < inst_count; ++i) {
    uint8_t op = 0;
    int32_t operand = 0;
    if (!read_u8(in, op)) {
      if (error) *error = "Truncated instruction";
      return chunk;
    }
    if (use_sleb128) {
      if (!read_sleb128(in, operand)) {
        if (error) *error = "Truncated instruction operand";
        return chunk;
      }
    } else {
      if (!read_i32(in, operand)) {
        if (error) *error = "Truncated instruction";
        return chunk;
      }
    }
    int32_t line = 0, column = 0;
    if (has_debug) {
      if (use_sleb128) {
        if (!read_sleb128(in, line) || !read_sleb128(in, column)) {
          if (error) *error = "Truncated instruction debug info";
          return chunk;
        }
      } else {
        if (!read_i32(in, line) || !read_i32(in, column)) {
          if (error) *error = "Truncated instruction debug info";
          return chunk;
        }
      }
    }
    chunk.instructions_.push_back(Instruction{
        .op = static_cast<OpCode>(op),
        .operand = operand,
        .line = static_cast<int>(line),
        .column = static_cast<int>(column),
    });
  }

  // Function metadata section
  uint32_t fn_count = 0;
  if (!read_u32(in, fn_count)) {
    // Functions may be absent — that's ok for a minimal chunk.
    return chunk;
  }
  for (uint32_t i = 0; i < fn_count; ++i) {
    FunctionInfo fi;
    uint64_t entry = 0;
    int32_t pc = 0;
    if (!read_str(in, fi.name) || !read_u64(in, entry) || !read_i32(in, pc)) {
      if (error) *error = "Truncated function metadata";
      return chunk;
    }
    fi.entry = static_cast<std::size_t>(entry);
    fi.param_count = pc;
    chunk.functions_.push_back(std::move(fi));
  }

  // Struct metadata section
  uint32_t sm_count = 0;
  if (!read_u32(in, sm_count)) {
    return chunk;
  }
  for (uint32_t i = 0; i < sm_count; ++i) {
    StructMeta sm;
    uint32_t fc = 0;
    if (!read_str(in, sm.name) || !read_u32(in, fc)) {
      if (error) *error = "Truncated struct metadata";
      return chunk;
    }
    for (uint32_t j = 0; j < fc; ++j) {
      std::string fn;
      if (!read_str(in, fn)) {
        if (error) *error = "Truncated struct field name";
        return chunk;
      }
      sm.field_names.push_back(std::move(fn));
    }
    chunk.struct_metas_.push_back(std::move(sm));
  }

  // Enum metadata section
  uint32_t em_count = 0;
  if (!read_u32(in, em_count)) {
    return chunk;
  }
  for (uint32_t i = 0; i < em_count; ++i) {
    EnumMeta em;
    uint32_t vc = 0;
    if (!read_str(in, em.name) || !read_u32(in, vc)) {
      if (error) *error = "Truncated enum metadata";
      return chunk;
    }
    for (uint32_t j = 0; j < vc; ++j) {
      std::string vn;
      int32_t pc = 0;
      if (!read_str(in, vn) || !read_i32(in, pc)) {
        if (error) *error = "Truncated enum variant";
        return chunk;
      }
      em.variants.push_back(std::move(vn));
      em.variant_param_counts.push_back(pc);
    }
    chunk.enum_metas_.push_back(std::move(em));
  }

  return chunk;
}

} // namespace kinglet
