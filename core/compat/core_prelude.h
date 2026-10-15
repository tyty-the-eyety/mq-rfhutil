// core_prelude.h - force-included before each shared RFHUtil source on non-Windows
// builds (the same technique tests/UnitTests/prelude.h uses on Windows).
//
// Several shared sources include RFHUtil/rfhutil.h, which pulls in MFC and
// DataArea.h. Defining that header's include guard skips its body; the shared
// sources only needed Names.h from that include chain.
#pragma once

#include "stdafx.h"
#include "Names.h"

#define AFX_RFHUTIL_H__96F2439F_AE02_4650_8A3E_EE7F0EC7E72C__INCLUDED_
