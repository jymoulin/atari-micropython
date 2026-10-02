#include <stdint.h>
#include <stddef.h>     // size_t, used by the path conversion hook below

// MicroPython port configuration for Atari TOS / EmuTOS (m68k)
//
// Target: Atari STE + TwiSTer accelerator (68020 @ 24 MHz, 8 MB alt-RAM),
// TOS 2.06 or EmuTOS, optionally under Geneva. Falcon (68030) also runs this
// binary. FreeMiNT is supported but not required.

// Use EVERYTHING level for maximum Python compatibility (f-strings, advanced
// builtins, OrderedDict, set operations, etc). Features not available on
// TOS are explicitly disabled below.
#define MICROPY_CONFIG_ROM_LEVEL        (MICROPY_CONFIG_ROM_LEVEL_EVERYTHING)

// Compiler and REPL
#define MICROPY_ENABLE_COMPILER         (1)
#define MICROPY_ENABLE_GC               (1)
#define MICROPY_HELPER_REPL             (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT  (1)
#define MICROPY_PY_BUILTINS_INPUT       (1)
#define MICROPY_PY_BUILTINS_HELP        (1)
#define MICROPY_PY_BUILTINS_HELP_MODULES (1)

// Readline with history and cursor key support
#define MICROPY_USE_READLINE            (1)
#define MICROPY_USE_READLINE_HISTORY    (1)
#define MICROPY_READLINE_HISTORY_SIZE   (50)
#define MICROPY_HAL_HAS_STDIO_MODE_SWITCH (1)

// The TOS console speaks VT52, not ANSI/VT100. Rather than translate on the
// way out, we tell readline not to emit escape sequences at all: it then
// calls mp_hal_move_cursor_back() / mp_hal_erase_line_from_cursor(), which
// mphalport.h implements with VT52 codes.
#define MICROPY_HAL_HAS_VT100           (0)

// Use setjmp-based NLR (no native m68k NLR implementation)
#define MICROPY_NLR_SETJMP              (1)

// Memory
#define MICROPY_ALLOC_PATH_MAX          (256)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT  (16)
#define MICROPY_ALLOC_QSTR_CHUNK_INIT   (2048)
#define MICROPY_QSTR_BYTES_IN_LEN       (2)
#define MICROPY_QSTR_BYTES_IN_HASH      (2)
// 1 MB default: the TwiSTer has 8 MB of alt-RAM, so we can be generous.
// Overridable at runtime with -m <kb>. On a plain 1 MB STE, use -m 128.
#define MICROPY_HEAP_SIZE               (1024 * 1024)

// --- Features NOT available on TOS ---

// Native code: @micropython.native, @micropython.viper and @micropython.asm_m68k.
// The emitter comes from the X68000 port (py/asmm68k.c) and generates plain
// 68000 code, so one binary serves the 68000, 68020 and 68030 alike.
#define MICROPY_EMIT_M68K               (1)
#define MICROPY_EMIT_INLINE_M68K        (1)
// No 32-bit multiply on the 68000: native code calls a helper for small ints.
#define MICROPY_SMALL_INT_MUL_HELPER    (1)
// Freshly generated code must be pushed out of the instruction cache before it
// runs: the 68020/030 have one, and CACHEON.PRG (project ../cache) turns it on.
// See atari_mphal.c.
extern void *atari_commit_exec(void *buf, size_t len, void *reloc);
#define MP_PLAT_COMMIT_EXEC(buf, len, reloc) atari_commit_exec((buf), (len), (reloc))

// The other native code emitters
#define MICROPY_EMIT_X64                (0)
#define MICROPY_EMIT_X86                (0)
#define MICROPY_EMIT_THUMB              (0)
#define MICROPY_EMIT_ARM                (0)
#define MICROPY_EMIT_INLINE_THUMB       (0)
#define MICROPY_EMIT_INLINE_THUMB_FLOAT (0)
#define MICROPY_EMIT_INLINE_RV32        (0)

// No threading (TOS is single-tasking; Geneva/MiNT threads are not exposed)
#define MICROPY_PY_THREAD               (0)

// uctypes wants 31-bit small integers (a static assertion in
// extmod/moductypes.c); REPR_B gives 30. It is a niche module -- raw struct
// layout access -- and nothing else in the build needs it.
#define MICROPY_PY_UCTYPES              (0)

// No POSIX-specific modules
#define MICROPY_PY_FFI                  (0)
#define MICROPY_PY_TERMIOS              (0)
#define MICROPY_PY_SIGNAL               (0)
#define MICROPY_PY_SELECT               (0)

// Networking deferred to phase 5 (STinG on TOS, MiNTnet under FreeMiNT)
#define MICROPY_PY_SOCKET               (0)
#define MICROPY_PY_NETWORK              (0)
#define MICROPY_PY_BLUETOOTH            (0)
#define MICROPY_PY_LWIP                 (0)
#define MICROPY_PY_OPENAMP              (0)
#define MICROPY_PY_ONEWIRE              (0)

