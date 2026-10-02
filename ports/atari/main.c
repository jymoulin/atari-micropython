// Entry point for the Atari TOS / EmuTOS port.
//
// Differences from the Amiga port this derives from:
//  - the stack is sized by MiNTlib's _stksize (no StackSwap dance)
//  - the GC heap prefers alt-RAM (TT-RAM), which on a TwiSTer-equipped STE
//    is 8 MB of memory that is not shared with the video shifter
//  - there is no current-directory lock to save and restore
//  - a CPU check up front, because this binary is 68020+ only

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "py/builtin.h"
#include "py/mpstate.h"
#include "py/misc.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/stackctrl.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"
#include "shared/readline/readline.h"
#include "py/persistentcode.h"
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
#include "genhdr/mpversion.h"
#include "genhdr/atariversion.h"

#include <mint/osbind.h>
#include <mint/basepage.h>

_Static_assert(
    offsetof(mp_state_ctx_t, thread.dict_locals) % 4 == 0,
    "Root pointer alignment broken on m68k - reapply patches/mpstate_alignment.patch"
);

// MiNTlib: size of the stack carved out of the TPA. The startup code shrinks
// the process memory block to fit, which is what frees the rest of RAM for
// Malloc/Mxalloc. Getting this wrong is the classic TOS out-of-memory bug.
long _stksize = 128L * 1024L;

// Set once mp_init() has run: until then there is no runtime to print an
// exception with, and nlr_jump_fail() must not try.
static int vm_ready = 0;

// Init tracing. TOS gives us no debugger worth the name, so the port keeps a
// switch that narrates startup on the console (visible under Hatari's
// --conout 2). Off now that the port boots; turn it back on with
//   make COPT=-DATARI_DEBUG_INIT=1
#ifndef ATARI_DEBUG_INIT
#define ATARI_DEBUG_INIT (0)
#endif
#if ATARI_DEBUG_INIT
#define TRACE(s)    do { printf("[init] " s "\r\n"); fflush(stdout); } while (0)
#else
#define TRACE(s)    do { } while (0)
#endif

// Alignment probe. MICROPY_OBJ_BASE_ALIGNMENT aligns objects inside the text
// segment, but every address in a PRG shifts with the load address, and GEMDOS
// is not required to hand out a 4-byte aligned TPA -- nor is a third-party
// alt-RAM driver. If this lands on an odd multiple of 2, every MicroPython
// object does too, and MICROPY_OBJ_REPR_A reads them back as qstrs.
static const long align_probe __attribute__((aligned(4))) = 0;

// ST-RAM ends at phystop (system variable at 0x42E); anything above it is
// alternate RAM, wherever the board maps it. The TwiSTer puts its 8 MB just
// above ST-RAM, around 0x400000 -- not at 0x01000000 like a TT, which an
// earlier version of this test assumed, and so reported every alt-RAM address
// on a real STE as ST-RAM.
static unsigned long stram_top = 0x01000000UL;

// The 68020 has a 256-byte instruction cache that is off out of reset; whether
// anything turned it on is the accelerator driver's business, but a port that
// runs ten times slower than expected wants to know.
//
// movec is written as raw opcodes (with d0 as the operand) so that the same
// source assembles in the 68000 build, whose assembler rejects the mnemonic.
// None of these is ever reached on a 68000/68010, which has no CACR: every
// caller checks cpu_has_cacr() first.
static int get_cookie(const char *id, long *val);
extern int atari_cpu_check(void);

#define MOVEC_CACR_D0   ".word 0x4e7a,0x0002"   // movec %cacr,%d0
#define MOVEC_D0_CACR   ".word 0x4e7b,0x0002"   // movec %d0,%cacr

static int cpu_has_cacr(void) {
    long cpu = 0;
    return get_cookie("_CPU", &cpu) && cpu >= 20;
}

static long read_cacr(void) {
    register long v __asm__("d0");
    __asm__ volatile (MOVEC_CACR_D0 : "=r" (v));
    return v;
}

