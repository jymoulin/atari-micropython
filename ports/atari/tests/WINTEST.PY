# GEM windows and event loop: gem.App, gem.Window.
#
#   MPY.TTP WINTEST.PY          from the desktop (needs the AES)
#
# The test sends itself (appl_write) the messages the AES would send
# -- redraw, move, full screen, close -- and checks the effect of each,
# pixels included. At the end, a window stays open for manual testing:
# move it, resize it, press a key, then close it with its close box.
# Under Hatari (argument "auto"), the key and the close are simulated.

import gem
import sys
import time
import uos

auto = "auto" in sys.argv[1:]
ok = 0
ko = 0
differe = []        # nothing on screen during pixel checks


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


dessins = [0]


def dessine(c, x, y, w, h):
    dessins[0] += 1
    c.color(fill=0)
    c.fill_rect(x, y, x + w - 1, y + h - 1)          # white background
    c.color(fill=1, line=1, text=1)
    c.fill_rect(x + w // 2 - 10, y + h // 2 - 10, x + w // 2 + 10, y + h // 2 + 10)
    c.rect(x + 2, y + 2, x + w - 3, y + h - 3)
    c.text(x + 6, y + 6, "Fenetre MicroPython")


with gem.App() as app:
    moi = gem._gem.ap_id()
    dx, dy, dw, dh = app.desk
    verifie("bureau", dw > 100 and dh > 100, "%d,%d %dx%d" % (dx, dy, dw, dh))

    w = app.window("WINTEST", dx + 20, dy + 20, dw // 2, dh // 2,
                   kind=gem.NAME | gem.CLOSER | gem.FULLER | gem.MOVER
                   | gem.SIZER | gem.INFO)
    w.on_draw = dessine
    w.info("zone d'info")
    w.open()
    r = w.rect
    verifie("ouverture", w.is_open and r[2] > 0, "%r" % (r,))
    x, y, ww, hh = w.work
    verifie("zone de travail dans la fenetre",
            x >= r[0] and y > r[1] and x + ww <= r[0] + r[2] and y + hh <= r[1] + r[3],
            "%r" % ((x, y, ww, hh),))

    # 1. Redraw requested from the AES, handled by the loop
    w.invalidate()
    app.run(timeout=1000)
    verifie("WM_REDRAW traite", dessins[0] >= 1, "%d appels" % dessins[0])
    cx, cy = x + ww // 2, y + hh // 2
    verifie("contenu : carre central", app.canvas.pixel(cx, cy) == 1)
    verifie("contenu : fond", app.canvas.pixel(x + ww // 4, y + hh * 3 // 4) == 0)

    # 2. Second window on top, then closed by message
    # Placed so that its WORK AREA, not its title bar, covers the central
    # square of the first one.
    w2 = app.window("seconde", cx - 30, cy - 40, dw // 3, dh // 3)
    w2.on_draw = lambda c, a, b, cw, ch: (c.color(fill=0),
                                          c.fill_rect(a, b, a + cw - 1, b + ch - 1))
    w2.open()
    app.run(timeout=500)
    a2, b2, c2, d2 = w2.work
    dedans = a2 <= cx < a2 + c2 and b2 <= cy < b2 + d2
    verifie("recouvrement : la seconde est dessus",
            dedans and app.canvas.pixel(cx, cy) == 0, "%r" % ((a2, b2, c2, d2),))
    gem._gem.appl_write(moi, (gem.WM_CLOSED, moi, 0, w2.handle, 0, 0, 0, 0))
    app.run(timeout=1000)
    verifie("WM_CLOSED ferme la seconde", not w2.is_open)
    verifie("la premiere est redessinee dessous", app.canvas.pixel(cx, cy) == 1)
    w2.delete()

    # 3. Move by message
    avant = dessins[0]
    nx, ny = r[0] + 30, r[1] + 20
    gem._gem.appl_write(moi, (gem.WM_MOVED, moi, 0, w.handle, nx, ny, r[2], r[3]))
    app.run(timeout=1000)
    r2 = w.rect
    # The AES may adjust the position (TOS 2.06 set y = 62 for 59 requested,
    # on the STE): we want it to have moved, not pixel-exact.
    verifie("WM_MOVED", abs(r2[0] - nx) <= 8 and abs(r2[1] - ny) <= 8,
            "%r pour (%d, %d)" % (r2, nx, ny))
    x, y, ww, hh = w.work
    verifie("contenu suit le deplacement",
            app.canvas.pixel(x + ww // 2, y + hh // 2) == 1)

    # 4. Full screen then back
    gem._gem.appl_write(moi, (gem.WM_FULLED, moi, 0, w.handle, 0, 0, 0, 0))
    app.run(timeout=1000)
    plein = w.rect
    verifie("WM_FULLED agrandit", plein[2] > r2[2] and plein[3] > r2[3], "%r" % (plein,))
    gem._gem.appl_write(moi, (gem.WM_FULLED, moi, 0, w.handle, 0, 0, 0, 0))
    app.run(timeout=1000)
    verifie("WM_FULLED revient", tuple(w.rect) == tuple(r2), "%r" % (w.rect,))

    # Reported under EmuTOS on the STE: after full screen then back, the
    # window could no longer be moved. Without a mouse under Hatari, we
    # check what depends on us: the window is still on top (otherwise the
    # AES does not pass it the drag), and a WM_MOVED is still applied.
    haut = gem._wind_get(0, gem.WF_TOP)[0]
    verifie("toujours au premier plan", haut == w.handle, "top = %d" % haut)
    r3 = w.rect
    gem._gem.appl_write(moi, (gem.WM_MOVED, moi, 0, w.handle, r3[0] + 16, r3[1] + 16, r3[2], r3[3]))
    app.run(timeout=1000)
    r4 = w.rect
    verifie("WM_MOVED apres plein ecran", abs(r4[0] - r3[0] - 16) <= 8 and abs(r4[1] - r3[1] - 16) <= 8,
            "%r" % (r4,))

    hatari("hatari-shortcut screenshot")

    # 5. Manual: key, then close via the close box
    for ligne in differe:
        print(ligne)
    differe = None
    print()
    print("--- a toi : deplace, agrandis, tape une touche, puis ferme la fenetre ---")
    touches = []
    # The instruction goes in the window's INFO LINE: with -q nothing is
    # shown on screen, and the instruction stayed invisible.
    w.info("deplace, agrandis, tape une touche, puis ferme cette fenetre")
    app.on_key = lambda k: (touches.append(k), w.info("touche 0x%04x -- ferme la fenetre" % k))
    if auto:
        # Wait for the key itself, not a duration: in fast-forward, Hatari
        # runs ~10 emulated s per real second on a STE, and the key arrived
        # after a 3 s wait.
        def touche_auto(k):
            touches.append(k)
            app.stop()
        app.on_key = touche_auto
        hatari("hatari-event keypress a", attendre=False)
        app.run(timeout=60000)
        gem._gem.appl_write(moi, (gem.WM_CLOSED, moi, 0, w.handle, 0, 0, 0, 0))
    # Every AES message of the manual phase goes to the log: if the
    # window refuses to move, we will know whether the AES sent WM_MOVED.
    app.trace = []
    app.run()                       # returns when the window is closed
    noms = {20: "REDRAW", 21: "TOPPED", 22: "CLOSED", 23: "FULLED",
            27: "SIZED", 28: "MOVED", 29: "NEWTOP", 50: "AP_TERM"}
    print("   messages AES recus a la main : %d" % len(app.trace))
    for m in app.trace:
        print("      %-8s fenetre %d  %d,%d %dx%d" % (
            noms.get(m[0], str(m[0])), m[3], m[4], m[5], m[6], m[7]))
    if auto or touches:
        verifie("touche recue par la boucle", len(touches) >= 1,
                "%s" % ["0x%04x" % k for k in touches])
    else:
        print("   (aucune touche tapee : cette verification est facultative)")
    verifie("run() rend la main a la fermeture", not w.is_open)

print()
print("BILAN : %d reussites, %d echecs" % (ok, ko))
print("FIN DU TEST")
