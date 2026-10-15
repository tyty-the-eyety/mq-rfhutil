// rfh_codepage.cpp - see rfh_codepage.h

#include "rfh_codepage.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

int rfh_get_acp()
{
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
		};

		std::vector<std::string> names;
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
		thread_local std::map<std::pair<int, int>, iconv_t> cache;

		auto key = std::make_pair(fromCcsid, toCcsid);
		auto it = cache.find(key);
		if (it != cache.end())
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

		cache[key] = cd;
		return cd;
	}
}

int rfh_get_acp()
{
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

	iconv_t cd = converterFor(fromCcsid, toCcsid);
	if (cd == (iconv_t)-1)
	{
		return 0;
	}

	// reset any shift state left by a previous call
	iconv(cd, NULL, NULL, NULL, NULL);

	char in[1] = { input[0] };
	char * inPtr = in;
	size_t inLeft = 1;
	char * outPtr = output;
	size_t outLeft = (size_t)outputMax;

	if (iconv(cd, &inPtr, &inLeft, &outPtr, &outLeft) == (size_t)-1)
	{
		if (errno == EILSEQ)
		{
			// not representable: Windows substitutes the default character
			output[0] = '.';
			return 1;
		}

		return 0;
	}

	return (int)((size_t)outputMax - outLeft);
}

int rfh_ucs2_to_c_locale(const RFH_UCS2 * ucs, char * mbs, int mbsBytes)
{
	int count = 0;

	while (ucs[count] != 0)
	{
		if (ucs[count] > 0xFF)
		{
			return -1;
		}

		if (count >= mbsBytes)
		{
			return count;
		}

		mbs[count] = (char)ucs[count];
		count++;
	}

	if (count < mbsBytes)
	{
		mbs[count] = 0;
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

#endif