// -C: turn the instruction cache on for the duration of the run, restoring
// whatever was there on the way out. Needed under TOS 2.06, which -- per the
// TwiSTer manual -- sets the machine cookie wrongly on a 68020-equipped 1040
// STE, so the desktop's cache switch never reaches the CPU and CACR stays 0.
// EmuTOS gets this right and leaves the cache on by itself.
static long saved_cacr = -1;
static int want_icache = 0;

// Writes CACR and reads it straight back, in the same supervisor call, so we
// can tell "the write never took" from "something cleared it afterwards".
static long icache_old, icache_readback;

static long icache_on(void) {
    register long r __asm__("d0");
    __asm__ volatile (MOVEC_CACR_D0 : "=r" (r));
    long old = r;
    r = old | 0x09;             // bit 0 enable, bit 3 clear the cache first
    __asm__ volatile (MOVEC_D0_CACR "\n\t" MOVEC_CACR_D0 : "+r" (r));
    icache_old = old;
    icache_readback = r;
    return old;
}

static long cacr_restore_val;
static long icache_restore(void) {
    register long r __asm__("d0") = cacr_restore_val;
    __asm__ volatile (MOVEC_D0_CACR : : "r" (r));
    return 0;
}

static long read_phystop(void) {
    return (long)*(volatile uint32_t *)0x42EL;
}

#define IN_ALTRAM(p)    ((unsigned long)(p) >= stram_top)

// For the DMA users (sound, disk): everything at or above this address is out
// of reach of the ST's DMA chips, whatever the 68020 can see.
unsigned long atari_stram_top(void) {
    return stram_top;
}

static void atari_report(const char *fmt, ...);

static char *stack_top;
static char *heap = NULL;
static unsigned long heap_size = MICROPY_HEAP_SIZE;
static int heap_in_altram = 0;
// -s on the command line: allocate the GC heap in ST-RAM even when alt-RAM is
// available. Lets us test the four program/heap bank combinations with two
// binaries instead of four.
static int force_stram_heap = 0;
// -w: hold the screen until a key is pressed before terminating. A program
// launched from the desktop otherwise scrolls its last line away behind the
// shell's own redraw.
static int wait_before_exit = 0;
// -x: narrate the shutdown on the console. The log cannot do this job alone,
// since part of what we want to watch happens after the log is closed.
static int trace_exit = 0;

// -q : rien a l'ecran (le journal, lui, recoit tout). Pour les programmes
// GEM, dont les fenetres se font recouvrir par le moindre affichage console.
extern int atari_quiet;

// -t : duree de chaque etape du demarrage, en ms (compteur 200 Hz, donc
// a 5 ms pres). Pour savoir ou passent les secondes d'attente au lancement.
static int time_startup = 0;
static int test_heap = 0;       // -H
static uint32_t t_last;
static void tmark(const char *what) {
    if (!time_startup) {
        return;
    }
    uint32_t now = mp_atari_hz200();
    char buf[80];
    int n = snprintf(buf, sizeof(buf), "[t] %-24s %5lu ms\r\n", what,
                     (unsigned long)(now - t_last) * 5);
    atari_log_write(buf, (size_t)n);
    Fwrite(1, n, buf);
    t_last = now;
}

static void exit_mark(const char *msg) {
    size_t len = strlen(msg);
    atari_log_write(msg, len);
    if (trace_exit) {
        Fwrite(1, (long)len, msg);
    }
}

static void exit_atexit_marker(void) {
    // Runs from MiNTlib's exit(), after main() has returned.
    exit_mark("[x] atexit reached\r\n");
}

// ---------------------------------------------------------------------------
// System probing (all of this needs supervisor mode)
// ---------------------------------------------------------------------------

static long probe_cookie_id;
static long probe_cookie_val;

static long probe_cookie(void) {
    uint32_t *jar = *(uint32_t **)0x5A0L;
    if (jar == NULL) {
        return 0;
    }
    while (jar[0] != 0) {
        if (jar[0] == (uint32_t)probe_cookie_id) {
            probe_cookie_val = (long)jar[1];
            return 1;
        }
        jar += 2;
    }
    return 0;
}

