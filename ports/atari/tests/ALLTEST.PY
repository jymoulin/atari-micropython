# Tous les tests d'un coup, pour comparer les configurations (TOS, EmuTOS,
# MagiC, 68000 / 68020 / 68030, ST / STE / Falcon...).
#
#   MPY20.TTP ALLTEST.PY                tests automatiques, sans risque
#   MPY20.TTP ALLTEST.PY i2c disk dma   en plus ceux qui demandent du materiel
#   MPYRUN.PRG puis ALLGEM.PY           le meme, plus GEMTEST, WINTEST et
#                                       MENUTEST (il faut le mode GEM)
#   MPY20.TTP ALLTEST.PY SMOKE VIPERTEST    seulement ceux-la
#
# Options :
#   gem    GEMTEST, WINTEST, MENUTEST (interactifs : suivre leurs consignes)
#   i2c    I2CTEST, RTCTEST : bus I2C de la TwiSTer. Ailleurs (Falcon, ST
#          d'origine) leurs registres n'existent pas : bombes assurees.
#   disk   DISKTEST : lecture BIOS du lecteur C:
#   dma    DMATEST : seulement depuis un vrai disque IDE
#   auto   sous Hatari : les tests GEM simulent les gestes de l'utilisateur,
#          et HWTEST n'attend pas de touche en monochrome
#   nobench  sans BENCH a la fin
#
# Sans option, les tests qui dependent du materiel se choisissent seuls :
# SNDTEST si le cookie _SND annonce le son DMA, NATTEST si NATMOD.MPY est
# la. Chaque test s'execute dans ce meme interpreteur, dans son propre
# espace de noms ; sa sortie s'affiche normalement, et ses lignes "ok",
# "NON", "ECHEC" et "BILAN" sont comptees au passage.
#
# A la fin : un tableau, et une ligne ajoutee a ALLTEST.SUM, qui garde
# ainsi une ligne par configuration essayee.

import sys
import gc
import time
import uos

try:
    import platform
except ImportError:
    platform = None

args = [a for a in sys.argv[1:]]
options = set(a.lower() for a in args)
choisis = [a.upper().replace(".PY", "") for a in args
           if a.lower() not in ("gem", "i2c", "disk", "dma", "auto", "nobench")]


def existe(nom):
    try:
        uos.stat(nom)
        return True
    except OSError:
        return False


def cookie(nom, defaut=0):
    v = uos.cookie(nom)
    return defaut if v is None else v


# (nom, arguments, condition, raison si saute)
son_dma = bool(cookie("_SND") & 2)
TESTS = [
    ("SMOKE", [], True, ""),
    ("ITERTEST", [], True, ""),
    ("GCTEST", [], True, ""),
    ("FSTEST", [], True, ""),
    ("OSTEST", [], True, ""),
    ("PFTEST", [], True, ""),
    ("HWTEST", ["auto"] if "auto" in options else [], True, ""),
    ("SNDTEST", [], son_dma, "pas de son DMA (_SND)"),
    ("VIPERTEST", [], True, ""),
    ("NATTEST", [], existe("NATMOD.MPY"), "NATMOD.MPY absent"),
    ("DISKTEST", [], "disk" in options, "option disk"),
    ("DMATEST", [], "dma" in options, "option dma"),
    ("I2CTEST", [], "i2c" in options, "option i2c"),
    ("RTCTEST", [], "i2c" in options, "option i2c"),
    ("GEMTEST", ["auto"] if "auto" in options else [], "gem" in options, "option gem"),
    ("WINTEST", ["auto"] if "auto" in options else [], "gem" in options, "option gem"),
    ("MENUTEST", ["auto"] if "auto" in options else [], "gem" in options, "option gem"),
    ("BENCH", [], "nobench" not in options, "option nobench"),
]

_print = print


class Compteur:
    def __init__(self):
        self.ok = 0
        self.ko = 0
        self.bilan = None       # (reussites, echecs) si le test l'a dit
        self.fin = False
        self.lignes = []        # pour BENCH : ses mesures

    def ligne(self, s):
        t = s.strip()
        if t.startswith("BILAN"):
            r = t.split(":", 1)[1].strip() if ":" in t else ""
            if r.startswith("OK"):
                self.bilan = (1, 0)
            elif r.startswith("ECHEC"):
                self.bilan = (0, 1)
            else:
                mots = r.replace(",", " ").split()
                try:
                    self.bilan = (int(mots[0]), int(mots[2]))
                except (ValueError, IndexError):
                    pass
        elif t.startswith("FIN DU TEST"):
            self.fin = True
        elif t.startswith("ok"):
            self.ok += 1
        elif t.startswith("NON") or t.startswith("ECHEC"):
            self.ko += 1
        if (t.startswith("boucle") or t.startswith("ecriture")) and "=" in t:
            # "boucle vide : 20000 tours en 4535 ms = 226 us/tour"
            # devient "boucle vide 226 us/tour"
            self.lignes.append("%s %s" % (" ".join(t.split(":")[0].split()),
                                          t.split("=")[-1].strip()))


compteur = None


