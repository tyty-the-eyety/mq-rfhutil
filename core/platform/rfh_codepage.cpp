// rfh_codepage.cpp - see rfh_codepage.h

#include "rfh_codepage.h"

#include <stdlib.h>
#include <string.h>

namespace
{
	// set by RfhAcpScope; 0 when no scope is active on this thread
	thread_local int acpOverride = 0;
}

RfhAcpScope::RfhAcpScope(int acp) : m_previous(acpOverride)
{
	if (acp > 0)
	{
		acpOverride = acp;
	}
}

RfhAcpScope::~RfhAcpScope()
{
	acpOverride = m_previous;
}

#ifdef _WIN32
#include <mbstring.h>

int rfh_get_acp()
{
	if (acpOverride > 0)
	{
		return acpOverride;
	}
	return (int)GetACP();
}

namespace
{
	BOOL ccsidInstalled;		// result of checking if code page is installed
	int ccsidInsCheck;			// code page to search for

	BOOL CALLBACK EnumCodePagesProc(LPSTR lpCodePageString)
	{
		// check if this is the code page we are looking for
		if (atoi(lpCodePageString) == ccsidInsCheck)
		{
			// remember we found the one we wanted
			ccsidInstalled = TRUE;
		}

		// continue if not found yet
		return !ccsidInstalled;
	}
}

BOOL rfh_is_codepage_installed(int ccsid)
{
	ccsidInstalled = FALSE;
	ccsidInsCheck = ccsid;

	// calls back to EnumCodePagesProc for each installed code page
	EnumSystemCodePagesA(&EnumCodePagesProc, CP_INSTALLED);

	return ccsidInstalled;
}

int rfh_convert_char(int fromCcsid, int toCcsid, const char * input, char * output, int outputMax)
{
	int tcount = 0;
	char defChar = '.';
	wchar_t ucsChar = 0;

	if ((fromCcsid > 0) && (toCcsid > 0) && (outputMax > 0))
	{
		// translate to UCS-2 first
		tcount = MultiByteToWideChar(fromCcsid, 0, input, 1, &ucsChar, 1);

		if (tcount > 0)
		{
			// then to the target code page
			tcount = WideCharToMultiByte(toCcsid, 0, &ucsChar, 1, output, outputMax, &defChar, NULL);
		}
	}

	return tcount;
}

int rfh_ucs2_to_c_locale(const RFH_UCS2 * ucs, char * mbs, int mbsBytes)
{
	return (int)wcstombs(mbs, ucs, mbsBytes);
}

int rfh_c_locale_to_ucs2(const char * mbs, RFH_UCS2 * ucs, int ucsChars)
{
	return (int)mbstowcs(ucs, mbs, ucsChars);
}

int rfh_WideCharToMultiByte(int codepage, unsigned int flags, const RFH_UCS2 * in, int inChars,
	char * out, int outBytes, const char * defaultChar, BOOL * usedDefaultChar)
{
	if ((inChars < 0) || (outBytes <= 0))
	{
		return 0;
	}
	return WideCharToMultiByte((UINT)codepage, flags, in, inChars, out, outBytes, defaultChar, usedDefaultChar);
}

int rfh_MultiByteToWideChar(int codepage, unsigned int flags, const char * in, int inBytes,
	RFH_UCS2 * out, int outChars)
{
	if ((inBytes < 0) || (outChars <= 0))
	{
		return 0;
	}
	return MultiByteToWideChar((UINT)codepage, flags, in, inBytes, out, outChars);
}

int rfh_mbclen(const unsigned char * text)
{
	return (int)_mbclen(text);
}

#else

