// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "ir/kir_container.h"

#include "ir/kir_numeric.h"

namespace kinglet {

namespace {

void set_nested_from_type(KirContainerType *out, const Type &inner) {
  if (inner.kind == TypeKind::Array && inner.element_type) {
    out->nested_shape = KirContainerShape::Array;
    out->nested_element_type = kir_type_from_surface_type(*inner.element_type);
    if (inner.element_type->kind == TypeKind::Array && inner.element_type->element_type) {
      // Third level is not tracked; scalar element_type on nested is enough for m[i][j].
    }
  } else if (inner.kind == TypeKind::Map) {
    out->nested_shape = KirContainerShape::Map;
    if (inner.key_type) {
      out->nested_key_type = kir_type_from_surface_type(*inner.key_type);
    }
    if (inner.element_type) {
      out->nested_element_type = kir_type_from_surface_type(*inner.element_type);
    }
  }
}

} // namespace

KirContainerType kir_container_peel_element(const KirContainerType &container) {
  KirContainerType out;
  out.shape = container.nested_shape;
  out.element_type = container.nested_element_type;
  out.key_type = container.nested_key_type;
  return out;
}

KirContainerType kir_container_clone(const KirContainerType &container) { return container; }

KirContainerType kir_container_from_surface_type(const Type &type) {
  KirContainerType out;
  if (type.kind == TypeKind::Array && type.element_type) {
    out.shape = KirContainerShape::Array;
    out.element_type = kir_type_from_surface_type(*type.element_type);
    if (type.element_type->kind == TypeKind::Array || type.element_type->kind == TypeKind::Map) {
      set_nested_from_type(&out, *type.element_type);
    }
    return out;
  }
  if (type.kind == TypeKind::Map) {
    out.shape = KirContainerShape::Map;
    if (type.key_type) {
      out.key_type = kir_type_from_surface_type(*type.key_type);
    }
    if (type.element_type) {
      out.element_type = kir_type_from_surface_type(*type.element_type);
      if (type.element_type->kind == TypeKind::Array || type.element_type->kind == TypeKind::Map) {
        set_nested_from_type(&out, *type.element_type);
      }
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
