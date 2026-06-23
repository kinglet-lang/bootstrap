// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "ir/kir.h"
#include "frontend/types/types.h"

namespace kinglet {

KirContainerType kir_container_from_surface_type(const Type &type);
KirContainerType kir_container_peel_element(const KirContainerType &container);
KirContainerType kir_container_clone(const KirContainerType &container);
bool kir_container_is_array(const KirContainerType &container);
bool kir_container_is_map(const KirContainerType &container);

} // namespace kinglet
