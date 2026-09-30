/* MPYRUN.PRG -- lanceur GEM pour MicroPython.
 *
 * POURQUOI : un programme TOS (.TTP) lance depuis le bureau n'obtient pas la
 * souris et voit son dessin recouvert par la console -- TOS passe l'ecran en
 * mode texte. L'extension .GTP regle cela sous TOS 2.06 et EmuTOS, mais
 * MagiC ne la connait pas. Un .PRG, lui, est lance en mode GEM par tous les
 * bureaux : celui-ci choisit le script, puis lance MPY20.TTP (68020 et plus)
 * ou MPY00.TTP (toute machine) avec, selon le processeur.
 *
 * Il se met dans le dossier du script choisi, passe -q (rien a l'ecran, les
 * fenetres GEM y tiendraient mal compagnie a la console) et -l <script>.LOG,
 * puis affiche le code de retour.
 *
 * A poser a cote de MPY20.TTP et/ou MPY00.TTP.
 */

#include <gem.h>
#include <mint/osbind.h>
#include <string.h>

/* Les binaires possibles, du meilleur au plus general. MPY20.TTP exige un
 * 68020 : il n'est essaye que si le cookie _CPU le permet. MPY.TTP est
 * l'ancien nom, garde pour ne pas casser une installation existante. */
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

/* Ajoute s a tail, qui est une chaine Pascal (octet de longueur en tete). */
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
        /* Sans AES, rien a faire ici : c'est tout l'interet du lanceur. */
        Cconws("MPYRUN a besoin du GEM. Lance MPY.TTP directement.\r\n");
        Cconin();
        return 1;
    }
    graf_mouse(ARROW, 0L);

    /* Dossier courant -> "X:\chemin\*.PY" */
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

    /* Se placer dans le dossier du script, pour que les chemins relatifs du
     * script -- et le journal -- tombent au bon endroit. */
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

    /* <script>.LOG a cote du script */
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

    /* Rendre notre place d'application AES : sous un AES mono-tache, le fils
     * en a besoin pour son propre appl_init. */
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
    {   /* code de retour en decimal */
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
