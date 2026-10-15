MicroPython pour Atari ST/STE/Falcon
====================================

OBJECTIF
--------
Porter MicroPython sur Atari TOS. Version actuelle : v1.29.0.

Depot : ~/Downloads/github/atari-upython, un VRAI fork git de
micropython/micropython. Branche de travail : atari-port, posee sur le tag
v1.29.0 (voir SUIVRE MICROPYTHON plus bas).
L'ancien depot (~/Documents/Dev/Atari/upython, v1.28, parti d'une copie du
port Amiga d'OoZe1911 et sans lien git avec l'amont) reste en archive : il
fonctionne, on n'y touche plus. Son historique detaille est dans
ChangeLog.txt, repris ici.

MACHINE CIBLE
-------------
Atari STE + carte TwiSTer : 68020 a 24 MHz, 8 Mo de RAM alternative,
bus IDE, USB et I2C. TOS 2.06 ou EmuTOS, avec Geneva et Teradesk.
Le Falcon (68030) execute le meme binaire, sous TOS, MagiC ou FreeMiNT
(ALLTEST 10/10 sous Hatari, Falcon + FreeMiNT 1.19 ; sockets par MiNTnet).
Sous MiNT, les noms 8.3 sortent en minuscules (FSTEST compare sans casse).

DEUX BINAIRES, nommes d'apres le processeur qu'ils demandent :
  MPY20.TTP   make              68020 et plus : STE+TwiSTer, TT, Falcon
  MPY00.TTP   make CPU=68000    toute machine, ST/STE d'origine compris
Chacun a son dossier (build/ et build-68000/). MPY20.TTP lance sur un
68000 le dit et rend la main (cpucheck.c, compile en 68000 quoi qu'il
arrive) ; MPY00.TTP tourne aussi sur 68020/030, plus lentement. Il faut
2 Mo de RAM : le binaire fait ~550 Ko et le tas 1 Mo par defaut (sur une
machine d'1 Mo, refus propre : "Free: ST-RAM 21KB").
MPYRUN.PRG choisit seul : MPY20.TTP si _CPU >= 20, sinon MPY00.TTP, et a
defaut l'ancien nom MPY.TTP. Les outils (Makefile.tools) sont en 68000.
Dans les commentaires des tests, "MPY.TTP" designe l'un ou l'autre.
MPY00.TTP n'est valide que sous Hatari pour l'instant (30/09/2026, ST
68000 a 8 MHz, EmuTOS et TOS 2.06) : tous les tests passent.

VIRGULE FLOTTANTE LOGICIELLE par defaut : ni la TwiSTer ni un Falcon030 de
base n'ont de FPU.

  make            soft float : STE+TwiSTer, TT, Falcon de base
  make FPU=1      hard float : machine equipee d'un 68881/68882
                  (le TT en a toujours un, le Falcon presque jamais)

La toolchain n'a que deux jeux de bibliotheques : celui par defaut, en
68000 sans FPU, et m68020-60, qui suppose un 68881 -- son demarrage
MiNTlib verifie la presence du FPU et refuse de tourner sans. Il n'y a
pas de variante 68020-sans-FPU, d'ou la separation des drapeaux dans le
Makefile : notre code est compile en 68020 sans instruction FPU
(CPU_FLAGS) et l'edition de liens prend les bibliotheques par defaut
(LD_CPU_FLAGS). Du code de bibliotheque 68000 tourne tres bien sur un
68020.

ETAT ACTUEL
-----------
Phase 1 (REPL minimal) : TERMINEE, VALIDEE SUR MATERIEL REEL y compris
dans la configuration qui echouait (adresse de chargement impaire,
reglee par MICROPY_OBJ_REPR_B) --
STE + TwiSTer, EmuTOS 1.4, Geneva + Teradesk, USB et alt-RAM actifs.
Sous TOS 2.06, meme machine, le demarrage echoue avec un
"TypeError: 'NoneType' object isn't callable" : soupcon d'adresse de
chargement non alignee sur 4 (voir POINTS OUVERTS 6). Sous Hatari (TT, 68030, 8 Mo de
TT-RAM) le test de fumee tests/SMOKE.PY passe en entier, en -O2 comme
en -Os : entiers longs, flottants, chaines et f-strings, structures,
exceptions, sys, gc, re, time, binascii, base64 gele, deflate, lecture
de fichier par le VFS POSIX. REPL verifie a la main (Ctrl-C, Ctrl-D,
backspace).
Binaire : ports/atari/build/MPY20.TTP, 543 Ko en v1.29 avec GEM et code
natif (485 Ko a la phase 1), PRGFLAGS 0x07.
Il faisait 2,4 Mo jusqu'au 18/09/2026 : voir PIEGE MiNTlib/strcmp.

Budget memoire : ~1,5 Mo avec le tas par defaut d'1 Mo. Tient donc en
ST-RAM sur un STE de 2 Mo ou plus, meme sans RAM alternative.
main.c trace les etapes d'init (ATARI_DEBUG_INIT, a passer a 0 quand le
port sera fiable) et nlr_jump_fail() affiche l'exception.

Hatari 2.6.1 dans ~/Downloads/hatari-2.6.1_macos (TOS fourni dans le
bundle). Lancement : ports/atari/run-hatari.sh (TT + 8 Mo de TT-RAM par
defaut, sortie console renvoyee sur le terminal par --conout 2).

TOOLCHAIN
---------
m68k-atari-mintelf- (gcc 13.4.0) dans /opt/cross-mint/bin. Le Makefile
le trouve tout seul via MINT_BIN_DIR, il n'a pas besoin d'etre dans PATH.
Son editeur de liens produit directement un PRG GEMDOS (0x601A) : pas
d'etape de conversion depuis l'ELF.
  m68k-atari-mintelf-flags   lit et ecrit les PRGFLAGS
  -m68020-60                 multilib reel de cette toolchain

