// stdafx.h - non-Windows stand-in for RFHUtil/StdAfx.h.
//
// The shared RFHUtil sources start with #include "stdafx.h", which on Windows
// pulls in MFC. Linux file names are case-sensitive, so the real StdAfx.h is not
// found and this header supplies the portable equivalents instead.
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "rfh_platform.h"
