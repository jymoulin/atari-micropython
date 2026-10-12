# Smoke test of the Atari port. Run from MPY.TTP with SMOKE.PY as the
# command line. Each block prints ATTENDU: then the value obtained, so the
# check can be done by eye without a harness.

def chk(label, got, want):
    ok = "ok " if got == want else "NON"
    print("%s %-22s %r" % (ok, label, got))
    if got != want:
        print("       attendu: %r" % (want,))

print("--- entiers ---")
chk("int 32 bits", 1000000 * 1000, 1000000000)
chk("mpz (2**100)", 2 ** 100, 1267650600228229401496703205376)
chk("mpz negatif", (-3) ** 41, -36472996377170786403)
chk("decalage", 1 << 62, 4611686018427387904)
chk("division", 7 // -2, -4)

print("--- flottants ---")
chk("float simple", 1.5 * 2, 3.0)
chk("repr float", "%.3f" % (1 / 3), "0.333")

print("--- chaines ---")
chk("f-string", f"{2 + 2}={4}", "4=4")
chk("slice", "micropython"[5:], "python")
chk("encode utf8", "e".encode(), b"e")
print("   accents (doivent s'afficher):", "éàçüßœ")

print("--- structures ---")
chk("dict comp", {k: k * k for k in range(4)}, {0: 0, 1: 1, 2: 4, 3: 9})
chk("set", sorted({3, 1, 2, 1}), [1, 2, 3])
chk("sorted key", sorted(["bb", "a"], key=len), ["a", "bb"])

print("--- exceptions ---")
try:
    1 / 0
except ZeroDivisionError as e:
    print("ok  ZeroDivisionError    ", e)

print("--- modules ---")
import sys
print("   sys.platform =", sys.platform, " version =", sys.version)
print("   sys.path =", sys.path)

import gc
gc.collect()
print("   gc.mem_free =", gc.mem_free(), " mem_alloc =", gc.mem_alloc())

import re
chk("re", re.match(r"(\d+)-(\d+)", "12-34").group(2), "34")

import time
print("   time.localtime() =", time.localtime())
t0 = time.ticks_ms()
for _ in range(20000):
    pass
print("   20000 tours de boucle vide:", time.ticks_ms() - t0, "ms")

import binascii
chk("binascii", binascii.hexlify(b"AZ"), b"415a")

import base64
chk("base64 (gele)", base64.b64encode(b"Atari"), b"QXRhcmk=")

import deflate
import io
buf = io.BytesIO()
with deflate.DeflateIO(buf, deflate.ZLIB) as d:
    d.write(b"atari" * 20)
comp = buf.getvalue()
buf = io.BytesIO(comp)
with deflate.DeflateIO(buf, deflate.ZLIB) as d:
    back = d.read()
chk("deflate aller-retour", back, b"atari" * 20)
print("   100 octets compresses en", len(comp))

print("--- fichiers ---")
f = open("SMOKE.PY")
first = f.readline()
f.close()
chk("lecture fichier", first.startswith("#"), True)

print()
print("FIN DU TEST")
