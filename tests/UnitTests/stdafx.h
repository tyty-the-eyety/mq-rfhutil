// stdafx.h — minimal stub for the unit test project
// Overrides RFHUtil/stdafx.h so source files compile without the full
// application headers (no MQ, no DataArea, no resource.h).
// Windows uses MFC; other platforms use the portable core compat header.
#pragma once

#ifdef _WIN32
#define VC_EXTRALEAN
#include "targetver.h"
#include <afxwin.h>
#else
#include "../../core/compat/stdafx.h"
#endif
