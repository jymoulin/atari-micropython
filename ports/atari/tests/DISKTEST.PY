# Acces disque BIOS en LECTURE SEULE : Getbpb, Mediach, disk_read.
#
#   MPY.TTP DISKTEST.PY        lit le lecteur C:
#   MPY.TTP DISKTEST.PY D      lit le lecteur D:
#
# Aucune ecriture, nulle part : le port n'en offre pas. Eviter A: sans
# disquette dedans : TOS afficherait son alerte "lecteur ne repond pas".

import machine
import sys

lecteur = sys.argv[1] if len(sys.argv) > 1 else "C"

ok = 0
ko = 0


def verifie(nom, condition, detail=""):
    global ok, ko
    if condition:
        ok += 1
        print("   ok    %s %s" % (nom, detail))
    else:
        ko += 1
        print("   ECHEC %s %s" % (nom, detail))


print("--- arguments invalides ---")
for mauvais in (26, -1, "1"):
    try:
        machine.Getbpb(mauvais)
        verifie("refus de %r" % (mauvais,), False)
    except ValueError:
        verifie("refus de %r" % (mauvais,), True)

print()
print("--- lecteur %s: ---" % lecteur)
try:
    m = machine.Mediach(lecteur)
    verifie("Mediach", m in (0, 1, 2), "= %d (0 inchange, 1 peut-etre, 2 change)" % m)
except OSError as e:
    print("   Mediach : OSError %s -- pas de peripherique BIOS" % e)

try:
    bpb = machine.Getbpb(lecteur)
except OSError as e:
    # Le disque GEMDOS emule de Hatari n'a pas de peripherique BIOS derriere
    # lui : c'est la reponse attendue sous l'emulateur.
    print("   Getbpb : OSError %s -- pas de peripherique BIOS" % e)
    print("   (normal pour le disque GEMDOS emule de Hatari)")
    bpb = None

if bpb is not None:
    for k in ("recsiz", "clsiz", "clsizb", "rdlen", "fsiz", "fatrec",
              "datrec", "numcl", "bflags"):
        print("   %-7s %d" % (k, bpb[k]))
    verifie("recsiz plausible", bpb["recsiz"] in (512, 1024, 2048, 4096, 8192))
    fat = "FAT16" if bpb["bflags"] & 1 else "FAT12"
    taille = bpb["numcl"] * bpb["clsizb"] // 1024
    print("   %s, ~%d Ko de donnees" % (fat, taille))

    print()
    print("--- secteur de boot ---")
    s = machine.disk_read(lecteur, 0)
    verifie("longueur", len(s) == bpb["recsiz"], "%d octets" % len(s))
    print("   " + " ".join("%02x" % b for b in s[:16]))
    # Le BPB du secteur de boot est en little-endian (heritage MS-DOS).
    bps = s[11] | (s[12] << 8)
    verifie("octets/secteur du boot = recsiz", bps == bpb["recsiz"], "%d" % bps)
    somme = 0
    for i in range(0, 512, 2):
        somme = (somme + (s[i] << 8 | s[i + 1])) & 0xFFFF
    print("   somme de controle : 0x%04x %s" % (
        somme, "(0x1234 : secteur amorcable)" if somme == 0x1234 else ""))

    print()
    print("--- lecture multiple et premiere FAT ---")
    n = min(4, 65536 // bpb["recsiz"])
    s = machine.disk_read(lecteur, 1, n)
    verifie("%d secteurs d'un coup" % n, len(s) == n * bpb["recsiz"])

    # Dernier secteur de la partition. Au-dela de 32767, disk_read passe par
    # la forme longue de Rwabs (numero de secteur sur 32 bits) : c'est le
    # seul moyen de l'eprouver.
    dernier = bpb["datrec"] + bpb["numcl"] * bpb["clsiz"] - 1
    s = machine.disk_read(lecteur, dernier)
    forme = "forme longue" if dernier > 32767 else "forme courte"
    verifie("dernier secteur, n. %d (%s)" % (dernier, forme),
            len(s) == bpb["recsiz"])
    print("   " + " ".join("%02x" % b for b in s[:20]))
    if s[:4] == b"MPY-":
        print("   marqueur : %s" % bytes(s[:24]))

    try:
        machine.disk_read(lecteur, 0, 65536 // bpb["recsiz"] + 1)
        verifie("refus au-dela de 64 Ko", False)
    except ValueError:
        verifie("refus au-dela de 64 Ko", True)

print()
print("BILAN : %d reussites, %d echecs" % (ok, ko))
print("FIN DU TEST")
