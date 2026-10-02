# Phase 5 : memoire, ecran, clavier, son, port serie.
#
#   MPY.TTP HWTEST.PY          tout sauf le son et la serie
#   MPY.TTP HWTEST.PY sound    ajoute un bip (ca s'entend)
#   MPY.TTP HWTEST.PY serial   ajoute un essai du port RS-232

import machine
import sys
import time

mode = sys.argv[1] if len(sys.argv) > 1 else ""

print("--- lecture de la memoire ---")
# 0x4BA est le compteur a 200 Hz, sous 0x800 donc en espace protege :
# machine.mem32 passe en superviseur tout seul.
a = machine.mem32[0x4BA]
time.sleep_ms(100)
b = machine.mem32[0x4BA]
print("   _hz_200 : %d puis %d, soit %d tics en 100 ms" % (a, b, b - a))
print("   (attendu : environ 20)")

print("   _sysbase (0x4F2) : 0x%08x" % machine.mem32[0x4F2])
print("   version TOS      : 0x%04x" % machine.mem16[machine.mem32[0x4F2] + 2])

print()
print("--- l'ecran ---")
w, h, plans, taille = machine.screen_info()
print("   resolution %d : %dx%d, %d plan(s), %d octets" %
      (machine.Getrez(), w, h, plans, taille))
print("   Physbase 0x%08x, Logbase 0x%08x" %
      (machine.Physbase(), machine.Logbase()))

if plans == 1:
    print()
    print("--- dessin direct, sans VDI ---")
    import framebuf
    adresse = machine.Physbase()
    ecran = machine.mem_buffer(adresse, taille)
    fb = framebuf.FrameBuffer(ecran, w, h, framebuf.MONO_HMSB)

    # On sauvegarde l'ecran AVANT de le salir, pour rendre le bureau intact.
    t0 = time.ticks_ms()
    sauve = bytearray(taille)
    machine.memcpy(sauve, ecran, taille)
    print("   sauvegarde ecran (memcpy) : %d ms" % (time.ticks_ms() - t0))

    t0 = time.ticks_ms()
    machine.memset(ecran, 0, taille)
    t_memset = time.ticks_ms() - t0

    t0 = time.ticks_ms()
    fb.fill(0)
    t_fill = time.ticks_ms() - t0
    print("   effacement : memset %d ms, framebuf.fill %d ms" % (t_memset, t_fill))
    print("   (framebuf pose les pixels un par un en monochrome ; pour")
    print("    effacer ou recopier en bloc, memset et memcpy sont la)")

    t0 = time.ticks_ms()
    for i in range(0, w, 16):
        fb.line(i, 0, w - 1 - i, h - 1, 1)
    fb.rect(40, 40, w - 80, h - 80, 1)
    fb.text("MicroPython sur Atari", 60, 60, 1)
    fb.text("framebuf ecrit directement dans", 60, 76, 1)
    fb.text("la memoire ecran, sans VDI", 60, 92, 1)
    print("   dessin     : %d ms" % (time.ticks_ms() - t0))

    machine.Vsync()
    if "auto" in sys.argv[1:]:
        # Sous Hatari (ALLTEST.PY auto) : personne pour appuyer.
        print("   (auto : pas d'attente)")
        time.sleep_ms(500)
    else:
        print("   regarde l'ecran ! (une touche pour continuer)")
        sys.stdin.read(1)

    # Et on rend l'ecran tel qu'on l'a trouve.
    machine.memcpy(ecran, sauve, taille)
else:
    print("   (en couleur les plans sont entrelaces : framebuf ne sait pas")
    print("    dessiner la-dedans, il faudra le VDI. En monochrome, si.)")

print()
print("--- clavier ---")
k = machine.Kbshift()
print("   Kbshift = 0x%02x" % k)
noms = ["Shift droit", "Shift gauche", "Control", "Alternate", "Caps lock"]
actives = [noms[i] for i in range(5) if k & (1 << i)]
print("   touches tenues :", actives if actives else "aucune")

print()
print("--- divers ---")
print("   Random() :", machine.Random())
print("   couleur 0 :", hex(machine.Setcolor(0, -1)))

if mode == "sound":
    print()
    print("--- son ---")
    # Une macro Dosound : canal A, periode, volume, puis extinction.
    bip = bytes([
        0x00, 0x80, 0x01, 0x00,     # periode du canal A
        0x07, 0xFE,                 # mixeur : canal A en tonalite
        0x08, 0x0F,                 # volume A au maximum
        0x80, 0x00,                 # temporisation
        0x81, 0x08, 0x02,           # decroissance du volume
        0xFF, 0x00,                 # fin
    ])
    machine.Dosound(bip)
    print("   bip envoye au YM2149")
    time.sleep_ms(500)
    print("   registre 7 du PSG (mixeur) : 0x%02x" % machine.Giaccess(7))

if mode == "serial":
    print()
    print("--- port serie ---")
    u = machine.UART(machine.UART.AUX if hasattr(machine.UART, "AUX") else 1,
                     baudrate=9600, bits=8, parity=None, stop=1, timeout=500)
    print("  ", u)
    u.write(b"MicroPython sur Atari\r\n")
    print("   ecrit. Lecture pendant 500 ms...")
    d = u.read(64)
    print("   recu :", d if d else "rien (normal sans correspondant)")

print()
print("FIN DU TEST")
