/*
Copyright (c) IBM Corporation 2000, 2026
All rights reserved. This program and the accompanying materials
are made available under the terms of the Apache License, Version 2.0
which accompanies this distribution, and is available at
http://www.apache.org/licenses/LICENSE-2.0
*/

// DataFormatter.h: the Data tab display formatters, moved out of DataArea so that
// the Windows application and the portable core share one implementation. The
// members keep DataArea's names, so the method bodies are DataArea's own.
//
// Give it the data with setData and the display code pages with setDisplay, set
// the other inputs, call one get...Data method, then read its output member. Each
// output is a NUL-terminated buffer in the display code page (app->ansi), owned by
// the formatter and freed by its destructor. A formatter keeps its outputs, so a
// second call for the same view and settings returns at once, as in DataArea.
// A formatter shows one message: call setData once, before the first get...Data
// call; later calls would show the outputs made for the first message.
//
// The padding that the formatters add to their buffers also hides a write that
// overruns a buffer by up to kLookAheadPad bytes, so a guard-page allocator
// finds only larger overruns of the work buffers (it finds any overrun of the
// formatter's own members, and of the copy of the data).
//
// Deliberate differences from DataArea, all to stay within buffers:
// - the data and every buffer the formatters allocate are followed by
//   kLookAheadPad zero bytes, because the moved methods read a few bytes past
//   the end of their data without checking the remaining length (DataArea's data
//   has 2 zero bytes). For example, findcrlf reads the byte after its last
//   character (two after a DBCS lead byte), the character view's EDI option
//   compares segment tags of up to 3 bytes and reads a UNA's delimiters 6 and 8
//   bytes on, and the XML and parsed views compare 9-byte CDATA markers; CJsonParse
//   reads the byte after a backslash. xmlsubs.cpp does not need it: it checks the
//   remaining length (tools/port/port-parser-fixes.pl);
// - the both view allocates 65 bytes per 16 bytes of data; DataArea allocates
//   4 per byte plus 65, and writes past the end for messages over 1040 bytes;
// - the fixed view indexes its display table with an unsigned byte;
// - the EDI option of the character view stops skipping blanks at the end of the
//   data;
// - the parsed and XML views stop copying a tag name, an attribute list, an end
//   tag name or a value when its work area is full; a long name is cut short and
//   parsing goes on, where DataArea overruns the work area;
// - the parsed view does not look for a trailing '/' or '\\' in an empty tag name
//   ("< />"); DataArea reads, and may clear, the byte before the name;
// - the table directory must leave room for a table file name (see tablesUsable);
//   a longer one is treated as no tables, where DataArea overruns its file name;
// - the UCS-2 fallback of the character view converts from the data itself;
//   DataArea reads the units from a 4-unit local array;
// - the display table is built when the formatter is made, and again by
//   setDisplay for the display code page. DataArea fills it with '.' and builds it
//   in the first character or both view, which Windows always shows first; the
//   other views use it without building it.

#pragma once

#include <string>
#include <vector>

#include "rfh_codepage.h"
#include "rfh_platform.h"

// Zero bytes after the data and after each work buffer, which the moved methods
// may read past the end of their data (see the list above).
const unsigned int kLookAheadPad = 32;

// Size of the file name buffers in the moved code that build a conversion table
// path (fileName[512]).
const int kTablePathMax = 512;

class DataFormatter
{
public:
	DataFormatter();
	~DataFormatter();

	DataFormatter(const DataFormatter &) = delete;
	DataFormatter & operator=(const DataFormatter &) = delete;

	// Copies the data to show, followed by kLookAheadPad zero bytes, and points
	// fileData and fileSize at the copy.
	void setData(const unsigned char * bytes, unsigned int size);

	// The data setData was given (not the copy), for callers that check it is
	// still the data they show.
	const unsigned char * source() const { return sourceData; }

	// The display code pages, { rfh_get_acp(), 437 } until setDisplay is called.
	// setDisplay also rebuilds the display table for codepages.ansi, as Windows
	// builds it for GetACP(), which is the code page it shows text in. Call it
	// before the first get...Data call.
	void setDisplay(const DisplayCodepages & codepages);
	const DisplayCodepages & displayCodepages() const { return display; }