Fichiers ecrits dans ports/atari/ :
  Makefile          cross-compilation, -Os, binaires MPY20.TTP / MPY00.TTP
  cpucheck.c        refus de MPY20.TTP sur 68000 (toujours compile en 68000)
  run-hatari.sh     lancement sous Hatari, console sur le terminal,
                    EmuTOS francais + clavier AZERTY par position
  tests/SMOKE.PY    test de fumee (entiers longs, flottants, re, deflate,
                    time, gc, base64 gele, accents, lecture de fichier)
  tests/DMATEST.PY  DMA vers la RAM alternative (point ouvert n.1) --
                    n'a de sens que sur un vrai disque IDE
  tests/FSTEST.PY   systeme de fichiers : chemins, listdir, stat, walk
  tests/BENCH.PY    vitesse de l'interprete
  tests/PFTEST.PY   cookie jar et module platform
  tests/I2CTEST.PY  bus I2C : scan et lecture du DS3231
  tests/RTCTEST.PY  DS3231 de pres : temperature, EEPROM, derive
  tests/HWTEST.PY   memoire, ecran (framebuf direct), clavier, son, serie
  tools/videoreg.c  VIDEOREG.PRG : registres video STE, et "fix" pour
                    remettre le scroll materiel a zero
                    (les deux : make -f Makefile.tools)
  mpconfigport.h    config MicroPython (ROM level EVERYTHING)
  mphalport.h       HAL : timing, charset, primitives VT52
  atari_mphal.c     console GEMDOS/BIOS, charset Atari ST, _hz_200, delais
  main.c            point d'entree, REPL, heap en alt-RAM, check CPU
  modtime.c         time via GEMDOS Tgetdate/Tgettime
  modatarios.c      module uos : listdir, stat, mkdir, rename, drives...
  modules/os.py     module os gele : walk, makedirs, removedirs
  modules/_atpath.py  os.path pour les chemins GEMDOS (C:\, 8.3)
  modules/platform.py machine, systeme, FPU, video, multitache
  machine_atari.c   module machine : I2C sur le bus de la TwiSTer
  machine_mem.c     mem8/16/32, mem_buffer, ecran, clavier, son YM2149
  machine_uart.c    machine.UART : RS-232, MIDI, imprimante
  modzlib.c         repris tel quel de l'Amiga (sans dependance OS)
  manifest.py       modules geles reduits au minimum pour la phase 1

MODIFICATIONS DU COEUR (hors ports/atari)
-----------------------------------------
Quatre commits, un par sujet, au-dessus du tag amont :
  py/misc.h              __builtin_strcmp (voir PIEGE MiNTlib/strcmp)
  py/mpstate.h           _gc_lock_pad : alignement m68k des racines du GC
  extmod/vfs_posix*.c    hook MICROPY_VFS_POSIX_CONVERT_PATH (noms de
                         fichiers UTF-8 -> jeu de caracteres Atari)
  py/ + mpy-cross/       emetteur natif m68k : asmm68k.c/.h, emitnm68k.c,
                         emitinlinem68k.c et leurs accroches (voir PHASE 9)
NON repris de l'ancien depot : les ~15 retouches "AMIGA FIX: force heap
iter" (vm.c, runtime.c, obj*.c, modbuiltins.c). Elles servent en REPR_A ;
en REPR_B un pointeur n'a besoin que d'etre pair, et la pile m68k l'assure.
tests/ITERTEST.PY passe par chacun de ces endroits : 13/13 en 68030, et
en 68000 charge a une adresse 4n+2.

SUIVRE MICROPYTHON
------------------
  git fetch origin --tags
  git rebase --onto v1.30.0 v1.29.0 atari-port
Les conflits possibles sont dans les fichiers du coeur ci-dessus (surtout
py/emitnative.c, py/compile.c et mpy-cross/main.c). Puis : make clean,
make, make CPU=68000, et la serie de tests sous Hatari.
Passage 1.28 -> 1.29 (30/09/2026) : aucun conflit de texte. Seul
changement subi : py/mkrules.mk lit LIBS et non plus LIB.
"origin" pointe sur github.com/micropython/micropython : ne jamais y
pousser. Publication : remote "fork" = git@github.com:jymoulin/atari-micropython
(branche atari-port, poussee le 15/10/2026).

PLAN
----
Phase 0   toolchain + Hatari -- FAITE
Phase 1   REPL minimal -- FAITE, validee sur le STE
Phase 2   systeme de fichiers -- FAITE, FSTEST.PY passe en entier
Phase 3   plateforme (cookie jar) -- FAITE, testee sur machine
Phase 4   GEM : form_alert, fsel_exinput -- FAITE
Phase 4bis machine.I2C sur le bus TwiSTer -- FAITE, validee sur machine
Phase 5   BIOS / XBIOS -- FAITE ET VALIDEE SUR LE STE (TOS 2.06, le
          21/09/2026) : SNDTEST 12/12, DISKTEST 10/10, SMOKE complet
Phase 6   VDI, dialogues AES et fenetres -- FAITE. Sur le STE (23/09) en
          .GTP : GEMTEST et WINTEST passent toutes leurs verifications
          automatiques sous TOS 2.06 et EmuTOS 1.4 -- fenetres deplacees,
          agrandies, recouvertes, refermees. Menus : MENUTEST 10/10
          sur le STE (28/09, tous les choix faits a la souris). PHASE 6
          COMPLETE
Phase 7   reseau : module socket sur STinG -- FAIT, NETTEST 26/26 sous
          Hatari (TT, STE TOS 2.06, ST 68000) ; a valider sur machine
          (net). MiNTnet (FreeMiNT) : FAIT (15/10), NETTEST 28/28 sous
          Hatari (Falcon, FreeMiNT 1.19, inet4 + lo0)
Phase 8   TLS (BearSSL/mbedTLS) -- optionnel, lourd
Phase 9   emetteurs natif, viper, asm_m68k -- FAITE, validee sur le STE
          (28/09/2026) : VIPERTEST 28/28, NATTEST 7/7, cache actif
Abandonne ARexx (aucun equivalent Atari)

