// SPDX-License-Identifier: Apache-2.0
// rfh_parsed_headers.cpp - see rfh_parsed_headers.h

#include "rfh_parsed_headers.h"

#include <cmqc.h>
#include <cmqpsc.h>
#include <string.h>
#include <stdio.h>

#include <algorithm>
#include <sstream>

#include "rfhconst.h"
#include "rfh_platform.h"
#include "rfh_codepage.h"
#include "comsubs.h"
#include "mqsubs.h"

namespace rfh
{
namespace
{
	MQLONG readLong(const unsigned char * p, bool reverse)
	{
		MQLONG v;
		memcpy(&v, p, sizeof(v));
		return reverse ? reverseBytes4(v) : v;
	}

	void writeLong(unsigned char * p, MQLONG v, bool reverse)
	{
		MQLONG w = reverse ? reverseBytes4(v) : v;
		memcpy(p, &w, sizeof(w));
	}

	void appendLong(std::vector<unsigned char> & out, MQLONG v, bool reverse)
	{
		MQLONG w = reverse ? reverseBytes4(v) : v;
		const unsigned char * p = reinterpret_cast<const unsigned char *>(&w);
		out.insert(out.end(), p, p + 4);
	}

	void appendBytes(std::vector<unsigned char> & out, const void * src, size_t n)
	{
		const unsigned char * p = static_cast<const unsigned char *>(src);
		out.insert(out.end(), p, p + n);
	}

	// Copy at most `maxLen` bytes from `src`, trimming trailing blanks/nuls, and
	// store as a std::string.
	std::string charField(const unsigned char * src, size_t maxLen, bool ebcdic)
	{
		char tmp[64] = { 0 };
		size_t n = (maxLen < sizeof(tmp)) ? maxLen : (sizeof(tmp) - 1);
		if (ebcdic)
			EbcdicToAscii(src, (unsigned int)n, (unsigned char *)tmp);
		else
			memcpy(tmp, src, n);
		tmp[n] = 0;
		// trim trailing blanks and nuls
		int end = (int)n;
		while (end > 0 && (tmp[end - 1] == ' ' || tmp[end - 1] == '\0'))
			--end;
		return std::string(tmp, end);
	}

	void writeCharField(unsigned char * dst, size_t fieldLen, const std::string & src, bool ebcdic)
	{
		// blank-pad to fieldLen
		unsigned char tmp[64];
		size_t n = fieldLen < sizeof(tmp) ? fieldLen : sizeof(tmp) - 1;
		memset(tmp, ' ', n);
		size_t copyLen = src.size() < n ? src.size() : n;
		memcpy(tmp, src.c_str(), copyLen);
		if (ebcdic)
			AsciiToEbcdic(tmp, (unsigned int)n, dst);
		else
			memcpy(dst, tmp, n);
	}

	std::string hexBytes(const unsigned char * src, size_t n)
	{
		std::string r;
		r.resize(n * 2);
		static const char hex[] = "0123456789abcdef";
		for (size_t i = 0; i < n; ++i)
		{
			r[2 * i]     = hex[src[i] >> 4];
			r[2 * i + 1] = hex[src[i] & 0xf];
		}
		return r;
	}

	void unhexBytes(const std::string & hex, unsigned char * dst, size_t n)
	{
		memset(dst, 0, n);
		size_t chars = hex.size() < 2 * n ? hex.size() : 2 * n;
		for (size_t i = 0; i + 1 < chars; i += 2)
		{
			auto nibble = [](char c) -> unsigned char {
				if (c >= '0' && c <= '9') return (unsigned char)(c - '0');
				if (c >= 'a' && c <= 'f') return (unsigned char)(c - 'a' + 10);
				if (c >= 'A' && c <= 'F') return (unsigned char)(c - 'A' + 10);
				return 0;
			};
			dst[i / 2] = (unsigned char)((nibble(hex[i]) << 4) | nibble(hex[i + 1]));
		}
	}

	// Find the text content of <tag>...</tag> within xml[0..len).
	// Returns empty string when the tag is absent.
	std::string xmlGet(const std::string & xml, const char * openTag, const char * closeTag)
	{
		size_t pos = xml.find(openTag);
		if (pos == std::string::npos)
			return {};
		size_t start = pos + strlen(openTag);
		size_t end = xml.find(closeTag, start);
		if (end == std::string::npos)
			return {};
		return xml.substr(start, end - start);
	}

	// Build <tag>value</tag> when value is non-empty.
	std::string xmlPut(const char * tag, const std::string & value)
	{
		if (value.empty())
			return {};
		std::string r;
		r += '<'; r += tag; r += '>';
		r += value;
		r += '<'; r += '/'; r += tag; r += '>';
		return r;
	}

