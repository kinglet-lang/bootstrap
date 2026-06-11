#pragma once

#include <cstdint>

namespace kinglet {

int64_t vm_truncate_i32(int64_t value);
int64_t vm_truncate_u8(int64_t value);
int64_t vm_truncate_u16(int64_t value);
int64_t vm_truncate_i16(int64_t value);

bool vm_opcode_is_i32_arithmetic(uint8_t opcode_raw);

} // namespace kinglet
