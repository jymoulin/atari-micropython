// mbedTLS configuration for MPY20SSL.TTP (make SSL=1).
#ifndef MICROPY_INCLUDED_MBEDTLS_CONFIG_H
#define MICROPY_INCLUDED_MBEDTLS_CONFIG_H

// bn_mul.h has 68020 assembly (64-bit mulu.l) for the bignum inner loops:
// they dominate the handshake. SSL=1 builds for the 68020 only.
#define MBEDTLS_HAVE_ASM
// Faster reduction modulo the NIST primes (P-256, P-384...).
#define MBEDTLS_ECP_NIST_OPTIM
// Bulk ciphers that suit a 68k: ChaCha20-Poly1305 is 32-bit add, rotate
// and xor; GCM's GHASH is much faster with 8-bit tables (+3.5 KB per
// connection).
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_GCM_LARGE_TABLE

// Time: MiNTLib time(), and our millisecond counter.
#include <time.h>
time_t atari_mbedtls_time(time_t *timer);
#define MBEDTLS_PLATFORM_TIME_MACRO atari_mbedtls_time
#define MBEDTLS_PLATFORM_MS_TIME_ALT

// Memory from the GC heap (tracked allocator), entropy from
// mp_hal_get_random() (mbedtls_port.c).
#define MICROPY_MBEDTLS_CONFIG_BARE_METAL (1)

#include "extmod/mbedtls/mbedtls_config_common.h"

#endif // MICROPY_INCLUDED_MBEDTLS_CONFIG_H
