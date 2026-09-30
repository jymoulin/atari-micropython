"""CPython-compatible zipfile module for reading and writing ZIP archives."""

import io
import os
import struct
import deflate
import zlib

ZIP_STORED = 0
ZIP_DEFLATED = 8

_LOCAL_SIG = b'PK\x03\x04'
_CD_SIG = b'PK\x01\x02'
_EOCD_SIG = b'PK\x05\x06'


class ZipInfo:
    """Metadata for a file inside a ZIP archive."""

    def __init__(self, filename='', date_time=(1980, 1, 1, 0, 0, 0)):
        self.filename = filename
        self.date_time = date_time
        self.compress_type = ZIP_STORED
        self.CRC = 0
        self.compress_size = 0
        self.file_size = 0
        self.external_attr = 0
        self._header_offset = 0

    def __repr__(self):
        return "<ZipInfo '%s'>" % self.filename


class ZipFile:
    """Read or write ZIP archives.

    Usage:
        # Read
        with ZipFile("archive.zip", "r") as z:
            print(z.namelist())
            data = z.read("file.txt")
            z.extractall("dest/")

        # Write (stored)
        with ZipFile("out.zip", "w") as z:
            z.write("myfile.py")
            z.writestr("hello.txt", "Hello Amiga!")

        # Write (compressed)
        with ZipFile("out.zip", "w", ZIP_DEFLATED) as z:
            z.write("bigfile.dat")
    """

    def __init__(self, file, mode='r', compression=ZIP_STORED):
        if mode not in ('r', 'w'):
            raise ValueError("mode must be 'r' or 'w'")
        self.mode = mode
        self._compression = compression
        if isinstance(file, str):
            self._fp = open(file, 'rb' if mode == 'r' else 'wb')
            self._own_fp = True
        else:
            self._fp = file
            self._own_fp = False
        self._filelist = []
        self._name_index = {}
        self.closed = False

        if mode == 'r':
            self._read_central_dir()

    def _read_central_dir(self):
        """Parse the central directory from the end of the ZIP file."""
        fp = self._fp
        fp.seek(0, 2)
        file_size = fp.tell()
        if file_size < 22:
            raise Exception("not a zip file")

        # Try EOCD at the very end (no comment — most common case)
        fp.seek(file_size - 22)
        eocd = fp.read(22)
        if eocd[:4] != _EOCD_SIG:
            # Search last 4KB for EOCD with comment
            search_len = min(4096, file_size)
            fp.seek(file_size - search_len)
            data = fp.read(search_len)
            pos = data.rfind(_EOCD_SIG)
            if pos < 0:
                raise Exception("not a zip file")
            eocd = data[pos:pos + 22]

        # Parse EOCD
        (total_disk, total_entries, cd_size, cd_offset, comment_len
         ) = struct.unpack('<HHIIH', eocd[8:22])

        # Read central directory entries
        fp.seek(cd_offset)
        for _ in range(total_entries):
            hdr = fp.read(46)
            if len(hdr) < 46 or hdr[:4] != _CD_SIG:
                break

            (ver_made, ver_need, flags, method,
             mod_time, mod_date, crc, comp_size, uncomp_size,
             name_len, extra_len, comment_len,
             disk_num, int_attr, ext_attr,
             local_offset) = struct.unpack(
                '<HHHHHHIIIHHHHHII', hdr[4:46])

            name = fp.read(name_len).decode('utf-8')
            skip = extra_len + comment_len
            if skip > 0:
                fp.read(skip)

            info = ZipInfo(name)
            info.compress_type = method
            info.CRC = crc
            info.compress_size = comp_size
            info.file_size = uncomp_size
            info._header_offset = local_offset
            info.external_attr = ext_attr
            info.date_time = (
                ((mod_date >> 9) & 0x7F) + 1980,
                (mod_date >> 5) & 0x0F,
                mod_date & 0x1F,
                (mod_time >> 11) & 0x1F,
                (mod_time >> 5) & 0x3F,
                (mod_time & 0x1F) * 2,
            )

            self._filelist.append(info)
            self._name_index[name] = info

    def namelist(self):
        """Return list of archive member names."""
        result = []
        for info in self._filelist:
            result.append(info.filename)
        return result

    def infolist(self):
        """Return list of ZipInfo objects for archive members."""
        return list(self._filelist)

    def getinfo(self, name):
        """Return ZipInfo for a named member."""
        try:
            return self._name_index[name]
        except KeyError:
            raise KeyError("no item named '%s'" % name)

    def read(self, name):
        """Read and return bytes of a named member."""
        if self.mode != 'r':
            raise Exception("read() requires mode 'r'")
        info = self.getinfo(name) if isinstance(name, str) else name

        fp = self._fp
        fp.seek(info._header_offset)
        local = fp.read(30)
        if local[:4] != _LOCAL_SIG:
            raise Exception("bad local file header")
        name_len, extra_len = struct.unpack('<HH', local[26:30])

        fp.seek(info._header_offset + 30 + name_len + extra_len)
        raw = fp.read(info.compress_size)

        if info.compress_type == ZIP_STORED:
            data = raw
        elif info.compress_type == ZIP_DEFLATED:
            data = zlib.decompress(raw, -15)
        else:
            raise Exception("unsupported compression method %d" % info.compress_type)

        if zlib.crc32(data) != info.CRC:
            raise Exception("CRC mismatch for '%s'" % info.filename)

        return data

    def extract(self, member, path='.'):
        """Extract a member to a directory (default: current dir)."""
        if isinstance(member, str):
            info = self.getinfo(member)
        else:
            info = member

        target = path + '/' + info.filename

        parts = target.split('/')
        for i in range(1, len(parts)):
            dirpath = '/'.join(parts[:i])
            if dirpath:
                try:
                    os.mkdir(dirpath)
                except OSError:
                    pass

        if info.filename.endswith('/'):
            return target

        data = self.read(info)
        with open(target, 'wb') as f:
            f.write(data)
        return target

    def extractall(self, path='.'):
        """Extract all members to a directory."""
        for info in self._filelist:
            self.extract(info, path)

    def _dos_datetime(self, dt):
        """Encode date_time tuple to DOS time and date words."""
        mod_time = (dt[3] << 11) | (dt[4] << 5) | (dt[5] // 2)
        mod_date = ((dt[0] - 1980) << 9) | (dt[1] << 5) | dt[2]
        return mod_time, mod_date

    def write(self, filename, arcname=None):
        """Add a file from the filesystem to the archive."""
        if self.mode != 'w':
            raise Exception("write() requires mode 'w'")
        if arcname is None:
            arcname = filename
        with open(filename, 'rb') as f:
            data = f.read()
        info = ZipInfo(arcname)
        info.compress_type = self._compression
        self.writestr(info, data)

    def writestr(self, zinfo_or_name, data):
        """Write data directly to the archive under the given name."""
        if self.mode != 'w':
            raise Exception("writestr() requires mode 'w'")
        if isinstance(zinfo_or_name, str):
            info = ZipInfo(zinfo_or_name)
            info.compress_type = self._compression
        else:
            info = zinfo_or_name

        if isinstance(data, str):
            data = data.encode('utf-8')

        info.file_size = len(data)
        info.CRC = zlib.crc32(data)

        if info.compress_type == ZIP_DEFLATED:
            out = io.BytesIO()
            with deflate.DeflateIO(out, deflate.RAW) as d:
                d.write(data)
            compressed = out.getvalue()
            info.compress_size = len(compressed)
        else:
            info.compress_type = ZIP_STORED
            compressed = data
            info.compress_size = len(data)

        info._header_offset = self._fp.tell()

        name_bytes = info.filename.encode('utf-8')
        mod_time, mod_date = self._dos_datetime(info.date_time)

        header = struct.pack('<4sHHHHHIIIHH',
            _LOCAL_SIG,
            20,
            0,
            info.compress_type,
            mod_time,
            mod_date,
            info.CRC,
            info.compress_size,
            info.file_size,
            len(name_bytes),
            0)
        self._fp.write(header)
        self._fp.write(name_bytes)
        self._fp.write(compressed)

        self._filelist.append(info)
        self._name_index[info.filename] = info

    def _write_central_dir(self):
        """Write central directory and EOCD at the end of the archive."""
        fp = self._fp
        cd_offset = fp.tell()

        for info in self._filelist:
            name_bytes = info.filename.encode('utf-8')
            mod_time, mod_date = self._dos_datetime(info.date_time)

            cd = struct.pack('<4sHHHHHHIIIHHHHHII',
                _CD_SIG,
                20,
                20,
                0,
                info.compress_type,
                mod_time,
                mod_date,
                info.CRC,
                info.compress_size,
                info.file_size,
                len(name_bytes),
                0,
                0,
                0,
                0,
                info.external_attr,
                info._header_offset)
            fp.write(cd)
            fp.write(name_bytes)

        cd_size = fp.tell() - cd_offset

        eocd = struct.pack('<4sHHHHIIH',
            _EOCD_SIG,
            0,
            0,
            len(self._filelist),
            len(self._filelist),
            cd_size,
            cd_offset,
            0)
        fp.write(eocd)

    def close(self):
        """Close the archive, writing central directory if in write mode."""
        if self.closed:
            return
        if self.mode == 'w':
            self._write_central_dir()
        self.closed = True
        if self._own_fp:
            self._fp.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
