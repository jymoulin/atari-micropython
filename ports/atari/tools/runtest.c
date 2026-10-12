/* RUNTEST.PRG -- launcher for automated tests under Hatari.
 *
 * Hatari can start a program on its own (--auto) and stop after a given
 * number of VBLs (--run-vbls), but --auto passes no arguments. So this
 * launcher reads its command line from RUNTEST.ARG, next to it, and
 * launches MPY.TTP with it. Example RUNTEST.ARG:
 *     -l RESULT.TXT SMOKE.PY
 * The -l log lands directly in the Mac folder that serves as Hatari's
 * GEMDOS drive: that is what we read back, not the screen.
 *
 * Development tool only, it has no business on the real machine.
 */

#include <mint/osbind.h>
#include <string.h>

/* Move to the drive that holds RUNTEST.ARG. When a disk image occupies
 * C:, Hatari's GEMDOS drive moves to D: or further, and nothing
 * guarantees that --auto leaves us there as the current drive. */
static void aller_chez_soi(void) {
    long h = Fopen("RUNTEST.ARG", 0);
    if (h >= 0) {
        Fclose((int)h);
        return;
    }
    long carte = Drvmap();
    for (int d = 2; d < 26; d++) {
        if (!(carte & (1L << d))) {
            continue;
        }
        char chemin[16] = "X:\\RUNTEST.ARG";
        chemin[0] = (char)('A' + d);
        h = Fopen(chemin, 0);
        if (h >= 0) {
            Fclose((int)h);
            Dsetdrv(d);
            Dsetpath("\\");
            return;
        }
    }
}

static long lire_hz200(void) {
    return *(volatile long *)0x4BAL;
}

int main(void) {
    char tail[128];
    aller_chez_soi();
    long h = Fopen("RUNTEST.ARG", 0);
    long n = 0;
    if (h >= 0) {
        n = Fread((int)h, (long)sizeof(tail) - 2, tail + 1);
        Fclose((int)h);
    }
    if (n < 0) {
        n = 0;
    }
    /* Cut at the first end of line. */
    for (long i = 0; i < n; i++) {
        char c = tail[1 + i];
        if (c == '\r' || c == '\n') {
            n = i;
            break;
        }
    }
    tail[0] = (char)n;          /* GEMDOS command line: Pascal string */
    tail[1 + n] = '\0';

    /* Time (200 Hz counter) just before the launch, so that MPY -t
     * can measure loading and relocation, which precede main. */
    {
        long t0 = Supexec(lire_hz200);
        long f = Fcreate("RUNTEST.T0", 0);
        if (f >= 0) {
            Fwrite((int)f, 4, &t0);
            Fclose((int)f);
        }
    }
    long r = Pexec(0, "MPY.TTP", tail, NULL);

    /* The return code, in case MPY.TTP wrote nothing. */
    h = Fcreate("RUNTEST.RET", 0);
    if (h >= 0) {
        char buf[16];
        int len = 0;
        long v = r;
        char tmp[12];
        int k = 0;
        int neg = v < 0;
        if (neg) {
            v = -v;
        }
        do {
            tmp[k++] = (char)('0' + v % 10);
            v /= 10;
        } while (v && k < 11);
        if (neg) {
            buf[len++] = '-';
        }
        while (k) {
            buf[len++] = tmp[--k];
        }
        buf[len++] = '\r';
        buf[len++] = '\n';
        Fwrite((int)h, len, buf);
        Fclose((int)h);
    }
    return 0;
}
