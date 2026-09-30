# Barre de menus GEM : gem.App.menu, gem.Menu.
#
#   MPYRUN.PRG puis MENUTEST.PY       (il faut l'AES, et le mode GEM)
#
# Partie automatique : le test s'envoie lui-meme les messages MN_SELECTED que
# l'AES enverrait, et verifie rappels, titres re-normalises, coches et
# entrees grisees en relisant l'arbre d'objets. Partie manuelle : ouvre les
# menus a la souris, coche "Son", puis Fichier > Quitter.
# Sous Hatari (argument "auto"), la fin est simulee.

import gem
import sys
import time
import uos

auto = "auto" in sys.argv[1:]
ok = 0
ko = 0


def verifie(nom, condition, detail=""):
    global ok, ko
    if condition:
        ok += 1
        print("   ok    %s %s" % (nom, detail))
    else:
        ko += 1
        print("   ECHEC %s %s" % (nom, detail))


def hatari(cmd, attendre=True):
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


appels = []

with gem.App() as app:
    moi = gem._gem.ap_id()
    son = [False]

    def bascule_son():
        son[0] = not son[0]
        app._menu.check("Son", son[0])
        appels.append("son")

    m = app.menu([("Fichier", [("Ouvrir...", lambda: appels.append("ouvrir")),
                               None,
                               ("Quitter", app.stop)]),
                  ("Options", [("Son", bascule_son),
                               ("Couleurs", lambda: appels.append("couleurs"))])],
                 about=("A propos de MENUTEST...", lambda: appels.append("apropos")),
                 title="MENUTEST")
    verifie("menu_bar", m.shown)

    w = app.window("MENUTEST", kind=gem.NAME | gem.MOVER | gem.INFO)
    w.on_draw = lambda c, x, y, ww, hh: (c.color(fill=0),
                                         c.fill_rect(x, y, x + ww - 1, y + hh - 1))
    w.open()
    w.info("ouvre les menus, coche Options > Son, puis Fichier > Quitter")
    app.run(timeout=500)
    hatari("hatari-shortcut screenshot")

    def choisit(titre, entree):
        t, e = m.title_obj(titre), m.item_obj(entree)
        # L'AES met le titre en inverse video avant d'envoyer MN_SELECTED ;
        # on le fait aussi, pour verifier qu'on le remet bien en normal.
        gem.aes(33, (t, 0), (m.addr,))
        gem._gem.appl_write(moi, (gem.MN_SELECTED, moi, 0, t, e, 0, 0, 0))
        app.run(timeout=800)
        return t

    t = choisit("Fichier", "Ouvrir...")
    verifie("rappel Ouvrir", appels[-1:] == ["ouvrir"], "%r" % appels)
    verifie("titre remis en normal", not (m.state(t) & gem.SELECTED))

    choisit("MENUTEST", "A propos de MENUTEST...")
    verifie("rappel A propos (menu bureau)", appels[-1:] == ["apropos"])

    choisit("Options", "Son")
    verifie("coche posee", m.state("Son") & gem.CHECKED, "etat %d" % m.state("Son"))
    choisit("Options", "Son")
    verifie("coche retiree", not (m.state("Son") & gem.CHECKED))

    m.enable("Couleurs", False)
    verifie("entree grisee", m.state("Couleurs") & gem.DISABLED)
    m.enable("Couleurs", True)
    verifie("entree reactivee", not (m.state("Couleurs") & gem.DISABLED))

    try:
        m.check("Inexistant")
        verifie("libelle inconnu refuse", False)
    except ValueError:
        verifie("libelle inconnu refuse", True)

    # Manuel : jusqu'a Fichier > Quitter (app.stop). Trace des messages.
    print()
    print("--- a toi : menus a la souris, Options > Son, puis Fichier > Quitter ---")
    app.trace = []
    avant = len(appels)
    if auto:
        gem._gem.appl_write(moi, (gem.MN_SELECTED, moi, 0, m.title_obj("Fichier"),
                                  m.item_obj("Quitter"), 0, 0, 0))
    app.run()
    verifie("Quitter rend la main", True)
    noms = {10: "MN_SELECTED", 20: "REDRAW", 21: "TOPPED", 28: "MOVED"}
    print("   messages AES recus a la main : %d" % len(app.trace))
    for msg in app.trace:
        print("      %-11s %d %d" % (noms.get(msg[0], str(msg[0])), msg[3], msg[4]))
    print("   rappels a la main : %r" % appels[avant:])

print()
print("BILAN : %d reussites, %d echecs" % (ok, ko))
print("FIN DU TEST")
