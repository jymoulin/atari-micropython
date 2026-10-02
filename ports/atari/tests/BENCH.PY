# Mesure du cout du hook Ctrl-C et de l'etat du cache.
# Comparer MPY.TTP (hook tous les 64 opcodes) et MPY_H.TTP (tous les 4096).
import time
import sys

# Qui mesure quoi. En try : les anciennes versions (MPY_A, MPY_B) n'ont pas
# toutes les memes fonctions de platform.
try:
    import platform
    print("systeme          :", platform.system(), platform.release())
except Exception as e:
    print("systeme          : ? (%s)" % e)
try:
    import uos
    print("uname            :", uos.uname())
    print("multitache       :", platform.multitasking())
    # La date de la ROM tranche : TOS 2.06 est de 1991, EmuTOS recent.
    # (Redondant depuis que platform reconnait EmuTOS a son en-tete "ETOS",
    # garde pour les anciens binaires.)
    i = uos.osinfo()
    d = i["date"]
    print("ROM              : version %x, date (mm/jj/aaaa) %02x/%02x/%04x, EmuTOS %s" % (
        i["version"], (d >> 24) & 0xFF, (d >> 16) & 0xFF, d & 0xFFFF, i["emutos"]))
except Exception:
    pass
print("binaire          :", sys.version)

N = 20000
t0 = time.ticks_ms()
for i in range(N):
    pass
t = time.ticks_ms() - t0
print("boucle vide      : %d tours en %d ms = %d us/tour" % (N, t, t * 1000 // N))

N2 = 5000
t0 = time.ticks_ms()
x = 0
for i in range(N2):
    x = x + i * 3
t = time.ticks_ms() - t0
print("boucle arithm.   : %d tours en %d ms = %d us/tour" % (N2, t, t * 1000 // N2))

b = bytearray(8192)
t0 = time.ticks_ms()
for i in range(4096):
    b[i] = i & 0xFF
t = time.ticks_ms() - t0
print("ecriture tableau : 4096 en %d ms = %d us/octet" % (t, t * 1000 // 4096))
print("FIN DU TEST")
