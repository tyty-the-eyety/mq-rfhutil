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

#ifndef _WIN32
// Win32 flag values. Linux accepts both and ignores them: iconv produces
// precomposed characters.
#define WC_DISCARDNS	0x0010
#define WC_DEFAULTCHAR	0x0040
#endif

// WideCharToMultiByte for UCS-2 in host byte order. Converts inChars code units
// (inChars >= 0) to codepage and returns the bytes written, or 0 when the code page
// is not available or the output does not fit in outBytes (Windows returns 0 with
// ERROR_INSUFFICIENT_BUFFER). There is no size-query mode: outBytes must be
// positive. On Linux a character the code page cannot represent becomes
// defaultChar[0] ('?' when defaultChar is NULL) and sets *usedDefaultChar when that
// is non-NULL; Windows may choose a "best fit" character instead.
int rfh_WideCharToMultiByte(int codepage, unsigned int flags, const RFH_UCS2 * in, int inChars,
	char * out, int outBytes, const char * defaultChar, BOOL * usedDefaultChar);

// MultiByteToWideChar to UCS-2 in host byte order. Converts inBytes bytes
// (inBytes >= 0) and returns the code units written, or 0 when the code page is not
// available or the output does not fit in outChars. flags must be 0. On Linux an
// incomplete or invalid sequence ends the conversion, and the code units converted
// before it are returned.
int rfh_MultiByteToWideChar(int codepage, unsigned int flags, const char * in, int inBytes,
	RFH_UCS2 * out, int outChars);

// _mbclen: the length (1 or 2) of the multibyte character at text in the ANSI code
// page. Linux: 2 for a lead byte of a double-byte ANSI code page (932, 936, 949 or
// 950), otherwise 1.
int rfh_mbclen(const unsigned char * text);
