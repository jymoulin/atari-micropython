# Frozen modules for the Atari TOS port.
#
# Anything needing sockets (urequests, smtplib, email) waits for phase 5.
#
# We freeze base64.py directly (not via require()) to avoid pulling the
# binascii.py shim from micropython-lib, which would shadow the C binascii
# module.

freeze("$(MPY_LIB_DIR)/python-stdlib/base64", "base64.py")
freeze("$(PORT_DIR)/modules", "datetime.py")

freeze("$(PORT_DIR)/modules", "_atpath.py")
freeze("$(PORT_DIR)/modules", "os.py")
freeze("$(PORT_DIR)/modules", "platform.py")
freeze("$(PORT_DIR)/modules", "gem.py")

# --- pas encore portes ---
# freeze("$(PORT_DIR)/modules", "gzip.py")
# freeze("$(PORT_DIR)/modules", "zlib.py")
# freeze("$(PORT_DIR)/modules", "zipfile.py")

# --- phase 5 (needs modsocket.c) ---
# freeze("$(PORT_DIR)/modules", "urequests.py")
# freeze("$(PORT_DIR)/modules", "smtplib.py")
# freeze("$(PORT_DIR)/modules", "email/__init__.py")
# freeze("$(PORT_DIR)/modules", "email/utils.py")
# freeze("$(PORT_DIR)/modules", "email/header.py")
# freeze("$(PORT_DIR)/modules", "email/encoders.py")
# freeze("$(PORT_DIR)/modules", "email/message.py")
# freeze("$(PORT_DIR)/modules", "email/mime/__init__.py")
# freeze("$(PORT_DIR)/modules", "email/mime/base.py")
# freeze("$(PORT_DIR)/modules", "email/mime/text.py")
# freeze("$(PORT_DIR)/modules", "email/mime/multipart.py")
# freeze("$(PORT_DIR)/modules", "email/mime/application.py")
