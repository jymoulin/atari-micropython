// socket module over STinG (TCP/IP stack for TOS, cookie 'STiK') or
// MiNTnet (the BSD sockets of FreeMiNT, through the MiNTLib).
//
// The stack is chosen once: MiNTnet when the 'MiNT' cookie is there and
// the kernel opens an AF_INET socket (inet4.xdd loaded), else STinG.
//
// MiNTnet: descriptors are always non-blocking; blocking calls and
// timeouts are done here with poll(), in short slices so that Ctrl-C keeps
// working. MiNTLib errno values are what MP_Exxx use, no translation.
//
// STinG publishes a table of functions (TPL) behind its cookie. They follow
// the Pure C convention: arguments on the stack in their natural size (a
// 16-bit int takes one word), result in d0, d0-d2/a0-a1 clobbered. gcc
// pushes every int as 32 bits, so the calls go through a small trampoline,
// sting_call(), which copies a ready-made block of words onto the stack.
// The TPL() macro builds that block: W() for a 16-bit argument, L() for a
// 32-bit one (address, IP, pointer).
//
// STinG has no BSD sockets, and the mapping has a few corners:
//   - one handle is one connection. A passive TCP_open waits for one peer
//     and then BECOMES that connection. accept() hands the handle to the new
//     socket and opens a fresh passive one on the same port for the next
//     client. STinG-LW v13 keeps listening meanwhile and queues the clients
//     that arrive; before v13 the reopening fails while the previous
//     connection still uses the port, and accept() keeps retrying.
//   - a UDP handle locks onto the first peer that talks to it, and sends to
//     one peer only: sendto() to another address reopens the handle (same
//     local port).
//   - IP addresses are 32-bit values in network order, which on m68k is
//     simply the natural order.
// Blocking calls poll STinG (which runs on its own, from interrupts) and
// keep Ctrl-C working; settimeout() and setblocking() behave as in CPython.
// Every open handle is noted, and atari_socket_shutdown() closes what a
// script left open: STinG would otherwise keep it after we exit.

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "py/runtime.h"
#include "py/stream.h"
#include "py/mperrno.h"
#include "py/mphal.h"

#if MICROPY_PY_SOCKET

int atari_get_cookie(const char *name, uint32_t *val);

// ---------------------------------------------------------------------------
// Calling STinG
// ---------------------------------------------------------------------------

// long sting_call(void *fn, const void *args, unsigned long nbytes)
// Copies nbytes (even) of arguments onto the stack, calls fn, returns d0.
// Saves every register gcc expects preserved, whatever the callee does.
long atari_sting_call(void *fn, const void *args, unsigned long nbytes);
__asm__ (
    "    .text\n"
    "    .even\n"
    "    .globl  atari_sting_call\n"
    "atari_sting_call:\n"
    "    link    %a6,#0\n"
    "    movem.l %d2-%d7/%a2-%a5,-(%sp)\n"
    "    move.l  8(%a6),%a0\n"
    "    move.l  12(%a6),%a1\n"
    "    move.l  16(%a6),%d0\n"
    "    sub.l   %d0,%sp\n"
    "    move.l  %sp,%a2\n"
    "    lsr.l   #1,%d0\n"
    "    bra.s   2f\n"
    "1:  move.w  (%a1)+,(%a2)+\n"
    "2:  dbra    %d0,1b\n"
    "    jsr     (%a0)\n"
    "    lea     -40(%a6),%sp\n"
    "    movem.l (%sp)+,%d2-%d7/%a2-%a5\n"
    "    unlk    %a6\n"
    "    rts\n"
    );

#define W(x) ((uint16_t)(x))
#define L(x) ((uint16_t)((uint32_t)(x) >> 16)), ((uint16_t)(uint32_t)(x))
#define TPL(idx, ...) ({ \
        const uint16_t _a[] = { __VA_ARGS__ }; \
        atari_sting_call(sting_tpl->fn[idx], _a, sizeof(_a)); \
    })

// Positions in the TPL function table (transprt.h, struct tpl).
enum {
    KRmalloc, KRfree, KRgetfree, KRrealloc, get_err_text, getvstr,
    carrier_detect, TCP_open, TCP_close, TCP_send, TCP_wait_state,
    TCP_ack_wait, UDP_open, UDP_close, UDP_send, CNkick, CNbyte_count,
    CNget_char, CNget_NDB, CNget_block, housekeep, resolve, ser_disable,
    ser_enable, set_flag, clear_flag, CNgetinfo, on_port, off_port, setvstr,
    query_port, CNgets, ICMP_send, ICMP_handler, ICMP_discard, TCP_info,
    cntrl_port, UDP_info, RAW_open, RAW_close, RAW_out, CN_setopt, CN_getopt,
    TPL_COUNT
};

typedef struct {
    const char *module, *author, *version;
    void *fn[TPL_COUNT];
} tpl_t;

typedef struct {
    char magic[10];             // "STiKmagic"
    void *get_dftab;            // TPL *get_dftab(const char *name)
    void *etm_exec;
    void *cfg;
    void *basepage;
} __attribute__((packed)) drv_list_t;

// Structures shared with STinG: 16-bit alignment, as Pure C lays them out.
typedef struct {
    uint16_t lport, rport;
    uint32_t rhost, lhost;
} __attribute__((packed)) cab_t;

typedef struct {
    uint16_t protocol;
    cab_t address;
    uint16_t status;
} __attribute__((packed)) cib_t;

typedef struct {
    char *ptr;                  // block to KRfree
    char *ndata;
    uint16_t len;
    void *next;
} __attribute__((packed)) ndb_t;

typedef struct {
    uint32_t request;
    uint16_t state;
    uint32_t unacked;
    uint32_t srtt;
} __attribute__((packed)) tcpib_t;

#define TCP_ACTIVE      0x0000
#define TCP_PASSIVE     0xffff
#define UDP_EXTEND      0x0000
#define TCPI_STATE      1
#define TLISTEN         1
#define TESTABLISH      4
#define TCLOSE_WAIT     7

#define E_NORMAL        0
#define E_OBUFFULL      (-1)
#define E_NODATA        (-2)
#define E_EOF           (-3)
#define E_RRESET        (-4)
#define E_UA            (-5)
#define E_NOMEM         (-6)
#define E_REFUSE        (-7)
#define E_BADHANDLE     (-9)
#define E_LISTEN        (-10)
#define E_NOCCB         (-11)
#define E_CONNECTFAIL   (-13)
#define E_USERTIMEOUT   (-15)
#define E_CNTIMEOUT     (-16)
#define E_UNREACHABLE   (-24)
#define E_NOROUTINE     (-26)
#define E_PARAMETER     (-30)

