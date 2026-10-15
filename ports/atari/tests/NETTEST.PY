# The socket module, on top of STinG (TOS) or MiNTnet (FreeMiNT).
#
#   MPY20.TTP NETTEST.PY          loopback 127.0.0.1: no network
#   MPY20.TTP NETTEST.PY net      plus DNS and HTTP to the outside
#   MPY20.TTP NETTEST.PY net example.com    another HTTP server
#
# Without a TCP/IP stack: the test says so and stops (nothing to check).
# Server and client are in this same program: the stack runs its
# connections by itself (interrupts, kernel), we simply alternate the calls.

import sys
import socket
import time

try:
    import select
except ImportError:
    select = None

args = sys.argv[1:]
net = "net" in args
hote = [a for a in args if a != "net"]
hote = hote[0] if hote else "example.com"

ok = ko = 0
LOCAL = "127.0.0.1"
PORT = 7777


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


def erreur(f):
    # the code (errno) or message of the OSError raised by f(), else None
    try:
        f()
    except OSError as e:
        return e.args[0]
    return None


def recoit(s, n):
    # exactly n bytes (or fewer if the connection closes)
    morceaux = []
    while n > 0:
        b = s.recv(n)
        if not b:
            break
        morceaux.append(b)
        n -= len(b)
    return b"".join(morceaux)


pile = socket.stack()
if pile is None:
    print("ni STinG ni MiNTnet : rien a tester")
    print("BILAN : OK")
    print("FIN DU TEST")
    raise SystemExit

print("pile :", pile, socket.sting() or "")

section("adresses")
check("getaddrinfo litteral", socket.getaddrinfo(LOCAL, 80)[0][-1], (LOCAL, 80))
check("getaddrinfo localhost", socket.getaddrinfo("localhost", 21)[0][-1], (LOCAL, 21))
check("inet_pton", socket.inet_pton(socket.AF_INET, "10.1.2.3"), b"\x0a\x01\x02\x03")
check("inet_ntop", socket.inet_ntop(socket.AF_INET, b"\xc0\xa8\x17\xca"), "192.168.23.202")

section("TCP en boucle locale")
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((LOCAL, PORT))
srv.listen(2)
check("getsockname du serveur", srv.getsockname()[1], PORT)

cli = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
t0 = time.ticks_ms()
cli.connect((LOCAL, PORT))
print("   connexion en %d ms" % time.ticks_diff(time.ticks_ms(), t0))
conn, adr = srv.accept()
check("accept : adresse du client", adr[0], LOCAL)
check("getpeername = getsockname", cli.getsockname()[1], adr[1])
check("getpeername du client", cli.getpeername(), (LOCAL, PORT))

cli.send(b"bonjour")
check("recv cote serveur", recoit(conn, 7), b"bonjour")
conn.sendall(b"salut, client")
check("recv cote client", recoit(cli, 13), b"salut, client")

