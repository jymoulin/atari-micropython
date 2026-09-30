"""CPython-compatible zlib module using deflate.DeflateIO and native _zlib.crc32."""

import io
import deflate
from _zlib import crc32

# Compression levels (MicroPython's deflate ignores the level,
# but we define them for API compatibility)
Z_NO_COMPRESSION = 0
Z_BEST_SPEED = 1
Z_BEST_COMPRESSION = 9
Z_DEFAULT_COMPRESSION = -1

# Wbits constants
DEF_WBITS = 15
MAX_WBITS = 15


def compress(data, level=-1):
    """Compress data and return bytes with zlib header/trailer."""
    out = io.BytesIO()
    with deflate.DeflateIO(out, deflate.ZLIB, DEF_WBITS) as d:
        d.write(data)
    return out.getvalue()


def decompress(data, wbits=DEF_WBITS):
    """Decompress data from zlib, raw deflate, or gzip format.

    wbits controls the format:
      8..15  = zlib format (default)
      -8..-15 = raw deflate (no header)
      24..31 = gzip format (wbits + 16)
    """
    if wbits < 0:
        fmt = deflate.RAW
        wbits = -wbits
    elif wbits > 15:
        fmt = deflate.GZIP
        wbits = wbits - 16
    else:
        fmt = deflate.ZLIB

    inp = io.BytesIO(data)
    with deflate.DeflateIO(inp, fmt, wbits) as d:
        return d.read()
