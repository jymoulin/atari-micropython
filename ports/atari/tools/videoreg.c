/* VIDEOREG.PRG -- show (and optionally clear) the STE video registers.
 *
 * Symptom this exists for: after some resets the display is shifted by a
 * dozen pixels, with a band that belongs on the right appearing on the left.
 * That is the signature of the STE hardware scroll being left switched on:
 *
 *   $FF8265  HSCROLL, the fine horizontal scroll, 0-15 pixels
 *   $FF820F  line offset, extra words the shifter skips at each line end
 *
 * The two work together. HSCROLL shifts the picture left by N pixels and the
 * shifter fetches one extra word per line to fill the gap; the line offset
 * tells it how many words to skip. Leave HSCROLL non-zero and the whole
 * display slides, with the wrapped word showing up on the other side. TOS
 * clears them at a cold boot but not always on a reset, and nothing else
 * touches them unless a game or demo did.
 *
 *   VIDEOREG          show the registers
 *   VIDEOREG fix      clear HSCROLL and the line offset, then show them
 *
 * These registers live above $FF8000, which is supervisor-only on every ST,
 * hence the Supexec.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>

#define VBASE_HI    0xFFFF8201L     /* video base, high byte              */
#define VBASE_MID   0xFFFF8203L     /* video base, middle byte            */
#define VCOUNT_HI   0xFFFF8205L     /* video address counter, high        */
#define VCOUNT_MID  0xFFFF8207L     /* video address counter, middle      */
#define VCOUNT_LO   0xFFFF8209L     /* video address counter, low  (STE)  */
#define VBASE_LO    0xFFFF820DL     /* video base, low byte        (STE)  */
#define LINEOFF     0xFFFF820FL     /* line offset in words        (STE)  */
#define SYNCMODE    0xFFFF820AL     /* sync mode: 50/60 Hz                */
#define SHIFTMODE   0xFFFF8260L     /* resolution                         */
#define HSCROLL_NP  0xFFFF8264L     /* fine scroll, no prefetch    (STE)  */
#define HSCROLL     0xFFFF8265L     /* fine scroll                 (STE)  */

#define RB(a)   (*(volatile unsigned char *)(a))

static struct {
    unsigned char base_hi, base_mid, base_lo;
    unsigned char cnt_hi, cnt_mid, cnt_lo;
    unsigned char lineoff, sync, shift, hscroll;
} r;

static long read_regs(void)
{
    r.base_hi  = RB(VBASE_HI);
    r.base_mid = RB(VBASE_MID);
    r.base_lo  = RB(VBASE_LO);
    r.cnt_hi   = RB(VCOUNT_HI);
    r.cnt_mid  = RB(VCOUNT_MID);
    r.cnt_lo   = RB(VCOUNT_LO);
    r.lineoff  = RB(LINEOFF);
    r.sync     = RB(SYNCMODE);
    r.shift    = RB(SHIFTMODE);
    r.hscroll  = RB(HSCROLL);
    return 0;
}

static long clear_scroll(void)
{
    /* Order matters: stop the fine scroll first, then stop skipping words.
     * Writing $FF8264 as well covers the variant without prefetch. */
    RB(HSCROLL)    = 0;
    RB(HSCROLL_NP) = 0;
    RB(LINEOFF)    = 0;
    return 0;
}

int main(int argc, char **argv)
{
    int fix = (argc > 1 && strcmp(argv[1], "fix") == 0);

    if (fix) {
        Supexec(clear_scroll);
    }
    Supexec(read_regs);

    printf("Registres video STE%s\r\n", fix ? " (apres remise a zero)" : "");
    printf("  $FF8201/03/0D  base ecran   %02x%02x%02x\r\n",
           r.base_hi, r.base_mid, r.base_lo);
    printf("  $FF8205/07/09  compteur     %02x%02x%02x\r\n",
           r.cnt_hi, r.cnt_mid, r.cnt_lo);
    printf("  $FF820A        sync mode    %02x   (%s)\r\n",
           r.sync, (r.sync & 2) ? "50 Hz" : "60 Hz");
    printf("  $FF8260        resolution   %02x   (%s)\r\n",
           r.shift, (r.shift & 3) == 0 ? "basse" :
                    (r.shift & 3) == 1 ? "moyenne" : "haute");
    printf("  $FF820F        decalage     %02x   (%d mots sautes par ligne)\r\n",
           r.lineoff, r.lineoff);
    printf("  $FF8265        HSCROLL      %02x   (%d pixels)\r\n",
           r.hscroll & 0x0F, r.hscroll & 0x0F);
    printf("\r\n");

    if ((r.hscroll & 0x0F) != 0 || r.lineoff != 0) {
        printf("Le scroll materiel est ACTIF : c'est ce qui decale l'image.\r\n");
        if (!fix) {
            printf("Relancer avec l'argument  fix  pour le remettre a zero.\r\n");
        }
    } else {
        printf("Pas de scroll materiel : l'image n'est pas decalee par ces\r\n");
        printf("registres. Chercher ailleurs (mode video, overscan, moniteur).\r\n");
    }

    printf("\r\nUne touche pour sortir.\r\n");
    Cconin();
    return 0;
}
