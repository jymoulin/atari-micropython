// Low-level 'uos' module for the Atari TOS port.
//
// Everything here talks to GEMDOS directly rather than going through
// MiNTlib's POSIX layer. That is a deliberate choice: GEMDOS semantics are
// what the user sees in Teradesk and EmuCon -- drive letters, 8.3 names,
// a DTA-based directory walk -- and a thin translation of them is easier to
// reason about than a POSIX emulation whose path rewriting we would then
// have to second-guess. File reading and writing still go through the POSIX
// VFS, which MiNTlib serves.
//
// Paths: both '\' and '/' are accepted on the way in and normalised to '\',
// which every TOS version understands. Filenames are converted between the
// Atari ST character set and UTF-8 at the boundary, as the console layer does.

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "py/runtime.h"
#include "py/objstr.h"
#include "py/objtuple.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "extmod/vfs.h"     // MP_S_IFDIR, MP_S_IFREG
#include "shared/timeutils/timeutils.h"

#include <mint/osbind.h>
#include <mint/ostruct.h>   // _DOSTIME

#define PATH_MAX_ST     (260)

// MiNTlib's <mint/ostruct.h> already declares the DTA (_DTA, with dta_*
// fields), so we use that rather than re-declaring the layout here.

#define FA_READONLY     0x01
#define FA_HIDDEN       0x02
#define FA_SYSTEM       0x04
#define FA_VOLUME       0x08
#define FA_SUBDIR       0x10
#define FA_ARCHIVE      0x20
// Everything but the volume label: Fsfirst's attribute argument says which
// extra kinds to include, plain files always match.
#define FA_ALL          (FA_READONLY | FA_HIDDEN | FA_SYSTEM | FA_SUBDIR | FA_ARCHIVE)

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

// GEMDOS reports failure as a small negative number. Map the ones a file API
// can actually produce; anything else becomes EIO rather than a lie.
static int gemdos_errno(long err) {
    switch (err) {
        case -32: return MP_EINVAL;     // invalid function
        case -33: return MP_ENOENT;     // file not found
        case -34: return MP_ENOENT;     // path not found
        case -35: return MP_EMFILE;     // no more handles
        case -36: return MP_EACCES;     // access denied
        case -37: return MP_EBADF;      // invalid handle
        case -39: return MP_ENOMEM;     // insufficient memory
        case -40: return MP_EFAULT;     // invalid memory block address
        case -46: return MP_ENODEV;     // invalid drive
        case -49: return MP_ENOENT;     // no more files
        case -58: return MP_EXDEV;      // not the same drive
        case -66: return MP_ENOEXEC;    // invalid program format
        // BIOS errors, from Rwabs and friends.
        case -2:  return MP_ENODEV;     // drive not ready
        case -13: return MP_EROFS;      // write protected
        case -14: return MP_EAGAIN;     // media changed: retry
        case -15: return MP_ENODEV;     // unknown device
        default:  return MP_EIO;
    }
}

static void raise_gemdos(long err) {
    mp_raise_OSError(gemdos_errno(err));
}

