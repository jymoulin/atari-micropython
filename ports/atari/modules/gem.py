# gem: draw with the VDI and talk to the AES, from Python.
#
#   import gem
#   with gem.Screen() as s:
#       s.clear()
#       s.color(fill=2)
#       s.fill_rect(10, 10, 200, 100)
#       s.text(20, 120, "Hello Atari")
#   choice = gem.alert("Save?", ("Yes", "No"))
#   name = gem.fileselect("*.PY")
#
# The C side (_gem) only provides the bare minimum: generic vdi() and aes(),
# and the calls that carry strings. Everything else is here, on top of
# _gem.vdi(), with the VDI opcode numbers spelled out.
#
# The AES must be running: a program launched from the desktop (or from a
# shell under the desktop) has it, a program in the AUTO folder does not.

import _gem
from _gem import vdi, aes

# graf_mouse
ARROW = 0
BUSY = 2
M_OFF = 256
M_ON = 257

# vswr_mode
REPLACE = 1
TRANSPARENT = 2
XOR = 3
REVERSE = 4

# vsf_interior
HOLLOW = 0
SOLID = 1
PATTERN = 2
HATCH = 3

# form_alert, icon
NOICON = 0
NOTE = 1
QUESTION = 2
STOP = 3

# evnt_multi
MU_KEYBD = 1
MU_BUTTON = 2
MU_MESAG = 16
MU_TIMER = 32

# wind_create: frame elements
NAME = 1
CLOSER = 2
FULLER = 4
MOVER = 8
INFO = 16
SIZER = 32

# AES messages
WM_REDRAW = 20
WM_TOPPED = 21
WM_CLOSED = 22
WM_FULLED = 23
WM_SIZED = 27
WM_MOVED = 28
WM_NEWTOP = 29
AP_TERM = 50

# wind_get / wind_set
WF_WORKXYWH = 4
WF_CURRXYWH = 5
WF_PREVXYWH = 6
WF_FULLXYWH = 7
WF_TOP = 10
WF_FIRSTXYWH = 11
WF_NEXTXYWH = 12

# menus: AES object types, flags and states, selection message
G_BOX = 20
G_IBOX = 25
G_STRING = 28
G_TITLE = 32
LASTOB = 0x20
SELECTED = 1
CHECKED = 4
DISABLED = 8
MN_SELECTED = 10

_apps = 0


def _app_enter():
    global _apps
    if _apps == 0:
        if _gem.appl_init() < 0:
            raise OSError("pas d'AES (programme lance depuis AUTO ?)")
    _apps += 1


def _app_leave():
    global _apps
    _apps -= 1
    if _apps == 0:
        _gem.appl_exit()