#include <errno.h>
#include <iconv.h>
#include <stdio.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
	// converterFor key for UCS-2 in host byte order; CCSIDs are positive
	const int kHostUcs2 = -1;

	// Windows maps these five bytes of 1252 to the C1 control of the same value; glibc
	// rejects them.
	bool isUndefined1252(unsigned int value)
	{
		return (value == 0x81) || (value == 0x8D) || (value == 0x8F) || (value == 0x90) || (value == 0x9D);
	}

	// Writes out the converter's pending shift state or held-back character after the
	// last input. False if it does not fit.
	bool flushConverter(iconv_t cd, char ** outPtr, size_t * outLeft)
	{
		return (iconv(cd, NULL, NULL, outPtr, outLeft) != (size_t)-1) || (errno != E2BIG);
	}

	// iconv names to try for an IBM CCSID, most specific first.
	std::vector<std::string> iconvNames(int ccsid)
	{
		static const std::map<int, const char *> specific = {
			{ 367, "ASCII" },
			{ 813, "ISO-8859-7" },
			{ 819, "ISO-8859-1" },
			{ 912, "ISO-8859-2" },
			{ 915, "ISO-8859-5" },
			{ 916, "ISO-8859-8" },
			{ 920, "ISO-8859-9" },
			{ 923, "ISO-8859-15" },
			{ 1089, "ISO-8859-6" },
			{ 1208, "UTF-8" },
			// Windows ANSI double-byte code pages, not glibc's IBM variants (IBM932 for
			// 932 differs from Windows-31J, for example in the NEC row 13 characters)
			{ 932, "CP932" },
			{ 936, "CP936" },
			{ 949, "CP949" },
			{ 950, "CP950" },
			// Windows code page numbers that differ from the IBM CCSID
			{ 20127, "ASCII" },
			{ 65001, "UTF-8" },
			{ 28591, "ISO-8859-1" },
			{ 28592, "ISO-8859-2" },
			{ 28593, "ISO-8859-3" },
			{ 28594, "ISO-8859-4" },
			{ 28595, "ISO-8859-5" },
			{ 28596, "ISO-8859-6" },
			{ 28597, "ISO-8859-7" },
			{ 28598, "ISO-8859-8" },
			{ 28599, "ISO-8859-9" },
			{ 28603, "ISO-8859-13" },
			{ 28605, "ISO-8859-15" },
			{ 20273, "IBM273" },
			{ 20277, "IBM277" },
			{ 20278, "IBM278" },
			{ 20280, "IBM280" },
			{ 20284, "IBM284" },
			{ 20285, "IBM285" },
			{ 20290, "IBM290" },
			{ 20297, "IBM297" },
			{ 20420, "IBM420" },
			{ 20423, "IBM423" },
			{ 20424, "IBM424" },
			{ 20871, "IBM871" },
			{ 20880, "IBM880" },
			{ 20905, "IBM905" },
			{ 21025, "IBM1025" },
			// glibc has no IBM924. IBM-924 is 1047 with the Latin-9 substitutions, so about 8
			// code points differ (euro, S/s/Z/z caron, OE/oe, Y diaeresis replace
			// currency, broken bar, diaeresis, acute, cedilla, 1/4, 1/2, 3/4).
			{ 20924, "IBM1047" },
		};

		std::vector<std::string> names;
		if (ccsid == kHostUcs2)
		{
			names.push_back((__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) ? "UTF-16LE" : "UTF-16BE");
			return names;
		}
		auto it = specific.find(ccsid);
		if (it != specific.end())
		{
			names.push_back(it->second);
		}

		char buf[32];
		snprintf(buf, sizeof(buf), "IBM%03d", ccsid);	// IBM037, IBM500, IBM1047, IBM1140
		names.push_back(buf);
		snprintf(buf, sizeof(buf), "CP%d", ccsid);		// CP437, CP850, CP1252
		names.push_back(buf);
		return names;
	}

	// Cached converters, one per thread because an iconv_t is not thread-safe.
	// Failures are cached as (iconv_t)-1 so a missing code page is probed once.
	iconv_t converterFor(int fromCcsid, int toCcsid)
	{
		struct Cache
		{
			std::map<std::pair<int, int>, iconv_t> handles;

			~Cache()
			{
				for (auto & entry : handles)
				{
					if (entry.second != (iconv_t)-1)
					{
						iconv_close(entry.second);
					}
				}
			}
		};
		thread_local Cache cache;

		auto key = std::make_pair(fromCcsid, toCcsid);
		auto it = cache.handles.find(key);
		if (it != cache.handles.end())
		{
			return it->second;
		}

		iconv_t cd = (iconv_t)-1;
		for (const std::string & to : iconvNames(toCcsid))
		{
			for (const std::string & from : iconvNames(fromCcsid))
			{
				cd = iconv_open(to.c_str(), from.c_str());
				if (cd != (iconv_t)-1)
				{
					break;
				}
			}

			if (cd != (iconv_t)-1)
			{
				break;
			}
		}

		cache.handles[key] = cd;
		return cd;
	}
}

