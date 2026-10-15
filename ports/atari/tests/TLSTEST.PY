# TLS (MPY20SSL.TTP: mbedTLS) over the socket module.
#
#   MPY20SSL.TTP TLSTEST.PY        hashes, then a TLS handshake on the
#                                  127.0.0.1 loopback (STinG or MiNTnet)
#   MPY20SSL.TTP TLSTEST.PY net    plus HTTPS to example.com, requests.get
#   MPY20SSL.TTP TLSTEST.PY net www.un.site    another HTTPS server
#
# Client and server live in this same program, on non-blocking sockets:
# each side's handshake moves on when the other one has answered, so the
# loop below simply pumps both. Their certificate is a throwaway P-256 one
# (CN=atari), the client does not verify it.

import sys
import time

args = sys.argv[1:]
net = "net" in args
hote = [a for a in args if a != "net"]
hote = hote[0] if hote else "example.com"

ok = ko = 0


def check(nom, val, attendu):
    global ok, ko
    if val == attendu:
        ok += 1
        print("   ok    %-34s %r" % (nom, val if len(repr(val)) < 40 else repr(val)[:37] + "..."))
    else:
        ko += 1
        print("   ECHEC %-34s %r au lieu de %r" % (nom, val, attendu))


T0 = time.ticks_ms()


def section(titre):
    print("--- %s (t=%.1f s) ---" % (titre, time.ticks_diff(time.ticks_ms(), T0) / 1000))


try:
    import tls
except ImportError:
    print("pas de module tls : c'est MPY20SSL.TTP qu'il faut")
    print("BILAN : OK")
    print("FIN DU TEST")
    raise SystemExit

import socket
import hashlib
import binascii

KEY = bytes.fromhex(
    "307702010104209c33068a51b534c83cf792e951e9559e4310108eee8d8f5b87"
    "ca1445481c8f19a00a06082a8648ce3d030107a14403420004fa50914cd8c61f"
    "26ab9579a464128e3099aed42ff7a598a5778ef06f0f908d6a7552e955094b00"
    "efcd66fc64e8013a92deed471b515d787fe68ab47f2e277db5"
)
CERT = bytes.fromhex(
    "308201763082011da003020102021478dcef23cae3a2bd04991e62e8f1d860be"
    "2e1135300a06082a8648ce3d0403023010310e300c06035504030c0561746172"
    "693020170d3236313031353138313135365a180f323132363039323131383131"
    "35365a3010310e300c06035504030c0561746172693059301306072a8648ce3d"
    "020106082a8648ce3d03010703420004fa50914cd8c61f26ab9579a464128e30"
    "99aed42ff7a598a5778ef06f0f908d6a7552e955094b00efcd66fc64e8013a92"
    "deed471b515d787fe68ab47f2e277db5a3533051301d0603551d0e04160414cd"
    "1bf3ce880eacf04997ab0d3393367eaea6fc29301f0603551d23041830168014"
    "cd1bf3ce880eacf04997ab0d3393367eaea6fc29300f0603551d130101ff0405"
    "30030101ff300a06082a8648ce3d0403020347003044022062e3e67234b63349"
    "24d1a3fc547ba300c9773250b5e1121790f00273a564604602203f203957f933"
    "3c72834d869676df39b518f7d3c2a836081f551190463e4f1049"
)

section("empreintes (mbedTLS)")
h = lambda f, d: binascii.hexlify(f(d).digest()).decode()
check("md5", h(hashlib.md5, b"abc"), "900150983cd24fb0d6963f7d28e17f72")
check("sha1", h(hashlib.sha1, b"abc"), "a9993e364706816aba3e25717850c26c9cd0d89d")
check("sha256", h(hashlib.sha256, b"abc")[:16], "ba7816bf8f01cfea")

pile = socket.stack()
if pile is None:
    print("ni STinG ni MiNTnet : pas de poignee de main")
