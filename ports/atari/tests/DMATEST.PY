# Test DMA / RAM alternative.
#
# Le controleur DMA du ST ne sait pas adresser la RAM alternative. Si le
# driver IDE de la carte ne fait pas de tampon intermediaire, toute lecture
# de fichier vers un tampon du tas MicroPython (qui est en RAM alternative)
# revient corrompue -- silencieusement.
#
# A lancer depuis un VRAI disque IDE. Sous Hatari le disque GEMDOS passe par
# le systeme de fichiers de l'hote, sans le moindre DMA : le test y passera
# toujours, ce qui ne prouve rien.
#
#   MPY.TTP DMATEST.PY        tas en RAM alternative (le cas a risque)
#   MPY.TTP -s DMATEST.PY     tas en ST-RAM (le temoin)
#
# Le motif est construit UNE fois : a ~600 us le tour de boucle Python sur un
# STE, tout le reste doit se faire par comparaisons de blocs, qui sont en C.

import gc
import time

NAME = "DMATEST.TMP"
CHUNK = 8192
CHUNKS = 32                      # 256 Ko

print("--- preparation du motif ---")
t0 = time.ticks_ms()
base = bytearray(CHUNK)
for i in range(CHUNK):
    base[i] = (i * 7) & 0xFF
print("   %d octets en %d ms" % (CHUNK, time.ticks_ms() - t0))
print("   motif a l'adresse 0x%x" % id(base))

def estampille(n):
    # Quatre octets en tete, pour qu'un bloc lu a la place d'un autre se voie.
    base[0] = 0x5A
    base[1] = n & 0xFF
    base[2] = (n >> 8) & 0xFF
    base[3] = 0xA5

print("--- ecriture de %d Ko ---" % (CHUNK * CHUNKS // 1024))
t0 = time.ticks_ms()
f = open(NAME, "wb")
for n in range(CHUNKS):
    estampille(n)
    f.write(base)
f.close()
print("   ecrit en %d ms" % (time.ticks_ms() - t0))

print("--- relecture et verification ---")
buf = bytearray(CHUNK)
print("   tampon a l'adresse 0x%x" % id(buf))
gc.collect()

f = open(NAME, "rb")
erreurs = 0
premier = None
t0 = time.ticks_ms()
for n in range(CHUNKS):
    lu = f.readinto(buf)
    estampille(n)
    if lu != CHUNK:
        print("NON  bloc %d : %d octets lus au lieu de %d" % (n, lu, CHUNK))
        erreurs += 1
        continue
    if buf != base:                      # comparaison en C, pas en Python
        erreurs += 1
        if premier is None:
            # On ne descend a l'octet que pour le premier bloc fautif.
            for i in range(CHUNK):
                if buf[i] != base[i]:
                    premier = (n, i, base[i], buf[i])
                    break
f.close()
print("   relu en %d ms" % (time.ticks_ms() - t0))

if erreurs == 0:
    print("ok   %d blocs relus a l'identique" % CHUNKS)
else:
    print("NON  %d blocs sur %d corrompus" % (erreurs, CHUNKS))
    if premier:
        n, i, a, b = premier
        print("     premier ecart : bloc %d octet %d, attendu %02x, lu %02x"
              % (n, i, a, b))
    print("     => le DMA n'atteint pas ce tampon. Il faudra un tampon")
    print("        intermediaire en ST-RAM dans la couche fichier.")

print()
print("Pense a effacer %s" % NAME)
print("FIN DU TEST")
