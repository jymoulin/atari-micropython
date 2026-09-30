# Code natif PRECOMPILE : un .mpy produit sur le Mac par le mpy-cross du port.
#
#   cd ports/atari && make natmod      -> build/NATMOD.MPY
#   copier NATMOD.MPY a cote, puis : MPY.TTP NATTEST.PY
#
# Le mpy-cross doit etre celui du port (REPR_B, make mpy-cross-atari) : le
# code natif fige l'encodage des constantes, et un module produit par le
# mpy-cross ordinaire se chargerait sans erreur puis calculerait faux.
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