// We have our own os module (modatarios.c)
#define MICROPY_PY_OS                   (0)

// TLS deferred (no AmiSSL equivalent; BearSSL/mbedTLS is a later phase)
#define MICROPY_PY_SSL                  (0)

// --- machine module: I2C on the TwiSTer/MonSTer bus ---
//
// MICROPY_PY_MACHINE stays OFF: extmod/modmachine.c wants a pile of port hooks
// (reset, freq, idle, unique_id) that mean nothing here. The port defines its
// own 'machine' module in machine_atari.c instead, and only borrows the I2C
// protocol from extmod, which is where the real work is.
#define MICROPY_PY_MACHINE              (0)
#define MICROPY_PY_MACHINE_I2C          (1)
#define MICROPY_PY_MACHINE_SOFTI2C      (1)

// EVERYTHING enables sys std files but we don't provide stream objects
#define MICROPY_PY_SYS_STDFILES         (1)
#define MICROPY_PY_SYS_STDIO_BUFFER     (1)

// No atexit / executable
#define MICROPY_PY_SYS_ATEXIT           (0)
#define MICROPY_PY_SYS_EXECUTABLE       (0)
// DUPTERM redirects VFS POSIX stdout/stderr writes through mp_hal_stdout_tx_strn(),
// which does UTF-8 -> Atari ST charset conversion and ANSI -> VT52 translation.
#define MICROPY_PY_OS_DUPTERM           (1)

// No .mpy saving or marshal
#define MICROPY_PERSISTENT_CODE_SAVE    (0)
#define MICROPY_PERSISTENT_CODE_SAVE_FILE (0)
#define MICROPY_PERSISTENT_CODE_SAVE_FUN (0)
#define MICROPY_PERSISTENT_CODE_LOAD    (1)
#define MICROPY_PY_MARSHAL              (0)

// No alternative VFS implementations
#define MICROPY_VFS_FAT                 (0)
#define MICROPY_VFS_LFS1                (0)
#define MICROPY_VFS_LFS2                (0)
#define MICROPY_VFS_ROM                 (0)

// No raw file I/O (we use POSIX VFS)
#define MICROPY_PY_IO_FILEIO            (0)

// MD5/SHA1 require mbedTLS/axTLS (not available)
#define MICROPY_PY_HASHLIB_MD5          (0)
#define MICROPY_PY_HASHLIB_SHA1         (0)

// --- Features explicitly ENABLED ---

#define MICROPY_ENABLE_FINALISER        (1)
#define MICROPY_VFS                     (1)
#define MICROPY_VFS_POSIX               (1)
#define MICROPY_READER_VFS              (1)
#define MICROPY_PY_SYS_EXIT             (1)
#define MICROPY_PY_SYS_PATH             (1)
#define MICROPY_PY_SYS_ARGV             (1)
// A STRING, not a boolean: py/modsys.c builds a str object straight from it
// (MP_DEFINE_STR_OBJ), so a (1) here means a string at address 1 -- bus error
// on the first read of sys.platform. The Amiga port carries the same bug.
#define MICROPY_PY_SYS_PLATFORM         "atari"
#define MICROPY_LONGINT_IMPL            (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_FLOAT_IMPL              (MICROPY_FLOAT_IMPL_FLOAT)
#define MICROPY_PY_TIME_INCLUDEFILE     "ports/atari/modtime.c"
#define MICROPY_PY_TIME_EXTRA_GLOBALS \
    { MP_ROM_QSTR(MP_QSTR_strftime), MP_ROM_PTR(&mp_time_strftime_obj) },
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (1)
#define MICROPY_PY_TIME_TIME_TIME_NS    (1)
#define MICROPY_EPOCH_IS_1970           (1)
#define MICROPY_HAL_HAS_TIMEZONE        (1)
#define MICROPY_PY_DEFLATE_COMPRESS     (1)

// VFS POSIX filename conversion: UTF-8 -> Atari ST charset.
// The Atari ST character set is NOT Latin-1 above 0x7F (it is closer to
// CP437 with Atari-specific glyphs), so a translation table is required.
extern const char *atari_utf8_to_st(const char *s, char *buf, size_t bufsize);
#define MICROPY_VFS_POSIX_CONVERT_PATH(path, buf, sz) atari_utf8_to_st(path, buf, sz)
#define MICROPY_GCREGS_SETJMP           (1)

