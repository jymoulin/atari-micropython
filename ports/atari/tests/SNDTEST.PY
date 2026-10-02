# Son DMA du STE / TT : machine.stram, dmaplay, dmapos, dmastop.
#
#   MPY.TTP SNDTEST.PY
#
# Joue un la (440 Hz) d'une seconde, puis le meme en stereo et en boucle.
# Sur la vraie machine, on doit l'ENTENDRE : c'est le vrai test. La duree
# mesuree dit si la puce lit bien a la cadence annoncee.

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
    # t0 : pris juste apres dmaplay. Les affichages faits entre-temps
    # prennent du temps (une centaine de ms sous ALLTEST.PY, en ST-RAM a
    # 8 MHz) : mesurer depuis ici raccourcissait la duree d'autant.
    while machine.dmapos() is not None:
        if time.ticks_diff(time.ticks_ms(), t0) > limite_ms:
            machine.dmastop()
            return -1
    return time.ticks_diff(time.ticks_ms(), t0)


print("--- garde-fou : un tampon hors ST-RAM doit etre refuse ---")
# Le tas est en RAM alternative (sauf -s) : un bytearray ordinaire n'est pas
# lisible par la DMA, et le port doit le dire au lieu de jouer du bruit.
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

# Onde carree : 8 bits signes, donc -60..+60 en complement a deux.
# On construit UNE periode, puis on la repete : une boucle Python par
# echantillon prendrait des dizaines de secondes sur un STE a 8 MHz.
# L'affectation par tranche garde la longueur exacte : buf est un tampon
# en ST-RAM pris par reference, il ne doit surtout pas etre redimensionne.
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
