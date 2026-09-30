# VDI et AES : module gem.
#
#   MPY.TTP GEMTEST.PY          a lancer DEPUIS LE BUREAU (il faut l'AES)
#
# Le dessin se verifie tout seul : chaque trace est relu pixel par pixel avec
# v_get_pixel. Puis trois interactions, a faire a la main :
#   - une alerte a deux boutons : appuie sur Return (bouton par defaut, Non) ;
#   - le selecteur de fichiers : appuie sur Return (OK, GEMTEST.PY propose) ;
#   - une touche : appuie sur a.
# Sous Hatari (argument "auto"), ces touches sont envoyees par le script
# hatari-test.sh, et une capture d'ecran est prise apres le dessin.

import gem
import sys
import time
import uos

auto = "auto" in sys.argv[1:]
ok = 0
ko = 0
# Pendant le dessin, RIEN ne doit s'afficher : la console ecrit sur le meme
# ecran, et v_get_pixel relirait son texte au lieu de nos traces.
differe = []


def verifie(nom, condition, detail=""):
    global ok, ko
    if condition:
        ok += 1
        ligne = "   ok    %s %s" % (nom, detail)
    else:
        ko += 1
        ligne = "   ECHEC %s %s" % (nom, detail)
    if differe is not None:
        differe.append(ligne)
    else:
        print(ligne)


def hatari(cmd, attendre=True):
    """Sous Hatari : fait executer une commande par hatari-test.sh.

    attendre=True attend qu'elle soit faite (capture d'ecran). Pour une
    touche, attendre=False et appeler le dialogue AUSSITOT : le relais
    l'envoie une demi-seconde plus tard, dialogue affiche. Envoyee avant,
    elle serait prise par le guet du Ctrl-C et l'AES ne la verrait pas.
    """
    if not auto:
        return
    with open("HATARI.CMD", "w") as f:
        f.write(cmd + "\n")
    if not attendre:
        return
    t0 = time.ticks_ms()
    while time.ticks_diff(time.ticks_ms(), t0) < 20000:
        try:
            uos.stat("HATARI.CMD")
        except OSError:
            return
        time.sleep_ms(50)
    print("   (Hatari n'a pas repondu a : %s)" % cmd)


print("--- station de travail (resultats apres le dessin) ---")
with gem.Screen() as s:
    verifie("AES", gem._gem.aes_version() > 0, "version 0x%04x" % gem._gem.aes_version())
    verifie("handle VDI", s.handle > 0, "%d" % s.handle)
    verifie("taille", s.width >= 320 and s.height >= 200,
            "%d x %d, %d couleurs" % (s.width, s.height, s.colors))
    verifie("police systeme", s.char_w > 0 and s.char_h > 0,
            "%d x %d" % (s.char_w, s.char_h))

    s.clear()
    verifie("clear", s.pixel(5, 5) == 0 and s.pixel(s.width - 5, s.height - 5) == 0)

    s.color(line=1, fill=1, text=1)
    s.fill_rect(10, 10, 60, 40)
    verifie("fill_rect dedans", s.pixel(30, 25) == 1)
    verifie("fill_rect dehors", s.pixel(70, 25) == 0)

    # Tout en fractions de l'ecran : 320 x 200 (Hatari, ST basse) comme
    # 640 x 400 (monochrome) doivent tenir, sans rien dessiner au-dela du
    # bord -- le VDI ne coupe pas sans clip et le trace revient par la gauche.
    W, H = s.width, s.height
    s.line(W // 4, 10, W // 2, 10)
    verifie("line", s.pixel(W * 3 // 8, 10) == 1 and s.pixel(W * 3 // 8, 12) == 0)

    s.rect(W * 5 // 8, 10, W * 7 // 8, 40)
    verifie("rect", s.pixel(W * 5 // 8, 25) == 1 and s.pixel(W * 3 // 4, 25) == 0)

    r = H // 10
    cx, cy = W // 4, H // 4 + r
    s.circle(cx, cy, r, fill=True)
    verifie("cercle plein", s.pixel(cx, cy) == 1 and s.pixel(cx + r + 5, cy) == 0)

    cx2 = W * 3 // 4
    s.circle(cx2, cy, r)
    verifie("cercle creux", s.pixel(cx2, cy) == 0 and s.pixel(cx2 + r, cy) == 1)

    s.polygon((10, H // 2, 60, H // 2, 35, H // 2 + 30))
    verifie("polygone", s.pixel(35, H // 2 + 10) == 1 and s.pixel(12, H // 2 + 25) == 0)

    msg = "Atari éàç"
    y = H * 3 // 4
    s.text(10, y, msg)
    encre = 0
    for yy in range(y, y + s.char_h):
        for xx in range(10, 10 + s.char_w * len(msg), 2):
            if s.pixel(xx, yy) == 1:
                encre += 1
    verifie("texte", encre > 20, "%d pixels encres (echantillon)" % encre)

    s.mode(gem.XOR)
    s.fill_rect(10, 10, 60, 40)
    verifie("mode XOR efface", s.pixel(30, 25) == 0)
    s.mode(gem.REPLACE)

    s.text(10, y + 2 * s.char_h, "GEMTEST : dessin termine")
    hatari("hatari-shortcut screenshot")

for ligne in differe:
    print(ligne)
differe = None

print()
print("--- alerte ---")
print("   (l'alerte le dit elle-meme)")
hatari("hatari-event keypress 28", attendre=False)
b = gem.alert("GEMTEST, alerte GEM depuis\nMicroPython. Accents : éàç\nClique un bouton, ou Return.",
              ("Oui", "Non"), default=2, icon=gem.QUESTION)
verifie("alert", b == 2 if auto else b in (1, 2), "bouton %d" % b)

try:
    gem.alert("x" * 200)
    verifie("alerte trop longue refusee", False)
except ValueError:
    verifie("alerte trop longue refusee", True)

print()
print("--- selecteur de fichiers ---")
print("   (le titre du selecteur le dit)")
hatari("hatari-event keypress 28", attendre=False)
f = gem.fileselect("*.PY", name="GEMTEST.PY",
                   title="GEMTEST : valide par OK")
print("   rendu : %r" % f)
if auto:
    verifie("fileselect", f is not None and f.endswith("\\GEMTEST.PY"))
else:
    verifie("fileselect", f is None or "\\" in f)

print()
print("--- clavier ---")
# La consigne passe par une alerte : avec -q, rien ne s'affiche a l'ecran,
# et l'utilisateur ne pouvait pas deviner ce qu'on attendait de lui.
hatari("hatari-event keypress 28", attendre=False)
gem.alert("Apres ce OK,\ntape n'importe quelle touche.", ("OK",))
hatari("hatari-event keypress a", attendre=False)
k = gem.wait_key()
# A la main, n'importe quelle touche fait l'affaire : ce qu'on verifie, c'est
# que wait_key rende un code de touche plausible, pas laquelle.
verifie("wait_key", (k & 0xFF) != 0 or (k >> 8) != 0, "0x%04x" % k)

print()
print("BILAN : %d reussites, %d echecs" % (ok, ko))
print("FIN DU TEST")
