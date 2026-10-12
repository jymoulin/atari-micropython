/* MPYRUN.PRG -- GEM launcher for MicroPython.
 *
 * WHY: a TOS program (.TTP) launched from the desktop does not get the
 * mouse and sees its drawing overwritten by the console -- TOS switches the
 * screen to text mode. The .GTP extension fixes that under TOS 2.06 and
 * EmuTOS, but MagiC does not know it. A .PRG, on the other hand, is launched
 * in GEM mode by every desktop: this one picks the script, then launches
 * MPY20.TTP (68020 and up) or MPY00.TTP (any machine) with it, depending on
 * the processor.
 *
 * It moves into the chosen script's folder, passes -q (nothing on screen,
 * GEM windows would keep poor company with the console there) and
 * -l <script>.LOG, then shows the return code.
 *
 * Put it next to MPY20.TTP and/or MPY00.TTP.
 */

#include <gem.h>
#include <mint/osbind.h>
#include <string.h>

/* The possible binaries, from best to most general. MPY20.TTP requires a
 * 68020: it is only tried if the _CPU cookie allows it. MPY.TTP is the
 * old name, kept so as not to break an existing installation. */
static const char *const progs[] = { "MPY20.TTP", "MPY00.TTP", "MPY.TTP" };

#define EFILNF  (-33L)

static long cpu_cookie;
static long read_cpu(void) {
    long *jar = *(long **)0x5A0L;
    cpu_cookie = 0;
    if (jar) {
        for (; jar[0]; jar += 2) {
            if (jar[0] == 0x5F435055L) {        /* "_CPU" */
                cpu_cookie = jar[1];
                break;
            }
        }
    }
    return 0;
}

static char path[260];
static char name[128];
static char tail[128];
static char logname[128];
static char message[200];

/* Append s to tail, which is a Pascal string (length byte first). */
static void add(const char *s) {
    unsigned char n = (unsigned char)tail[0];
    size_t l = strlen(s);
    if (n + l + 1 >= sizeof(tail) - 2) {
        return;
    }
    if (n) {
        tail[1 + n] = ' ';
        n++;
    }
    memcpy(tail + 1 + n, s, l);
    n = (unsigned char)(n + l);
    tail[0] = (char)n;
    tail[1 + n] = '\0';
}

int main(void) {
    short button = 0;

    if (appl_init() < 0) {
        /* No AES, nothing to do here: that is the launcher's whole point. */
        Cconws("MPYRUN a besoin du GEM. Lance MPY.TTP directement.\r\n");
        Cconin();
        return 1;
    }
    graf_mouse(ARROW, 0L);

    /* Current folder -> "X:\path\*.PY" */
    path[0] = (char)('A' + Dgetdrv());
    path[1] = ':';
    Dgetpath(path + 2, 0);
    if (path[strlen(path) - 1] != '\\') {
        strcat(path, "\\");
    }
    strcat(path, "*.PY");
    name[0] = '\0';

    if (fsel_exinput(path, name, &button, "MicroPython : quel script ?") == 0
            || button == 0 || name[0] == '\0') {
        appl_exit();
        return 0;
    }

    /* Move into the script's folder, so that the script's relative paths
     * -- and the log -- end up in the right place. */
    {
        char *fin = strrchr(path, '\\');
        if (fin) {
            *fin = '\0';
        }
        if (path[1] == ':') {
            Dsetdrv(path[0] - 'A');
        }
        Dsetpath(path + 2);
    }

    /* <script>.LOG next to the script */
    strcpy(logname, name);
    {
        char *point = strrchr(logname, '.');
        if (point) {
            *point = '\0';
        }
        strcat(logname, ".LOG");
    }

    tail[0] = 0;
    tail[1] = '\0';
    add("-q");
    add("-l");
    add(logname);
    add(name);

    /* Give up our AES application slot: under a single-tasking AES, the child
     * needs it for its own appl_init. */
    appl_exit();
    long ret = EFILNF;
    Supexec(read_cpu);
    for (unsigned i = (cpu_cookie >= 20) ? 0 : 1; i < sizeof(progs) / sizeof(progs[0]); i++) {
        ret = Pexec(0, progs[i], tail, 0L);
        if (ret != EFILNF) {
            break;
        }
    }
    if (appl_init() < 0) {
        return 0;
    }
    graf_mouse(ARROW, 0L);

    if (ret < 0) {
        strcpy(message, "[1][MPYRUN|MPY20.TTP / MPY00.TTP|n'a pas pu etre lance.|Erreur ");
    } else if (ret == 0) {
        strcpy(message, "[1][MPYRUN|");
        strcat(message, name);
        strcat(message, "|s'est termine normalement.|Journal : ");
        strcat(message, logname);
        strcat(message, "][ OK ]");
        form_alert(1, message);
        appl_exit();
        return 0;
    } else {
        strcpy(message, "[3][MPYRUN|");
        strcat(message, name);
        strcat(message, "|s'est arrete sur une erreur.|Voir ");
        strcat(message, logname);
        strcat(message, "|Code ");
    }
    {   /* return code in decimal */
        char n[12];
        int i = 0;
        long v = ret < 0 ? -ret : ret;
        char tmp[12];
        int k = 0;
        do {
            tmp[k++] = (char)('0' + v % 10);
            v /= 10;
        } while (v && k < 11);
        if (ret < 0) {
            n[i++] = '-';
        }
        while (k) {
            n[i++] = tmp[--k];
        }
        n[i] = '\0';
        strcat(message, n);
    }
    strcat(message, "][ OK ]");
    form_alert(1, message);
    appl_exit();
    return 0;
}
