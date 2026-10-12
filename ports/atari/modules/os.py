# os module for Atari TOS -- wraps the C uos module and adds the pure-Python
# parts of the standard interface.
from uos import *
import _atpath as path


def makedirs(name, exist_ok=False):
    """Create a directory and any missing parents."""
    import uos
    import _atpath
    import errno as _errno

    drive, rest = _atpath.splitdrive(name)
    rooted = bool(rest) and (rest[0] == "\\" or rest[0] == "/")
    parts = [p for p in rest.replace("/", "\\").split("\\") if p]

    current = drive + ("\\" if rooted else "")
    for part in parts:
        current = _atpath.join(current, part)
        try:
            uos.mkdir(current)
        except OSError as e:
            # NEVER compare to a hardcoded number here: MiNTlib numbers its
            # errno values after the GEMDOS error codes, so EEXIST is 85 and
            # ENOENT is 33, not the 17 and 2 of Linux.
            if e.errno == _errno.EEXIST:
                continue
            raise
    if not parts:
        raise OSError(_errno.EEXIST if exist_ok else _errno.ENOENT, name)


def walk(top, topdown=True):
    """Directory tree generator, like CPython's os.walk."""
    import uos
    import _atpath

    try:
        entries = uos.ilistdir(top)
    except OSError:
        return

    dirs = []
    files = []
    for entry in entries:
        name = entry[0]
        if entry[1] == 0x4000:          # S_IFDIR
            dirs.append(name)
        else:
            files.append(name)

    if topdown:
        yield (top, dirs, files)

    for d in dirs:
        yield from walk(_atpath.join(top, d), topdown)

    if not topdown:
        yield (top, dirs, files)


def removedirs(name):
    """Remove a directory, then its now-empty parents."""
    import uos
    import _atpath

    uos.rmdir(name)
    while True:
        name = _atpath.dirname(name)
        if not name or name.endswith(":"):
            break
        try:
            uos.rmdir(name)
        except OSError:
            break


class redirect:
    """Redirect a standard GEMDOS handle for the duration of a block.

        with redirect(1, open("OUT.TXT", "w")) as r:
            system("C:\\BIN\\LS.TTP")

    The target is an open file or a handle number. The file is not
    closed on leaving the block: that is up to the caller, who opened it.

    WARNING: redirection is NOT ESTABLISHED as safe on this machine. It
    rebooted the STE on 15/09/2026 (Fclose of the saved handle), then
    crashed with bombs on 16/09 at the same spot once that Fclose was removed.
    The sources disagree: th.otto's dupbug.c test comments out its
    Fclose, and MagiC as well as FreeMiNT keep reference counts that TOS
    lacks, but MiNTlib's posix/system.c and stdio/popen.c do the full
    sequence, Fclose included. And unix/dup2.c warns that TOS has a "double
    re-direction bug". So we take the cautious side: never close the
    saved handle. Under MiNT and MagiC this leaks one reference per
    redirection, of no consequence at this scale.
    To be checked with FORCETST.TTP before relying on it.
    """

    def __init__(self, std, target):
        self._std = std
        self._target = target
        self._saved = None

    def __enter__(self):
        import uos
        fd = self._target if isinstance(self._target, int) else self._target.fileno()
        self._saved = uos.dup(self._std)
        uos.force(self._std, fd)
        return self

    def __exit__(self, *exc):
        import uos
        if self._saved is not None:
            uos.force(self._std, self._saved)
            self._saved = None      # above all, NO uos.close() here
        return False