#define CNO_TCP_NODELAY 0x5401  // CN_setopt, STinG-LW v13

#define TCP_BUFFER      8192    // advisory buffer size for the old TCP.STX
// Before v13 STinG has no TCP_NODELAY: a segment shorter than the MSS waits
// (Nagle) for the previous ones to be acknowledged, and the peer delays its
// ACKs. So big writes go out in whole segments.
#define STING_MSS       1460
#define SEND_CHUNK      (4 * STING_MSS)

static tpl_t *sting_tpl;

static tpl_t *sting_find(void) {
    if (sting_tpl == NULL) {
        uint32_t v;
        if (atari_get_cookie("STiK", &v) && v != 0) {
            drv_list_t *drv = (drv_list_t *)v;
            if (memcmp(drv->magic, "STiKmagic", 10) == 0) {
                static const char name[] = "TRANSPORT_TCPIP";
                const uint16_t a[] = { L(name) };
                sting_tpl = (tpl_t *)atari_sting_call(drv->get_dftab, a, sizeof(a));
            }
        }
    }
    return sting_tpl;
}

// ---------------------------------------------------------------------------
// Which stack
// ---------------------------------------------------------------------------

enum { NET_NONE, NET_MINT, NET_STING };
static int8_t net_stack = -1;

static int net_find(void) {
    if (net_stack <= NET_NONE) {
        net_stack = NET_NONE;
        uint32_t v;
        if (atari_get_cookie("MiNT", &v)) {
            int fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (fd >= 0) {
                close(fd);
                net_stack = NET_MINT;
            }
        }
        if (net_stack == NET_NONE && sting_find() != NULL) {
            net_stack = NET_STING;
        }
    }
    return net_stack;
}

static int net_need(void) {
    int n = net_find();
    if (n == NET_NONE) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no TCP/IP stack (STinG or MiNTnet)"));
    }
    return n;
}

static int errno_of(int e) {
    switch (e) {
        case E_OBUFFULL:
        case E_NODATA: return MP_EAGAIN;
        case E_EOF: return MP_ENOTCONN;
        case E_RRESET:
        case E_UA: return MP_ECONNRESET;
        case E_NOMEM:
        case E_NOCCB: return MP_ENOMEM;
        case E_REFUSE:
        case E_CONNECTFAIL: return MP_ECONNREFUSED;
        case E_BADHANDLE: return MP_EBADF;
        case E_LISTEN: return MP_ENOTCONN;
        case E_USERTIMEOUT:
        case E_CNTIMEOUT: return MP_ETIMEDOUT;
        case E_UNREACHABLE: return MP_EHOSTUNREACH;
        case E_PARAMETER: return MP_EINVAL;
    }
    return 0;
}

// Raise OSError(errno) when there is a matching errno, else OSError with
// STinG's own message (DNS failures and the like).
static MP_NORETURN void sting_raise(int e) {
    int err = errno_of(e);
    if (err) {
        mp_raise_OSError(err);
    }
    const char *text = (const char *)TPL(get_err_text, W(e));
    mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("STinG: %s"), text ? text : "?");
}

// ---------------------------------------------------------------------------
// Open handles, to close them at exit
// ---------------------------------------------------------------------------

#define MAX_OPEN 32
static int16_t open_handle[MAX_OPEN];
static uint8_t open_tcp[MAX_OPEN];
static uint8_t open_count;

static void note_open(int16_t h, bool tcp) {
    if (open_count < MAX_OPEN) {
        open_handle[open_count] = h;
        open_tcp[open_count] = tcp;
        open_count++;
    }
}

static void note_close(int16_t h) {
    for (int i = 0; i < open_count; i++) {
        if (open_handle[i] == h) {
            open_count--;
            open_handle[i] = open_handle[open_count];
            open_tcp[i] = open_tcp[open_count];
            return;
        }
    }
}

static void handle_close(int16_t h, bool tcp) {
    if (tcp) {
        TPL(TCP_close, W(h), W(0), L(0));
    } else {
        TPL(UDP_close, W(h));
    }
    note_close(h);
}

void atari_socket_shutdown(void) {
    while (open_count > 0) {
        handle_close(open_handle[0], open_tcp[0]);
    }
}

// ---------------------------------------------------------------------------
// Addresses
// ---------------------------------------------------------------------------

static bool parse_ip(const char *s, uint32_t *ip) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        unsigned n = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + (*s++ - '0');
            if (n > 255) {
                return false;
            }
        }
        v = (v << 8) | n;
        if (i < 3 && *s++ != '.') {
            return false;
        }
    }
    if (*s != 0) {
        return false;
    }
    *ip = v;
    return true;
}

static mp_obj_t ip_str(uint32_t ip) {
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (unsigned)(ip >> 24),
        (unsigned)(ip >> 16) & 255, (unsigned)(ip >> 8) & 255, (unsigned)ip & 255);
    return mp_obj_new_str(buf, n);
}

static mp_obj_t addr_tuple(uint32_t ip, uint16_t port) {
    mp_obj_t t[2] = { ip_str(ip), MP_OBJ_NEW_SMALL_INT(port) };
    return mp_obj_new_tuple(2, t);
}

// Name or dotted quad -> addresses. Returns how many (at least 1), raises
// if the name does not resolve.
static int host_lookup(const char *host, uint32_t *list, int max) {
    if (parse_ip(host, list)) {
        return 1;
    }
    if (strcmp(host, "localhost") == 0) {
        list[0] = 0x7f000001;
        return 1;
    }
    if (net_need() == NET_MINT) {
        // MiNTLib resolver: /etc/hosts, then the servers of /etc/resolv.conf
        struct hostent *he = gethostbyname(host);
        if (he == NULL || he->h_addrtype != AF_INET || he->h_addr_list[0] == NULL) {
            mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("MiNTnet: can't resolve %s"), host);
        }
        int n = 0;
        while (n < max && he->h_addr_list[n] != NULL) {
            memcpy(&list[n], he->h_addr_list[n], 4);
            n++;
        }
        return n;
    }
    int16_t n = (int16_t)TPL(resolve, L(host), L(0), L(list), W(max));
    if (n <= 0) {
        sting_raise(n < 0 ? n : -17);   // E_CANTRESOLVE
    }
    return n;
}

