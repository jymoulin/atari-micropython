// The 'machine' module for the Atari port: I2C on the TwiSTer / MonSTer bus.
//
// The board exposes three 16-bit registers in I/O space:
//
//   0xFFFFFE02  direction   bit 0 = SCL, bit 1 = SDA, 1 = input
//   0xFFFFFE04  SCL         write 0/1 to drive, read to sense
//   0xFFFFFE06  SDA         likewise
//
// I/O space is supervisor-only on every ST, so the bus cannot be touched from
// user mode. Rather than a Supexec per bit -- ruinous under Geneva, where a
// trap costs real time -- the whole transfer runs inside ONE Supexec, and the
// pin primitives below assume they are already in supervisor mode.
//
// The protocol itself is extmod/machine_i2c.c: start, stop, ACK, NACK, clock
// stretching, scan(), readfrom_mem() and the rest. This file only supplies
// the five primitives it needs.

#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "extmod/modmachine.h"

#include <mint/osbind.h>

#define I2C_DIR     (*(volatile uint16_t *)0xFFFFFE02L)
#define I2C_SCL     (*(volatile uint16_t *)0xFFFFFE04L)
#define I2C_SDA     (*(volatile uint16_t *)0xFFFFFE06L)

#define DIR_SCL_IN  0x0001
#define DIR_SDA_IN  0x0002

// Line identifiers, as passed around by the SoftI2C code.
#define PIN_SCL     0
#define PIN_SDA     1

// What we last drove onto each line, and the current direction register.
// Reading back a line we are driving is not guaranteed to work on this board,
// so the shadow is what we answer with when it does not.
static uint16_t i2c_dir_shadow = DIR_SCL_IN | DIR_SDA_IN;
static uint8_t i2c_level[2] = { 1, 1 };
static uint8_t i2c_scl_readable = 0;

// SDA is a true open-drain line: released means switched to input, and the
// bus pull-ups bring it high. That the existing RTC driver reads data back
// this way is proof enough that the pull-ups are there.
//
// SCL is driven both ways by default, which is what that same driver does.
// If the probe at init finds SCL readable, we release it instead and the
// clock stretching in extmod/machine_i2c.c starts working for free.

void mp_hal_pin_open_drain(mp_hal_pin_obj_t pin) {
    i2c_dir_shadow |= (pin == PIN_SCL) ? DIR_SCL_IN : DIR_SDA_IN;
    I2C_DIR = i2c_dir_shadow;
    i2c_level[pin] = 1;
}

void mp_hal_pin_od_low(mp_hal_pin_obj_t pin) {
    i2c_dir_shadow &= (pin == PIN_SCL) ? ~DIR_SCL_IN : ~DIR_SDA_IN;
    I2C_DIR = i2c_dir_shadow;
    if (pin == PIN_SCL) {
        I2C_SCL = 0;
    } else {
        I2C_SDA = 0;
    }
    i2c_level[pin] = 0;
}

void mp_hal_pin_od_high(mp_hal_pin_obj_t pin) {
    if (pin == PIN_SDA || i2c_scl_readable) {
        // Release and let the pull-up do the work.
        i2c_dir_shadow |= (pin == PIN_SCL) ? DIR_SCL_IN : DIR_SDA_IN;
        I2C_DIR = i2c_dir_shadow;
    } else {
        // Drive the clock high actively.
        i2c_dir_shadow &= ~DIR_SCL_IN;
        I2C_DIR = i2c_dir_shadow;
        I2C_SCL = 1;
    }
    i2c_level[pin] = 1;
}

int mp_hal_pin_read(mp_hal_pin_obj_t pin) {
    if (pin == PIN_SCL && !i2c_scl_readable) {
        // Not readable on this board: answer what we are driving, so that
        // the clock-stretching loop sees the clock as released and moves on.
        return i2c_level[PIN_SCL];
    }
    uint16_t dirbit = (pin == PIN_SCL) ? DIR_SCL_IN : DIR_SDA_IN;
    if (!(i2c_dir_shadow & dirbit)) {
        i2c_dir_shadow |= dirbit;
        I2C_DIR = i2c_dir_shadow;
    }
    return ((pin == PIN_SCL) ? I2C_SCL : I2C_SDA) & 1;
}

// ---------------------------------------------------------------------------
// Supervisor wrappers
// ---------------------------------------------------------------------------

// Is SCL readable as an input? If it is, clock stretching works and slow
// devices can hold the master off; if not, we drive the clock and hope no
// device on this bus ever needs to stretch it. Point 3 of the open questions
// in README.txt, answered by the machine rather than by me.
static long probe_scl(void) {
    uint16_t saved = I2C_DIR;

    I2C_DIR = DIR_SCL_IN | DIR_SDA_IN;      // let both float
    mp_hal_delay_us_fast(10);
    int high = I2C_SCL & 1;

    I2C_DIR = DIR_SDA_IN;                   // SCL as output
    I2C_SCL = 0;
    mp_hal_delay_us_fast(10);
    int low = I2C_SCL & 1;

    I2C_DIR = DIR_SCL_IN | DIR_SDA_IN;
    mp_hal_delay_us_fast(10);
    I2C_DIR = saved;

    // Readable means the pin follows what the bus does: high when released,
    // low when we pull it down.
    i2c_scl_readable = (high == 1 && low == 0);
    return 0;
}