// The same mapping for the other C files of the port.
void atari_raise_gemdos(long err) {
    raise_gemdos(err);
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

// UTF-8 Python string -> GEMDOS path: charset conversion plus '/' -> '\'.
static const char *path_arg(mp_obj_t obj, char *buf, size_t bufsize) {
    const char *utf8 = mp_obj_str_get_str(obj);
    const char *st = atari_utf8_to_st(utf8, buf, bufsize);

    // atari_utf8_to_st returns its argument unchanged for pure ASCII, in which
    // case we still need our own copy to rewrite the separators.
    if (st != buf) {
        size_t len = strlen(st);
        if (len >= bufsize) {
            mp_raise_OSError(MP_EINVAL);
        }
        memcpy(buf, st, len + 1);
    }
    for (char *p = buf; *p; p++) {
        if (*p == '/') {
            *p = '\\';
        }
    }
    return buf;
}

// Same conversion, but without rewriting the separators: environment values
// are not paths, and a '/' in one has no business becoming a backslash.
static const char *str_arg(mp_obj_t obj, char *buf, size_t bufsize) {
    const char *utf8 = mp_obj_str_get_str(obj);
    const char *st = atari_utf8_to_st(utf8, buf, bufsize);
    if (st != buf) {
        size_t len = strlen(st);
        if (len >= bufsize) {
            mp_raise_OSError(MP_EINVAL);
        }
        memcpy(buf, st, len + 1);
    }
    return buf;
}

// A GEMDOS filename back to a Python string.
static mp_obj_t str_from_st(const char *s, size_t len) {
    char utf8[PATH_MAX_ST * 3];
    mp_uint_t n = atari_st_to_utf8(s, len, utf8, sizeof(utf8));
    return mp_obj_new_str(utf8, n);
}

// "C:", "C:\", "C:/" -- a drive root has no directory entry of its own, so
// listdir and stat have to special-case it.
static int is_drive_root(const char *p) {
    if (p[0] == '\0') {
        return 0;
    }
    if (p[1] == ':' && (p[2] == '\0' || (p[2] == '\\' && p[3] == '\0'))) {
        return 1;
    }
    return (p[0] == '\\' && p[1] == '\0');
}

// ---------------------------------------------------------------------------
// Directory listing
// ---------------------------------------------------------------------------

// Fsfirst/Fsnext report through the DTA, so it has to stay put for the whole
// walk and the caller's DTA has to be handed back afterwards.
static mp_obj_t listdir_common(const char *path, int want_tuples) {
    char spec[PATH_MAX_ST + 8];
    size_t len = strlen(path);

    if (len == 0) {
        strcpy(spec, "*.*");
    } else if (len + 5 > sizeof(spec)) {
        mp_raise_OSError(MP_EINVAL);
    } else {
        strcpy(spec, path);
        char last = spec[len - 1];
        if (last != '\\' && last != ':') {
            strcat(spec, "\\");
        }
        strcat(spec, "*.*");
    }

    _DTA dta;
    _DTA *old_dta = Fgetdta();
    Fsetdta(&dta);

    mp_obj_t list = mp_obj_new_list(0, NULL);
    long err = Fsfirst(spec, FA_ALL);
    while (err == 0) {
        const char *name = dta.dta_name;
        if (!(name[0] == '.' && (name[1] == '\0'
                || (name[1] == '.' && name[2] == '\0')))) {
            mp_obj_t nm = str_from_st(name, strlen(name));
            if (want_tuples) {
                mp_obj_t t[3] = {
                    nm,
                    MP_OBJ_NEW_SMALL_INT((dta.dta_attribute & FA_SUBDIR)
                                         ? MP_S_IFDIR : MP_S_IFREG),
                    MP_OBJ_NEW_SMALL_INT(0),
                };
                mp_obj_list_append(list, mp_obj_new_tuple(3, t));
            } else {
                mp_obj_list_append(list, nm);
            }
        }
        err = Fsnext();
    }
    Fsetdta(old_dta);

    // -49 ENMFIL is how the walk ends; -33 means the pattern matched nothing,
    // which for a directory that exists simply means it is empty.
    if (err != -49 && err != -33 && err != 0) {
        raise_gemdos(err);
    }
    return list;
}

static mp_obj_t mod_os_listdir(size_t n_args, const mp_obj_t *args) {
    char buf[PATH_MAX_ST];
    const char *path = (n_args > 0) ? path_arg(args[0], buf, sizeof(buf)) : "";
    return listdir_common(path, 0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_os_listdir_obj, 0, 1, mod_os_listdir);

static mp_obj_t mod_os_ilistdir(size_t n_args, const mp_obj_t *args) {
    char buf[PATH_MAX_ST];
    const char *path = (n_args > 0) ? path_arg(args[0], buf, sizeof(buf)) : "";
    // Not a true generator: TOS directories are small and a list keeps the DTA
    // walk atomic, which matters under a multitasking layer like Geneva.
    return mp_getiter(listdir_common(path, 1), NULL);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_os_ilistdir_obj, 0, 1, mod_os_ilistdir);

// ---------------------------------------------------------------------------
// Current directory
// ---------------------------------------------------------------------------

static mp_obj_t mod_os_getcwd(void) {
    char path[PATH_MAX_ST];
    int drive = (int)Dgetdrv();
    long err = Dgetpath(path, drive + 1);
    if (err < 0) {
        raise_gemdos(err);
    }

    char full[PATH_MAX_ST + 4];
    full[0] = (char)('A' + drive);
    full[1] = ':';
    if (path[0] == '\0') {
        full[2] = '\\';
        full[3] = '\0';
    } else {
        strncpy(full + 2, path, sizeof(full) - 3);
        full[sizeof(full) - 1] = '\0';
    }
    return str_from_st(full, strlen(full));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_os_getcwd_obj, mod_os_getcwd);

static mp_obj_t mod_os_chdir(mp_obj_t path_in) {
    char buf[PATH_MAX_ST];
    const char *path = path_arg(path_in, buf, sizeof(buf));

    if (path[0] != '\0' && path[1] == ':') {
        int drive = path[0];
        if (drive >= 'a' && drive <= 'z') {
            drive -= 'a';
        } else if (drive >= 'A' && drive <= 'Z') {
            drive -= 'A';
        } else {
            mp_raise_OSError(MP_ENODEV);
        }
        long err = Dsetdrv(drive);
        (void)err;      // Dsetdrv returns the drive bitmap, not a status
        path += 2;
        if (path[0] == '\0') {
            return mp_const_none;   // "C:" alone means just the drive
        }
    }

    long err = Dsetpath(path);
    if (err < 0) {
        raise_gemdos(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_os_chdir_obj, mod_os_chdir);

// ---------------------------------------------------------------------------
// Creating and removing
// ---------------------------------------------------------------------------

// Fsfirst on the path itself, for stat() and for telling "already there" from
// "refused". Returns 0 and fills dta on success, else the GEMDOS error.
static long stat_entry(const char *path, _DTA *dta) {
    _DTA *old_dta = Fgetdta();
    Fsetdta(dta);
    long err = Fsfirst(path, FA_ALL);
    Fsetdta(old_dta);
    return err;
}

static mp_obj_t mod_os_mkdir(mp_obj_t path_in) {
    char buf[PATH_MAX_ST];
    const char *path = path_arg(path_in, buf, sizeof(buf));
    long err = Dcreate(path);
    if (err < 0) {
        // GEMDOS says "access denied" whether the name is taken or the drive
        // is write-protected. makedirs(exist_ok=True) needs to tell them
        // apart, so look.
        _DTA dta;
        if (err == -36 && stat_entry(path, &dta) == 0) {
            mp_raise_OSError(MP_EEXIST);
        }
        raise_gemdos(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_os_mkdir_obj, mod_os_mkdir);

static mp_obj_t mod_os_rmdir(mp_obj_t path_in) {
    char buf[PATH_MAX_ST];
    const char *path = path_arg(path_in, buf, sizeof(buf));
    long err = Ddelete(path);
    if (err < 0) {
        raise_gemdos(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_os_rmdir_obj, mod_os_rmdir);

static mp_obj_t mod_os_remove(mp_obj_t path_in) {
    char buf[PATH_MAX_ST];
    const char *path = path_arg(path_in, buf, sizeof(buf));
    long err = Fdelete(path);
    if (err < 0) {
        raise_gemdos(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_os_remove_obj, mod_os_remove);

static mp_obj_t mod_os_rename(mp_obj_t old_in, mp_obj_t new_in) {
    char oldbuf[PATH_MAX_ST], newbuf[PATH_MAX_ST];
    const char *oldp = path_arg(old_in, oldbuf, sizeof(oldbuf));
    const char *newp = path_arg(new_in, newbuf, sizeof(newbuf));
    long err = Frename(0, oldp, newp);
    if (err < 0) {
        raise_gemdos(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_os_rename_obj, mod_os_rename);

// ---------------------------------------------------------------------------
// stat
// ---------------------------------------------------------------------------

static mp_obj_t mod_os_stat(mp_obj_t path_in) {
    char buf[PATH_MAX_ST];
    const char *path = path_arg(path_in, buf, sizeof(buf));

    mp_int_t mode, size = 0;
    mp_int_t mtime = 0;

    if (is_drive_root(path)) {
        // A drive root has no directory entry. Confirm the drive is there
        // with Dfree rather than inventing an answer.
        uint32_t dfree[4];
        int drive = (path[1] == ':') ? (toupper((unsigned char)path[0]) - 'A' + 1) : 0;
        long err = Dfree(dfree, drive);
        if (err < 0) {
            raise_gemdos(err);
        }
        mode = MP_S_IFDIR | 0755;
    } else {
        _DTA dta;
        long err = stat_entry(path, &dta);
        if (err < 0) {
            raise_gemdos(err);
        }
        if (dta.dta_attribute & FA_SUBDIR) {
            mode = MP_S_IFDIR | 0755;
        } else {
            mode = MP_S_IFREG | ((dta.dta_attribute & FA_READONLY) ? 0444 : 0644);
        }
        size = (mp_int_t)dta.dta_size;
        mtime = (mp_int_t)atari_dostime_to_epoch(dta.dta_date, dta.dta_time);
    }

    mp_obj_t t[10] = {
        MP_OBJ_NEW_SMALL_INT(mode),         // st_mode
        MP_OBJ_NEW_SMALL_INT(0),            // st_ino
        MP_OBJ_NEW_SMALL_INT(0),            // st_dev
        MP_OBJ_NEW_SMALL_INT(1),            // st_nlink
        MP_OBJ_NEW_SMALL_INT(0),            // st_uid
        MP_OBJ_NEW_SMALL_INT(0),            // st_gid
        mp_obj_new_int(size),               // st_size
        mp_obj_new_int(mtime),              // st_atime  (GEMDOS keeps one time)
        mp_obj_new_int(mtime),              // st_mtime
        mp_obj_new_int(mtime),              // st_ctime
    };
    return mp_obj_new_tuple(10, t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_os_stat_obj, mod_os_stat);

static mp_obj_t mod_os_statvfs(mp_obj_t path_in) {
    char buf[PATH_MAX_ST];
    const char *path = path_arg(path_in, buf, sizeof(buf));

    int drive = 0;      // 0 = current drive
    if (path[0] != '\0' && path[1] == ':') {
        drive = toupper((unsigned char)path[0]) - 'A' + 1;
    }

    uint32_t d[4];      // free clusters, total clusters, bytes/sector, sectors/cluster
    long err = Dfree(d, drive);
    if (err < 0) {
        raise_gemdos(err);
    }
    mp_int_t bsize = (mp_int_t)(d[2] * d[3]);

    mp_obj_t t[10] = {
        mp_obj_new_int(bsize),              // f_bsize
        mp_obj_new_int(bsize),              // f_frsize
        mp_obj_new_int((mp_int_t)d[1]),     // f_blocks
        mp_obj_new_int((mp_int_t)d[0]),     // f_bfree
        mp_obj_new_int((mp_int_t)d[0]),     // f_bavail
        MP_OBJ_NEW_SMALL_INT(0),            // f_files
        MP_OBJ_NEW_SMALL_INT(0),            // f_ffree
        MP_OBJ_NEW_SMALL_INT(0),            // f_favail
        MP_OBJ_NEW_SMALL_INT(0),            // f_flag
        MP_OBJ_NEW_SMALL_INT(12),           // f_namemax  (8.3)
    };
    return mp_obj_new_tuple(10, t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_os_statvfs_obj, mod_os_statvfs);

// ---------------------------------------------------------------------------
// The cookie jar
// ---------------------------------------------------------------------------
//
// Only the jar POINTER, at 0x5A0, needs supervisor mode: the low 2 KB of RAM
// is protected on every ST. The jar it points at lives in ordinary memory, so
// the walk itself runs in user mode.

static uint32_t *jar_ptr;

static long read_jar_ptr(void) {
    jar_ptr = *(uint32_t **)0x5A0L;
    return 0;
}

static uint32_t *cookie_jar(void) {
    Supexec(read_jar_ptr);
    return jar_ptr;
}

// A cookie id is four characters packed into a long.
static uint32_t cookie_id(const char *s) {
    uint32_t id = 0;
    for (int i = 0; i < 4; i++) {
        id = (id << 8) | (uint8_t)(s[i] ? s[i] : ' ');
        if (!s[i]) {
            // Pad a short name with spaces, as the convention has it.
            for (int j = i + 1; j < 4; j++) {
                id = (id << 8) | (uint8_t)' ';
            }
            break;
        }
    }
    return id;
}

// For the C side of the port: 1 and *val filled in if the cookie exists.
int atari_get_cookie(const char *name, uint32_t *val) {
    uint32_t want = cookie_id(name);
    uint32_t *jar = cookie_jar();
    if (jar == NULL) {
        return 0;
    }
    for (; jar[0] != 0; jar += 2) {
        if (jar[0] == want) {
            *val = jar[1];
            return 1;
        }
    }
    return 0;
}

// os.cookie("_CPU") -> 20, or None when absent.
static mp_obj_t mod_os_cookie(mp_obj_t name_in) {
    const char *name = mp_obj_str_get_str(name_in);
    uint32_t want = cookie_id(name);

    uint32_t *jar = cookie_jar();
    if (jar == NULL) {
        return mp_const_none;
    }
    while (jar[0] != 0) {
        if (jar[0] == want) {
            return mp_obj_new_int_from_uint(jar[1]);
        }
        jar += 2;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_os_cookie_obj, mod_os_cookie);

// Every cookie, as {name: value}. The jar is how an Atari says what it is:
// which TOS, which video shifter, whether NVDI or Geneva or MiNT is loaded.
static mp_obj_t mod_os_cookies(void) {
    mp_obj_t dict = mp_obj_new_dict(0);
    uint32_t *jar = cookie_jar();
    if (jar == NULL) {
        return dict;
    }
    // The jar ends with a zero id; guard the walk anyway, a corrupt jar
    // should not hang the interpreter.
    for (int n = 0; n < 256 && jar[0] != 0; n++, jar += 2) {
        char name[5];
        name[0] = (char)(jar[0] >> 24);
        name[1] = (char)(jar[0] >> 16);
        name[2] = (char)(jar[0] >> 8);
        name[3] = (char)(jar[0]);
        name[4] = '\0';
        mp_obj_dict_store(dict, mp_obj_new_str(name, 4),
                          mp_obj_new_int_from_uint(jar[1]));
    }
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_os_cookies_obj, mod_os_cookies);

// The OS header, at the pointer in _sysbase (0x4F2). EmuTOS deliberately
// reports a TOS version number and, on this machine at least, installs no
// ETOS cookie -- so the only honest way to recognise it is by the mark it
// leaves in its own OS header (see read_osheader).
static long os_version_val, os_date_val, os_conf_val, is_emutos_val;

static long read_osheader(void) {
    const uint8_t *sys = *(const uint8_t **)0x4F2L;
    os_version_val = *(const uint16_t *)(sys + 2);
    os_date_val    = *(const uint32_t *)(sys + 0x18);
    os_conf_val    = *(const uint16_t *)(sys + 0x1C);

    // EmuTOS writes "ETOS" in the long at +0x2C of its OS header, a field
    // (p_rsv2) that Atari's TOS leaves at 0. Checked on three ROM images:
    // EmuTOS 1 MB multilingual and 512 KB FR carry it, TOS 2.06 and 4.04
    // have 0. This is the test that counts.
    //
    // The old test only looked for the string "EmuTOS" in the first 8 KB,
    // and missed EmuTOS 1.4 both on the STE and under Hatari: in the 1 MB
    // image the string first appears at byte 30186. It stays as a fallback
    // for any old EmuTOS without the header mark.
    is_emutos_val = (*(const uint32_t *)(sys + 0x2C) == 0x45544F53UL);  // "ETOS"
    if (is_emutos_val) {
        return 0;
    }
    const char *rom = (const char *)sys;
    for (int i = 0; i < 8192; i++) {
        if (rom[i] == 'E' && rom[i + 1] == 'm' && rom[i + 2] == 'u'
                && rom[i + 3] == 'T' && rom[i + 4] == 'O' && rom[i + 5] == 'S') {
            is_emutos_val = 1;
            break;
        }
    }
    return 0;
}

// {'version': 0x0206, 'date': ..., 'conf': ..., 'emutos': False}
static mp_obj_t mod_os_osinfo(void) {
    Supexec(read_osheader);
    mp_obj_t d = mp_obj_new_dict(4);
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_version),
                      mp_obj_new_int(os_version_val));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_date),
                      mp_obj_new_int_from_uint((mp_uint_t)os_date_val));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_conf),
                      mp_obj_new_int(os_conf_val));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_emutos),
                      mp_obj_new_bool(is_emutos_val));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_os_osinfo_obj, mod_os_osinfo);

// os.settime(year, month, day, hour, minute, second) -- set the GEMDOS clock.
// The software clock is kept by the 200 Hz interrupt and loses ticks whenever
// something masks interrupts, so being able to put it back in step with a
// hardware RTC from Python is worth the twenty lines.
static mp_obj_t mod_os_settime(size_t n_args, const mp_obj_t *args) {
    mp_int_t year = mp_obj_get_int(args[0]);
    mp_int_t month = mp_obj_get_int(args[1]);
    mp_int_t day = mp_obj_get_int(args[2]);
    mp_int_t hour = mp_obj_get_int(args[3]);
    mp_int_t minute = mp_obj_get_int(args[4]);
    mp_int_t second = (n_args > 5) ? mp_obj_get_int(args[5]) : 0;

    if (year < 1980 || year > 2107 || month < 1 || month > 12
            || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) {
        mp_raise_ValueError(MP_ERROR_TEXT("date out of range"));
    }

    // GEMDOS packs the date as yyyyyyymmmmddddd from 1980, and the time as
    // hhhhhmmmmmmsssss with the seconds halved -- one bit short, hence the
    // two-second granularity.
    unsigned int d = (unsigned int)(((year - 1980) << 9) | (month << 5) | day);
    unsigned int t = (unsigned int)((hour << 11) | (minute << 5) | (second / 2));

    long err = Tsetdate(d);
    if (err < 0) {
        raise_gemdos(err);
    }
    err = Tsettime(t);
    if (err < 0) {
        raise_gemdos(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_os_settime_obj, 5, 6, mod_os_settime);

// (ST-RAM free, alternate RAM free), in bytes.
static mp_obj_t mod_os_meminfo(void) {
    extern unsigned long free_stram(void);
    extern unsigned long free_altram(void);
    mp_obj_t t[2] = {
        mp_obj_new_int_from_uint(free_stram()),
        mp_obj_new_int_from_uint(free_altram()),
    };
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_os_meminfo_obj, mod_os_meminfo);

// ---------------------------------------------------------------------------
// Atari-specific
// ---------------------------------------------------------------------------

// Which drive letters exist, as a list of strings: ['A:', 'C:', 'D:'].
static mp_obj_t mod_os_drives(void) {
    unsigned long map = (unsigned long)Drvmap();
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (int i = 0; i < 26; i++) {
        if (map & (1UL << i)) {
            char name[3] = { (char)('A' + i), ':', '\0' };
            mp_obj_list_append(list, mp_obj_new_str(name, 2));
        }
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_os_drives_obj, mod_os_drives);

// 0 = nothing there, 1 = directory, 2 = file. os.path.exists/isdir/isfile go
// through this rather than catching an OSError per call: on a machine where a
// Python statement costs ~600 us, raising is not free.
static mp_obj_t mod_os_stat_type(mp_obj_t path_in) {
    char buf[PATH_MAX_ST];
    const char *path = path_arg(path_in, buf, sizeof(buf));

    if (is_drive_root(path)) {
        uint32_t dfree[4];
        int drive = (path[1] == ':') ? (toupper((unsigned char)path[0]) - 'A' + 1) : 0;
        return MP_OBJ_NEW_SMALL_INT(Dfree(dfree, drive) < 0 ? 0 : 1);
    }
    _DTA dta;
    if (stat_entry(path, &dta) < 0) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    return MP_OBJ_NEW_SMALL_INT((dta.dta_attribute & FA_SUBDIR) ? 1 : 2);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_os_stat_type_obj, mod_os_stat_type);

// Read or set the GEMDOS attribute byte of a file.
static mp_obj_t mod_os_attrib(size_t n_args, const mp_obj_t *args) {
    char buf[PATH_MAX_ST];
    const char *path = path_arg(args[0], buf, sizeof(buf));
    long err;
    if (n_args > 1) {
        err = Fattrib(path, 1, (int)mp_obj_get_int(args[1]));
    } else {
        err = Fattrib(path, 0, 0);
    }
    if (err < 0) {
        raise_gemdos(err);
    }
    return mp_obj_new_int((mp_int_t)err);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_os_attrib_obj, 1, 2, mod_os_attrib);

// Run a TOS program and return its exit code. The command tail GEMDOS wants
// is a Pascal string: a length byte, then the arguments.
static mp_obj_t mod_os_system(size_t n_args, const mp_obj_t *args) {
    char progbuf[PATH_MAX_ST];
    const char *prog = path_arg(args[0], progbuf, sizeof(progbuf));

    char tail[128];
    tail[0] = 0;
    tail[1] = '\0';
    if (n_args > 1) {
        char argbuf[PATH_MAX_ST];
        const char *a = path_arg(args[1], argbuf, sizeof(argbuf));
        size_t len = strlen(a);
        if (len > sizeof(tail) - 2) {
            len = sizeof(tail) - 2;
        }
        tail[0] = (char)len;
        memcpy(tail + 1, a, len);
        tail[len + 1] = '\0';
    }

    long ret = Pexec(0, prog, tail, NULL);
    if (ret < 0) {
        raise_gemdos(ret);
    }
    return mp_obj_new_int((mp_int_t)ret);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_os_system_obj, 1, 2, mod_os_system);

static mp_obj_t mod_os_getenv(mp_obj_t name_in) {
    const char *name = mp_obj_str_get_str(name_in);
    const char *val = getenv(name);
    if (val == NULL) {
        return mp_const_none;
    }
    return str_from_st(val, strlen(val));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_os_getenv_obj, mod_os_getenv);

// os.putenv(name, value) and os.unsetenv(name). The environment belongs to
// the process, so these only affect us and the programs we start with
// os.system() -- GEMDOS has no way to hand anything back to the shell.
static mp_obj_t mod_os_putenv(mp_obj_t name_in, mp_obj_t value_in) {
    char namebuf[128], valbuf[PATH_MAX_ST];
    const char *name = str_arg(name_in, namebuf, sizeof(namebuf));
    const char *value = str_arg(value_in, valbuf, sizeof(valbuf));
    if (setenv(name, value, 1) != 0) {
        mp_raise_OSError(MP_ENOMEM);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_os_putenv_obj, mod_os_putenv);

static mp_obj_t mod_os_unsetenv(mp_obj_t name_in) {
    char namebuf[128];
    const char *name = str_arg(name_in, namebuf, sizeof(namebuf));
    unsetenv(name);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_os_unsetenv_obj, mod_os_unsetenv);

// os.utime(path, times=None) -- set a file's timestamp.
//
// GEMDOS keeps exactly ONE timestamp per file, so the (atime, mtime) pair
// that Python hands over is honoured by its mtime alone. times=None means
// "now". Remember the two-second granularity: the seconds field is halved
// to fit in five bits, so reading back what you wrote may give you one
// second less.
static mp_obj_t mod_os_utime(size_t n_args, const mp_obj_t *args) {
    char buf[PATH_MAX_ST];
    const char *path = path_arg(args[0], buf, sizeof(buf));

    unsigned int date, time;
    if (n_args < 2 || args[1] == mp_const_none) {
        date = (unsigned int)Tgetdate();
        time = (unsigned int)Tgettime();
    } else {
        // Accept either a (atime, mtime) pair, as CPython does, or a bare
        // number -- a single timestamp is what the filesystem actually holds.
        mp_obj_t when = args[1];
        if (mp_obj_is_type(when, &mp_type_tuple) || mp_obj_is_type(when, &mp_type_list)) {
            size_t len;
            mp_obj_t *items;
            mp_obj_get_array(when, &len, &items);
            if (len != 2) {
                mp_raise_ValueError(MP_ERROR_TEXT("utime wants (atime, mtime)"));
            }
            when = items[1];        // mtime
        }
        mp_int_t secs = mp_obj_get_int(when) + mp_hal_timezone_offset_s();
        timeutils_struct_time_t tm;
        timeutils_seconds_since_1970_to_struct_time((timeutils_timestamp_t)secs, &tm);
        if (tm.tm_year < 1980 || tm.tm_year > 2107) {
            mp_raise_ValueError(MP_ERROR_TEXT("date out of range"));
        }
        date = (unsigned int)(((tm.tm_year - 1980) << 9) | (tm.tm_mon << 5) | tm.tm_mday);
        time = (unsigned int)((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
    }

    // Fdatime needs an open handle. Read/write first, because some TOS
    // versions refuse to stamp a handle opened read-only; fall back to
    // read-only so that at least the error we report is the real one.
    long fd = Fopen(path, 2);
    if (fd < 0) {
        fd = Fopen(path, 0);
    }
    if (fd < 0) {
        raise_gemdos(fd);
    }
    _DOSTIME dt;
    dt.date = (unsigned short)date;
    dt.time = (unsigned short)time;
    long err = Fdatime(&dt, (int)fd, 1);
    Fclose((int)fd);
    if (err < 0) {
        raise_gemdos(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_os_utime_obj, 1, 2, mod_os_utime);

// Handle plumbing: os.dup(), os.force() and os.close().
//
// These are what makes os.system() genuinely useful: GEMDOS gives a child
// the handles of its parent, so redirecting a TOS program's output into a
// file is a matter of pointing handle 1 elsewhere for the duration.
//
// Fforce is NOT dup2. It makes the STANDARD handle Hstd denote whatever
// Hnew currently denotes, and it is the standard handles (0-5) that a child
// inherits.
//
// THE TRAP, unsettled, and it took the machine down twice. On this STE,
// closing the saved handle after Fforce(1, saved) caused an immediate
// reboot (2026-09-15), and removing that Fclose still left bombs at the
// same spot (2026-09-16). The evidence does not all point the same way:
//   - th.otto's regression test for this is called dupbug.c and has its
//     Fclose commented out;
//   - MagiC's Fforce documents differing from "der Original-Routine"
//     precisely on the reference count, and FreeMiNT's sys_f_force does
//     fp->links++, which plain TOS has no equivalent of;
//   - BUT MiNTlib's own posix/system.c and stdio/popen.c do the whole
//     sequence including the Fclose, so it is not universally wrong;
//   - and MiNTlib's unix/dup2.c warns "TOS 1.4 still has double
//     re-direction bug!", so TOS has form here.
// One thing IS settled from source: MiNTlib file descriptors are GEMDOS
// handles (unix/open.c returns Fopen's value straight), so fileno() is a
// legitimate argument to Fforce.
// Until FORCETST.TTP says which call bombs and whether Geneva is involved,
// os.redirect() in os.py takes the cautious side and never closes the saved
// handle.
//
// os.close() stays available for handles you opened yourself and have not
// forced onto a standard handle.
static mp_obj_t mod_os_dup(mp_obj_t handle_in) {
    long fd = Fdup((int)mp_obj_get_int(handle_in));
    if (fd < 0) {
        raise_gemdos(fd);
    }
    return mp_obj_new_int((mp_int_t)fd);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_os_dup_obj, mod_os_dup);

static mp_obj_t mod_os_force(mp_obj_t std_in, mp_obj_t new_in) {
    long err = Fforce((int)mp_obj_get_int(std_in), (int)mp_obj_get_int(new_in));
    if (err < 0) {
        raise_gemdos(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_os_force_obj, mod_os_force);

static mp_obj_t mod_os_close(mp_obj_t handle_in) {
    long err = Fclose((int)mp_obj_get_int(handle_in));
    if (err < 0) {
        raise_gemdos(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_os_close_obj, mod_os_close);

static mp_obj_t mod_os_uname(void) {
    extern const char *atari_machine_name(void);
    extern long atari_tos_version(void);

    long v = atari_tos_version();
    char release[16];
    snprintf(release, sizeof(release), "%ld.%02ld", (v >> 8) & 0xFF, v & 0xFF);

    mp_obj_t t[5] = {
        mp_obj_new_str("tos", 3),
        mp_obj_new_str(atari_machine_name(), strlen(atari_machine_name())),
        mp_obj_new_str(release, strlen(release)),
        mp_obj_new_str(MICROPY_VERSION_STRING, strlen(MICROPY_VERSION_STRING)),
        mp_obj_new_str("m68k", 4),
    };
    static const qstr fields[5] = {
        MP_QSTR_sysname, MP_QSTR_nodename, MP_QSTR_release,
        MP_QSTR_version, MP_QSTR_machine,
    };
    return mp_obj_new_attrtuple(fields, 5, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_os_uname_obj, mod_os_uname);

// ---------------------------------------------------------------------------

// GEMDOS takes '\\'; '/' is accepted on input and normalised away, but this is
// the separator the platform actually shows the user.
static const MP_DEFINE_STR_OBJ(mod_os_sep_obj, "\\");

static const mp_rom_map_elem_t mp_module_os_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_uos) },
    { MP_ROM_QSTR(MP_QSTR_listdir), MP_ROM_PTR(&mod_os_listdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_ilistdir), MP_ROM_PTR(&mod_os_ilistdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_getcwd), MP_ROM_PTR(&mod_os_getcwd_obj) },
    { MP_ROM_QSTR(MP_QSTR_chdir), MP_ROM_PTR(&mod_os_chdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkdir), MP_ROM_PTR(&mod_os_mkdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_rmdir), MP_ROM_PTR(&mod_os_rmdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove), MP_ROM_PTR(&mod_os_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_unlink), MP_ROM_PTR(&mod_os_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_rename), MP_ROM_PTR(&mod_os_rename_obj) },
    { MP_ROM_QSTR(MP_QSTR_stat), MP_ROM_PTR(&mod_os_stat_obj) },
    { MP_ROM_QSTR(MP_QSTR_statvfs), MP_ROM_PTR(&mod_os_statvfs_obj) },
    { MP_ROM_QSTR(MP_QSTR_system), MP_ROM_PTR(&mod_os_system_obj) },
    { MP_ROM_QSTR(MP_QSTR_getenv), MP_ROM_PTR(&mod_os_getenv_obj) },
    { MP_ROM_QSTR(MP_QSTR_putenv), MP_ROM_PTR(&mod_os_putenv_obj) },
    { MP_ROM_QSTR(MP_QSTR_unsetenv), MP_ROM_PTR(&mod_os_unsetenv_obj) },
    { MP_ROM_QSTR(MP_QSTR_utime), MP_ROM_PTR(&mod_os_utime_obj) },
    { MP_ROM_QSTR(MP_QSTR_dup), MP_ROM_PTR(&mod_os_dup_obj) },
    { MP_ROM_QSTR(MP_QSTR_force), MP_ROM_PTR(&mod_os_force_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mod_os_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_uname), MP_ROM_PTR(&mod_os_uname_obj) },
    { MP_ROM_QSTR(MP_QSTR_drives), MP_ROM_PTR(&mod_os_drives_obj) },
    { MP_ROM_QSTR(MP_QSTR_attrib), MP_ROM_PTR(&mod_os_attrib_obj) },
    { MP_ROM_QSTR(MP_QSTR__stat_type), MP_ROM_PTR(&mod_os_stat_type_obj) },
    { MP_ROM_QSTR(MP_QSTR_cookie), MP_ROM_PTR(&mod_os_cookie_obj) },
    { MP_ROM_QSTR(MP_QSTR_cookies), MP_ROM_PTR(&mod_os_cookies_obj) },
    { MP_ROM_QSTR(MP_QSTR_meminfo), MP_ROM_PTR(&mod_os_meminfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_osinfo), MP_ROM_PTR(&mod_os_osinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_settime), MP_ROM_PTR(&mod_os_settime_obj) },
    { MP_ROM_QSTR(MP_QSTR_sep), MP_ROM_PTR(&mod_os_sep_obj) },

    { MP_ROM_QSTR(MP_QSTR_FA_READONLY), MP_ROM_INT(FA_READONLY) },
    { MP_ROM_QSTR(MP_QSTR_FA_HIDDEN), MP_ROM_INT(FA_HIDDEN) },
    { MP_ROM_QSTR(MP_QSTR_FA_SYSTEM), MP_ROM_INT(FA_SYSTEM) },
    { MP_ROM_QSTR(MP_QSTR_FA_VOLUME), MP_ROM_INT(FA_VOLUME) },
    { MP_ROM_QSTR(MP_QSTR_FA_SUBDIR), MP_ROM_INT(FA_SUBDIR) },
    { MP_ROM_QSTR(MP_QSTR_FA_ARCHIVE), MP_ROM_INT(FA_ARCHIVE) },
};
static MP_DEFINE_CONST_DICT(mp_module_os_globals, mp_module_os_globals_table);

const mp_obj_module_t mp_module_uos = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_os_globals,
};

MP_REGISTER_MODULE(MP_QSTR_uos, mp_module_uos);
