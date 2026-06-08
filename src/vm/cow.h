#pragma once

namespace kinglet {

struct Value;

// Deep copy for assignment (StoreLocal). Mutations stay in-place on locals.
Value value_deep_clone(const Value &value);

} // namespace kinglet
