#ifndef MICROPY_INCLUDED_ATARI_MPHALPORT_H
#define MICROPY_INCLUDED_ATARI_MPHALPORT_H

#include <stdint.h>
#include <stddef.h>

// --- Timing ---
//
// The system 200 Hz counter (_hz_200 at 0x4BA) is our time base: 5 ms
// resolution, monotonic since boot. The ST protects RAM below 0x800 against
// user-mode access, so reading it goes through Supexec(). That costs a trap
// per call (a few microseconds), which is fine at this granularity.
//
// Unlike the Amiga port, ticks_ms() is a real clock here, not a stub.

uint32_t mp_atari_hz200(void);              // raw 200 Hz counter
mp_uint_t mp_hal_ticks_ms(void);
mp_uint_t mp_hal_ticks_us(void);
static inline mp_uint_t mp_hal_ticks_cpu(void) { return 0; }

void mp_hal_delay_ms(mp_uint_t ms);
void mp_hal_delay_us(mp_uint_t us);

// Calibrated busy-wait, needed for bit-banged I2C (phase 4bis) where _hz_200
// is far too coarse. mp_atari_calibrate_delay() must run once at startup.
void mp_atari_calibrate_delay(void);
void mp_hal_delay_us_fast(mp_uint_t us);

// time_ns() support -- 1 s precision via GEMDOS Tgetdate/Tgettime.
uint64_t mp_hal_time_ns(void);
uint32_t atari_dostime_to_epoch(unsigned int date, unsigned int time);

static inline void mp_hal_set_interrupt_char(char c) { (void)c; }

// TOS has no EINTR, so just execute the syscall once.
#include <errno.h>
#define MP_HAL_RETRY_SYSCALL(ret, syscall, raise) { \
    (ret) = (syscall); \
    if ((ret) == -1) { \
        int err = errno; \
        (void)err; \
        raise; \
    } \
}

// Timezone offset (local = utc + offset). TOS has no locale database;
// this reads the TZ environment variable, else returns 0 (UTC).
int32_t mp_hal_timezone_offset_s(void);

// UTF-8 <-> Atari ST charset conversion.
// I2C lines on the TwiSTer bus, as SoftI2C addresses them: 0 = SCL, 1 = SDA.
// The implementations live in machine_atari.c and assume supervisor mode --
// the whole transfer runs inside a single Supexec.
// A "pin" here is simply which of the two bus lines is meant, so an int does.
// It has to be a macro, not a typedef: py/mphal.h falls back to its virtual
// pin API unless mp_hal_pin_obj_t is already DEFINED.
#define mp_hal_pin_obj_t        int
#define mp_hal_get_pin_obj(p)   ((int)mp_obj_get_int(p))
#define MP_HAL_PIN_FMT          "%d"
#define mp_hal_pin_name(p)      (p)

void mp_hal_pin_open_drain(mp_hal_pin_obj_t pin);
void mp_hal_pin_od_low(mp_hal_pin_obj_t pin);
void mp_hal_pin_od_high(mp_hal_pin_obj_t pin);
int mp_hal_pin_read(mp_hal_pin_obj_t pin);

int atari_log_open(const char *path);
void atari_log_close(void);
void atari_log_write(const char *str, size_t len);

const char *atari_utf8_to_st(const char *s, char *buf, size_t bufsize);
mp_uint_t atari_st_to_utf8(const char *src, size_t srclen, char *dst, size_t dstsize);

// Console raw/cooked mode switching for readline
void mp_hal_stdio_mode_raw(void);
void mp_hal_stdio_mode_orig(void);

// readline is built with MICROPY_HAL_HAS_VT100=0, so it emits no ANSI
// sequences of its own and calls these two primitives instead. That is what
// spares us a general ANSI-to-VT52 translator on the output path.
#include "py/misc.h"
void mp_hal_move_cursor_back_impl(unsigned int pos);
void mp_hal_erase_line_from_cursor_impl(unsigned int n_chars_to_erase);
static inline void mp_hal_move_cursor_back(uint pos) {
    mp_hal_move_cursor_back_impl(pos);
}
static inline void mp_hal_erase_line_from_cursor(uint n_chars_to_erase) {
    mp_hal_erase_line_from_cursor_impl(n_chars_to_erase);
}

// Override mp_hal_readline so input() switches to raw mode before calling
// readline (the REPL does this itself, but input() doesn't).
#include "shared/readline/readline.h"
static inline int mp_hal_readline(vstr_t *line, const char *prompt) {
    mp_hal_stdio_mode_raw();
    int ret = readline(line, prompt);
    mp_hal_stdio_mode_orig();
    return ret;
}
#define mp_hal_readline mp_hal_readline

#endif // MICROPY_INCLUDED_ATARI_MPHALPORT_H