PHASE 7 : LE MODULE socket (modsocket.c, sur STinG ou MiNTnet)
---------------------------------------------------------------
Pile choisie une fois : MiNTnet si le cookie MiNT est la et qu'un socket
AF_INET s'ouvre (inet4.xdd charge), sinon STinG. socket.stack() rend
'MiNTnet', 'STinG' ou None. MiNTnet : sockets BSD de la MiNTLib,
descripteurs toujours non bloquants, attentes par poll() en tranches de
20 ms (Ctrl-C), errno de la MiNTLib = MP_Exxx ; getaddrinfo par
gethostbyname (/etc/hosts, /etc/resolv.conf). +22 Ko de binaire.
setsockopt : SO_REUSEADDR, SO_BROADCAST, TCP_NODELAY traduits pour
MiNTnet ; sous STinG seul TCP_NODELAY compte (CN_setopt, v13).
Banc Hatari FreeMiNT : ~/Documents/Dev/Atari/mint/reseau/tools/
hatari-net.sh (disquette 2,88 Mo, Falcon, inet4 + dummy.xif), une ligne
"exec u:/a/bin/MPY20.TTP u:/a/bin/NETTEST.PY" dans le .cnf.

API des sockets de MicroPython : socket(), bind, listen, accept, connect,
send, sendall, recv, sendto, recvfrom, settimeout, setblocking, makefile,
read/readline/write, getsockname, getpeername, with ; getaddrinfo (DNS par
STinG), inet_pton/ntop ; select.poll() marche dessus. socket.sting() donne
(module, auteur, version) ou None. requests (micropython-lib) est gele :
HTTP seulement, HTTPS attend TLS (phase 8).
Appel du TPL : convention Pure C (arguments a leur taille, un int16 = un
mot). Un trampoline asm (atari_sting_call) recopie un bloc de mots prepare
par la macro TPL(idx, W(..), L(..)) : pas d'en-tete STinG a inclure.
Ecarts avec BSD, dus a STinG :
  - un handle = une connexion : accept() passe le handle au nouveau socket
    et rouvre une ecoute sur le port. STinG-LW v13 (noyau 92b5414c) garde
    l'ecoute et met les clients suivants en file (4) : NETTEST 28/28.
    Avant v13, la reouverture echoue tant qu'une connexion occupe le
    port : un client a la fois, les autres refuses.
  - UDP : un handle se fixe sur le premier correspondant ; sendto vers un
    autre rouvre le handle (meme port local).
  - TCP_NODELAY : CN_setopt(CNO_TCP_NODELAY) depuis v13 ; avant,
    E_NOROUTINE, ignore. send() decoupe toujours en segments entiers
    (4 x 1460). Ecrire-ecrire-lire x 10 : 2,5 s en v9, 0,2 s en v13.
Les handles ouverts sont notes et fermes a la sortie (atari_socket_
shutdown) : STinG les garderait sinon. Sur 68000, le GC coute plus que la
pile : recv() n'alloue que ce qui est arrive ; preferer readinto() et des
memoryview pour les gros transferts (55 Ko/s en boucle locale sur ST 8 MHz
au lieu de 11).
Test : tests/NETTEST.PY (boucle locale ; "net" : DNS, HTTP, requests).
Sous Hatari : STING=1 ./hatari-test.sh NETTEST.PY (KIT de ToTest/sting).

PHASE 6 : LE MODULE gem
-----------------------
modgem.c (_gem, sur la gemlib de la MiNTlib, -lgem) + modules/gem.py gele.
Le C ne fait que l'indispensable : vdi() et aes() generiques (tableaux en
tuples, ptsin a plat x0, y0, ...), plus les appels qui portent des chaines
(v_gtext, form_alert, fsel_exinput, conversion UTF-8 <-> jeu ST) et
l'ouverture de la station. Le reste est en Python par-dessus _gem.vdi() :
  with gem.Screen() as s:        appl_init, graf_handle, v_opnvwk,
                                 wind_update(BEG) et souris cachee
      s.width, s.height, s.colors, s.char_w, s.char_h
      s.clear() line() polyline() rect() fill_rect() polygon()
      s.circle(x, y, r, fill=) ellipse() text(x, y, "...") pixel(x, y)
      s.color(line=, fill=, text=) mode(gem.XOR) fill_style() rgb() clip()
  gem.alert("texte", ("Oui", "Non"), default=2, icon=gem.QUESTION) -> 1..3
  gem.fileselect("*.PY", name=, title=) -> chemin complet ou None
  gem.wait_key() -> scan << 8 | caractere
Fenetres et boucle d'evenements :
  with gem.App() as app:
      w = app.window("Titre", x, y, w, h, kind=gem.NAME | gem.CLOSER |
                     gem.FULLER | gem.MOVER | gem.SIZER | gem.INFO)
      w.on_draw = lambda c, x, y, w, h: ...   # c : un Canvas, clip pose
      w.on_close = ...        # rendre False refuse la fermeture
      w.open(); w.info("..."); w.title("..."); w.invalidate()
      app.on_key / on_click / on_timer (toutes les app.tick ms)
      app.run(timeout=None)   # rend la main a la derniere fermeture
  run() traite WM_REDRAW (par la liste des rectangles visibles), TOPPED,
  CLOSED, MOVED, SIZED, FULLED et AP_TERM. evnt_multi a un minuteur pour
  rendre la main a Python ; Ctrl-C au clavier leve KeyboardInterrupt.
  ECRAN D'UNE APPLI LANCEE : sous TOS mono-tache, pendant qu'une appli
  tourne, le bureau n'est qu'un fond gris SANS ICONES, avec le nom du
  programme en haut. C'est normal ; les icones reviennent a la sortie.
  Des carres de l'ancien ecran (icones) sous la souris viennent de la
  copie que garde le pointeur, prise avant l'effacement : App les efface
  au demarrage (souris cachee, form_dial FMD_FINISH, souris remontree).
  app.trace = [] journalise les messages AES recus.
