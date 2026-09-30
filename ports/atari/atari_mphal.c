// Hardware abstraction layer for Atari TOS / EmuTOS.
//
// Console I/O goes through GEMDOS for input (Crawcin gives us scan codes,
// which we need for the cursor keys) and GEMDOS Fwrite for output (one trap
// per buffer instead of one per character, and it honours redirection).
//
// Two translations happen on the way out: UTF-8 to the Atari ST character
// set, and nothing else -- readline is configured with MICROPY_HAL_HAS_VT100=0
// so it never emits ANSI sequences, and we hand it VT52 primitives instead.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mint/osbind.h>

#include "py/mpconfig.h"
#include "py/mphal.h"
#include "py/runtime.h"

#define CON_DEV 2   // BIOS device 2 = console (keyboard + screen)

// Input goes through GEMDOS, not the BIOS.
//
// Bconin(2) reads the PHYSICAL console, whatever the shell that launched us
// may have arranged. Under Geneva that has two visible consequences: touching
// the BIOS console makes Geneva hand the process a window of its own, so a
// program started from EmuCON2 prints into a second window instead of the
// shell's; and the shell cannot get its keyboard back afterwards.
//
// Cconis() and Crawcin() are the GEMDOS equivalents and read standard input,
// which is inherited and redirectable. Crawcin() keeps the scan code in bits
// 16-23 when standard input really is the console, so the arrow keys still
// work; when it is a pipe or a shell window we simply get the character,
// which is what we want there anyway.
#define con_avail()     (Cconis() != 0)
#define con_read()      Crawcin()

// -l FILE: everything printed is also appended to a file, as UTF-8 and with
// plain LF, so the result can be read on another machine. Screen output is
// unaffected -- this is a tee, not a redirection.
static long log_handle = -1;

int atari_log_open(const char *path) {
    long h = Fcreate(path, 0);
    if (h < 0) {
        return 0;
    }
    log_handle = h;
    return 1;
}

void atari_log_close(void) {
    if (log_handle >= 0) {
        Fclose((short)log_handle);
        log_handle = -1;
    }
}

// Called with the text as MicroPython holds it: UTF-8, before the conversion
// to the Atari character set that the screen needs.
void atari_log_write(const char *str, size_t len) {
    if (log_handle >= 0 && len > 0) {
        Fwrite((short)log_handle, (long)len, str);
    }
}

// ---------------------------------------------------------------------------
// System 200 Hz timer
// ---------------------------------------------------------------------------

// _hz_200 lives at 0x4BA. The ST traps user-mode access to RAM below 0x800,
// so every read goes through Supexec (a trap, a few microseconds).
static long read_hz200(void) {
    return (long)*(volatile uint32_t *)0x4BAL;
}

uint32_t mp_atari_hz200(void) {
    return (uint32_t)Supexec(read_hz200);
}

mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)(mp_atari_hz200() * 5);
}

mp_uint_t mp_hal_ticks_us(void) {
    // 5 ms granularity -- the counter is all we have until we hook a MFP
    // timer in a later phase.
    return (mp_uint_t)(mp_atari_hz200() * 5000);
}

// ---------------------------------------------------------------------------
// Calibrated busy-wait
// ---------------------------------------------------------------------------
//
// _hz_200 is far too coarse for microsecond delays (needed by bit-banged I2C).
// We calibrate a spin loop against it once at startup. This must be measured
// on the running machine: a 68020 at 24 MHz with the instruction cache enabled
// runs this loop an order of magnitude faster than a plain 68000.

static volatile uint32_t spin_per_us = 4;   // conservative default

static void spin(uint32_t n) {
    while (n--) {
        __asm__ __volatile__("nop");
    }
}

