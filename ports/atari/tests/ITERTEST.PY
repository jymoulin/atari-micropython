# Les iterateurs poses sur la pile (mp_obj_iter_buf_t).
#
#   MPY.TTP ITERTEST.PY
#
# Ecrit le 30/09/2026 au passage en 1.29. Le depot 1.28 heritait du port
# Amiga une quinzaine de retouches du coeur ("AMIGA FIX: force heap iter")
# qui allouaient ces iterateurs sur le tas : sur m68k la pile n'est alignee
# que sur 2 octets, et un pointeur d'adresse 4n+2 se lit comme un qstr en
# REPR_A. Notre port est en REPR_B, ou un pointeur n'a besoin que d'etre
# pair : les retouches sont inutiles et la branche 1.29 ne les reprend pas.
# Ce test passe par chacun des endroits qu'elles modifiaient.

try:
    from collections import deque
except ImportError:
    deque = None

ok = ko = 0


def check(nom, val, attendu):
    global ok, ko
    if val == attendu:
        ok += 1
        print("   ok    %-34s %r" % (nom, val))
    else:
        ko += 1
        print("   ECHEC %-34s %r au lieu de %r" % (nom, val, attendu))


def gen(n):
    for i in range(n):
        yield i


class It:                       # iterable ecrit en Python
    def __init__(self, n):
        self.n = n

    def __iter__(self):
        return iter(range(self.n))


def profond(n, f):
    # Appelle f a differentes profondeurs de pile, avec un nombre variable
    # d'arguments, pour faire varier l'alignement des variables locales.
    if n == 0:
        return f()
    if n & 1:
        return profond(n - 1, f)
    return (lambda a, b, c: profond(n - 1, f))(1, 2, 3)


def serie():
    r = []
    l = list(range(10))
    d = {"a": 1, "b": 2, "c": 3}
    s = {1, 2, 3, 4}
    r.append(all(l[1:]))                        # modbuiltins : all
    r.append(any(x > 8 for x in l))             # any
    r.append(all(gen(3)))
    r.append(sum(l))                            # sum
    r.append(sum(gen(5), 100))
    r.append(sum(It(4)))
    r.append(min(l))                            # min / max
    r.append(max(gen(7)))
    r.append(max(d.values()))
    r.append("-".join(str(x) for x in l[:4]))   # objstr : join
    r.append("".join(["a", "b", "c"]))
    r.append(3 in l)                            # runtime : operateur in
    r.append(3 in gen(5))
    r.append(9 in It(5))
    a, b, c = gen(3)                            # runtime : depaquetage
    r.append((a, b, c))
    a, *b = It(5)
    r.append((a, b))
    r.append((lambda *x: x)(*gen(3)))           # runtime : appel avec *args
    r.append((lambda *x: x)(0, *l[:2], *It(2)))
    r.append("a" in d.keys())                   # objdict : vues
    r.append("q" in d.keys())
    r.append(sorted(d.items()))
    r.append(sorted(s | set(gen(6))))           # objset
    r.append(s.issubset(range(10)))
    r.append(s.issuperset(gen(3)))
    r.append(s.isdisjoint(It(1)))
    t = set(s)
    t.update(gen(7), It(9))
    r.append(sorted(t))
    r.append(s == {4, 3, 2, 1})
    n = 0
    for i in l:                                 # vm : FOR_ITER sur la pile
        for j in gen(3):
            for k in d:
                n += 1
    r.append(n)
    r.append([x * y for x in l[:3] for y in It(3)])
    if deque:
        q = deque((), 8)                        # objdeque : extend
        q.extend(gen(5))
        r.append(list(q))
    return r


reference = serie()
check("resultat de reference", reference[:6], [True, True, False, 45, 110, 6])
check("join, in", reference[9:14], ["0-1-2-3", "abc", True, True, False])
check("depaquetage, *args", reference[14:18],
      [(0, 1, 2), (0, [1, 2, 3, 4]), (0, 1, 2), (0, 0, 1, 0, 1)])
check("boucles imbriquees", reference[27], 90)
for p in (1, 2, 3, 4, 7, 12, 25):
    check("meme resultat, profondeur %d" % p, profond(p, serie) == reference, True)


# Une exception au milieu d'une iteration ne doit rien laisser trainer.
def casse():
    for i in gen(10):
        for j in It(10):
            if i == 3 and j == 4:
                raise ValueError(i * 10 + j)


try:
    casse()
    v = None
except ValueError as e:
    v = e.args[0]
check("exception en pleine boucle", v, 34)

import gc
gc.collect()
check("apres collecte", serie() == reference, True)


# Blocs try / with : la VM range deux drapeaux dans les bits bas de ses
# pointeurs de pile (MP_TAGPTR_MAKE), qui doivent donc etre alignes sur 4.
# Sur un ST (tas a 4n+2, alloca aligne sur 2), le premier "return" depuis un
# "try" donnait une Address Error (02/10/2026) : ces cas-la.
def petit(x):
    try:
        return x + 1
    except ValueError:
        return -1


def gros(x):
    # Assez de variables locales pour que l'etat de la fonction soit pris
    # sur le tas plutot que sur la pile C.
    a0 = a1 = a2 = a3 = a4 = a5 = a6 = a7 = a8 = a9 = x
    b0 = b1 = b2 = b3 = b4 = b5 = b6 = b7 = b8 = b9 = x
    c0 = c1 = c2 = c3 = c4 = c5 = c6 = c7 = c8 = c9 = x
    l = [a0, a9, b0, b9, c0, c9]
    try:
        for i in It(3):
            try:
                if i == 2:
                    return sum(l) + i
            finally:
                l.append(0)
    except ValueError:
        return -1


class Ctx:
    def __enter__(self):
        return self

    def __exit__(self, *a):
        return False


def avec(x):
    with Ctx():
        for i in gen(5):
            if i == x:
                return i * 10
    return -1


def gen_try():
    try:
        yield 1
        yield 2
    finally:
        pass


check("return dans un try (petite fonction)", [petit(i) for i in range(3)], [1, 2, 3])
check("return dans try/finally (grosse)", gros(5), 32)
check("return dans un with", avec(3), 30)
check("generateur avec finally", list(gen_try()), [1, 2])
check("le tout, en profondeur", profond(9, lambda: (petit(1), gros(1), avec(2))), (2, 8, 20))

print("BILAN : %d reussites, %d echecs" % (ok, ko))
print("FIN DU TEST")
