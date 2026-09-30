# Emetteurs natif, viper et assembleur m68k (phase 9).
#
#   MPY.TTP VIPERTEST.PY
#
# Chaque fonction compilee en code machine est comparee a son equivalent en
# Python ordinaire. Puis quelques mesures de vitesse.

import micropython
import gc
import time

ok = 0
ko = 0


def verifie(nom, obtenu, attendu):
    global ok, ko
    if obtenu == attendu:
        ok += 1
        print("   ok    %-38s %r" % (nom, obtenu))
    else:
        ko += 1
        print("   ECHEC %-38s %r au lieu de %r" % (nom, obtenu, attendu))


print("--- @micropython.native ---")


@micropython.native
def n_somme(n):
    s = 0
    for i in range(n):
        s += i
    return s


verifie("boucle", n_somme(1000), 499500)


@micropython.native
def n_appel(f, x):
    return f(x) + [len("abc")]


verifie("appel de Python, liste", n_appel(lambda v: [v, v * 2], 7), [7, 14, 3])


@micropython.native
def n_exceptions(n):
    # Les exceptions passent par setjmp dans le code natif, et le compteur
    # de boucle vit dans un registre sauve par setjmp : ce test verifie
    # NLR_BUF_IDX_LOCAL_1, calcule d'apres le jmp_buf de la MiNTlib.
    total = 0
    for i in range(n):
        try:
            if i % 3 == 0:
                raise ValueError(i)
            total += i
        except ValueError as e:
            total += 100 * e.args[0]
    return total


attendu = 0
for i in range(10):
    attendu += 100 * i if i % 3 == 0 else i
verifie("exceptions attrapees en boucle", n_exceptions(10), attendu)


@micropython.native
def n_propage():
    raise KeyError("dehors")


try:
    n_propage()
    verifie("exception propagee vers Python", "rien", "KeyError")
except KeyError as e:
    verifie("exception propagee vers Python", e.args[0], "dehors")


@micropython.native
def n_generateur(n):
    for i in range(n):
        yield i * i


verifie("generateur natif", list(n_generateur(6)), [0, 1, 4, 9, 16, 25])


@micropython.native
def n_texte(a, b):
    return "%s-%s" % (a, b.upper())


verifie("chaines", n_texte("atari", "ste"), "atari-STE")

print()
print("--- @micropython.viper ---")


@micropython.viper
def v_arith(a: int, b: int) -> int:
    return (a + b) * (a - b) + (a << 3) - (b >> 1)


verifie("arithmetique", v_arith(17, 5), (17 + 5) * (17 - 5) + (17 << 3) - (5 >> 1))
verifie("arithmetique, negatifs", v_arith(-9, 4), (-9 + 4) * (-9 - 4) + (-9 << 3) - (4 >> 1))


@micropython.viper
def v_mul(a: int, b: int) -> int:
    return a * b


verifie("multiplication (aide)", v_mul(12345, 6789), 12345 * 6789)
verifie("multiplication signee", v_mul(-300, 7), -2100)


@micropython.viper
def v_bits(a: int, b: int) -> int:
    return (a & b) | ((a ^ b) << 8)


verifie("et, ou, ou exclusif", v_bits(0x5A, 0x3C), (0x5A & 0x3C) | ((0x5A ^ 0x3C) << 8))


@micropython.viper
def v_cmp_signe(a: int, b: int) -> int:
    r = 0
    if a < b:
        r |= 1
    if a > b:
        r |= 2
    if a <= b:
        r |= 4
    if a >= b:
        r |= 8
    if a == b:
        r |= 16
    if a != b:
        r |= 32
    return r


def attendu_cmp(a, b):
    return ((a < b) * 1 | (a > b) * 2 | (a <= b) * 4 | (a >= b) * 8
            | (a == b) * 16 | (a != b) * 32)


for a, b in ((-1, 1), (1, -1), (5, 5), (-7, -3)):
    verifie("comparaisons signees %d, %d" % (a, b), v_cmp_signe(a, b), attendu_cmp(a, b))


@micropython.viper
def v_cmp_nonsigne(a: uint, b: uint) -> int:
    r = 0
    if a < b:
        r |= 1
    if a > b:
        r |= 2
    if a <= b:
        r |= 4
    if a >= b:
        r |= 8
    if a == b:
        r |= 16
    if a != b:
        r |= 32
    return r


