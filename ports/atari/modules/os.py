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
    """Rediriger une poignee standard GEMDOS le temps d'un bloc.

        with redirect(1, open("OUT.TXT", "w")) as r:
            system("C:\\BIN\\LS.TTP")

    La cible est un fichier ouvert ou un numero de poignee. Le fichier n'est
    pas ferme en sortie du bloc : c'est a l'appelant, qui l'a ouvert.

    ATTENTION : la redirection N'EST PAS ACQUISE sur cette machine. Elle a
    fait redemarrer le STE le 15/09/2026 (Fclose de la poignee sauvee), puis
    plante en bombes le 16/09 au meme endroit une fois ce Fclose retire.
    Les sources ne concordent pas : le test dupbug.c de th.otto commente son
    Fclose et MagiC comme FreeMiNT comptent des references que TOS n'a pas,
    mais posix/system.c et stdio/popen.c de la MiNTlib font la sequence
    complete, Fclose compris. Et unix/dup2.c avertit que TOS a un "double
    re-direction bug". On prend donc le parti prudent : ne jamais fermer la
    poignee sauvee. Sous MiNT et MagiC cela laisse filer une reference par
    redirection, sans consequence a cette echelle.
    A verifier avec FORCETST.TTP avant de s'appuyer dessus.
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
            self._saved = None      # surtout PAS de uos.close() ici
        return False
