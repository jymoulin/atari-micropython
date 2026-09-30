# Les derniers manques GEMDOS de la phase 5 :
# putenv/unsetenv, utime (Fdatime) et dup/force/close (Fdup/Fforce).
#
#   MPY.TTP OSTEST.PY

import uos
import os
import sys
import time

# La redirection de poignees a plante la machine deux fois, et la cause n'est
# pas encore etablie : elle ne s'execute donc QUE sur demande explicite.
#   MPY.TTP OSTEST.PY          tout sauf la redirection
#   MPY.TTP OSTEST.PY force    ajoute la redirection
avec_force = "force" in sys.argv[1:]

ok = 0
ko = 0


def verifie(nom, condition, detail=""):
    global ok, ko
    if condition:
        ok += 1
        print("   ok   %s %s" % (nom, detail))
    else:
        ko += 1
        print("   ECHEC %s %s" % (nom, detail))


print("--- variables d'environnement ---")
uos.putenv("MPY_TEST", "bonjour")
verifie("putenv/getenv", uos.getenv("MPY_TEST") == "bonjour",
        repr(uos.getenv("MPY_TEST")))
uos.putenv("MPY_TEST", "rebonjour")
verifie("putenv ecrase", uos.getenv("MPY_TEST") == "rebonjour")
uos.unsetenv("MPY_TEST")
verifie("unsetenv", uos.getenv("MPY_TEST") is None)
verifie("variable absente", uos.getenv("MPY_ABSENT_XYZ") is None)

print()
print("--- horodatage des fichiers (utime) ---")
f = open("UTIME.TMP", "w")
f.write("horodatage")
f.close()

# 1990-06-15 12:30:00 en temps local, converti en epoch par le module time.
cible = time.mktime((1990, 6, 15, 12, 30, 0, 0, 0))
uos.utime("UTIME.TMP", (cible, cible))
lu = uos.stat("UTIME.TMP")[8]
t = time.localtime(lu)
print("   demande : 1990-06-15 12:30:00")
print("   relu    : %04d-%02d-%02d %02d:%02d:%02d" % t[0:6])
# GEMDOS ne garde les secondes que par pas de deux : 2 s d'ecart admis.
verifie("utime", abs(lu - cible) <= 2, "ecart %d s" % (lu - cible))

uos.utime("UTIME.TMP")          # sans argument : maintenant
maintenant = uos.stat("UTIME.TMP")[8]
verifie("utime sans argument", abs(maintenant - time.time()) <= 3)

print()
print("--- redirection de poignees (dup / force) ---")
if not avec_force:
    print("   ignoree. Relancer avec l'argument  force  pour l'inclure,")
    print("   et seulement apres que FORCETST.TTP soit passe sans bombe.")
else:
    # On detourne la poignee 1 -- notre propre sortie -- vers un fichier, on
    # ecrit, puis on la remet en place.
    #
    # os.redirect ne ferme JAMAIS la poignee sauvee : sous TOS et EmuTOS,
    # Fforce ne prend pas de reference, et fermer la poignee sauvee apres
    # l'avoir remise en place fait redemarrer la machine (constate le
    # 15/09/2026). Cela n'a pas suffi : le 16/09, trois bombes en MPY.TTP et
    # deux en MPY_STR.TTP au meme endroit. FORCETST.TTP sert a savoir lequel
    # des deux appels est en cause.
    sortie = open("FORCE.TMP", "w")
    with os.redirect(1, sortie):
        print("CECI VA DANS LE FICHIER")
    sortie.close()

    # "with" et pas open(...).read() : MicroPython ne compte pas les
    # references, un fichier non ferme le reste jusqu'au passage du GC.
    # TOS laisse effacer un fichier ouvert ; MagiC refuse, et il a raison
    # (EACCES a la ligne du remove, constate le 21/09/2026).
    with open("FORCE.TMP", "r") as f:
        contenu = f.read()
    verifie("redirection vers un fichier", "CECI VA DANS LE FICHIER" in contenu,
            repr(contenu))
    verifie("remise en place", True, "(tu lis cette ligne, donc oui)")
    uos.remove("FORCE.TMP")

uos.remove("UTIME.TMP")

print()
print("BILAN : %d reussites, %d echecs" % (ok, ko))
print("FIN DU TEST")