# a big block: alternate non-blocking send and read
N = 60000
data = (bytes(range(256)) * (N // 256 + 1))[:N]   # fast, even on a 68000
mv = memoryview(data)            # zero-copy slices: less GC
cli.setblocking(False)
recu = []
nrecu = envoye = 0
t0 = time.ticks_ms()
while nrecu < N:
    if envoye < N:
        try:
            envoye += cli.send(mv[envoye:envoye + 8192])
        except OSError:
            pass                       # buffer full: read first
    conn.settimeout(2)
    b = conn.recv(4096)
    recu.append(b)
    nrecu += len(b)
dt = time.ticks_diff(time.ticks_ms(), t0)
check("60000 octets intacts", b"".join(recu) == data, True)
print("   debit en boucle locale : %d Ko/s" % (N * 1000 // 1024 // max(dt, 1)))
cli.setblocking(True)
conn.settimeout(None)

if select:
    p = select.poll()
    p.register(conn, select.POLLIN)
    check("poll : rien a lire", p.poll(0), [])
    cli.write(b"x")
    r = p.poll(1000)
    check("poll : donnee arrivee", len(r) == 1 and r[0][1] & select.POLLIN != 0, True)
    conn.read(1)

cli.write(b"ligne 1\nligne 2\n")
f = conn.makefile("rb")
check("readline 1", f.readline(), b"ligne 1\n")
check("readline 2", f.readline(), b"ligne 2\n")

# write-write-read: with Nagle, the second byte waits for the (delayed) ACK
# of the first one. STinG before v13 ignores TCP_NODELAY.
cli.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
t0 = time.ticks_ms()
for i in range(10):
    cli.send(b"a")
    cli.send(b"b")
    recoit(conn, 2)
    conn.send(b"c")
    recoit(cli, 1)
print("   10 x (ecrire, ecrire, lire) avec TCP_NODELAY : %d ms" % time.ticks_diff(time.ticks_ms(), t0))
check("TCP_NODELAY accepte", True, True)

conn.settimeout(0.5)
t0 = time.ticks_ms()
e = erreur(lambda: conn.recv(10))
dt = time.ticks_diff(time.ticks_ms(), t0)
import errno
check("recv : delai depasse", e, errno.ETIMEDOUT)
check("delai respecte (~500 ms)", 400 <= dt <= 1500, True)
conn.setblocking(False)
check("recv non bloquant", erreur(lambda: conn.recv(10)), errno.EAGAIN)
conn.setblocking(True)

cli.close()
check("fin de connexion : recv -> b''", conn.recv(10), b"")
conn.close()
check("recv apres close", erreur(lambda: conn.recv(1)), errno.EBADF)

section("deuxieme client sur le meme port")
srv.settimeout(3)
cli2 = None
for essai in range(20):
    # the server listens again once the port is free
    srv.setblocking(False)
    erreur(srv.accept)
    cli2 = socket.socket()
    if erreur(lambda: cli2.connect((LOCAL, PORT))) is None:
        break
    cli2.close()
    time.sleep_ms(100)
srv.settimeout(3)
conn2, adr2 = srv.accept()
cli2.send(b"encore")
check("deuxieme connexion", recoit(conn2, 6), b"encore")
conn2.close()
cli2.close()

section("deux clients avant accept")
# BSD sockets have a backlog; STinG-LW keeps listening from v13 on.
c1 = socket.socket()
c2 = socket.socket()
c1.settimeout(3)
c2.settimeout(3)
e = erreur(lambda: c1.connect((LOCAL, PORT))) or erreur(lambda: c2.connect((LOCAL, PORT)))
if e is None:
    a1, _ = srv.accept()
    a2, _ = srv.accept()
    c1.send(b"un")
    c2.send(b"deux")
    check("deux clients en file", (recoit(a1, 2), recoit(a2, 4)), (b"un", b"deux"))
    a1.close()
    a2.close()
elif pile == "STinG":
    print("   --    deux clients en file : non (STinG avant v13 ?)", e)
else:
    check("deux clients en file", e, None)
c1.close()
c2.close()
srv.close()

with socket.socket() as s:
    check("connexion refusee", erreur(lambda: s.connect((LOCAL, PORT + 1))), errno.ECONNREFUSED)

section("UDP en boucle locale")
u1 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
u1.bind((LOCAL, PORT + 2))
u2 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
u2.sendto(b"ping", (LOCAL, PORT + 2))
u1.settimeout(2)
d, a = u1.recvfrom(100)
check("datagramme recu", d, b"ping")
u1.sendto(b"pong!", a)
u2.settimeout(2)
check("reponse", u2.recv(100), b"pong!")
u2.sendto(b"abcdefgh", (LOCAL, PORT + 2))
check("datagramme tronque a 3", u1.recv(3), b"abc")
u1.close()
u2.close()

try:
    import requests
except ImportError:
    requests = None
check("module requests (gele)", requests is not None, True)

if net:
    print("--- reseau : DNS et HTTP vers", hote, "---")
    try:
        t0 = time.ticks_ms()
        ai = socket.getaddrinfo(hote, 80)
        print("   %s -> %s en %d ms" % (hote, ai[0][-1][0], time.ticks_diff(time.ticks_ms(), t0)))
        check("DNS", len(ai) > 0, True)
        s = socket.socket()
        s.settimeout(10)
        s.connect(ai[0][-1])
        s.sendall(b"GET / HTTP/1.0\r\nHost: " + hote.encode() + b"\r\nConnection: close\r\n\r\n")
        t0 = time.ticks_ms()
        rep = []
        while True:
            b = s.recv(2048)
            if not b:
                break
            rep.append(b)
        s.close()
        rep = b"".join(rep)
        dt = time.ticks_diff(time.ticks_ms(), t0)
        print("   " + rep.split(b"\r\n")[0].decode())
        print("   %d octets en %d ms" % (len(rep), dt))
        check("reponse HTTP", rep[:5], b"HTTP/")
        if requests:
            r = requests.get("http://" + hote + "/")
            print("   requests.get : %d, %d octets" % (r.status_code, len(r.content)))
            check("requests.get", r.status_code, 200)
            r.close()
    except OSError as e:
        ko += 1
        print("   ECHEC reseau :", e)

section("fin")
print("BILAN : %d reussites, %d echecs" % (ok, ko))
print("FIN DU TEST")
