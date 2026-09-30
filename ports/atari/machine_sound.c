// machine.stram, and the STE / TT DMA sound.
//
// The DMA sound chip plays signed 8-bit samples straight out of memory, with
// no CPU involvement once started. TOS 2.06 has no call for it -- the XBIOS
// sound functions arrived with the Falcon -- so we drive its registers at
// 0xFF8900 directly, which is what every STE program did anyway.
//
// THE CONSTRAINT THAT SHAPES THIS FILE: the DMA chips only see ST-RAM. On
// the TwiSTer the alternate RAM sits on the 68020 side of the bus, where the
// sound DMA cannot reach, and our GC heap lives there. So a sample must be in
// a buffer from machine.stram(), and anything above phystop is refused with
// a clear message rather than played as garbage.

#include <string.h>

#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/objarray.h"

#include <mint/osbind.h>

extern unsigned long atari_stram_top(void);
extern int atari_get_cookie(const char *name, uint32_t *val);

// ---------------------------------------------------------------------------
// machine.stram(n): n bytes of ST-RAM, as a bytearray.
//
// Taken from GEMDOS with Mxalloc(n, 0), outside the GC heap: GEMDOS gives it
// back when the program ends, not before. Allocate what you need once and
// reuse it -- calling this in a loop would leak until exit.
static mp_obj_t machine_stram(mp_obj_t n_in) {
    mp_int_t n = mp_obj_get_int(n_in);
    if (n <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("size must be positive"));
    }
    n = (n + 1) & ~1;               // DMA works on words: keep it even
    void *p = (void *)Mxalloc(n, 0);
    if (p == NULL || (long)p < 0) {
        mp_raise_OSError(MP_ENOMEM);
    }
    memset(p, 0, (size_t)n);
    return mp_obj_new_bytearray_by_ref((size_t)n, p);
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_stram_obj, machine_stram);

// ---------------------------------------------------------------------------
// DMA sound registers. Each is the odd byte of a 16-bit register.

#define SND_CTRL        (*(volatile uint8_t *)0xFF8901)   // bit0 play, bit1 loop
#define SND_START_H     (*(volatile uint8_t *)0xFF8903)
#define SND_START_M     (*(volatile uint8_t *)0xFF8905)
#define SND_START_L     (*(volatile uint8_t *)0xFF8907)
#define SND_COUNT_H     (*(volatile uint8_t *)0xFF8909)
#define SND_COUNT_M     (*(volatile uint8_t *)0xFF890B)
#define SND_COUNT_L     (*(volatile uint8_t *)0xFF890D)
#define SND_END_H       (*(volatile uint8_t *)0xFF890F)
#define SND_END_M       (*(volatile uint8_t *)0xFF8911)
#define SND_END_L       (*(volatile uint8_t *)0xFF8913)
#define SND_MODE        (*(volatile uint8_t *)0xFF8921)   // bit7 mono, bits0-1 rate

// The only four rates the STE knows.
static const uint32_t snd_rates[4] = { 6258, 12517, 25033, 50066 };

// Supervisor-side state: the registers are in I/O space.
static uint32_t snd_start, snd_end;
static uint8_t snd_mode, snd_ctrl;
static uint32_t snd_count;

static long snd_do_play(void) {
    SND_CTRL = 0;                   // stop before touching the addresses
    SND_START_H = (uint8_t)(snd_start >> 16);
    SND_START_M = (uint8_t)(snd_start >> 8);
    SND_START_L = (uint8_t)snd_start;
    SND_END_H = (uint8_t)(snd_end >> 16);
    SND_END_M = (uint8_t)(snd_end >> 8);
    SND_END_L = (uint8_t)snd_end;
    SND_MODE = snd_mode;
    SND_CTRL = snd_ctrl;
    return 0;
}

static long snd_do_stop(void) {
    SND_CTRL = 0;
    return 0;
}

static long snd_do_status(void) {
    snd_ctrl = SND_CTRL;
    snd_count = ((uint32_t)SND_COUNT_H << 16) | ((uint32_t)SND_COUNT_M << 8)
                | SND_COUNT_L;
    return 0;
}

