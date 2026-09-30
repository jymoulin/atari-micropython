// _gem: the VDI and AES, through the MiNTlib's gemlib.
//
// Deliberately thin. Two generic entry points, vdi() and aes(), reach every
// call either layer has, with the parameter arrays passed as tuples. On top
// of that, only the calls where C earns its place: those that carry strings
// (charset conversion, buffers the AES writes into) and the workstation
// setup. Everything ergonomic -- a Screen object, alert(), fileselect() --
// lives in the frozen gem.py, where it is easy to read and change.
//
// Strings cross the boundary in the Atari ST character set, converted from
// and to UTF-8 as the console and the filesystem already do.

#include <string.h>

#include "py/runtime.h"
#include "py/objtuple.h"
#include "py/objstr.h"
#include "py/mperrno.h"
#include "mphalport.h"

#include <gem.h>
#include <mint/osbind.h>

// ---------------------------------------------------------------------------
// Helpers

// Fill a short array from a tuple/list of ints. Returns the count.
static size_t shorts_from(mp_obj_t seq, short *dst, size_t max, const char *what) {
    if (seq == MP_OBJ_NULL || seq == mp_const_none) {
        return 0;
    }
    size_t n;
    mp_obj_t *items;
    mp_obj_get_array(seq, &n, &items);
    if (n > max) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("too many values in %s"), what);
    }
    for (size_t i = 0; i < n; i++) {
        dst[i] = (short)mp_obj_get_int(items[i]);
    }
    return n;
}

static mp_obj_t tuple_of_shorts(const short *src, size_t n) {
    mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(n, NULL));
    for (size_t i = 0; i < n; i++) {
        t->items[i] = MP_OBJ_NEW_SMALL_INT(src[i]);
    }
    return MP_OBJ_FROM_PTR(t);
}

// UTF-8 Python string -> ST charset, NUL-terminated, in buf.
static const char *st_string(mp_obj_t obj, char *buf, size_t size) {
    const char *utf8 = mp_obj_str_get_str(obj);
    const char *st = atari_utf8_to_st(utf8, buf, size);
    if (st != buf) {            // pure ASCII comes back unconverted
        size_t len = strlen(st);
        if (len >= size) {
            mp_raise_ValueError(MP_ERROR_TEXT("string too long"));
        }
        memcpy(buf, st, len + 1);
    }
    return buf;
}

static mp_obj_t py_string(const char *st) {
    char buf[512];
    mp_uint_t n = atari_st_to_utf8(st, strlen(st), buf, sizeof(buf));
    return mp_obj_new_str(buf, n);
}

// ---------------------------------------------------------------------------
// Generic VDI: _gem.vdi(opcode, handle, intin=(), ptsin=(), subop=0)
//   -> (intout, ptsout, handle)
// ptsin is a flat sequence x0, y0, x1, y1, ... The counts come back from the
// driver in control[], so the result always has the right length. handle is
// returned because v_opnvwk and friends hand a new one back in control[6].

#define VDI_INTIN_MAX   256
#define VDI_PTSIN_MAX   1024        // 512 points
#define VDI_INTOUT_MAX  256
#define VDI_PTSOUT_MAX  256

static short vdi_control[12];
static short vdi_intin[VDI_INTIN_MAX];
static short vdi_ptsin[VDI_PTSIN_MAX];
static short vdi_intout[VDI_INTOUT_MAX];
static short vdi_ptsout[VDI_PTSOUT_MAX];