// ('host', port) -> ip and port. An empty host is 0.0.0.0 (for bind).
static void parse_addr(mp_obj_t addr, uint32_t *ip, uint16_t *port) {
    mp_obj_t *items;
    mp_obj_get_array_fixed_n(addr, 2, &items);
    const char *host = mp_obj_str_get_str(items[0]);
    *port = (uint16_t)mp_obj_get_int(items[1]);
    if (*host == 0) {
        *ip = 0;
    } else {
        host_lookup(host, ip, 1);
    }
}

// ---------------------------------------------------------------------------
// The socket object
// ---------------------------------------------------------------------------

#define MOD_AF_INET     2
#define MOD_SOCK_STREAM 1
#define MOD_SOCK_DGRAM  2

typedef struct {
    mp_obj_base_t base;
    int16_t handle;             // STinG handle or MiNT descriptor, -1 if none
    uint8_t type;               // MOD_SOCK_STREAM or MOD_SOCK_DGRAM
    bool mint;                  // MiNTnet socket, else STinG
    bool nodelay;               // STinG: TCP_NODELAY asked for
    bool listening;
    bool connecting;            // non-blocking connect in progress
    bool closed;
    uint16_t lport;             // bound local port (0 = any)
    uint32_t lhost;
    uint32_t rhost;             // UDP: peer the handle is opened towards
    uint16_t rport;
    int32_t timeout_ms;         // -1 = blocking
} socket_obj_t;

static const mp_obj_type_t socket_type;

#define IS_TCP(s) ((s)->type == MOD_SOCK_STREAM)

// Between two polls: Ctrl-C and the scheduler, then the timeout.
// Returns 0 to poll again, else MP_EAGAIN or MP_ETIMEDOUT.
static int sock_wait(socket_obj_t *s, mp_uint_t t0) {
    if (s->timeout_ms == 0) {
        return MP_EAGAIN;
    }
    if (s->timeout_ms > 0 && mp_hal_ticks_ms() - t0 >= (mp_uint_t)s->timeout_ms) {
        return MP_ETIMEDOUT;
    }
    mp_event_handle_nowait();
    return 0;
}

static int tcp_state(int16_t h) {
    tcpib_t ib;
    ib.request = TCPI_STATE;
    int16_t r = (int16_t)TPL(TCP_info, W(h), L(&ib));
    return r < 0 ? r : ib.state;
}

static cib_t *handle_info(int16_t h) {
    return (cib_t *)TPL(CNgetinfo, W(h));
}

static void sock_check_open(socket_obj_t *s) {
    if (s->closed) {
        mp_raise_OSError(MP_EBADF);
    }
}

// --- MiNTnet helpers

#define MINT_SLICE_MS   20      // poll() slice between two Ctrl-C checks

// Wait until the descriptor has one of the poll() events, within the
// socket's timeout. Returns 0 when ready, else MP_EAGAIN, MP_ETIMEDOUT or
// the errno of poll().
static int mint_wait(socket_obj_t *s, short events) {
    if (s->timeout_ms == 0) {
        return MP_EAGAIN;
    }
    mp_uint_t t0 = mp_hal_ticks_ms();
    for (;;) {
        long slice = MINT_SLICE_MS;
        if (s->timeout_ms > 0) {
            mp_uint_t el = mp_hal_ticks_ms() - t0;
            if (el >= (mp_uint_t)s->timeout_ms) {
                return MP_ETIMEDOUT;
            }
            if ((mp_uint_t)s->timeout_ms - el < (mp_uint_t)slice) {
                slice = s->timeout_ms - el;
            }
        }
        struct pollfd p = { s->handle, events, 0 };
        int r = poll(&p, 1, slice);
        if (r > 0) {
            return 0;
        }
        if (r < 0 && errno != EINTR) {
            return errno;
        }
        mp_event_handle_nowait();
    }
}

static void mint_sockaddr(struct sockaddr_in *sa, uint32_t ip, uint16_t port) {
    memset(sa, 0, sizeof(*sa));
    sa->sin_family = AF_INET;
    sa->sin_port = htons(port);
    sa->sin_addr.s_addr = htonl(ip);
}

static mp_obj_t mint_addr_tuple(const struct sockaddr_in *sa) {
    return addr_tuple(ntohl(sa->sin_addr.s_addr), ntohs(sa->sin_port));
}

static void mint_nonblock(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

// Bytes waiting on a descriptor that poll() found readable; 0 if the kernel
// cannot tell (end of stream, or no FIONREAD).
static mp_uint_t mint_pending(int fd) {
    long n = 0;
    if (ioctl(fd, FIONREAD, &n) < 0 || n < 0) {
        return 0;
    }
    return n;
}

static mp_uint_t mint_read(socket_obj_t *s, void *buf, mp_uint_t len, struct sockaddr_in *from, int *errcode) {
    for (;;) {
        socklen_t n = sizeof(*from);
        long r = from ? recvfrom(s->handle, buf, len, 0, (struct sockaddr *)from, &n)
            : recv(s->handle, buf, len, 0);
        if (r >= 0) {
            return r;
        }
        int e = errno;
        if (e == EAGAIN) {
            e = mint_wait(s, POLLIN);
        }
        if (e) {
            *errcode = e;
            return MP_STREAM_ERROR;
        }
    }
}

static mp_uint_t mint_write(socket_obj_t *s, const void *buf, mp_uint_t len, const struct sockaddr_in *to, int *errcode) {
    for (;;) {
        long r = to ? sendto(s->handle, buf, len, 0, (const struct sockaddr *)to, sizeof(*to))
            : send(s->handle, buf, len, 0);
        if (r >= 0) {
            return r;
        }
        int e = errno;
        if (e == EAGAIN) {
            e = mint_wait(s, POLLOUT);
        }
        if (e) {
            *errcode = e;
            return MP_STREAM_ERROR;
        }
    }
}

static MP_NORETURN void mint_raise(void) {
    mp_raise_OSError(errno);
}

static mp_obj_t socket_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 3, false);
    int stack = net_need();
    int af = n_args > 0 ? mp_obj_get_int(args[0]) : MOD_AF_INET;
    int st = n_args > 1 ? mp_obj_get_int(args[1]) : MOD_SOCK_STREAM;
    if (af != MOD_AF_INET || (st != MOD_SOCK_STREAM && st != MOD_SOCK_DGRAM)) {
        mp_raise_OSError(MP_EOPNOTSUPP);
    }
    socket_obj_t *s = mp_obj_malloc_with_finaliser(socket_obj_t, type);
    s->handle = -1;
    s->type = st;
    s->mint = stack == NET_MINT;
    s->nodelay = false;
    if (s->mint) {
        int fd = socket(AF_INET, st == MOD_SOCK_STREAM ? SOCK_STREAM : SOCK_DGRAM, 0);
        if (fd < 0) {
            mint_raise();
        }
        mint_nonblock(fd);
        s->handle = fd;
    }
    s->listening = false;
    s->connecting = false;
    s->closed = false;
    s->lport = 0;
    s->lhost = 0;
    s->rhost = 0;
    s->rport = 0;
    s->timeout_ms = -1;
    return MP_OBJ_FROM_PTR(s);
}

