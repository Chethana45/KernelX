#include <kernel/types.h>

/*
 * Minimal compiler-runtime helper for unsigned 64-bit division.
 *
 * GCC may emit a call to __udivdi3 for uint64_t division in a freestanding
 * 32-bit kernel. Keeping this implementation in the kernel avoids depending
 * on a multilib libgcc archive being installed on the build host.
 */
uint64_t __udivdi3(uint64_t numerator, uint64_t denominator) {
    if (denominator == 0) return 0;

    uint64_t quotient = 0;
    uint64_t remainder = 0;

    for (int bit = 63; bit >= 0; --bit) {
        uint64_t carry = remainder >> 63;
        remainder = (remainder << 1) | ((numerator >> bit) & 1ULL);

        if (carry || remainder >= denominator) {
            remainder -= denominator;
            quotient |= (1ULL << bit);
        }
    }
    return quotient;
}
