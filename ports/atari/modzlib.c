// Native _zlib module — provides crc32() for the frozen zlib module.
// CRC32 must be in C for performance on m68k.

#include <stdint.h>
#include "py/runtime.h"

// CRC32 with polynomial 0xEDB88320 (same as zlib/gzip/zip)
static uint32_t crc32_byte(uint32_t crc, uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; i++) {
        crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return crc;
}

// _zlib.crc32(data[, value]) — compute CRC32 checksum.
// Compatible with CPython's zlib.crc32().
static mp_obj_t mod_zlib_crc32(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
    uint32_t crc = 0xFFFFFFFF;
    if (n_args > 1) {
        crc = (uint32_t)mp_obj_get_int_truncated(args[1]) ^ 0xFFFFFFFF;
    }
    const uint8_t *data = bufinfo.buf;
    for (size_t i = 0; i < bufinfo.len; i++) {
        crc = crc32_byte(crc, data[i]);
    }
    crc ^= 0xFFFFFFFF;
    return mp_obj_new_int_from_uint(crc);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_zlib_crc32_obj, 1, 2, mod_zlib_crc32);

static const mp_rom_map_elem_t zlib_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__zlib) },
    { MP_ROM_QSTR(MP_QSTR_crc32), MP_ROM_PTR(&mod_zlib_crc32_obj) },
};
static MP_DEFINE_CONST_DICT(zlib_module_globals, zlib_module_globals_table);

const mp_obj_module_t mp_module_zlib_native = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&zlib_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__zlib, mp_module_zlib_native);