	// Decode folder bytes to UTF-8 given the NameValueCCSID.
	std::string decodeFolder(const unsigned char * bytes, MQLONG len, MQLONG nvCcsid,
	                          std::vector<std::string> & warnings)
	{
		if (len <= 0)
			return {};
		int ctype = getCcsidType(nvCcsid);
		if (ctype == CHAR_EBCDIC)
		{
			std::vector<char> tmp(len + 1, 0);
			EbcdicToAscii(bytes, (unsigned int)len, (unsigned char *)tmp.data());
			return std::string(tmp.data(), len);
		}
		// UCS-2 LE (1200) or UCS-2 BE (1201)
		if (nvCcsid == 1200 || nvCcsid == 1201)
		{
			bool bigEndian = (nvCcsid == 1201);
			if (len % 2 != 0)
			{
				warnings.push_back("RFH2 folder UCS-2 length is odd");
				len &= ~1;
			}
			std::vector<RFH_UCS2> ucs(len / 2 + 1, 0);
			memcpy(ucs.data(), bytes, len);
			// rfh_WideCharToMultiByte expects host-byte-order (LE on x86); swap BE input
			if (bigEndian)
			{
				for (MQLONG i = 0; i < len / 2; ++i)
				{
					unsigned short v;
					memcpy(&v, &ucs[i], 2);
					v = (unsigned short)((v >> 8) | (v << 8));
					memcpy(&ucs[i], &v, 2);
				}
			}
			// query byte count first with a dummy single-char output buffer
			std::vector<char> mb(len * 3 + 4, 0);
			BOOL usedDefault = FALSE;
			int written = rfh_WideCharToMultiByte(1208, 0, ucs.data(), (int)(len / 2),
			                                      mb.data(), (int)mb.size() - 1,
			                                      nullptr, &usedDefault);
			if (written <= 0)
				return {};
			return std::string(mb.data(), written);
		}
		// ASCII / UTF-8 (819, 1208, etc.)
		return std::string(reinterpret_cast<const char *>(bytes), len);
	}

	// Encode folder from UTF-8 to the target NameValueCCSID.
	// Returns the encoded bytes.
	std::vector<unsigned char> encodeFolder(const std::string & utf8, MQLONG nvCcsid,
	                                         std::vector<std::string> & warnings)
	{
		int ctype = getCcsidType(nvCcsid);
		if (ctype == CHAR_EBCDIC)
		{
			std::vector<unsigned char> out(utf8.size());
			AsciiToEbcdic((unsigned char *)utf8.c_str(), (unsigned int)utf8.size(), out.data());
			return out;
		}
		if (nvCcsid == 1200 || nvCcsid == 1201)
		{
			bool bigEndian = (nvCcsid == 1201);
			// Estimate output buffer: each UTF-8 char needs at most one UCS-2 code unit
			std::vector<RFH_UCS2> wc(utf8.size() + 1, 0);
			int wcLen = rfh_MultiByteToWideChar(1208, 0, utf8.c_str(), (int)utf8.size(),
			                                    wc.data(), (int)wc.size() - 1);
			if (wcLen <= 0)
				return {};
			if (bigEndian)
			{
				for (int i = 0; i < wcLen; ++i)
				{
					unsigned short v;
					memcpy(&v, &wc[i], 2);
					v = (unsigned short)((v >> 8) | (v << 8));
					memcpy(&wc[i], &v, 2);
				}
			}
			std::vector<unsigned char> out(wcLen * 2);
			memcpy(out.data(), wc.data(), wcLen * 2);
			return out;
		}
		// ASCII / UTF-8 pass-through
		return std::vector<unsigned char>(utf8.begin(), utf8.end());
		(void)warnings;
	}

