#pragma once

#include "runtime/kinglet_rt_value.h"

#ifdef __cplusplus
extern "C" {
#endif

// Lowered user entry (source-level main). Defined by generated native code.
int kinglet_main(void);

#ifdef __cplusplus
}
#endif