# En non signe, 0xFFFFFFFF est le plus GRAND des entiers 32 bits.
verifie("comparaisons non signees 1, 0xFFFFFFFF",
        v_cmp_nonsigne(1, 0xFFFFFFFF), 1 | 4 | 32)
verifie("comparaisons non signees 0xFFFFFFFF, 1",
        v_cmp_nonsigne(0xFFFFFFFF, 1), 2 | 8 | 32)
verifie("comparaisons non signees 3, 3", v_cmp_nonsigne(3, 3), 4 | 8 | 16)


@micropython.viper
def v_booleen(a: int, b: int) -> int:
    # Une comparaison vaut 1 ou 0, comme sur les autres architectures.
    return int(a < b) + int(a > b) * 10


verifie("valeur d'une comparaison", v_booleen(1, 2), 1)
verifie("valeur d'une comparaison (2)", v_booleen(3, 2), 10)


@micropython.viper
def v_objet(a: int, b: int) -> bool:
    return a < b


verifie("comparaison rendue en objet", v_objet(1, 2), True)


@micropython.viper
def v_ptr(buf) -> int:
    p8 = ptr8(buf)
    p16 = ptr16(buf)
    p32 = ptr32(buf)
    p32[1] = 0x11223344           # octets 4..7
    s = p8[0] + p16[1] * 256      # lectures
    return s


b = bytearray(b"\x05\x00\x12\x34" + b"\x00" * 4)
r = v_ptr(b)


@micropython.viper
def v_nonsigne(buf) -> int:
    # ptr8 et ptr16 sont NON signes : 0xFF vaut 255, 0xFFFE vaut 65534.
    return ptr8(buf)[0] + ptr16(buf)[1] * 1000


verifie("ptr8/ptr16 non signes", v_nonsigne(bytearray(b"\xff\x00\xff\xfe")),
        255 + 65534 * 1000)
verifie("ptr32 : ordre des octets (big-endian)", bytes(b[4:8]), b"\x11\x22\x33\x44")
verifie("ptr8 et ptr16", r, 5 + 0x1234 * 256)


@micropython.viper
def v_somme(buf, n: int) -> int:
    p = ptr8(buf)
    s = 0
    for i in range(n):
        s += p[i]
    return s


data = bytearray(list(range(256)) * 4)
verifie("somme d'un tampon", v_somme(data, len(data)), sum(data))

print()
print("--- @micropython.asm_m68k ---")


@micropython.asm_m68k
def a_ajoute(a, b):
    movel([8, fp], d0)
    addl([12, fp], d0)


verifie("addition en assembleur", a_ajoute(1234, 4321), 5555)


@micropython.asm_m68k
def a_remplit(buf, n, v):
    movel([8, fp], a0)
    movel([12, fp], d1)
    movel([16, fp], d0)
    label(boucle)
    moveb(d0, [a0.inc])
    subql(1, d1)
    bne(boucle)


t = bytearray(16)
a_remplit(t, 10, 0x42)
verifie("remplissage d'un bytearray", bytes(t), b"\x42" * 10 + b"\x00" * 6)

print()
print("--- cache d'instructions ---")
# On fabrique 30 fonctions natives successives, en liberant chaque fois la
# precedente : le GC reutilise la meme memoire, et chaque fonction doit rendre
# SA valeur. ATTENTION, ce test ne prouve pas a lui seul le vidage du cache :
# sous Hatari il passe aussi sans (verifie le 28/09/2026). Le cache
# d'instructions du 68020 ne fait que 256 octets, et la compilation execute
# assez d'autre code pour le purger. Il compte surtout sur 68040/060.
faux = 0
for i in range(30):
    g = {}
    exec("@micropython.native\ndef f():\n    return %d\n" % (i * 7 + 1), g)
    if g["f"]() != i * 7 + 1:
        faux += 1
    g = None
    gc.collect()
verifie("30 fonctions regenerees, valeurs justes", faux, 0)

print()
print("--- vitesse (20000 tours) ---")


def p_boucle(n):
    s = 0
    for i in range(n):
        s += i
    return s


@micropython.viper
def v_boucle(n: int) -> int:
    s = 0
    for i in range(n):
        s += i
    return s


for nom, f in (("Python", p_boucle), ("native", n_somme), ("viper", v_boucle)):
    t0 = time.ticks_ms()
    f(20000)
    dt = time.ticks_diff(time.ticks_ms(), t0)
    print("   %-7s %6d ms" % (nom, dt))

print()
print("BILAN : %d reussites, %d echecs" % (ok, ko))
print("FIN DU TEST")