static void sock_close(socket_obj_t *s) {
    if (s->handle >= 0) {
        if (s->mint) {
            close(s->handle);
        } else {
            handle_close(s->handle, IS_TCP(s));
        }
        s->handle = -1;
    }
    s->closed = true;
}

// STinG-LW v13: TCP_NODELAY on a handle (on a listening one, it goes to the
// connection accepted). Older kernels answer E_NOROUTINE: ignored.
static void sting_apply_nodelay(socket_obj_t *s) {
    if (s->nodelay && s->handle >= 0 && sting_tpl->fn[CN_setopt] != NULL) {
        int16_t one = 1;
        TPL(CN_setopt, W(s->handle), W(CNO_TCP_NODELAY), L(&one), W(sizeof(one)));
    }
}

// --- UDP: a handle towards (rhost, rport), or open to anyone if rhost is 0.
static void udp_open(socket_obj_t *s, uint32_t rhost, uint16_t rport) {
    if (s->handle >= 0) {
        if (rhost == 0 || (rhost == s->rhost && rport == s->rport)) {
            return;
        }
        handle_close(s->handle, false);     // another peer: reopen
        s->handle = -1;
    }
    cab_t cab = { s->lport, rport, rhost, s->lhost };
    int16_t h = (int16_t)TPL(UDP_open, L(&cab), W(UDP_EXTEND));
    if (h < 0) {
        if (h == E_PARAMETER) {
            mp_raise_OSError(MP_EADDRINUSE);
        }
        sting_raise(h);
    }
    note_open(h, false);
    s->handle = h;
    s->rhost = rhost;
    s->rport = rport;
    s->lport = handle_info(h)->address.lport;   // keep it across reopenings
}

// --- TCP: (re)open the passive handle of a listening socket.
static int16_t tcp_listen_open(socket_obj_t *s) {
    cab_t cab = { s->lport, 0, 0, s->lhost };
    int16_t h = (int16_t)TPL(TCP_open, L(&cab), W(TCP_PASSIVE), W(0), W(TCP_BUFFER));
    if (h >= 0) {
        note_open(h, true);
        s->handle = h;
        s->lport = handle_info(h)->address.lport;
        sting_apply_nodelay(s);
    }
    return h;
}

