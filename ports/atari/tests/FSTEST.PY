# Test de la couche systeme de fichiers (phase 2).
# Cree et detruit un sous-repertoire MPYTEST dans le repertoire courant.

import os
import errno

def chk(label, got, want):
    ok = "ok " if got == want else "NON"
    print("%s %-20s %r" % (ok, label, got))
    if got != want:
        print("       attendu: %r" % (want,))

print("--- la machine ---")
u = os.uname()
print("   uname   :", u)
print("   sysname %s, machine %s, release %s" % (u.sysname, u.machine, u.release))
print("   drives  :", os.drives())
print("   getcwd  :", os.getcwd())
print("   sep     : %r" % os.sep)

print("--- os.path (pur calcul) ---")
p = os.path
chk("join simple", p.join("C:\\DIR", "F.PY"), "C:\\DIR\\F.PY")
chk("join lecteur", p.join("C:", "F.PY"), "C:F.PY")
chk("join absolu", p.join("C:\\A", "\\B"), "\\B")
chk("join slash", p.join("C:/DIR", "F.PY"), "C:/DIR\\F.PY")
chk("splitdrive", p.splitdrive("C:\\A\\B"), ("C:", "\\A\\B"))
chk("split", p.split("C:\\A\\B.PY"), ("C:\\A", "B.PY"))
chk("split racine", p.split("C:\\B.PY"), ("C:\\", "B.PY"))
chk("basename", p.basename("C:\\A\\B.PY"), "B.PY")
chk("dirname", p.dirname("C:\\A\\B.PY"), "C:\\A")
chk("splitext", p.splitext("C:\\A\\B.PY"), ("C:\\A\\B", ".PY"))
chk("isabs oui", p.isabs("C:\\A"), True)
chk("isabs non", p.isabs("C:A"), False)
chk("normpath", p.normpath("C:\\A\\.\\B\\..\\C"), "C:\\A\\C")
chk("normpath slash", p.normpath("C:/A/B/../C"), "C:\\A\\C")

print("--- creation ---")
D = "MPYTEST"
if p.isdir(D):
    for f in os.listdir(D):
        os.remove(p.join(D, f))
    os.rmdir(D)

os.mkdir(D)
chk("isdir apres mkdir", p.isdir(D), True)
chk("isfile sur un dir", p.isfile(D), False)

try:
    os.mkdir(D)
    print("NON  mkdir en double n'a pas leve")
except OSError as e:
    # errno.EEXIST, pas 17 : la MiNTlib numerote ses erreurs d'apres les
    # codes GEMDOS, donc EEXIST vaut 85 sur cette plateforme.
    chk("mkdir en double", e.errno, errno.EEXIST)

f = open(p.join(D, "HELLO.TXT"), "w")
f.write("bonjour l'Atari\n")
f.close()

chk("isfile", p.isfile(p.join(D, "HELLO.TXT")), True)
chk("getsize", p.getsize(p.join(D, "HELLO.TXT")), 16)
chk("listdir", sorted(os.listdir(D)), ["HELLO.TXT"])

st = os.stat(p.join(D, "HELLO.TXT"))
print("   stat mode 0x%x, taille %d, mtime %d" % (st[0], st[6], st[8]))
chk("stat = fichier", st[0] & 0x8000 != 0, True)

print("--- relecture ---")
f = open(p.join(D, "HELLO.TXT"))
chk("contenu", f.read(), "bonjour l'Atari\n")
f.close()

print("--- renommage et effacement ---")
os.rename(p.join(D, "HELLO.TXT"), p.join(D, "SALUT.TXT"))
chk("apres rename", os.listdir(D), ["SALUT.TXT"])

print("--- walk ---")
os.mkdir(p.join(D, "SUB"))
f = open(p.join(D, "SUB", "IN.TXT"), "w")
f.write("x")
f.close()
vus = []
for rep, dirs, fics in os.walk(D):
    vus.append((rep, sorted(dirs), sorted(fics)))
for v in vus:
    print("   ", v)
chk("walk : 2 niveaux", len(vus), 2)

print("--- espace disque ---")
vfs = os.statvfs(os.getcwd())
print("   bloc %d octets, %d blocs, %d libres" % (vfs[0], vfs[2], vfs[3]))
print("   soit %d Ko libres" % (vfs[0] * vfs[3] // 1024))

print("--- menage ---")
os.remove(p.join(D, "SUB", "IN.TXT"))
os.rmdir(p.join(D, "SUB"))
os.remove(p.join(D, "SALUT.TXT"))
os.rmdir(D)
chk("tout efface", p.exists(D), False)

print()
print("FIN DU TEST")