static long init_bus(void) {
    i2c_dir_shadow = DIR_SCL_IN | DIR_SDA_IN;
    I2C_DIR = i2c_dir_shadow;
    i2c_level[0] = i2c_level[1] = 1;
    return 0;
}

// The whole transfer, in one trip to supervisor mode.
static mp_obj_base_t *xfer_self;
static uint16_t xfer_addr;
static size_t xfer_n;
static mp_machine_i2c_buf_t *xfer_bufs;
static unsigned int xfer_flags;
static int xfer_ret;

static long do_transfer(void) {
    xfer_ret = mp_machine_soft_i2c_transfer(xfer_self, xfer_addr, xfer_n,
                                            xfer_bufs, xfer_flags);
    return 0;
}

static int atari_i2c_transfer(mp_obj_base_t *self, uint16_t addr, size_t n,
                              mp_machine_i2c_buf_t *bufs, unsigned int flags) {
    xfer_self = self;
    xfer_addr = addr;
    xfer_n = n;
    xfer_bufs = bufs;
    xfer_flags = flags;
    Supexec(do_transfer);
    return xfer_ret;
}

// ---------------------------------------------------------------------------
// The type
// ---------------------------------------------------------------------------

extern const mp_obj_type_t atari_i2c_type;

static void atari_i2c_print(const mp_print_t *print, mp_obj_t self_in,
                            mp_print_kind_t kind) {
    mp_machine_soft_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    (void)kind;
    mp_printf(print, "I2C(TwiSTer, freq=%u, SCL %s)",
              500000 / (self->us_delay ? self->us_delay : 1),
              i2c_scl_readable ? "readable, clock stretching on"
                               : "write-only, no clock stretching");
}

static mp_obj_t atari_i2c_make_new(const mp_obj_type_t *type, size_t n_args,
                                   size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_freq, ARG_timeout };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_freq, MP_ARG_INT, {.u_int = 100000} },
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 50000} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
                              MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_machine_soft_i2c_obj_t *self = mp_obj_malloc(mp_machine_soft_i2c_obj_t,
                                                    &atari_i2c_type);
    self->scl = PIN_SCL;
    self->sda = PIN_SDA;
    self->us_timeout = args[ARG_timeout].u_int;

    // Half a clock period, as SoftI2C counts it. One microsecond is the floor:
    // the calibrated busy-wait cannot do better, and on a 24 MHz 68020 the
    // register accesses themselves already cost more than that.
    mp_int_t freq = args[ARG_freq].u_int;
    if (freq <= 0) {
        freq = 100000;
    }
    self->us_delay = 500000 / freq;
    if (self->us_delay == 0) {
        self->us_delay = 1;
    }

    Supexec(init_bus);
    Supexec(probe_scl);
    Supexec(init_bus);
    return MP_OBJ_FROM_PTR(self);
}

// scl_readable is worth exposing: it decides whether clock stretching works,
// and it is exactly the hardware question this port could not answer on paper.
static mp_obj_t atari_i2c_scl_readable(mp_obj_t self_in) {
    (void)self_in;
    return mp_obj_new_bool(i2c_scl_readable);
}
static MP_DEFINE_CONST_FUN_OBJ_1(atari_i2c_scl_readable_obj, atari_i2c_scl_readable);

static const mp_machine_i2c_p_t atari_i2c_p = {
    .transfer = atari_i2c_transfer,
};

MP_DEFINE_CONST_OBJ_TYPE(
    atari_i2c_type,
    MP_QSTR_I2C,
    MP_TYPE_FLAG_NONE,
    make_new, atari_i2c_make_new,
    print, atari_i2c_print,
    protocol, &atari_i2c_p,
    locals_dict, &mp_machine_i2c_locals_dict
    );

// ---------------------------------------------------------------------------

