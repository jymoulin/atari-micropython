// machine.mem8/16/32, machine.mem_buffer, and the screen and XBIOS bits.
//
// The point of this file is access: being able to read and write any address
// from the REPL, and to hand a slice of raw memory to Python as a buffer.
// That is what turns the interpreter into an instrument for exploring a
// machine -- the TwiSTer's USB controller, a cartridge, the shifter -- without
// recompiling anything.

#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/objarray.h"

#include <mint/osbind.h>

// Two regions need supervisor mode on every ST: the system variables below
// 0x800, and the I/O space from 0xFF8000 up. Everything else is plain RAM and
// is reached directly, which matters -- a trap per access would make a loop
// over screen memory unusable.
#define NEEDS_SUPER(a)  ((a) < 0x800UL || (a) >= 0xFF8000UL)

static unsigned long peek_addr;
static unsigned long peek_val;
static int peek_size;

static long do_peek(void) {
    switch (peek_size) {
        case 1: peek_val = *(volatile uint8_t *)peek_addr; break;
        case 2: peek_val = *(volatile uint16_t *)peek_addr; break;
        default: peek_val = *(volatile uint32_t *)peek_addr; break;
    }
    return 0;
}

static long do_poke(void) {
    switch (peek_size) {
        case 1: *(volatile uint8_t *)peek_addr = (uint8_t)peek_val; break;
        case 2: *(volatile uint16_t *)peek_addr = (uint16_t)peek_val; break;
        default: *(volatile uint32_t *)peek_addr = (uint32_t)peek_val; break;
    }
    return 0;
}

typedef struct _machine_mem_obj_t {
    mp_obj_base_t base;
    unsigned int elem_size;     // 1, 2 or 4
} machine_mem_obj_t;

static void machine_mem_print(const mp_print_t *print, mp_obj_t self_in,
                              mp_print_kind_t kind) {
    (void)kind;
    machine_mem_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<mem%u>", self->elem_size * 8);
}

static mp_obj_t machine_mem_subscr(mp_obj_t self_in, mp_obj_t index,
                                   mp_obj_t value) {
    machine_mem_obj_t *self = MP_OBJ_TO_PTR(self_in);
    unsigned long addr = (unsigned long)mp_obj_get_int_truncated(index);

    if (value == MP_OBJ_NULL) {
        return MP_OBJ_NULL;     // deletion is not a thing here
    }

    peek_addr = addr;
    peek_size = self->elem_size;

    if (value == MP_OBJ_SENTINEL) {
        if (NEEDS_SUPER(addr)) {
            Supexec(do_peek);
        } else {
            do_peek();
        }
        return mp_obj_new_int_from_uint(peek_val);
    }

    peek_val = (unsigned long)mp_obj_get_int_truncated(value);
    if (NEEDS_SUPER(addr)) {
        Supexec(do_poke);
    } else {
        do_poke();
    }
    return mp_const_none;
}

MP_DEFINE_CONST_OBJ_TYPE(
    machine_mem_type,
    MP_QSTR_mem,
    MP_TYPE_FLAG_NONE,
    print, machine_mem_print,
    subscr, machine_mem_subscr
    );

const machine_mem_obj_t machine_mem8_obj = { { &machine_mem_type }, 1 };
const machine_mem_obj_t machine_mem16_obj = { { &machine_mem_type }, 2 };
const machine_mem_obj_t machine_mem32_obj = { { &machine_mem_type }, 4 };

// machine.mem_buffer(addr, length) -> a bytearray sharing that memory.
// No copy: writing to it writes there. Screen memory handed to framebuf is
// the whole reason this exists.
static mp_obj_t machine_mem_buffer(mp_obj_t addr_in, mp_obj_t len_in) {
    unsigned long addr = (unsigned long)mp_obj_get_int_truncated(addr_in);
    mp_int_t len = mp_obj_get_int(len_in);
    if (len <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("length must be positive"));
    }
    if (NEEDS_SUPER(addr)) {
        // A buffer is handed out for direct access, with no chance to switch
        // to supervisor on the way in. Refusing is better than a bus error
        // the first time Python touches it.
        mp_raise_ValueError(MP_ERROR_TEXT("address needs supervisor mode"));
    }
    return mp_obj_new_bytearray_by_ref((size_t)len, (void *)addr);
}
MP_DEFINE_CONST_FUN_OBJ_2(machine_mem_buffer_obj, machine_mem_buffer);

// Bulk memory moves, because Python cannot do these at any reasonable speed.
// framebuf draws mono pixels one at a time -- 530 ms to clear a 640x400 screen
// on a 24 MHz 68020 -- whereas a memset of the same 32000 bytes is a couple of
// milliseconds. Use framebuf for shapes and these for wholesale work:
// clearing, double buffering, saving the screen before scribbling on it.
//
// Both accept either an integer address or any buffer object.
static void *addr_or_buffer(mp_obj_t obj, mp_uint_t len, int writable) {
    if (mp_obj_is_int(obj)) {
        unsigned long a = (unsigned long)mp_obj_get_int_truncated(obj);
        if (NEEDS_SUPER(a)) {
            mp_raise_ValueError(MP_ERROR_TEXT("address needs supervisor mode"));
        }
        return (void *)a;
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(obj, &bufinfo, writable ? MP_BUFFER_WRITE : MP_BUFFER_READ);
    if (bufinfo.len < len) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer too small"));
    }
    return bufinfo.buf;
}

