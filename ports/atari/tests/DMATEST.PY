# DMA / alternate RAM test.
#
# The ST DMA controller cannot address alternate RAM. If the board's IDE
# driver does not use an intermediate bounce buffer, any file read into a
# buffer on the MicroPython heap (which lives in alternate RAM) comes back
# corrupted -- silently.
#
# Run from a REAL IDE disk. Under Hatari the GEMDOS drive goes through the
# host file system, with no DMA at all: the test will always pass there,
# which proves nothing.
#
#   MPY.TTP DMATEST.PY        heap in alternate RAM (the risky case)
#   MPY.TTP -s DMATEST.PY     heap in ST-RAM (the control)
#
# The pattern is built ONCE: at ~600 us per Python loop iteration on a
# STE, everything else must be done by block comparisons, which are in C.

import gc
import time

NAME = "DMATEST.TMP"
CHUNK = 8192
CHUNKS = 32                      # 256 KB

print("--- preparation du motif ---")
t0 = time.ticks_ms()
base = bytearray(CHUNK)
for i in range(CHUNK):
    base[i] = (i * 7) & 0xFF
print("   %d octets en %d ms" % (CHUNK, time.ticks_ms() - t0))
print("   motif a l'adresse 0x%x" % id(base))

def estampille(n):
    # Four header bytes, so that a block read in place of another shows up.
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
    if buf != base:                      # comparison in C, not in Python
        erreurs += 1
        if premier is None:
            # Only drill down to the byte for the first faulty block.
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
