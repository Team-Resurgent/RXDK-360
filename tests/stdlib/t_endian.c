/* C <endian.h> byte-order primitives on the big-endian Xbox 360 (PowerPC). The
   consolidated picolibc branch re-fixed 360 endian detection ("un-clobber
   endianness for the 360"), and only C++ <bit> and C23 <stdbit.h> asserted
   big-endian here -- the C endian.h surface (BYTE_ORDER, the htobe/htole and
   be/le-toh families, __builtin_bswap) had no test. On a big-endian host the
   host-to-big and big-to-host conversions are identity and the little-endian
   ones are byte swaps. */
#include "rxdk_test.h"
#include <sys/types.h>   /* __uint16_t/__uint32_t/__uint64_t the endian.h macros cast to */
#include <endian.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    /* the platform is big-endian: both the macro and the actual memory layout */
    CHECK(BYTE_ORDER == BIG_ENDIAN, "BYTE_ORDER == BIG_ENDIAN");
    CHECK(BIG_ENDIAN != LITTLE_ENDIAN, "BIG_ENDIAN != LITTLE_ENDIAN");

    uint32_t v = 0x01020304u;
    unsigned char b[4];
    memcpy(b, &v, 4);
    CHECK(b[0] == 0x01 && b[1] == 0x02 && b[2] == 0x03 && b[3] == 0x04,
          "uint32 is stored MSB-first in memory (big-endian)");

    /* __builtin_bswap correctness */
    CHECK((int)__builtin_bswap16(0x1234u) == 0x3412, "bswap16");
    CHECK_EQI((long)__builtin_bswap32(0x12345678u), (long)0x78563412u, "bswap32");
    CHECK(__builtin_bswap64(0x0123456789ABCDEFull) == 0xEFCDAB8967452301ull, "bswap64");

    /* host<->big is identity on a big-endian host */
    CHECK((int)htobe16(0x1234u) == 0x1234, "htobe16 identity (BE host)");
    CHECK_EQI((long)htobe32(0x12345678u), (long)0x12345678u, "htobe32 identity (BE host)");
    CHECK(htobe64(0x0123456789ABCDEFull) == 0x0123456789ABCDEFull, "htobe64 identity (BE host)");
    CHECK((int)be16toh(0x1234u) == 0x1234, "be16toh identity (BE host)");
    CHECK_EQI((long)be32toh(0x12345678u), (long)0x12345678u, "be32toh identity (BE host)");

    /* host<->little is a byte swap on a big-endian host */
    CHECK((int)htole16(0x1234u) == 0x3412, "htole16 swaps (BE host)");
    CHECK_EQI((long)htole32(0x12345678u), (long)0x78563412u, "htole32 swaps (BE host)");
    CHECK(htole64(0x0123456789ABCDEFull) == 0xEFCDAB8967452301ull, "htole64 swaps (BE host)");
    CHECK((int)le16toh(0x1234u) == 0x3412, "le16toh swaps (BE host)");
    CHECK_EQI((long)le32toh(0x12345678u), (long)0x78563412u, "le32toh swaps (BE host)");

    CHECK_DONE("endian");
    return 0;
}