// Returns 1 and sets *val if the cookie exists.
static int get_cookie(const char *id, long *val) {
    probe_cookie_id = ((long)id[0] << 24) | ((long)id[1] << 16)
                    | ((long)id[2] << 8) | (long)id[3];
    if (Supexec(probe_cookie)) {
        *val = probe_cookie_val;
        return 1;
    }
    return 0;
}

// Shared with modatarios.c, which serves them as os.uname().
const char *atari_machine_name(void) {
    long mch = 0;
    if (!get_cookie("_MCH", &mch)) {
        return "Atari";
    }
    switch (mch >> 16) {
        case 0: return "ST";
        case 1: return (mch & 0xFFFF) ? "Mega STE" : "STE";
        case 2: return "TT";
        case 3: return "Falcon";
        default: return "Atari";
    }
}

static long read_tos_version(void) {
    uint16_t *sysbase = *(uint16_t **)0x4F2L;
    return (long)sysbase[1];     // os_version, offset 2 in the OS header
}

// Mxalloc is not in the oldest TOS versions; fall back to Malloc there.
long atari_tos_version(void) {
    return Supexec(read_tos_version);
}

static int have_mxalloc(void) {
    return Supexec(read_tos_version) >= 0x0104;
}

// Shared with modatarios.c, which serves them as os.meminfo().
unsigned long free_stram(void);
unsigned long free_altram(void);

unsigned long free_stram(void) {
    return have_mxalloc() ? (unsigned long)Mxalloc(-1L, 0)
                          : (unsigned long)Malloc(-1L);
}

unsigned long free_altram(void) {
    return have_mxalloc() ? (unsigned long)Mxalloc(-1L, 1) : 0UL;
}

// ---------------------------------------------------------------------------
// quit() and exit() builtins
// ---------------------------------------------------------------------------

static mp_obj_t mp_builtin_quit(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        mp_raise_type(&mp_type_SystemExit);
    } else {
        mp_raise_type_arg(&mp_type_SystemExit, args[0]);
    }
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_builtin_quit_obj, 0, 1, mp_builtin_quit);
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_builtin_exit_obj, 0, 1, mp_builtin_quit);

// ---------------------------------------------------------------------------
// REPL
// ---------------------------------------------------------------------------

