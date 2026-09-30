# gem : dessiner avec le VDI et dialoguer avec l'AES, depuis Python.
#
#   import gem
#   with gem.Screen() as s:
#       s.clear()
#       s.color(fill=2)
#       s.fill_rect(10, 10, 200, 100)
#       s.text(20, 120, "Bonjour l'Atari")
#   choix = gem.alert("Enregistrer ?", ("Oui", "Non"))
#   nom = gem.fileselect("*.PY")
#
# Le C (_gem) ne fournit que l'indispensable : vdi() et aes() generiques, et
# les appels qui portent des chaines. Tout le reste est ici, par-dessus
# _gem.vdi(), avec les numeros d'opcode du VDI en clair.
#
# L'AES doit tourner : un programme lance depuis le bureau (ou un shell sous
# le bureau) l'a, un programme du dossier AUTO non.

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

# form_alert, icone
NOICON = 0
NOTE = 1
QUESTION = 2
STOP = 3

# evnt_multi
MU_KEYBD = 1
MU_BUTTON = 2
MU_MESAG = 16
MU_TIMER = 32

# wind_create : elements du cadre
NAME = 1
CLOSER = 2
FULLER = 4
MOVER = 8
INFO = 16
SIZER = 32

# messages AES
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

# menus : types, drapeaux et etats d'objet AES, message de choix
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
    """Les primitives de dessin, sur une station de travail VDI ouverte
    (self.handle). Partagees par Screen et par les fenetres d'une App."""

    # --- attributs ---------------------------------------------------------

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
        vdi(16, self.handle, ptsin=(w, 0))         # vsl_width (impair)

    def rgb(self, index, r, g, b):
        """Couleur de palette, composantes de 0 a 1000."""
        vdi(14, self.handle, (index, r, g, b))     # vs_color

    def clip(self, x1=None, y1=None, x2=None, y2=None):
        if x1 is None:
            vdi(129, self.handle, (0,), (0, 0, 0, 0))
        else:
            vdi(129, self.handle, (1,), (x1, y1, x2, y2))

    # --- dessin ------------------------------------------------------------

    def clear(self):
        vdi(3, self.handle)                        # v_clrwk

    def line(self, x1, y1, x2, y2):
        vdi(6, self.handle, ptsin=(x1, y1, x2, y2))

    def polyline(self, points):
        """points : sequence plate x0, y0, x1, y1, ..."""
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
        else:                                      # v_arc de 0 a 360 degres
            vdi(11, self.handle, (0, 3600), (x, y, 0, 0, 0, 0, r, 0), subop=2)

    def ellipse(self, x, y, rx, ry, fill=False):
        if fill:
            vdi(11, self.handle, ptsin=(x, y, rx, ry), subop=5)
        else:
            vdi(11, self.handle, (0, 3600), (x, y, rx, ry), subop=6)

    def text(self, x, y, s):
        _gem.v_gtext(self.handle, x, y, s)

    def pixel(self, x, y):
        """Index de couleur du pixel (x, y)."""
        return vdi(105, self.handle, ptsin=(x, y))[0][1]   # v_get_pixel



class Screen(Canvas):
    """L'ecran, par une station de travail virtuelle du VDI.

    lock=True prend wind_update(BEG_UPDATE) : sous un systeme multitache,
    personne d'autre ne dessine pendant ce temps. hide_mouse=True cache la
    souris le temps du dessin, sans quoi elle laisse des traces.
    Coordonnees en pixels, (0, 0) en haut a gauche.
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
        vdi(39, self.handle, (0, 5))               # texte : gauche, haut
        vdi(23, self.handle, (SOLID,))             # remplissage plein
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
    """Boite d'alerte GEM. Rend le numero du bouton choisi, a partir de 1.

    Au plus 5 lignes de 30 caracteres (coupees ici si besoin) et 3 boutons.
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
    """Selecteur de fichiers GEM. Rend le chemin complet, ou None si annule."""
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
    """Attend une touche. Rend scan code << 8 | caractere.

    Par GEMDOS (Crawcin), comme input() : c'est un programme TOS. Une
    touche frappee avant l'appel a deja ete mise de cote par le guet du
    Ctrl-C ; elle est rendue en premier, sans scan code. Ctrl-C leve
    KeyboardInterrupt.
    """
    return _gem.wait_key()


# --- fenetres ---------------------------------------------------------------


def _wind_get(h, field):
    return aes(104, (h, field), n_intout=5)[0][1:]


def _wind_set(h, field, a=0, b=0, c=0, d=0):
    aes(105, (h, field, a, b, c, d))


def _inter(r1, r2):
    """Intersection de deux rectangles (x, y, w, h), ou None."""
    x = max(r1[0], r2[0])
    y = max(r1[1], r2[1])
    x2 = min(r1[0] + r1[2], r2[0] + r2[2])
    y2 = min(r1[1] + r1[3], r2[1] + r2[3])
    if x2 <= x or y2 <= y:
        return None
    return (x, y, x2 - x, y2 - y)


