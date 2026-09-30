# urequests — HTTP/1.1 client for MicroPython AmigaOS port
# Supports GET, POST, PUT, DELETE, HEAD
# Supports HTTP and HTTPS (via AmiSSL)
# Handles chunked transfer encoding and gzip decompression

import socket


class Response:
    def __init__(self, f):
        self.raw = f
        self.encoding = "utf-8"
        self._cached = None
        self.status_code = None
        self.reason = None
        self.headers = {}
        self._buf = b""

    def _read_raw(self, size):
        """Read exact number of bytes from socket, using internal buffer."""
        if len(self._buf) >= size:
            result = self._buf[:size]
            self._buf = self._buf[size:]
            return result
        parts = [self._buf]
        have = len(self._buf)
        self._buf = b""
        while have < size:
            chunk = self.raw.recv(min(4096, size - have))
            if not chunk:
                break
            parts.append(chunk)
            have = have + len(chunk)
        data = b"".join(parts)
        if len(data) > size:
            self._buf = data[size:]
            data = data[:size]
        return data

    def _read_line(self):
        """Read a line ending with \\n, buffered."""
        while b"\n" not in self._buf:
            chunk = self.raw.recv(4096)
            if not chunk:
                line = self._buf
                self._buf = b""
                return line
            self._buf = self._buf + chunk
        idx = self._buf.index(b"\n") + 1
        line = self._buf[:idx]
        self._buf = self._buf[idx:]
        return line

    def _read_headers(self):
        while True:
            line = self._read_line()
            if not line or line == b"\r\n":
                break
            if b":" in line:
                k, v = line.split(b":", 1)
                self.headers[k.decode().lower()] = v.strip().decode()

    def _read_chunked(self):
        """Read chunked transfer encoding using list accumulation."""
        chunks = []
        while True:
            line = self._read_line()
            if not line:
                break
            size_str = line.strip()
            if b";" in size_str:
                size_str = size_str.split(b";")[0]
            if not size_str:
                break
            chunk_size = int(size_str, 16)
            if chunk_size == 0:
                self._read_line()
                break
            chunk = self._read_raw(chunk_size)
            chunks.append(chunk)
            self._read_line()
        return b"".join(chunks)

    @property
    def text(self):
        try:
            return self.content.decode(self.encoding)
        except (UnicodeError, ValueError):
            # MicroPython only supports utf-8 decode.
            # Replace non-ASCII bytes with '?' to make valid utf-8.
            b = self.content
            arr = bytearray(len(b))
            for i in range(len(b)):
                arr[i] = b[i] if b[i] < 128 else 63  # 63 = '?'
            return arr.decode()

    @property
    def content(self):
        if self._cached is None:
            te = self.headers.get("transfer-encoding", "")
            if "chunked" in te:
                raw_data = self._read_chunked()
            else:
                cl = self.headers.get("content-length")
                if cl:
                    raw_data = self._read_raw(int(cl))
                else:
                    chunks = []
                    while True:
                        chunk = self.raw.recv(4096)
                        if not chunk:
                            break
                        chunks.append(chunk)
                    raw_data = b"".join(chunks)
            if self.raw:
                self.raw.close()
                self.raw = None
            # Decompress if gzip/deflate encoded
            ce = self.headers.get("content-encoding", "")
            if "gzip" in ce or "deflate" in ce:
                import deflate, io
                raw_data = deflate.DeflateIO(io.BytesIO(raw_data), deflate.GZIP).read()
            self._cached = raw_data
        return self._cached

    def json(self):
        import json
        return json.loads(self.text)

    def close(self):
        if self.raw:
            self.raw.close()
            self.raw = None


def request(method, url, data=None, json_data=None, headers=None):
    # Parse URL
    use_ssl = False
    if url.startswith("http://"):
        url = url[7:]
        port = 80
    elif url.startswith("https://"):
        url = url[8:]
        port = 443
        use_ssl = True
    else:
        raise ValueError("Unsupported protocol")

    # Separate host and path
    slash = url.find("/")
    if slash < 0:
        host = url
        path = "/"
    else:
        host = url[:slash]
        path = url[slash:]

    # Separate host and port
    if ":" in host:
        host, port = host.split(":", 1)
        port = int(port)

    # DNS + connect
    addr = socket.getaddrinfo(host, port)[0][-1]
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(addr)

    # Wrap with TLS if HTTPS
    if use_ssl:
        import ssl
        s = ssl.wrap_socket(s, server_hostname=host)

    # Build request (HTTP/1.1 with Connection: close)
    s.send(b"%s %s HTTP/1.1\r\n" % (method, path))
    s.send(b"Host: %s\r\n" % host)
    s.send(b"Connection: close\r\n")
    s.send(b"Accept-Encoding: gzip\r\n")
    s.send(b"User-Agent: MicroPython-Amiga/1.27\r\n")

    if headers:
        for k in headers:
            s.send(b"%s: %s\r\n" % (k, headers[k]))

    if json_data is not None:
        import json
        data = json.dumps(json_data)
        s.send(b"Content-Type: application/json\r\n")

    if data:
        if isinstance(data, str):
            data = data.encode()
        s.send(b"Content-Length: %d\r\n" % len(data))

    s.send(b"\r\n")

    if data:
        s.send(data)

    # Parse response
    resp = Response(s)
    line = resp._read_line()
    # HTTP/1.1 200 OK\r\n
    parts = line.split(None, 2)
    resp.status_code = int(parts[1])
    resp.reason = parts[2].strip().decode() if len(parts) > 2 else ""
    resp._read_headers()

    return resp


def get(url, **kw):
    return request(b"GET", url, **kw)


def post(url, **kw):
    return request(b"POST", url, **kw)


def put(url, **kw):
    return request(b"PUT", url, **kw)


def delete(url, **kw):
    return request(b"DELETE", url, **kw)


def head(url, **kw):
    return request(b"HEAD", url, **kw)
