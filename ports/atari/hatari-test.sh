#!/bin/sh
# Execute un script de test sous Hatari, sans intervention, et affiche son
# journal. Hatari s'arrete seul apres VBLS images.
#
#   ./hatari-test.sh SMOKE.PY
#   ./hatari-test.sh OSTEST.PY force
#   MACHINE=ste CPULEVEL=2 ./hatari-test.sh SNDTEST.PY
#   MONITOR=mono ./hatari-test.sh GEMTEST.PY auto     # 640 x 400
#   EXTRA=/tmp/DIAG.PY ./hatari-test.sh DIAG.PY          # script hors tests/
#   AUTOPRG='C:\\MPYRUN.PRG' HCMDS='14:hatari-event keypress 28' ./hatari-test.sh X
#       AUTOPRG : demarrer autre chose que RUNTEST.PRG
#       HCMDS   : touches envoyees depuis le Mac a des instants donnes
#   ACSI=disque.img ./hatari-test.sh DISKTEST.PY C
#   BIN=/tmp/autre/MPY.TTP ./hatari-test.sh BENCH.PY   # autre binaire
#
# Commandes vers Hatari depuis le test : le script ecrit une commande de
# controle Hatari dans C:\HATARI.CMD (par ex. "hatari-event keypress 28"
# pour Return, "hatari-shortcut screenshot"). Une boucle, ici, l'envoie par
# --cmd-fifo puis efface le fichier ; le test attend cet effacement avant de
# continuer. Captures d'ecran dans /tmp/mpy-shots.
#
# ACSI : monte une image de disque en ACSI 0. EmuTOS (le tos.img fourni avec
# Hatari) la lit avec ses propres pilotes et lui donne C: ; le disque GEMDOS
# emule passe alors en D:. C'est le seul moyen d'avoir un vrai peripherique
# BIOS sous l'emulateur, donc d'eprouver Getbpb/Rwabs.
#
# Mecanique : --auto ne passe pas d'arguments, donc RUNTEST.PRG (tools/
# runtest.c) lit la ligne de commande dans RUNTEST.ARG et lance MPY.TTP avec
# "-l RESULT.TXT". Le journal atterrit directement dans le dossier du Mac qui
# sert de disque GEMDOS : on le relit ici. Code de retour de MPY.TTP dans
# RUNTEST.RET.
#
# Prerequis : make, puis make -f Makefile.tools (pour RUNTEST.PRG).

HATARI=${HATARI:-$HOME/Downloads/hatari-2.6.1_macos/Hatari.app/Contents/MacOS/Hatari}
TOS=${TOS:-$HOME/Downloads/hatari-2.6.1_macos/Hatari.app/Contents/Resources/tos.img}
MACHINE=${MACHINE:-tt}
TTRAM=${TTRAM:-8}
VBLS=${VBLS:-3000}
if [ -n "$ACSI" ]; then
    DISK_OPTS="--acsi 0=$ACSI --gemdos-drive D"
    AUTO='D:\RUNTEST.PRG'
else
    DISK_OPTS=""
    AUTO='C:\RUNTEST.PRG'
fi
AUTO=${AUTOPRG:-$AUTO}          # AUTOPRG : demarrer autre chose (MPYRUN.PRG)
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=${BIN:-$HERE/build/MPY20.TTP}
# Tout le temporaire dans /tmp, hors de ~/Documents (et d'iCloud).
D=/tmp/mpy-hdtest
OUT=/tmp/mpy-hatari.out
FIFO=/tmp/mpy-hatari.fifo
SHOTS=/tmp/mpy-shots

[ $# -ge 1 ] || { echo "usage: $0 SCRIPT.PY [arguments]"; exit 2; }
[ -f "$BIN" ] || { echo "$BIN absent : make"; exit 1; }
[ -f "$HERE/build/RUNTEST.PRG" ] || { echo "RUNTEST.PRG absent : make -f Makefile.tools"; exit 1; }

rm -rf "$D" && mkdir -p "$D"
cp "$BIN" "$D/MPY.TTP"
cp "$HERE/build/RUNTEST.PRG" "$D/"
[ -f "$HERE/build/MPYRUN.PRG" ] && cp "$HERE/build/MPYRUN.PRG" "$D/"
cp "$HERE"/tests/*.PY "$D/"
# EXTRA : fichiers en plus (scripts de diagnostic ecrits dans /tmp, par ex.)
for f in $EXTRA; do cp "$f" "$D/"; done
printf -- '%s-l RESULT.TXT %s\r\n' "${QUIET:+-q }" "$*" > "$D/RUNTEST.ARG"

rm -f "$FIFO"; mkdir -p "$SHOTS"

# Relais HATARI.CMD -> fifo de commande de Hatari, tant que Hatari tourne.
# Un "cat" par commande : Hatari lit son fifo en non bloquant, et un
# ecrivain garde ouvert en permanence le noie sous les EAGAIN.
(
    while [ ! -p "$FIFO" ]; do sleep 0.2; done
    while [ -p "$FIFO" ]; do
        if [ -f "$D/HATARI.CMD" ]; then
            sleep 0.5
            tr -d '\r' < "$D/HATARI.CMD" > "$FIFO"
            rm -f "$D/HATARI.CMD"
        fi
        sleep 0.2
    done
) 2>/dev/null &
RELAY=$!

# HCMDS : commandes envoyees depuis le Mac a des instants donnes, sous la
# forme "delai:commande;delai:commande" (delais en secondes REELLES, cumules).
# Pour piloter ce qui tourne avant tout script Python -- le lanceur MPYRUN,
# par exemple -- quand HATARI.CMD n'est pas utilisable.
SENDER=
if [ -n "$HCMDS" ]; then
    (
        while [ ! -p "$FIFO" ]; do sleep 0.2; done
        IFS=';'
        for entry in $HCMDS; do
            unset IFS
            sleep "${entry%%:*}"
            printf '%s\n' "${entry#*:}" > "$FIFO"
            IFS=';'
        done
    ) 2>/dev/null &
    SENDER=$!
fi

# --addr24 off : sinon Hatari l'active lui-meme pour la TT-RAM et ouvre une
# boite de dialogue qui bloque tout. perl/alarm : macOS n'a pas timeout(1).
perl -e 'alarm shift; exec @ARGV' 300 "$HATARI" \
    --tos "$TOS" --machine "$MACHINE" ${CPULEVEL:+--cpulevel $CPULEVEL} ${MONITOR:+--monitor $MONITOR} \
    --addr24 off --ttram "$TTRAM" --fpu none --memsize "${MEMSIZE:-4}" \
    --drive-a off --drive-b off --fast-boot on \
    --harddrive "$D" $DISK_OPTS --auto "$AUTO" \
    --run-vbls "$VBLS" --fast-forward on --sound off --confirm-quit off \
    --cmd-fifo "$FIFO" --screenshot-dir "$SHOTS" --screenshot-format png \
    > "$OUT" 2>&1
status=$?
kill $RELAY $SENDER 2>/dev/null; wait $RELAY 2>/dev/null; rm -f "$FIFO"
[ $status -eq 142 ] && echo "*** Hatari tue au bout de 300 s (bloque ?)"

if [ -f "$D/RESULT.TXT" ]; then
    cat "$D/RESULT.TXT"
else
    echo "*** pas de RESULT.TXT -- voir $OUT"
fi
printf "code de retour de MPY.TTP : "
cat "$D/RUNTEST.RET" 2>/dev/null || echo "(inconnu : RUNTEST n'a pas repris la main)"