static mp_obj_t gem_vdi(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_opcode, ARG_handle, ARG_intin, ARG_ptsin, ARG_subop };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_opcode, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_handle, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_intin, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_ptsin, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_subop, MP_ARG_INT, {.u_int = 0} },
    };
    mp_arg_val_t a[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed), allowed, a);

    size_t n_int = shorts_from(a[ARG_intin].u_obj, vdi_intin, VDI_INTIN_MAX, "intin");
    size_t n_pts = shorts_from(a[ARG_ptsin].u_obj, vdi_ptsin, VDI_PTSIN_MAX, "ptsin");
    if (n_pts & 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("ptsin needs x, y pairs"));
    }

    memset(vdi_control, 0, sizeof(vdi_control));
    vdi_control[0] = (short)a[ARG_opcode].u_int;
    vdi_control[1] = (short)(n_pts / 2);
    vdi_control[3] = (short)n_int;
    vdi_control[5] = (short)a[ARG_subop].u_int;
    vdi_control[6] = (short)a[ARG_handle].u_int;

    VDIPB pb = { vdi_control, vdi_intin, vdi_ptsin, vdi_intout, vdi_ptsout };
    vdi(&pb);

    size_t n_ptsout = (size_t)(unsigned short)vdi_control[2] * 2;
    size_t n_intout = (size_t)(unsigned short)vdi_control[4];
    if (n_ptsout > VDI_PTSOUT_MAX) {
        n_ptsout = VDI_PTSOUT_MAX;
    }
    if (n_intout > VDI_INTOUT_MAX) {
        n_intout = VDI_INTOUT_MAX;
    }
    mp_obj_t r[3] = {
        tuple_of_shorts(vdi_intout, n_intout),
        tuple_of_shorts(vdi_ptsout, n_ptsout),
        MP_OBJ_NEW_SMALL_INT(vdi_control[6]),
    };
    return mp_obj_new_tuple(3, r);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(gem_vdi_obj, 2, gem_vdi);

// ---------------------------------------------------------------------------
// Generic AES: _gem.aes(opcode, intin=(), addrin=(), n_intout=1, n_addrout=0)
//   -> (intout, addrout)
// addrin holds raw addresses. For anything that needs a string or a buffer,
// use the dedicated calls below: an address into a Python object is only
// safe while that object is alive, and nothing here can guarantee it.

static short aes_control[5];
static short aes_intin[16];
static short aes_intout[16];
static long aes_addrin[16];
static long aes_addrout[16];

static mp_obj_t gem_aes(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_opcode, ARG_intin, ARG_addrin, ARG_n_intout, ARG_n_addrout };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_opcode, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_intin, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_addrin, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_n_intout, MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_n_addrout, MP_ARG_INT, {.u_int = 0} },
    };
    mp_arg_val_t a[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed), allowed, a);

    size_t n_int = shorts_from(a[ARG_intin].u_obj, aes_intin, 16, "intin");
    size_t n_addr = 0;
    if (a[ARG_addrin].u_obj != mp_const_none) {
        mp_obj_t *items;
        mp_obj_get_array(a[ARG_addrin].u_obj, &n_addr, &items);
        if (n_addr > 16) {
            mp_raise_ValueError(MP_ERROR_TEXT("too many values in addrin"));
        }
        for (size_t i = 0; i < n_addr; i++) {
            aes_addrin[i] = (long)mp_obj_get_int_truncated(items[i]);
        }
    }
    mp_int_t n_intout = a[ARG_n_intout].u_int;
    mp_int_t n_addrout = a[ARG_n_addrout].u_int;
    if (n_intout < 0 || n_intout > 16 || n_addrout < 0 || n_addrout > 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad output count"));
    }

    aes_control[0] = (short)a[ARG_opcode].u_int;
    aes_control[1] = (short)n_int;
    aes_control[2] = (short)n_intout;
    aes_control[3] = (short)n_addr;
    aes_control[4] = (short)n_addrout;

    AESPB pb = { aes_control, aes_global, aes_intin, aes_intout, aes_addrin, aes_addrout };
    aes(&pb);

    mp_obj_t addr = mp_obj_new_tuple((size_t)n_addrout, NULL);
    mp_obj_tuple_t *t = MP_OBJ_TO_PTR(addr);
    for (mp_int_t i = 0; i < n_addrout; i++) {
        t->items[i] = mp_obj_new_int_from_uint((mp_uint_t)aes_addrout[i]);
    }
    mp_obj_t r[2] = { tuple_of_shorts(aes_intout, (size_t)n_intout), addr };
    return mp_obj_new_tuple(2, r);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(gem_aes_obj, 1, gem_aes);