static mp_obj_t machine_memset(mp_obj_t dst_in, mp_obj_t val_in, mp_obj_t len_in) {
    mp_uint_t len = (mp_uint_t)mp_obj_get_int(len_in);
    void *dst = addr_or_buffer(dst_in, len, 1);
    memset(dst, mp_obj_get_int(val_in) & 0xFF, len);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(machine_memset_obj, machine_memset);

static mp_obj_t machine_memcpy(mp_obj_t dst_in, mp_obj_t src_in, mp_obj_t len_in) {
    mp_uint_t len = (mp_uint_t)mp_obj_get_int(len_in);
    void *dst = addr_or_buffer(dst_in, len, 1);
    void *src = addr_or_buffer(src_in, len, 0);
    memmove(dst, src, len);     // memmove: the screen and a buffer may overlap
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(machine_memcpy_obj, machine_memcpy);

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

static mp_obj_t machine_physbase(void) {
    return mp_obj_new_int_from_uint((mp_uint_t)Physbase());
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_physbase_obj, machine_physbase);

static mp_obj_t machine_logbase(void) {
    return mp_obj_new_int_from_uint((mp_uint_t)Logbase());
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_logbase_obj, machine_logbase);

static mp_obj_t machine_getrez(void) {
    return mp_obj_new_int(Getrez());
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_getrez_obj, machine_getrez);

static mp_obj_t machine_vsync(void) {
    Vsync();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_vsync_obj, machine_vsync);

// Screen geometry for the current resolution: (width, height, planes, bytes).
// Mono is one plane, which is exactly what framebuf.MONO_HMSB expects; the
// colour modes interleave their planes word by word and framebuf cannot draw
// into that, so this is also how Python finds out whether it can.
static mp_obj_t machine_screen_info(void) {
    int rez = Getrez();
    int w, h, planes;
    switch (rez) {
        case 0:  w = 320; h = 200; planes = 4; break;   // low
        case 1:  w = 640; h = 200; planes = 2; break;   // medium
        default: w = 640; h = 400; planes = 1; break;   // high (mono)
    }
    mp_obj_t t[4] = {
        MP_OBJ_NEW_SMALL_INT(w),
        MP_OBJ_NEW_SMALL_INT(h),
        MP_OBJ_NEW_SMALL_INT(planes),
        MP_OBJ_NEW_SMALL_INT(w * h * planes / 8),
    };
    return mp_obj_new_tuple(4, t);
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_screen_info_obj, machine_screen_info);

// setcolor(index, value): 0x0RGB, three bits per gun on an ST, four on an STE.
// Returns the previous value.
static mp_obj_t machine_setcolor(mp_obj_t idx_in, mp_obj_t col_in) {
    int idx = mp_obj_get_int(idx_in);
    int col = mp_obj_get_int(col_in);
    return mp_obj_new_int(Setcolor(idx, col));
}
MP_DEFINE_CONST_FUN_OBJ_2(machine_setcolor_obj, machine_setcolor);

static mp_obj_t machine_setpalette(mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len < 32) {
        mp_raise_ValueError(MP_ERROR_TEXT("palette needs 16 words"));
    }
    Setpalette(bufinfo.buf);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_setpalette_obj, machine_setpalette);

// ---------------------------------------------------------------------------
// Keyboard, sound, and a few odds and ends
// ---------------------------------------------------------------------------

// Bit 0 right shift, 1 left shift, 2 control, 3 alt, 4 caps lock.
static mp_obj_t machine_kbshift(size_t n_args, const mp_obj_t *args) {
    int mode = (n_args > 0) ? mp_obj_get_int(args[0]) : -1;
    return mp_obj_new_int(Kbshift(mode));
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_kbshift_obj, 0, 1, machine_kbshift);

static mp_obj_t machine_random(void) {
    return mp_obj_new_int_from_uint((mp_uint_t)Random());
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_random_obj, machine_random);

// Sound, the cheap half: hand the YM2149 a sound macro and let it play.
static mp_obj_t machine_dosound(mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    Dosound(bufinfo.buf);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_dosound_obj, machine_dosound);

// giaccess(reg) reads a YM2149 register, giaccess(reg, value) writes one.
// Registers 0-13: tone periods, noise, mixer, volumes, envelope.
static mp_obj_t machine_giaccess(size_t n_args, const mp_obj_t *args) {
    int reg = mp_obj_get_int(args[0]);
    if (n_args > 1) {
        int val = mp_obj_get_int(args[1]);
        return mp_obj_new_int(Giaccess(val, reg | 0x80));
    }
    return mp_obj_new_int(Giaccess(0, reg) & 0xFF);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_giaccess_obj, 1, 2, machine_giaccess);