static void do_repl(void) {
    long cpu = 0, mch = 0;
    get_cookie("_CPU", &cpu);
    get_cookie("_MCH", &mch);

    const char *machine = atari_machine_name();

    mp_hal_stdout_tx_str("MicroPython v" MICROPY_VERSION_STRING " on " ATARI_BUILD_TIMESTAMP
        " build " ATARI_BUILD_NUM "\r\n");
    atari_report("%s with 680%02ld %s |  heap: %luKB %s\r\n",
           machine, cpu ? cpu : 0L,
           #ifdef __HAVE_68881__
           "+FPU ",
           #else
           "(soft float) ",
           #endif
           heap_size / 1024,
           heap_in_altram ? "(alt-RAM)" : "(ST-RAM)");
    atari_report("Free: ST-RAM %luKB, alt-RAM %luKB\r\n",
           free_stram() / 1024, free_altram() / 1024);
    mp_hal_stdout_tx_str("Use Ctrl-D to exit, Ctrl-E for paste mode\r\n");
    mp_hal_stdout_tx_str("Type \"help()\" for more information.\r\n");

    mp_hal_stdio_mode_raw();

    vstr_t line;
    vstr_init(&line, 32);

    for (;;) {
        MP_STATE_THREAD(gc_lock_depth) = 0;
        vstr_reset(&line);
        int ret = readline(&line, ">>> ");
        mp_parse_input_kind_t parse_input_kind = MP_PARSE_SINGLE_INPUT;

        if (ret == CHAR_CTRL_C) {
            mp_hal_stdout_tx_str("\r\n");
            continue;
        } else if (ret == CHAR_CTRL_D) {
            mp_hal_stdout_tx_str("\r\n");
            break;
        } else if (ret == CHAR_CTRL_E) {
            mp_hal_stdout_tx_str("\r\npaste mode; Ctrl-C to cancel, Ctrl-D to finish\r\n=== ");
            vstr_reset(&line);
            for (;;) {
                char c = mp_hal_stdin_rx_chr();
                if (c == CHAR_CTRL_C) {
                    mp_hal_stdout_tx_str("\r\n");
                    goto input_restart;
                } else if (c == CHAR_CTRL_D) {
                    mp_hal_stdout_tx_str("\r\n");
                    break;
                } else {
                    vstr_add_byte(&line, c);
                    if (c == '\r') {
                        mp_hal_stdout_tx_str("\r\n=== ");
                    } else {
                        mp_hal_stdout_tx_strn(&c, 1);
                    }
                }
            }
            parse_input_kind = MP_PARSE_FILE_INPUT;
        } else if (vstr_len(&line) == 0) {
            continue;
        } else {
            while (mp_repl_continue_with_input(vstr_null_terminated_str(&line))) {
                vstr_add_byte(&line, '\n');
                ret = readline(&line, "... ");
                if (ret == CHAR_CTRL_C) {
                    mp_hal_stdout_tx_str("\r\n");
                    goto input_restart;
                } else if (ret == CHAR_CTRL_D) {
                    break;
                }
            }
        }

        mp_hal_stdio_mode_orig();
        {
            nlr_buf_t nlr;
            if (nlr_push(&nlr) == 0) {
                mp_lexer_t *lex = mp_lexer_new_from_str_len(
                    MP_QSTR__lt_stdin_gt_,
                    vstr_str(&line), vstr_len(&line), 0);
                qstr source_name = lex->source_name;
                mp_parse_tree_t parse_tree = mp_parse(lex, parse_input_kind);
                mp_obj_t module_fun = mp_compile(&parse_tree, source_name,
                    parse_input_kind == MP_PARSE_SINGLE_INPUT);
                mp_call_function_0(module_fun);
                nlr_pop();
            } else {
                if (mp_obj_is_subclass_fast(
                        MP_OBJ_FROM_PTR(((mp_obj_base_t *)nlr.ret_val)->type),
                        MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
                    break;
                }
                mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
            }
        }
        mp_hal_stdio_mode_raw();
        continue;
    input_restart:
        continue;
    }

    vstr_clear(&line);
    mp_hal_stdio_mode_orig();
    mp_hal_stdout_tx_str("Bye!\r\n");
}

static int do_str(const char *str) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, str, strlen(str), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, false);
        mp_call_function_0(module_fun);
        nlr_pop();
        return 0;
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
        return 1;
    }
}

static int do_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        printf("Error: cannot open file '%s'\r\n", filename);
        return 1;
    }
    fclose(f);

    size_t len = strlen(filename);
    if (len >= 4 && strcmp(filename + len - 4, ".mpy") == 0) {
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            qstr qfilename = qstr_from_str(filename);
            mp_compiled_module_t cm;
            cm.context = m_new_obj(mp_module_context_t);
            cm.context->module.globals = mp_globals_get();
            mp_raw_code_load_file(qfilename, &cm);
            mp_obj_t module_fun = mp_make_function_from_proto_fun(
                cm.rc, cm.context, NULL);
            mp_call_function_0(module_fun);
            nlr_pop();
            return 0;
        } else {
            mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
            return 1;
        }
    }

    int ret = pyexec_file(filename);
    return ret == 0 ? 1 : 0;    // pyexec_file returns 1 on success
}

// ---------------------------------------------------------------------------
// VM setup
// ---------------------------------------------------------------------------

// Write a unique value to every 32-bit word of the heap and read it all back.
// An expansion board's alt-RAM is the one part of this machine nobody has
// tested with a program that touches a megabyte at a time, and a GC heap that
// does not hold what was written to it looks exactly like an interpreter bug.
// Each word gets its own address as the pattern, so a mirrored or partially
// decoded address range shows up too.
static int heap_selftest(char *p, unsigned long size) {
    volatile uint32_t *w = (volatile uint32_t *)p;
    unsigned long n = size / 4;

    for (unsigned long i = 0; i < n; i++) {
        w[i] = (uint32_t)(uintptr_t)&w[i] ^ 0xA5A5A5A5UL;
    }
    for (unsigned long i = 0; i < n; i++) {
        uint32_t want = (uint32_t)(uintptr_t)&w[i] ^ 0xA5A5A5A5UL;
        if (w[i] != want) {
            printf("HEAP TEST FAILED at %p: wrote %08lx, read %08lx\r\n",
                   (void *)&w[i], (unsigned long)want, (unsigned long)w[i]);
            return 0;
        }
    }
    return 1;
}