// ---------------------------------------------------------------------------
// Application and workstation

static mp_obj_t gem_appl_init(void) {
    return MP_OBJ_NEW_SMALL_INT(appl_init());
}
static MP_DEFINE_CONST_FUN_OBJ_0(gem_appl_init_obj, gem_appl_init);

static mp_obj_t gem_appl_exit(void) {
    return MP_OBJ_NEW_SMALL_INT(appl_exit());
}
static MP_DEFINE_CONST_FUN_OBJ_0(gem_appl_exit_obj, gem_appl_exit);

// AES version, from the global array appl_init filled in (0 before it).
static mp_obj_t gem_aes_version(void) {
    return MP_OBJ_NEW_SMALL_INT((unsigned short)aes_global[0]);
}
static MP_DEFINE_CONST_FUN_OBJ_0(gem_aes_version_obj, gem_aes_version);

// -> (physical handle, char width, char height, box width, box height)
static mp_obj_t gem_graf_handle(void) {
    short w[4];
    short h = graf_handle(&w[0], &w[1], &w[2], &w[3]);
    mp_obj_t r[5] = {
        MP_OBJ_NEW_SMALL_INT(h), MP_OBJ_NEW_SMALL_INT(w[0]),
        MP_OBJ_NEW_SMALL_INT(w[1]), MP_OBJ_NEW_SMALL_INT(w[2]),
        MP_OBJ_NEW_SMALL_INT(w[3]),
    };
    return mp_obj_new_tuple(5, r);
}
static MP_DEFINE_CONST_FUN_OBJ_0(gem_graf_handle_obj, gem_graf_handle);

