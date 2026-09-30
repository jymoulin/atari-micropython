# os.path for Atari TOS.
#
# GEMDOS conventions:
#   "C:"          a drive, with its own current directory
#   "C:\DIR\F.PY" absolute on drive C
#   "\DIR\F.PY"   absolute on the current drive
#   "DIR\F.PY"    relative
# '/' is accepted everywhere in place of '\' and the C module normalises it
# before GEMDOS ever sees it, so both spellings work here too.
#
# As on DOS, "C:FOO" means FOO relative to drive C's current directory, which
# is why join("C:", "FOO") does NOT insert a separator.

import uos

sep = "\\"
altsep = "/"
extsep = "."
pathsep = ";"
curdir = "."
pardir = ".."


def _isep(c):
    return c == "\\" or c == "/"


def splitdrive(path):
    if len(path) > 1 and path[1] == ":":
        return (path[:2], path[2:])
    return ("", path)


def isabs(path):
    drive, rest = splitdrive(path)
    return bool(rest) and _isep(rest[0])


def join(*paths):
    if not paths:
        return ""
    result = paths[0]
    for p in paths[1:]:
        if not p:
            continue
        d, rest = splitdrive(p)
        if d or (rest and _isep(rest[0])):
            # Absolute, or names another drive: it wins.
            result = p
        elif not result:
            result = p
        elif _isep(result[-1]) or result[-1] == ":":
            result += p
        else:
            result += sep + p
    return result


def split(path):
    drive, rest = splitdrive(path)
    i = max(rest.rfind("\\"), rest.rfind("/"))
    if i < 0:
        return (drive, rest)
    head = rest[:i + 1]
    tail = rest[i + 1:]
    # Keep the separator only when it is the root itself.
    if len(head) > 1:
        head = head.rstrip("\\/")
        if not head:
            head = sep
    return (drive + head, tail)


def basename(path):
    return split(path)[1]


def dirname(path):
    return split(path)[0]


def splitext(path):
    name = basename(path)
    i = name.rfind(".")
    if i <= 0:
        return (path, "")
    ext = name[i:]
    return (path[:len(path) - len(ext)], ext)


def normpath(path):
    drive, rest = splitdrive(path)
    rooted = bool(rest) and _isep(rest[0])
    parts = rest.replace("/", "\\").split("\\")

    out = []
    for p in parts:
        if p == "" or p == curdir:
            continue
        if p == pardir:
            if out and out[-1] != pardir:
                out.pop()
            elif not rooted:
                out.append(p)
            continue
        out.append(p)

    joined = sep.join(out)
    if rooted:
        joined = sep + joined
    if not joined and not drive:
        return curdir
    return drive + joined


def abspath(path):
    if not path:
        path = curdir
    drive, rest = splitdrive(path)
    if drive and rest and _isep(rest[0]):
        return normpath(path)
    cwd = uos.getcwd()
    if drive:
        # "C:FOO" -- relative to that drive, which we cannot query without
        # changing to it. Only the current drive's cwd is known.
        cdrive, _ = splitdrive(cwd)
        if cdrive.upper() != drive.upper():
            return normpath(path)
        return normpath(join(cwd, rest))
    return normpath(join(cwd, path))


def exists(path):
    return uos._stat_type(path) != 0


def isdir(path):
    return uos._stat_type(path) == 1


def isfile(path):
    return uos._stat_type(path) == 2


def getsize(path):
    return uos.stat(path)[6]


def getmtime(path):
    return uos.stat(path)[8]
