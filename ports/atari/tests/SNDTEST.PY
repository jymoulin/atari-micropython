# STE / TT DMA sound: machine.stram, dmaplay, dmapos, dmastop.
#
#   MPY.TTP SNDTEST.PY
#
# Plays a one-second A (440 Hz), then the same in stereo and looped.
# On the real machine, you must HEAR it: that is the real test. The
# measured duration tells whether the chip reads at the announced rate.

import machine
import time

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


def attend_fin(t0, limite_ms=5000):
    # t0: taken right after dmaplay. The output printed in between takes
    # time (about a hundred ms under ALLTEST.PY, in ST-RAM at 8 MHz):
    # measuring from here shortened the duration by as much.
    while machine.dmapos() is not None:
        if time.ticks_diff(time.ticks_ms(), t0) > limite_ms:
            machine.dmastop()
            return -1
    return time.ticks_diff(time.ticks_ms(), t0)


print("--- garde-fou : un tampon hors ST-RAM doit etre refuse ---")
# The heap is in alternate RAM (except with -s): an ordinary bytearray is not
# readable by the DMA, and the port must say so instead of playing noise.
try:
    machine.dmaplay(bytearray(1000))
    machine.dmastop()
    print("   (joue sans erreur : tas en ST-RAM, lance avec -s ou sans alt-RAM)")
except ValueError as e:
    verifie("refus hors ST-RAM", "ST-RAM" in str(e), str(e))

print()
print("--- un la d'une seconde, mono, 25 kHz ---")
TAUX = 25033
buf = machine.stram(TAUX)
verifie("stram", len(buf) == TAUX + (TAUX & 1), "%d octets" % len(buf))

# Square wave: signed 8 bits, so -60..+60 in two's complement.
# Build ONE period, then repeat it: a Python loop per sample would take
# tens of seconds on an 8 MHz STE.
# Slice assignment keeps the exact length: buf is a ST-RAM buffer held
# by reference, it must never be resized.
periode = TAUX // 440
une = bytes((60 if i < periode // 2 else -60) & 0xFF for i in range(periode))
t0 = time.ticks_ms()
buf[:] = (une * (len(buf) // periode + 1))[:len(buf)]
print("   remplissage : %d ms" % time.ticks_diff(time.ticks_ms(), t0))

reel = machine.dmaplay(buf, rate=25000)
t0 = time.ticks_ms()
verifie("taux arrondi", reel == 25033, "%d Hz" % reel)
pos = machine.dmapos()
verifie("dmapos pendant la lecture", pos is not None, repr(pos))
duree = attend_fin(t0)
print("   duree mesuree : %d ms (attendu ~1000)" % duree)
verifie("duree", 850 <= duree <= 1250)
verifie("dmapos apres la fin", machine.dmapos() is None)

print()
print("--- arrondi des taux ---")
for demande, attendu in ((8000, 6258), (12000, 12517), (44100, 50066)):
    reel = machine.dmaplay(buf, rate=demande)
    machine.dmastop()
    verifie("rate=%d" % demande, reel == attendu, "-> %d" % reel)

print()
print("--- stereo : la meme onde deux fois plus courte ---")
machine.dmaplay(buf, rate=25033, stereo=True)
duree = attend_fin(time.ticks_ms())
print("   duree mesuree : %d ms (attendu ~500)" % duree)
verifie("duree stereo", 400 <= duree <= 650)

print()
print("--- boucle, puis arret ---")
machine.dmaplay(buf, rate=25033, loop=True)
time.sleep_ms(1500)
verifie("boucle toujours active apres 1,5 s", machine.dmapos() is not None)
machine.dmastop()
verifie("dmastop", machine.dmapos() is None)

print()
print("BILAN : %d reussites, %d echecs" % (ok, ko))
print("FIN DU TEST")
