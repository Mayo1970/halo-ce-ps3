/* newlib's limits.h for PS3 game units, whose long is 32-bit (halo_ps3_prefix.h):
newlib keeps its own long, and the LONG_ limits become the 32-bit ones */

#pragma push_macro("long")
#undef long
#include_next <limits.h>
#pragma pop_macro("long")

#ifdef long
#undef LONG_MAX
#undef LONG_MIN
#undef ULONG_MAX
#define LONG_MAX 2147483647
#define LONG_MIN (-2147483647 - 1)
#define ULONG_MAX 4294967295U
#endif
