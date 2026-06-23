// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/pipeline/pipeline.h"

#include "frontend/checker/type_checker.h"
#include "ir/kir.h"
#include "ir/kir_specialize.h"
#include "ir/kir_typing.h"

namespace kinglet {

void prepare_kir(KirModule &kir, const TypeChecker &checker) {
  checker.populate_kir_types(&kir);
  infer_kir_types(&kir);
  specialize_kir_arithmetic(&kir);
}

} // namespace kinglet