static mp_obj_t gem_graf_mouse(mp_obj_t mode) {
    return MP_OBJ_NEW_SMALL_INT(graf_mouse((short)mp_obj_get_int(mode), NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_1(gem_graf_mouse_obj, gem_graf_mouse);

static mp_obj_t gem_wind_update(mp_obj_t mode) {
    return MP_OBJ_NEW_SMALL_INT(wind_update((short)mp_obj_get_int(mode)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(gem_wind_update_obj, gem_wind_update);

// v_opnvwk(physical_handle) -> (handle, work_out)
// work_in: device 1 (the screen, per the current resolution), solid lines,
// colour 1 everywhere, raster coordinates (2). work_out is the 57 values
// v_opnvwk returns -- 45 in intout and 12 in ptsout, in that order.
static mp_obj_t gem_v_opnvwk(mp_obj_t phys) {
    short work_in[11] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2 };
    short work_out[57];
    memset(work_out, 0, sizeof(work_out));
    short handle = (short)mp_obj_get_int(phys);
    v_opnvwk(work_in, &handle, work_out);
    if (handle <= 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("v_opnvwk failed"));
    }
    mp_obj_t r[2] = { MP_OBJ_NEW_SMALL_INT(handle), tuple_of_shorts(work_out, 57) };
    return mp_obj_new_tuple(2, r);
}
static MP_DEFINE_CONST_FUN_OBJ_1(gem_v_opnvwk_obj, gem_v_opnvwk);

static mp_obj_t gem_v_clsvwk(mp_obj_t h) {
    v_clsvwk((short)mp_obj_get_int(h));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(gem_v_clsvwk_obj, gem_v_clsvwk);

// v_gtext(handle, x, y, text): text in UTF-8, drawn in the ST charset.
static mp_obj_t gem_v_gtext(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    char buf[256];
    const char *s = st_string(args[3], buf, sizeof(buf));
    v_gtext((short)mp_obj_get_int(args[0]), (short)mp_obj_get_int(args[1]),
            (short)mp_obj_get_int(args[2]), s);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gem_v_gtext_obj, 4, 4, gem_v_gtext);

// ---------------------------------------------------------------------------
// Dialogs

// form_alert(default_button, "[icon][lines][buttons]") -> button (1-based)
static mp_obj_t gem_form_alert(mp_obj_t def, mp_obj_t text) {
    char buf[256];
    const char *s = st_string(text, buf, sizeof(buf));
    return MP_OBJ_NEW_SMALL_INT(form_alert((short)mp_obj_get_int(def), s));
}
static MP_DEFINE_CONST_FUN_OBJ_2(gem_form_alert_obj, gem_form_alert);

// fsel_exinput(path, name, title) -> (button, path, name)
// path is the directory with its wildcard ("C:\\*.PY"), name the preselected
// file. button is 1 for OK, 0 for Cancel. fsel_exinput needs AES 1.4 (TOS
// 1.04 and later); older AES get fsel_input and no title.
static mp_obj_t gem_fsel_exinput(mp_obj_t path_in, mp_obj_t name_in, mp_obj_t title_in) {
    char path[256];
    char name[128];
    char title[64];
    st_string(path_in, path, sizeof(path));
    st_string(name_in, name, sizeof(name));
    st_string(title_in, title, sizeof(title));
    short button = 0;
    short ok;
    if ((unsigned short)aes_global[0] >= 0x0140) {
        ok = fsel_exinput(path, name, &button, title);
    } else {
        ok = fsel_input(path, name, &button);
    }
    if (ok == 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("file selector failed"));
    }
    mp_obj_t r[3] = { MP_OBJ_NEW_SMALL_INT(button), py_string(path), py_string(name) };
    return mp_obj_new_tuple(3, r);
}
static MP_DEFINE_CONST_FUN_OBJ_3(gem_fsel_exinput_obj, gem_fsel_exinput);

// pending_char() -> the character the Ctrl-C hook set aside, or -1.
// The hook reads the keyboard while Python runs, so a key typed before a
// GEM call is already out of the BIOS buffer and the AES will never see it.
extern int atari_take_pending_char(void);
static mp_obj_t gem_pending_char(void) {
    return MP_OBJ_NEW_SMALL_INT(atari_take_pending_char());
}
static MP_DEFINE_CONST_FUN_OBJ_0(gem_pending_char_obj, gem_pending_char);

// wait_key() -> scan code << 8 | character
//
// Through GEMDOS (Crawcin), not the AES's evnt_keybd: this is a TOS program,
// and it is the path input() already uses -- it follows redirection, and the
// key the Ctrl-C hook set aside is simply served first (without scan code,
// the hook only kept the character). evnt_keybd also hung under Hatari with
// EmuTOS on an STE, when TOS 2.06 on the same machine and EmuTOS on a TT
// worked; unexplained, and a good reason not to depend on it here.
// Ctrl-C raises KeyboardInterrupt, as it would anywhere else.
static mp_obj_t gem_wait_key(void) {
    int c = atari_take_pending_char();
    if (c >= 0) {
        if (c == 3) {
            mp_raise_type(&mp_type_KeyboardInterrupt);
        }
        return MP_OBJ_NEW_SMALL_INT(c);
    }
    long k = Crawcin();
    int ascii = (int)(k & 0xFF);
    int scan = (int)((k >> 16) & 0xFF);
    if (ascii == 3) {
        mp_raise_type(&mp_type_KeyboardInterrupt);
    }
    return MP_OBJ_NEW_SMALL_INT((scan << 8) | ascii);
}
static MP_DEFINE_CONST_FUN_OBJ_0(gem_wait_key_obj, gem_wait_key);

// ---------------------------------------------------------------------------
// Windows and events: the three calls the generic aes() cannot make safely,
// because each hands the AES an address that must stay valid.

// Run one AES call on the static arrays. Returns intout[0].
static short aes_call(short opcode, short n_intin, short n_intout,
                      short n_addrin, short n_addrout) {
    aes_control[0] = opcode;
    aes_control[1] = n_intin;
    aes_control[2] = n_intout;
    aes_control[3] = n_addrin;
    aes_control[4] = n_addrout;
    AESPB pb = { aes_control, aes_global, aes_intin, aes_intout, aes_addrin, aes_addrout };
    aes(&pb);
    return aes_intout[0];
}

static short evnt_msg[8];

// evnt_multi(flags, timer_ms=0, clicks=0x102, mask=1, state=1)
//   -> (events, mx, my, buttons, kstate, key, nclicks, msg)
// msg is the 8-word message buffer, meaningful when events has MU_MESAG.
// The rectangle events (MU_M1/MU_M2) are left out: nothing uses them yet.
static mp_obj_t gem_evnt_multi(size_t n_args, const mp_obj_t *args) {
    short flags = (short)mp_obj_get_int(args[0]);
    unsigned long timer = (n_args > 1) ? (unsigned long)mp_obj_get_int(args[1]) : 0;
    memset(aes_intin, 0, sizeof(aes_intin));
    aes_intin[0] = flags;
    aes_intin[1] = (short)((n_args > 2) ? mp_obj_get_int(args[2]) : 0x102);
    aes_intin[2] = (short)((n_args > 3) ? mp_obj_get_int(args[3]) : 1);
    aes_intin[3] = (short)((n_args > 4) ? mp_obj_get_int(args[4]) : 1);
    aes_intin[14] = (short)(timer & 0xFFFF);        // low word first
    aes_intin[15] = (short)(timer >> 16);
    memset(evnt_msg, 0, sizeof(evnt_msg));
    aes_addrin[0] = (long)evnt_msg;
    aes_call(25, 16, 7, 1, 0);
    mp_obj_t r[8];
    for (int i = 0; i < 7; i++) {
        r[i] = MP_OBJ_NEW_SMALL_INT(aes_intout[i]);
    }
    r[7] = tuple_of_shorts(evnt_msg, 8);
    return mp_obj_new_tuple(8, r);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gem_evnt_multi_obj, 1, 5, gem_evnt_multi);

// appl_write(ap_id, msg) -- send an 8-word message. An application can send
// one to itself: that is how a window asks for its own redraw, and how the
// tests drive the event loop without anyone at the keyboard.
static short write_buf[8];
static mp_obj_t gem_appl_write(mp_obj_t id_in, mp_obj_t msg_in) {
    memset(write_buf, 0, sizeof(write_buf));
    shorts_from(msg_in, write_buf, 8, "msg");
    aes_intin[0] = (short)mp_obj_get_int(id_in);
    aes_intin[1] = 16;
    aes_addrin[0] = (long)write_buf;
    return MP_OBJ_NEW_SMALL_INT(aes_call(12, 2, 1, 1, 0));
}
static MP_DEFINE_CONST_FUN_OBJ_2(gem_appl_write_obj, gem_appl_write);

// wind_set_str(handle, field, text) for WF_NAME (2) and WF_INFO (3).
// The AES keeps the POINTER and reads the string at every redraw of the
// frame, so it must outlive the call: one buffer per (window, field), kept
// here. Handles are small integers; beyond the table we refuse.
#define WSTR_SLOTS  32
#define WSTR_LEN    80
static char wstr[WSTR_SLOTS][2][WSTR_LEN];

static mp_obj_t gem_wind_set_str(mp_obj_t h_in, mp_obj_t field_in, mp_obj_t text_in) {
    mp_int_t h = mp_obj_get_int(h_in);
    mp_int_t field = mp_obj_get_int(field_in);
    if (h < 0 || h >= WSTR_SLOTS || (field != 2 && field != 3)) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad window handle or field"));
    }
    char *buf = wstr[h][field - 2];
    st_string(text_in, buf, WSTR_LEN);
    aes_intin[0] = (short)h;
    aes_intin[1] = (short)field;
    aes_intin[2] = (short)((unsigned long)buf >> 16);
    aes_intin[3] = (short)((unsigned long)buf & 0xFFFF);
    aes_intin[4] = 0;
    aes_intin[5] = 0;
    return MP_OBJ_NEW_SMALL_INT(aes_call(105, 6, 1, 0, 0));
}
static MP_DEFINE_CONST_FUN_OBJ_3(gem_wind_set_str_obj, gem_wind_set_str);

// ap_id() -- our application id, from the global array (-1 before appl_init).
static mp_obj_t gem_ap_id(void) {
    return MP_OBJ_NEW_SMALL_INT(aes_global[2]);
}
static MP_DEFINE_CONST_FUN_OBJ_0(gem_ap_id_obj, gem_ap_id);

// ---------------------------------------------------------------------------
// Menus: the tree is built in Python, in a bytearray. These two helpers are
// all it needs from C. The GC never moves an object, so the address of a
// bytearray stays valid for as long as the bytearray is referenced.

// addressof(buffer) -> address of its data
static mp_obj_t gem_addressof(mp_obj_t buf_in) {
    mp_buffer_info_t bi;
    mp_get_buffer_raise(buf_in, &bi, MP_BUFFER_READ);
    return mp_obj_new_int_from_uint((mp_uint_t)(uintptr_t)bi.buf);
}
static MP_DEFINE_CONST_FUN_OBJ_1(gem_addressof_obj, gem_addressof);

// to_st(str) -> bytes in the Atari ST character set
static mp_obj_t gem_to_st(mp_obj_t s_in) {
    char buf[256];
    const char *s = st_string(s_in, buf, sizeof(buf));
    return mp_obj_new_bytes((const byte *)s, strlen(s));
}
static MP_DEFINE_CONST_FUN_OBJ_1(gem_to_st_obj, gem_to_st);

// ---------------------------------------------------------------------------

static const mp_rom_map_elem_t gem_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__gem) },
    { MP_ROM_QSTR(MP_QSTR_vdi), MP_ROM_PTR(&gem_vdi_obj) },
    { MP_ROM_QSTR(MP_QSTR_aes), MP_ROM_PTR(&gem_aes_obj) },
    { MP_ROM_QSTR(MP_QSTR_appl_init), MP_ROM_PTR(&gem_appl_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_appl_exit), MP_ROM_PTR(&gem_appl_exit_obj) },
    { MP_ROM_QSTR(MP_QSTR_aes_version), MP_ROM_PTR(&gem_aes_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_graf_handle), MP_ROM_PTR(&gem_graf_handle_obj) },
    { MP_ROM_QSTR(MP_QSTR_graf_mouse), MP_ROM_PTR(&gem_graf_mouse_obj) },
    { MP_ROM_QSTR(MP_QSTR_wind_update), MP_ROM_PTR(&gem_wind_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_v_opnvwk), MP_ROM_PTR(&gem_v_opnvwk_obj) },
    { MP_ROM_QSTR(MP_QSTR_v_clsvwk), MP_ROM_PTR(&gem_v_clsvwk_obj) },
    { MP_ROM_QSTR(MP_QSTR_v_gtext), MP_ROM_PTR(&gem_v_gtext_obj) },
    { MP_ROM_QSTR(MP_QSTR_form_alert), MP_ROM_PTR(&gem_form_alert_obj) },
    { MP_ROM_QSTR(MP_QSTR_fsel_exinput), MP_ROM_PTR(&gem_fsel_exinput_obj) },
    { MP_ROM_QSTR(MP_QSTR_pending_char), MP_ROM_PTR(&gem_pending_char_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait_key), MP_ROM_PTR(&gem_wait_key_obj) },
    { MP_ROM_QSTR(MP_QSTR_evnt_multi), MP_ROM_PTR(&gem_evnt_multi_obj) },
    { MP_ROM_QSTR(MP_QSTR_appl_write), MP_ROM_PTR(&gem_appl_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_wind_set_str), MP_ROM_PTR(&gem_wind_set_str_obj) },
    { MP_ROM_QSTR(MP_QSTR_ap_id), MP_ROM_PTR(&gem_ap_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_addressof), MP_ROM_PTR(&gem_addressof_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_st), MP_ROM_PTR(&gem_to_st_obj) },
};
static MP_DEFINE_CONST_DICT(gem_module_globals, gem_module_globals_table);

const mp_obj_module_t mp_module_gem = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&gem_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__gem, mp_module_gem);