class Canvas:
    """The drawing primitives, on an open VDI workstation
    (self.handle). Shared by Screen and by the windows of an App."""

    # --- attributes --------------------------------------------------------

    def color(self, line=None, fill=None, text=None):
        if line is not None:
            vdi(17, self.handle, (line,))          # vsl_color
        if fill is not None:
            vdi(25, self.handle, (fill,))          # vsf_color
        if text is not None:
            vdi(22, self.handle, (text,))          # vst_color

    def mode(self, m):
        vdi(32, self.handle, (m,))                 # vswr_mode

    def fill_style(self, interior, style=1):
        vdi(23, self.handle, (interior,))          # vsf_interior
        vdi(24, self.handle, (style,))             # vsf_style

    def line_width(self, w):
        vdi(16, self.handle, ptsin=(w, 0))         # vsl_width (odd)

    def rgb(self, index, r, g, b):
        """Palette colour, components from 0 to 1000."""
        vdi(14, self.handle, (index, r, g, b))     # vs_color

    def clip(self, x1=None, y1=None, x2=None, y2=None):
        if x1 is None:
            vdi(129, self.handle, (0,), (0, 0, 0, 0))
        else:
            vdi(129, self.handle, (1,), (x1, y1, x2, y2))

    # --- drawing -----------------------------------------------------------

    def clear(self):
        vdi(3, self.handle)                        # v_clrwk

    def line(self, x1, y1, x2, y2):
        vdi(6, self.handle, ptsin=(x1, y1, x2, y2))

    def polyline(self, points):
        """points: flat sequence x0, y0, x1, y1, ..."""
        vdi(6, self.handle, ptsin=points)

    def rect(self, x1, y1, x2, y2):
        vdi(6, self.handle, ptsin=(x1, y1, x2, y1, x2, y2, x1, y2, x1, y1))

    def fill_rect(self, x1, y1, x2, y2):
        vdi(114, self.handle, ptsin=(x1, y1, x2, y2))   # vr_recfl

    def polygon(self, points):
        vdi(9, self.handle, ptsin=points)          # v_fillarea

    def circle(self, x, y, r, fill=False):
        if fill:
            vdi(11, self.handle, ptsin=(x, y, 0, 0, r, 0), subop=4)
        else:                                      # v_arc from 0 to 360 degrees
            vdi(11, self.handle, (0, 3600), (x, y, 0, 0, 0, 0, r, 0), subop=2)

    def ellipse(self, x, y, rx, ry, fill=False):
        if fill:
            vdi(11, self.handle, ptsin=(x, y, rx, ry), subop=5)
        else:
            vdi(11, self.handle, (0, 3600), (x, y, rx, ry), subop=6)

    def text(self, x, y, s):
        _gem.v_gtext(self.handle, x, y, s)

    def pixel(self, x, y):
        """Colour index of pixel (x, y)."""
        return vdi(105, self.handle, ptsin=(x, y))[0][1]   # v_get_pixel



class Screen(Canvas):
    """The screen, through a VDI virtual workstation.

    lock=True takes wind_update(BEG_UPDATE): under a multitasking system,
    nobody else draws in the meantime. hide_mouse=True hides the
    mouse while drawing, otherwise it leaves trails.
    Coordinates in pixels, (0, 0) at top left.
    """

    def __init__(self, lock=True, hide_mouse=True):
        self._lock = lock
        self._hide = hide_mouse
        self.handle = 0

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def open(self):
        _app_enter()
        try:
            phys, self.char_w, self.char_h, self.box_w, self.box_h = _gem.graf_handle()
            self.handle, wo = _gem.v_opnvwk(phys)
        except Exception:
            _app_leave()
            raise
        self.width = wo[0] + 1
        self.height = wo[1] + 1
        self.colors = wo[13]
        if self._lock:
            _gem.wind_update(1)
        if self._hide:
            vdi(123, self.handle)                  # v_hide_c
        vdi(39, self.handle, (0, 5))               # text: left, top
        vdi(23, self.handle, (SOLID,))             # solid fill
        return self

    def close(self):
        if not self.handle:
            return
        if self._hide:
            vdi(122, self.handle, (1,))            # v_show_c
        if self._lock:
            _gem.wind_update(0)
        _gem.v_clsvwk(self.handle)
        self.handle = 0
        _app_leave()

def alert(message, buttons=("OK",), default=1, icon=NOICON):
    """GEM alert box. Returns the number of the chosen button, from 1.

    At most 5 lines of 30 characters (wrapped here if needed) and 3 buttons.
    """
    for c in "[]|":
        if c in message or any(c in b for b in buttons):
            raise ValueError("'[', ']' et '|' sont reserves par l'AES")
    if not 1 <= len(buttons) <= 3:
        raise ValueError("1 a 3 boutons")
    lines = []
    for para in message.split("\n"):
        while len(para) > 30:
            cut = para.rfind(" ", 0, 31)
            if cut <= 0:
                cut = 30
            lines.append(para[:cut])
            para = para[cut:].lstrip()
        lines.append(para)
    if len(lines) > 5:
        raise ValueError("message trop long : 5 lignes de 30 caracteres")
    spec = "[%d][%s][%s]" % (icon, "|".join(lines), "|".join(buttons))
    _app_enter()
    try:
        _gem.graf_mouse(M_ON)
        return _gem.form_alert(default, spec)
    finally:
        _app_leave()