static void snd_check_hardware(void) {
    // _SND bit 1: 8-bit stereo DMA sound (STE, Mega STE, TT, Falcon).
    uint32_t snd = 0;
    if (!atari_get_cookie("_SND", &snd) || !(snd & 2)) {
        mp_raise_msg(&mp_type_OSError,
                     MP_ERROR_TEXT("no DMA sound on this machine (plain ST?)"));
    }
}

// machine.dmaplay(buf, rate=12517, stereo=False, loop=False)
//
// Plays buf, signed 8-bit samples, interleaved left/right when stereo.
// Returns at once: the chip carries on alone. The rate is snapped to the
// nearest of the four the STE supports, and the one actually used is
// returned.
static mp_obj_t machine_dmaplay(size_t n_args, const mp_obj_t *pos_args,
                                mp_map_t *kw_args) {
    enum { ARG_buf, ARG_rate, ARG_stereo, ARG_loop };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_buf, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_rate, MP_ARG_INT, {.u_int = 12517} },
        { MP_QSTR_stereo, MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_loop, MP_ARG_BOOL, {.u_bool = false} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed), allowed, args);

    snd_check_hardware();

    mp_buffer_info_t bi;
    mp_get_buffer_raise(args[ARG_buf].u_obj, &bi, MP_BUFFER_READ);
    uint32_t start = (uint32_t)(uintptr_t)bi.buf;
    uint32_t len = (uint32_t)bi.len & ~1UL;         // whole words only
    if (len == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer too short"));
    }
    if (start & 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer must start on an even address"));
    }
    if (start + len > atari_stram_top()) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer not in ST-RAM: use machine.stram()"));
    }

    // Nearest supported rate.
    mp_int_t want = args[ARG_rate].u_int;
    int best = 0;
    for (int i = 1; i < 4; i++) {
        mp_int_t d_best = want - (mp_int_t)snd_rates[best];
        mp_int_t d_i = want - (mp_int_t)snd_rates[i];
        if (d_best < 0) {
            d_best = -d_best;
        }
        if (d_i < 0) {
            d_i = -d_i;
        }
        if (d_i < d_best) {
            best = i;
        }
    }

    snd_start = start;
    snd_end = start + len;          // the chip stops BEFORE this address
    snd_mode = (uint8_t)((args[ARG_stereo].u_bool ? 0x00 : 0x80) | best);
    snd_ctrl = (uint8_t)(1 | (args[ARG_loop].u_bool ? 2 : 0));

    // Keep the buffer alive while the chip reads it: if the Python side
    // dropped its last reference, the GC could hand those bytes to someone
    // else in mid-play.
    MP_STATE_PORT(atari_dma_sound_buf) = args[ARG_buf].u_obj;

    Supexec(snd_do_play);
    return mp_obj_new_int((mp_int_t)snd_rates[best]);
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_dmaplay_obj, 1, machine_dmaplay);

static mp_obj_t machine_dmastop(void) {
    uint32_t snd = 0;
    if (atari_get_cookie("_SND", &snd) && (snd & 2)) {
        Supexec(snd_do_stop);
    }
    MP_STATE_PORT(atari_dma_sound_buf) = MP_OBJ_NULL;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_dmastop_obj, machine_dmastop);

// machine.dmapos() -> byte offset into the buffer being played, or None when
// the chip is idle. A one-shot sample that has finished reads as None, which
// is the easy way to wait for the end:
//     while machine.dmapos() is not None: machine.Vsync()
static mp_obj_t machine_dmapos(void) {
    snd_check_hardware();
    Supexec(snd_do_status);
    if (!(snd_ctrl & 1)) {
        MP_STATE_PORT(atari_dma_sound_buf) = MP_OBJ_NULL;
        return mp_const_none;
    }
    return mp_obj_new_int((mp_int_t)(snd_count - snd_start));
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_dmapos_obj, machine_dmapos);

MP_REGISTER_ROOT_POINTER(mp_obj_t atari_dma_sound_buf);

// Called by main() on the way out. A looping sample would otherwise keep
// playing after exit -- out of a buffer GEMDOS has just taken back.
void atari_sound_shutdown(void) {
    uint32_t snd = 0;
    if (atari_get_cookie("_SND", &snd) && (snd & 2)) {
        Supexec(snd_do_stop);
    }
}
