#include "ir/kir_container.h"

#include "ir/kir_numeric.h"

namespace kinglet {

KirContainerType kir_container_from_surface_type(const Type &type) {
  KirContainerType out;
  if (type.kind == TypeKind::Array && type.element_type) {
    out.shape = KirContainerShape::Array;
    out.element_type = kir_type_from_surface_type(*type.element_type);
    return out;
  }
  if (type.kind == TypeKind::Map) {
    out.shape = KirContainerShape::Map;
    if (type.key_type) {
      out.key_type = kir_type_from_surface_type(*type.key_type);
    }
    if (type.element_type) {
      out.element_type = kir_type_from_surface_type(*type.element_type);
    }
    return out;
  }
  return out;
}

bool kir_container_is_array(const KirContainerType &container) {
  return container.shape == KirContainerShape::Array;
}

bool kir_container_is_map(const KirContainerType &container) {
  return container.shape == KirContainerShape::Map;
}

} // namespace kinglet