def fileselect(pattern="*.*", directory=None, name="", title="Choisir un fichier"):
    """GEM file selector. Returns the full path, or None if cancelled."""
    if directory is None:
        import uos
        directory = uos.getcwd()
    if not directory.endswith("\\"):
        directory += "\\"
    _app_enter()
    try:
        _gem.graf_mouse(M_ON)
        button, path, chosen = _gem.fsel_exinput(directory + pattern, name, title)
    finally:
        _app_leave()
    if button == 0 or not chosen:
        return None
    return path[:path.rfind("\\") + 1] + chosen


def wait_key():
    """Wait for a key. Returns scan code << 8 | character.

    Through GEMDOS (Crawcin), like input(): this is a TOS program. A
    key typed before the call has already been set aside by the Ctrl-C
    watch; it is returned first, without a scan code. Ctrl-C raises
    KeyboardInterrupt.
    """
    return _gem.wait_key()


# --- windows ----------------------------------------------------------------


def _wind_get(h, field):
    return aes(104, (h, field), n_intout=5)[0][1:]


def _wind_set(h, field, a=0, b=0, c=0, d=0):
    aes(105, (h, field, a, b, c, d))


def _inter(r1, r2):
    """Intersection of two rectangles (x, y, w, h), or None."""
    x = max(r1[0], r2[0])
    y = max(r1[1], r2[1])
    x2 = min(r1[0] + r1[2], r2[0] + r2[2])
    y2 = min(r1[1] + r1[3], r2[1] + r2[3])
    if x2 <= x or y2 <= y:
        return None
    return (x, y, x2 - x, y2 - y)


class Window:
    """A GEM window. Created by App.window(), not directly.

    The content is drawn in on_draw(canvas, x, y, w, h), called back for
    each rectangle to redraw, with the clip already set: draw the whole
    work area without worrying about the rest. (x, y, w, h) is the whole
    work area, in screen coordinates.
    """

    def __init__(self, app, title, kind, x, y, w, h):
        self.app = app
        self.kind = kind
        self.on_draw = None
        self.on_close = None
        # wind_create receives the MAXIMUM size -- the full-screen one
        # (WM_FULLED) --, hence the whole desktop. The wanted size is
        # only given at opening.
        self.handle = aes(100, (kind,) + tuple(app.desk))[0][0]   # wind_create
        if self.handle < 0:
            raise OSError("plus de fenetre disponible")
        self._rect = (x, y, w, h)
        self.is_open = False
        self.title(title)

    def title(self, text):
        if self.kind & NAME:
            _gem.wind_set_str(self.handle, 2, text)

    def info(self, text):
        if self.kind & INFO:
            _gem.wind_set_str(self.handle, 3, text)

    def open(self):
        x, y, w, h = self._rect
        aes(101, (self.handle, x, y, w, h))                   # wind_open
        self.is_open = True
        return self

    def close(self):
        if self.is_open:
            aes(102, (self.handle,))                          # wind_close
            self.is_open = False

    def delete(self):
        self.close()
        if self.handle > 0:
            aes(103, (self.handle,))                          # wind_delete
            self.app._windows.pop(self.handle, None)
            self.handle = 0

    @property
    def work(self):
        """Work area (x, y, w, h), in screen coordinates."""
        return _wind_get(self.handle, WF_WORKXYWH)

    @property
    def rect(self):
        """Whole window, frame included."""
        return _wind_get(self.handle, WF_CURRXYWH)

    def move(self, x, y, w, h):
        _wind_set(self.handle, WF_CURRXYWH, x, y, w, h)
        self._rect = (x, y, w, h)

    def top(self):
        _wind_set(self.handle, WF_TOP)

    def redraw(self, area=None):
        """Redraw area (x, y, w, h), the whole work area by default.
        Walks the list of visible rectangles kept by the AES: a partly
        covered window is only redrawn where it can be seen.
        """
        if not self.is_open or self.on_draw is None:
            return
        work = self.work
        area = _inter(area or work, work)
        if area is None:
            return
        cv = self.app.canvas
        _gem.wind_update(1)
        _gem.graf_mouse(M_OFF)
        try:
            r = _wind_get(self.handle, WF_FIRSTXYWH)
            while r[2] and r[3]:
                clip = _inter(r, area)
                if clip:
                    cv.clip(clip[0], clip[1], clip[0] + clip[2] - 1,
                            clip[1] + clip[3] - 1)
                    self.on_draw(cv, *work)
                r = _wind_get(self.handle, WF_NEXTXYWH)
        finally:
            cv.clip()
            _gem.graf_mouse(M_ON)
            _gem.wind_update(0)

    def invalidate(self):
        """Request a redraw through the AES (WM_REDRAW message to itself)."""
        x, y, w, h = self.work
        _gem.appl_write(_gem.ap_id(),
                        (WM_REDRAW, _gem.ap_id(), 0, self.handle, x, y, w, h))


