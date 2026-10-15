// Platform hooks for mbedTLS (MPY20SSL.TTP): time and entropy.

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <mint/osbind.h>

#include "py/mpconfig.h"
#include "py/mphal.h"

#if MICROPY_SSL_MBEDTLS

#include "mbedtls_config_port.h"
#include "mbedtls/platform_time.h"

time_t atari_mbedtls_time(time_t *timer) {
    return time(timer);
}

mbedtls_ms_time_t mbedtls_ms_time(void) {
    return mp_hal_ticks_ms();
}

// --- Entropy
//
// Under FreeMiNT, /dev/urandom (fed by interrupt timings in the kernel).
// Under TOS and MagiC there is no such device, so we collect timing jitter
// between free-running counters on different clocks: the video address
// counter (pixel clock), MFP timer C (2.4576 MHz) and the 200 Hz tick,
// sampled around busy loops whose length depends on what was read before.
// Interrupts (VBL, timer C, keyboard) land at unpredictable points of those
// loops. mbedTLS hashes all of it (SHA-512 accumulator) before use. Weaker
// than a kernel pool, but never constant from one run to the next.

static uint8_t *rnd_out;
static size_t rnd_len;

static long rnd_jitter(void) {
    volatile const uint8_t *vcount = (const uint8_t *)0xffff8209;
    volatile const uint8_t *tcdr = (const uint8_t *)0xfffffa23;
    volatile const uint32_t *hz200 = (const uint32_t *)0x4ba;
    uint32_t x = *hz200;
    for (size_t i = 0; i < rnd_len; i++) {
        uint32_t acc = 0;
        for (int k = 0; k < 16; k++) {
            for (volatile unsigned j = (x & 31) + 1; j > 0; j--) {
            }
            x = x * 69069 + 1 + ((uint32_t)*vcount << 8) + *tcdr;
            acc = ((acc << 3) | (acc >> 29)) ^ x ^ *hz200;
        }
        rnd_out[i] ^= (uint8_t)(acc ^ (acc >> 8) ^ (acc >> 16) ^ (acc >> 24));
    }
    return 0;
}

void mp_hal_get_random(size_t n, uint8_t *buf) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        long r = read(fd, buf, n);
        close(fd);
        if (r == (long)n) {
            return;
        }
    }
    uint32_t seed = (uint32_t)Random() ^ ((uint32_t)Tgettime() << 16) ^ Tgetdate();
    for (size_t i = 0; i < n; i++) {
        buf[i] = (uint8_t)(seed >> ((i & 3) * 8));
    }
    rnd_out = buf;
    rnd_len = n;
    Supexec(rnd_jitter);
}

#endif // MICROPY_SSL_MBEDTLS
