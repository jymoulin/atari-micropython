// machine.Getbpb, machine.Mediach and machine.disk_read: the BIOS disk calls.
//
// READ-ONLY, DELIBERATELY. Rwabs can write any sector of any drive, and one
// wrong sector number turns a partition into scrap -- including the CF card
// this port is tested from. Writing is left out until there is a use for it
// that justifies the risk; reading already covers the tools that matter
// (boot sector dump, disk imaging, forensics on a damaged floppy).
//
// Buffers: the floppy controller's DMA sees only ST-RAM, like the sound DMA.
// Our heap is in alternate RAM, so every read goes through a bounce buffer
// taken from ST-RAM with Mxalloc(n, 0) and copied out afterwards.

#include <string.h>

#include "py/runtime.h"
#include "py/mperrno.h"

#include <mint/osbind.h>
#include <mint/ostruct.h>

extern void atari_raise_gemdos(long err);

// A drive as 0..25, or as "C" / "c:".
static int drive_arg(mp_obj_t d) {
    if (mp_obj_is_int(d)) {
        mp_int_t n = mp_obj_get_int(d);
        if (n < 0 || n > 25) {
            mp_raise_ValueError(MP_ERROR_TEXT("drive must be 0..25"));
        }
        return (int)n;
    }
    const char *s = mp_obj_str_get_str(d);
    char c = s[0];
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c < 'A' || c > 'Z') {
        mp_raise_ValueError(MP_ERROR_TEXT("drive must be a letter"));
    }
    return c - 'A';
}

static _BPB *get_bpb(int drive) {
    _BPB *bpb = (_BPB *)Getbpb(drive);
    if (bpb == NULL || (long)bpb < 0) {
        mp_raise_OSError(MP_ENODEV);
    }
    return bpb;
}

// machine.Getbpb(drive) -> dict describing the filesystem layout.
static mp_obj_t machine_getbpb(mp_obj_t drive_in) {
    _BPB *bpb = get_bpb(drive_arg(drive_in));
    mp_obj_t d = mp_obj_new_dict(9);
    #define PUT(name, v) mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_##name), \
                                           mp_obj_new_int((unsigned short)(v)))
    PUT(recsiz, bpb->recsiz);       // bytes per (logical) sector
    PUT(clsiz, bpb->clsiz);         // sectors per cluster
    PUT(clsizb, bpb->clsizb);       // bytes per cluster
    PUT(rdlen, bpb->rdlen);         // root directory length, in sectors
    PUT(fsiz, bpb->fsiz);           // FAT length, in sectors
    PUT(fatrec, bpb->fatrec);       // first sector of the second FAT
    PUT(datrec, bpb->datrec);       // first data sector
    PUT(numcl, bpb->numcl);         // number of clusters
    PUT(bflags, bpb->bflags);       // bit 0: 16-bit FAT
    #undef PUT
    return d;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_getbpb_obj, machine_getbpb);

// machine.Mediach(drive) -> 0 unchanged, 1 maybe changed, 2 changed.
static mp_obj_t machine_mediach(mp_obj_t drive_in) {
    long r = Mediach(drive_arg(drive_in));
    if (r < 0) {
        // A BIOS error, not a state: -15 for a drive with no BIOS device
        // behind it (Hatari's GEMDOS-emulated drive, a network drive).
        atari_raise_gemdos(r);
    }
    return mp_obj_new_int(r);
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_mediach_obj, machine_mediach);

#define DISK_READ_MAX   (64 * 1024L)

// machine.disk_read(drive, sector, count=1) -> bytes
//
// Logical sectors of the drive, numbered from 0 (the boot sector), each
// Getbpb(drive)["recsiz"] bytes -- 512 on a floppy, often more on a big
// hard disk partition. At most 64 KB per call.
static mp_obj_t machine_disk_read(size_t n_args, const mp_obj_t *args) {
    int drive = drive_arg(args[0]);
    mp_int_t sector = mp_obj_get_int(args[1]);
    mp_int_t count = (n_args > 2) ? mp_obj_get_int(args[2]) : 1;
    if (sector < 0 || count < 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad sector or count"));
    }

    _BPB *bpb = get_bpb(drive);
    long size = (long)(unsigned short)bpb->recsiz * count;
    if (size > DISK_READ_MAX) {
        mp_raise_ValueError(MP_ERROR_TEXT("at most 64 KB per read"));
    }

    // ST-RAM bounce buffer, see the top of the file.
    void *bounce = (void *)Mxalloc(size, 0);
    if (bounce == NULL || (long)bounce < 0) {
        mp_raise_OSError(MP_ENOMEM);
    }

    long err;
    if (sector > 32767) {
        // Long form: sector -1 in the short slot, the real one appended.
        err = Lrwabs(0, bounce, (int)count, sector, drive);
    } else {
        err = Rwabs(0, bounce, (int)count, (int)sector, drive);
    }
    if (err < 0) {
        Mfree(bounce);
        atari_raise_gemdos(err);
    }

    mp_obj_t result = mp_obj_new_bytes(bounce, (size_t)size);
    Mfree(bounce);
    return result;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_disk_read_obj, 2, 3, machine_disk_read);