Menus :
  m = app.menu([("Fichier", [("Ouvrir...", ouvrir), None, ("Quitter", app.stop)]),
                ("Options", [("Son", son)])],
               about=("A propos de X...", a_propos), title="X")
  None = separateur. m.check("Son", True), m.enable("Son", False),
  m.state("Son") & gem.CHECKED. Tant qu'un menu est affiche, run() tourne
  meme sans fenetre ; quitter par app.stop().
  L'arbre d'objets est construit en Python dans un bytearray (adresse par
  _gem.addressof : le GC ne deplace rien). Le premier menu porte "A propos"
  et SIX PLACES pour les accessoires : l'AES y ecrit, et TOS 2.06 retaille
  ce menu selon le nombre d'accessoires charges (constate : 8 lignes
  construites, 1 gardee sans accessoire). Sans ces places, l'AES ecrirait
  hors de l'arbre.
  PIEGE DES TESTS : avec -q, une consigne affichee par print() est
  invisible. Les consignes passent donc par l'interface elle-meme :
  ligne d'info de la fenetre, texte de l'alerte, titre du selecteur.
  PIEGE GEM : wind_create recoit la taille MAXIMALE (celle du plein
  ecran) ; la taille voulue se donne a wind_open. Window le fait.
  COMMENT LANCER UNE APPLICATION GEM -- constate sur le STE le
  22/09/2026 : en .TTP lance depuis le bureau, les fenetres s'ouvrent mais
  le texte de la console les recouvre et la SOURIS NE REPOND PAS (le
  clavier, si). TOS bascule l'ecran en mode texte pour un programme TOS et
  ne lui donne pas la souris. Deux facons de faire, dans l'ordre :
  1. MPYRUN.PRG (tools/mpyrun.c, make -f Makefile.tools), a poser a cote
     de MPY20.TTP / MPY00.TTP. C'est un programme GEM ordinaire, donc TOUS les bureaux
     le lancent en mode GEM, MagiC compris. Il choisit le script avec le
     selecteur de fichiers, se place dans son dossier, puis lance
     MPYxx.TTP -q -l <script>.LOG <script>, et affiche le code de retour.
  2. Copier le binaire en MPY.GTP : le bureau demande les parametres
     comme pour un .TTP et lance en mode GEM. Marche sous TOS 2.06 et
     EmuTOS, mais PAS sous MagiC, qui ne connait pas cette extension
     (rien dans ses sources).
  Dans les deux cas, -q est indispensable : sans lui le moindre print()
  s'inscrit sur le dessin.
Il faut l'AES : lancer depuis le bureau ou un shell sous le bureau, pas
depuis AUTO.
A SAVOIR :
- La console et le VDI dessinent sur le MEME ecran : un print() pendant
  un dessin s'y inscrit, et v_get_pixel le relit. GEMTEST differe ses
  affichages pour cette raison.
- Sans clip, le VDI ne coupe pas au bord : un trace hors ecran revient
  par la gauche.
- Le guet du Ctrl-C retire du tampon clavier la touche qu'il trouve et la
  garde pour input() ; l'AES ne la verra jamais. Une touche frappee AVANT
  une alerte n'y repond donc pas, ce qui est plutot sain. wait_key() la
  rend en premier (sans scan code).
- wait_key passe par GEMDOS (Crawcin), pas par evnt_keybd : sous Hatari,
  evnt_keybd avec EmuTOS sur un STE bloquait ou corrompait la memoire
  (NameError sur une globale) ; TOS 2.06 sur le meme STE et EmuTOS sur TT
  marchaient. Cause non etablie. Prudence donc avec _gem.aes(20, ...).

PHASE 9 : CODE NATIF, VIPER ET ASSEMBLEUR (repris du port X68000)
------------------------------------------------------------------
Emetteur m68k du port X680x0 de Yuichi Nakamura (base v1.25), adapte a la
v1.28 : py/asmm68k.c/.h, emitnm68k.c, emitinlinem68k.c, et ses accroches
(compile.c, emit.h, emitnative.c, mpconfig.h, nativeglue, persistentcode.h,
smallint, asmbase, py.mk, mpy-cross). Code 68000 pur : un seul emetteur
pour 68000, 68020 et 68030.
  @micropython.native     bytecode compile en code machine (~x3)
  @micropython.viper      entiers machine, ptr8/16/32 (~x40 a x60)
  @micropython.asm_m68k   assembleur ; arguments en [8,fp], [12,fp]...,
                          resultat dans d0 ; un bytearray passe son adresse
Choix et adaptations :
- NLR par setjmp (celui de la MiNTlib), pas le NLR natif du X68000 (non
  importe, pas plus que son crochet dans nlr.h, qui s'imposerait a tout
  m68k, port Amiga compris). NLR_BUF_IDX_LOCAL_1 = 2 + 3 : d4 est au mot
  3 du jmp_buf de la MiNTlib (mintlib/setjmp.S).
- MP_NATIVE_ARCH_M68K = 13, APRES RV64IMC (le fork l'avait a 12).
- Vidage du cache d'instructions apres chaque fonction generee
  (MP_PLAT_COMMIT_EXEC, atari_mphal.c) : CACR bit 3 sur 68020/030,
  cpusha sur 68040/060. Le test de VIPERTEST passe aussi SANS sous Hatari
  (cache de 256 octets purge par la compilation elle-meme) : il ne prouve
  pas le vidage, qui reste necessaire par principe et surtout sur 040/060.
- Multiplication viper par une fonction d'aide (le 68000 n'a pas de
  multiplication 32 bits) : MICROPY_SMALL_INT_MUL_HELPER.
Trois defauts du port X68000, trouves par VIPERTEST et corriges :
  comparaisons non signees inversees (CC/CS), comparaison valant 0xFF et
  non 1 (scc, d'ou neg.b), chargements 8/16 bits a signe etendu (ptr8
  rendait -128..127 ; un qstr > 32767 aurait ete lu faux).
CODE NATIF PRECOMPILE (.mpy) : le code natif fige l'encodage des
constantes (petits entiers, None, True, qstr), qui depend de la
representation d'objets. Notre REPR_B n'est donc PAS transparent pour
mpy-cross : un .mpy natif fait par le mpy-cross ordinaire (REPR_A) se
charge sans erreur puis calcule faux (constaté : "int + str"). D'ou :
  cd ports/atari && make mpy-cross-atari   -> mpy-cross/build-atari/
  mpy-cross/build-atari/mpy-cross -march=m68k -msmall-int-bits=30 x.py
Le mpy-cross ordinaire refuse -march=m68k avec ce message. Compile SUR
l'Atari, le code natif n'a pas ce probleme.
Tests : tests/VIPTEST.PY (28, ex-VIPERTEST : 9 lettres, tronque en VIPERTES.PY sur disque TOS), tests/NATTEST.PY + make natmod (7).
Sous Hatari, STE 68020 a 8 MHz, 20000 tours de boucle vide : Python
12 s, native 4,2 s, viper 0,2 s.
Reste possible : le NLR natif (nlrm68k.c), plus rapide que setjmp.