// Defined in machine_mem.c and machine_uart.c.
extern const mp_obj_type_t machine_uart_type;
extern const struct _machine_mem_obj_t machine_mem8_obj;
extern const struct _machine_mem_obj_t machine_mem16_obj;
extern const struct _machine_mem_obj_t machine_mem32_obj;
extern const mp_obj_fun_builtin_fixed_t machine_mem_buffer_obj;
extern const mp_obj_fun_builtin_fixed_t machine_memset_obj;
extern const mp_obj_fun_builtin_fixed_t machine_memcpy_obj;
extern const mp_obj_fun_builtin_fixed_t machine_physbase_obj;
extern const mp_obj_fun_builtin_fixed_t machine_logbase_obj;
extern const mp_obj_fun_builtin_fixed_t machine_getrez_obj;
extern const mp_obj_fun_builtin_fixed_t machine_vsync_obj;
extern const mp_obj_fun_builtin_fixed_t machine_screen_info_obj;
extern const mp_obj_fun_builtin_fixed_t machine_setcolor_obj;
extern const mp_obj_fun_builtin_fixed_t machine_setpalette_obj;
extern const mp_obj_fun_builtin_var_t machine_kbshift_obj;
extern const mp_obj_fun_builtin_fixed_t machine_random_obj;
extern const mp_obj_fun_builtin_fixed_t machine_dosound_obj;
extern const mp_obj_fun_builtin_var_t machine_giaccess_obj;
extern const mp_obj_fun_builtin_fixed_t machine_stram_obj;
extern const mp_obj_fun_builtin_var_t machine_dmaplay_obj;
extern const mp_obj_fun_builtin_fixed_t machine_dmastop_obj;
extern const mp_obj_fun_builtin_fixed_t machine_dmapos_obj;
extern const mp_obj_fun_builtin_fixed_t machine_getbpb_obj;
extern const mp_obj_fun_builtin_fixed_t machine_mediach_obj;
extern const mp_obj_fun_builtin_var_t machine_disk_read_obj;

static const mp_rom_map_elem_t machine_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_machine) },

    { MP_ROM_QSTR(MP_QSTR_I2C), MP_ROM_PTR(&atari_i2c_type) },
    { MP_ROM_QSTR(MP_QSTR_i2c_scl_readable), MP_ROM_PTR(&atari_i2c_scl_readable_obj) },
    { MP_ROM_QSTR(MP_QSTR_UART), MP_ROM_PTR(&machine_uart_type) },

    { MP_ROM_QSTR(MP_QSTR_mem8), MP_ROM_PTR(&machine_mem8_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem16), MP_ROM_PTR(&machine_mem16_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem32), MP_ROM_PTR(&machine_mem32_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem_buffer), MP_ROM_PTR(&machine_mem_buffer_obj) },
    { MP_ROM_QSTR(MP_QSTR_memset), MP_ROM_PTR(&machine_memset_obj) },
    { MP_ROM_QSTR(MP_QSTR_memcpy), MP_ROM_PTR(&machine_memcpy_obj) },

    { MP_ROM_QSTR(MP_QSTR_Physbase), MP_ROM_PTR(&machine_physbase_obj) },
    { MP_ROM_QSTR(MP_QSTR_Logbase), MP_ROM_PTR(&machine_logbase_obj) },
    { MP_ROM_QSTR(MP_QSTR_Getrez), MP_ROM_PTR(&machine_getrez_obj) },
    { MP_ROM_QSTR(MP_QSTR_Vsync), MP_ROM_PTR(&machine_vsync_obj) },
    { MP_ROM_QSTR(MP_QSTR_screen_info), MP_ROM_PTR(&machine_screen_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_Setcolor), MP_ROM_PTR(&machine_setcolor_obj) },
    { MP_ROM_QSTR(MP_QSTR_Setpalette), MP_ROM_PTR(&machine_setpalette_obj) },

    { MP_ROM_QSTR(MP_QSTR_Kbshift), MP_ROM_PTR(&machine_kbshift_obj) },
    { MP_ROM_QSTR(MP_QSTR_Random), MP_ROM_PTR(&machine_random_obj) },
    { MP_ROM_QSTR(MP_QSTR_Dosound), MP_ROM_PTR(&machine_dosound_obj) },
    { MP_ROM_QSTR(MP_QSTR_Giaccess), MP_ROM_PTR(&machine_giaccess_obj) },

    { MP_ROM_QSTR(MP_QSTR_stram), MP_ROM_PTR(&machine_stram_obj) },
    { MP_ROM_QSTR(MP_QSTR_dmaplay), MP_ROM_PTR(&machine_dmaplay_obj) },
    { MP_ROM_QSTR(MP_QSTR_dmastop), MP_ROM_PTR(&machine_dmastop_obj) },
    { MP_ROM_QSTR(MP_QSTR_dmapos), MP_ROM_PTR(&machine_dmapos_obj) },

    { MP_ROM_QSTR(MP_QSTR_Getbpb), MP_ROM_PTR(&machine_getbpb_obj) },
    { MP_ROM_QSTR(MP_QSTR_Mediach), MP_ROM_PTR(&machine_mediach_obj) },
    { MP_ROM_QSTR(MP_QSTR_disk_read), MP_ROM_PTR(&machine_disk_read_obj) },
};
static MP_DEFINE_CONST_DICT(machine_module_globals, machine_module_globals_table);

const mp_obj_module_t mp_module_machine = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&machine_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_machine, mp_module_machine);