// Where everything landed, printed before the interpreter starts -- a failure
// during mp_init() would otherwise hide it. The alignment audit is the point:
// MICROPY_OBJ_REPR_A tags objects in the low two bits of their address, so a
// single object sitting at an odd multiple of 2 turns calls into nonsense
// ("TypeError: 'bool' object isn't callable" and friends).
// printf() goes straight to GEMDOS, so it would bypass the -l log. Route the
// startup report through here instead: same line on screen, same line in the
// file.
static void atari_report(const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if (n > (int)sizeof(line) - 1) {
        n = (int)sizeof(line) - 1;
    }
    atari_log_write(line, (size_t)n);
    if (!atari_quiet) {
        Fwrite(1, n, line);
    }
}

static void report_memory(void) {
    const void *addrs[5];
    const char *names[5];
    addrs[0] = &align_probe;        names[0] = "rodata";
    addrs[1] = &mp_type_str;        names[1] = "type_str";
    addrs[2] = &mp_type_bool;       names[2] = "type_bool";
    addrs[3] = &mp_state_ctx;       names[3] = "bss";
    addrs[4] = heap;                names[4] = "heap";

    atari_report("text %p %s | data %p | bss %p | heap %p %s %luKB\r\n",
           (void *)_base->p_tbase, IN_ALTRAM(_base->p_tbase) ? "alt" : "ST",
           (void *)_base->p_dbase, (void *)_base->p_bbase,
           (void *)heap, IN_ALTRAM(heap) ? "alt" : "ST", heap_size / 1024);

    // Informational under REPR_B: a 2-byte aligned object is read correctly,
    // just a little slower on a 68020. Under REPR_A this was fatal.
    int odd = 0;
    for (int i = 0; i < 5; i++) {
        if ((unsigned long)addrs[i] & 3) {
            atari_report("note: %s at %p (2-byte aligned)\r\n", names[i], addrs[i]);
            odd = 1;
        }
    }
    if (!odd) {
        atari_report("align: all 4-byte aligned\r\n");
    }

    long cacr = cpu_has_cacr() ? Supexec(read_cacr) : 0;
    long cpu = 0, mch = 0;
    get_cookie("_CPU", &cpu);
    get_cookie("_MCH", &mch);
    atari_report("CACR %08lx: I-cache %s%s | hook/%d | _CPU %ld _MCH %lx\r\n",
           (unsigned long)cacr,
           cpu < 20 ? "none" : (cacr & 1) ? "on" : "OFF",
           want_icache ? " (-C)" : "",
           MICROPY_VM_HOOK_COUNT, cpu, (unsigned long)mch);
    fflush(stdout);
}

