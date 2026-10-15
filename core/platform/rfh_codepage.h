// rfh_codepage.h - code page services used by the shared RFHUtil sources.
//
// Windows keeps the original Win32/CRT calls, so behaviour there is unchanged.
// Linux reproduces the Windows results with iconv.
#pragma once

#include "rfh_platform.h"

// The ANSI code page used for display translation.
// Windows: GetACP(). Linux: the RFHUTIL_ACP environment variable if it holds a
// positive number, otherwise 1252 (Western European, the common Windows default).
int rfh_get_acp();

// TRUE if conversions to and from the code page are available on this system.
BOOL rfh_is_codepage_installed(int ccsid);

// The code page arguments may be Windows code page numbers or IBM CCSIDs. Linux also
// accepts IBM CCSIDs that Windows does not (for example 273). This is intentional,
// display-only, and does not affect message or file bytes.
//
// Translates the single byte input[0] from fromCcsid to toCcsid.
// Returns the number of bytes written to output (at most outputMax), or 0 when a
// code page is not positive or not available. A character that the target code
// page cannot represent becomes '.'. Windows WideCharToMultiByte may instead choose a
// "best fit" character (for example 1252 A-grave to 437 'A'), where Linux gives '.'.
// output must be non-NULL: there is no size-query mode.
int rfh_convert_char(int fromCcsid, int toCcsid, const char * input, char * output, int outputMax);

// NUL-terminated UCS-2 (host byte order) to single-byte text, with the semantics of
// the MSVC C-locale wcstombs that RFHUtil has always used: code units 0-255 map to
// the byte of the same value. Returns the bytes written excluding the terminator,
// or -1 if a code unit is above 255. Like wcstombs, stops after mbsBytes
// bytes without looking further, and then writes no terminator. Both buffers must be non-NULL.
int rfh_ucs2_to_c_locale(const RFH_UCS2 * ucs, char * mbs, int mbsBytes);

// NUL-terminated single-byte text to UCS-2 with the semantics of the MSVC C-locale
// mbstowcs: each byte maps to the code unit of the same value. Converts at most
// ucsChars code units and returns the count excluding the terminator. Writes a
// terminator when there is room.
int rfh_c_locale_to_ucs2(const char * mbs, RFH_UCS2 * ucs, int ucsChars);
