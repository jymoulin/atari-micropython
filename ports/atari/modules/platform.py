# platform module for Atari TOS.
#
# Everything here is read from the cookie jar, which is how an Atari says what
# it is: which machine, which processor, which video shifter, and which of
# EmuTOS / MagiC / FreeMiNT / Geneva / NVDI happens to be loaded.

import sys
import uos

_MACHINE = {0: "ST", 1: "STE", 2: "TT", 3: "Falcon", 4: "Milan", 5: "ARAnyM"}
_VIDEO = {0: "ST shifter", 1: "STE shifter", 2: "TT shifter", 3: "Falcon Videl"}
_FPU = {0: "none", 1: "SFP004", 2: "68881/68882", 4: "68881", 6: "68882",
        8: "68040 internal", 16: "68060 internal"}


def _cookie(name, default=None):
    v = uos.cookie(name)
    return default if v is None else v


def machine():
    """The Atari model: 'ST', 'STE', 'Mega STE', 'TT', 'Falcon'."""
    mch = _cookie("_MCH")
    if mch is None:
        return "ST"
    major = mch >> 16
    if major == 1 and (mch & 0xFFFF):
        return "Mega STE"
    return _MACHINE.get(major, "Atari")


def processor():
    """'68000', '68020', ... from the _CPU cookie."""
    return "680%02d" % _cookie("_CPU", 0)


def fpu():
    return _FPU.get(_cookie("_FPU", 0) >> 16, "unknown")


def video():
    return _VIDEO.get(_cookie("_VDO", 0) >> 16, "unknown")


def _magic_version():
    """(version, state, date) of MagiC, or None.

    The MagX cookie points to a structure whose 3rd long word is
    the address of the AES variables (AESVARS, see ostruct.h in the
    MagiC sources). After checking the magic $87654321, we read there:
    +16 the date (DDMMYYYY in BCD: D_BCD_DDMMYYYY in aesmain.s, which is
    authoritative over the "mmddyyyy" comment in ostruct.h), +48 the version
    ($0620 = 6.20), +50
    the state (0 alpha, 1 beta, 2 gamma, 3 final). aesvars is 0 as long as
    the AES has not started, for instance from the AUTO folder.

    The version in the system header (uname().release) is NOT suitable:
    MagiC imitates a TOS there, and showed "2.00" on the STE.
    """
    import machine
    p = _cookie("MagX")
    if not p:
        return None
    aes = machine.mem32[p + 8]
    if not aes or machine.mem32[aes] != 0x87654321:
        return None
    return (machine.mem16[aes + 48], machine.mem16[aes + 50],
            machine.mem32[aes + 16])


def system():
    """The operating system actually running, not just 'TOS'.

    EmuTOS reports a TOS version number on purpose, and does not always
    install an ETOS cookie -- on a TwiSTer STE it installs none. So the ROM
    signature is what decides, and the cookie is only a fallback.
    """
    if _cookie("MagX") is not None:
        return "MagiC"
    if _cookie("MiNT") is not None:
        return "FreeMiNT"
    if uos.osinfo()["emutos"] or _cookie("ETOS") is not None:
        return "EmuTOS"
    return "TOS"


def release():
    """Version of that operating system, as a string."""
    sys_name = system()
    if sys_name == "EmuTOS":
        v = _cookie("ETOS")
        if v:
            return "%d.%d" % ((v >> 8) & 0xFF, v & 0xFF)
        # No ETOS cookie: EmuTOS mimics a TOS version, so report that plus
        # its build date, which is the part that actually identifies it.
        info = uos.osinfo()
        d = info["date"]
        return "(TOS %s, build %02x/%02x/%04x)" % (
            uos.uname().release, (d >> 24) & 0xFF, (d >> 16) & 0xFF, d & 0xFFFF)
    if sys_name == "MagiC":
        m = _magic_version()
        if m is None:
            return "?"
        v, etat, date = m
        txt = "%x.%02x" % (v >> 8, v & 0xFF)
        if etat < 3:
            txt += " " + ("alpha", "beta", "gamma")[etat]
        # date DDMMYYYY in BCD: 0x17031999 -> 17/03/1999
        return "%s (%02x/%02x/%04x)" % (
            txt, date >> 24, (date >> 16) & 0xFF, date & 0xFFFF)
    if sys_name == "FreeMiNT":
        v = _cookie("MiNT", 0)
        return "%d.%d" % ((v >> 8) & 0xFF, v & 0xFF)
    return uos.uname().release


def version():
    return "%s %s" % (system(), release())


def multitasking():
    """Which multitasking layer is in the way, if any."""
    if _cookie("Gnva") is not None:
        return "Geneva"
    if _cookie("MagX") is not None:
        return "MagiC"
    if _cookie("MiNT") is not None:
        return "MiNT"
    return None


def node():
    return machine()


def python_implementation():
    return "MicroPython"


def python_version():
    v = sys.implementation.version
    return "%d.%d.%d" % (v[0], v[1], v[2])


def platform():
    return "%s-%s-%s-%s-MicroPython_%s" % (
        system(), release(), machine(), processor(), python_version())


def uname():
    return uos.uname()


def atari_info():
    """One line of everything worth knowing about this machine."""
    st, alt = uos.meminfo()
    mt = multitasking()
    return "%s %s | %s %s | FPU: %s | %s | free: %dK ST, %dK alt%s" % (
        machine(), processor(), system(), release(), fpu(), video(),
        st // 1024, alt // 1024,
        " | " + mt if mt else "")
