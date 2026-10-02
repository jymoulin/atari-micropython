# ALLTEST.PY avec les tests GEM : a lancer par MPYRUN.PRG (ou MPY20.GTP),
# qui ne sait pas passer d'arguments au script. GEMTEST, WINTEST et
# MENUTEST demandent quelques gestes : suivre leurs consignes a l'ecran.
#
#   MPYRUN.PRG, puis choisir ALLGEM.PY
#   MPY20.TTP ALLGEM.PY i2c       les options d'ALLTEST.PY marchent aussi

import sys

sys.argv[1:1] = ["gem"]
with open("ALLTEST.PY") as f:
    exec(compile(f.read(), "ALLTEST.PY", "exec"), {"__name__": "__main__"})
