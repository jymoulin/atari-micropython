# PRECOMPILED native code: a .mpy produced on the Mac by the port's mpy-cross.
#
#   cd ports/atari && make natmod      -> build/NATMOD.MPY
#   copy NATMOD.MPY alongside, then: MPY.TTP NATTEST.PY
#
# The mpy-cross must be the port's one (REPR_B, make mpy-cross-atari): native
# code freezes the encoding of constants, and a module produced by the
# ordinary mpy-cross would load without error and then compute wrong results.
import sys
sys.path.insert(0, "")
import natmod
ok = ko = 0
def verifie(nom, o, a):
    global ok, ko
    if o == a:
        ok += 1; print("   ok    %-28s %r" % (nom, o))
    else:
        ko += 1; print("   ECHEC %-28s %r au lieu de %r" % (nom, o, a))
verifie("module charge depuis .mpy", natmod.__file__.upper().endswith(".MPY"), True)
verifie("native : boucle", natmod.somme(1000), 499500)
verifie("native : exceptions", natmod.attrape(10), 1 + 3 + 5 + 7 + 9)
verifie("viper : somme ptr8", natmod.v_somme(bytearray(list(range(256))), 256), sum(range(256)))
verifie("viper : multiplication", natmod.v_mul(-321, 45), -321 * 45)
verifie("viper : non signe", natmod.v_cmp(1, 0xFFFFFFFF), 1)
verifie("asm_m68k", natmod.a_ajoute(40, 2), 42)
print("BILAN : %d reussites, %d echecs" % (ok, ko))
print("FIN DU TEST")