int rfh_get_acp()
{
	if (acpOverride > 0)
	{
		return acpOverride;
	}

	const char * env = getenv("RFHUTIL_ACP");
	if (env != NULL)
	{
		int acp = atoi(env);
		if (acp > 0)
		{
			return acp;
		}
	}

	return 1252;
}

BOOL rfh_is_codepage_installed(int ccsid)
{
	if (ccsid <= 0)
	{
		return FALSE;
	}

	// a code page is usable when it converts to and from UTF-8
	return ((converterFor(ccsid, 1208) != (iconv_t)-1) && (converterFor(1208, ccsid) != (iconv_t)-1)) ? TRUE : FALSE;
}

int rfh_convert_char(int fromCcsid, int toCcsid, const char * input, char * output, int outputMax)
{
	if ((fromCcsid <= 0) || (toCcsid <= 0) || (outputMax <= 0))
	{
		return 0;
	}

	// the same two steps as Windows: to UCS-2, then to the target code page
	RFH_UCS2 ucs[2] = { 0, 0 };
	int units = rfh_MultiByteToWideChar(fromCcsid, 0, input, 1, ucs, 2);
	if (units <= 0)
	{
		return 0;
	}
	return rfh_WideCharToMultiByte(toCcsid, 0, ucs, units, output, outputMax, ".", NULL);
}

int rfh_ucs2_to_c_locale(const RFH_UCS2 * ucs, char * mbs, int mbsBytes)
{
	int count = 0;

	// like wcstombs: stop after mbsBytes bytes without looking further
	while (count < mbsBytes)
	{
		if (ucs[count] > 0xFF)
		{
			return -1;
		}

		mbs[count] = (char)ucs[count];

		if (ucs[count] == 0)
		{
			return count;
		}

		count++;
	}

	return count;
}

int rfh_c_locale_to_ucs2(const char * mbs, RFH_UCS2 * ucs, int ucsChars)
{
	int count = 0;

	while ((mbs[count] != 0) && (count < ucsChars))
	{
		ucs[count] = (RFH_UCS2)(unsigned char)mbs[count];
		count++;
	}

	if (count < ucsChars)
	{
		ucs[count] = 0;
	}

	return count;
}

