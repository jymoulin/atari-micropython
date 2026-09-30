# Test de la couche plateforme (phase 3) : cookie jar et module platform.

import platform
import uos

print("--- ce que dit la machine ---")
print("  ", platform.atari_info())
print()
print("   machine    :", platform.machine())
print("   processeur :", platform.processor())
print("   FPU        :", platform.fpu())
print("   video      :", platform.video())
print("   systeme    :", platform.system(), platform.release())
print("   multitache :", platform.multitasking())
print("   platform() :", platform.platform())
print("   uname()    :", platform.uname())

print()
print("--- le cookie jar au complet ---")
c = uos.cookies()
for nom in sorted(c.keys()):
    v = c[nom]
    # Beaucoup de cookies rangent deux mots de 16 bits, d'autres un pointeur.
    print("   %-5s 0x%08x  (%d, %d)" % (nom, v, v >> 16, v & 0xFFFF))
print("   %d cookies" % len(c))

print()
print("--- acces direct ---")
print("   _CPU   :", uos.cookie("_CPU"))
print("   absent :", uos.cookie("ZZZZ"))
st, alt = uos.meminfo()
print("   memoire: %d Ko ST-RAM, %d Ko alternative" % (st // 1024, alt // 1024))

print()
print("FIN DU TEST")