PHASE 5 EN DETAIL : CE QUI VAUT LA PEINE DANS BIOS/XBIOS
--------------------------------------------------------
Classe par valeur ajoutee, pas par difficulte.

1. machine.mem8/16/32          ~80 lignes
   Lecture/ecriture de n'importe quelle adresse, avec passage en
   superviseur automatique au-dessus de 0xFF8000 et en dessous de
   0x800. C'est l'outil qui permet d'explorer n'importe quelle carte
   depuis le REPL, sans recompiler -- y compris l'USB de la TwiSTer.

2. Serie et MIDI               ~150 lignes
   Rsconf (vitesse, controle de flux), Bconin/Bconout/Bconstat/Bcostat
   sur les peripheriques 1 (AUX) et 3 (MIDI), Iorec pour l'etat du
   tampon d'entree, Bconmap la ou il y a plusieurs ports. Ouvre le
   dialogue avec un modem, un GPS, un capteur, une autre machine. Le
   MIDI vient presque gratuitement avec la meme plomberie.

3. Ecran                       ~120 lignes
   Physbase/Logbase/Getrez/Setscreen/Vsync, Setcolor et EsetColor /
   EsetPalette pour les 4096 couleurs du STE.
   IMPORTANT : framebuf est deja compile dans le binaire. En monochrome
   640x400 l'ecran est un seul plan, donc
   framebuf.FrameBuffer(memoire ecran, 640, 400, framebuf.MONO_HMSB)
   donne tout le dessin de framebuf directement sur l'ecran, sans une
   ligne de C de plus. En couleur les plans sont entrelaces et framebuf
   ne sait pas faire : il faudra le VDI (phase 6) ou un petit blitter
   conscient des plans.

4. Son                         ~60 + ~120 lignes
   Dosound et Giaccess pour le YM2149 : bips, musique, et l'acces
   direct aux registres du PSG. Puis le son DMA du STE (Soundcmd,
   Setbuffer, Setmode, Buffoper, Buffptr) pour jouer un echantillon
   depuis un bytearray.

5. Clavier                     ~40 lignes
   Kbshift (etat des touches mortes), Kbrate (vitesse de repetition),
   Ikbdws pour parler au processeur clavier.

6. Disque brut                 ~80 lignes
   Rwabs, Getbpb, Mediach : lecture et ecriture secteur par secteur,
   pour les outils de disquette et l'imagerie. Puissant et sans
   filet -- ecrire un secteur ne pardonne pas.

REALISE (18/09/2026) pour les points 4 et 6, dans machine :
  stram(n)             n octets de ST-RAM (Mxalloc mode 0), en bytearray.
                       Hors tas GC, rendus par GEMDOS a la sortie
                       seulement : allouer une fois, reutiliser.
  dmaplay(buf, rate=12517, stereo=False, loop=False) -> taux reel
                       8 bits signes. Taux arrondi au plus proche des
                       quatre du STE (6258, 12517, 25033, 50066).
  dmapos()             octet en cours, ou None quand la puce est a l'arret
  dmastop()
  Getbpb(lecteur) / Mediach(lecteur) / disk_read(lecteur, secteur, n=1)
  LA CONTRAINTE : les puces DMA (son, disquette) ne voient QUE la ST-RAM.
  Sur la TwiSTer l'alt-RAM est cote 68020, hors de leur portee, et notre
  tas y est. dmaplay refuse donc tout tampon au-dessus de phystop, avec
  un message qui renvoie a stram() ; disk_read passe par un tampon
  intermediaire en ST-RAM. Le tampon joue est garde en racine du GC tant
  que la puce le lit, et main() coupe le son en sortant (sinon une boucle
  continuerait dans de la memoire rendue a GEMDOS).
  Pas d'ECRITURE disque, volontairement : un mauvais numero de secteur
  detruit une partition, la carte CF de test comprise. Aucun appel XBIOS
  pour le son : TOS 2.06 n'en a pas (arrives avec le Falcon), on pilote
  les registres $FF8900 comme tout programme STE.

DELIBEREMENT ECARTE : Setexc et les vecteurs d'interruption avec
rappel Python. Un interprete ne peut pas tourner dans un gestionnaire
d'interruption, et le detour par une file d'attente donnerait un
mecanisme fragile pour un gain douteux. Vsync() couvre le seul besoin
de synchronisation qui compte vraiment.

GEMDOS : COUVERT EN ENTIER depuis le 2026-09-15. Les trois derniers
manques sont combles dans modatarios.c, et os.py les reexporte tous
(from uos import *) :

  os.utime(chemin, (atime, mtime))   Fdatime. GEMDOS ne garde qu'UNE
      date par fichier, donc seul mtime est honore ; sans argument,
      c'est l'heure courante. Granularite de deux secondes.
  os.dup(p) / os.force(std, p) / os.close(p)   Fdup, Fforce, Fclose.
      Fforce n'est PAS dup2 : il fait pointer la poignee STANDARD std
      vers ce que designe p, et ce sont les poignees standard 0-5 qui
      sont transmises a un programme lance par os.system().
      PASSER PAR os.redirect(), gestionnaire de contexte de os.py :
        with os.redirect(1, open("OUT.TXT", "w")):
            os.system("C:\\BIN\\LS.TTP")
      REDIRECTION : validee sous TOS 2.06 seul le 18/09/2026. Elle a
      plante deux fois sous Geneva + Teradesk (redemarrage le 15/09
      avec os.close de la poignee sauvee, bombes le 16/09 sans).
      os.redirect() ne ferme jamais la poignee sauvee : les sources ne
      s'accordent pas sur ce point (dupbug.c de th.otto le commente,
      la MiNTlib le fait dans system() et popen()), on garde le cote
      sur. Etabli par unix/open.c de la MiNTlib : ses descripteurs
      SONT des poignees GEMDOS, fileno() convient donc a Fforce.
      os.close() reste bon pour une poignee qu'on a ouverte soi-meme et
      qu'on n'a pas forcee sur une poignee standard.
  os.putenv(nom, valeur) / os.unsetenv(nom)   L'environnement appartient
      au processus : cela n'affecte que nous et nos programmes fils,
      GEMDOS ne sait rien rendre au shell.

