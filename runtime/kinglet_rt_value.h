#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Native value wire format:
// - Plain integers: full int64 two's-complement (no tag bits).
// - Heap refs: pointer in low 48 bits, 0xFFFE in bits 48..63.
using kl_h = int64_t;

static constexpr uint64_t KL_HEAP_MARK = 0xFFFEULL << 48;
static constexpr uint64_t KL_INLINE_ENUM_MARK = 0xFFFDULL << 48;

static inline int kl_is_inline_enum(kl_h value) {
  return (static_cast<uint64_t>(value) & (0xFFFFULL << 48)) ==
         static_cast<uint64_t>(KL_INLINE_ENUM_MARK);
}

static inline kl_h kl_enum_inline(int32_t type_index, int32_t variant_index) {
  return static_cast<kl_h>(KL_INLINE_ENUM_MARK |
                         (static_cast<uint64_t>(type_index & 0xFFFF) << 16) |
                         static_cast<uint64_t>(variant_index & 0xFFFF));
}

static inline kl_h kl_from_int(int64_t value) {
  return static_cast<kl_h>(value);
}

static inline int64_t kl_to_int(kl_h value) {
  return static_cast<int64_t>(value);
}

static inline int kl_is_heap(kl_h value) {
  return (static_cast<uint64_t>(value) & (0xFFFFULL << 48)) ==
         static_cast<uint64_t>(KL_HEAP_MARK);
}

static inline void *kl_unbox_ptr(kl_h value) {
  return reinterpret_cast<void *>(static_cast<intptr_t>(
      static_cast<uint64_t>(value) & ~static_cast<uint64_t>(KL_HEAP_MARK)));
}

static inline kl_h kl_box_ptr(void *ptr) {
  return static_cast<kl_h>(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)) |
                             static_cast<uint64_t>(KL_HEAP_MARK));
}

kl_h kl_string_new(const char *data, int32_t len);
int32_t kl_string_len(kl_h value);
int32_t kl_string_view(kl_h value, const char **data, int32_t *len);

void kl_set_program_args(int32_t argc, const char **argv);

kl_h kl_native_out(int32_t argc, const kl_h *args);
kl_h kl_native_out_ln(int32_t argc, const kl_h *args);
kl_h kl_native_err(int32_t argc, const kl_h *args);
kl_h kl_native_err_ln(int32_t argc, const kl_h *args);
kl_h kl_native_in(int32_t argc, const kl_h *args, int32_t secret);
kl_h kl_native_fs_read(kl_h path);
kl_h kl_native_fs_write(kl_h path, kl_h content);
kl_h kl_native_sys_args(void);
int32_t kl_value_len(kl_h value);

kl_h kl_array_new(int32_t count, const kl_h *elements);
kl_h kl_array_get(kl_h array, int32_t index);
int32_t kl_array_len(kl_h array);
kl_h kl_array_push(kl_h array, kl_h value);
kl_h kl_array_resize(kl_h array, kl_h count, kl_h default_value);
kl_h kl_array_pop(kl_h array);
kl_h kl_array_clear(kl_h array);
kl_h kl_array_insert(kl_h array, kl_h index, kl_h value);
kl_h kl_array_reverse(kl_h array);
int32_t kl_contains(kl_h object, kl_h needle);
int64_t kl_index_of(kl_h object, kl_h needle);

kl_h kl_map_new(int32_t pair_count, const kl_h *pairs);
int32_t kl_map_has(kl_h map, kl_h key);
kl_h kl_map_keys(kl_h map);
// Generic indexed access dispatching across maps, strings, and arrays.
kl_h kl_index_get(kl_h object, kl_h key);
kl_h kl_index_set(kl_h object, kl_h key, kl_h value);
// remove() on maps (by key) and arrays (by index, returns removed element).
kl_h kl_remove(kl_h object, kl_h key);

int32_t kl_str_starts_with(kl_h str, kl_h prefix);
int32_t kl_str_ends_with(kl_h str, kl_h suffix);
kl_h kl_str_replace(kl_h str, kl_h old_str, kl_h new_str);
kl_h kl_str_split(kl_h str, kl_h delim);
kl_h kl_str_trim(kl_h str);
kl_h kl_str_to_upper(kl_h str);
kl_h kl_str_to_lower(kl_h str);

kl_h kl_struct_new(int32_t type_index, int32_t field_count, const kl_h *fields);
int32_t kl_struct_type_index(kl_h object);
kl_h kl_struct_field_at(kl_h object, int32_t field_index);
kl_h kl_struct_field_set(kl_h object, int32_t field_index, kl_h value);
kl_h kl_slice(kl_h value, int64_t start, int64_t end);

kl_h kl_float_new(double value);
double kl_float_get(kl_h value);
kl_h kl_float_from_bits(int64_t bits);
int64_t kl_float_to_bits(kl_h value);
kl_h kl_bool_to_string(kl_h value);
kl_h kl_null_to_string(void);

// Polymorphic arithmetic mirroring the VM's tag dispatch: string concat for
// `+`, IEEE double math when either side is a boxed float, int64 otherwise.
kl_h kl_value_add(kl_h left, kl_h right);
kl_h kl_value_sub(kl_h left, kl_h right);
kl_h kl_value_mul(kl_h left, kl_h right);
kl_h kl_value_div(kl_h left, kl_h right);
kl_h kl_value_mod(kl_h left, kl_h right);
// Three-way compare: strings lexicographic, floats as doubles, else int64.
int32_t kl_value_cmp(kl_h left, kl_h right);

kl_h kl_enum_new(int32_t type_index, int32_t variant_index);
kl_h kl_enum_new_payload(int32_t type_index, int32_t variant_index, int32_t count,
                         const kl_h *elements);
kl_h kl_enum_payload_at(kl_h value, int32_t index);
kl_h kl_cast_to_int(kl_h value);
kl_h kl_cast_to_float(kl_h value);
kl_h kl_cast_to_string(kl_h value);
kl_h kl_cast_to_char(kl_h value);
int32_t kl_value_eq(kl_h left, kl_h right);
int32_t kl_value_is_err(kl_h value);
int32_t kl_exit_code(kl_h value);

#ifdef __cplusplus
}
#endif
