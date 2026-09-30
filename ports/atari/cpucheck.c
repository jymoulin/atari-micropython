// Refuse to run the 68020 build on a lesser processor.
//
// This file is always compiled for the plain 68000 (see the Makefile), and
// must stay self-contained: on a 68000 anything else in MPY20.TTP -- down to
// get_cookie() in main.c, which gcc builds with extb.l -- dies on an illegal
// instruction. That is what the first version of this check did, being a few
// lines of main() that called get_cookie(): "Panic: Illegal Instruction"
// instead of the message, seen under Hatari on 30/09/2026.

#include <mint/osbind.h>

#if defined(ATARI_BUILD_68020)
static long cpu_cookie;

static long read_cpu_cookie(void) {
    const long *jar = *(const long **)0x5A0L;       // _p_cookies
    cpu_cookie = 0;                                 // no jar: a plain 68000
    if (jar) {
        for (; jar[0]; jar += 2) {
            if (jar[0] == 0x5F435055L) {            // "_CPU"
                cpu_cookie = jar[1];
                break;
            }
        }
    }
    return 0;
}
#endif

// Returns 0 when the processor is good enough for this build.
int atari_cpu_check(void) {
    #if defined(ATARI_BUILD_68020)
    Supexec(read_cpu_cookie);
    if (cpu_cookie < 20) {
        Cconws("MPY20.TTP needs a 68020 or better, and this machine has a 680");
        Cconout('0' + (int)(cpu_cookie / 10));
        Cconout('0');
        Cconws(".\r\nUse MPY00.TTP instead.\r\n");
        return 1;
    }
    #endif
    return 0;
}
