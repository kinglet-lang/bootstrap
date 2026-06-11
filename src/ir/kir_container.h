#pragma once

#include "ir/kir.h"
#include "types/types.h"

namespace kinglet {

KirContainerType kir_container_from_surface_type(const Type &type);
bool kir_container_is_array(const KirContainerType &container);
bool kir_container_is_map(const KirContainerType &container);

} // namespace kinglet
