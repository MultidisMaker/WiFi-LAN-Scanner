#include "Oui.h"

#if defined(WLS_OUI_EMBEDDED) && __has_include("OuiData.gen.inc")
#include "OuiData.gen.inc"
#else
OuiTable embeddedOuiTable() { return {}; }
#endif
