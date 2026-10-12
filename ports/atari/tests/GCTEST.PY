# The garbage collector: objects that are kept must survive collections.
#
#   MPY.TTP GCTEST.PY
#
# Written on 21/09/2026 after a NameError on a global variable that was
# defined: the GC recognised no pointer when the end of the heap was not
# 16-byte aligned, and therefore freed everything that was alive.

import gc

garde = {}
for i in range(200):
    garde["cle%d" % i] = [i, str(i) * 3, (i, i + 1)]
temoin = "une chaine qui doit survivre"

print("tas : %d libres, %d utilises" % (gc.mem_free(), gc.mem_alloc()))

# Plenty of garbage, to force several implicit collections.
for tour in range(6):
    dechets = [bytearray(1024) for _ in range(200)]
    dechets = None
    gc.collect()

bon = 0
for i in range(200):
    v = garde.get("cle%d" % i)
    if v == [i, str(i) * 3, (i, i + 1)]:
        bon += 1
print("objets gardes intacts : %d / 200" % bon)
print("temoin :", temoin)
print("BILAN :", "OK" if bon == 200 and temoin == "une chaine qui doit survivre" else "ECHEC")
print("FIN DU TEST")