static mp_obj_t socket_bind(mp_obj_t self_in, mp_obj_t addr) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    sock_check_open(s);
    parse_addr(addr, &s->lhost, &s->lport);
    if (s->mint) {
        struct sockaddr_in sa;
        mint_sockaddr(&sa, s->lhost, s->lport);
        if (bind(s->handle, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
            mint_raise();
        }
        return mp_const_none;
    }
    if (!IS_TCP(s)) {
        udp_open(s, 0, 0);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_bind_obj, socket_bind);

static mp_obj_t socket_listen(size_t n_args, const mp_obj_t *args) {
    socket_obj_t *s = MP_OBJ_TO_PTR(args[0]);
    sock_check_open(s);
    if (s->mint) {
        if (listen(s->handle, n_args > 1 ? mp_obj_get_int(args[1]) : 4) < 0) {
            mint_raise();
        }
        s->listening = true;
        return mp_const_none;
    }
    if (!IS_TCP(s) || s->handle >= 0) {
        mp_raise_OSError(MP_EINVAL);
    }
    int16_t h = tcp_listen_open(s);
    if (h < 0) {
        if (h == E_PARAMETER) {
            mp_raise_OSError(MP_EADDRINUSE);
        }
        sting_raise(h);
    }
    s->listening = true;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_listen_obj, 1, 2, socket_listen);

static mp_obj_t socket_accept(mp_obj_t self_in) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    sock_check_open(s);
    if (!s->listening) {
        mp_raise_OSError(MP_EINVAL);
    }
    if (s->mint) {
        socket_obj_t *c = mp_obj_malloc_with_finaliser(socket_obj_t, &socket_type);
        *c = *s;
        c->handle = -1;
        c->listening = false;
        c->timeout_ms = -1;
        struct sockaddr_in sa;
        for (;;) {
            socklen_t n = sizeof(sa);
            int fd = accept(s->handle, (struct sockaddr *)&sa, &n);
            if (fd >= 0) {
                mint_nonblock(fd);
                c->handle = fd;
                break;
            }
            int e = errno;
            if (e == EAGAIN) {
                e = mint_wait(s, POLLIN);
            }
            if (e) {
                mp_raise_OSError(e);
            }
        }
        mp_obj_t t[2] = { MP_OBJ_FROM_PTR(c), mint_addr_tuple(&sa) };
        return mp_obj_new_tuple(2, t);
    }
    mp_uint_t t0 = mp_hal_ticks_ms();
    for (;;) {
        if (s->handle < 0) {
            // the previous client may still hold the port
            int16_t h = tcp_listen_open(s);
            if (h < 0 && h != E_PARAMETER) {
                sting_raise(h);
            }
        }
        if (s->handle >= 0) {
            int st = tcp_state(s->handle);
            if (st < 0) {
                handle_close(s->handle, true);
                s->handle = -1;
                sting_raise(st);
            }
            if (st != TLISTEN) {
                break;
            }
        }
        int w = sock_wait(s, t0);
        if (w) {
            mp_raise_OSError(w);
        }
    }
    // The passive handle has become the connection: it changes owner.
    socket_obj_t *c = mp_obj_malloc_with_finaliser(socket_obj_t, &socket_type);
    *c = *s;
    c->listening = false;
    c->timeout_ms = -1;
    s->handle = -1;
    tcp_listen_open(s);         // ready for the next one, if the port allows
    cib_t *ci = handle_info(c->handle);
    mp_obj_t t[2] = { MP_OBJ_FROM_PTR(c), addr_tuple(ci->address.rhost, ci->address.rport) };
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(socket_accept_obj, socket_accept);

static mp_obj_t socket_connect(mp_obj_t self_in, mp_obj_t addr) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    sock_check_open(s);
    uint32_t rhost;
    uint16_t rport;
    parse_addr(addr, &rhost, &rport);
    if (s->mint) {
        struct sockaddr_in sa;
        mint_sockaddr(&sa, rhost, rport);
        if (connect(s->handle, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
            return mp_const_none;
        }
        int e = errno;
        if (e != EINPROGRESS && e != EAGAIN) {
            mp_raise_OSError(e);
        }
        e = mint_wait(s, POLLOUT);
        if (e) {
            mp_raise_OSError(e == MP_EAGAIN ? MP_EINPROGRESS : e);
        }
        int err = 0;
        socklen_t n = sizeof(err);
        if (getsockopt(s->handle, SOL_SOCKET, SO_ERROR, &err, &n) < 0) {
            err = errno;
        }
        if (err) {
            mp_raise_OSError(err);
        }
        return mp_const_none;
    }
    if (!IS_TCP(s)) {
        udp_open(s, rhost, rport);
        return mp_const_none;
    }
    if (s->handle >= 0) {
        mp_raise_OSError(s->connecting ? MP_EALREADY : MP_EINVAL);
    }
    cab_t cab = { s->lport, rport, rhost, s->lhost };
    int16_t h = (int16_t)TPL(TCP_open, L(&cab), W(TCP_ACTIVE), W(0), W(TCP_BUFFER));
    if (h < 0) {
        sting_raise(h);
    }
    note_open(h, true);
    s->handle = h;
    sting_apply_nodelay(s);
    mp_uint_t t0 = mp_hal_ticks_ms();
    for (;;) {
        int st = tcp_state(h);
        if (st == TESTABLISH || st == TCLOSE_WAIT) {
            s->connecting = false;
            return mp_const_none;
        }
        int w = st < 0 || st == 0 ? -1 : sock_wait(s, t0);
        if (w == MP_EAGAIN) {
            s->connecting = true;
            mp_raise_OSError(MP_EINPROGRESS);
        }
        if (w) {
            handle_close(h, true);
            s->handle = -1;
            if (w == MP_ETIMEDOUT) {
                mp_raise_OSError(MP_ETIMEDOUT);
            }
            sting_raise(st < 0 ? st : E_REFUSE);
        }
    }
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_connect_obj, socket_connect);

// --- Data. Return a count, or MP_STREAM_ERROR with *errcode set.

static mp_uint_t tcp_send(socket_obj_t *s, const uint8_t *buf, mp_uint_t len, int *errcode) {
    mp_uint_t done = 0, chunk = SEND_CHUNK;
    mp_uint_t t0 = mp_hal_ticks_ms();
    while (done < len) {
        mp_uint_t n = len - done < chunk ? len - done : chunk;
        if (n < len - done && n > STING_MSS) {
            n -= n % STING_MSS;   // full segments, or Nagle holds the tail
        }
        int16_t r = (int16_t)TPL(TCP_send, W(s->handle), L(buf + done), W(n));
        if (r == E_NORMAL) {
            done += n;
            continue;
        }
        if (r == E_OBUFFULL && n > STING_MSS) {
            chunk = n / 2;      // less room than that: try smaller
            continue;
        }
        if (r != E_OBUFFULL && r != E_LISTEN) {
            if (done) {
                return done;
            }
            *errcode = r == E_EOF ? MP_EPIPE : errno_of(r) ? errno_of(r) : MP_EIO;
            return MP_STREAM_ERROR;
        }
        int w = sock_wait(s, t0);
        if (w) {
            if (done) {
                return done;
            }
            *errcode = w;
            return MP_STREAM_ERROR;
        }
        chunk = SEND_CHUNK;
    }
    return done;
}

// Wait until a TCP connection has data: returns how much (> 0), 0 at end
// of stream, or MP_STREAM_ERROR with *errcode set.
static mp_uint_t tcp_wait_data(socket_obj_t *s, int *errcode) {
    mp_uint_t t0 = mp_hal_ticks_ms();
    for (;;) {
        int16_t c = (int16_t)TPL(CNbyte_count, W(s->handle));
        if (c > 0) {
            return c;
        }
        if (c == E_EOF) {
            return 0;
        }
        if (c < 0 && c != E_NODATA && c != E_LISTEN) {
            *errcode = errno_of(c) ? errno_of(c) : MP_EIO;
            return MP_STREAM_ERROR;
        }
        int w = sock_wait(s, t0);
        if (w) {
            *errcode = w;
            return MP_STREAM_ERROR;
        }
    }
}

static mp_uint_t tcp_recv(socket_obj_t *s, uint8_t *buf, mp_uint_t len, int *errcode) {
    mp_uint_t c = tcp_wait_data(s, errcode);
    if (c == 0 || c == MP_STREAM_ERROR) {
        return c;
    }
    int16_t n = c < len ? c : len > 32767 ? 32767 : len;
    int16_t r = (int16_t)TPL(CNget_block, W(s->handle), L(buf), W(n));
    if (r < 0) {
        *errcode = errno_of(r) ? errno_of(r) : MP_EIO;
        return MP_STREAM_ERROR;
    }
    return r;
}

static mp_uint_t udp_recv(socket_obj_t *s, uint8_t *buf, mp_uint_t len, int *errcode) {
    mp_uint_t t0 = mp_hal_ticks_ms();
    for (;;) {
        int16_t c = (int16_t)TPL(CNbyte_count, W(s->handle));
        if (c > 0) {
            ndb_t *ndb = (ndb_t *)TPL(CNget_NDB, W(s->handle));
            if (ndb != NULL) {
                mp_uint_t n = ndb->len < len ? ndb->len : len;
                memcpy(buf, ndb->ndata, n);
                TPL(KRfree, L(ndb->ptr));
                TPL(KRfree, L(ndb));
                return n;
            }
        } else if (c < 0 && c != E_NODATA && c != E_LISTEN) {
            *errcode = errno_of(c) ? errno_of(c) : MP_EIO;
            return MP_STREAM_ERROR;
        }
        int w = sock_wait(s, t0);
        if (w) {
            *errcode = w;
            return MP_STREAM_ERROR;
        }
    }
}

static mp_uint_t sock_read(mp_obj_t self_in, void *buf, mp_uint_t len, int *errcode) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    if (s->closed || s->handle < 0 || s->listening) {
        *errcode = s->closed ? MP_EBADF : MP_ENOTCONN;
        return MP_STREAM_ERROR;
    }
    if (s->mint) {
        return mint_read(s, buf, len, NULL, errcode);
    }
    return IS_TCP(s) ? tcp_recv(s, buf, len, errcode) : udp_recv(s, buf, len, errcode);
}

