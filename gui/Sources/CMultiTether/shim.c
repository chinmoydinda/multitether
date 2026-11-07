#include "multitether_c.h"

// SwiftPM requires at least one translation unit for a C target.
static const int multitether_c_shim_anchor = 0;
const int* multitether_c_shim_unused(void);
const int* multitether_c_shim_unused(void) { return &multitether_c_shim_anchor; }
