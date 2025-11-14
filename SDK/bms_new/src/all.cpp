#ifdef BMS

#include <algorithm>
#include <cctype>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define NOMINMAX
#include <direct.h>
#include <windows.h>

#include "basetypes.h"
#include "characterset.h"
#include "Color.h"
#include "commonmacros.h"
#include "checksum_md5.h"
#include "filesystem.h"
#include "generichash.h"
#include "tier0/dbg.h"
#include "tier0/mem.h"
#include "icvar.h"
#include "tier1/characterset.h"
#include "tier1/convar.h"
#include "tier1/convar_serverbounded.h"
#include "tier1/interface.h"
#include "tier1/strtools.h"
#include "tier1/utlhash.h"
#include "tier1/utlsortvector.h"
#include "tier1/utlstring.h"
#include "tier1/utlvector.h"
#include "tier1/utlbuffer.h"
#include <KeyValues.h>
#include <vstdlib/IKeyValuesSystem.h>
#include "memdbgon.h"


#include "characterset.cpp"
#include "checksum_md5.cpp"
#include "cpu.cpp"
#include "cvar.cpp"
#include "generichash.cpp"
#include "lib.cpp"
#include "strtools.cpp"
#include "KeyValues.cpp"
#include "utlbuffer.cpp"
#include "utlstring.cpp"

#endif