static mp_uint_t sock_write(mp_obj_t self_in, const void *buf, mp_uint_t len, int *errcode) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    if (s->closed || s->handle < 0 || s->listening) {
        *errcode = s->closed ? MP_EBADF : MP_ENOTCONN;
        return MP_STREAM_ERROR;
    }
    if (s->mint) {
        return mint_write(s, buf, len, NULL, errcode);
    }
    if (IS_TCP(s)) {
        return tcp_send(s, buf, len, errcode);
    }
    if (len > 32767) {
        *errcode = MP_EINVAL;
        return MP_STREAM_ERROR;
    }
    int16_t r = (int16_t)TPL(UDP_send, W(s->handle), L(buf), W(len));
    if (r < 0) {
        *errcode = errno_of(r) ? errno_of(r) : MP_EIO;
        return MP_STREAM_ERROR;
    }
    return len;
}

static mp_uint_t sock_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    if (request == MP_STREAM_CLOSE) {
        sock_close(s);
        return 0;
    }
    if (request == MP_STREAM_POLL) {
        if (s->closed) {
            return MP_STREAM_POLL_NVAL;
        }
        mp_uint_t ret = 0;
        if (s->handle < 0) {
            return 0;
        }
        if (s->mint) {
            struct pollfd p = { s->handle, 0, 0 };
            if (arg & MP_STREAM_POLL_RD) {
                p.events |= POLLIN;
            }
            if (arg & MP_STREAM_POLL_WR) {
                p.events |= POLLOUT;
            }
            if (poll(&p, 1, 0) > 0) {
                ret = (p.revents & POLLIN ? MP_STREAM_POLL_RD : 0)
                    | (p.revents & POLLOUT ? MP_STREAM_POLL_WR : 0)
                    | (p.revents & POLLHUP ? MP_STREAM_POLL_HUP : 0)
                    | (p.revents & POLLERR ? MP_STREAM_POLL_ERR : 0)
                    | (p.revents & POLLNVAL ? MP_STREAM_POLL_NVAL : 0);
            }
            return ret;
        }
        if (s->listening) {
            int st = tcp_state(s->handle);
            if ((arg & MP_STREAM_POLL_RD) && st != TLISTEN) {
                ret |= MP_STREAM_POLL_RD;
            }
            return ret;
        }
        int16_t c = (int16_t)TPL(CNbyte_count, W(s->handle));
        if (IS_TCP(s)) {
            int st = tcp_state(s->handle);
            if ((arg & MP_STREAM_POLL_RD) && (c > 0 || c == E_EOF)) {
                ret |= MP_STREAM_POLL_RD;
            }
            if ((arg & MP_STREAM_POLL_WR) && (st == TESTABLISH || st == TCLOSE_WAIT)) {
                ret |= MP_STREAM_POLL_WR;
            }
            if (c == E_EOF) {
                ret |= MP_STREAM_POLL_HUP;
            } else if (c < 0 && c != E_NODATA && c != E_LISTEN) {
                ret |= MP_STREAM_POLL_ERR;
            }
        } else {
            if ((arg & MP_STREAM_POLL_RD) && c > 0) {
                ret |= MP_STREAM_POLL_RD;
            }
            ret |= arg & MP_STREAM_POLL_WR;
        }
        return ret;
    }
    *errcode = MP_EINVAL;
    return MP_STREAM_ERROR;
}

static mp_obj_t socket_send(mp_obj_t self_in, mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    int err;
    mp_uint_t n = sock_write(self_in, bufinfo.buf, bufinfo.len, &err);
    if (n == MP_STREAM_ERROR) {
        mp_raise_OSError(err);
    }
    return mp_obj_new_int_from_uint(n);
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_send_obj, socket_send);

static mp_obj_t socket_sendall(mp_obj_t self_in, mp_obj_t buf_in) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    const uint8_t *p = bufinfo.buf;
    mp_uint_t left = bufinfo.len;
    while (left > 0) {
        int err;
        mp_uint_t n = sock_write(self_in, p, left, &err);
        if (n == MP_STREAM_ERROR) {
            if (err == MP_EAGAIN && s->timeout_ms != 0) {
                continue;
            }
            mp_raise_OSError(err);
        }
        p += n;
        left -= n;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_sendall_obj, socket_sendall);

static mp_obj_t socket_recv(mp_obj_t self_in, mp_obj_t len_in) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_int_t len = mp_obj_get_int(len_in);
    if (s->mint && IS_TCP(s) && !s->closed && s->handle >= 0 && !s->listening) {
        struct pollfd p = { s->handle, POLLIN, 0 };
        if (poll(&p, 1, 0) <= 0) {
            int e = mint_wait(s, POLLIN);
            if (e) {
                mp_raise_OSError(e);
            }
        }
        mp_uint_t c = mint_pending(s->handle);
        if (c > 0 && c < (mp_uint_t)len) {
            len = c;
        }
    } else if (IS_TCP(s) && !s->mint && !s->closed && s->handle >= 0 && !s->listening) {
        // Allocate only what has arrived: recv(8192) for a few bytes would
        // otherwise feed the garbage collector, slow on a 68000.
        int err;
        mp_uint_t c = tcp_wait_data(s, &err);
        if (c == MP_STREAM_ERROR) {
            mp_raise_OSError(err);
        }
        if (c < (mp_uint_t)len) {
            len = c;
        }
    }
    vstr_t vstr;
    vstr_init_len(&vstr, len);
    int err;
    mp_uint_t n = sock_read(self_in, vstr.buf, len, &err);
    if (n == MP_STREAM_ERROR) {
        vstr_clear(&vstr);
        mp_raise_OSError(err);
    }
    vstr.len = n;
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_recv_obj, socket_recv);

static mp_obj_t socket_sendto(mp_obj_t self_in, mp_obj_t buf_in, mp_obj_t addr) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    sock_check_open(s);
    if (IS_TCP(s)) {
        return socket_send(self_in, buf_in);
    }
    uint32_t rhost;
    uint16_t rport;
    parse_addr(addr, &rhost, &rport);
    if (s->mint) {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
        struct sockaddr_in sa;
        mint_sockaddr(&sa, rhost, rport);
        int err;
        mp_uint_t n = mint_write(s, bufinfo.buf, bufinfo.len, &sa, &err);
        if (n == MP_STREAM_ERROR) {
            mp_raise_OSError(err);
        }
        return mp_obj_new_int_from_uint(n);
    }
    udp_open(s, rhost, rport);
    return socket_send(self_in, buf_in);
}
static MP_DEFINE_CONST_FUN_OBJ_3(socket_sendto_obj, socket_sendto);

static mp_obj_t socket_getpeername(mp_obj_t self_in);