static void vm_init(int argc, char **argv) {
    if (heap == NULL) {
        // Prefer alt-RAM: the GC sweeps the heap constantly, and on an STE
        // the ST-RAM bus is shared with the video shifter.
        if (force_stram_heap && have_mxalloc()) {
            // Mode 0 = ST-RAM only. Plain Malloc() would NOT do: with PRGFLAGS
            // bit 2 set, GEMDOS is free to answer it from alt-RAM, which is
            // exactly what -s is meant to rule out.
            heap = (char *)Mxalloc((long)heap_size, 0);
            heap_in_altram = 0;
        } else if (have_mxalloc()) {
            heap = (char *)Mxalloc((long)heap_size, 3);   // 3 = alt-RAM preferred
            heap_in_altram = (heap != NULL)
                && ((unsigned long)heap >= 0x01000000UL);
        } else {
            heap = (char *)Malloc((long)heap_size);
        }
        // GEMDOS reports failure as 0, but Mxalloc can also return a
        // negative error code; a signed test catches both.
        if ((long)heap <= 0) {
            heap = NULL;
            printf("Error: cannot allocate %luKB heap\r\n", heap_size / 1024);
            atari_report("Free: ST-RAM %luKB, alt-RAM %luKB\r\n",
                   free_stram() / 1024, free_altram() / 1024);
            exit(1);
        }
        tmark("allocation du tas");
        // Pas de memset : le GC remet chaque bloc a zero en l'allouant
        // (MICROPY_GC_CONSERVATIVE_CLEAR, py/gc.c). Il coutait 165 ms a
        // 8 MHz sur le STE, pour rien.
    }
    report_memory();
    // -H : autotest du tas, ecriture puis relecture de tout le Mo. Il a servi
    // a la chasse au probleme d'alignement en alt-RAM, resolu depuis ; il
    // reste utile pour diagnostiquer une carte memoire douteuse, mais coutait
    // 1,2 s a 8 MHz a chaque lancement.
    if (test_heap) {
        if (!heap_selftest(heap, heap_size)) {
            printf("The GC heap does not hold what is written to it. Refusing to\r\n");
            printf("run: every failure after this point would be a lie.\r\n");
            printf("Try -s to put the heap in ST-RAM.\r\n");
            exit(1);
        }
        atari_report("heap self-test ok (-H)\r\n");
        tmark("autotest du tas");
    }
    // GEMDOS only promises an even address, and a plain ST hands out 4n+2
    // (0xe29be under EmuTOS). The VM needs 4-byte aligned stack frames
    // (see alloca in mpconfigport.h), and frames of big functions come from
    // this heap: align both ends on 16 bytes, the GC block size. heap
    // itself is left as GEMDOS gave it, for Mfree.
    char *gc_start = (char *)(((unsigned long)heap + 15) & ~15UL);
    char *gc_end = (char *)(((unsigned long)heap + heap_size) & ~15UL);
    gc_init(gc_start, gc_end);
    TRACE("gc_init ok");
    mp_stack_ctrl_init();
    mp_stack_set_top(stack_top);
    mp_stack_set_limit(40000);      // 40KB for Python, leaves room for C calls
    mp_init();
    vm_ready = 1;
    TRACE("mp_init ok");
    {
        mp_obj_t vfs_obj = MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(
            &mp_type_vfs_posix, 0, 0, NULL);
        mp_obj_t mount_args[2] = { vfs_obj, MP_OBJ_NEW_QSTR(MP_QSTR__slash_) };
        mp_vfs_mount(2, mount_args, (mp_map_t *)&mp_const_empty_map);
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
    }
    TRACE("vfs mounted");
    tmark("mp_init et VFS");
    for (int i = 1; i < argc; i++) {
        mp_obj_list_append(mp_sys_argv,
            MP_OBJ_NEW_QSTR(qstr_from_str(argv[i])));
    }
    TRACE("argv ok");
}

static void cleanup(void) {
    atari_log_close();
    if (saved_cacr != -1) {
        cacr_restore_val = saved_cacr;
        Supexec(icache_restore);
        saved_cacr = -1;
    }
    if (heap != NULL) {
        Mfree(heap);
        heap = NULL;
    }
}