ETAT (18/09/2026, TOS 2.06 + ALTRAMON, sans Geneva ni Teradesk) :
TOUT PASSE. FORCETST.TTP etapes 1 a 4 sans bombe ; tests/OSTEST.PY
8 reussites sur 8, redirection comprise (argument "force"), utime relu
a la seconde pres. Les deux plantages des 15 et 16/09 ont eu lieu sous
Geneva + Teradesk : Geneva est le suspect designe, mais pas encore
prouve coupable -- il faudrait FORCETST sous Geneva pour le dire.
Test : tests/OSTEST.PY [force], build/FORCETST.TTP.

DECISIONS PRISES
----------------
- libc : MiNTlib (fournit open/read/write/stat/opendir, fonctionne aussi
  sur TOS pur). Pas libcmini, trop limitee pour VFS_POSIX.
- Heap GC en RAM alternative via Mxalloc(taille, 3), 1 Mo par defaut,
  reglable par -m <ko>. La ST-RAM partage son bus avec le shifter video.
- Console : readline compile avec MICROPY_HAL_HAS_VT100=0, le port
  fournit les primitives VT52. Pas de traducteur ANSI->VT52 necessaire.
- Entree clavier : Cconis()/Crawcin() (GEMDOS), pas Bconin(2) (BIOS).
  Le BIOS lit la console physique et ignore les redirections : sous
  Geneva, y toucher fait ouvrir une fenetre propre au processus et prive
  le shell de son clavier. Les codes de touche restent disponibles dans
  les bits 16-23 quand l'entree standard est la console, donc les
  fleches fonctionnent toujours.
- Sortie : Fwrite(1,...) avec insertion des CR (la console TOS ne
  convertit pas LF en CRLF).
- L'heure vient de GEMDOS. MicroPython ne parle PAS au RTC en I2C :
  EmuTOS et le driver RTC s'en chargent deja.
- GEMDOS ne code les secondes que sur 5 bits, donc PAR PAS DE DEUX. Un
  ecart d'une seconde avec le DS3231 est le plancher de ce que le
  format sait representer, pas une derive : mesure sur la machine, une
  seconde d'avance. Ne rien chercher en dessous de deux secondes.
  RTCTEST.PY sync recale GEMDOS sur le DS3231 si besoin.
- PIEGE : la MiNTlib numerote ses errno d'apres les codes d'erreur
  GEMDOS, pas comme Linux. ENOENT vaut 33, EACCES 36, EEXIST 85,
  EIO 90. MicroPython prend les valeurs du systeme
  (MICROPY_USE_INTERNAL_ERRNO = 0), donc tout code Python qui compare
  e.errno a un nombre ecrit en dur est faux ici. Utiliser le module
  errno.
- MICROPY_OBJ_REPR_B au lieu du REPR_A par defaut. REPR_A etiquette les
  objets dans les DEUX bits de poids faible d'un pointeur, donc exige un
  alignement sur 4 octets -- impossible a garantir ici : un programme
  GEMDOS est charge ou le systeme veut, et sur le vrai STE sous Geneva
  le TPA tombe regulierement sur un multiple impair de 2. Tout le
  programme se decale avec, objets constants compris, et l'interprete
  les relit comme des qstr. Mesure sur la machine : texte en 0x2a28c6
  plante, texte en 0x576ff4 marche. REPR_B reconnait un pointeur a
  (o & 1) == 0, donc 2 octets suffisent -- ce que m68k et GEMDOS
  donnent toujours. Cout : 30 bits d'entier court au lieu de 31 (au
  dela, MPZ prend le relais) et uctypes desactive (il exige 31 bits).
- MICROPY_OBJ_BASE_ALIGNMENT force l'alignement des objets sur 4 octets.
  L'ABI m68k aligne les structures sur 2 octets, or MICROPY_OBJ_REPR_A
  se sert des deux bits de poids faible d'un pointeur comme etiquette de
  type : un objet pose a une adresse == 2 (mod 4) est relu comme un
  qstr. C'etait la cause des "TypeError: 'x' object isn't callable".
  Pas -malign-int, qui changerait la disposition des structures que l'on
  partage avec la MiNTlib.
- Consequence : ce port suppose un 68020+, qui accepte les acces 32 bits
  non alignes. Une variante -m68000 demanderait d'aligner aussi les
  tables (mp_rom_map_elem_t), aujourd'hui sur 2 octets.
- lib/ elague : seuls uzlib, re1.5, crypto-algorithms, micropython-lib,
  libm, libm_dbl, oofatfs et littlefs restent. Les autres etaient les
  SDK des ports STM32/RP2/NXP/Alif/etc (1 Go). Recuperables dans
  l'historique git si besoin : git checkout 7c0d1017 -- lib/<nom>.

POINTS OUVERTS / A VERIFIER SUR MATERIEL
----------------------------------------
1. DMA et RAM alternative : REGLE, sans rien avoir a faire.
   tests/DMATEST.PY passe sur le vrai disque IDE dans les quatre
   configurations (EmuTOS et TOS 2.06, tas en RAM alternative et en
   ST-RAM) : 256 Ko ecrits puis relus a l'identique. Le driver de la
   TwiSTer sait donc atteindre la RAM alternative -- ou n'utilise pas
   le DMA du ST. Pas de tampon intermediaire a prevoir, la couche
   fichier de la phase 2 s'ecrit normalement.
   Mesure au passage : la RAM alternative est 1,6 fois plus rapide que
   la ST-RAM en lecture de fichier (640 ms contre 1010 ms pour 256 Ko)
   et 1,7 fois en ecriture. Le choix du tas en RAM alternative est donc
   justifie.
   A savoir : la TwiSTer mappe sa RAM juste au-dessus de la ST-RAM,
   pas a 0x01000000 comme un TT.