static mp_obj_t socket_recvfrom(mp_obj_t self_in, mp_obj_t len_in) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_obj_t t[2];
    if (s->mint) {
        sock_check_open(s);
        mp_int_t len = mp_obj_get_int(len_in);
        vstr_t vstr;
        vstr_init_len(&vstr, len);
        struct sockaddr_in sa;
        int err;
        mp_uint_t n = mint_read(s, vstr.buf, len, &sa, &err);
        if (n == MP_STREAM_ERROR) {
            vstr_clear(&vstr);
            mp_raise_OSError(err);
        }
        vstr.len = n;
        t[0] = mp_obj_new_bytes_from_vstr(&vstr);
        t[1] = IS_TCP(s) ? socket_getpeername(self_in) : mint_addr_tuple(&sa);
        return mp_obj_new_tuple(2, t);
    }
    t[0] = socket_recv(self_in, len_in);
    cib_t *ci = handle_info(s->handle);
    t[1] = addr_tuple(ci->address.rhost, ci->address.rport);
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_recvfrom_obj, socket_recvfrom);

static mp_obj_t socket_settimeout(mp_obj_t self_in, mp_obj_t t_in) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    if (t_in == mp_const_none) {
        s->timeout_ms = -1;
    } else {
        #if MICROPY_PY_BUILTINS_FLOAT
        s->timeout_ms = (int32_t)(mp_obj_get_float(t_in) * 1000);
        #else
        s->timeout_ms = mp_obj_get_int(t_in) * 1000;
        #endif
        if (s->timeout_ms < 0) {
            mp_raise_ValueError(NULL);
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_settimeout_obj, socket_settimeout);

static mp_obj_t socket_setblocking(mp_obj_t self_in, mp_obj_t flag) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    s->timeout_ms = mp_obj_is_true(flag) ? -1 : 0;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_setblocking_obj, socket_setblocking);

// Option numbers of this module (those of MicroPython's lwIP ports).
#define MOD_SOL_SOCKET      1
#define MOD_SO_REUSEADDR    4
#define MOD_SO_BROADCAST    0x20
#define MOD_IPPROTO_TCP     6
#define MOD_TCP_NODELAY     1

// MiNTnet: translated and passed on. STinG: only TCP_NODELAY means something
// (STinG-LW v13), the others are accepted and ignored.
static mp_obj_t socket_setsockopt(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    socket_obj_t *s = MP_OBJ_TO_PTR(args[0]);
    sock_check_open(s);
    mp_int_t level = mp_obj_get_int(args[1]);
    mp_int_t opt = mp_obj_get_int(args[2]);
    bool nodelay = level == MOD_IPPROTO_TCP && opt == MOD_TCP_NODELAY;
    if (!s->mint) {
        if (nodelay && IS_TCP(s)) {
            s->nodelay = mp_obj_is_true(args[3]);
            sting_apply_nodelay(s);
        }
        return mp_const_none;
    }
    int val;
    const void *ptr = &val;
    socklen_t len = sizeof(val);
    if (mp_obj_is_int(args[3])) {
        val = mp_obj_get_int(args[3]);
    } else {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args[3], &bufinfo, MP_BUFFER_READ);
        ptr = bufinfo.buf;
        len = bufinfo.len;
    }
    if (level == MOD_SOL_SOCKET) {
        level = SOL_SOCKET;
        opt = opt == MOD_SO_REUSEADDR ? SO_REUSEADDR
            : opt == MOD_SO_BROADCAST ? SO_BROADCAST : opt;
    } else if (nodelay) {
        level = IPPROTO_TCP;
        opt = TCP_NODELAY;
    }
    if (setsockopt(s->handle, level, opt, ptr, len) < 0) {
        mint_raise();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_setsockopt_obj, 4, 4, socket_setsockopt);

static mp_obj_t socket_makefile(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_makefile_obj, 1, 3, socket_makefile);

static mp_obj_t socket_getsockname(mp_obj_t self_in) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    if (s->mint && s->handle >= 0) {
        struct sockaddr_in sa;
        socklen_t n = sizeof(sa);
        if (getsockname(s->handle, (struct sockaddr *)&sa, &n) < 0) {
            mint_raise();
        }
        return mint_addr_tuple(&sa);
    }
    if (s->handle >= 0) {
        cib_t *ci = handle_info(s->handle);
        return addr_tuple(ci->address.lhost, ci->address.lport);
    }
    return addr_tuple(s->lhost, s->lport);
}
static MP_DEFINE_CONST_FUN_OBJ_1(socket_getsockname_obj, socket_getsockname);

static mp_obj_t socket_getpeername(mp_obj_t self_in) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    if (s->handle < 0 || s->listening) {
        mp_raise_OSError(MP_ENOTCONN);
    }
    if (s->mint) {
        struct sockaddr_in sa;
        socklen_t n = sizeof(sa);
        if (getpeername(s->handle, (struct sockaddr *)&sa, &n) < 0) {
            mint_raise();
        }
        return mint_addr_tuple(&sa);
    }
    cib_t *ci = handle_info(s->handle);
    return addr_tuple(ci->address.rhost, ci->address.rport);
}
static MP_DEFINE_CONST_FUN_OBJ_1(socket_getpeername_obj, socket_getpeername);