class App:
    """A GEM application: windows and event loop.

        with gem.App() as app:
            w = app.window("Hello", 40, 40, 300, 200)
            w.on_draw = lambda c, x, y, w_, h: (c.color(fill=0),
                                                c.fill_rect(x, y, x + w_ - 1, y + h - 1))
            w.open()
            app.run()

    run() returns when the last window is closed, when stop() is
    called, or after timeout ms. The callbacks:
      on_key(code)            key (scan << 8 | character)
      on_click(x, y, buttons, nclicks)
      on_timer()              every tick ms (if defined)
    """

    def __init__(self):
        self._windows = {}
        self.on_key = None
        self.on_click = None
        self.on_timer = None
        self._menu = None
        self.tick = 200
        self._running = False
        _app_enter()
        try:
            phys, self.char_w, self.char_h, self.box_w, self.box_h = _gem.graf_handle()
            self.canvas = Canvas()
            self.canvas.handle, wo = _gem.v_opnvwk(phys)
        except Exception:
            _app_leave()
            raise
        self.width = wo[0] + 1
        self.height = wo[1] + 1
        self.colors = wo[13]
        vdi(39, self.canvas.handle, (0, 5))     # text: left, top
        vdi(23, self.canvas.handle, (SOLID,))
        _gem.graf_mouse(ARROW)
        self._refresh_desk()
        # Received messages, to log them: app.trace = []
        self.trace = None

    def _refresh_desk(self):
        """Have the AES redraw the whole desktop, with the mouse hidden.

        The pointer keeps a copy of what it covers. If the screen was
        repainted while it was not hidden -- at launch, when the desktop clears
        the screen --, it restores squares of the old screen as it moves.
        Hiding it, having the screen redrawn (form_dial FMD_FINISH on the whole
        desktop), then showing it again starts over from a clean screen.
        """
        x, y, w, h = self.desk
        _gem.graf_mouse(M_OFF)
        try:
            aes(51, (3, 0, 0, 0, 0, x, y, w, h))    # form_dial(FMD_FINISH)
        finally:
            _gem.graf_mouse(M_ON)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def close(self):
        if self._menu is not None:
            self._menu.hide()
            self._menu = None
        for w in list(self._windows.values()):
            w.delete()
        if self.canvas.handle:
            _gem.v_clsvwk(self.canvas.handle)
            self.canvas.handle = 0
            _app_leave()

    @property
    def desk(self):
        """Usable desktop area (x, y, w, h), below the menu bar."""
        return _wind_get(0, WF_WORKXYWH)

    def window(self, title, x=None, y=None, w=None, h=None,
               kind=NAME | CLOSER | FULLER | MOVER | SIZER):
        dx, dy, dw, dh = self.desk
        if w is None:
            w = dw * 2 // 3
        if h is None:
            h = dh * 2 // 3
        if x is None:
            x = dx + (dw - w) // 2
        if y is None:
            y = dy + (dh - h) // 2
        win = Window(self, title, kind, x, y, w, h)
        self._windows[win.handle] = win
        return win

    def menu(self, menus, about=None, title=None):
        """Install a menu bar. See the Menu class."""
        if self._menu is not None:
            self._menu.hide()
        self._menu = Menu(self, menus, about, title)
        self._menu.show()
        return self._menu

    def stop(self):
        self._running = False

    def run(self, timeout=None):
        import time
        self._running = True
        t0 = time.ticks_ms()
        flags = MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER
        while self._running and (self._menu is not None
                                 or any(w.is_open for w in self._windows.values())):
            # The timer hands control back to Python regularly: without it,
            # the interpreter would stay blocked in the AES and the Ctrl-C hook
            # would no longer run.
            ev, mx, my, mb, ks, key, nc, msg = _gem.evnt_multi(flags, self.tick)
            if ev & MU_MESAG:
                self._message(msg)
            if ev & MU_KEYBD:
                if key & 0xFF == 3:
                    raise KeyboardInterrupt
                if self.on_key:
                    self.on_key(key)
            if ev & MU_BUTTON and self.on_click:
                self.on_click(mx, my, mb, nc)
            if ev & MU_TIMER and self.on_timer:
                self.on_timer()
            if timeout is not None and time.ticks_diff(time.ticks_ms(), t0) >= timeout:
                break
        self._running = False

    def _message(self, msg):
        kind, h = msg[0], msg[3]
        if self.trace is not None:
            self.trace.append(tuple(msg))
        if kind == AP_TERM:
            self._running = False
            return
        if kind == MN_SELECTED:
            if self._menu is not None:
                self._menu._selected(msg[3], msg[4])
            return
        win = self._windows.get(h)
        if win is None:
            return
        if kind == WM_REDRAW:
            win.redraw(msg[4:8])
        elif kind in (WM_TOPPED, WM_NEWTOP):
            win.top()
        elif kind == WM_CLOSED:
            if win.on_close is None or win.on_close() is not False:
                win.close()
        elif kind in (WM_MOVED, WM_SIZED):
            win.move(*msg[4:8])
            if kind == WM_SIZED:
                win.invalidate()
        elif kind == WM_FULLED:
            full = _wind_get(h, WF_FULLXYWH)
            if tuple(win.rect) == tuple(full):
                win.move(*_wind_get(h, WF_PREVXYWH))
            else:
                win.move(*full)
            win.invalidate()


