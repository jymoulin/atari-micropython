# ALLTEST.PY with the GEM tests: to be started from MPYRUN.PRG (or MPY20.GTP),
# which cannot pass arguments to the script. GEMTEST, WINTEST and
# MENUTEST need a few user actions: follow their on-screen instructions.
#
#   MPYRUN.PRG, then choose ALLGEM.PY
#   MPY20.TTP ALLGEM.PY i2c       ALLTEST.PY options work too

import sys

sys.argv[1:1] = ["gem"]
with open("ALLTEST.PY") as f:
    exec(compile(f.read(), "ALLTEST.PY", "exec"), {"__name__": "__main__"})
