# Test of the platform layer (phase 3): cookie jar and platform module.

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
    # Many cookies hold two 16-bit words, others a pointer.
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