class Window:
    """Une fenetre GEM. Creee par App.window(), pas directement.

    Le contenu se dessine dans on_draw(canvas, x, y, w, h), rappele pour
    chaque rectangle a redessiner, avec le clip deja pose : dessiner toute la
    zone de travail sans se soucier du reste. (x, y, w, h) est la zone de
    travail entiere, en coordonnees ecran.
    """

    def __init__(self, app, title, kind, x, y, w, h):
        self.app = app
        self.kind = kind
        self.on_draw = None
        self.on_close = None
        # wind_create recoit la taille MAXIMALE -- celle du plein ecran
        # (WM_FULLED) --, donc le bureau entier. La taille voulue n'est
        # donnee qu'a l'ouverture.
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
        """Zone de travail (x, y, w, h), en coordonnees ecran."""
        return _wind_get(self.handle, WF_WORKXYWH)

    @property
    def rect(self):
        """Fenetre entiere, cadre compris."""
        return _wind_get(self.handle, WF_CURRXYWH)

    def move(self, x, y, w, h):
        _wind_set(self.handle, WF_CURRXYWH, x, y, w, h)
        self._rect = (x, y, w, h)

    def top(self):
        _wind_set(self.handle, WF_TOP)

    def redraw(self, area=None):
        """Redessine area (x, y, w, h), toute la zone de travail par defaut.
        Parcourt la liste des rectangles visibles que tient l'AES : une
        fenetre en partie recouverte n'est redessinee que la ou elle se voit.
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
        """Demande un redessin par l'AES (message WM_REDRAW a soi-meme)."""
        x, y, w, h = self.work
        _gem.appl_write(_gem.ap_id(),
                        (WM_REDRAW, _gem.ap_id(), 0, self.handle, x, y, w, h))


class App:
    """Une application GEM : fenetres et boucle d'evenements.

        with gem.App() as app:
            w = app.window("Bonjour", 40, 40, 300, 200)
            w.on_draw = lambda c, x, y, w_, h: (c.color(fill=0),
                                                c.fill_rect(x, y, x + w_ - 1, y + h - 1))
            w.open()
            app.run()

    run() rend la main quand la derniere fenetre est fermee, quand stop() est
    appele, ou au bout de timeout ms. Les rappels :
      on_key(code)            touche (scan << 8 | caractere)
      on_click(x, y, boutons, nclics)
      on_timer()              toutes les tick ms (si defini)
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
        vdi(39, self.canvas.handle, (0, 5))     # texte : gauche, haut
        vdi(23, self.canvas.handle, (SOLID,))
        _gem.graf_mouse(ARROW)
        self._refresh_desk()
        # Messages recus, si on veut les journaliser : app.trace = []
        self.trace = None

    def _refresh_desk(self):
        """Fait redessiner tout le bureau par l'AES, souris cachee.

        Le pointeur garde une copie de ce qu'il recouvre. Si l'ecran a ete
        repeint sans qu'il soit cache -- au lancement, quand le bureau efface
        l'ecran --, il restitue en bougeant des carres de l'ancien ecran.
        Le cacher, faire redessiner (form_dial FMD_FINISH sur le bureau
        entier), puis le reafficher repart d'un ecran propre.
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
        """Zone utile du bureau (x, y, w, h), sous la barre de menus."""
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
        """Installe une barre de menus. Voir la classe Menu."""
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
            # Le minuteur rend la main a Python regulierement : sans lui,
            # l'interprete resterait bloque dans l'AES et le hook Ctrl-C ne
            # tournerait plus.
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
    """Barre de menus GEM, creee par App.menu().

        app.menu([("Fichier", [("Ouvrir...", ouvrir), None, ("Quitter", app.stop)]),
                  ("Options", [("Son", bascule_son)])],
                 about=("A propos de MonAppli...", a_propos))

    None dans une liste d'entrees trace un separateur. Le premier titre est
    celui du menu bureau (le nom de l'application, title=, par defaut
    "Python") : il porte "A propos" et les six places que l'AES reserve aux
    accessoires -- elles doivent exister, l'AES y ecrit.

    L'arbre d'objets est construit ici, dans un bytearray : l'AES le lit et
    y ecrit (etats des entrees) tant que le menu est affiche. Le GC ne
    deplace jamais un objet, son adresse reste donc valable ; Menu garde la
    reference.
    """

    def __init__(self, app, menus, about=None, title=None):
        import struct
        cw, ch = app.char_w, app.char_h
        W, H = app.width, app.height
        self._cb = {}           # objet entree -> rappel
        self._labels = {}       # libelle -> objet entree
        self._titles = {}       # libelle de titre -> objet titre
        self._title_of = {}     # objet entree -> objet titre

        desk = [(about[0] if about else "A propos...", about[1] if about else None), None]
        desk += [("Accessoire %d" % i, None) for i in range(1, 7)]
        menus = [(title or "Python", desk)] + list(menus)

        objs = []   # [next, head, tail, type, flags, state, spec, x, y, w, h]
        texts = []  # (objet, bytes) : chaines a placer apres les objets

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
                larg = max(larg, 22)    # noms d'accessoires
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

        # Un seul bloc : les objets (24 octets chacun), puis les chaines.
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
        """Etat AES de l'objet (SELECTED, CHECKED, DISABLED...)."""
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
