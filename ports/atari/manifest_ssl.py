# Frozen modules for MPY20SSL.TTP (make SSL=1): everything of MPY20.TTP,
# plus the CPython-style ssl wrapper over the tls module. requests uses tls
# directly, so HTTPS works with it as is.

include("$(PORT_DIR)/manifest.py")
require("ssl")
