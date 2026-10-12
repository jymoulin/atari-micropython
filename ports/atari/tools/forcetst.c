/* FORCETST.TTP -- isolate what crashes in Fdup / Fforce.
 *
 * MPY.TTP OSTEST.PY rebooted the STE, then crashed with bombs once the
 * faulty Fclose was removed: three bombs in alternate RAM, two in ST-RAM,
 * i.e. address error and bus error. A vector number that changes with the
 * RAM the program runs in smells of a wild pointer.
 *
 * The MiNTlib sources (~/Downloads/github/atari-mintlib) have since
 * ruled out one hypothesis: unix/open.c returns the value of Fopen()
 * directly, so MiNTlib descriptors ARE GEMDOS handles and Fforce does
 * receive a valid number. Step 4 below only confirms it on the
 * machine.
 *
 * They also opened another one: posix/system.c and stdio/popen.c do
 * exactly the sequence Fdup / Fforce / Fforce / Fclose, Fclose included. So
 * MiNTlib considers it legal, even though it rebooted this very machine.
 * And unix/dup2.c carries this warning: "use with caution,
 * TOS 1.4 still has double re-direction bug!". So it remains to find out
 * whether it is TOS, or Geneva virtualising GEMDOS on top of it.
 *
 *   FORCETST      show the help
 *   FORCETST 1    Fdup(1) alone, nothing else
 *   FORCETST 2    Fdup + Fforce round trip, WITHOUT writing in between
 *   FORCETST 3    same but with a write during the redirection
 *   FORCETST 4    compare the numbering of Fcreate and of open()
 *
 * No step closes the saved handle: that is what rebooted the machine
 * on 15/09, and it is already established.
 */

#include <mint/osbind.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define FICHIER "FORCETST.TMP"

/* Everything goes through GEMDOS handle 1, like the port. */
static void dis(const char *s) {
    Fwrite(1, (long)strlen(s), s);
}

static void disl(const char *etiquette, long v) {
    char buf[96];
    int n = snprintf(buf, sizeof(buf), "%s = %ld (0x%lx)\r\n", etiquette, v, v);
    Fwrite(1, (long)n, buf);
}

static void pause_touche(void) {
    dis("  -- une touche --\r\n");
    Crawcin();
}

int main(int argc, char **argv) {
    int etape = (argc > 1) ? atoi(argv[1]) : 0;

    dis("\r\nFORCETST -- Fdup / Fforce, etape par etape\r\n\r\n");

    if (etape < 1 || etape > 4) {
        dis("  FORCETST 1   Fdup(1) seul\r\n");
        dis("  FORCETST 2   Fdup + Fforce aller-retour, sans ecrire\r\n");
        dis("  FORCETST 3   idem, avec une ecriture pendant\r\n");
        dis("  FORCETST 4   numerotation Fcreate contre open()\r\n\r\n");
        dis("Lancer dans l'ordre. La derniere ligne affichee avant les\r\n");
        dis("bombes designe l'appel fautif.\r\n");
        pause_touche();
        return 0;
    }

    if (etape == 4) {
        /* MiNTlib's unix/open.c returns the value of Fopen() directly,
         * so the two numberings must match. Confirmation. */
        long h = Fcreate(FICHIER, 0);
        disl("Fcreate  rend", h);
        if (h >= 0) {
            Fclose((int)h);
        }
        int fd = open(FICHIER, O_WRONLY | O_TRUNC);
        disl("open()   rend", (long)fd);
        if (fd >= 0) {
            close(fd);
        }
        dis("\r\nAttendu : memes valeurs ou valeurs voisines, la MiNTlib\r\n");
        dis("rendant directement ce que rend Fopen(). Un ecart franc\r\n");
        dis("contredirait ses sources et expliquerait tout.\r\n");
        Fdelete(FICHIER);
        pause_touche();
        return 0;
    }

    /* Steps 1 to 3: the sequence itself. */
    dis("etape A : Fcreate du fichier de destination\r\n");
    long cible = Fcreate(FICHIER, 0);
    disl("  poignee cible", cible);
    if (cible < 0) {
        dis("Fcreate a echoue, on s'arrete.\r\n");
        pause_touche();
        return 1;
    }

    dis("etape B : Fdup(1)\r\n");
    dis("  (si ca bombe ici, c'est Fdup, et on abandonne la redirection)\r\n");
    long sauve = Fdup(1);
    disl("  poignee sauvee", sauve);
    if (sauve < 0) {
        dis("Fdup a echoue proprement, on s'arrete.\r\n");
        Fclose((int)cible);
        Fdelete(FICHIER);
        pause_touche();
        return 1;
    }

    if (etape == 1) {
        dis("\r\netape 1 terminee sans bombe : Fdup fonctionne.\r\n");
        dis("La poignee sauvee n'est PAS fermee, volontairement.\r\n");
        Fclose((int)cible);
        Fdelete(FICHIER);
        pause_touche();
        return 0;
    }

    /* From here on output goes to the file: nothing more on screen
     * until handle 1 has been put back. This is expected. */
    dis("etape C : Fforce(1, cible) -- l'ecran se tait maintenant\r\n");
    long r1 = Fforce(1, (int)cible);

    long r2 = -1;
    if (etape == 3) {
        const char *msg = "CECI VA DANS LE FICHIER\r\n";
        r2 = Fwrite(1, (long)strlen(msg), msg);
    }

    dis("etape D : Fforce(1, sauve) -- retour a l'ecran\r\n");
    long r3 = Fforce(1, (int)sauve);

    /* If we read this, everything went through. */
    dis("\r\nrevenu a l'ecran.\r\n");
    disl("  Fforce(1, cible) a rendu", r1);
    if (etape == 3) {
        disl("  Fwrite pendant la redirection", r2);
    }
    disl("  Fforce(1, sauve) a rendu", r3);

    Fclose((int)cible);

    if (etape == 3) {
        char relu[64];
        memset(relu, 0, sizeof(relu));
        long h = Fopen(FICHIER, 0);
        if (h >= 0) {
            long n = Fread((int)h, (long)sizeof(relu) - 1, relu);
            Fclose((int)h);
            disl("  octets relus", n);
            dis("  contenu : ");
            dis(relu);
            dis("\r\n");
        }
    }

    Fdelete(FICHIER);
    dis("\r\netape terminee sans bombe.\r\n");
    pause_touche();
    return 0;
}