int rfh_WideCharToMultiByte(int codepage, unsigned int flags, const RFH_UCS2 * in, int inChars,
	char * out, int outBytes, const char * defaultChar, BOOL * usedDefaultChar)
{
	(void)flags;
	if ((codepage <= 0) || (inChars < 0) || (outBytes <= 0))
	{
		return 0;
	}
	iconv_t cd = converterFor(kHostUcs2, codepage);
	if (cd == (iconv_t)-1)
	{
		return 0;
	}
	iconv(cd, NULL, NULL, NULL, NULL);
	if (usedDefaultChar != NULL)
	{
		*usedDefaultChar = FALSE;
	}

	// one character at a time, so that each unrepresentable one becomes the default
	char * outPtr = out;
	size_t outLeft = (size_t)outBytes;
	for (int i = 0; i < inChars; i++)
	{
		RFH_UCS2 units[2] = { in[i], 0 };
		size_t inLeft = sizeof(RFH_UCS2);
		size_t defaults = 1;		// default characters for this input if it is unrepresentable

		if ((codepage == 1252) && isUndefined1252(units[0]))
		{
			if (outLeft == 0)
			{
				return 0;
			}
			*outPtr++ = (char)units[0];
			outLeft--;
			continue;
		}

		if ((units[0] >= 0xD800) && (units[0] <= 0xDBFF) && (i + 1 < inChars) && (in[i + 1] >= 0xDC00) && (in[i + 1] <= 0xDFFF))
		{
			units[1] = in[i + 1];
			inLeft = 2 * sizeof(RFH_UCS2);
			defaults = 2;		// Windows substitutes each 16-bit unit of an unmappable pair
			i++;
		}

		char * inPtr = (char *)units;
		if (iconv(cd, &inPtr, &inLeft, &outPtr, &outLeft) == (size_t)-1)
		{
			if (errno == E2BIG)
			{
				return 0;
			}
			// EILSEQ: not representable; EINVAL: half of a surrogate pair
			iconv(cd, NULL, NULL, NULL, NULL);
			if (outLeft < defaults)
			{
				return 0;
			}
			for (size_t d = 0; d < defaults; d++)
			{
				*outPtr++ = (defaultChar != NULL) ? defaultChar[0] : '?';
				outLeft--;
			}
			if (usedDefaultChar != NULL)
			{
				*usedDefaultChar = TRUE;
			}
		}
	}

	if (!flushConverter(cd, &outPtr, &outLeft))
	{
		return 0;
	}
	return (int)((size_t)outBytes - outLeft);
}

int rfh_MultiByteToWideChar(int codepage, unsigned int flags, const char * in, int inBytes,
	RFH_UCS2 * out, int outChars)
{
	if ((codepage <= 0) || ((flags != 0) && (flags != MB_PRECOMPOSED)) || (inBytes < 0) || (outChars <= 0))
	{
		return 0;
	}
	iconv_t cd = converterFor(codepage, kHostUcs2);
	if (cd == (iconv_t)-1)
	{
		return 0;
	}
	iconv(cd, NULL, NULL, NULL, NULL);

	char * inPtr = (char *)in;
	size_t inLeft = (size_t)inBytes;
	char * outPtr = (char *)out;
	size_t outLeft = (size_t)outChars * sizeof(RFH_UCS2);

	if (codepage == 1252)
	{
		// single-byte: convert a byte at a time to place the five undefined bytes
		while (inLeft > 0)
		{
			if (isUndefined1252((unsigned char)*inPtr))
			{
				if (outLeft < sizeof(RFH_UCS2))
				{
					return 0;
				}
				*(RFH_UCS2 *)outPtr = (RFH_UCS2)(unsigned char)*inPtr;
				outPtr += sizeof(RFH_UCS2);
				outLeft -= sizeof(RFH_UCS2);
				inPtr++;
				inLeft--;
				continue;
			}

			size_t one = 1;
			if (iconv(cd, &inPtr, &one, &outPtr, &outLeft) == (size_t)-1)
			{
				if (errno == E2BIG)
				{
					return 0;
				}
				break;
			}
			inLeft--;
		}
	}
	else if ((iconv(cd, &inPtr, &inLeft, &outPtr, &outLeft) == (size_t)-1) && (errno == E2BIG))
	{
		return 0;
	}

	if (!flushConverter(cd, &outPtr, &outLeft))
	{
		return 0;
	}
	return (int)(((size_t)outChars * sizeof(RFH_UCS2) - outLeft) / sizeof(RFH_UCS2));
}

int rfh_mbclen(const unsigned char * text)
{
	unsigned char ch = text[0];
	switch (rfh_get_acp())
	{
	case 932:	// Shift-JIS
		return (((ch >= 0x81) && (ch <= 0x9F)) || ((ch >= 0xE0) && (ch <= 0xFC))) ? 2 : 1;
	case 936:	// GBK
	case 949:	// Unified Hangul
	case 950:	// Big5
		return ((ch >= 0x81) && (ch <= 0xFE)) ? 2 : 1;
	default:
		return 1;
	}
}

#endif