void mp_atari_calibrate_delay(void) {
    // Measure how many spin iterations fit in 20 ticks (100 ms).
    uint32_t t0 = mp_atari_hz200();
    while (mp_atari_hz200() == t0) {
        // align on a tick boundary so we don't count a partial one
    }
    t0 = mp_atari_hz200();

    uint32_t iterations = 0;
    while (mp_atari_hz200() - t0 < 20) {
        spin(100);
        iterations += 100;
    }

    // iterations happened in 100000 us. Guard against a zero result.
    uint32_t per_us = iterations / 100000;
    spin_per_us = (per_us == 0) ? 1 : per_us;
}

void mp_hal_delay_us_fast(mp_uint_t us) {
    spin((uint32_t)us * spin_per_us);
}

// ---------------------------------------------------------------------------
// Atari ST character set
// ---------------------------------------------------------------------------
//
// The Atari ST character set is not Latin-1. Positions 0x80-0xBF hold the
// accented Latin glyphs (a CP437-derived layout with Atari additions); above
// that sit Hebrew and mathematical symbols, which we do not map.
//
// A zero entry means "no mapping" and converts to '?'.

static const uint16_t st_to_unicode[0x40] = {
    // 0x80
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    // 0x90
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x00DF, 0x0192,
    // 0xA0
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    // 0xB0
    0x00E3, 0x00F5, 0x00D8, 0x00F8, 0x0153, 0x0152, 0x00C0, 0x00C3,
    0x00D5, 0x00A8, 0x00B4, 0x2020, 0x00B6, 0x00A9, 0x00AE, 0x2122,
};

// Unicode codepoint -> Atari ST byte, or 0 if unmappable.
static unsigned char unicode_to_st(uint32_t cp) {
    if (cp < 0x80) {
        return (unsigned char)cp;
    }
    for (int i = 0; i < 0x40; i++) {
        if (st_to_unicode[i] == cp) {
            return (unsigned char)(0x80 + i);
        }
    }
    return 0;
}

