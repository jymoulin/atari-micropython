/* RUNTEST.PRG -- lanceur pour les tests automatiques sous Hatari.
 *
 * Hatari sait demarrer un programme tout seul (--auto) et s'arreter apres
 * un nombre donne de VBL (--run-vbls), mais --auto ne passe aucun argument.
 * Ce lanceur lit donc sa ligne de commande dans RUNTEST.ARG, a cote de lui,
 * et lance MPY.TTP avec. Exemple de RUNTEST.ARG :
 *     -l RESULT.TXT SMOKE.PY
 * Le journal -l arrive directement dans le dossier du Mac qui sert de disque
 * GEMDOS a Hatari : c'est lui qu'on relit, pas l'ecran.
 *
 * Outil de developpement uniquement, rien a faire sur la vraie machine.
 */

#include <mint/osbind.h>
#include <string.h>

/* Se placer sur le lecteur qui porte RUNTEST.ARG. Quand une image de disque
 * occupe C:, le disque GEMDOS de Hatari passe en D: ou plus loin, et rien ne
 * garantit que --auto nous y laisse comme lecteur courant. */
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
    /* Couper a la premiere fin de ligne. */
    for (long i = 0; i < n; i++) {
        char c = tail[1 + i];
        if (c == '\r' || c == '\n') {
            n = i;
            break;
        }
    }
    tail[0] = (char)n;          /* ligne de commande GEMDOS : chaine Pascal */
    tail[1 + n] = '\0';

    /* Heure (compteur 200 Hz) juste avant le lancement, pour que MPY -t
     * puisse mesurer le chargement et la relocation, qui precedent main. */
    {
        long t0 = Supexec(lire_hz200);
        long f = Fcreate("RUNTEST.T0", 0);
        if (f >= 0) {
            Fwrite((int)f, 4, &t0);
            Fclose((int)f);
        }
    }
    long r = Pexec(0, "MPY.TTP", tail, NULL);

    /* Le code de retour, pour le cas ou MPY.TTP n'aurait rien ecrit. */
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