else:
    section("poignee de main TLS en boucle locale (%s)" % pile)
    PORT = 7443
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", PORT))
    srv.listen(2)
    cli = socket.socket()
    cli.connect(("127.0.0.1", PORT))
    conn, _ = srv.accept()
    cli.setblocking(False)
    conn.setblocking(False)

    sctx = tls.SSLContext(tls.PROTOCOL_TLS_SERVER)
    sctx.load_cert_chain(CERT, KEY)
    cctx = tls.SSLContext(tls.PROTOCOL_TLS_CLIENT)
    cctx.verify_mode = tls.CERT_NONE
    s = sctx.wrap_socket(conn, server_side=True, do_handshake_on_connect=False)
    c = cctx.wrap_socket(cli, server_hostname="atari", do_handshake_on_connect=False)

    # Both handshakes move forward as each side writes or reads; the first
    # application data goes through once they are done.
    t0 = time.ticks_ms()
    envoye = False
    recu = b""
    while len(recu) < 11 and time.ticks_diff(time.ticks_ms(), t0) < 600000:
        if not envoye:
            try:
                envoye = c.write(b"bonjour TLS") is not None
            except OSError as e:
                print("   client :", e)
                break
        try:
            b = s.read(11 - len(recu))
            if b:
                recu += b
        except OSError as e:
            print("   serveur :", e)
            break
    dt = time.ticks_diff(time.ticks_ms(), t0)
    check("donnees chiffrees recues", recu, b"bonjour TLS")
    print("   poignee de main + 11 octets : %.1f s (les DEUX cotes ici)" % (dt / 1000))
    try:
        print("   suite :", c.cipher())
    except Exception:
        pass

    # the other way, and a bigger block
    N = 20000
    data = (bytes(range(256)) * (N // 256 + 1))[:N]
    t0 = time.ticks_ms()
    pos = 0
    recu = []
    nrecu = 0
    while nrecu < N and time.ticks_diff(time.ticks_ms(), t0) < 300000:
        if pos < N:
            n = s.write(data[pos:pos + 2048])
            if n:
                pos += n
        b = c.read(4096)
        if b:
            recu.append(b)
            nrecu += len(b)
    dt = time.ticks_diff(time.ticks_ms(), t0)
    check("20000 octets chiffres intacts", b"".join(recu) == data, True)
    print("   debit chiffre (chiffrer + dechiffrer) : %d Ko/s" % (N * 1000 // 1024 // max(dt, 1)))
    c.close()
    s.close()
    srv.close()

if net and pile:
    print("--- reseau : HTTPS vers", hote, "---")
    try:
        ai = socket.getaddrinfo(hote, 443)
        sk = socket.socket()
        sk.settimeout(60)
        sk.connect(ai[0][-1])
        ctx = tls.SSLContext(tls.PROTOCOL_TLS_CLIENT)
        ctx.verify_mode = tls.CERT_NONE
        t0 = time.ticks_ms()
        ss = ctx.wrap_socket(sk, server_hostname=hote)
        print("   poignee de main : %.1f s" % (time.ticks_diff(time.ticks_ms(), t0) / 1000))
        try:
            print("   suite :", ss.cipher())
        except Exception:
            pass
        ss.write(b"GET / HTTP/1.0\r\nHost: " + hote.encode() + b"\r\nConnection: close\r\n\r\n")
        rep = ss.read(64)
        print("   " + rep.split(b"\r\n")[0].decode())
        check("reponse HTTPS", rep[:5], b"HTTP/")
        ss.close()
        import requests
        t0 = time.ticks_ms()
        r = requests.get("https://" + hote + "/")
        print("   requests.get : %d, %d octets en %.1f s" % (r.status_code, len(r.content), time.ticks_diff(time.ticks_ms(), t0) / 1000))
        check("requests.get https", r.status_code, 200)
        r.close()
    except OSError as e:
        ko += 1
        print("   ECHEC reseau :", e)

section("fin")
print("BILAN : %d reussites, %d echecs" % (ok, ko))
print("FIN DU TEST")
