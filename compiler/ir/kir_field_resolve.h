#pragma once

#include "ir/kir.h"

namespace kinglet {

// Rewrite FieldGet/FieldSet operands from constant-string pool indices to
// compile-time field indices when the field name is unique in the module.
// Idempotent: sets KirModule::field_operands_resolved when done.
void resolve_kir_field_operands(KirModule &module);

} // namespace kinglet