# --- menus ------------------------------------------------------------------


class Menu:
    """GEM menu bar, created by App.menu().

        app.menu([("File", [("Open...", do_open), None, ("Quit", app.stop)]),
                  ("Options", [("Sound", toggle_sound)])],
                 about=("About MyApp...", about_box))

    None in a list of entries draws a separator. The first title is
    the desk menu's (the application name, title=, "Python" by
    default): it carries "About" and the six slots the AES reserves for
    accessories -- they must exist, the AES writes into them.

    The object tree is built here, in a bytearray: the AES reads it and
    writes into it (entry states) as long as the menu is shown. The GC never
    moves an object, so its address stays valid; Menu keeps the
    reference.
    """

    def __init__(self, app, menus, about=None, title=None):
        import struct
        cw, ch = app.char_w, app.char_h
        W, H = app.width, app.height
        self._cb = {}           # entry object -> callback
        self._labels = {}       # label -> entry object
        self._titles = {}       # title label -> title object
        self._title_of = {}     # entry object -> title object

        desk = [(about[0] if about else "A propos...", about[1] if about else None), None]
        desk += [("Accessoire %d" % i, None) for i in range(1, 7)]
        menus = [(title or "Python", desk)] + list(menus)

        objs = []   # [next, head, tail, type, flags, state, spec, x, y, w, h]
        texts = []  # (object, bytes): strings to place after the objects

        def new(typ, x, y, w, h, spec=0, text=None, state=0):
            objs.append([-1, -1, -1, typ, 0, state, spec, x, y, w, h])
            if text is not None:
                texts.append((len(objs) - 1, _gem.to_st(text) + b"\0"))
            return len(objs) - 1

        def link(parent, children):
            objs[parent][1] = children[0]
            objs[parent][2] = children[-1]
            for a, b in zip(children, children[1:]):
                objs[a][0] = b
            objs[children[-1]][0] = parent

        bar_h = ch + 2
        title_h = ch + 3
        root = new(G_IBOX, 0, 0, W, H)
        bar = new(G_BOX, 0, 0, W, bar_h, spec=0x1100)
        active = new(G_IBOX, 2 * cw, 0, 0, title_h)
        titles = []
        tx = 0
        for name, _ in menus:
            t = " %s " % name
            o = new(G_TITLE, tx, 0, len(t) * cw, title_h, text=t)
            titles.append(o)
            self._titles[name] = o
            tx += len(t) * cw
        objs[active][9] = tx
        screen = new(G_IBOX, 0, title_h, W, H - title_h)
        drops = []
        for k, (name, items) in enumerate(menus):
            larg = max([len(it[0]) for it in items if it] + [8]) + 4
            if k == 0:
                larg = max(larg, 22)    # accessory names
            x = objs[active][7] + objs[titles[k]][7]
            if x + larg * cw > W:
                x = W - larg * cw - 1
            d = new(G_BOX, x, 0, larg * cw, len(items) * ch, spec=0xFF1100)
            drops.append(d)
            entries = []
            for j, it in enumerate(items):
                if it is None:
                    e = new(G_STRING, 0, j * ch, larg * cw, ch,
                            text="-" * larg, state=DISABLED)
                else:
                    label, cb = it
                    e = new(G_STRING, 0, j * ch, larg * cw, ch,
                            text=("  " + label + " " * larg)[:larg])
                    self._cb[e] = cb
                    self._title_of[e] = titles[k]
                    if k > 0 or j == 0:
                        self._labels[label] = e
                entries.append(e)
            link(d, entries)
        link(root, [bar, screen])
        link(bar, [active])
        link(active, titles)
        link(screen, drops)
        objs[-1][4] |= LASTOB

        # A single block: the objects (24 bytes each), then the strings.
        n = len(objs)
        taille = n * 24 + sum(len(b) for _, b in texts)
        self._buf = bytearray(taille)
        base = _gem.addressof(self._buf)
        off = n * 24
        for o, b in texts:
            objs[o][6] = base + off
            self._buf[off:off + len(b)] = b
            off += len(b)
        for i, o in enumerate(objs):
            struct.pack_into(">hhhHHHLhhhh", self._buf, i * 24, *o)
        self.addr = base
        self.shown = False

    def show(self):
        if aes(30, (1,), (self.addr,))[0][0] == 0:     # menu_bar
            raise OSError("menu_bar a echoue")
        self.shown = True

    def hide(self):
        if self.shown:
            aes(30, (0,), (self.addr,))
            self.shown = False

    def _obj(self, label):
        try:
            return self._labels[label]
        except KeyError:
            raise ValueError("pas d'entree de menu %r" % label)

    def check(self, label, on=True):
        aes(31, (self._obj(label), 1 if on else 0), (self.addr,))   # menu_icheck

    def enable(self, label, on=True):
        aes(32, (self._obj(label), 1 if on else 0), (self.addr,))   # menu_ienable

    def state(self, label_or_obj):
        """AES state of the object (SELECTED, CHECKED, DISABLED...)."""
        o = label_or_obj if isinstance(label_or_obj, int) else (
            self._titles.get(label_or_obj) or self._obj(label_or_obj))
        return (self._buf[o * 24 + 10] << 8) | self._buf[o * 24 + 11]

    def title_obj(self, name):
        return self._titles[name]

    def item_obj(self, label):
        return self._obj(label)

    def _selected(self, title, item):
        try:
            cb = self._cb.get(item)
            if cb:
                cb()
        finally:
            aes(33, (title, 1), (self.addr,))                          # menu_tnormal
