// machine.UART -- the serial port and MIDI, through BIOS and XBIOS.
//
// BIOS character devices: 0 = printer, 1 = AUX (RS-232), 2 = console,
// 3 = MIDI. Reading goes through Bconstat/Bconin one character at a time,
// which is what the BIOS offers. Writing does NOT: Bconout per character
// would be painfully slow, so AUX goes out through GEMDOS handle 2 and MIDI
// through XBIOS Midiws, both of which take a whole string.

#include <string.h>

#include "py/runtime.h"
#include "py/stream.h"
#include "py/mperrno.h"
#include "py/mphal.h"

#include <mint/osbind.h>

#define DEV_PRT     0
#define DEV_AUX     1
#define DEV_CON     2
#define DEV_MIDI    3

typedef struct _machine_uart_obj_t {
    mp_obj_base_t base;
    uint8_t dev;
    uint16_t timeout_ms;
} machine_uart_obj_t;

extern const mp_obj_type_t machine_uart_type;

// Rsconf speed codes, from the MFP's divider. Index = what XBIOS wants.
static const uint16_t uart_bauds[] = {
    19200, 9600, 4800, 3600, 2400, 2000, 1800, 1200,
    600, 300, 200, 150, 134, 110, 75, 50,
};

static int baud_to_code(mp_int_t baud) {
    for (unsigned i = 0; i < MP_ARRAY_SIZE(uart_bauds); i++) {
        if (uart_bauds[i] == baud) {
            return (int)i;
        }
    }
    return -1;
}

// The MFP 68901 USART control register, built from the usual Python-side
// arguments. Bit 7 asks for the divide-by-16 clock, which is what every
// standard rate assumes.
static int make_ucr(mp_int_t bits, char parity, mp_int_t stop) {
    int ucr = 0x80;
    switch (bits) {
        case 8: ucr |= 0x00; break;
        case 7: ucr |= 0x20; break;
        case 6: ucr |= 0x40; break;
        case 5: ucr |= 0x60; break;
        default: mp_raise_ValueError(MP_ERROR_TEXT("bits must be 5..8"));
    }
    if (parity == 'E') {
        ucr |= 0x06;            // parity enabled, even
    } else if (parity == 'O') {
        ucr |= 0x04;            // parity enabled, odd
    }
    if (stop == 1) {
        ucr |= 0x08;
    } else if (stop == 2) {
        ucr |= 0x18;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("stop must be 1 or 2"));
    }
    return ucr;
}

static void uart_configure(machine_uart_obj_t *self, size_t n_args,
                           const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_baudrate, ARG_bits, ARG_parity, ARG_stop, ARG_flow, ARG_timeout };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_bits, MP_ARG_INT, {.u_int = 8} },
        { MP_QSTR_parity, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_stop, MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_flow, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_timeout, MP_ARG_INT, {.u_int = 1000} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
                     MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    self->timeout_ms = (uint16_t)args[ARG_timeout].u_int;

    if (self->dev != DEV_AUX) {
        // MIDI runs at 31250 baud by definition and the printer port has no
        // rate of its own; only the RS-232 is configurable.
        return;
    }

    int speed = -1;
    if (args[ARG_baudrate].u_int > 0) {
        speed = baud_to_code(args[ARG_baudrate].u_int);
        if (speed < 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("unsupported baudrate"));
        }
    }

    char parity = 'N';
    if (args[ARG_parity].u_obj != mp_const_none) {
        const char *p = mp_obj_str_get_str(args[ARG_parity].u_obj);
        parity = (p[0] >= 'a') ? (char)(p[0] - 32) : p[0];
    }
    int ucr = make_ucr(args[ARG_bits].u_int, parity, args[ARG_stop].u_int);

    // -1 means "leave this one alone" for every Rsconf argument.
    Rsconf(speed, args[ARG_flow].u_int, ucr, -1, -1, -1);
}

static mp_obj_t machine_uart_make_new(const mp_obj_type_t *type, size_t n_args,
                                      size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_check_num(n_args, n_kw, 0, MP_OBJ_FUN_ARGS_MAX, true);

    int dev = DEV_AUX;
    if (n_args > 0) {
        dev = mp_obj_get_int(all_args[0]);
        if (dev < 0 || dev > 3) {
            mp_raise_ValueError(MP_ERROR_TEXT("device must be 0..3"));
        }
        n_args--;
        all_args++;
    }

    machine_uart_obj_t *self = mp_obj_malloc(machine_uart_obj_t, &machine_uart_type);
    self->dev = (uint8_t)dev;
    self->timeout_ms = 1000;

    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, all_args + n_args);
    uart_configure(self, n_args, all_args, &kw_args);
    return MP_OBJ_FROM_PTR(self);
}

