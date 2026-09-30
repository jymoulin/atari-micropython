/* FORCETST.TTP -- isoler ce qui plante dans Fdup / Fforce.
 *
 * MPY.TTP OSTEST.PY a fait redemarrer le STE, puis plante en bombes une fois
 * le Fclose fautif retire : trois bombes en RAM alternative, deux en ST-RAM,
 * soit erreur d'adresse et erreur de bus. Un numero de vecteur qui change
 * avec la RAM ou tourne le programme sent le pointeur fou.
 *
 * Les sources de la MiNTlib (~/Downloads/github/atari-mintlib) ont depuis
 * elimine une hypothese : unix/open.c rend directement la valeur de Fopen(),
 * donc les descripteurs de la MiNTlib SONT des poignees GEMDOS et Fforce
 * recoit bien un numero valide. L'etape 4 ci-dessous ne fait plus que le
 * confirmer sur la machine.
 *
 * Elles en ont aussi ouvert une autre : posix/system.c et stdio/popen.c font
 * exactement la sequence Fdup / Fforce / Fforce / Fclose, Fclose compris. La
 * MiNTlib la tient donc pour licite, alors qu'elle a fait redemarrer cette
 * machine-ci. Et unix/dup2.c porte cet avertissement : "use with caution,
 * TOS 1.4 still has double re-direction bug!". Reste donc a savoir si c'est
 * TOS, ou Geneva qui virtualise GEMDOS par-dessus.
 *
 *   FORCETST      affiche l'aide
 *   FORCETST 1    Fdup(1) seul, rien d'autre
 *   FORCETST 2    Fdup + Fforce aller-retour, SANS ecrire entre les deux
 *   FORCETST 3    idem mais avec une ecriture pendant la redirection
 *   FORCETST 4    compare la numerotation de Fcreate et de open()
 *
 * Aucune etape ne ferme la poignee sauvee : c'est cela qui avait fait
 * redemarrer la machine le 15/09, et c'est deja etabli.
 */

#include <mint/osbind.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define FICHIER "FORCETST.TMP"

/* Tout passe par GEMDOS handle 1, comme le port. */
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
        /* unix/open.c de la MiNTlib rend directement la valeur de Fopen(),
         * donc les deux numerotations doivent coincider. Confirmation. */
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

    /* Etapes 1 a 3 : la sequence proprement dite. */
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

    /* A partir d'ici l'affichage part dans le fichier : plus rien a l'ecran
     * tant qu'on n'a pas remis la poignee 1 en place. C'est normal. */
    dis("etape C : Fforce(1, cible) -- l'ecran se tait maintenant\r\n");
    long r1 = Fforce(1, (int)cible);

    long r2 = -1;
    if (etape == 3) {
        const char *msg = "CECI VA DANS LE FICHIER\r\n";
        r2 = Fwrite(1, (long)strlen(msg), msg);
    }

    dis("etape D : Fforce(1, sauve) -- retour a l'ecran\r\n");
    long r3 = Fforce(1, (int)sauve);

    /* Si on lit ceci, tout est passe. */
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
