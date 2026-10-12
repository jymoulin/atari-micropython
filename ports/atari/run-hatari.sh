#!/bin/sh
# Runs MPY20.TTP under Hatari, console output redirected to the terminal.
#
#   ./run-hatari.sh            # TT: 68030 + 8 MB of TT-RAM (= the closest
#                              #     to a STE + TwiSTer for alt-RAM)
#   MACHINE=falcon ./run-hatari.sh
#   MACHINE=ste CPULEVEL=2 ./run-hatari.sh   # STE forced to 68020
#   AUTO='C:\MPY20.TTP' ./run-hatari.sh        # boots straight into the REPL
#   FLOPPY=1 ./run-hatari.sh                 # re-enables drives A: and B:
#   FPU=68882 ./run-hatari.sh                # machine with FPU (FPU=1 binary)
#
# By default the emulated machine has NO FPU (--fpu none), like the STE with
# TwiSTer and like a stock Falcon030: that is what the binary built by a
# plain "make" assumes. A real TT, on the other hand, always has a 68882.
#
# By default both floppy drives are unplugged (--drive-a/b off) and TOS is
# patched to skip part of its initialisation (--fast-boot): with no floppy
# to look for, startup is noticeably shorter. We do not use them, the
# program comes from the GEMDOS drive.
#
# MEMSIZE (MB of ST-RAM, 4 by default) matters: the binary is 2.6 MB and the
# heap 1 MB, so without alternate RAM at least 4 MB is needed, or -m 256.
#
# KEYBOARD
# --------
# Default: EmuTOS in French (AZERTY) + "scancode" mapping, that is Hatari
# sends the physical POSITION of the key, not the character macOS derives
# from it. The Mac is French AZERTY and so is the Atari: keys therefore land
# in the same place, including digits (with Shift) and accented letters.
# It is also what we will get on the real STE.
# Brackets and braces stay where the Atari puts them, that is on
# Alternate combinations -- as on the real machine.
#
#   COUNTRY=us KEYMAP=symbolic ./run-hatari.sh   # English TOS, mapping by
#                                                # symbol (QWERTY)
#   KEYMAP=<file> ./run-hatari.sh                # custom mapping table,
#                                                # see in the Hatari package
#                                                # doc/keymap-sample.txt

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
# Test scripts go onto the GEMDOS drive along with the binary.
[ -d "$HERE/tests" ] && cp "$HERE"/tests/*.PY "$DRIVE/" 2>/dev/null

# The keyboard mapping type is not exposed on the command line, only
# in a configuration file -- which we generate.
#   0 = symbolic (by character)   1 = scancode (by position)   2 = loaded
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

# The keyboard layout comes from the ROM (no NVRAM) on ST/STE, and from
# the NVRAM on TT and Falcon.
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
