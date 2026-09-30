# Test du bus I2C de la TwiSTer (phase 4bis).
#
# Ce qu'on cherche a savoir, dans l'ordre :
#   1. les registres sont-ils bien ceux de la MonSTer (0xFFFFFE02/04/06) ?
#   2. SCL est-il relisable, donc le clock stretching possible ?
#   3. le DS1307 de l'horloge repond-il a l'adresse 0x68 ?
#
# Un scan() qui rend [0x68] repond aux trois d'un coup.
#
# Ce test ne fait que LIRE. Il ne touche pas a l'heure du RTC.

import machine
import time

print("--- ouverture du bus ---")
i2c = machine.I2C(freq=50000)
print("  ", i2c)
print("   SCL relisable :", machine.i2c_scl_readable(i2c))
if not machine.i2c_scl_readable(i2c):
    print("   (l'horloge est pilotee en sortie, pas de clock stretching --")
    print("    sans consequence pour un DS1307, qui n'en demande pas)")

print()
print("--- scan du bus ---")
t0 = time.ticks_ms()
trouve = i2c.scan()
print("   %d adresse(s) en %d ms : %s" % (
    len(trouve), time.ticks_ms() - t0, [hex(a) for a in trouve]))

if not trouve:
    print()
    print("   Personne ne repond. Soit les adresses des registres ne sont")
    print("   pas celles de la MonSTer, soit la polarite du registre de")
    print("   direction est inverse. Rien d'autre a en conclure pour")
    print("   l'instant.")
else:
    for a in trouve:
        nom = {0x68: "DS1307 / DS3231 (horloge)",
               0x50: "EEPROM 24Cxx",
               0x57: "EEPROM du DS3231",
               0x3C: "afficheur OLED SSD1306",
               0x27: "expandeur PCF8574",
               0x48: "ADC/temperature"}.get(a, "inconnu")
        print("   0x%02x  %s" % (a, nom))

print()
print("--- lecture de l'horloge ---")
if 0x68 in trouve:
    def bcd(v):
        return (v & 0x0F) + ((v >> 4) * 10)

    d = i2c.readfrom_mem(0x68, 0, 7)
    sec, minu, heure = bcd(d[0] & 0x7F), bcd(d[1]), bcd(d[2] & 0x3F)
    jour, mois, annee = bcd(d[4]), bcd(d[5] & 0x1F), bcd(d[6])
    print("   registres bruts :", [hex(b) for b in d])
    print("   RTC  : %02d/%02d/20%02d  %02d:%02d:%02d" %
          (jour, mois, annee, heure, minu, sec))
    t = time.localtime()
    print("   GEMDOS: %02d/%02d/%04d  %02d:%02d:%02d" %
          (t[2], t[1], t[0], t[3], t[4], t[5]))
    print()
    print("   Les deux doivent concorder : le driver RTC du dossier AUTO")
    print("   a lu cette meme puce au demarrage pour regler GEMDOS.")
else:
    print("   pas de DS1307 a 0x68, lecture sautee")

print()
print("FIN DU TEST")