int main(int argc, char **argv) {
    // Before anything else, and in code built for the 68000: see cpucheck.c.
    if (atari_cpu_check()) {
        return 1;
    }

    t_last = mp_atari_hz200();
    int stack_dummy;
    stack_top = (char *)&stack_dummy;

    stram_top = (unsigned long)Supexec(read_phystop);

    // REPR_B needs objects on even addresses, which m68k gives us for free.
    // Read the address through a volatile pointer: gcc knows the declared
    // alignment of align_probe and folds a plain test to false -- which is
    // how the first version of this check silently passed on a machine that
    // was loading us on an odd multiple of 2.
    const void *volatile probe = &align_probe;
    if ((unsigned long)probe & 1) {
        printf("FATAL: program loaded at an odd address (%p).\r\n", probe);
        return 1;
    }

    #if defined(__mc68020__)
    // This binary (MPY20.TTP) uses 68020 instructions. Say so plainly rather
    // than dying with an illegal instruction on a stock ST/STE. The 68000
    // build (make CPU=68000, MPY00.TTP) runs everywhere.
    long cpu = 0;
    if (get_cookie("_CPU", &cpu) && cpu < 20) {
        printf("MPY20.TTP needs a 68020 or better (found 680%02ld): use MPY00.TTP.\r\n", cpu);
        return 1;
    }
    #endif

    #ifdef __HAVE_68881__
    // Built with hardware floating point: say so rather than dying on the
    // first F-line instruction. (The soft-float build has no such check.)
    long fpu = 0;
    if (!get_cookie("_FPU", &fpu) || (fpu >> 16) == 0) {
        printf("This build needs a 68881/68882 FPU. Use the soft-float build.\r\n");
        return 1;
    }
    #endif

    if (want_icache && !cpu_has_cacr()) {
        atari_report("-C: no instruction cache on this CPU, ignored\r\n");
        want_icache = 0;
    }
    if (want_icache) {
        saved_cacr = Supexec(icache_on);
        atari_report("-C: CACR was %08lx, wrote %08lx, reads back %08lx\r\n",
               (unsigned long)icache_old,
               (unsigned long)(icache_old | 0x09),
               (unsigned long)icache_readback);
        fflush(stdout);
    }

    // Measure the spin loop before anything else needs a microsecond delay.
    mp_atari_calibrate_delay();

    // Les options, dans n'importe quel ordre, jusqu'au premier argument qui
    // n'en est pas une (-c ou le nom du script).
    while (argc >= 2 && argv[1][0] == '-' && argv[1][1] != '\0'
           && strcmp(argv[1], "-c") != 0) {
        const char *opt = argv[1];
        if (strcmp(opt, "-q") == 0) {
            atari_quiet = 1;
        } else if (strcmp(opt, "-t") == 0) {
            time_startup = 1;
        } else if (strcmp(opt, "-H") == 0) {
            test_heap = 1;
        } else if (strcmp(opt, "-x") == 0) {
            trace_exit = 1;
            atexit(exit_atexit_marker);
        } else if (strcmp(opt, "-w") == 0) {
            wait_before_exit = 1;
        } else if (strcmp(opt, "-C") == 0) {
            want_icache = 1;
        } else if (strcmp(opt, "-s") == 0) {
            force_stram_heap = 1;
        } else if (strcmp(opt, "-l") == 0 && argc >= 3) {
            if (!atari_log_open(argv[2])) {
                printf("Cannot create log file '%s'\r\n", argv[2]);
                return 1;
            }
            argc -= 1;
            argv += 1;
        } else if (strcmp(opt, "-m") == 0 && argc >= 3) {
            heap_size = (unsigned long)atol(argv[2]) * 1024;
            if (heap_size == 0) {
                heap_size = MICROPY_HEAP_SIZE;
            }
            argc -= 1;
            argv += 1;
        } else {
            printf("Unknown option '%s'\r\n", opt);
            printf("Usage: MPY [-q] [-t] [-H] [-x] [-w] [-C] [-s] [-l fichier] "
                   "[-m ko] [-c code | script.py]\r\n");
            return 1;
        }
        argc -= 1;
        argv += 1;
    }

    if (time_startup) {
        // Sous Hatari, RUNTEST.PRG note l'heure juste avant de nous lancer :
        // la difference couvre le chargement et la relocation du binaire.
        long f = Fopen("RUNTEST.T0", 0);
        if (f >= 0) {
            uint32_t t0 = 0;
            Fread((int)f, 4, &t0);
            Fclose((int)f);
            uint32_t t_main = t_last;
            char buf[80];
            int n = snprintf(buf, sizeof(buf), "[t] %-24s %5lu ms\r\n",
                             "chargement (avant main)",
                             (unsigned long)(t_main - t0) * 5);
            atari_log_write(buf, (size_t)n);
            Fwrite(1, n, buf);
        }
    }
    tmark("calibration + options");
    int ret = 0;
    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        vm_init(argc - 1, argv + 1);
        ret = do_str(argv[2]);
        gc_sweep_all();
        mp_deinit();
    } else if (argc > 1) {
        vm_init(argc, argv);
        // sys.path[0] = the script's directory. TOS separators are '\' and
        // '/', with 'C:' style drive prefixes.
        {
            const char *script = argv[1];
            const char *last_sep = NULL;
            for (const char *p = script; *p; p++) {
                if (*p == '\\' || *p == '/' || *p == ':') {
                    last_sep = p;
                }
            }
            if (last_sep != NULL) {
                size_t dir_len = last_sep - script + 1;   // keep ':' , drop '\'
                if (*last_sep != ':') {
                    dir_len--;
                }
                mp_obj_list_t *path = MP_OBJ_TO_PTR(mp_sys_path);
                if (path->len > 0) {
                    path->items[0] = mp_obj_new_str(script, dir_len);
                }
            }
        }
        ret = do_file(argv[1]);
        gc_sweep_all();
        mp_deinit();
    } else {
        vm_init(argc, argv);
        do_repl();
        gc_sweep_all();
        mp_deinit();
    }

    if (wait_before_exit) {
        mp_hal_stdout_tx_str("\r\n-- une touche pour sortir --\r\n");
        Cconin();
    }

    {
        char msg[48];
        int n = snprintf(msg, sizeof(msg), "[exit] status=%d\r\n", ret);
        atari_log_write(msg, (size_t)n);
        if (trace_exit) {
            Fwrite(1, n, msg);
        }
    }

    // Each step named, so a hang can be placed exactly. Mfree of an alt-RAM
    // block is the one that worries me: the TwiSTer's alternate RAM is served
    // by a third-party driver, and freeing it is the least travelled path.
    if (saved_cacr != -1) {
        cacr_restore_val = saved_cacr;
        Supexec(icache_restore);
        saved_cacr = -1;
    }

    {
        extern void atari_sound_shutdown(void);
        atari_sound_shutdown();     // before the heap and its buffers go
    }
    exit_mark("[x] freeing heap\r\n");
    if (heap != NULL) {
        Mfree(heap);
        heap = NULL;
    }
    exit_mark("[x] heap freed\r\n");

    atari_log_close();
    exit_mark("[x] log closed, returning from main\r\n");
    return ret;
}