	// Inputs.
	unsigned char *	fileData;			// set by setData
	unsigned int	fileSize;			// set by setData
	int				fileCcsid;			// the message's code page (the MQMD's)
	int				fileIntFormat;		// NUMERIC_PC or NUMERIC_HOST
	int				m_char_format;		// CHAR_* of the data (DataArea::m_char_format)
	std::string		conversionTableDir;	// MQ's conversion tables (Windows: conv\table under the
										// MQ install directory; Linux: /opt/mqm/lib/iconv); empty for none.
										// A directory of kTablePathMax - 32 characters or more leaves no
										// room for a table file name and is treated as none.

	// TRUE when conversionTableDir is a usable table directory.
	BOOL tablesUsable() const
	{
		return !conversionTableDir.empty() && conversionTableDir.size() < (size_t)(kTablePathMax - 32);
	}

	// Outputs.
	unsigned char *	m_data_ascii;
	unsigned char *	m_data_hex;
	unsigned char *	m_data_both;
	unsigned char *	m_data_xml;
	unsigned char *	m_data_parsed;
	unsigned char *	m_data_json;
	unsigned char *	m_data_fix;
	std::string		m_error_msg;		// set by the JSON and fixed views when they fail

	void getCharacterData(const int charFormat, const int crlf, const int edi, const int BOM, BOOL indent);
	void getHexData();
	void getBothData(const int charFormat, const int BOM);
	void getXmlData(const int charFormat);
	void getParsedData(const int charFormat);
	void getJsonData(const int charFormat);
	void getFixData(const int charFormat);

private:
	unsigned char * getTranslatedData(int * length, const int charFormat, const int ccsid);
	void getNameValueData(const unsigned char * input, int inputLen, unsigned char * output, int maxLength, int ccsid, const int charFormat);
	int xlateEbcdicChar(int fromCcsid, int toCcsid, const char * input, char * output);
	int checkForUnicodeMarker(unsigned char * data);
	int findcrlf(const unsigned char *datain, const int maxchar, const int charSet);
	BOOL isLeadChar(unsigned char ch, const int charFormat);
	int MQEBC2ASC(unsigned char * out, const unsigned char * in, int len, int ccsidIn, int ccsidOut);
	int MQEBC2UCS(RFH_UCS2 *out, const unsigned char *in, int len, int ccsidIn);
	void findMQInstallDirectory(unsigned char * dir, int dirSize);

	void logTraceEntry(const char * traceInfo);
	void dumpTraceData(const char * label, const unsigned char * data, unsigned int length);
	void updateMsgText() {}				// DataArea shows m_error_msg; callers read it instead

	DisplayCodepages display;			// see setDisplay
	const DisplayCodepages * app;		// &display: the moved code reads app->ansi and app->oem

	BOOL traceEnabled;
	BOOL verboseTrace;

	// the settings each output was made with
	int saveEdi;
	int saveBOM;
	int saveParsedFormat;
	int saveXMLFormat;
	int saveBothFormat;
	int saveCharCrlf;
	int saveCharFormat;
	int saveJsonFormat;
	int saveFixFormat;
	BOOL saveIndentOn;

	char invalidCharTable[256];
	BOOL invalidCharTableNotBuilt;

	// MQ conversion tables, loaded when first needed
	int MQCcsidLen;
	int MQUcsCcsid;
	int MQCcsidOut;
	int MQCcsidIn;
	unsigned char * MQCcsidPtr;
	RFH_UCS2 * MQUcsPtr;

	std::vector<unsigned char> ownedData;	// the copy fileData points at
	const unsigned char * sourceData;

	// XML work areas (file-scope statics in DataArea.cpp)
	char xmlVarName[8192 + 1024];			// tag name
	char tempVarValue[2 * 8192 + 1024];		// tag value
	char tempVarName[8192 + 1];				// holding area for a tag name
	char tempEndVarName[8192 + 2];			// '/' and a tag name
	char tempVarAttr[2 * 8192 + 1];
	char xmlAttrStr[65536];
};
