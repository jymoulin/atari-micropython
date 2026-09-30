# gzip — CPython-compatible gzip API wrapping MicroPython's deflate module

import deflate
import io


def decompress(data):
    """Decompress gzip-compressed bytes. Returns decompressed bytes."""
    return deflate.DeflateIO(io.BytesIO(data), deflate.GZIP).read()


def compress(data, compresslevel=6):
    """Compress bytes using gzip. Returns compressed bytes."""
    out = io.BytesIO()
    with deflate.DeflateIO(out, deflate.GZIP) as f:
        f.write(data)
    return out.getvalue()