static void machine_uart_print(const mp_print_t *print, mp_obj_t self_in,
                               mp_print_kind_t kind) {
    (void)kind;
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    static const char *names[] = { "printer", "AUX", "console", "MIDI" };
    mp_printf(print, "UART(%d, %s, timeout=%u)",
              self->dev, names[self->dev], self->timeout_ms);
}

static mp_obj_t machine_uart_init(size_t n_args, const mp_obj_t *args,
                                  mp_map_t *kw_args) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uart_configure(self, n_args - 1, args + 1, kw_args);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_uart_init_obj, 1, machine_uart_init);

// How many characters are waiting. Bconstat answers "some" or "none", not a
// count, so this is 0 or 1 -- enough to drive a polling loop.
static mp_obj_t machine_uart_any(mp_obj_t self_in) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(Bconstat(self->dev) ? 1 : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_uart_any_obj, machine_uart_any);

static mp_uint_t machine_uart_read(mp_obj_t self_in, void *buf_in, mp_uint_t size,
                                   int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t *buf = buf_in;
    mp_uint_t n = 0;
    uint32_t deadline = mp_hal_ticks_ms() + self->timeout_ms;

    while (n < size) {
        if (Bconstat(self->dev)) {
            buf[n++] = (uint8_t)(Bconin(self->dev) & 0xFF);
            continue;
        }
        if ((int32_t)(mp_hal_ticks_ms() - deadline) >= 0) {
            break;
        }
        // Let Ctrl-C through while we wait, and give the rest of the machine
        // a chance -- a tight poll loop under Geneva starves everything else.
        mp_atari_check_signals();
        if (MP_STATE_THREAD(mp_pending_exception) != MP_OBJ_NULL) {
            break;
        }
    }

    if (n == 0) {
        *errcode = MP_EAGAIN;
        return MP_STREAM_ERROR;
    }
    return n;
}

static mp_uint_t machine_uart_write(mp_obj_t self_in, const void *buf_in,
                                    mp_uint_t size, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    const char *buf = buf_in;
    (void)errcode;

    if (self->dev == DEV_AUX) {
        // GEMDOS handle 2 is AUX, and Fwrite takes the whole string.
        long written = Fwrite(2, (long)size, buf);
        return (written < 0) ? 0 : (mp_uint_t)written;
    }
    if (self->dev == DEV_MIDI) {
        // Midiws takes count-1, as the XBIOS has always done.
        Midiws((int)size - 1, buf);
        return size;
    }
    for (mp_uint_t i = 0; i < size; i++) {
        Bconout(self->dev, (unsigned char)buf[i]);
    }
    return size;
}

static mp_uint_t machine_uart_ioctl(mp_obj_t self_in, mp_uint_t request,
                                    uintptr_t arg, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (request == MP_STREAM_POLL) {
        mp_uint_t ret = 0;
        if ((arg & MP_STREAM_POLL_RD) && Bconstat(self->dev)) {
            ret |= MP_STREAM_POLL_RD;
        }
        if ((arg & MP_STREAM_POLL_WR) && Bcostat(self->dev)) {
            ret |= MP_STREAM_POLL_WR;
        }
        return ret;
    }
    *errcode = MP_EINVAL;
    return MP_STREAM_ERROR;
}

static const mp_rom_map_elem_t machine_uart_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_uart_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_any), MP_ROM_PTR(&machine_uart_any_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },

    { MP_ROM_QSTR(MP_QSTR_PRINTER), MP_ROM_INT(DEV_PRT) },
    { MP_ROM_QSTR(MP_QSTR_AUX), MP_ROM_INT(DEV_AUX) },
    { MP_ROM_QSTR(MP_QSTR_CONSOLE), MP_ROM_INT(DEV_CON) },
    { MP_ROM_QSTR(MP_QSTR_MIDI), MP_ROM_INT(DEV_MIDI) },
};
static MP_DEFINE_CONST_DICT(machine_uart_locals_dict, machine_uart_locals_dict_table);

static const mp_stream_p_t machine_uart_stream_p = {
    .read = machine_uart_read,
    .write = machine_uart_write,
    .ioctl = machine_uart_ioctl,
    .is_text = false,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_uart_type,
    MP_QSTR_UART,
    MP_TYPE_FLAG_NONE,
    make_new, machine_uart_make_new,
    print, machine_uart_print,
    protocol, &machine_uart_stream_p,
    locals_dict, &machine_uart_locals_dict
    );