7. VITESSE : REGLEE, de 299 a 216 us par tour de boucle vide.
   Le hook Ctrl-C valait ~19% (defaut passe de 64 a 4096 opcodes) et le
   cache d'instructions ~12% (CACHEON.PRG dans AUTO\, projet ../cache). Detail des
   mesures :

                        hook/64    hook/4096
     TOS 2.06            299          248     (I-cache eteint)
     EmuTOS 1.4          267          217     (I-cache actif)

   Deux effets, tous deux reels et cumulatifs :
   - le hook Ctrl-C (un trap BIOS Bconstat) : ~19% a lui seul. Le
     defaut passe de 64 a 4096 opcodes.
   - le cache d'instructions du 68020 : ~12%. Il est ETEINT sous
     TOS 2.06 alors que le CPX affiche le contraire, et actif sous
     EmuTOS. Le manuel de la TwiSTer donne la raison : "TOS 2.06 sets
     the machine cookie differently when operating the 68020 in the
     1040 STE", donc l'interrupteur de cache du bureau ne parvient pas
     au processeur. Remedes du constructeur : environment setter,
     patch de TOS 2.06, ou EmuTOS. Option -C ajoutee pour l'activer
     nous-memes le temps de l'execution (CACR restaure en sortant).
   Reste un facteur ~3 avec le 68030 a 32 MHz emule par Hatari, ce qui
   n'a rien d'anormal : horloge, largeur de bus et taille des caches.
   A verifier aussi : la frequence reelle du processeur depend du
   cavalier 4 et de SPEED.PRG (manuel, chapitre 2.4).
2. REGLE, ET VALIDE SUR MATERIEL. tests/I2CTEST.PY et tests/RTCTEST.PY
   passent sur la machine, avec la sonde inoffensive de 0x2C.
   Les registres sont bien ceux de la MonSTer. i2c.scan() rend
   trois adresses sur cette machine :
     0x68  DS3231 (et non DS1307 : le manuel de la carte le confirme)
     0x57  EEPROM AT24C32 de 4 Ko, sur le meme module d'horloge
     0x2C  INCONNU, ni le manuel ni le module ne l'expliquent
   readfrom_mem() fonctionne, donc repeated-start et ACK aussi.
   Scan complet des 128 adresses en 150 ms.
3. REGLE, et par la machine : SCL EST relisable. Le port le sonde au
   demarrage et bascule alors SCL en collecteur ouvert, ce qui active le
   clock stretching de extmod. i2c.scan() confirme : "SCL readable,
   clock stretching on".
4. Collision sur le bus I2C : le bus fonctionne sous Geneva, mais le
   risque theorique demeure. RTCTIME.PRG s'accroche aux appels XBIOS
   Gettime/Settime, donc un programme qui demande l'heure pendant une
   de nos transactions declencherait un acces concurrent. Une parade
   existe -- masquer les interruptions le temps de la transaction --
   mais elle ferait perdre des tics a l'horloge systeme, qui en perd
   deja (voir ci-dessous). Non traite, juge peu probable.
8. L'horloge GEMDOS derive : ~2 secondes de retard sur le DS3231
   constatees. Normal, elle est entretenue par l'interruption a 200 Hz
   et tout ce qui masque les interruptions lui fait perdre des tics.
   os.settime() permet de la recaler depuis Python.
5. PRGFLAGS : resolu. L'editeur de liens met deja 0x07 (fastload, +
   programme charge en alt-RAM, + Malloc peut rendre de l'alt-RAM) et le
   Makefile les repose explicitement. Pour le test DMA du point 1 :
   make PRGFLAGS=0x01, tout reste alors en ST-RAM sauf notre tas Mxalloc.

TOUS LES TESTS D'UN COUP : tests/ALLTEST.PY
-------------------------------------------
  MPY20.TTP ALLTEST.PY                 automatiques, sans risque
  MPY20.TTP ALLTEST.PY i2c disk dma    + tests de materiel (TwiSTer, IDE)
  MPYRUN.PRG puis ALLGEM.PY            + GEMTEST, WINTEST, MENUTEST
  MPY20.TTP ALLTEST.PY SMOKE BENCH     seulement ceux-la
  hatari-test.sh ALLTEST.PY auto       sous Hatari (gestes GEM simules)
Chaque test tourne dans le meme interpreteur, dans son propre espace de
noms ; un print() intercepte compte les lignes ok / NON / ECHEC / BILAN.
SNDTEST est saute sans son DMA (_SND), NATTEST sans NATMOD.MPY. A la fin :
un tableau, et UNE LIGNE AJOUTEE A ALLTEST.SUM (configuration, version,
resultats, chiffres de BENCH) : une ligne par configuration essayee.
Ne JAMAIS mettre i2c ailleurs que sur la TwiSTer (bombes).

A SAVOIR
--------
- PIEGE alignement m68k, corrige le 02/10/2026 (trouve par ALLTEST.PY) :
  gcc m68k n'aligne la pile que sur 2 octets, et GEMDOS rend des blocs a
  4n+2 sur un ST. Or la VM range deux drapeaux dans les bits bas de ses
  pointeurs de pile (MP_TAGPTR_MAKE, blocs try/with) : un "return" depuis
  un "try" donnait une Address Error sur 68000. Trois corrections :
  1. le tas du GC est aligne sur 16 octets (main.c) ;
  2. alloca, qui loge l'etat des petites fonctions, arrondit a 4
     (mpconfigport.h ; PAS __builtin_alloca_with_align, voir 3) ;
  3. le GC parcourt la pile C aux DEUX decalages, 4n et 4n+2 (gc_collect,
     main.c) : un pointeur local peut etre a l'un ou a l'autre. Sans ca,
     le point 2 faisait liberer des objets vivants (bus error sur TT apres
     GCTEST puis FSTEST). Le depot 1.28 a les memes defauts, latents.
  Reste theorique : un pointeur a 4n+2 DANS une structure du tas (champ
  apres un uint16_t) echapperait au GC, qui lit le tas par mots de 4.
- PIEGE MiNTlib/strcmp, corrige le 18/09/2026 : le binaire faisait 2,4 Mo
  au lieu de 485 Ko. MicroPython compresse ses messages d'erreur et compte
  sur gcc pour reduire un arbre de strcmp() a une constante a chaque
  MP_ERROR_TEXT. Or string.h de la MiNTlib definit strcmp (et strlen) en
  asm volatile des que __OPTIMIZE__ est defini, et gcc ne simplifie rien a
  travers de l'asm : chaque message traine 210 boucles de comparaison
  (7 a 23 Ko par site). Correctif d'une ligne dans py/misc.h,
  __builtin_strcmp au lieu de strcmp. Valide sous Hatari (SMOKE, PFTEST,
  SNDTEST).
