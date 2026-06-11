#include "vm/numeric.h"

#include "vm/chunk.h"

namespace kinglet {

int64_t vm_truncate_i32(int64_t value) {
  return static_cast<int64_t>(static_cast<int32_t>(value));
}

int64_t vm_truncate_u8(int64_t value) {
  return static_cast<int64_t>(static_cast<uint8_t>(value));
}

int64_t vm_truncate_u16(int64_t value) {
  return static_cast<int64_t>(static_cast<uint16_t>(value));
}

int64_t vm_truncate_i16(int64_t value) {
  return static_cast<int64_t>(static_cast<int16_t>(value));
}

bool vm_opcode_is_i32_arithmetic(uint8_t opcode_raw) {
  const auto op = static_cast<OpCode>(opcode_raw);
  return op == OpCode::AddI32 || op == OpCode::SubtractI32 || op == OpCode::MultiplyI32 ||
         op == OpCode::DivideI32 || op == OpCode::ModuloI32;
}

} // namespace kinglet
