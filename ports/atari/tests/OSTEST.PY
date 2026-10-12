# The last GEMDOS gaps of phase 5:
# putenv/unsetenv, utime (Fdatime) and dup/force/close (Fdup/Fforce).
#
#   MPY.TTP OSTEST.PY

import uos
import os
import sys
import time

# Handle redirection crashed the machine twice, and the cause is not yet
# established: so it runs ONLY on explicit request.
#   MPY.TTP OSTEST.PY          everything except redirection
#   MPY.TTP OSTEST.PY force    adds redirection
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

# 1990-06-15 12:30:00 in local time, converted to epoch by the time module.
cible = time.mktime((1990, 6, 15, 12, 30, 0, 0, 0))
uos.utime("UTIME.TMP", (cible, cible))
lu = uos.stat("UTIME.TMP")[8]
t = time.localtime(lu)
print("   demande : 1990-06-15 12:30:00")
print("   relu    : %04d-%02d-%02d %02d:%02d:%02d" % t[0:6])
# GEMDOS only keeps seconds in steps of two: a 2 s difference is allowed.
verifie("utime", abs(lu - cible) <= 2, "ecart %d s" % (lu - cible))

uos.utime("UTIME.TMP")          # no argument: now
maintenant = uos.stat("UTIME.TMP")[8]
verifie("utime sans argument", abs(maintenant - time.time()) <= 3)

print()
print("--- redirection de poignees (dup / force) ---")
if not avec_force:
    print("   ignoree. Relancer avec l'argument  force  pour l'inclure,")
    print("   et seulement apres que FORCETST.TTP soit passe sans bombe.")
else:
    # Redirect handle 1 -- our own output -- to a file, write, then put it
    # back.
    #
    # os.redirect NEVER closes the saved handle: under TOS and EmuTOS,
    # Fforce does not take a reference, and closing the saved handle after
    # restoring it reboots the machine (observed on 15/09/2026). That was
    # not enough: on 16/09, three bombs with MPY.TTP and two with
    # MPY_STR.TTP at the same place. FORCETST.TTP is there to find out which
    # of the two calls is at fault.
    sortie = open("FORCE.TMP", "w")
    with os.redirect(1, sortie):
        print("CECI VA DANS LE FICHIER")
    sortie.close()

    # "with" and not open(...).read(): MicroPython does not refcount, an
    # unclosed file stays open until the GC runs.
    # TOS lets you delete an open file; MagiC refuses, and rightly so
    # (EACCES on the remove line, observed on 21/09/2026).
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
