# Le ramasse-miettes : des objets gardes doivent survivre aux collectes.
#
#   MPY.TTP GCTEST.PY
#
# Ecrit le 21/09/2026 apres un NameError sur une variable globale pourtant
# definie : le GC ne reconnaissait aucun pointeur quand la fin du tas
# n'etait pas alignee sur 16 octets, et liberait donc tout ce qui vivait.

import gc

garde = {}
for i in range(200):
    garde["cle%d" % i] = [i, str(i) * 3, (i, i + 1)]
temoin = "une chaine qui doit survivre"

print("tas : %d libres, %d utilises" % (gc.mem_free(), gc.mem_alloc()))

# Des dechets en quantite, pour forcer plusieurs collectes implicites.
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