// Decode one UTF-8 sequence starting at s[0]; *consumed gets its byte length.
static uint32_t utf8_decode(const char *s, size_t avail, size_t *consumed) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) {
        *consumed = 1;
        return c;
    } else if ((c & 0xE0) == 0xC0 && avail >= 2) {
        *consumed = 2;
        return ((uint32_t)(c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
    } else if ((c & 0xF0) == 0xE0 && avail >= 3) {
        *consumed = 3;
        return ((uint32_t)(c & 0x0F) << 12)
             | (((unsigned char)s[1] & 0x3F) << 6)
             | ((unsigned char)s[2] & 0x3F);
    } else if ((c & 0xF8) == 0xF0 && avail >= 4) {
        *consumed = 4;
        return ((uint32_t)(c & 0x07) << 18)
             | (((unsigned char)s[1] & 0x3F) << 12)
             | (((unsigned char)s[2] & 0x3F) << 6)
             | ((unsigned char)s[3] & 0x3F);
    }
    *consumed = 1;
    return c;
}

// UTF-8 C string -> Atari ST, for paths handed to GEMDOS.
// Returns s unchanged when it is pure ASCII (fast path, no copy).
const char *atari_utf8_to_st(const char *s, char *buf, size_t bufsize) {
    const char *p = s;
    while (*p) {
        if ((unsigned char)*p > 0x7F) {
            goto convert;
        }
        p++;
    }
    return s;
convert:;
    size_t i = 0, j = 0;
    size_t len = strlen(s);
    while (s[i] && j < bufsize - 1) {
        size_t used;
        uint32_t cp = utf8_decode(s + i, len - i, &used);
        unsigned char st = unicode_to_st(cp);
        buf[j++] = st ? (char)st : '?';
        i += used;
    }
    buf[j] = '\0';
    return buf;
}

// Atari ST -> UTF-8, for names coming back from GEMDOS (listdir, getcwd).
// Returns the number of bytes written to dst.
mp_uint_t atari_st_to_utf8(const char *src, size_t srclen, char *dst, size_t dstsize) {
    size_t j = 0;
    for (size_t i = 0; i < srclen && j + 3 < dstsize; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c < 0x80) {
            dst[j++] = (char)c;
            continue;
        }
        uint32_t cp = (c < 0xC0) ? st_to_unicode[c - 0x80] : 0;
        if (cp == 0) {
            dst[j++] = '?';
        } else if (cp < 0x800) {
            dst[j++] = (char)(0xC0 | (cp >> 6));
            dst[j++] = (char)(0x80 | (cp & 0x3F));
        } else {
            dst[j++] = (char)(0xE0 | (cp >> 12));
            dst[j++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[j++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    return (mp_uint_t)j;
}

// ---------------------------------------------------------------------------
// Console input
// ---------------------------------------------------------------------------
//
// The console read returns a long: bits 0-7 hold the ASCII code, bits 16-23 the
// scan code. Cursor keys and friends come back with ASCII 0, so we translate
// their scan codes into the ANSI sequences readline expects and feed them
// back one character at a time through a small pending queue.

static char pending[8];
static uint8_t pending_len = 0;
static uint8_t pending_pos = 0;

static void push_pending(const char *s) {
    size_t n = strlen(s);
    if (n > sizeof(pending)) {
        n = sizeof(pending);
    }
    memcpy(pending, s, n);
    pending_len = (uint8_t)n;
    pending_pos = 0;
}

// Scan code -> the escape sequence readline understands, or NULL.
static const char *scancode_to_ansi(unsigned char scan) {
    switch (scan) {
        case 0x48: return "\x1b[A";   // up
        case 0x50: return "\x1b[B";   // down
        case 0x4D: return "\x1b[C";   // right
        case 0x4B: return "\x1b[D";   // left
        case 0x47: return "\x1b[H";   // Clr/Home -> Home
        case 0x62: return "\x1b[F";   // Undo -> End (the ST has no End key)
        case 0x53: return "\x1b[3~";  // Delete
        default:   return NULL;
    }
}

// Turn one raw console read into characters queued for readline.
// Returns the first character, or 0 if the key produced nothing.
static int dispatch_key(long key) {
    unsigned char ascii = (unsigned char)(key & 0xFF);
    unsigned char scan = (unsigned char)((key >> 16) & 0xFF);

    if (ascii == 0) {
        const char *seq = scancode_to_ansi(scan);
        if (seq == NULL) {
            return 0;
        }
        push_pending(seq);
        return pending[pending_pos++];
    }

    // Delete sends ASCII 0x7F on the ST; readline treats 127 as backspace,
    // so translate it to a real delete sequence using the scan code.
    if (scan == 0x53) {
        push_pending("\x1b[3~");
        return pending[pending_pos++];
    }

    return ascii;
}

int mp_hal_stdin_rx_chr(void) {
    if (pending_pos < pending_len) {
        return pending[pending_pos++];
    }
    for (;;) {
        int c = dispatch_key(con_read());
        if (c != 0) {
            return c;
        }
    }
}

// ---------------------------------------------------------------------------
// Console output
// ---------------------------------------------------------------------------

// The TOS console does not turn LF into CR+LF, so a bare "\n" from print()
// would step diagonally down the screen. We insert the CR ourselves, unless
// the caller already emitted one.
// -q : plus rien a l'ecran, seulement dans le journal. Indispensable pour une
// application GEM : la console et les fenetres partagent l'ecran, et le
// moindre print() barbouille le dessin.
int atari_quiet = 0;

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    atari_log_write(str, len);
    if (atari_quiet) {
        return (mp_uint_t)len;
    }

    // Fast path: pure ASCII with no bare LF goes straight out.
    int needs_work = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c > 0x7F || (c == '\n' && (i == 0 || str[i - 1] != '\r'))) {
            needs_work = 1;
            break;
        }
    }
    if (!needs_work) {
        return (mp_uint_t)Fwrite(1, len, str);
    }

    char buf[256];
    mp_uint_t total = 0;
    size_t i = 0, j = 0;
    char prev = 0;
    while (i < len) {
        size_t used;
        uint32_t cp = utf8_decode(str + i, len - i, &used);
        if (cp == '\n' && prev != '\r') {
            buf[j++] = '\r';
        }
        if (cp < 0x80) {
            buf[j++] = (char)cp;
        } else {
            unsigned char st = unicode_to_st(cp);
            buf[j++] = st ? (char)st : '?';
        }
        prev = (char)cp;
        i += used;
        if (j >= sizeof(buf) - 2) {
            total += (mp_uint_t)Fwrite(1, j, buf);
            j = 0;
        }
    }
    if (j > 0) {
        total += (mp_uint_t)Fwrite(1, j, buf);
    }
    return total;
}

// VT52 primitives used by readline (MICROPY_HAL_HAS_VT100 is 0).
void mp_hal_move_cursor_back_impl(unsigned int pos) {
    char buf[16];
    while (pos > 0) {
        unsigned int n = (pos > sizeof(buf)) ? sizeof(buf) : pos;
        memset(buf, '\b', n);
        Fwrite(1, n, buf);
        pos -= n;
    }
}

void mp_hal_erase_line_from_cursor_impl(unsigned int n_chars_to_erase) {
    (void)n_chars_to_erase;
    Fwrite(1, 2, "\x1bK");   // VT52: erase to end of line
}

// The BIOS console is always unbuffered and never echoes, so raw/cooked
// switching is a no-op. Kept so the REPL and input() can call it.
void mp_hal_stdio_mode_raw(void) {
}

void mp_hal_stdio_mode_orig(void) {
}

// ---------------------------------------------------------------------------
// Ctrl-C
// ---------------------------------------------------------------------------
//
// TOS has no signals. We poll the console: if a key is waiting we must
// consume it to see what it is, so anything that is not Ctrl-C is queued
// for the next mp_hal_stdin_rx_chr().

void mp_atari_check_signals(void) {
    if (pending_pos < pending_len) {
        return;     // already holding input, don't read more
    }
    if (!con_avail()) {
        return;
    }
    long key = con_read();
    if ((key & 0xFF) == 3) {    // Ctrl-C
        mp_sched_keyboard_interrupt();
        return;
    }
    int c = dispatch_key(key);
    if (c != 0 && pending_len == 0) {
        // A plain character: hold it as a one-byte pending sequence.
        pending[0] = (char)c;
        pending_len = 1;
        pending_pos = 0;
    }
}

// The key the Ctrl-C hook took off the keyboard buffer while Python was
// running, if any. Once taken it is out of reach of the AES, so a GEM call
// that waits for a key (gem.wait_key) must look here first. -1 if none.
int atari_take_pending_char(void) {
    if (pending_pos < pending_len) {
        return (unsigned char)pending[pending_pos++];
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Native code and the instruction cache
// ---------------------------------------------------------------------------
//
// The native emitter writes machine code as data, then jumps into it. A
// 68020/030 with its instruction cache on (CACHEON.PRG, ../cache) may still hold stale
// instructions for those addresses, and would run them: silently wrong code.
// So after each function is generated, the cache is invalidated.
//   68000        no cache, nothing to do
//   68020/030    CACR bit 3 clears the instruction cache (the 030 data cache
//                is write-through, so memory is already up to date)
//   68040/060    cpusha bc: push the copyback data cache, then invalidate
// All of it needs supervisor mode, hence one Supexec per generated function
// -- not per call, so the cost does not matter.

extern int atari_get_cookie(const char *name, uint32_t *val);

static long flush_020_030(void) {
    // Raw opcodes: the assembler of the 68000 build rejects movec.
    __asm__ volatile (
        ".word 0x4e7a,0x0002\n\t"     // movec %cacr,%d0
        "orw   #0x0008,%%d0\n\t"
        ".word 0x4e7b,0x0002"          // movec %d0,%cacr
        ::: "d0", "memory");
    return 0;
}

static long flush_040_060(void) {
    __asm__ volatile (".word 0xf4f8" ::: "memory");   // cpusha bc
    return 0;
}

void *atari_commit_exec(void *buf, size_t len, void *reloc) {
    (void)len;
    (void)reloc;
    static int cpu = -1;
    if (cpu < 0) {
        uint32_t v = 0;
        cpu = atari_get_cookie("_CPU", &v) ? (int)v : 0;
    }
    if (cpu >= 40) {
        Supexec(flush_040_060);
    } else if (cpu >= 20) {
        Supexec(flush_020_030);
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Delays
// ---------------------------------------------------------------------------

void mp_hal_delay_ms(mp_uint_t ms) {
    uint32_t start = mp_atari_hz200();
    uint32_t ticks = (ms + 4) / 5;      // 200 Hz -> 5 ms per tick
    while (mp_atari_hz200() - start < ticks) {
        mp_atari_check_signals();
        if (MP_STATE_THREAD(mp_pending_exception) != MP_OBJ_NULL) {
            return;
        }
    }
}

void mp_hal_delay_us(mp_uint_t us) {
    if (us >= 5000) {
        mp_hal_delay_ms(us / 1000);
    } else {
        mp_hal_delay_us_fast(us);
    }
}

// ---------------------------------------------------------------------------
// Wall clock
// ---------------------------------------------------------------------------

// Convert a packed GEMDOS date/time pair to seconds since the Unix epoch,
// in LOCAL time -- the caller subtracts the timezone offset if it wants UTC.
// Used for the wall clock and for file timestamps, which use the same format.
uint32_t atari_dostime_to_epoch(unsigned int d, unsigned int t) {
    int year = 1980 + ((d >> 9) & 0x7F);
    int month = (d >> 5) & 0x0F;
    int day = d & 0x1F;
    int hour = (t >> 11) & 0x1F;
    int minute = (t >> 5) & 0x3F;
    int second = (t & 0x1F) * 2;

    // Days since 1970-01-01 (civil calendar algorithm).
    int y = year;
    int m = month;
    if (m <= 2) {
        y -= 1;
        m += 12;
    }
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned long yoe = (unsigned long)(y - era * 400);
    unsigned long doy = (153UL * (m - 3) + 2) / 5 + day - 1;
    unsigned long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097L + (long)doe - 719468L;

    return (uint32_t)((uint32_t)days * 86400UL
                    + (uint32_t)hour * 3600UL
                    + (uint32_t)minute * 60UL
                    + (uint32_t)second);
}

uint64_t mp_hal_time_ns(void) {
    // GEMDOS date/time: 1 second resolution, already local time. On EmuTOS
    // and with the RTC driver installed these come from the hardware clock.
    uint32_t secs = atari_dostime_to_epoch((unsigned int)Tgetdate(),
                                           (unsigned int)Tgettime());
    return ((uint64_t)secs - (uint64_t)mp_hal_timezone_offset_s())
           * 1000000000ULL;
}

// TOS has no locale database. Honour TZ if MiNTlib parsed one, else UTC.
int32_t mp_hal_timezone_offset_s(void) {
    static int32_t cached = 0;
    static int done = 0;
    if (!done) {
        const char *tz = getenv("TZ");
        if (tz != NULL) {
            // Minimal parse of the "STDoffset" form, e.g. "CET-1" or "UTC+5".
            // POSIX TZ offsets are west-positive, we want east-positive.
            const char *p = tz;
            while (*p && (*p < '0' || *p > '9') && *p != '-' && *p != '+') {
                p++;
            }
            if (*p) {
                cached = -(int32_t)(atoi(p) * 3600);
            }
        }
        done = 1;
    }
    return cached;
}