static mp_obj_t socket_close_meth(mp_obj_t self_in) {
    sock_close(MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(socket_close_obj, socket_close_meth);

static void socket_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<socket %s %s=%d>", IS_TCP(s) ? "tcp" : "udp", s->mint ? "fd" : "handle", s->handle);
}

static const mp_rom_map_elem_t socket_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&socket_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&mp_stream___exit___obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&socket_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind), MP_ROM_PTR(&socket_bind_obj) },
    { MP_ROM_QSTR(MP_QSTR_listen), MP_ROM_PTR(&socket_listen_obj) },
    { MP_ROM_QSTR(MP_QSTR_accept), MP_ROM_PTR(&socket_accept_obj) },
    { MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&socket_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_send), MP_ROM_PTR(&socket_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_sendall), MP_ROM_PTR(&socket_sendall_obj) },
    { MP_ROM_QSTR(MP_QSTR_recv), MP_ROM_PTR(&socket_recv_obj) },
    { MP_ROM_QSTR(MP_QSTR_sendto), MP_ROM_PTR(&socket_sendto_obj) },
    { MP_ROM_QSTR(MP_QSTR_recvfrom), MP_ROM_PTR(&socket_recvfrom_obj) },
    { MP_ROM_QSTR(MP_QSTR_settimeout), MP_ROM_PTR(&socket_settimeout_obj) },
    { MP_ROM_QSTR(MP_QSTR_setblocking), MP_ROM_PTR(&socket_setblocking_obj) },
    { MP_ROM_QSTR(MP_QSTR_setsockopt), MP_ROM_PTR(&socket_setsockopt_obj) },
    { MP_ROM_QSTR(MP_QSTR_makefile), MP_ROM_PTR(&socket_makefile_obj) },
    { MP_ROM_QSTR(MP_QSTR_getsockname), MP_ROM_PTR(&socket_getsockname_obj) },
    { MP_ROM_QSTR(MP_QSTR_getpeername), MP_ROM_PTR(&socket_getpeername_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
};
static MP_DEFINE_CONST_DICT(socket_locals_dict, socket_locals_dict_table);

static const mp_stream_p_t socket_stream_p = {
    .read = sock_read,
    .write = sock_write,
    .ioctl = sock_ioctl,
};

static MP_DEFINE_CONST_OBJ_TYPE(
    socket_type,
    MP_QSTR_socket,
    MP_TYPE_FLAG_NONE,
    make_new, socket_make_new,
    print, socket_print,
    protocol, &socket_stream_p,
    locals_dict, &socket_locals_dict
    );

// ---------------------------------------------------------------------------
// Module functions
// ---------------------------------------------------------------------------

// getaddrinfo(host, port, af=0, type=0, proto=0, flags=0)
static mp_obj_t mod_getaddrinfo(size_t n_args, const mp_obj_t *args) {
    const char *host = mp_obj_str_get_str(args[0]);
    mp_int_t port = mp_obj_get_int(args[1]);
    mp_int_t type = n_args > 3 ? mp_obj_get_int(args[3]) : 0;
    uint32_t list[8];
    int n = host_lookup(host, list, 8);
    mp_obj_t res = mp_obj_new_list(0, NULL);
    for (int i = 0; i < n; i++) {
        mp_obj_t t[5] = {
            MP_OBJ_NEW_SMALL_INT(MOD_AF_INET),
            MP_OBJ_NEW_SMALL_INT(type ? type : MOD_SOCK_STREAM),
            MP_OBJ_NEW_SMALL_INT(0),
            MP_OBJ_NEW_QSTR(MP_QSTR_),
            addr_tuple(list[i], (uint16_t)port),
        };
        mp_obj_list_append(res, mp_obj_new_tuple(5, t));
    }
    return res;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_getaddrinfo_obj, 2, 6, mod_getaddrinfo);

// inet_pton(AF_INET, '1.2.3.4') -> b'\x01\x02\x03\x04'
static mp_obj_t mod_inet_pton(mp_obj_t af_in, mp_obj_t s_in) {
    uint32_t ip;
    if (mp_obj_get_int(af_in) != MOD_AF_INET || !parse_ip(mp_obj_str_get_str(s_in), &ip)) {
        mp_raise_OSError(MP_EINVAL);
    }
    return mp_obj_new_bytes((const byte *)&ip, 4);
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_inet_pton_obj, mod_inet_pton);

static mp_obj_t mod_inet_ntop(mp_obj_t af_in, mp_obj_t b_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(b_in, &bufinfo, MP_BUFFER_READ);
    if (mp_obj_get_int(af_in) != MOD_AF_INET || bufinfo.len != 4) {
        mp_raise_OSError(MP_EINVAL);
    }
    const uint8_t *b = bufinfo.buf;
    return ip_str(((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | (b[2] << 8) | b[3]);
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_inet_ntop_obj, mod_inet_ntop);

// sting() -> (module, author, version) of the loaded STinG, or None.
static mp_obj_t mod_sting(void) {
    tpl_t *t = sting_find();
    if (t == NULL) {
        return mp_const_none;
    }
    mp_obj_t v[3] = {
        mp_obj_new_str(t->module, strlen(t->module)),
        mp_obj_new_str(t->author, strlen(t->author)),
        mp_obj_new_str(t->version, strlen(t->version)),
    };
    return mp_obj_new_tuple(3, v);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_sting_obj, mod_sting);

// stack() -> 'MiNTnet', 'STinG' or None: the stack the sockets go through.
static mp_obj_t mod_stack(void) {
    int n = net_find();
    return n == NET_MINT ? MP_OBJ_NEW_QSTR(MP_QSTR_MiNTnet)
         : n == NET_STING ? MP_OBJ_NEW_QSTR(MP_QSTR_STinG) : mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_stack_obj, mod_stack);

static const mp_rom_map_elem_t mp_module_socket_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_socket) },
    { MP_ROM_QSTR(MP_QSTR_socket), MP_ROM_PTR(&socket_type) },
    { MP_ROM_QSTR(MP_QSTR_getaddrinfo), MP_ROM_PTR(&mod_getaddrinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_inet_pton), MP_ROM_PTR(&mod_inet_pton_obj) },
    { MP_ROM_QSTR(MP_QSTR_inet_ntop), MP_ROM_PTR(&mod_inet_ntop_obj) },
    { MP_ROM_QSTR(MP_QSTR_sting), MP_ROM_PTR(&mod_sting_obj) },
    { MP_ROM_QSTR(MP_QSTR_stack), MP_ROM_PTR(&mod_stack_obj) },
    { MP_ROM_QSTR(MP_QSTR_AF_INET), MP_ROM_INT(MOD_AF_INET) },
    { MP_ROM_QSTR(MP_QSTR_SOCK_STREAM), MP_ROM_INT(MOD_SOCK_STREAM) },
    { MP_ROM_QSTR(MP_QSTR_SOCK_DGRAM), MP_ROM_INT(MOD_SOCK_DGRAM) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_TCP), MP_ROM_INT(MOD_IPPROTO_TCP) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_UDP), MP_ROM_INT(17) },
    { MP_ROM_QSTR(MP_QSTR_SOL_SOCKET), MP_ROM_INT(MOD_SOL_SOCKET) },
    { MP_ROM_QSTR(MP_QSTR_SO_REUSEADDR), MP_ROM_INT(MOD_SO_REUSEADDR) },
    { MP_ROM_QSTR(MP_QSTR_SO_BROADCAST), MP_ROM_INT(MOD_SO_BROADCAST) },
    { MP_ROM_QSTR(MP_QSTR_TCP_NODELAY), MP_ROM_INT(MOD_TCP_NODELAY) },
};
static MP_DEFINE_CONST_DICT(mp_module_socket_globals, mp_module_socket_globals_table);

const mp_obj_module_t mp_module_socket = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_socket_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_socket, mp_module_socket);

#endif // MICROPY_PY_SOCKET
