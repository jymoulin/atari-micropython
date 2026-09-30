# Le module d'horloge de la TwiSTer, de plus pres.
#
# Le scan a rendu 0x68 ET 0x57 : ce n'est pas un DS1307 mais un module
# DS3231 (le manuel de la carte le confirme), qui embarque en plus une
# EEPROM AT24C32 de 4 Ko. Le DS3231 a aussi un capteur de temperature.
#
#   MPY.TTP RTCTEST.PY          lecture seule
#   MPY.TTP RTCTEST.PY write    teste aussi l'ecriture EEPROM (reversible)
#   MPY.TTP RTCTEST.PY sync     recale l'horloge GEMDOS sur le DS3231

import machine
import sys
import time

RTC = 0x68
EEPROM = 0x57
INCONNU = 0x2C

i2c = machine.I2C(freq=50000)
mode = sys.argv[1] if len(sys.argv) > 1 else ""


def bcd(v):
    return (v & 0x0F) + ((v >> 4) * 10)


print("--- DS3231 : temperature ---")
# Registres 0x11 (partie entiere, signee) et 0x12 (bits 7-6 = quarts de degre)
t = i2c.readfrom_mem(RTC, 0x11, 2)
entier = t[0] if t[0] < 128 else t[0] - 256
frac = (t[1] >> 6) * 25
print("   %d.%02d degres C" % (entier, frac))
print("   (c'est le capteur du DS3231, qui s'en sert pour compenser son")
print("    quartz -- d'ou sa precision. Il mesure l'interieur du boitier.)")

print()
print("--- DS3231 : etat ---")
st = i2c.readfrom_mem(RTC, 0x0F, 1)[0]
print("   registre 0x0F = 0x%02x" % st)
print("   oscillateur arrete depuis la derniere mise a l'heure :",
      "OUI" if st & 0x80 else "non")
print("   32 kHz en sortie :", "oui" if st & 0x08 else "non")

ctrl = i2c.readfrom_mem(RTC, 0x0E, 1)[0]
print("   registre 0x0E = 0x%02x, oscillateur %s" %
      (ctrl, "arrete sur batterie" if ctrl & 0x80 else "actif"))

print()
print("--- EEPROM AT24C32 a 0x57 ---")
try:
    d = i2c.readfrom_mem(EEPROM, 0, 16, addrsize=16)
    print("   16 premiers octets :", " ".join("%02x" % b for b in d))
    vierge = all(b == 0xFF for b in d)
    print("   vierge :", "oui" if vierge else "non")
except Exception as e:
    print("   lecture impossible :", e)

if mode == "write":
    # Cette EEPROM-la est celle du module d'horloge, et TOSWIT_4 n'y touche
    # pas (il ecrit ailleurs, voir plus bas). On verifie quand meme que la
    # zone visee est vierge avant d'y ecrire quoi que ce soit.
    print("   verification que 0x100 est vierge...")
    try:
        avant = i2c.readfrom_mem(EEPROM, 0x100, 12, addrsize=16)
        if not all(b == 0xFF for b in avant):
            print("   NON VIERGE :", " ".join("%02x" % b for b in avant))
            print("   ecriture ANNULEE -- quelque chose occupe deja la place.")
        else:
            i2c.writeto_mem(EEPROM, 0x100, b"MicroPython!", addrsize=16)
            time.sleep_ms(20)      # l'AT24C32 demande ~10 ms par page
            back = i2c.readfrom_mem(EEPROM, 0x100, 12, addrsize=16)
            print("   relu :", bytes(back))
            print("   ", "ok" if bytes(back) == b"MicroPython!" else "NON, ecart")
    except Exception as e:
        print("   ecriture impossible :", e)
else:
    print("   (relancer avec l'argument  write  pour tester l'ecriture)")

print()
print("--- 0x2C : NE PAS ECRIRE ---")
# Le desassemblage de TOSWIT_4.TOS montre qu'il parle a DEUX composants sur
# ces memes lignes : 0xD0/0xD1 (le DS3231) et 0x58/0x59, soit 0x2C. Et le
# protocole de 0x2C n'est pas celui d'une EEPROM : l'octet qui suit l'adresse
# est un octet de CONTROLE, construit par decalages et masques, et une
# lecture rend 16 bits. C'est de la configuration de la carte -- tres
# probablement la banque de ROM de demarrage, que TOSWIT sert justement a
# changer.
#
# On se contente donc d'une lecture nue, sans octet de controle : rien n'est
# selectionne, rien n'est ecrit.
try:
    d = i2c.readfrom(INCONNU, 2)
    print("   lecture nue, 2 octets :", " ".join("%02x" % b for b in d))
except Exception as e:
    print("   pas de reponse :", e)
print("   Aucune ecriture ici : ce composant decide vraisemblablement de")
print("   la banque de TOS au demarrage.")

print()
print("--- derive de l'horloge systeme ---")
d = i2c.readfrom_mem(RTC, 0, 7)
rtc_s = bcd(d[0] & 0x7F)
rtc_m = bcd(d[1])
rtc_h = bcd(d[2] & 0x3F)
g = time.localtime()
rtc_total = rtc_h * 3600 + rtc_m * 60 + rtc_s
gem_total = g[3] * 3600 + g[4] * 60 + g[5]
print("   DS3231 : %02d:%02d:%02d" % (rtc_h, rtc_m, rtc_s))
print("   GEMDOS : %02d:%02d:%02d" % (g[3], g[4], g[5]))
print("   ecart  : %d secondes" % (gem_total - rtc_total))
print("   Le DS3231 fait foi. L'horloge GEMDOS est entretenue par")
print("   l'interruption a 200 Hz, et tout ce qui masque les interruptions")
print("   -- disque, pilotes, Geneva -- lui fait perdre des tics.")

if mode == "sync":
    import uos
    print()
    jour, mois, annee = bcd(d[4]), bcd(d[5] & 0x1F), bcd(d[6]) + 2000
    print("   recalage de GEMDOS sur le DS3231 :")
    print("   %02d/%02d/%04d %02d:%02d:%02d" %
          (jour, mois, annee, rtc_h, rtc_m, rtc_s))
    uos.settime(annee, mois, jour, rtc_h, rtc_m, rtc_s)
    g = time.localtime()
    print("   GEMDOS dit maintenant : %02d:%02d:%02d" % (g[3], g[4], g[5]))
    print("   (GEMDOS ne garde les secondes que par pas de deux : un bit")
    print("    manque dans son format, ce n'est pas une imprecision de")
    print("    notre part.)")

print()
print("FIN DU TEST")
