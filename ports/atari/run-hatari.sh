#!/bin/sh
# Lance MPY20.TTP sous Hatari, sortie console redirigee sur le terminal.
#
#   ./run-hatari.sh            # TT : 68030 + 8 Mo de TT-RAM (= le plus proche
#                              #      d'un STE + TwiSTer pour l'alt-RAM)
#   MACHINE=falcon ./run-hatari.sh
#   MACHINE=ste CPULEVEL=2 ./run-hatari.sh   # STE force en 68020
#   AUTO='C:\MPY20.TTP' ./run-hatari.sh        # demarre direct sur le REPL
#   FLOPPY=1 ./run-hatari.sh                 # reactive les lecteurs A: et B:
#   FPU=68882 ./run-hatari.sh                # machine avec FPU (binaire FPU=1)
#
# Par defaut la machine emulee n'a PAS de FPU (--fpu none), comme le STE a
# TwiSTer et comme un Falcon030 de base : c'est ce que suppose le binaire
# construit par un simple "make". Un vrai TT, lui, a toujours un 68882.
#
# Par defaut les deux lecteurs de disquette sont debranches (--drive-a/b off)
# et TOS est patche pour sauter une partie de son initialisation
# (--fast-boot) : sans disquette a chercher, le demarrage est nettement plus
# court. On ne s'en sert pas, le programme vient du disque GEMDOS.
#
# MEMSIZE (Mo de ST-RAM, 4 par defaut) compte : le binaire fait 2,6 Mo et le
# tas 1 Mo, donc sans RAM alternative il faut au moins 4 Mo, ou -m 256.
#
# CLAVIER
# -------
# Par defaut : EmuTOS en francais (AZERTY) + mapping "scancode", c'est a dire
# que Hatari transmet la POSITION physique de la touche, pas le caractere que
# macOS en tire. Le Mac est en AZERTY francais et l'Atari aussi : les touches
# tombent donc au meme endroit, y compris les chiffres (avec Shift) et les
# accents. C'est aussi ce qu'on aura sur le vrai STE.
# Les crochets et accolades restent la ou l'Atari les met, c'est a dire sur
# des combinaisons Alternate -- comme sur la vraie machine.
#
#   COUNTRY=us KEYMAP=symbolic ./run-hatari.sh   # TOS anglais, mapping par
#                                                # symbole (QWERTY)
#   KEYMAP=<fichier> ./run-hatari.sh             # table de correspondance
#                                                # perso, voir dans le paquet
#                                                # Hatari doc/keymap-sample.txt

HATARI=${HATARI:-$HOME/Downloads/hatari-2.6.1_macos/Hatari.app/Contents/MacOS/Hatari}
TOS=${TOS:-$HOME/Downloads/hatari-2.6.1_macos/Hatari.app/Contents/Resources/tos.img}
MACHINE=${MACHINE:-tt}
TTRAM=${TTRAM:-8}
COUNTRY=${COUNTRY:-fr}
FLOPPY=${FLOPPY:-0}
FPU=${FPU:-none}
KEYMAP=${KEYMAP:-scancode}
HERE=$(cd "$(dirname "$0")" && pwd)
DRIVE=${DRIVE:-$HERE/build/hd}

set -e
[ -x "$HATARI" ] || { echo "Hatari introuvable : $HATARI"; exit 1; }
[ -f "$HERE/build/MPY20.TTP" ] || { echo "build/MPY20.TTP absent, lance make"; exit 1; }

mkdir -p "$DRIVE"
cp "$HERE/build/MPY20.TTP" "$DRIVE/"
# Les scripts de test partent sur le disque GEMDOS avec le binaire.
[ -d "$HERE/tests" ] && cp "$HERE"/tests/*.PY "$DRIVE/" 2>/dev/null

# Le type de mapping clavier ne s'expose pas en ligne de commande, seulement
# dans un fichier de configuration -- que l'on genere.
#   0 = symbolique (par caractere)   1 = scancode (par position)   2 = charge
CFG=$HERE/build/keyboard.cfg
case "$KEYMAP" in
    symbolic) KEYTYPE=0; KEYFILE= ;;
    scancode) KEYTYPE=1; KEYFILE= ;;
    *)        KEYTYPE=2; KEYFILE=$KEYMAP ;;
esac
cat > "$CFG" <<CFGEOF
[Keyboard]
bDisableKeyRepeat = FALSE
nKeymapType = $KEYTYPE
szMappingFileName = $KEYFILE
CFGEOF

# La disposition du clavier vient de la ROM (pas de NVRAM) sur ST/STE, et de
# la NVRAM sur TT et Falcon.
case "$MACHINE" in
    tt|falcon) LOCALE="--layout $COUNTRY --language $COUNTRY" ;;
    *)         LOCALE="--country $COUNTRY" ;;
esac

if [ "$FLOPPY" = "1" ]; then
    FLOPPY_OPTS=""
    echo "Lecteurs de disquette actifs"
else
    FLOPPY_OPTS="--drive-a off --drive-b off --fast-boot on"
fi

echo "Machine $MACHINE, FPU $FPU, clavier $COUNTRY ($KEYMAP), disque GEMDOS $DRIVE"

exec "$HATARI" \
    -c "$CFG" \
    --tos "$TOS" \
    --machine "$MACHINE" \
    ${CPULEVEL:+--cpulevel $CPULEVEL} \
    --ttram "$TTRAM" \
    --fpu "$FPU" \
    --memsize "${MEMSIZE:-4}" \
    $LOCALE \
    ${AUTO:+--auto "$AUTO"} \
    $FLOPPY_OPTS \
    --harddrive "$DRIVE" \
    --conout 2 \
    --sound off \
    "$@"