- MagiC : tout passe sauf OSTEST (EACCES), faute du TEST, corrigee : il
  effacait un fichier reste ouvert (open(...).read() sans close ;
  MicroPython ne compte pas les references). TOS laisse faire, MagiC
  refuse, a raison. platform lit la vraie version de MagiC (cookie MagX
  -> AESVARS : magic +0, date JJMMAAAA en BCD +16, version +48, etat
  +50) ; uname().release donnait "2.00", l'en-tete TOS que MagiC imite.
  Valide sur la machine le 21/09/2026.
- DEMARRAGE : sur le STE, depuis l'entree dans main, 570 ms a 8 MHz et
  385 ms a 24 MHz (1,5 s avant le 28/09/2026). L'autotest du tas
  (1,2 s a 8 MHz sur le STE) n'est plus fait qu'avec -H ; la mise a zero
  du tas est supprimee, le GC remettant chaque bloc a zero en l'allouant
  (MICROPY_GC_CONSERVATIVE_CLEAR). -t detaille chaque etape.
- VITESSE ET RESIDENTS : STinG avec une IP configuree ralentit une boucle
  Python de 11,5 % (~10 % du processeur pris en tache de fond, 29/09) ;
  NVDI et la pile USB, moins de 0,2 %. Mesurer sans STinG.
- VITESSE : 216 us par tour de boucle vide sur le STE a 16 MHz (TOS 2.06,
  I-cache actif, alt-RAM), identique avant et apres la reduction du
  binaire. Verifier le mode 8/16 MHz avant toute mesure : a 8 MHz on
  tombe a ~500 us, ce qui a failli passer pour une regression.
- MagiC sur la TwiSTer : ALTRAMON.PRG doit etre lance APRES MAGXBOOT.
  MAGXBOOT impose le debut de la FastRAM a $01000000 (convention TT,
  magxboot.s:153) ; la TwiSTer met la sienne juste au-dessus de la ST-RAM,
  donc MagiC ne la voit pas (magibios.s:1939). Lance apres, ALTRAMON
  fait son Maddalt aupres de MagiC, qui l'accepte (Bmaddalt).
- Fichiers a tester sur la machine : ~/Downloads/ToTest/UPYTHON/, avec
  les consignes dans ~/Downloads/ToTest/todo_upython.txt. Ce dossier est
  partage avec les autres projets Atari : n'y toucher qu'a UPYTHON/ et a
  todo_upython.txt.
- Tests automatiques sous Hatari : ports/atari/hatari-test.sh SCRIPT.PY
  [args]. Temporaires dans /tmp (mpy-hdtest, mpy-hatari.out) ; BIN=
  pour tester un autre binaire. Lance Hatari sans intervention via RUNTEST.PRG (tools/
  runtest.c, make -f Makefile.tools) et affiche le journal.
  ACSI=image.img monte une image de disque en C: (le disque GEMDOS passe
  en D:) : seul moyen d'avoir un peripherique BIOS sous l'emulateur.
  Image FAT16 : newfs_msdos -F 16 -s <secteurs> -o 1 -S 512 sur un
  fichier cree par dd, puis une table MBR (type 0x06, debut secteur 1)
  ajoutee devant.
  Commandes vers Hatari depuis un test : ecrire la commande dans
  C:\HATARI.CMD (touche : "hatari-event keypress 28", capture :
  "hatari-shortcut screenshot" -> /tmp/mpy-shots). MONITOR=mono pour
  640 x 400 ; TOS=.../tos206fr.img pour TOS 2.06 ; EXTRA= pour copier un
  script de /tmp ; VBLS= si le test manque de temps (mono : 71 Hz).
  Etat au 19/09/2026, binaire de 485 Ko : SMOKE, PFTEST, FSTEST,
  OSTEST force, SNDTEST (TT 12/12, STE 68020 11/11) et DISKTEST (10/10
  sur image ACSI de 64 Mo, forme longue de Rwabs comprise) passent.
- Le journal (-l) est ecrit par GEMDOS avec un cache en ecriture
  differee : le fichier peut faire 0 octet juste apres l'execution et
  n'etre reellement ecrit qu'au lancement du programme suivant. Ne pas
  retirer la carte CF entre les deux.
- FENETRE QUI NE SE FERME PAS : ce n'est PAS le port, c'est Geneva ou
  Teradesk. Constat de Fabrice sur la machine : sous TOS seul, sans
  Geneva ni Teradesk, EmuCON2 reprend la main et MPY.TTP se ferme
  normalement, dans toutes les combinaisons. Le programme sortait deja
  proprement de son cote (le journal se termine par "[exit] status=0"),
  c'est bien le gestionnaire de fenetres qui garde la console ouverte.
  Rien a corriger ici ; on ne cherche plus.

OPTIONS DU BINAIRE
------------------
  -q           rien a l'ecran, tout dans le journal (-l) : pour les
               programmes GEM, dont le dessin serait sinon recouvert
  -t           duree de chaque etape du demarrage, en ms
  -H           autotest du tas au demarrage (ecriture/relecture du Mo) :
               pour diagnostiquer une carte memoire douteuse
  -m <ko>      taille du tas (defaut 1024)
  -s           tas en ST-RAM meme si la RAM alternative existe
  -C           active le cache d'instructions du 68020 pendant l'execution
  -l <fichier> journalise tout l'affichage dans un fichier, en UTF-8
               (c'est un doublement, l'ecran continue d'afficher sauf -q)
  Les options se donnent dans n'importe quel ordre, avant le script.
  -c "<code>"  execute du Python et sort
  <fichier.py> execute un script

CONSTRUCTION
------------
Un changement dans mpconfigport.h demande un "make clean" : les
en-tetes generes (genhdr/moduledefs.h) ne suivent pas la config.

  cd mpy-cross && make            # une fois : compilateur .py -> .mpy
  git submodule update --init --depth 1 lib/micropython-lib   # une fois
  cd ports/atari && make              # build/MPY20.TTP        (68020+)
  cd ports/atari && make CPU=68000    # build-68000/MPY00.TTP  (68000)
  cd ports/atari && make -f Makefile.tools   # MPYRUN.PRG, RUNTEST.PRG...
Toolchain dans le PATH pour mpy-cross-atari/natmod : /opt/cross-mint/bin.
