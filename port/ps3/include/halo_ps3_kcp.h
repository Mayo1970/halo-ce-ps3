/* KCP's 64-bit integers in the Xbox layer (force-included in ikcp.c and p2p.c, which include ikcp.h): its own
typedefs are long long, which the game's long makes int int (trap 11) */

#include <stdint.h>

#define __IINT64_DEFINED
#define __IUINT64_DEFINED
typedef int64_t IINT64;
typedef uint64_t IUINT64;