def print_compte(*a, sep=" ", end="\n", file=None):
    if file is not None:
        _print(*a, sep=sep, end=end, file=file)
        return
    _print(*a, sep=sep, end=end)
    if compteur is not None:
        for s in sep.join(str(x) for x in a).split("\n"):
            compteur.ligne(s)


def configuration():
    morceaux = []
    if platform:
        for f in ("machine", "processor", "fpu", "system", "release"):
            try:
                morceaux.append(str(getattr(platform, f)()))
            except Exception:
                pass
    try:
        st, alt = uos.meminfo()
        morceaux.append("%dK ST + %dK alt" % (st // 1024, alt // 1024))
    except Exception:
        pass
    return " ".join(morceaux)


def lance(nom, targs):
    global compteur
    fichier = nom + ".PY"
    if not existe(fichier):
        return ("absent", 0, 0, 0)
    compteur = Compteur()
    argv = sys.argv[:]
    sys.argv[:] = [fichier] + targs
    g = {"__name__": "__main__", "__file__": fichier, "print": print_compte}
    erreur = None
    t0 = time.ticks_ms()
    try:
        with open(fichier) as f:
            code = compile(f.read(), fichier, "exec")
        exec(code, g)
    except KeyboardInterrupt:
        raise
    except SystemExit:
        pass
    except BaseException as e:
        erreur = e
        _print("*** exception dans", fichier)
        sys.print_exception(e)
    duree = time.ticks_diff(time.ticks_ms(), t0)
    sys.argv[:] = argv
    c = compteur
    compteur = None
    g = None
    code = None
    gc.collect()
    if c.bilan is not None:
        reussis, rates = c.bilan
    else:
        reussis, rates = c.ok, c.ko
    if erreur is not None:
        etat = "ERREUR"
    elif rates:
        etat = "ECHEC"
    elif not c.fin:
        etat = "INCOMPLET"
    else:
        etat = "ok"
    return (etat, reussis, rates, duree, c.lignes)


_print("=" * 60)
_print("ALLTEST :", configuration())
_print(sys.version)
_print("tas libre au depart : %d octets" % gc.mem_free())
_print("=" * 60)

resultats = []
interrompu = False
for nom, targs, cond, raison in TESTS:
    if choisis:
        if nom not in choisis:
            continue
    elif not cond:
        resultats.append((nom, "saute", 0, 0, 0, raison))
        continue
    _print()
    _print("#" * 20, nom, " ".join(targs), "#" * 20)
    try:
        r = lance(nom, targs)
    except KeyboardInterrupt:
        _print("*** interrompu (Ctrl-C)")
        resultats.append((nom, "INTERROMPU", 0, 0, 0, ""))
        interrompu = True
        break
    if r[0] == "absent":
        resultats.append((nom, "absent", 0, 0, 0, nom + ".PY introuvable"))
    else:
        etat, reussis, rates, duree, lignes = r
        detail = ", ".join(lignes) if nom == "BENCH" else ""
        resultats.append((nom, etat, reussis, rates, duree, detail))

_print()
_print("=" * 60)
_print("RESUME :", configuration())
_print("%-10s %-10s %8s %8s  %s" % ("test", "etat", "reussis", "echecs", "duree"))
n_ok = n_ko = n_saute = 0
for nom, etat, reussis, rates, duree, detail in resultats:
    if etat in ("saute", "absent"):
        n_saute += 1
        _print("%-10s %-10s %8s %8s  %s" % (nom, etat, "", "", detail))
        continue
    if etat == "ok":
        n_ok += 1
    else:
        n_ko += 1
    _print("%-10s %-10s %8d %8d  %5.1f s" % (nom, etat, reussis, rates, duree / 1000))
    if detail:
        _print("           " + detail)
_print("-" * 60)
_print("BILAN GLOBAL : %d tests ok, %d en echec, %d sautes%s" %
       (n_ok, n_ko, n_saute, " (interrompu)" if interrompu else ""))
_print("tas libre a la fin : %d octets" % gc.mem_free())

# Une ligne par configuration, cumulee d'un lancement a l'autre.
try:
    t = time.localtime()
    morceaux = []
    for nom, etat, reussis, rates, duree, detail in resultats:
        if etat in ("saute", "absent"):
            continue
        if etat == "ok":
            morceaux.append(nom)
        else:
            morceaux.append("%s:%s(%d/%d)" % (nom, etat, reussis, reussis + rates))
    bench = [d for n, e, r1, r2, du, d in resultats if n == "BENCH" and d]
    with open("ALLTEST.SUM", "a") as f:
        f.write("%04d-%02d-%02d %02d:%02d | %s | %s | %d ok, %d echec | %s%s\r\n" % (
            t[0], t[1], t[2], t[3], t[4], configuration(),
            sys.version.split(";")[-1].strip(), n_ok, n_ko, " ".join(morceaux),
            (" | " + bench[0]) if bench else ""))
    _print("ligne ajoutee a ALLTEST.SUM")
except Exception as e:
    _print("ALLTEST.SUM non ecrit :", e)
_print("FIN DU TEST")