// Ctrl-C support: poll the console every N bytecode operations.
// This enables KeyboardInterrupt during Python execution (loops, calls).
extern void mp_atari_check_signals(void);
// How often the VM stops to look for Ctrl-C. Each check is a Bconstat() BIOS
// trap, which is cheap on plain TOS but not under a multitasking layer like
// Geneva, so this is deliberately coarse: Ctrl-C latency of a few thousand
// bytecodes is imperceptible, a trap every 64 bytecodes is not.
// Measured on a TwiSTer STE under Geneva: going from 64 to 4096 took an empty
// Python loop from 267 to 217 us per iteration, about 19% -- a BIOS trap every
// 64 bytecodes really does cost that much under a multitasking layer. Ctrl-C
// latency at 4096 is still well under a millisecond of real work.
#ifndef MICROPY_VM_HOOK_COUNT
#define MICROPY_VM_HOOK_COUNT   (4096)
#endif
#define MICROPY_VM_HOOK_INIT    static unsigned int vm_hook_div = MICROPY_VM_HOOK_COUNT;
#define MICROPY_VM_HOOK_POLL    if (--vm_hook_div == 0) { vm_hook_div = MICROPY_VM_HOOK_COUNT; mp_atari_check_signals(); }
#define MICROPY_VM_HOOK_LOOP    MICROPY_VM_HOOK_POLL
#define MICROPY_VM_HOOK_RETURN  MICROPY_VM_HOOK_POLL

// quit() and exit() builtins
extern const struct _mp_obj_fun_builtin_var_t mp_builtin_quit_obj;
extern const struct _mp_obj_fun_builtin_var_t mp_builtin_exit_obj;
#define MICROPY_PORT_BUILTINS \
    { MP_ROM_QSTR(MP_QSTR_quit), MP_ROM_PTR(&mp_builtin_quit_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_exit), MP_ROM_PTR(&mp_builtin_exit_obj) },

// Object representation: B, not the default A.
//
// REPR_A tags objects in the low TWO bits of a pointer, so every object must
// sit on a 4-byte boundary. That is not something we can guarantee here: a
// GEMDOS program is loaded wherever the OS puts it, and on a real STE under
// Geneva the TPA regularly lands on an odd multiple of 2. Every address in
// the program shifts with it -- including every constant object -- and the
// interpreter then reads objects back as qstrs. The symptom is a call landing
// on whatever that index names: "TypeError: 'bool' object isn't callable",
// in a different place each time the memory map changes. Measured on the
// machine: text at 0x2a28c6 fails, text at 0x576ff4 works.
//
// REPR_B identifies a pointer with (o & 1) == 0, so 2-byte alignment is
// enough -- which m68k and GEMDOS both always give us. The cost is one bit
// of small-integer range (30 bits instead of 31); beyond that MPZ takes over,
// so nothing breaks, some arithmetic just gets slower.
#define MICROPY_OBJ_REPR                (MICROPY_OBJ_REPR_B)

// gcc aligns structures on 2 bytes on m68k (that is the ABI), but
// MICROPY_OBJ_REPR_A uses the low two bits of a pointer as a type tag: an
// object landing at an address == 2 (mod 4) is read back as a qstr, and the
// interpreter then calls whatever that index happens to name. Force every
// object header to 4 bytes. Still worth doing under REPR_B, which tolerates
// 2-byte alignment: a 68020 reads a misaligned 32-bit word, but slowly.
//
// Not -malign-int: that would change the layout of every struct we share
// with MiNTlib, which is compiled without it.
#define MICROPY_OBJ_BASE_ALIGNMENT      __attribute__((aligned(4)))

// Type definitions for m68k.
//
// We deliberately do NOT typedef mp_int_t/mp_uint_t here: since v1.28
// MicroPython derives them from MP_INT_TYPE, which defaults to intptr_t --
// and on m68k intptr_t is a 32-bit long, exactly what we want. Redefining
// them by hand (as the Amiga port still does) is redundant and conflicts
// with py/mpconfig.h.
typedef long mp_off_t;

#define MP_SSIZE_MAX INT32_MAX

#include <alloca.h>

// The VM keeps two flag bits in the low bits of its value-stack pointers
// (MP_TAGPTR_MAKE in py/bc.h, used by every try/with block), so the state of
// a running function must be 4-byte aligned. Small functions get that state
// from alloca(), which m68k gcc only aligns on 2 bytes. On a 68000 under
// EmuTOS this gave an address error on the first "return" from inside a
// "try" (02/10/2026, found by ALLTEST.PY). So take 2 bytes more and round
// up by hand. NOT __builtin_alloca_with_align(size, 32): with it, m68k gcc
// 13 builds code that crashes the TT after a few hundred collections
// (GCTEST then FSTEST, bus error), whereas this version passes.
#undef alloca
#define alloca(size) \
    ((void *)(((uintptr_t)__builtin_alloca((size) + 2) + 2) & ~(uintptr_t)3))

#define MICROPY_HW_BOARD_NAME           "Atari"
#if defined(__mc68020__)
#define MICROPY_HW_MCU_NAME             "M68020"
#else
#define MICROPY_HW_MCU_NAME             "M68000"
#endif

// Override sys.version to show MicroPython version + build instead of git hash
#include "genhdr/atariversion.h"
#define MICROPY_BANNER_NAME_AND_VERSION \
    "MicroPython v" MICROPY_VERSION_STRING " on " ATARI_BUILD_TIMESTAMP \
    " build " ATARI_BUILD_NUM

#define MP_STATE_PORT MP_STATE_VM