	// Wrap encoded folder XML in a length-prefixed block.
	void appendFolder(std::vector<unsigned char> & out, const std::string & utf8, MQLONG nvCcsid,
	                  bool reverse, std::vector<std::string> & warnings)
	{
		auto encoded = encodeFolder(utf8, nvCcsid, warnings);
		if (encoded.empty())
			return;
		appendLong(out, (MQLONG)encoded.size(), reverse);
		out.insert(out.end(), encoded.begin(), encoded.end());
	}

} // anonymous namespace

// ---- DLQ ------------------------------------------------------------------

size_t parseDlq(const unsigned char * data, size_t length, int ccsid, int encoding,
                DlqFields & out, std::vector<std::string> & warnings)
{
	bool ebcdic = (getCcsidType(ccsid) == CHAR_EBCDIC);
	bool reverse = (encoding != NUMERIC_PC);

	if (length < sizeof(MQDLH))
	{
		warnings.push_back("DLQ header too short");
		return 0;
	}
	char strucId[5] = {};
	if (ebcdic)
		EbcdicToAscii(data, 4, (unsigned char *)strucId);
	else
		memcpy(strucId, data, 4);
	if (memcmp(strucId, MQDLH_STRUC_ID, 4) != 0)
		return 0;
	if (readLong(data + 4, reverse) != MQDLH_VERSION_1)
		return 0;

	MQDLH dlh;
	memcpy(&dlh, data, sizeof(dlh));

	out.included   = true;
	out.reason     = readLong(data + 8, reverse);
	out.destQueueName        = charField(data + 12,  48, ebcdic);
	out.destQueueManagerName = charField(data + 60,  48, ebcdic);
	MQLONG enc = readLong(data + 108, reverse);
	out.encoding      = getIntEncode(enc);
	out.pdEncoding    = getPDEncode(enc);
	out.floatEncoding = getFloatEncode(enc);
	out.ccsid         = readLong(data + 112, reverse);
	out.format        = charField(data + 116, 8, ebcdic);
	out.putApplType   = readLong(data + 124, reverse);
	out.putApplName   = charField(data + 128, 28, ebcdic);
	out.putDate       = charField(data + 156,  8, ebcdic);
	out.putTime       = charField(data + 164,  8, ebcdic);

	(void)dlh; (void)warnings;
	return sizeof(MQDLH);
}

void buildDlq(const DlqFields & f, std::vector<unsigned char> & out)
{
	if (!f.included)
		return;

	// Build into a zeroed struct, then append.
	unsigned char buf[sizeof(MQDLH)];
	memset(buf, 0, sizeof(buf));

	bool reverse = (f.encoding != NUMERIC_PC);

	memcpy(buf + 0, MQDLH_STRUC_ID, 4);
	writeLong(buf + 4, MQDLH_VERSION_1, reverse);
	writeLong(buf + 8, (MQLONG)f.reason, reverse);
	writeCharField(buf + 12,  48, f.destQueueName, false);
	writeCharField(buf + 60,  48, f.destQueueManagerName, false);

	MQLONG enc = (MQLONG)(f.encoding == NUMERIC_PC ? MQENC_INTEGER_REVERSED : MQENC_INTEGER_NORMAL);
	enc |= (MQLONG)(f.pdEncoding == NUMERIC_PC ? MQENC_DECIMAL_REVERSED : MQENC_DECIMAL_NORMAL);
	enc |= (MQLONG)(f.floatEncoding == FLOAT_PC   ? MQENC_FLOAT_IEEE_NORMAL :
	                f.floatEncoding == FLOAT_UNIX  ? MQENC_FLOAT_IEEE_REVERSED :
	                f.floatEncoding == FLOAT_390   ? MQENC_FLOAT_S390 :
	                f.floatEncoding == FLOAT_TNS   ? MQENC_FLOAT_TNS :
	                0);
	writeLong(buf + 108, enc, reverse);
	writeLong(buf + 112, (MQLONG)f.ccsid, reverse);
	writeCharField(buf + 116, 8, f.format, false);
	writeLong(buf + 124, (MQLONG)f.putApplType, reverse);
	writeCharField(buf + 128, 28, f.putApplName, false);
	writeCharField(buf + 156,  8, f.putDate, false);
	writeCharField(buf + 164,  8, f.putTime, false);

	appendBytes(out, buf, sizeof(buf));
}

// ---- CICS -----------------------------------------------------------------

size_t parseCics(const unsigned char * data, size_t length, int ccsid, int encoding,
                 CicsFields & out, std::vector<std::string> & warnings)
{
	bool ebcdic = (getCcsidType(ccsid) == CHAR_EBCDIC);
	bool reverse = (encoding != NUMERIC_PC);

	if (length < MQCIH_LENGTH_1)
	{
		warnings.push_back("CICS header too short");
		return 0;
	}
	char strucId[5] = {};
	if (ebcdic)
		EbcdicToAscii(data, 4, (unsigned char *)strucId);
	else
		memcpy(strucId, data, 4);
	if (memcmp(strucId, MQCIH_STRUC_ID, 4) != 0)
		return 0;

	MQLONG version = readLong(data + 4, reverse);
	if (version != MQCIH_VERSION_1 && version != MQCIH_VERSION_2)
	{
		warnings.push_back("CICS header unrecognised version");
		return 0;
	}
	size_t hdrLen = (version == MQCIH_VERSION_1) ? MQCIH_LENGTH_1 : MQCIH_LENGTH_2;
	if (length < hdrLen)
	{
		warnings.push_back("CICS header shorter than declared version length");
		return 0;
	}

	out.included = true;
	out.version  = (int)version;

	MQLONG enc = readLong(data + 12, reverse);
	out.encoding      = getIntEncode(enc);
	out.pdEncoding    = getPDEncode(enc);
	out.floatEncoding = getFloatEncode(enc);
	out.ccsid         = readLong(data + 16, reverse);
	out.format        = charField(data + 20, 8, ebcdic);
	out.flags         = readLong(data + 28, reverse);
	out.returnCode    = readLong(data + 32, reverse);
	out.compCode      = readLong(data + 36, reverse);
	out.reason        = readLong(data + 40, reverse);
	out.uowControl    = readLong(data + 44, reverse);
	out.getWaitInterval    = readLong(data + 48, reverse);
	out.linkType           = readLong(data + 52, reverse);
	out.outputDataLength   = readLong(data + 56, reverse);
	out.facilityKeepTime   = readLong(data + 60, reverse);
	out.adsDescriptor      = readLong(data + 64, reverse);
	out.conversationalTask = readLong(data + 68, reverse);
	out.taskEndStatus      = readLong(data + 72, reverse);
	out.facility           = hexBytes(data + 76, 8);
	out.function           = charField(data + 84, 4, ebcdic);
	out.abendCode          = charField(data + 88, 4, ebcdic);
	out.authenticator      = charField(data + 92, 8, ebcdic);
	// Reserved1 at 100
	out.replyToFormat      = charField(data + 108, 8, ebcdic);
	out.remoteSysId        = charField(data + 116, 4, ebcdic);
	out.remoteTransId      = charField(data + 120, 4, ebcdic);
	out.transactionId      = charField(data + 124, 4, ebcdic);
	out.facilityLike       = charField(data + 128, 4, ebcdic);
	out.attentionId        = charField(data + 132, 4, ebcdic);
	out.startCode          = charField(data + 136, 4, ebcdic);
	out.cancelCode         = charField(data + 140, 4, ebcdic);
	out.nextTransactionId  = charField(data + 144, 4, ebcdic);
	// Reserved2 at 148, Reserved3 at 156 — skipped

	if (version == MQCIH_VERSION_2)
	{
		out.cursorPosition = readLong(data + 164, reverse);
		out.errorOffset    = readLong(data + 168, reverse);
		out.inputItem      = readLong(data + 172, reverse);
		// Reserved4 at 176
	}

	(void)warnings;
	return hdrLen;
}

void buildCics(const CicsFields & f, std::vector<unsigned char> & out)
{
	if (!f.included)
		return;

	size_t hdrLen = (f.version == MQCIH_VERSION_1) ? MQCIH_LENGTH_1 : MQCIH_LENGTH_2;
	std::vector<unsigned char> buf(hdrLen, 0);
	unsigned char * p = buf.data();

	bool reverse = (f.encoding != NUMERIC_PC);

	memcpy(p + 0, MQCIH_STRUC_ID, 4);
	writeLong(p + 4, (MQLONG)f.version, reverse);
	writeLong(p + 8, (MQLONG)hdrLen, reverse);

	MQLONG enc = (MQLONG)(f.encoding == NUMERIC_PC ? MQENC_INTEGER_REVERSED : MQENC_INTEGER_NORMAL);
	enc |= (MQLONG)(f.pdEncoding == NUMERIC_PC ? MQENC_DECIMAL_REVERSED : MQENC_DECIMAL_NORMAL);
	enc |= (MQLONG)(f.floatEncoding == FLOAT_PC   ? MQENC_FLOAT_IEEE_NORMAL :
	                f.floatEncoding == FLOAT_UNIX  ? MQENC_FLOAT_IEEE_REVERSED :
	                f.floatEncoding == FLOAT_390   ? MQENC_FLOAT_S390 :
	                f.floatEncoding == FLOAT_TNS   ? MQENC_FLOAT_TNS :
	                0);
	writeLong(p + 12, enc, reverse);
	writeLong(p + 16, (MQLONG)f.ccsid, reverse);
	writeCharField(p + 20, 8, f.format, false);
	writeLong(p + 28, (MQLONG)f.flags, reverse);
	writeLong(p + 32, (MQLONG)f.returnCode, reverse);
	writeLong(p + 36, (MQLONG)f.compCode, reverse);
	writeLong(p + 40, (MQLONG)f.reason, reverse);
	writeLong(p + 44, (MQLONG)f.uowControl, reverse);
	writeLong(p + 48, (MQLONG)f.getWaitInterval, reverse);
	writeLong(p + 52, (MQLONG)f.linkType, reverse);
	writeLong(p + 56, (MQLONG)f.outputDataLength, reverse);
	writeLong(p + 60, (MQLONG)f.facilityKeepTime, reverse);
	writeLong(p + 64, (MQLONG)f.adsDescriptor, reverse);
	writeLong(p + 68, (MQLONG)f.conversationalTask, reverse);
	writeLong(p + 72, (MQLONG)f.taskEndStatus, reverse);
	unhexBytes(f.facility, p + 76, 8);
	writeCharField(p + 84, 4, f.function, false);
	writeCharField(p + 88, 4, f.abendCode, false);
	writeCharField(p + 92, 8, f.authenticator, false);
	// Reserved1 at 100 stays zero
	writeCharField(p + 108, 8, f.replyToFormat, false);
	writeCharField(p + 116, 4, f.remoteSysId, false);
	writeCharField(p + 120, 4, f.remoteTransId, false);
	writeCharField(p + 124, 4, f.transactionId, false);
	writeCharField(p + 128, 4, f.facilityLike, false);
	writeCharField(p + 132, 4, f.attentionId, false);
	writeCharField(p + 136, 4, f.startCode, false);
	writeCharField(p + 140, 4, f.cancelCode, false);
	writeCharField(p + 144, 4, f.nextTransactionId, false);
	// Reserved2 at 148, Reserved3 at 156 stay zero

	if (f.version == MQCIH_VERSION_2)
	{
		writeLong(p + 164, (MQLONG)f.cursorPosition, reverse);
		writeLong(p + 168, (MQLONG)f.errorOffset, reverse);
		writeLong(p + 172, (MQLONG)f.inputItem, reverse);
		// Reserved4 at 176 stays zero
	}

	appendBytes(out, buf.data(), buf.size());
}

// ---- IMS ------------------------------------------------------------------
// MQIIH field offsets (from the struct definition):
//   0: StrucId(4), 4: Version(4), 8: StrucLength(4), 12: Encoding(4),
//   16: CodedCharSetId(4), 20: Format(8), 28: Flags(4),
//   32: LTermOverride(8), 40: MFSMapName(8), 48: ReplyToFormat(8),
//   56: Authenticator(8), 64: TranInstanceId(16), 80: TranState(1),
//   81: CommitMode(1), 82: SecurityScope(1), 83: Reserved(1)
//   Total: 84 = MQIIH_LENGTH_1

size_t parseIms(const unsigned char * data, size_t length, int ccsid, int encoding,
                ImsFields & out, std::vector<std::string> & warnings)
{
	bool ebcdic = (getCcsidType(ccsid) == CHAR_EBCDIC);
	bool reverse = (encoding != NUMERIC_PC);

	if (length < MQIIH_LENGTH_1)
	{
		warnings.push_back("IMS header too short");
		return 0;
	}
	char strucId[5] = {};
	if (ebcdic)
		EbcdicToAscii(data, 4, (unsigned char *)strucId);
	else
		memcpy(strucId, data, 4);
	if (memcmp(strucId, MQIIH_STRUC_ID, 4) != 0)
		return 0;
	if (readLong(data + 4, reverse) != MQIIH_VERSION_1)
	{
		warnings.push_back("IMS header unrecognised version");
		return 0;
	}

	out.included = true;

	MQLONG enc = readLong(data + 12, reverse);
	out.encoding      = getIntEncode(enc);
	out.pdEncoding    = getPDEncode(enc);
	out.floatEncoding = getFloatEncode(enc);
	out.ccsid         = readLong(data + 16, reverse);
	out.format        = charField(data + 20, 8, ebcdic);
	out.flags         = readLong(data + 28, reverse);
	out.lTerm         = charField(data + 32, 8, ebcdic);
	out.mapName       = charField(data + 40, 8, ebcdic);
	out.replyToFormat = charField(data + 48, 8, ebcdic);
	out.authenticator = charField(data + 56, 8, ebcdic);
	// TranInstanceId at 64 (16 bytes) — opaque, not exposed in fields
	out.transState    = (int)(unsigned char)data[80];
	out.commitMode    = (int)(unsigned char)data[81];
	out.securityScope = (int)(unsigned char)data[82];

	(void)warnings;
	return MQIIH_LENGTH_1;
}

void buildIms(const ImsFields & f, std::vector<unsigned char> & out)
{
	if (!f.included)
		return;

	std::vector<unsigned char> buf(MQIIH_LENGTH_1, 0);
	unsigned char * p = buf.data();

	bool reverse = (f.encoding != NUMERIC_PC);

	memcpy(p + 0, MQIIH_STRUC_ID, 4);
	writeLong(p + 4, MQIIH_VERSION_1, reverse);
	writeLong(p + 8, MQIIH_LENGTH_1, reverse);

	MQLONG enc = (MQLONG)(f.encoding == NUMERIC_PC ? MQENC_INTEGER_REVERSED : MQENC_INTEGER_NORMAL);
	enc |= (MQLONG)(f.pdEncoding == NUMERIC_PC ? MQENC_DECIMAL_REVERSED : MQENC_DECIMAL_NORMAL);
	enc |= (MQLONG)(f.floatEncoding == FLOAT_PC   ? MQENC_FLOAT_IEEE_NORMAL :
	                f.floatEncoding == FLOAT_UNIX  ? MQENC_FLOAT_IEEE_REVERSED :
	                f.floatEncoding == FLOAT_390   ? MQENC_FLOAT_S390 :
	                f.floatEncoding == FLOAT_TNS   ? MQENC_FLOAT_TNS :
	                0);
	writeLong(p + 12, enc, reverse);
	writeLong(p + 16, (MQLONG)f.ccsid, reverse);
	writeCharField(p + 20, 8, f.format, false);
	writeLong(p + 28, (MQLONG)f.flags, reverse);
	writeCharField(p + 32, 8, f.lTerm, false);
	writeCharField(p + 40, 8, f.mapName, false);
	writeCharField(p + 48, 8, f.replyToFormat, false);
	writeCharField(p + 56, 8, f.authenticator, false);
	// TranInstanceId (64..79) stays zero
	p[80] = (unsigned char)f.transState;
	p[81] = (unsigned char)f.commitMode;
	p[82] = (unsigned char)f.securityScope;
	// Reserved at 83 stays zero

	appendBytes(out, buf.data(), buf.size());
}

// ---- RFH v1 ---------------------------------------------------------------
// MQRFH fixed part offsets:
//   0: StrucId(4), 4: Version(4), 8: StrucLength(4), 12: Encoding(4),
//   16: CodedCharSetId(4), 20: Format(8), 28: Flags(4)  — total 32

size_t parseRfh1(const unsigned char * data, size_t length, int ccsid, int encoding,
                 Rfh1Fields & out, std::vector<std::string> & warnings)
{
	bool ebcdic = (getCcsidType(ccsid) == CHAR_EBCDIC);
	bool reverse = (encoding != NUMERIC_PC);

	if (length < MQRFH_STRUC_LENGTH_FIXED)
	{
		warnings.push_back("RFH1 header too short");
		return 0;
	}
	char strucId[5] = {};
	if (ebcdic)
		EbcdicToAscii(data, 4, (unsigned char *)strucId);
	else
		memcpy(strucId, data, 4);
	if (memcmp(strucId, MQRFH_STRUC_ID, 4) != 0)
		return 0;
	if (readLong(data + 4, reverse) != MQRFH_VERSION_1)
	{
		warnings.push_back("RFH1 header unrecognised version");
		return 0;
	}

	MQLONG strucLen = readLong(data + 8, reverse);
	if (strucLen < MQRFH_STRUC_LENGTH_FIXED || (MQLONG)length < strucLen)
	{
		warnings.push_back("RFH1 header declared length out of range");
		return 0;
	}

	out.included = true;

	MQLONG enc = readLong(data + 12, reverse);
	out.encoding      = getIntEncode(enc);
	out.pdEncoding    = getPDEncode(enc);
	out.floatEncoding = getFloatEncode(enc);
	out.ccsid         = readLong(data + 16, reverse);
	out.format        = charField(data + 20, 8, ebcdic);
	out.flags         = readLong(data + 28, reverse);

	// variable part: blank-separated name/value pairs
	size_t varLen = (size_t)strucLen - MQRFH_STRUC_LENGTH_FIXED;
	if (varLen > 0)
	{
		std::string var;
		if (ebcdic)
		{
			var.resize(varLen);
			EbcdicToAscii(data + MQRFH_STRUC_LENGTH_FIXED, (unsigned int)varLen,
			              (unsigned char *)var.data());
		}
		else
		{
			var.assign(reinterpret_cast<const char *>(data + MQRFH_STRUC_LENGTH_FIXED), varLen);
		}

		// scan for OPT_APP_GRP, OPT_MSG_TYPE, mcd, pubsub, pscr
		auto findNv = [&](const char * name) -> std::string {
			size_t nlen = strlen(name);
			size_t pos = var.find(name);
			while (pos != std::string::npos)
			{
				// name must be preceded by start-of-string or a blank
				if (pos != 0 && var[pos - 1] != ' ')
				{
					pos = var.find(name, pos + 1);
					continue;
				}
				size_t valStart = pos + nlen;
				if (valStart >= var.size() || var[valStart] != ' ')
					break;
				++valStart;
				size_t valEnd = var.find(' ', valStart);
				if (valEnd == std::string::npos)
					valEnd = var.size();
				return var.substr(valStart, valEnd - valStart);
			}
			return {};
		};

		out.appGroup  = findNv("OPT_APP_GRP");
		out.msgType   = findNv("OPT_MSG_TYPE");
		out.hasMcd    = (var.find("mcd") != std::string::npos);
		out.hasPubsub = (var.find("pubsub") != std::string::npos);
		out.hasPscr   = (var.find("pscr") != std::string::npos);
		out.pscrOther = var;
	}

	(void)warnings;
	return (size_t)strucLen;
}

void buildRfh1(const Rfh1Fields & f, std::vector<unsigned char> & out)
{
	if (!f.included)
		return;

	std::string var;
	if (!f.appGroup.empty())
		{ var += "OPT_APP_GRP "; var += f.appGroup; var += ' '; }
	if (!f.msgType.empty())
		{ var += "OPT_MSG_TYPE "; var += f.msgType; var += ' '; }
	if (f.hasMcd)    var += "mcd ";
	if (f.hasPubsub) var += "pubsub ";
	if (f.hasPscr)   var += "pscr ";

	// Pad to 4-byte boundary
	while (var.size() % 4 != 0)
		var += ' ';

	MQLONG strucLen = MQRFH_STRUC_LENGTH_FIXED + (MQLONG)var.size();
	bool reverse = (f.encoding != NUMERIC_PC);

	unsigned char fixed[MQRFH_STRUC_LENGTH_FIXED];
	memset(fixed, 0, sizeof(fixed));
	memcpy(fixed + 0, MQRFH_STRUC_ID, 4);
	writeLong(fixed + 4, MQRFH_VERSION_1, reverse);
	writeLong(fixed + 8, strucLen, reverse);

	MQLONG enc = (MQLONG)(f.encoding == NUMERIC_PC ? MQENC_INTEGER_REVERSED : MQENC_INTEGER_NORMAL);
	enc |= (MQLONG)(f.pdEncoding == NUMERIC_PC ? MQENC_DECIMAL_REVERSED : MQENC_DECIMAL_NORMAL);
	enc |= (MQLONG)(f.floatEncoding == FLOAT_PC   ? MQENC_FLOAT_IEEE_NORMAL :
	                f.floatEncoding == FLOAT_UNIX  ? MQENC_FLOAT_IEEE_REVERSED :
	                f.floatEncoding == FLOAT_390   ? MQENC_FLOAT_S390 :
	                f.floatEncoding == FLOAT_TNS   ? MQENC_FLOAT_TNS :
	                0);
	writeLong(fixed + 12, enc, reverse);
	writeLong(fixed + 16, (MQLONG)f.ccsid, reverse);
	writeCharField(fixed + 20, 8, f.format, false);
	writeLong(fixed + 28, (MQLONG)f.flags, reverse);

	appendBytes(out, fixed, sizeof(fixed));
	appendBytes(out, var.c_str(), var.size());
}

// ---- RFH v2 ---------------------------------------------------------------
// MQRFH2 fixed part offsets:
//   0: StrucId(4), 4: Version(4), 8: StrucLength(4), 12: Encoding(4),
//   16: CodedCharSetId(4), 20: Format(8), 28: Flags(4), 32: NameValueCCSID(4)
//   Total: 36

size_t parseRfh2(const unsigned char * data, size_t length, int ccsid, int encoding,
                 Rfh2Fields & out, std::vector<std::string> & warnings)
{
	bool ebcdic = (getCcsidType(ccsid) == CHAR_EBCDIC);
	bool reverse = (encoding != NUMERIC_PC);

	if (length < MQRFH_STRUC_LENGTH_FIXED_2)
	{
		warnings.push_back("RFH2 header too short");
		return 0;
	}
	char strucId[5] = {};
	if (ebcdic)
		EbcdicToAscii(data, 4, (unsigned char *)strucId);
	else
		memcpy(strucId, data, 4);
	if (memcmp(strucId, MQRFH_STRUC_ID, 4) != 0)
		return 0;
	if (readLong(data + 4, reverse) != MQRFH_VERSION_2)
	{
		warnings.push_back("RFH2 header unrecognised version");
		return 0;
	}

	MQLONG strucLen = readLong(data + 8, reverse);
	if (strucLen < MQRFH_STRUC_LENGTH_FIXED_2 || (MQLONG)length < strucLen)
	{
		warnings.push_back("RFH2 header declared length out of range");
		return 0;
	}

	out.included = true;

	MQLONG enc = readLong(data + 12, reverse);
	out.encoding        = getIntEncode(enc);
	out.pdEncoding      = getPDEncode(enc);
	out.floatEncoding   = getFloatEncode(enc);
	out.ccsid           = readLong(data + 16, reverse);
	out.format          = charField(data + 20, 8, ebcdic);
	out.flags           = readLong(data + 28, reverse);
	out.nameValueCcsid  = readLong(data + 32, reverse);

	// Walk sub-folders
	size_t pos = MQRFH_STRUC_LENGTH_FIXED_2;
	size_t end = (size_t)strucLen;

	while (pos + 4 <= end)
	{
		MQLONG folderLen = readLong(data + pos, reverse);
		pos += 4;
		if (folderLen < 0 || pos + (size_t)folderLen > end)
		{
			warnings.push_back("RFH2 sub-folder length out of range");
			break;
		}

		// Decode folder XML
		std::string xml = decodeFolder(data + pos, folderLen, out.nameValueCcsid, warnings);
		pos += (size_t)folderLen;

		// Identify the folder by its root element name
		// e.g., "<mcd><Msd>...</Msd></mcd>"
		size_t nameStart = xml.find('<');
		if (nameStart == std::string::npos)
			continue;
		nameStart += 1; // skip '<'
		size_t nameEnd = xml.find_first_of("> \t\n\r", nameStart);
		if (nameEnd == std::string::npos)
			continue;
		std::string folderName = xml.substr(nameStart, nameEnd - nameStart);

		// Find content within the root element
		std::string openTag  = "<" + folderName + ">";
		std::string closeTag = "</" + folderName + ">";
		std::string content;
		{
			size_t cstart = xml.find(openTag);
			if (cstart != std::string::npos)
			{
				cstart += openTag.size();
				size_t cend = xml.rfind(closeTag);
				if (cend != std::string::npos && cend >= cstart)
					content = xml.substr(cstart, cend - cstart);
			}
		}

		if (folderName == MQRFH2_MSG_CONTENT_FOLDER)
		{
			out.hasMcd = true;
			out.mcd.msgDomain = xmlGet(content, "<" MQMCD_MSG_DOMAIN ">", "</" MQMCD_MSG_DOMAIN ">");
			out.mcd.msgSet    = xmlGet(content, "<" MQMCD_MSG_SET    ">", "</" MQMCD_MSG_SET    ">");
			out.mcd.msgType   = xmlGet(content, "<" MQMCD_MSG_TYPE   ">", "</" MQMCD_MSG_TYPE   ">");
			out.mcd.msgFmt    = xmlGet(content, "<" MQMCD_MSG_FORMAT ">", "</" MQMCD_MSG_FORMAT ">");
		}
		else if (folderName == "jms")
		{
			out.hasJms = true;
			out.jms.dst          = xmlGet(content, "<Dst>",  "</Dst>");
			out.jms.rto          = xmlGet(content, "<Rto>",  "</Rto>");
			out.jms.expiration   = xmlGet(content, "<Exp>",  "</Exp>");
			out.jms.correlId     = xmlGet(content, "<Cid>",  "</Cid>");
			out.jms.groupId      = xmlGet(content, "<Gid>",  "</Gid>");
			out.jms.sequence     = xmlGet(content, "<Seq>",  "</Seq>");
			out.jms.timestamp    = xmlGet(content, "<Tms>",  "</Tms>");
			out.jms.priority     = xmlGet(content, "<Pri>",  "</Pri>");
			out.jms.deliveryMode = xmlGet(content, "<Dlv>",  "</Dlv>");
		}
		else if (folderName == MQRFH2_USER_FOLDER)
		{
			out.hasUsr = true;
			out.usrXml = xml;
		}
		else if (folderName == MQRFH2_PUBSUB_CMD_FOLDER)
		{
			out.hasPs = true;
			out.psXml = xml;
		}
		else if (folderName == MQRFH2_PUBSUB_RESP_FOLDER)
		{
			out.hasPscr = true;
			out.pscrXml = xml;
		}
		else
		{
			out.hasOther = true;
			out.otherXml += xml;
		}
	}

	return (size_t)strucLen;
}

void buildRfh2(const Rfh2Fields & f, std::vector<unsigned char> & out)
{
	if (!f.included)
		return;

	bool reverse = (f.encoding != NUMERIC_PC);
	std::vector<std::string> dummy;

	// Build sub-folder payload first so we know total StrucLength.
	std::vector<unsigned char> folders;

	auto addFolder = [&](const std::string & xml) {
		if (!xml.empty())
			appendFolder(folders, xml, (MQLONG)f.nameValueCcsid, reverse, dummy);
	};

	if (f.hasMcd)
	{
		std::string content;
		content += xmlPut(MQMCD_MSG_DOMAIN, f.mcd.msgDomain);
		content += xmlPut(MQMCD_MSG_SET,    f.mcd.msgSet);
		content += xmlPut(MQMCD_MSG_TYPE,   f.mcd.msgType);
		content += xmlPut(MQMCD_MSG_FORMAT, f.mcd.msgFmt);
		if (!content.empty())
			addFolder("<" MQRFH2_MSG_CONTENT_FOLDER ">" + content + "</" MQRFH2_MSG_CONTENT_FOLDER ">");
	}

	if (f.hasJms)
	{
		std::string content;
		content += xmlPut("Dst", f.jms.dst);
		content += xmlPut("Rto", f.jms.rto);
		content += xmlPut("Exp", f.jms.expiration);
		content += xmlPut("Cid", f.jms.correlId);
		content += xmlPut("Gid", f.jms.groupId);
		content += xmlPut("Seq", f.jms.sequence);
		content += xmlPut("Tms", f.jms.timestamp);
		content += xmlPut("Pri", f.jms.priority);
		content += xmlPut("Dlv", f.jms.deliveryMode);
		content += f.jms.userDef;
		if (!content.empty())
			addFolder("<jms>" + content + "</jms>");
	}

	// usr, psc, pscr, other are stored as complete XML with their root element
	if (f.hasUsr  && !f.usrXml.empty())  addFolder(f.usrXml);
	if (f.hasPs   && !f.psXml.empty())   addFolder(f.psXml);
	if (f.hasPscr && !f.pscrXml.empty()) addFolder(f.pscrXml);
	if (f.hasOther && !f.otherXml.empty()) addFolder(f.otherXml);

	MQLONG strucLen = MQRFH_STRUC_LENGTH_FIXED_2 + (MQLONG)folders.size();

	// Fixed header
	unsigned char fixed[MQRFH_STRUC_LENGTH_FIXED_2];
	memset(fixed, 0, sizeof(fixed));
	memcpy(fixed + 0, MQRFH_STRUC_ID, 4);
	writeLong(fixed + 4, MQRFH_VERSION_2, reverse);
	writeLong(fixed + 8, strucLen, reverse);

	MQLONG enc = (MQLONG)(f.encoding == NUMERIC_PC ? MQENC_INTEGER_REVERSED : MQENC_INTEGER_NORMAL);
	enc |= (MQLONG)(f.pdEncoding == NUMERIC_PC ? MQENC_DECIMAL_REVERSED : MQENC_DECIMAL_NORMAL);
	enc |= (MQLONG)(f.floatEncoding == FLOAT_PC   ? MQENC_FLOAT_IEEE_NORMAL :
	                f.floatEncoding == FLOAT_UNIX  ? MQENC_FLOAT_IEEE_REVERSED :
	                f.floatEncoding == FLOAT_390   ? MQENC_FLOAT_S390 :
	                f.floatEncoding == FLOAT_TNS   ? MQENC_FLOAT_TNS :
	                0);
	writeLong(fixed + 12, enc, reverse);
	writeLong(fixed + 16, (MQLONG)f.ccsid, reverse);
	writeCharField(fixed + 20, 8, f.format, false);
	writeLong(fixed + 28, (MQLONG)f.flags, reverse);
	writeLong(fixed + 32, (MQLONG)f.nameValueCcsid, reverse);

	appendBytes(out, fixed, sizeof(fixed));
	out.insert(out.end(), folders.begin(), folders.end());
}

// ---- Top-level ------------------------------------------------------------

void parseHeaders(const unsigned char * data, size_t dataLength,
                  int initialCcsid, int initialEncoding, ParsedHeaders & out)
{
	// Use the same chain-walking logic as walkHeaders but decode fields.
	// Format / encoding / ccsid chain through each header in sequence.
	int ccsid    = initialCcsid;
	int encoding = initialEncoding;
	size_t pos   = 0;

	// Determine the first header format from the data itself: DLH/CIH/IIH/RFH/RFH2
	// using the known StrucId bytes (ASCII or EBCDIC).
	auto tryHeader = [&](const char * strucId, size_t minLen) -> bool {
		if (pos + minLen > dataLength)
			return false;
		bool ebcdic = (getCcsidType(ccsid) == CHAR_EBCDIC);
		char tmp[5] = {};
		if (ebcdic)
			EbcdicToAscii(data + pos, 4, (unsigned char *)tmp);
		else
			memcpy(tmp, data + pos, 4);
		return memcmp(tmp, strucId, 4) == 0;
	};

	// Walk up to 8 headers (enough for any real message)
	for (int iter = 0; iter < 8 && pos < dataLength; ++iter)
	{
		if (tryHeader(MQDLH_STRUC_ID, sizeof(MQDLH)))
		{
			size_t n = parseDlq(data + pos, dataLength - pos, ccsid, encoding, out.dlq, out.warnings);
			if (n == 0) break;
			ccsid    = out.dlq.ccsid;
			encoding = out.dlq.encoding;
			pos += n;
		}
		else if (tryHeader(MQCIH_STRUC_ID, MQCIH_LENGTH_1))
		{
			size_t n = parseCics(data + pos, dataLength - pos, ccsid, encoding, out.cics, out.warnings);
			if (n == 0) break;
			ccsid    = out.cics.ccsid;
			encoding = out.cics.encoding;
			pos += n;
		}
		else if (tryHeader(MQIIH_STRUC_ID, MQIIH_LENGTH_1))
		{
			size_t n = parseIms(data + pos, dataLength - pos, ccsid, encoding, out.ims, out.warnings);
			if (n == 0) break;
			ccsid    = out.ims.ccsid;
			encoding = out.ims.encoding;
			pos += n;
		}
		else if (tryHeader(MQRFH_STRUC_ID, MQRFH_STRUC_LENGTH_FIXED))
		{
			// Distinguish RFH1 (version 1) from RFH2 (version 2)
			bool reverse = (encoding != NUMERIC_PC);
			if (pos + MQRFH_STRUC_LENGTH_FIXED_2 <= dataLength)
			{
				MQLONG version = readLong(data + pos + 4, reverse);
				if (version == MQRFH_VERSION_2)
				{
					size_t n = parseRfh2(data + pos, dataLength - pos, ccsid, encoding,
					                     out.rfh2, out.warnings);
					if (n == 0) break;
					ccsid    = out.rfh2.ccsid;
					encoding = out.rfh2.encoding;
					pos += n;
					continue;
				}
			}
			size_t n = parseRfh1(data + pos, dataLength - pos, ccsid, encoding,
			                     out.rfh1, out.warnings);
			if (n == 0) break;
			ccsid    = out.rfh1.ccsid;
			encoding = out.rfh1.encoding;
			pos += n;
		}
		else
		{
			break;
		}
	}
}

std::vector<unsigned char> buildHeaders(const ParsedHeaders & headers)
{
	if (!headers.hasAny())
		return {};

	std::vector<unsigned char> out;
	buildDlq(headers.dlq, out);
	buildCics(headers.cics, out);
	buildIms(headers.ims, out);
	buildRfh1(headers.rfh1, out);
	buildRfh2(headers.rfh2, out);
	return out;
}

} // namespace rfh
