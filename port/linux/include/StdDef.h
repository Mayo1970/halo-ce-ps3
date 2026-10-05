/* the game includes <StdDef.h>; include_next, as a case-insensitive file system
(Docker on Windows) also finds this file for <stddef.h> */
/* the host keeps its own long (the PS3 game's long is a 32-bit macro, halo_ps3_prefix.h) */
#pragma push_macro("long")
#undef long
#include_next <stddef.h>
#pragma pop_macro("long")