// The C stack is scanned at BOTH 2-byte offsets. m68k gcc only keeps the
// stack 2-byte aligned, so a pointer held in a local may sit at an address
// 4n or 4n+2, whatever the stack pointer is; the generic helper reads words
// from the current stack pointer onwards and sees only one of the two. It
// went unnoticed while locals and stack pointer shared their parity by
// luck; aligning the VM's frames (alloca in mpconfigport.h) broke the luck,
// and GCTEST followed by FSTEST then crashed on freed objects (02/10/2026).
// Reading a long at 4n+2 is legal on every 680x0.
#include <setjmp.h>

static void __attribute__((noinline)) gc_collect_stack(void) {
    jmp_buf regs;           // the callee-saved registers, in this frame
    setjmp(regs);
    uintptr_t lo = (uintptr_t)&regs & ~(uintptr_t)3;
    uintptr_t top = (uintptr_t)MP_STATE_THREAD(stack_top);
    gc_collect_root((void **)lo, (top - lo) / sizeof(void *));
    gc_collect_root((void **)(lo + 2), (top - lo - 2) / sizeof(void *));
}

void gc_collect(void) {
    gc_collect_start();
    gc_collect_stack();
    gc_collect_end();
}

// Called when something raises with no nlr_push() above it -- i.e. during
// startup or shutdown. Print the exception rather than just saying that one
// happened; without a debugger on TOS it is the only clue we get.
void nlr_jump_fail(void *val) {
    mp_hal_stdio_mode_orig();
    if (vm_ready && val != NULL) {
        printf("FATAL: uncaught exception outside any handler:\r\n");
        fflush(stdout);
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(val));
    } else {
        printf("FATAL: uncaught NLR before the VM was up (val=%p)\r\n", val);
    }
    fflush(stdout);
    cleanup();
    exit(1);
}

#ifndef NDEBUG
void __assert_func(const char *file, int line, const char *func, const char *expr) {
    (void)func;
    mp_hal_stdio_mode_orig();
    printf("Assertion '%s' failed, at file %s:%d\r\n", expr, file, line);
    cleanup();
    exit(1);
}
#endif
