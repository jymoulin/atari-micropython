#!/bin/sh
# Runs a test script under Hatari, unattended, and prints its log.
# Hatari stops by itself after VBLS frames.
#
#   ./hatari-test.sh SMOKE.PY
#   ./hatari-test.sh OSTEST.PY force
#   MACHINE=ste CPULEVEL=2 ./hatari-test.sh SNDTEST.PY
#   MONITOR=mono ./hatari-test.sh GEMTEST.PY auto     # 640 x 400
#   EXTRA=/tmp/DIAG.PY ./hatari-test.sh DIAG.PY          # script outside tests/
#   AUTOPRG='C:\\MPYRUN.PRG' HCMDS='14:hatari-event keypress 28' ./hatari-test.sh X
#       AUTOPRG : start something other than RUNTEST.PRG
#       HCMDS   : keys sent from the Mac at given times
#   ACSI=disque.img ./hatari-test.sh DISKTEST.PY C
#   BIN=/tmp/autre/MPY.TTP ./hatari-test.sh BENCH.PY   # another binary
#   STING=1 ./hatari-test.sh NETTEST.PY    # with STinG (reference KIT,
#       ~/Downloads/ToTest/sting/KIT, or STING=path of the KIT): AUTO\STING.PRG
#       and C:\STING; without a network driver, only the 127.0.0.1 loopback works
#
# Commands to Hatari from the test: the script writes a Hatari control
# command to C:\HATARI.CMD (e.g. "hatari-event keypress 28" for Return,
# "hatari-shortcut screenshot"). A loop, here, sends it through --cmd-fifo
# then deletes the file; the test waits for that deletion before going on.
# Screenshots go to /tmp/mpy-shots.
#
# ACSI: mounts a disk image as ACSI 0. EmuTOS (the tos.img shipped with
# Hatari) reads it with its own drivers and gives it C:; the emulated GEMDOS
# drive then moves to D:. It is the only way to get a real BIOS device
# under the emulator, hence to exercise Getbpb/Rwabs.
#
# Mechanics: --auto passes no arguments, so RUNTEST.PRG (tools/runtest.c)
# reads the command line from RUNTEST.ARG and starts MPY.TTP with
# "-l RESULT.TXT". The log lands directly in the Mac folder that serves as
# the GEMDOS drive: we read it back here. MPY.TTP exit code in
# RUNTEST.RET.
#
# Prerequisites: make, then make -f Makefile.tools (for RUNTEST.PRG).

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
AUTO=${AUTOPRG:-$AUTO}          # AUTOPRG: start something else (MPYRUN.PRG)
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=${BIN:-$HERE/build/MPY20.TTP}
# All temporary files in /tmp, outside ~/Documents (and iCloud).
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
# EXTRA: additional files (diagnostic scripts written in /tmp, for example)
for f in $EXTRA; do cp "$f" "$D/"; done
if [ -n "$STING" ]; then
    KIT=$STING; [ "$KIT" = 1 ] && KIT=$HOME/Downloads/ToTest/sting/KIT
    mkdir -p "$D/AUTO" "$D/STING"
    cp "$KIT/AUTO/STING.PRG" "$KIT/AUTO/STING.INF" "$D/AUTO/"
    for f in "$KIT"/STING/*; do
        case "$f" in */USB_NET.STX|*/CLI) ;; *) cp "$f" "$D/STING/" ;; esac
    done
fi
printf -- '%s-l RESULT.TXT %s\r\n' "${QUIET:+-q }" "$*" > "$D/RUNTEST.ARG"

rm -f "$FIFO"; mkdir -p "$SHOTS"

# Relay HATARI.CMD -> Hatari command fifo, as long as Hatari runs.
# One "cat" per command: Hatari reads its fifo non-blocking, and a
# writer kept open permanently floods it with EAGAIN.
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

# HCMDS: commands sent from the Mac at given times, in the form
# "delay:command;delay:command" (delays in REAL seconds, cumulative).
# To drive what runs before any Python script -- the MPYRUN launcher,
# for example -- when HATARI.CMD cannot be used.
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

# --addr24 off: otherwise Hatari enables it itself for TT-RAM and opens a
# dialog box that blocks everything. perl/alarm: macOS has no timeout(1).
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
