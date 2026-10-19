/*
Copyright (c) IBM Corporation 2000, 2026
All rights reserved. This program and the accompanying materials
are made available under the terms of the Apache License, Version 2.0
which accompanies this distribution, and is available at
http://www.apache.org/licenses/LICENSE-2.0
*/

// DataFormatter.cpp: the Data tab display formatters (see DataFormatter.h). The
// methods after the marker line were copied from DataArea.cpp by
// tools/port/port-formatters.pl, which lists its mechanical rewrites.

#include "stdafx.h"
#include <stdint.h>
#include <stdlib.h>
#include "comsubs.h"
#include "xmlsubs.h"
#include "mqsubs.h"
#include "rfhconst.h"
#include "Names.h"
#include "JsonParse.h"
#include "XMLParse.h"
#include "rfh_codepage.h"
#include "rfh_trace.h"
#include "DataFormatter.h"
#include "cmqc.h"

// The moved code assumes a little-endian host, as Windows is.
#ifndef _WIN32
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "DataFormatter assumes a little-endian host");
#endif

// from DataArea.cpp
#define MAX_ASCII_CHAR	32
#define MAX_XML_INDENT_LEVELS	15
#define BAD_XML_MSG		"*****RFHUtil format problem - Data does not appear to be XML\r\n"
#define CDATA_START		"<![CDATA["
#define CDATA_END		"]]>"

DataFormatter::DataFormatter()
{
	fileData = NULL;
	fileSize = 0;
	sourceData = NULL;
	fileCcsid = 0;
	fileIntFormat = NUMERIC_PC;
	m_char_format = CHAR_ASCII;
	display.ansi = rfh_get_acp();
	display.oem = 437;
	app = &display;

	m_data_ascii = NULL;
	m_data_hex = NULL;
	m_data_both = NULL;
	m_data_xml = NULL;
	m_data_parsed = NULL;
	m_data_json = NULL;
	m_data_fix = NULL;

	traceEnabled = rfhGetTrace()->isTraceEnabled();
	verboseTrace = rfhGetTrace()->verboseTrace;

	// DataArea's initial values (its constructor and resetDataArea)
	saveEdi = 0;
	saveBOM = 0;
	saveParsedFormat = CHAR_ASCII;
	saveXMLFormat = CHAR_ASCII;
	saveBothFormat = CHAR_ASCII;
	saveCharCrlf = IGNORE_CRLF;
	saveCharFormat = CHAR_ASCII;
	saveJsonFormat = CHAR_ASCII;
	saveFixFormat = CHAR_ASCII;
	saveIndentOn = FALSE;

	memset(xmlVarName, 0, sizeof(xmlVarName));
	memset(tempVarValue, 0, sizeof(tempVarValue));
	memset(tempVarName, 0, sizeof(tempVarName));
	memset(tempEndVarName, 0, sizeof(tempEndVarName));
	memset(tempVarAttr, 0, sizeof(tempVarAttr));
	memset(xmlAttrStr, 0, sizeof(xmlAttrStr));

	// see DataFormatter.h: built here rather than in the first character view
	buildInvalidCharTable(invalidCharTable);
	invalidCharTableNotBuilt = FALSE;

	MQCcsidLen = 0;
	MQUcsCcsid = 0;
	MQCcsidOut = 0;
	MQCcsidIn = 0;
	MQCcsidPtr = NULL;
	MQUcsPtr = NULL;
}

DataFormatter::~DataFormatter()
{
	unsigned char * buffers[] = { m_data_ascii, m_data_hex, m_data_both, m_data_xml,
		m_data_parsed, m_data_json, m_data_fix, MQCcsidPtr };
	for (unsigned char * buffer : buffers)
	{
		if (buffer != NULL)
		{
			rfhFree(buffer);
		}
	}
	if (MQUcsPtr != NULL)
	{
		rfhFree(MQUcsPtr);
	}
}

void DataFormatter::setData(const unsigned char * bytes, unsigned int size)
{
	// exactly the data and its pad, so the guard allocator sees any read past them
	std::vector<unsigned char> copy(size + kLookAheadPad, 0);
	if (size > 0)
	{
		memcpy(copy.data(), bytes, size);
	}
	ownedData.swap(copy);
	fileData = ownedData.data();
	fileSize = size;
	sourceData = bytes;
}

void DataFormatter::setDisplay(const DisplayCodepages & codepages)
{
	display = codepages;

	// buildInvalidCharTable (comsubs.cpp) marks the bytes that the ANSI code page
	// does not display, which it asks rfh_get_acp for
	RfhAcpScope acp(display.ansi);
	buildInvalidCharTable(invalidCharTable);
	invalidCharTableNotBuilt = FALSE;
}

void DataFormatter::logTraceEntry(const char * traceInfo)
{
	rfhGetTrace()->logTraceEntry(traceInfo);
}

void DataFormatter::dumpTraceData(const char * label, const unsigned char * data, unsigned int length)
{
	rfhGetTrace()->dumpTraceData(label, data, length);
}

// DataArea finds MQ's install directory here and appends conv\table to it; the
// formatter is given the table directory itself. A directory that leaves no room
// for a table file name is not copied (see tablesUsable).
void DataFormatter::findMQInstallDirectory(unsigned char * dir, int dirSize)
{
	dir[0] = 0;
	if (tablesUsable())
	{
		strncpy((char *)dir, conversionTableDir.c_str(), dirSize - 1);
		dir[dirSize - 1] = 0;
	}
}

// See DataFormatter.h: every buffer the formatters allocate is followed by
// kLookAheadPad zero bytes (rfhMalloc zero-fills).
#define rfhMalloc(length, tag) rfhMalloc((length) + kLookAheadPad, tag)

// ---- methods moved from DataArea.cpp by tools/port/port-formatters.pl ----

void DataFormatter::getHexData()

{
	// turn the file data into a hex format
	unsigned char	*hexStr;
	char			ch;				// work variable
	int				i=0;				// work variable
	int				k=0;				// work variable
	UINT			len;			// length of required area to acquire
	int				buffer=0;		// offset of the next character to processs
	int				remaining;		// number of bytes left to process
	int				offset;			// current location within the raw file data
	int				count=0;			// number of characters on a particular line
	int				location=0;		// offset of start of this line within the file
	char			traceInfo[512];	// work variable to build trace message

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering DataFormatter::getHexData() with fileSize=%d m_data_hex=%8.8X", fileSize, (unsigned int)(uintptr_t)m_data_hex);

		// trace entry to getHexData
		logTraceEntry(traceInfo);
	}

	// check already have the proper data formatted
	if (m_data_hex != NULL)
	{
		// already formatted - nothing to do
		return;
	}

	// calculate the required length
	// use two characters per data character plus 14 more
	// characters for every 16 characters
	len = (2 * fileSize) + ((fileSize / 8) * 7) + 47;
	m_data_hex = (unsigned char *) rfhMalloc(len, "HEXDATA ");
	m_data_hex[0] = 0;
	hexStr = m_data_hex;

	remaining = fileSize;
	while (remaining > 0)
	{
		if (remaining > 16)
		{
			count = 16;
		}
		else
		{
			count = remaining;
		} // endif

		// initialize the output strings and buffer offset
		offset = buffer;
		i = 9;    // initialize variable offset
		k = 0;    // initialize counter

		// add the offset to the beginning of the line
		sprintf((char *) hexStr, "%8.8d ", location);

		// calculate the next location
		location += 16;

		while (k < count)
		{
			ch = (unsigned char) fileData[buffer] >> 4;
			ch = HEX_NUMBERS[ch];
			hexStr[i++] = ch;

			ch = (unsigned char) fileData[buffer] & 0x0F;
			ch = HEX_NUMBERS[ch];
			hexStr[i++] = ch;

			// Go on to the next character
			remaining--;
			buffer++;
			k++;   // increment counter

			// Insert a blank for every fourth character
			if ((k%4 == 0) && (k < count))
			{
				hexStr[i++] = ' ';
			} // endif
		} // endwhile

		// overwrite the last space
		if (remaining > 0)
		{
			hexStr[i++] = '\r';		// Add a CR character to the line
			hexStr[i++] = '\n';		// Add a NL character to the line
		}

		hexStr[i] = '\0';		// Terminate the string
		hexStr += i;
	} // endwhile

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting DataFormatter::getHexData() with strlen(m_data_hex)=%d m_data_hex=%8.8X buffer=%d hexStr-m_data_hex=%d count=%d i=%d", (int)strlen((const char *)m_data_hex), (unsigned int)(uintptr_t)m_data_hex, buffer, (int)(hexStr-m_data_hex), count, i);

		// trace exit from getHexData
		logTraceEntry(traceInfo);
	}

	return;
}

int DataFormatter::xlateEbcdicChar(int fromCcsid, int toCcsid, const char *input, char *output)

{
	int			tcount=0;
	char		defChar = ' ';
	RFH_UCS2		ucsChar[4];		// area to translate one character

	// try the translation using windows
	// the called routine is locate in comsubs.cpp
	tcount = EbcdicCharToAsciiChar(fromCcsid, toCcsid, input, output);

	// did the translation work?
	if (0 == tcount)
	{
		// Windows couldn't do the translation so try MQ next
		// see if MQ provides a translation table from the input CCSID to UCS-2
		tcount = MQEBC2UCS(ucsChar, (unsigned char *)input, 1, fromCcsid);

		// was there an MQ table?
		if (tcount > 0)
		{
			tcount = rfh_WideCharToMultiByte(fromCcsid, 0, ucsChar, 1, output, 2, &defChar, NULL);
		}

		// was the combination of MQ and Windows able to do the translation?
		if (0 == tcount)
		{
			// didn't work - see if MQ can do the translation directly
			// windows could not do the translation so see if an MQ table can be used
			// if this does not work then return a 0 to indicate failure
			tcount = MQEBC2ASC((unsigned char *)output, (unsigned char *)input, 1, fromCcsid, toCcsid);
		}
	}

	// return the number of characters translated (0 if none otherwise 1)
	return tcount;
}

void DataFormatter::getCharacterData(const int charFormat, const int crlf, const int edi, const int BOM, BOOL indentOn)

{
	// turn the file data into an ASCII character format
	// replace any unprintable characters with periods
	unsigned char	*asciiStr;		// pointer to result string
	unsigned char	*dataPtr = fileData;
	unsigned char	ch;				// temporary working character
	unsigned char	delim;			// delimiter character for X.12 messages - offset 106 in message
									// or offset 9 in a UNA segment in an edifact message
	unsigned char	escape;			// escape character for edifact messages
	unsigned char	*data=NULL;		// pointer to data converted from Unicode
	char			*tempData;		// work area to reverse bytes for CP 1201
	int				fileLength = fileSize;
	int				ccsid;
	int				asciiCcsid=0;	// ASCII code page to use for EBCDIC to ASCII translations
	int				dataSize;
	int				dbcsCount;
	int				tcount=0;		// number of characters translated
	int				winXlate=0;		// number of EBCDIC characters translated using Windows
	int				MQXlate=0;		// number of EBCDIC characters translated using MQ translate tables
	int				defXlate=0;		// number of EBCDIC characters translated using built-in default table
	int				i;				// work variable
	int				j;				// work variable
	int				k;				// work variable
	int				newline;		// work variable
	UINT			len;			// length of required area to acquire
	int				buffer=0;		// offset of the next character to process
	int				remaining;		// number of bytes left to process
	int				offset;			// current location within the raw file data
	int				count;			// number of lines
	int				location=0;		// offset of start of this line within the file
	int				indent=0;		// indent is used only with edi
	RFH_UCS2			*wcsPtr;		// pointer to UCS-2 data
	bool			edimsg=false;	// process the message as an EDI message
	bool			unicode=false;
	bool			worked=true;	// indicator if MQ translation has failed, to avoid repeated overhead
	unsigned char	tempTxt[4];		// workarea to translate a single character
	char			defChar='.';	// default character to replace characters that do not translate properly
	char			traceInfo[512];	// work variable to build trace message

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering DataFormatter::getCharacterData() with charFormat=%d crlf=%d edi=%d BOM=%d indentOn=%d saveCharFormat=%d m_data_ascii=%8.8X fileCcsid=%d", charFormat, crlf, edi, BOM, indentOn, saveCharFormat, (unsigned int)(uintptr_t)m_data_ascii, fileCcsid);

		// trace entry to getCharacterData
		logTraceEntry(traceInfo);
	}

	// find the application object

	// check if already have the proper data formatted
	if (m_data_ascii != NULL)
	{
		// changed options?
		if ((charFormat == saveCharFormat) && (crlf == saveCharCrlf) && (edi == saveEdi) && (saveBOM == BOM) && (saveIndentOn == indentOn))
		{
			if (traceEnabled)
			{
				// trace early exit from getCharacterData
				logTraceEntry("Exit from DataFormatter::getCharacterData() - reuse existing data");
			}

			// no need to do anything - no changes to formating detected
			return;
		}

		rfhFree(m_data_ascii);
		m_data_ascii = NULL;
	}

	// remember the type of display data
	saveCharCrlf = crlf;
	saveEdi = edi;
	saveBOM = BOM;
	saveIndentOn = indentOn;
	saveCharFormat = charFormat;
	escape = 0;

	// check for Unicode (UCS) input
	if (((1200 == fileCcsid) || (13488 == fileCcsid) || (17584 == fileCcsid)) && (m_char_format > CHAR_EBCDIC))
	{
//		getCharDataUcs(charFormat, crlf, edi);
//		return;
	}

	if (invalidCharTableNotBuilt)
	{
		// figure out what code page this system is using
		// and figure out what the valid code points are
		buildInvalidCharTable(invalidCharTable);
		invalidCharTableNotBuilt = FALSE;
	}

	// need to do EBCDIC to ASCII translation?
	if (CHAR_EBCDIC == charFormat)
	{
		// figure out what code page to translate to
		// only need to do this once
		asciiCcsid = matchEbcdicToAscii(fileCcsid);
	}

	// check for a byte order mark (BOM)
	if (BOM)
	{
		// look for a character of FE at the front of the data
		if (fileData[0] == 254)
		{
			dataPtr++;
			fileLength--;
		}
	}

	if (edi)
	{
		if  ((fileLength > 106) && (memcmp(dataPtr, "ISA", 3) == 0))
		{
			delim = dataPtr[105];
			edimsg = true;
		}
		else
		{
			if ((fileLength > 10) && (memcmp(dataPtr, "UNA", 3) == 0))
			{
				delim = dataPtr[8];
				escape = dataPtr[6];
				edimsg = true;
			}
			else
			{
				if ((fileLength > 10) && (memcmp(dataPtr, "UNB", 3) == 0))
				{
					delim = '\'';
					escape = '?';
					edimsg = true;
				}
			}
		}
	}

	// calculate the length required
	if ((HONOR_CRLF == crlf) || (edimsg))
	{
		// have to allow for more characters, since there may be
		// an overhead of as much as 12 characters for every character
		// in the original data stream
		len = (fileLength * 12) + 45;
	}
	else
	{
		// use one character per data character plus 3 more
		// characters for every 32 characters
		len = fileLength + ((fileSize / 8) * 3) + 45;
	}

	m_data_ascii = (unsigned char *) rfhMalloc(len, "ASCIIDAT");
	m_data_ascii[0] = 0;
	asciiStr = m_data_ascii;

	// figure out how much data
	remaining = fileLength;

	// check if the data is in Unicode
	// support is provided for code pages 1200, 13488 and 17584
//	if (((1200 == fileCcsid) || (13488 == fileCcsid) || (17584 == fileCcsid)) && (m_char_format != CHAR_EBCDIC))
	if (0)
	{
		unicode = true;		// remember we are dealing with wide characters

		// figure out what code page to use
		switch (m_char_format)
		{
		case CHAR_CHINESE:
			{
				ccsid = 936;
				break;
			}
		case CHAR_KOREAN:
			{
				ccsid = 949;
				break;
			}
		case CHAR_TRAD_CHIN:
			{
				ccsid = 950;
				break;
			}
		case CHAR_JAPANESE:
			{
				ccsid = 932;
				break;
			}
		case CHAR_RUSSIAN:
			{
				ccsid = 1251;
				break;
			}
		case CHAR_THAI:
			{
				ccsid = 874;
				break;
			}
		default:
			{
				// check what the oem code page is
				if ((app->ansi >= 1250) && (app->ansi <= 1258))
				{
					// use the proper code page for this system
					ccsid = app->ansi;
				}
				else
				{
					// pick a default
					ccsid = 1252;
				}

				break;
			}
		}

		// allocate a temporary data area for the multibyte data
		data = (unsigned char *)rfhMalloc(fileLength * 2 + 4, "WCSDATA ");
		wcsPtr = (RFH_UCS2 *)&dataPtr;

		// check if big-endian data rather than little-endian
		if (NUMERIC_PC == fileIntFormat)
		{
			// data appears to be little-Endian
			// check which translation to use
			if ((ccsid >= 1250) && (ccsid <= 1258))
			{
				// use the built-in translation routines
				for (i=0; i<(int)fileSize/2; i++)
				{
					switch (ccsid)
					{
					case 1250:
						{
							// translate the current character
							WCSto1250(wcsPtr++, data + i++);

							break;
						}
					case 1251:
						{
							// translate the current character
							WCSto1251(wcsPtr++, data + i++);

							break;
						}
					case 1252:
						{
							// translate the current character
							WCSto1252(wcsPtr++, data + i++);

							break;
						}
					case 1253:
						{
							// translate the current character
							WCSto1253(wcsPtr++, data + i++);

							break;
						}
					case 1254:
						{
							// translate the current character
							WCSto1254(wcsPtr++, data + i++);

							break;
						}
					case 1255:
						{
							// translate the current character
							WCSto1255(wcsPtr++, data + i++);

							break;
						}
					case 1256:
						{
							// translate the current character
							WCSto1256(wcsPtr++, data + i++);

							break;
						}
					case 1257:
						{
							// translate the current character
							WCSto1257(wcsPtr++, data + i++);

							break;
						}
					case 1258:
						{
							// translate the current character
							WCSto1258(wcsPtr++, data + i++);

							break;
						}
					}
				}

				// set the size of the translated data
				dataSize = fileSize >> 1;
			}
			else
			{
				// try to use Windoze to do the translation
				dataSize = rfh_WideCharToMultiByte(ccsid, WC_DEFAULTCHAR, (const RFH_UCS2 *)fileData, fileSize / 2, (char *)data, fileSize * 2, &defChar, NULL);
			}
		}
		else
		{
			// data is UCS-2 and appears to be big-Endian
			// allocate another temporary area to reverse the bytes into
			tempData = (char *)rfhMalloc(fileSize + 2, "TEMPDAT2");

			i = 0;
			while (i < (int)fileLength)
			{
				// get the second of a pair of bytes
				tempData[i] = dataPtr[i + 1];
				tempData[i + 1] = dataPtr[i];
				i += 2;
			}

			// check which translation to use
			if ((ccsid >= 1250) && (ccsid <= 1258))
			{
				// point to the UCS-2 data
				wcsPtr = (RFH_UCS2 *)tempData;

				// use the built-in translation routines
				for (i=0; i<(int)fileLength/2; i++)
				{
					switch (ccsid)
					{
					case 1250:
						{
							// translate the current character
							WCSto1250(wcsPtr++, data + i++);

							break;
						}
					case 1251:
						{
							// translate the current character
							WCSto1251(wcsPtr++, data + i++);

							break;
						}
					case 1252:
						{
							// translate the current character
							WCSto1252(wcsPtr++, data + i++);

							break;
						}
					case 1253:
						{
							// translate the current character
							WCSto1253(wcsPtr++, data + i++);

							break;
						}
					case 1254:
						{
							// translate the current character
							WCSto1254(wcsPtr++, data + i++);

							break;
						}
					case 1255:
						{
							// translate the current character
							WCSto1255(wcsPtr++, data + i++);

							break;
						}
					case 1256:
						{
							// translate the current character
							WCSto1256(wcsPtr++, data + i++);

							break;
						}
					case 1257:
						{
							// translate the current character
							WCSto1257(wcsPtr++, data + i++);

							break;
						}
					case 1258:
						{
							// translate the current character
							WCSto1258(wcsPtr++, data + i++);

							break;
						}
					}
				}

				// set the size of the translated data
				dataSize = fileLength >> 1;
			}
			else
			{
				// translate from Unicode to multi-byte
				dataSize = rfh_WideCharToMultiByte(ccsid, WC_DEFAULTCHAR, (const RFH_UCS2 *)tempData, fileSize / 2, (char *)data, fileSize * 2, &defChar, NULL);
			}

			// release the temporary area
			rfhFree(tempData);
		}

		// check if the translation worked
		// it will not work if the user does not have the right code pages installed
		if (0 == dataSize)
		{
			// translation did not work - ignore unicode code page
			rfhFree(data);
			data = fileData;
			dataSize = fileLength;
			unicode = false;
		}
		else
		{
			// get the number of translated characters
			remaining = dataSize;
		}
	}
	else
	{
		data = dataPtr;
		dataSize = fileLength;
	}

	// check if have to replace the number of bytes
	// This is necessary for certain multi-byte code pages
	// with the number of characters
	if (charFormat > CHAR_EBCDIC)
	{
		dbcsCount = 0;
		i = 0;
		while (i < dataSize)
		{
			// check for two byte character
			if (isLeadChar(data[i], m_char_format))
			{
				i++;
			}

			dbcsCount++;
			i++;
		}

		remaining = dbcsCount;
	}

	// loop to write out all the text lines
	// note that remaining is a character count
	// for ascii and ebcdic, there is only one
	// character per byte, but not for dbcs
	while (remaining > 0)
	{
		// check if honoring crlf sequences
		if (crlf == HONOR_CRLF)
		{
			// strip off any leading crlf sequences in the front of the data
			while ((remaining > 0) && (('\n' == data[buffer]) || ('\r' == data[buffer])))
			{
				remaining--;
				buffer++;
				location++;
			}
		}

		// add the offset to the beginning of the line
		sprintf((char *) asciiStr, "%8.8d ", location);

		if ((edimsg) && (delim != 0))
		{
			count = findDelim(data + buffer, remaining, delim, escape);
		}
		else
		{
			if (remaining > MAX_ASCII_CHAR)
			{
				count = MAX_ASCII_CHAR;
			}
			else
			{
				count = remaining;
			} // endif
		}

		// check if honoring crlf sequences in the text
		if (crlf == HONOR_CRLF)
		{
			// update the count to the number of characters, up to MAX_ASCII_CHAR
			// set the count to the remaining characters in case no crlf is found
			count = remaining;
			count = findcrlf(data + buffer, count, charFormat);
		}

		// update the location counter for the display of the offset (first 8 characters)
		location += count;

		// initialize the output strings and buffer offset
		offset = buffer;
		i = 9;    // initialize variable offset after offset
		k = 0;    // initialize counter

		if ((edimsg) && (delim != 0))
		{
			// make sure not pointing at a CR or LF
			newline = 0;
			while ((newline < remaining) && (data[buffer + newline] <= ' '))
			{
				newline++;
			}

			// check if should adjust the indent
			if (memcmp(data + buffer + newline, "IEA", 3) == 0)
			{
				indent = 0;
			}
			else if (memcmp(data + buffer + newline, "GE", 2) == 0)
			{
				indent--;
			}
			else if (memcmp(data + buffer + newline, "SE", 2) == 0)
			{
				indent--;
			}

			// check if indenting
			if (indentOn)
			{
				j = 0;
				while (j < indent)
				{
					asciiStr[i++] = ' ';
					j++;
				}
			}

			// check if should adjust the indent
			if (memcmp(data + buffer + newline, "ISA", 3) == 0)
			{
				indent = 1;

				// new ISA - the delimiters may have changed - grab the new one
				if  (remaining > 106 + newline)
				{
					delim = data[buffer + newline + 105];
				}
			}
			else if (memcmp(data + buffer + newline, "GS", 2) == 0)
			{
				indent++;
			}
			else if (memcmp(data + buffer + newline, "ST", 2) == 0)
			{
				indent++;
			}
			else if (memcmp(data + buffer + newline, "UNA", 3) == 0)
			{
				// update the delimiter and escape characters
				delim = data[buffer + newline + 8];
				escape = data[buffer + newline + 6];
			}
		}

		while (k < count)
		{
			if (charFormat > CHAR_EBCDIC)
			{
				// handle double byte characters in the text
				// get the first character from the buffer
				ch = data[buffer];

				// check for double-byte character
				if (isLeadChar(ch, m_char_format))
				{
					// handle first byte of double byte character
					asciiStr[i++] = ch;

					// double-byte character
					location++;
					buffer++;

					// make sure that embedded nulls do not terminate our string prematurely
					if (0 == data[buffer])
					{
						// do not truncate the string by accident
						asciiStr[i++] = '.';
					}
					else
					{
						// not a zero character - hopefully OK
						asciiStr[i++] = data[buffer];
					}
				}
				else
				{
					asciiStr[i++] = invalidCharTable[ch];
				}
			}
			else
			{
				if (CHAR_EBCDIC == charFormat)
				{
					if (asciiCcsid > 0)
					{
						// start by trying to do a proper translation, using Windows or MQ
						tcount = xlateEbcdicChar(fileCcsid, asciiCcsid, (char *)data + buffer, (char *)tempTxt);

						// check if the translation worked
						if (1 == tcount)
						{
							// Windows or MQ was able to perform the translation
							// make sure character is displayable and
							// move on to the next output character
							asciiStr[i++] = invalidCharTable[tempTxt[0]];
							winXlate++;
						}
					}

					// wind up with a single character properly translated?
					if ((asciiCcsid <= 0) || (0 == tcount))
					{
						// use a default translation table
						asciiStr[i++] = invalidCharTable[eatab[data[buffer]]];
						defXlate++;
					}
				}
				else
				{
					// check for non-displayable characters
					// note that this check assumes code page 1252
					asciiStr[i++] = invalidCharTable[data[buffer]];
				} // endif
			}

			// Go on to the next character
			remaining--;
			buffer++;
			k++;   // increment counter
		} // endwhile

		// check if honoring crlf sequences
		if (crlf == HONOR_CRLF)
		{
			// strip off any crlf sequences, in case that is all that is left
			while ((remaining > 0) && (('\n' == data[buffer]) || ('\r' == data[buffer])))
			{
				remaining--;
				buffer++;
				location++;
			}
		}

		if (remaining > 0)
		{
			asciiStr[i++] = '\r';	// Add a carriage return character to the line
			asciiStr[i++] = '\n';	// Add a new line character to the line
		}

		asciiStr[i] = '\0';		// Terminate the string

		asciiStr += i;
	} // endwhile

	if ((unicode) && (data != NULL))
	{
		rfhFree(data);
	}

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting DataFormatter::getCharacterData() with strlen(m_data_ascii)=%d m_data_ascii=%8.8X buffer=%d location=%d i=%d winXlate=%d MQXlate=%d defXlate=%d asciiCcsid=%d", (int)strlen((const char *)m_data_ascii), (unsigned int)(uintptr_t)m_data_ascii, buffer, location, i, winXlate, MQXlate, defXlate, asciiCcsid);

		// trace exit from getCharacterData
		logTraceEntry(traceInfo);
	}
}

void DataFormatter::getBothData(const int charFormat, const int BOM)

{
	// turn the file data into a hex and character format
	// replace any unprintable characters with periods
	unsigned char	*asciiStr;
	unsigned char	*hexStr;
	unsigned char	*dataPtr = fileData;
	unsigned char	ch;			// work variable
	int			i;				// work variable
	int			j;				// work variable
	int			k;				// work variable
	int			tcount;			// number of characters translated
	int			winXlate=0;		// number of EBCDIC characters translated using Windows
	int			MQXlate=0;		// number of EBCDIC characters translated using MQ translate tables
	int			defXlate=0;		// number of EBCDIC characters translated using built-in default table
	int			asciiCcsid=0;	// ASCII code page to use for EBCDIC to ASCII translations
	UINT		len;			// length of required area to acquire
	int			fileLength = fileSize;
	int			buffer=0;
	int			remaining;		// number of bytes left to process
	int			offset;			// current location within the raw file data
	int			count;			// number of lines
	int			location=0;		// offset of start of this line within the file
	RFH_UCS2		ucsChar[4];		// area to translate one character
	char		tempCh[4];		// work area for translation
	char		defChar='.';
	char		traceInfo[512];		// work variable to build trace message

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering DataFormatter::getBothData() with charFormat=%d BOM=%d m_data_both=%8.8X", charFormat, BOM, (unsigned int)(uintptr_t)m_data_both);

		// trace entry to getBothData
		logTraceEntry(traceInfo);

		// check if verbose trace is enabled
		if (verboseTrace)
		{
			// dump out the formatted data
			dumpTraceData("fileData", fileData, fileSize);
		}
	}

	// check if already have the proper data formatted
	if (m_data_both != NULL)
	{
		// did options change?
		if ((saveBothFormat == charFormat) && (saveBOM == BOM))
		{
			if (traceEnabled)
			{
				// trace early exit from getBothData
				logTraceEntry("Exit from DataFormatter::getBothData() - reuse existing data");
			}

			// no need to do anything - no changes to formating detected
			return;
		}

		// need to change the data to match the desired format
		rfhFree(m_data_both);
	}

	if (invalidCharTableNotBuilt)
	{
		// figure out what code page this system is using
		// and figure out what the valid code points are
		buildInvalidCharTable(invalidCharTable);
		invalidCharTableNotBuilt = FALSE;
	}

	// need to do EBCDIC to ASCII translation?
	if (CHAR_EBCDIC == charFormat)
	{
		// figure out what code page to translate to
		// only need to do this once
		asciiCcsid = matchEbcdicToAscii(fileCcsid);
	}

	// remember the character format
	saveBothFormat = charFormat;

	// check for BOM
	if (BOM)
	{
		// check for a byte order mark in the data
		if (dataPtr[0] == 254)
		{
			// adjust the temporary pointer and data length
			dataPtr++;
			fileLength--;
		}
	}
	// calculate the required length
	// use three characters per data character plus 14 more
	// characters for every 16 characters
	len = ((fileLength + 15) / 16) * 65 + 65;		// 65 bytes per line of 16; Windows allocates (fileLength << 2) + 65
	m_data_both = (unsigned char *) rfhMalloc(len, "BOTHDATA");
	m_data_both[0] = 0;
	hexStr = m_data_both;

	// figure out how much data
	remaining = fileLength;

	while (remaining > 0)
	{
		// add the offset to the beginning of the line
		sprintf((char *) hexStr, "%8.8d ", location);

		location += 16;

		if (remaining > 16)
		{
			count = 16;
		}
		else
		{
			count = remaining;
		} // endif

		// initialize the output strings and buffer offset
		offset = buffer;
		asciiStr = hexStr + 45;
		i = 0;    // initialize variable offset
		j = 9;    // initial variable offset

		k = 0;    // initialize counter

		while (k < count)
		{
			if (CHAR_EBCDIC == charFormat)
			{
				// recognize the ccsid as an EBCDIC code page?
				if (asciiCcsid > 0)
				{
					// start by trying to do a proper translation, using Windows
					// this requires that the code page be installed
					tcount = rfh_MultiByteToWideChar(fileCcsid, 0, (char *)dataPtr + buffer, 1, ucsChar, 1);
					winXlate += tcount;

					// check if windows was able to do the translation
					if (0 == tcount)
					{
						// see if MQ provides a translation table for the codepage in question
						tcount = MQEBC2UCS(ucsChar, dataPtr + buffer, 1, fileCcsid);
						MQXlate += tcount;
					}

					// did the translation succeed?
					if (tcount > 0)
					{
						tcount = rfh_WideCharToMultiByte(asciiCcsid, 0, ucsChar, 1, (char *)&tempCh, 2, &defChar, NULL);
					}
				}

				// wind up with a single character properly translated?
				if ((asciiCcsid > 0) && (1 == tcount))
				{
					// move on to the next output character
					ch = tempCh[0];
				}
				else
				{
					// use a default EBCDIC/ASCII translation
					ch = eatab[dataPtr[buffer]];
					defXlate++;
				}
			}
			else
			{
				ch = dataPtr[buffer];
			} // endif

			// Make sure have a character that can be displayed
			asciiStr[i++] = invalidCharTable[ch];

			// always use the file data for the hex display
			ch = (unsigned char) dataPtr[buffer] >> 4;
			ch = HEX_NUMBERS[ch];
			hexStr[j++] = ch;

			ch = (unsigned char) dataPtr[buffer] & 0x0F;
			ch = HEX_NUMBERS[ch];
			hexStr[j++] = ch;

			// Go on to the next character
			remaining--;
			buffer++;
			k++;   // increment counter

			// Insert a blank for every fourth character in hex string
			if (k%4 == 0)
			{
				hexStr[j++] = ' ';
			} // endif

			// Insert a blank for every eighth character in ASCII string
			if (k%8 == 0)
			{
				asciiStr[i++] = ' ';
			} // endif
		} // endwhile

		// set first tab character
		//asciiStr[8] = '\t';

		if (remaining > 0)
		{
			asciiStr[i++] = '\r';	// Add a carriage return character to the line
			asciiStr[i++] = '\n';	// Add a new line character to the line
		}
		else
		{
			asciiStr[i] = 0;
			// pad the ascii string with blanks to proper length
			while (j < 45)
			{
				hexStr[j++] = ' ';
			}
		}

		hexStr = asciiStr + i;
	} // endwhile

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting DataFormatter::getBothData() with strlen(m_data_both)=%d m_data_both=%8.8X winXlate=%d MQXlate=%d defXlate=%d", (int)strlen((const char *)m_data_both), (unsigned int)(uintptr_t)m_data_both, winXlate, MQXlate, defXlate);

		// trace exit from getBothData
		logTraceEntry(traceInfo);

		// check if verbose trace is enabled
		if (verboseTrace)
		{
			// dump out the formatted data
			dumpTraceData("Both Data", m_data_both, (unsigned int)strlen((char *)m_data_both));
		}
	}
}

int DataFormatter::checkForUnicodeMarker(unsigned char *data)

{
	// check for byte order mark in either big endian or little endian
	if (((255 == data[0]) && (254 == data[1])) || ((254 == data[0]) && (255 == data[1])))
	{
		return 2;
	}
	else
	{
		return 0;
	}
}

unsigned char * DataFormatter::getTranslatedData(int * length, const int charFormat, const int ccsid)

{
	int				i=0;				// work variable
	int				j=0;				// work variable
	int				tcount;				// number of characters that were tranlated by Windows
	int				tcount2;			// number of characters that were tranlated by Windows
	int				winXlate=0;			// number of characters translated by Windows
	int				MQXlate=0;			// number of characters translated using MQ tables
	int				DefXlate=0;			// number of characters translated using default table
	int				asciiCcsid=0;		// ASCII code page to use for EBCDIC to ASCII translations
	int				charCount=0;		// number of UTF-8 characters after transformation from UCS-2
	int				extraOffset=0;		// will be 2 if there is a UCS-2 file marker at the front of the data
	int				endian=0;			// used for UCS-2 data only, endian is 0 for little endian data and 1 for big endian data
	int				mByte;				// indicator if data contains multibyte characters
	int				bom;				// indicator if byte order mark found for code page 1208
	int				failedChars=0;		// number of characters that could not be translated properly
	int				failedChars2=0;		// number of characters that could not be translated back properly
	int				chars1252=0;		// number of characters translated to 1252 code page
	int				charsASCII=0;		// number of characters that no translation was necessary for
	int				charsUCS=0;			// number of characters translated to UCS
	RFH_UCS2			ucs;				// wide character area to translate one character
	RFH_UCS2			ucsChar[4];			// wide character area to translate one character
	unsigned char	*newData=NULL;		// pointer to the ASCII or UTF-8 data area
	unsigned char	*tempData=NULL;		// temporary work area used to convert big-endian UCS-2 data to little-endian
	unsigned char	*mbcsData;			// pointer to mbcs data to check for multibyte characters
	char			traceInfo[512];		// work variable to build trace message
	char			defChar='.';		// character to be used for characters that cannot be translated

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering DataFormatter::getTranslatedData() with charFormat=%d ccsid=%d fileSize=%d", charFormat, ccsid, fileSize);

		// trace entry to getTranslatedData
		logTraceEntry(traceInfo);

		// check for verbose trace
		if (verboseTrace)
		{
			// dump out the input data
			dumpTraceData("newData", fileData, fileSize);
		}
	}

	// find the application object

	// initialize the length field to zero
	(*length) = 0;

	// check the ccsid for a UCS-2 data area
	if ((13488 == ccsid) || (17584 == ccsid) || (1200 == ccsid) || (1201 == ccsid))
	{
		// the data is in UCS-2 format
		// the next step is to figure out whether the 16-bit characters are big or little endian
		// the first step is to look for a byte order marker at the front of the data
		extraOffset = checkForUnicodeMarker(fileData);

		if (extraOffset > 0)
		{
			// there is a byte order marker
			// check if this is a big endian marker
			if ((255 == fileData[0]) && (254 == fileData[1]))
			{
				endian = 1;
			}
		}
		else
		{
			// no byte order marker, so have to look at the code page and encoding bits
			// check the code page
			if ((13488 == ccsid) || (17584 == ccsid) || (1200 == ccsid))
			{
				// check the data encoding
				if (fileIntFormat != NUMERIC_PC)
				{
					endian = 1;
				}
			}
			else
			{
				// code page 1201 is supposed to be big-endian
				endian = 1;
			}
		}

		// get a temporary area to hold the data after it is converted to multi-byte
		newData = (unsigned char *) rfhMalloc(fileSize * 2 + 1, "MULTIBYT");

		// check if the data is little-endian (1200) or big-endian (1201)
		if (0 == endian)
		{
			// little endian data - translate it as is
			// handle two byte unicode directly in the data area
			charCount = rfh_ucs2_to_c_locale((const RFH_UCS2 *)(fileData + extraOffset), (char *)newData, fileSize + 1 - extraOffset);

			// check if the conversion worked
			if ((0 == charCount) && ((1252 == app->ansi) || (1252 == app->oem)))
			{
				// the conversion failed
				// try translating to code page 1252 from UCS
				for (i=0; i<(int)((fileSize - extraOffset) / 2); i++)
				{
					// translate a character from UCS to code page 1252
					charCount = WCSto1252((const RFH_UCS2 *)(fileData + extraOffset) + i, newData + i);
				}

				// count for the trace line
				chars1252++;
			}

			// check if the conversion worked
			if (charCount > 0)
			{
				(*length) = (int)strlen((char *)newData);
				winXlate += charCount;
			}
		}
		else
		{
			// big-endian so the byte ordering must be changed before the data can be translated
			// start by converting to little-endian
			// get a temporary area to hold the data after it is converted to little-endian
			tempData = (unsigned char *) rfhMalloc(fileSize + 2, "TEMPDAT3");

			// copy the original data reversing each pair of bytes
			for (i=0; i<(int)fileSize; i += 2)
			{
				tempData[i] = fileData[i + 1];
				tempData[i + 1] = fileData[i];
			}

			// terminate the string properly (termination character is 2 bytes)
			tempData[fileSize] = 0;
			tempData[fileSize + 1] = 0;

			// convert the data from 2-byte unicode to multi-byte
			charCount = rfh_ucs2_to_c_locale((const RFH_UCS2 *)(tempData + extraOffset), (char *)newData, (fileSize - extraOffset)/2 + 1);

			// check if the conversion worked
			if (charCount > 0)
			{
				(*length) = (int)strlen((char *)newData);
				winXlate += charCount;
			}

			// release the temporary data area
			rfhFree(tempData);
		}
	}
	else if (1208 == ccsid)
	{
		// initialize some working variables
		mByte = 0;
		i = 0;

		// check for a BOM at the front of the data
		bom = 0;
		if ((0xef == fileData[0]) && (0xbb == fileData[1]) && (0xbf == fileData[2]))
		{
			// if found skip it
			bom = 1;
			i = 3;
		}

		// translate the data to UCS-2 first and then back to ascii characters
		// this might help to make some european characters easier to translate
		// first check if there are any multibyte characters in the data stream
		while ((i < (int)fileSize) && (0 == mByte))
		{
			if (fileData[i] > 127)
			{
				mByte = 1;
			}

			i++;
		}

		// check if any multibyte characters were in fact found
		if ((0 == mByte) && (0 == bom))
		{
			// nothing to do
			// return a pointer to the original data
			newData = fileData;

			// set the length
			(*length) = fileSize;
		}
		else
		{
			// try to translate some of the data to ASCII
			// point to the original data
			mbcsData = fileData;

			// allocate storage to hold the translated characters
			newData = (unsigned char *)rfhMalloc(fileSize + 1, "MBCSDATA");

			if (NULL == newData)
			{
				// malloc failed - just display the original data
				newData = fileData;

				if (traceEnabled)
				{
					// trace error allocating storage
					logTraceEntry("DataFormatter::getTranslatedData() malloc failed");
				}
			}
			else
			{
				i = 0;
				j = 0;
				// check for a byte order mark at the front of the data
				if (1 == bom)
				{
					// skip the byte order mark characters
					i = 3;
				}

				// check if the data needs to be translated
				if (1 == mByte)
				{
					// translate the individual characters
					while (i < (int)fileSize)
					{
						// does this character need translation?
						if (mbcsData[i] < 128)
						{
							// normal ASCII character - just copy it
							newData[j++] = mbcsData[i++];

							// count for the trace line
							charsASCII++;
						}
						else
						{
							// translate the character to a single UCS-2 character
							//tcount = mbtowc(&ucs, (const char *)mbcsData + i, numBytes);
							tcount = char1208toWCS(&ucs, mbcsData + i);

							// count for the trace line
							charsUCS++;

							// did the first translation work?
							if (tcount > 0)
							{
								// conversion to UCS-2 worked
								// try to translate to the code page for this system
								tcount2 = rfh_WideCharToMultiByte(app->ansi, WC_DEFAULTCHAR | WC_DISCARDNS, &ucs, 1, (char *)newData + j, MB_CUR_MAX + 1, ".", NULL);

								// check if the translation failed
								if (0 == tcount2)
								{
									// try again with the OEM code page for this system
									tcount2 = rfh_WideCharToMultiByte(app->oem, WC_DEFAULTCHAR | WC_DISCARDNS, &ucs, 1, (char *)newData + j, MB_CUR_MAX + 1, ".", NULL);
								}

								// did the translation fail?
								if ((0 == tcount2) && ((1252 == app->ansi) || (1252 == app->oem)))
								{
									// try translating to code page 1252
									tcount2 = WCSto1252(&ucs, newData + j);

									// count for the trace line
									chars1252++;
								}
								else if ((0 == tcount2) && ((1250 == app->ansi) || (1250 == app->oem)))
								{
									// try translating to code page 1250
									tcount2 = WCSto1250(&ucs, newData + j);

									// count for the trace line
									chars1252++;
								}
								else if ((0 == tcount2) && ((1251 == app->ansi) || (1251 == app->oem)))
								{
									// try translating to code page 1251
									tcount2 = WCSto1251(&ucs, newData + j);

									// count for the trace line
									chars1252++;
								}
								else if ((0 == tcount2) && ((1253 == app->ansi) || (1253 == app->oem)))
								{
									// try translating to code page 1253
									tcount2 = WCSto1253(&ucs, newData + j);

									// count for the trace line
									chars1252++;
								}
								else if ((0 == tcount2) && ((1254 == app->ansi) || (1254 == app->oem)))
								{
									// try translating to code page 1254
									tcount2 = WCSto1254(&ucs, newData + j);

									// count for the trace line
									chars1252++;
								}
								else if ((0 == tcount2) && ((1255 == app->ansi) || (1255 == app->oem)))
								{
									// try translating to code page 1255
									tcount2 = WCSto1255(&ucs, newData + j);

									// count for the trace line
									chars1252++;
								}
								else if ((0 == tcount2) && ((1256 == app->ansi) || (1256 == app->oem)))
								{
									// try translating to code page 1256
									tcount2 = WCSto1256(&ucs, newData + j);

									// count for the trace line
									chars1252++;
								}
								else if ((0 == tcount2) && ((1257 == app->ansi) || (1257 == app->oem)))
								{
									// try translating to code page 1257
									tcount2 = WCSto1257(&ucs, newData + j);

									// count for the trace line
									chars1252++;
								}
								else if ((0 == tcount2) && ((1258 == app->ansi) || (1258 == app->oem)))
								{
									// try translating to code page 1258
									tcount2 = WCSto1258(&ucs, newData + j);

									// count for the trace line
									chars1252++;
								}

								// did the conversion back succeed?
								if (0 == tcount2)
								{
									// keep count for trace purposes
									failedChars2++;

									// it failed - copy the original characters to the output buffer
									newData[j++] = mbcsData[i++];
								}
								else
								{
									// move past the last character in the output buffer, allowing for the null character appended
									j += tcount2;

									// skip past the characters that were processed in the input buffer
									i += tcount;
								}
							}
							else
							{
								// keep count for trace purposes
								failedChars++;

								// not a valid character - just copy it
								newData[j++] = mbcsData[i++];
							}
						}
					}
				}
				else
				{
					// just removing a byte order mark
					// copy the data starting with the fourth byte
					j = (int)fileSize - 3;
					memcpy(newData, mbcsData + 3, j);
				}
			}

			// terminate the string
			newData[j] = 0;

			// set the length
			(*length) = j;
		}
	}
	else
	{
		// Get the translated file data into a buffer
		if (charFormat == CHAR_EBCDIC)
		{
			// figure out what code page to translate to
			// only need to do this once
			asciiCcsid = matchEbcdicToAscii(ccsid);

			newData = (unsigned char *) rfhMalloc(fileSize * 2 + 1, "EBCDIC  ");
			if (ccsid > 0)
			{
				// do a translation, first trying to use the MQ translation tables and if not found
				// try the windows translation tables

				i = 0;
				j = 0;
				while (i < (int)fileSize)
				{
					if (asciiCcsid > 0)
					{
						// start by trying to do a proper translation, using Windows
						tcount = rfh_MultiByteToWideChar(ccsid, 0, (char *)fileData + i, 1, ucsChar, 1);
						winXlate += tcount;

						// check if windows was able to do the translation
						if (0 == tcount)
						{
							// see if MQ provides a translation table for the codepage in question
							tcount = MQEBC2UCS(ucsChar, fileData + i, 1, ccsid);
							MQXlate += tcount;
						}

						// did the function succeed or were able to use an MQ table?
						if (tcount > 0)
						{
							tcount = rfh_WideCharToMultiByte(asciiCcsid, 0, ucsChar, 1, (char *)newData + j, 2, &defChar, NULL);

							// check if the translation worked
							if ((0 == tcount) && ((1252 == app->ansi) || (1252 == app->oem)))
							{
								// try translating to code page 1252 from UCS
								tcount2 = WCSto1252(&ucsChar[0], newData + j);

								// count for the trace line
								chars1252++;
							}
							else if ((0 == tcount) && ((1250 == app->ansi) || (1250 == app->oem)))
							{
								// try translating to code page 1250 from UCS
								tcount2 = WCSto1250(&ucsChar[0], newData + j);

								// count for the trace line
								chars1252++;
							}
							else if ((0 == tcount) && ((1251 == app->ansi) || (1251 == app->oem)))
							{
								// try translating to code page 1251 from UCS
								tcount2 = WCSto1251(&ucsChar[0], newData + j);

								// count for the trace line
								chars1252++;
							}
							else if ((0 == tcount) && ((1253 == app->ansi) || (1253 == app->oem)))
							{
								// try translating to code page 1253 from UCS
								tcount2 = WCSto1253(&ucsChar[0], newData + j);

								// count for the trace line
								chars1252++;
							}
							else if ((0 == tcount) && ((1254 == app->ansi) || (1254 == app->oem)))
							{
								// try translating to code page 1254 from UCS
								tcount2 = WCSto1254(&ucsChar[0], newData + j);

								// count for the trace line
								chars1252++;
							}
							else if ((0 == tcount) && ((1255 == app->ansi) || (1255 == app->oem)))
							{
								// try translating to code page 1255 from UCS
								tcount2 = WCSto1255(&ucsChar[0], newData + j);

								// count for the trace line
								chars1252++;
							}
							else if ((0 == tcount) && ((1256 == app->ansi) || (1256 == app->oem)))
							{
								// try translating to code page 1256 from UCS
								tcount2 = WCSto1256(&ucsChar[0], newData + j);

								// count for the trace line
								chars1252++;
							}
							else if ((0 == tcount) && ((1257 == app->ansi) || (1257 == app->oem)))
							{
								// try translating to code page 1257 from UCS
								tcount2 = WCSto1257(&ucsChar[0], newData + j);

								// count for the trace line
								chars1252++;
							}
							else if ((0 == tcount) && ((1258 == app->ansi) || (1258 == app->oem)))
							{
								// try translating to code page 1258 from UCS
								tcount2 = WCSto1258(&ucsChar[0], newData + j);

								// count for the trace line
								chars1252++;
							}
						}
					}

					// wind up with a single character properly translated?
					if ((asciiCcsid > 0) && (1 == tcount))
					{
						// Windows was able to perform the translation
						// make sure have a displayable character and
						// move on to the next output character
						newData[j] = invalidCharTable[newData[j]];
					}
					else
					{
						// check if MQ translation failed before, to avoid the overhead of
						// going to the registry for each character
						// windows could not do the translation so see if can use an MQ table
						tcount = MQEBC2ASC(newData + j, fileData + i, 1, ccsid, asciiCcsid);

						// check if the translation worked
						if (tcount > 0)
						{
							// use the translated character
							newData[j] = invalidCharTable[newData[j]];
						}
						else
						{
							// use a default translation table
							newData[j] = invalidCharTable[eatab[fileData[i]]];
						}
					}

					i++;
					j++;
				}
				// try to use the code pages to do a better translation
				//translateEbcdicData(newData, fileData, fileSize, ccsid);
			}
			else
			{
				// use the default translation tables
				EbcdicToAscii((const unsigned char *)fileData, fileSize, newData);
				DefXlate += fileSize;
			}
		}
		else
		{
			newData = fileData;
		}

		(*length) = fileSize;
	}

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting DataFormatter::getTranslatedData() newData=%8.8X length=%d charCount=%d extraOffset=%d winXlate=%d MQXlate=%d DefXlate=%d failedChars=%d, failedChars2=%d chars1252=%d charsASCII=%d charsUCS=%d bom=%d mByte=%d i=%d j=%d", (unsigned int)(uintptr_t)newData, (*length), charCount, extraOffset, winXlate, MQXlate, DefXlate, failedChars, failedChars2, chars1252, charsASCII, charsUCS, bom, mByte, i, j);

		// trace exit from getTranslatedData
		logTraceEntry(traceInfo);

		// check for verbose trace
		if (verboseTrace)
		{
			dumpTraceData("newData", newData, (*length));
		}
	}

	return newData;
}

void DataFormatter::getXmlData(const int charFormat)

{
	// turn the file data into a xml format and return it as a CString
	// replace any unprintable characters with periods
	//
	// Recognize the following constructs
	//
	// <?xml ... > - XML Header
	// <!...     > - XML non-element item
	// <varname>..vardata..</varname>
	// <varname/>
	// <varname attr=value..> or <varname attr=value.../>
	//
	unsigned char	*tempData;			// pointer to temporary work area
	unsigned char	*tempData2;			// pointer to temporary work area
	char			*errmsg=NULL;		// pointer to error message
	int				i;					// work variable
	int				j;					// work variable
	int				savei;				// work variable
	int				rc;					// return code from called subroutines
	int				buffer=0;			// index of the next character to process
	int				remaining=0;		// number of bytes left to process
	int				len;				// length of bytes of memory to acquire
	int				length=0;			// number of bytes in the data area
	int				currentLevel=0;		// current depth relative to root element (root = 0)
	int				currentOfs=0;		// number of spaces to insert before a line
	int				endvar=0;			// indicator that end tag has been found
	int				specvar=0;			// null tag found
	int				whitespace=0;		// number of whitespace characters found between tags
	int				tempbuffer;
	char			traceInfo[512];		// work area to build trace message

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering DataFormatter::getXmlData() with charFormat=%d fileCcsid=%d fileSize=%d", charFormat, fileCcsid, fileSize);

		// trace entry to getXmlData
		logTraceEntry(traceInfo);
	}

	// check if already have the proper data formatted
	if (m_data_xml != NULL)
	{
		// did we change options?
		if (charFormat == saveXMLFormat)
		{
			if (traceEnabled)
			{
				// create the trace line
				sprintf(traceInfo, "Exiting DataFormatter::getXmlData() already exists m_data_xml=%8.8X strlen(m_data_xml)=%d", (unsigned int)(uintptr_t)m_data_xml, (int)strlen((char *)m_data_xml));

				// trace exit from getXmlData
				logTraceEntry(traceInfo);
			}

			// the area already exists - just return
			return;
		}

		rfhFree(m_data_xml);
		m_data_xml = NULL;
	}

	// remember the type of display data
	saveXMLFormat = charFormat;

	// calculate the length we require
	// we use one character per data character plus 3 more
	// characters for every 32 characters for any CR or LF characters
	len = (fileSize * 4) + 4096;
	m_data_xml = (unsigned char *) rfhMalloc(len, "XMLDATA ");
	m_data_xml[0] = 0;

	// check if there is any data to format
	if (0 == fileSize)
	{
		// nothing to do
		// this will result in m_data_xml pointing to a null string
		return;
	}

	// translate the data to ASCII as necessary
	// if no translation is required a pointer to the original data in fileData is returned
	tempData2 = getTranslatedData(&length, charFormat, fileCcsid);

	// check if the data length is greater than zero
	if (0 == length)
	{
		// nothing to process
		// release the acquired data area if necessary
		if (tempData2 != fileData)
		{
			// release the acquired memory so we don't leak memory
			rfhFree(tempData2);
		}

		// return with m_data_xml set as a zero length string
		return;
	}

	//.initialize a temporary memory area that will hold the
	// processed data with any unnecessary xml characters removed
	tempData = (unsigned char *) rfhMalloc(length + 1, "TEMPDAT4");

	// check if the malloc failed
	if (NULL == tempData)
	{
		// set an error message in the data area
		strcpy((char *)m_data_xml, "**Error allocating memory");

		// release the acquired data area if necessary
		if (tempData2 != fileData)
		{
			// release the acquired memory so we don't leak memory
			rfhFree(tempData2);
		}

		// check if trace is enabled
		if (traceEnabled)
		{
			// create the trace line
			sprintf(traceInfo, "DataFormatter::getXmlData() memory allocation failure for %d bytes", length + 1);

			// trace entry to getXmlData
			logTraceEntry(traceInfo);
		}

		// return with m_data_xml set to an error message
		return;
	}

	// initialize the allocated memory
	memset(tempData, 0, length + 1);

	// figure out how much data we have after we
	// remove comments, invalid characters, etc
	remaining = removeUnneededXML(tempData2, length, tempData);

	// Do a quick sanity check on the data and insert a message if
	// this doesn't look like XML
	// the check includes things like making sure the number of
	// begin and end brackets match.
	rc = checkIfXml(tempData, length, &errmsg);

	if (rc > 0)
	{
		// display a message instead of the data
		strcpy((char *) m_data_xml, BAD_XML_MSG);
		strcat((char *) m_data_xml, errmsg);

		// release the acquired data area if necessary
		if (tempData2 != fileData)
		{
			// release the acquired memory so we don't leak memory
			rfhFree(tempData2);
		}

		// release the second memory area
		rfhFree(tempData);

		// is trace enabled?
		if (traceEnabled)
		{
			// create the trace line
			sprintf(traceInfo, "Exiting DataFormatter::getXmlData() Data does not appear to be XML - rc=%d errmsg=%.128s", rc, errmsg);

			// trace exit from getXmlData
			logTraceEntry(traceInfo);
		}

		return;
	}

	// do some initialization of variables and work areas
	i = 0;
	while ((buffer < remaining) && (i < (len - 2048)))
	{
		// first, check if we have a beginning bracket
		if (tempData[buffer] != '<')
		{
			// We have some junk, so we will just put
			// it on its own line until we find a begin bracket
			// find the next beginning bracket
			whitespace = 0;
			savei = i;

			while ((buffer < remaining) && (tempData[buffer] != '<'))
			{
				// check if we have something other than a space
				if (tempData[buffer] > ' ')
				{
					whitespace = 1;
				}

				// replace any newlines, carriage returns and tabs with periods
				if ((tempData[buffer] != '\r') &&
					(tempData[buffer] != '\n') &&
					(tempData[buffer] != 127) &&
					(tempData[buffer] != '\t'))
				{
					m_data_xml[i++] = tempData[buffer++];
				}
				else
				{
					m_data_xml[i++] = '.';
					buffer++;
				}
			}

			if (1 == whitespace)
			{
				// put this junk on its own line
				m_data_xml[i++] = '\r';
				m_data_xml[i++] = '\n';

				// at this point, we have built the next line, so
				// insert the proper offset and then append the data
				j = 0;
				while ((j < currentLevel) && (j < MAX_XML_INDENT_LEVELS))
				{
					m_data_xml[i++] = ' ';
					j++;
				}
			}
			else
			{
				// write over the junk
				i = savei;
			}
		}

		// check if we found a begin bracket
		if ((buffer < remaining) && ('<' == tempData[buffer]))
		{
			// we found a begin bracket
			m_data_xml[i++] = tempData[buffer++];

			endvar = 0;
			specvar = 0;
			// check if this is the end of a variable
			if ('/' == tempData[buffer])
			{
				// Ending variable, set a variable to remind us
				endvar = 1;

				// We need to back up one character, to get the offsets correct
				i--;

				// now, reinsert the begin bracket character
				m_data_xml[i - 1] = m_data_xml[i];
			}

			// check if this is special XML construct
			if (('?' == tempData[buffer]) || ('!' == tempData[buffer]))
			{
				// Ending variable, set a variable to remind us
				specvar = 1;
			}

			// now, see if we can find an end bracket, isolating the variable name
			while ((buffer < remaining) && (tempData[buffer] != '>'))
			{
				// if we are in a special part of the XML, we need to
				// look for square brackets and ignore anything in between
				if ((1 == specvar) && ('[' == tempData[buffer]))
				{
					// found square bracket, now we must find
					// the trailing square bracket, ignoring
					// anything in between
					// first, get the square bracket
					m_data_xml[i++] = tempData[buffer++];

					// now, capture all characters on this line until we
					// find the trailing bracket
					while ((buffer < remaining) && (tempData[buffer] != ']'))
					{
						m_data_xml[i++] = tempData[buffer++];
					}
				}

				if (buffer < remaining)
				{
					if ((tempData[buffer] >= ' ') && (tempData[buffer] != 127))
					{
						// check for an escape sequence
						if ('&' == tempData[buffer])
						{
							// found an escape sequence
							buffer += setEscChar(tempData + buffer, m_data_xml + i++);
						}
						else
						{
							m_data_xml[i++] = tempData[buffer++];
						}
					}
					else
					{
						m_data_xml[i++] = '.';
						buffer++;
					}
				}
			}

			if ((buffer < remaining) && ('>' == tempData[buffer]))
			{
				if ('/' == tempData[buffer-1])
				{
					specvar = 1;
				}

				// First, append the end bracket to the work area
				m_data_xml[i++] = tempData[buffer++];

				// We have find the end of a variable name
				// check if we are beginning a new variable
				// or ending a previous one
				if ((1 == endvar) || (1 == specvar))
				{
					if (1 == endvar)
					{
						// ending a previous variable
						// don't allow negative numbers in level
						if (currentLevel > 0)
						{
							// decrement the level of this variable
							currentLevel--;
						}
						else
						{
							// Result would have been negative - flag as error
							strcpy((char *) m_data_xml + i, "***** RFHUtil format error parsing XML - too few levels*****");
							i += (int)strlen((char *) m_data_xml + i);
						}
					}

					// append this variable with the proper offset
					m_data_xml[i++] = '\r';
					m_data_xml[i++] = '\n';

					// at this point, we have built the next line, so
					// insert the proper offset and then append the data
					j = 0;
					while ((j < currentLevel) && (j < MAX_XML_INDENT_LEVELS))
					{
						m_data_xml[i++] = ' ';
						j++;
					}
				}
				else
				{
					// beginning a new variable name
					// first, increase the level
					currentLevel++;

					// we may have some whitespace between this item and the next
					// we need to possibly skip this
					tempbuffer = buffer;
					while ((tempbuffer < remaining) && (tempData[tempbuffer] != '<'))
					{
						tempbuffer++;
					}

					// At this point, we want to find the rest of the line,
					// starting with any data for this variable
					// If there is no data, we need to check if we are starting a
					// new level or if we just have a null variable
					if (('<' == tempData[tempbuffer]) &&
						(tempData[tempbuffer + 1] != '/') &&
						(memcmp(tempData + buffer, CDATA_START, sizeof(CDATA_START) - 1) != 0))
					{
						// start a new line
						m_data_xml[i++] = '\r';
						m_data_xml[i++] = '\n';

						j = 0;
						while ((j < currentLevel) && (j < MAX_XML_INDENT_LEVELS))
						{
							m_data_xml[i++] = ' ';
							j++;
						}
					}
					else
					{
						// We may have some data, so we will just put
						// it on the same line
						// check for CDATA
						if ((buffer < remaining - 10) &&
							(memcmp(tempData + buffer, CDATA_START, sizeof(CDATA_START) - 1) == 0))
						{
							// process data as CDATA
							// skip the CDATA header
							buffer += sizeof(CDATA_START) - 1;

							// append the CDATA header to the output buffer
							strcpy((char *) m_data_xml + i, CDATA_START);
							i += sizeof(CDATA_START) - 1;

							while ((buffer < remaining) &&
								   (memcmp(tempData + buffer, CDATA_END, sizeof(CDATA_END) - 1) != 0))
							{
								if ((tempData[buffer] != '\r') &&
									(tempData[buffer] != '\n') &&
									(tempData[buffer] != 127) &&
									(tempData[buffer] != '\t'))
								{
									m_data_xml[i++] = tempData[buffer++];
								}
								else
								{
									// count these characters as data
									whitespace = 1;
									m_data_xml[i++] = ' ';
									buffer++;
								}
							}

							// check if we found the CDATA end
							if (buffer < remaining)
							{
								// skip the end of the CDATA
								buffer += sizeof(CDATA_END) - 1;

								// append the CDATA end to the output buffer
								strcpy((char *) m_data_xml + i, CDATA_END);
								i += sizeof(CDATA_END) - 1;
							}
						}
						else
						{
							// handle as normal data
							while ((buffer < remaining) && (tempData[buffer] != '<'))
							{
								if ((tempData[buffer] >= ' ') && (tempData[buffer] != 127))
								{
									// check for an escape character in the data
									if ('&' == tempData[buffer])
									{
										// handle the escape sequence
										buffer += setEscChar(tempData + buffer, m_data_xml + i++);
									}
									else
									{
										m_data_xml[i++] = tempData[buffer++];
									}
								}
								else
								{
									// count these characters as data but substitute a period in the data display
									whitespace = 1;
									m_data_xml[i++] = '.';
									buffer++;
								}
							}
						}

						// check if we found only white space and a new variable or
						// if we found an end variable
						if ((buffer < remaining) && ('<' == tempData[buffer]) && ('/' == tempData[buffer + 1]))
						{
							// Now, check if we found a beginning bracket
							if (buffer < remaining)
							{
								// we must have found a beginning bracket,
								// so append it to the current line
								m_data_xml[i++] = tempData[buffer++];
							}

							// append the rest of the variable name to the work area
							while ((buffer < remaining) && (tempData[buffer] != '>'))
							{
								if ((tempData[buffer] != '\r') &&
									(tempData[buffer] != '\n') &&
									(tempData[buffer] != 127) &&
									(tempData[buffer] != '\t'))
								{
									m_data_xml[i++] = tempData[buffer++];
								}
								else
								{
									m_data_xml[i++] = '.';
									buffer++;
								}
							}

							// check if we found the trailing end bracket
							if (buffer < remaining)
							{
								// now, this should be an end variable
								// append the ending bracket
								m_data_xml[i++] = tempData[buffer++];
								currentLevel--;
							}
							else
							{
								// append an error message
								strcpy((char *) m_data_xml + i, "\r\n***** RFHUtil format error parsing XML - no trailing end bracket*****");
								i += (int)strlen((char *) m_data_xml + i);
							}
						}

						// append a new line to the work area
						m_data_xml[i++] = '\r';
						m_data_xml[i++] = '\n';

						// at this point, we have built the next line, so
						// insert the proper offset and then append the data
						j = 0;
						while ((j < currentLevel) && (j < MAX_XML_INDENT_LEVELS))
						{
							m_data_xml[i++] = ' ';
							j++;
						}
					}
				}
			}
			else
			{
				// Didn't find an end bracket, put the rest of the line on a separate line
				strcpy((char *) m_data_xml + i, "\r\n***** RFHUtil format error - missing end bracket\r\n");
				i += (int)strlen((char *) m_data_xml + i);
			}
		}

	}

	// check if there were a balanced number of begin and end tags
	if (currentLevel != 0)
	{
		strcpy((char *) m_data_xml + i, "\r\n***** RFHUtil format error - Unmatched beginning and ending tags found");
		i += (int)strlen((char *) m_data_xml + i);
	}

	// terminate the string
	m_data_xml[i] = 0;

	Rtrim((char *)m_data_xml);

	// release the temporary data areas
	rfhFree(tempData);
	if (tempData2 != fileData)
	{
		rfhFree(tempData2);
	}

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting DataFormatter::getXmlData() len=%d strlen(m_data_xml)=%d i=%d buffer=%d remaining=%d", len, (int)strlen((const char *)m_data_xml), i, buffer, remaining);

		// trace exit from getXmlData
		logTraceEntry(traceInfo);
	}

	return;
}

void DataFormatter::getJsonData(const int charFormat)

{
	int				result;				// result of parsing the user data
	int				len=0;				// length of formatted data area
	char			*ptr;				// pointer to formatted data area
	CJsonParse		json;				// JSON parser object
	char			traceInfo[512];		// work area to build trace message

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering DataFormatter::getJsonData() with charFormat=%d fileCcsid=%d fileSize=%d", charFormat, fileCcsid, fileSize);

		// trace entry to getJsonData
		logTraceEntry(traceInfo);
	}

	// check if we already have the proper data formatted
	if (m_data_json != NULL)
	{
		// did we change options?
		if (charFormat == saveJsonFormat)
		{
			if (traceEnabled)
			{
				// create the trace line
				sprintf(traceInfo, "Exiting DataFormatter::getJsonData() already exists m_data_json=%8.8X strlen(m_data_json)=%d", (unsigned int)(uintptr_t)m_data_json, (int)strlen((char *)m_data_json));

				// trace exit from getJsonData
				logTraceEntry(traceInfo);
			}

			// the area already exists - just return
			return;
		}

		rfhFree(m_data_json);
		m_data_json = NULL;
	}

	// parse the user data to create a DOM-like table in the parser object
	result = json.parse((const char *)fileData, fileSize);

	// check if the parse worked
	if (result != PARSE_OK)
	{
		// error parsing usr area
		m_error_msg = "Error parsing user data";
		updateMsgText();

		if (traceEnabled)
		{
			// explain the error
			sprintf(traceInfo, "Error parsing user data - %d", result);

			// trace exit from getJsonData
			logTraceEntry(traceInfo);

			// dump out the usr area
			dumpTraceData("fileData", (unsigned char *)fileData, fileSize);
		}

		return;
	}

	// allocate storage for a temporary data to format the data into
	ptr = (char *)rfhMalloc(fileSize * 16 + 8192, "JSONDATA");

	// make sure the malloc worked
	if (NULL == ptr)
	{
		// tell the user what happened
		// error parsing usr area
		m_error_msg = "Error allocating storage";
		updateMsgText();

		if (traceEnabled)
		{
			// explain the error
			sprintf(traceInfo, "Memory allocation failed - %d in getJsonData()", fileSize * 16 + 8192);

			// trace exit from getJsonData
			logTraceEntry(traceInfo);
		}

		// return
		return;
	}

	// create the parsed area
	ptr[0] = 0;
	json.buildParsedArea(ptr, fileSize * 16 + 8000, FALSE);

	// get the amount of storage that was really used
	len = (int)strlen(ptr);

	// allocate the necessary amount of storage
	m_data_json = (unsigned char *)rfhMalloc(len + 16, "JSONDAT2");

	// check if the malloc worked
	if (NULL == m_data_json)
	{
		// malloc failed
		// use the storage that was already acquired
		m_data_json = (unsigned char *)ptr;
	}
	else
	{
		// copy the formatted data to the actual area
		memcpy(m_data_json, ptr, len);

		// terminate the string
		m_data_json[len] = 0;
		m_data_json[len + 1] = 0;

		// free the acquired storage
		rfhFree(ptr);
	}

	// remember the type of display data
	saveJsonFormat = charFormat;

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting DataFormatter::getJsonData() len=%d strlen(m_data_json)=%d", len, (int)strlen((const char *)m_data_json));

		// trace exit from getJsonData
		logTraceEntry(traceInfo);
	}
}

void DataFormatter::getFixData(const int charFormat)

{
	int				len=0;				// length of formatted data area
	int				ofs=0;				// offset within the input data
	int				size=0;				// number of bytes to allocate
	int				tcount=0;			// indicator if character translation was successful
	int				asciiCcsid=0;		// ASCII code page to use for EBCDIC to ASCII translations
	char			tempTxt[4];			// work area to hold translated characters
	char			traceInfo[512];		// work area to build trace message

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering DataFormatter::getFixData() with charFormat=%d fileCcsid=%d fileSize=%d", charFormat, fileCcsid, fileSize);

		// trace entry to getFixData
		logTraceEntry(traceInfo);
	}

	// check if we already have the proper data formatted
	if (m_data_fix != NULL)
	{
		// did we change options?
		if (charFormat == saveFixFormat)
		{
			if (traceEnabled)
			{
				// create the trace line
				sprintf(traceInfo, "Exiting DataFormatter::getFixData() already exists m_data_fix=%8.8X strlen(m_data_fix)=%d", (unsigned int)(uintptr_t)m_data_fix, (int)strlen((char *)m_data_fix));

				// trace exit from getFixData
				logTraceEntry(traceInfo);
			}

			// the area already exists - just return
			return;
		}

		rfhFree(m_data_fix);
		m_data_fix = NULL;
	}

	// allocate storage for a temporary data to format the data into
	// each binary x'01' will be replaced by a CRLF sequence - the maximum storage that can be used is 2X the input
	// if the data is not fix format a message will be placed in the area instead
	size = fileSize * 2 + 512;
	m_data_fix = (unsigned char *)rfhMalloc(size, "FIXDATA ");

	// make sure the malloc worked
	if (NULL == m_data_fix)
	{
		// tell the user what happened
		// error parsing usr area
		m_error_msg = "Error allocating storage";
		updateMsgText();

		if (traceEnabled)
		{
			// explain the error
			sprintf(traceInfo, "Memory allocation failed - %d in getFixData()", size);

			// trace exit from getFixData
			logTraceEntry(traceInfo);
		}

		// return
		return;
	}

	// do we need to do EBCDIC to ASCII translation?
	if (CHAR_EBCDIC == charFormat)
	{
		// figure out what code page to translate to
		// we only need to do this once
		asciiCcsid = matchEbcdicToAscii(fileCcsid);
	}

	// start to work through the data one character at a time
	while (ofs < (int)fileSize)
	{
		// check for a binary x'01'
		if (1 == fileData[ofs])
		{
			// insert a CRLF sequence in the output
			m_data_fix[len++] = '\r';
			m_data_fix[len++] = '\n';
		}
		else
		{
			// does the character need to be translated?
			if (CHAR_EBCDIC == charFormat)
			{
				// translate the character to EBCDIC
				if (asciiCcsid > 0)
				{
					// start by trying to do a proper translation, using Windows or MQ
					tcount = xlateEbcdicChar(fileCcsid, asciiCcsid, (char *)fileData + ofs, (char *)tempTxt);

					// check if the translation worked
					if (1 == tcount)
					{
						// Windows or MQ was able to perform the translation
						// make sure we have a displayable character and
						// move on to the next output character
						m_data_fix[len] = invalidCharTable[(unsigned char)tempTxt[0]];
					}
				}

				// did we wind up with a single character properly translated?
				if ((asciiCcsid <= 0) || (0 == tcount))
				{
					// use a default translation table
					m_data_fix[len] = invalidCharTable[eatab[fileData[ofs]]];
				}
			}
			else
			{
				// move the input character to the output, making sure it is a valid display character
				m_data_fix[len] = invalidCharTable[fileData[ofs]];
			}

			// move on to the next output character
			len++;
		}

		// move on to the next input character
		ofs++;
	}

	// terminate the string
	m_data_fix[len] = 0;

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting DataFormatter::getFixData() size=%d len=%d strlen(m_data_fix)=%d", size, len, (int)strlen((const char *)m_data_fix));

		// trace exit from getFixData
		logTraceEntry(traceInfo);
	}
}

void DataFormatter::getNameValueData(const unsigned char * input, int inputLen, unsigned char * output, int maxLength, int ccsid, const int charFormat)

{
	// turn the file data into a xml format and return it as a CString
	// replace any unprintable characters with periods.
	// drop comments, embedded DTDs, etc
	//
	// Recognize the following constructs
	//
	// <?xml ... > - XML Header
	// <varname>..vardata..</varname>
	// <varname/>
	// <varname attr=value..> or <varname attr=value.../>
	//

	char	*errmsg=NULL;
	unsigned char	*parsed_data=output;
	unsigned char	*tempData;
	unsigned char	*tempData2;
	int		endName;
	int		attrLen;
	int		i;					// work variable
	int		j;					// work variable
	int		k;					// work variable
	int		rc;					// return code from subroutine call
	int		templen;
	int		savei;
	int		varNameLen=0;		// length of the tag name
	int		buffer=0;			// index of the next character to process
	int		remaining;			// number of bytes left to process
	int		length=0;			// number of bytes in the data area after any translation
	int		currentLevel=0;		// current depth relative to root element (root = 0)
	int		currentOfs=0;		// number of spaces to insert at beginning of a line
	int		endvar=0;			// end tag found
	int		specvar=0;			// null tag found
	int		whitespace=0;		// number of whitespace characters found between tags
	char	traceInfo[512];		// work area to build trace message

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering DataFormatter::getNameValueData() inputLen=%d maxLength=%d ccsid=%d charFormat=%d", inputLen, maxLength, ccsid, charFormat);

		// trace entry to getNameValueData
		logTraceEntry(traceInfo);
	}

	// check if there is any data to format
	if (0 == inputLen)
	{
		// nothing to do
		output[0] = 0;
		return;
	}

	// translate the data to ASCII as necessary
	// if no translation is required a pointer to the original data in fileData is returned
	length = inputLen;
	tempData2 = getTranslatedData(&length, charFormat, ccsid);

	// make sure that the translation worked
	if (0 == tempData2)
	{
		// just return
		return;
	}

	// check if the data length is greater than zero
	if (0 == length)
	{
		// nothing to process
		// release the acquired data area if necessary
		if (tempData2 != input)
		{
			// release the acquired memory so we don't leak memory
			rfhFree(tempData2);
		}

		// return with m_data_xml set as a zero length string
		return;
	}

	//.initialize a temporary memory area that will hold the
	// processed data with any unnecessary xml characters removed
	tempData = (unsigned char *) rfhMalloc(length + 1, "TEMPDAT5");
	memset(tempData, 0, length + 1);
	memset(xmlVarName, 0, sizeof(xmlVarName));
	memset(xmlAttrStr, 0, sizeof(xmlAttrStr));

	// figure out how much data we have after we
	// remove comments, invalid characters, etc
	remaining = removeUnneededXML(tempData2, length, tempData);

	// Do a quick sanity check on the data and inset a message if
	// this doesn't look like XML
	// the check includes things like making sure the number of
	// begin and end brackets match.
	rc = checkIfXml(tempData, length, &errmsg);

	if (rc > 0)
	{
		// display a message instead of the data
		strcpy((char *) parsed_data, BAD_XML_MSG);
		strcat((char *) parsed_data, errmsg);

		// release the acquired data areas if necessary
		rfhFree(tempData);
		if (tempData2 != input)
		{
			// release the acquired memory so we don't leak memory
			rfhFree(tempData2);
		}

		// is trace enabled?
		if (traceEnabled)
		{
			// create the trace line
			sprintf(traceInfo, "Exiting DataFormatter::getNameValueData() Data does not appear to be XML - rc=%d errmsg=%.128s", rc, errmsg);

			// trace exit from getNameValueData
			logTraceEntry(traceInfo);
		}

		return;
	}

	// do some initialization of variables and work areas
	i = 0;

	while ((buffer < remaining) && (i < (maxLength - 2048)))
	{
		// first, check if we have a beginning bracket
		if (tempData[buffer] != '<')
		{
			// We have some junk, so we will just put
			// it on its own line until we find a begin bracket
			// find the next beginning bracket
			whitespace = 0;
			savei = i;

			while ((buffer < remaining) && (tempData[buffer] != '<'))
			{
				// check if we have something other than a space
				if ((tempData[buffer] > ' ') && (tempData[buffer] < 128))
				{
					whitespace = 1;
				}

				if ((tempData[buffer] > ' ') && (tempData[buffer] < 127))
				{
					parsed_data[i++] = tempData[buffer++];
				}
				else
				{
					// replace with periods
					parsed_data[i++] = '.';
					buffer++;
				}
			}

			if (1 == whitespace)
			{
				// put this junk on its own line
				parsed_data[i++] = '\r';
				parsed_data[i++] = '\n';

				// at this point, we have built the next line, so
				// insert the proper offset and then append the data
				j = 0;
				while (j < currentLevel)
				{
					parsed_data[i++] = ' ';
					j++;
				}
			}
			else
			{
				// write over the junk
				i = savei;
			}
		}

		// check if we found a begin bracket
		if ((buffer < remaining) && ('<' == tempData[buffer]))
		{
			// we found a begin bracket
			// skip it
			buffer++;

			endvar = 0;
			specvar = 0;
			tempVarName[0] = 0;
			j = 0;

			// check if this is the end of a variable
			if ('/' == tempData[buffer])
			{
				// Ending variable, set a variable to remind us
				endvar = 1;
			}

			// check if this is special XML
			if (('?' == tempData[buffer]) || ('!' == tempData[buffer]))
			{
				// Ending variable, set a variable to remind us
				specvar = 1;
			}

			// now, see if we can find an end bracket, isolating the variable name
			endName = 0;
			attrLen = 0;
			while ((buffer < remaining) && (tempData[buffer] != '>'))
			{
				if ((tempData[buffer] <= ' ') && (endName == 0))
				{
					// switch to the attributes if we find a space
					endName = 1;
					buffer++;
				}
				else
				{
					if (0 == endName)
					{
						//capture the variable name
						if ((size_t)j < sizeof(tempVarName) - 1) { tempVarName[j++] = tempData[buffer]; } buffer++;
					}
					else
					{
						// ignore any leading spaces in the attributes
						if ((attrLen > 0) || (tempData[buffer] > ' '))
						{
							// capture the attributes
							if ((size_t)attrLen < sizeof(tempVarAttr) - 1) { tempVarAttr[attrLen++] = tempData[buffer]; } buffer++;
						}
						else
						{
							buffer++;
						}
					}
				}
			}

			// terminate the strings
			tempVarName[j] = 0;
			tempVarAttr[attrLen] = 0;

			// get rid of any trailing slash or space characters
			while ((attrLen > 0) && (('/' == tempVarAttr[attrLen]) || (tempVarAttr[attrLen] <= ' ')))
			{
				tempVarAttr[attrLen--] = 0;
			}

			if ('>' == tempData[buffer])
			{
				if ('/' == tempData[buffer-1])
				{
					specvar = 1;

					// remove the / from the variable name
					if ((tempVarName[0] != 0) && ('/' == tempVarName[strlen(tempVarName) - 1]))
					{
						tempVarName[strlen(tempVarName) - 1] = 0;
					}
				}

				// First, skip the end bracket
				buffer++;

				// We have the variable name isolated
				// check if we are beginning a new variable
				// or ending a previous one
				if ((1 == endvar) || (1 == specvar))
				{
					if (1 == endvar)
					{
						// ending a previous variable
						// don't allow negative numbers in level
						if (currentLevel > 0)
						{
							// decrement the level of this variable
							currentLevel--;

							// remove the last variable from the name list
							rc = removeVarName(xmlVarName, tempVarName);

							if (rc > 0)
							{
								// append an error message
								_snprintf_s((char *) parsed_data + i, maxLength - i, _TRUNCATE,
									"***** RFHUtil format error - end name does not match %.256s %.256s\r\n",
									xmlVarName, tempVarName);
								i += (int)strlen((char *) parsed_data + i);
							}
						}
						else
						{
							// Result would have been negative - flag as error
							strcpy((char *) parsed_data + i, "***** RFHUtil format error parsing XML - too few levels*****\r\n");
							i += (int)strlen((char *) parsed_data + i);
						}
					}

					// check if this is an empty variable
					// otherwise it is ignored
					if ((1 == specvar) && ('/' == tempData[buffer-2]))
					{
						// create a null entry for this value
						if ((tempVarName[0] != 0) && ('\\' == tempVarName[strlen(tempVarName) - 1]))
						{
							tempVarName[strlen(tempVarName) - 1] = 0;
						}

						// get any leading names in the hierarchy
						if (strlen(xmlVarName) > 0)
						{
							strcpy((char *) parsed_data + i, xmlVarName);
							i += (int)strlen(xmlVarName);
							parsed_data[i++] = '.';
						}

						// add the empty variable to the display
						strcpy((char *) parsed_data + i, tempVarName);
						i += (int)strlen(tempVarName);
						strcpy((char *) parsed_data + i, "=\'\'");
						i += 3;

						// append a carriage return/new line pair to start a new line
						parsed_data[i++] = '\r';
						parsed_data[i++] = '\n';

						// this is an empty variable but check for attributes
						if (attrLen > 0)
						{
							// start with a zero length output string
							xmlAttrStr[0] = 0;

							// extract the individual attributes into a temporary string
							processAttributes(xmlVarName, tempVarName, tempVarAttr, xmlAttrStr, sizeof(xmlAttrStr));

							// capture the attributes in the output area
							strcpy((char *) parsed_data + i, xmlAttrStr);
							i += (int)strlen(xmlAttrStr);
						}

					}
				}
				else
				{
					// beginning a new variable name
					// first, increase the level
					currentLevel++;

					// build a string with the current
					// higher level variables
					if (xmlVarName[0] != 0)
					{
						// guard: need room for "." + tempVarName + NUL
						if (strlen(xmlVarName) + 1 + strlen(tempVarName) < sizeof(xmlVarName))
						{
							strcat(xmlVarName, ".");
						}
						else
						{
							break;
						}
					}

					// append the latest variable name (guard: must still fit + NUL)
					if (strlen(xmlVarName) + strlen(tempVarName) < sizeof(xmlVarName))
					{
						strcat(xmlVarName, tempVarName);
					}
					else
					{
						break;
					}

					// At this point, we want to find the rest of the line,
					// starting with any data for this variable
					// If there is no data, we need to check if we are starting a
					// new level or if we just have a null variable
					if (('<' == tempData[buffer]) &&
						(tempData[buffer + 1] != '/') &&
						(memcmp(tempData + buffer, CDATA_START, 9) != 0))
					{
						// We are starting a new level, so let's stop at this point
						// append to the display data
						strcpy((char *) parsed_data + i, xmlVarName);
						i += (int)strlen(xmlVarName);
						parsed_data[i++] = '\r';
						parsed_data[i++] = '\n';
					}
					else
					{
						strcpy((char *) parsed_data + i, xmlVarName);
						i += (int)strlen(xmlVarName);

						// We may have some data, so we will collect it
						// in a temporary variable
						templen = 0;
						whitespace = 0;

						// check for a CDATA section
						if ((buffer < remaining - 10) && (memcmp(tempData + buffer, CDATA_START, 9) == 0))
						{
							// found a CDATA section
							buffer += 9;

							// now, gather the data
							while ((buffer < remaining) &&
								   (memcmp(tempData + buffer, CDATA_END, 3) != 0) &&
								   (templen < (sizeof(tempVarValue) - 1)))
							{
								if ((tempData[buffer] >= ' ') && (tempData[buffer] != 127))
								{
									tempVarValue[templen++] = tempData[buffer++];
								}
								else
								{
									// replace non-display characters with a period
									tempVarValue[templen++] = '.';
									buffer++;
								}
							}

							// check that the end of the CDATA section was found
							if ((buffer < remaining) && (memcmp(tempData + buffer, CDATA_END, 3) == 0))
							{
								// account for the terminating characters
								buffer += 3;

								// skip any whitespace at the end of the CDATA
								while ((buffer < remaining) && (tempData[buffer] < ' '))
								{
									// skip the whitespace
									buffer++;
								}
							}
						}
						else
						{
							// handle as ordinary data
							while ((buffer < remaining) && (tempData[buffer] != '<') && (templen < (sizeof(tempVarValue) - 1)))
							{
								// check if we have non-whitespace characters
								if (tempData[buffer] > ' ')
								{
									whitespace = 1;
								}

								// replace any non-printable characters with blanks
								if ((tempData[buffer] >= ' ') && (tempData[buffer] != 127))
								{
									// normal character
									tempVarValue[templen++] = tempData[buffer++];
								}
								else
								{
									// replace with a period
									tempVarValue[templen++] = '.';
									buffer++;
								}
							}
						}

						// terminate the value string, remove escape
						// sequences and append it
						tempVarValue[templen] = 0;

						// check if we should append the value
						if (('<' == tempData[buffer]) && ('/' == tempData[buffer + 1]))
						{
							parsed_data[i++] = '=';
							parsed_data[i++] = '\'';
							removeEscSeq(tempVarValue);
							strcpy((char *) parsed_data + i, tempVarValue);
							i += (int)strlen(tempVarValue);
							parsed_data[i++] = '\'';
						}

						// append a new line to the work area
						parsed_data[i++] = '\r';
						parsed_data[i++] = '\n';

						// Now, check if we found a beginning bracket
						if ((buffer < remaining) && ('<' == tempData[buffer]) && ('/' == tempData[buffer + 1]))
						{
							// we must have found a beginning bracket,
							// so skip it
							buffer++;
							k = 0;

							// isolate the ending variable name
							while ((buffer < remaining) && (tempData[buffer] != '>'))
							{
								if (tempData[buffer] <= ' ')
								{
									// terminate the variable name
									if ((size_t)k < sizeof(tempEndVarName) - 1) { tempEndVarName[k++] = 0; }
									buffer++;
								}
								else
								{
									if ((size_t)k < sizeof(tempEndVarName) - 1) { tempEndVarName[k++] = tempData[buffer]; } buffer++;
								}
							}

							// terminate the string
							tempEndVarName[k] = 0;

							// check if we found the trailing end bracket
							if (buffer < remaining)
							{
								// now, this should be an end variable
								// skip the ending bracket
								buffer++;
								currentLevel--;

								// Check if this is an ending element
								if ('/' == tempEndVarName[0])
								{
									// yes it is, check if it matches
									// remove the last variable from the name list
									rc = removeVarName(xmlVarName, tempEndVarName);

									if (rc > 0)
									{
										strcpy((char *) parsed_data + i, "***** RFHUtil format error - end name does not match\r\n");
										i += (int)strlen((char *) parsed_data + i);
									}
								}
								else
								{
									strcpy((char *) parsed_data + i, "***** RFHUtil format error parsing XML - missing element end\r\n");
									i += (int)strlen((char *) parsed_data + i);
								}

							}
							else
							{
								// append an error message
								strcpy((char *) parsed_data + i, "***** RFHUtil format error parsing XML - no trailing end bracket\r\n");
								i += (int)strlen((char *) parsed_data + i);
							}
						}
					}

					// check if we have found any attributes we should display
					if (attrLen > 0)
					{
						// start with a zero length output string
						xmlAttrStr[0] = 0;

						// get the attributes in the right display format
						processAttributes(xmlVarName, "", tempVarAttr, xmlAttrStr, sizeof(xmlAttrStr));

						// append the attribute lines to the output
						strcpy((char *) parsed_data + i, xmlAttrStr);

						// account for the storage in the output
						i += (int)strlen(xmlAttrStr);
					}
				}
			}
			else
			{
				// Didn't find an end bracket, put the rest of the line on a separate line
				strcpy((char *) parsed_data + i, "***** RFHUtil format error - missing end bracket\r\n");
				i += (int)strlen((char *) parsed_data + i);
			}
		}

	}

	// terminate the string
	parsed_data[i] = 0;

	Rtrim((char *)parsed_data);

	// release the temporary buffers
	rfhFree(tempData);
	if (tempData2 != input)
	{
		rfhFree(tempData2);
	}

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting DataFormatter::getNameValueData() maxLength=%d strlen(parsed_data)=%d i=%d buffer=%d remaining=%d", maxLength, (int)strlen((const char *)parsed_data), i, buffer, remaining);

		// trace exit from getNameValueData
		logTraceEntry(traceInfo);
	}

	return;
}

void DataFormatter::getParsedData(const int charFormat)

{
	// turn the file data into a xml format and return it as a CString
	// replace any unprintable characters with periods.
	// drop comments, embedded DTDs, etc
	//
	// Recognize the following constructs
	//
	// <?xml ... > - XML Header
	// <varname>..vardata..</varname>
	// <varname/>
	// <varname attr=value..> or <varname attr=value.../>
	//
	char	*errmsg=NULL;
	int		varNameLen=0;		// length of the tag name
	int		buffer=0;			// index of the next character to process
	int		len;				// number of bytes of storage acquired for the m_data_parsed area
	int		length=0;			// number of bytes in the data area after any translation
	int		currentLevel=0;		// current depth relative to root element (root = 0)
	int		currentOfs=0;		// number of spaces to insert at beginning of a line
	int		endvar=0;			// end tag found
	int		specvar=0;			// null tag found
	int		whitespace=0;		// number of whitespace characters found between tags
	char	traceInfo[512];		// work area to build trace message

	if (traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering DataFormatter::getParsedData() with charFormat=%d fileCcsid=%d fileSize=%d", charFormat, fileCcsid, fileSize);

		// trace entry to getParsedData
		logTraceEntry(traceInfo);
	}

	// check if we already have the proper data formatted
	if (m_data_parsed != NULL)
	{
		// did we change options?
		if (charFormat == saveParsedFormat)
		{
			return;
		}

		rfhFree(m_data_parsed);
		m_data_parsed = NULL;
	}

	// remember the type of display data
	saveParsedFormat = charFormat;


	// calculate the length we require
	// we use one character per data character plus 3 more
	// characters for every 32 characters
	len = (fileSize * 8) + 4096;
	m_data_parsed = (unsigned char *) rfhMalloc(len, "PARSEDAT");

	// make sure it worked
	if (NULL == m_data_parsed)
	{
		// just return
		return;
	}

	// initialize to a zero length string
	m_data_parsed[0] = 0;

	// check if there is any data to format
	if (0 == fileSize)
	{
		// nothing to do
		// this will result in m_data_parsed pointing to a null string
		return;
	}

	// parse the input data
	getNameValueData(fileData, fileSize, m_data_parsed, len, fileCcsid, charFormat);

	// translate the data to ASCII as necessary
	// if no translation is required a pointer to the original data in fileData is returned
/*	tempData2 = getTranslatedData(&length, charFormat, fileCcsid);

	// check if the data length is greater than zero
	if (0 == length)
	{
		// nothing to process
		// release the acquired data area if necessary
		if (tempData2 != fileData)
		{
			// release the acquired memory so we don't leak memory
			rfhFree(tempData2);
		}

		// return with m_data_xml set as a zero length string
		return;
	}

	//.initialize a temporary memory area that will hold the
	// processed data with any unnecessary xml characters removed
	tempData = (unsigned char *) rfhMalloc(length + 1, "XMLREMOV");
	memset(tempData, 0, length + 1);
	memset(xmlVarName, 0, sizeof(xmlVarName));
	memset(xmlAttrStr, 0, sizeof(xmlAttrStr));

	// figure out how much data we have after we
	// remove comments, invalid characters, etc
	remaining = removeUnneededXML(tempData2, length, tempData);

	// Do a quick sanity check on the data and inset a message if
	// this doesn't look like XML
	// the check includes things like making sure the number of
	// begin and end brackets match.
	rc = checkIfXml(tempData, length, &errmsg);

	if (rc > 0)
	{
		// display a message instead of the data
		strcpy((char *) m_data_parsed, BAD_XML_MSG);
		strcat((char *) m_data_parsed, errmsg);

		// release the acquired data areas if necessary
		rfhFree(tempData);
		if (tempData2 != fileData)
		{
			// release the acquired memory so we don't leak memory
			rfhFree(tempData2);
		}

		return;
	}

	// do some initialization of variables and work areas
	i = 0;

	while ((buffer < remaining) && (i < (len - 2048)))
	{
		// first, check if we have a beginning bracket
		if (tempData[buffer] != '<')
		{
			// We have some junk, so we will just put
			// it on its own line until we find a begin bracket
			// find the next beginning bracket
			whitespace = 0;
			savei = i;

			while ((buffer < remaining) && (tempData[buffer] != '<'))
			{
				// check if we have something other than a space
				if ((tempData[buffer] > ' ') && (tempData[buffer] < 128))
				{
					whitespace = 1;
				}

				if ((tempData[buffer] > ' ') && (tempData[buffer] < 127))
				{
					m_data_parsed[i++] = tempData[buffer++];
				}
				else
				{
					// replace with periods
					m_data_parsed[i++] = '.';
					buffer++;
				}
			}

			if (1 == whitespace)
			{
				// put this junk on its own line
				m_data_parsed[i++] = '\r';
				m_data_parsed[i++] = '\n';

				// at this point, we have built the next line, so
				// insert the proper offset and then append the data
				j = 0;
				while (j < currentLevel)
				{
					m_data_parsed[i++] = ' ';
					j++;
				}
			}
			else
			{
				// write over the junk
				i = savei;
			}
		}

		// check if we found a begin bracket
		if ((buffer < remaining) && ('<' == tempData[buffer]))
		{
			// we found a begin bracket
			// skip it
			buffer++;

			endvar = 0;
			specvar = 0;
			tempVarName[0] = 0;
			j = 0;

			// check if this is the end of a variable
			if ('/' == tempData[buffer])
			{
				// Ending variable, set a variable to remind us
				endvar = 1;
			}

			// check if this is special XML
			if (('?' == tempData[buffer]) || ('!' == tempData[buffer]))
			{
				// Ending variable, set a variable to remind us
				specvar = 1;
			}

			// now, see if we can find an end bracket, isolating the variable name
			endName = 0;
			attrLen = 0;
			while ((buffer < remaining) && (tempData[buffer] != '>'))
			{
				if ((tempData[buffer] <= ' ') && (endName == 0))
				{
					// switch to the attributes if we find a space
					endName = 1;
					buffer++;
				}
				else
				{
					if (0 == endName)
					{
						//capture the variable name
						if ((size_t)j < sizeof(tempVarName) - 1) { tempVarName[j++] = tempData[buffer]; } buffer++;
					}
					else
					{
						// ignore any leading spaces in the attributes
						if ((attrLen > 0) || (tempData[buffer] > ' '))
						{
							// capture the attributes
							if ((size_t)attrLen < sizeof(tempVarAttr) - 1) { tempVarAttr[attrLen++] = tempData[buffer]; } buffer++;
						}
						else
						{
							buffer++;
						}
					}
				}
			}

			// terminate the strings
			tempVarName[j] = 0;
			tempVarAttr[attrLen] = 0;

			// get rid of any trailing slash or space characters
			while ((attrLen > 0) && (('/' == tempVarAttr[attrLen]) || (tempVarAttr[attrLen] <= ' ')))
			{
				tempVarAttr[attrLen--] = 0;
			}

			if ('>' == tempData[buffer])
			{
				if ('/' == tempData[buffer-1])
				{
					specvar = 1;

					// remove the / from the variable name
					if ('/' == tempVarName[strlen(tempVarName) - 1])
					{
						tempVarName[strlen(tempVarName) - 1] = 0;
					}
				}

				// First, skip the end bracket
				buffer++;

				// We have the variable name isolated
				// check if we are beginning a new variable
				// or ending a previous one
				if ((1 == endvar) || (1 == specvar))
				{
					if (1 == endvar)
					{
						// ending a previous variable
						// don't allow negative numbers in level
						if (currentLevel > 0)
						{
							// decrement the level of this variable
							currentLevel--;

							// remove the last variable from the name list
							rc = removeVarName(xmlVarName, tempVarName);

							if (rc > 0)
							{
								// append an error message
								_snprintf_s((char *) m_data_parsed + i, len - i, _TRUNCATE,
									"***** RFHUtil format error - end name does not match %.256s %.256s\r\n",
									xmlVarName, tempVarName);
								i += (int)strlen((char *) m_data_parsed + i);
							}
						}
						else
						{
							// Result would have been negative - flag as error
							strcpy((char *) m_data_parsed + i, "***** RFHUtil format error parsing XML - too few levels*****\r\n");
							i += (int)strlen((char *) m_data_parsed + i);
						}
					}

					// check if this is an empty variable
					// otherwise it is ignored
					if ((1 == specvar) && ('/' == tempData[buffer-2]))
					{
						// create a null entry for this value
						if ('\\' == tempVarName[strlen(tempVarName) - 1])
						{
							tempVarName[strlen(tempVarName) - 1] = 0;
						}

						// get any leading names in the hierarchy
						if (strlen(xmlVarName) > 0)
						{
							strcpy((char *) m_data_parsed + i, xmlVarName);
							i += (int)strlen(xmlVarName);
							m_data_parsed[i++] = '.';
						}

						// add the empty variable to the display
						strcpy((char *) m_data_parsed + i, tempVarName);
						i += (int)strlen(tempVarName);
						strcpy((char *) m_data_parsed + i, "=\'\'");
						i += 3;
						// append a carriage return/new line pair to start a new line
						m_data_parsed[i++] = '\r';
						m_data_parsed[i++] = '\n';

						// this is an empty variable but check for attributes
						if (attrLen > 0)
						{
							// start with a zero length output string
							xmlAttrStr[0] = 0;

							// extract the individual attributes into a temporary string
							processAttributes(xmlVarName, tempVarName, tempVarAttr, xmlAttrStr, sizeof(xmlAttrStr));

							// capture the attributes in the output area
							strcpy((char *) m_data_parsed + i, xmlAttrStr);
							i += (int)strlen(xmlAttrStr);
						}

					}
				}
				else
				{
					// beginning a new variable name
					// first, increase the level
					currentLevel++;

					// build a string with the current
					// higher level variables
					if (xmlVarName[0] != 0)
					{
						// guard: need room for "." + tempVarName + NUL
						if (strlen(xmlVarName) + 1 + strlen(tempVarName) < sizeof(xmlVarName))
						{
							strcat(xmlVarName, ".");
						}
						else
						{
							break;
						}
					}

					// append the latest variable name (guard: must still fit + NUL)
					if (strlen(xmlVarName) + strlen(tempVarName) < sizeof(xmlVarName))
					{
						strcat(xmlVarName, tempVarName);
					}
					else
					{
						break;
					}

					// At this point, we want to find the rest of the line,
					// starting with any data for this variable
					// If there is no data, we need to check if we are starting a
					// new level or if we just have a null variable
					if (('<' == tempData[buffer]) &&
						(tempData[buffer + 1] != '/') &&
						(memcmp(tempData + buffer, CDATA_START, 9) != 0))
					{
						// We are starting a new level, so let's stop at this point
						// append to the display data
						strcpy((char *) m_data_parsed + i, xmlVarName);
						i += (int)strlen(xmlVarName);
						m_data_parsed[i++] = '\r';
						m_data_parsed[i++] = '\n';
					}
					else
					{
						strcpy((char *) m_data_parsed + i, xmlVarName);
						i += (int)strlen(xmlVarName);

						// We may have some data, so we will collect it
						// in a temporary variable
						templen = 0;
						whitespace = 0;

						// check for a CDATA section
						if ((buffer < remaining - 10) && (memcmp(tempData + buffer, CDATA_START, 9) == 0))
						{
							// found a CDATA section
							buffer += 9;

							// now, gather the data
							while ((buffer < remaining) &&
								   (memcmp(tempData + buffer, CDATA_END, 3) != 0) &&
								   (templen < (sizeof(tempVarValue) - 1)))
							{
								if ((tempData[buffer] >= ' ') && (tempData[buffer] != 127))
								{
									tempVarValue[templen++] = tempData[buffer++];
								}
								else
								{
									// replace non-display characters with a period
									tempVarValue[templen++] = '.';
									buffer++;
								}
							}

							// account for the terminating characters
							buffer += 3;
						}
						else
						{
							// handle as ordinary data
							while ((buffer < remaining) && (tempData[buffer] != '<') && (templen < (sizeof(tempVarValue) - 1)))
							{
								// check if we have non-whitespace characters
								if (tempData[buffer] > ' ')
								{
									whitespace = 1;
								}

								// replace any non-printable characters with blanks
								if ((tempData[buffer] >= ' ') && (tempData[buffer] != 127))
								{
									// normal character
									tempVarValue[templen++] = tempData[buffer++];
								}
								else
								{
									// replace with a period
									tempVarValue[templen++] = '.';
									buffer++;
								}
							}
						}

						// terminate the value string, remove escape
						// sequences and append it
						tempVarValue[templen] = 0;

						// check if we should append the value
						if (('<' == tempData[buffer]) && ('/' == tempData[buffer + 1]))
						{
							m_data_parsed[i++] = '=';
							m_data_parsed[i++] = '\'';
							removeEscSeq(tempVarValue);
							strcpy((char *) m_data_parsed + i, tempVarValue);
							i += (int)strlen(tempVarValue);
							m_data_parsed[i++] = '\'';
						}

						// append a new line to the work area
						m_data_parsed[i++] = '\r';
						m_data_parsed[i++] = '\n';

						// Now, check if we found a beginning bracket
						if ((buffer < remaining) && ('<' == tempData[buffer]) && ('/' == tempData[buffer + 1]))
						{
							// we must have found a beginning bracket,
							// so skip it
							buffer++;
							k = 0;

							// isolate the ending variable name
							while ((buffer < remaining) && (tempData[buffer] != '>'))
							{
								if (tempData[buffer] <= ' ')
								{
									// terminate the variable name
									if ((size_t)k < sizeof(tempEndVarName) - 1) { tempEndVarName[k++] = 0; }
									buffer++;
								}
								else
								{
									if ((size_t)k < sizeof(tempEndVarName) - 1) { tempEndVarName[k++] = tempData[buffer]; } buffer++;
								}
							}

							// terminate the string
							tempEndVarName[k] = 0;

							// check if we found the trailing end bracket
							if (buffer < remaining)
							{
								// now, this should be an end variable
								// skip the ending bracket
								buffer++;
								currentLevel--;

								// Check if this is an ending element
								if ('/' == tempEndVarName[0])
								{
									// yes it is, check if it matches
									// remove the last variable from the name list
									rc = removeVarName(xmlVarName, tempEndVarName);

									if (rc > 0)
									{
										strcpy((char *) m_data_parsed + i, "***** RFHUtil format error - end name does not match\r\n");
										i += (int)strlen((char *) m_data_parsed + i);
									}
								}
								else
								{
									strcpy((char *) m_data_parsed + i, "***** RFHUtil format error parsing XML - missing element end\r\n");
									i += (int)strlen((char *) m_data_parsed + i);
								}

							}
							else
							{
								// append an error message
								strcpy((char *) m_data_parsed + i, "***** RFHUtil format error parsing XML - no trailing end bracket\r\n");
								i += (int)strlen((char *) m_data_parsed + i);
							}
						}
					}

					// check if we have found any attributes we should display
					if (attrLen > 0)
					{
						// start with a zero length output string
						xmlAttrStr[0] = 0;

						// get the attributes in the right display format
						processAttributes(xmlVarName, "", tempVarAttr, xmlAttrStr, sizeof(xmlAttrStr));

						// append the attribute lines to the output
						strcpy((char *) m_data_parsed + i, xmlAttrStr);

						// account for the storage in the output
						i += (int)strlen(xmlAttrStr);
					}
				}
			}
			else
			{
				// Didn't find an end bracket, put the rest of the line on a separate line
				strcpy((char *) m_data_parsed + i, "***** RFHUtil format error - missing end bracket\r\n");
				i += (int)strlen((char *) m_data_parsed + i);
			}
		}

	}

	// terminate the string
	m_data_parsed[i] = 0;*/

	Rtrim((char *)m_data_parsed);

	// release the temporary buffers
/*	rfhFree(tempData);
	if (tempData2 != fileData)
	{
		rfhFree(tempData2);
	}*/

	if (traceEnabled)
	{
		// create the trace line
		//sprintf(traceInfo, "Exiting DataFormatter::getParsedData() len=%d strlen(m_data_parsed)=%d i=%d buffer=%d remaining=%d", len, strlen((const char *)m_data_parsed), i, buffer, remaining);
		sprintf(traceInfo, "Exiting DataFormatter::getParsedData() len=%d strlen(m_data_parsed)=%d buffer=%d", len, (int)strlen((const char *)m_data_parsed), buffer);

		// trace exit from getParsedData
		logTraceEntry(traceInfo);
	}

	return;
}

BOOL DataFormatter::isLeadChar(unsigned char ch, const int charFormat)

{
	BOOL	result=FALSE;

	// what type of characters are we dealing with?
	switch (charFormat)
	{
	case CHAR_CHINESE:
		{
			// assume code page 936
			if ((ch > 128) && (ch < 253))
			{
				result = TRUE;
			}

			break;
		}
	case CHAR_KOREAN:
		{
			// assume code page 949
			if ((ch > 142) && (ch < 255))
			{
				result = TRUE;
			}

			break;
		}
	case CHAR_TRAD_CHIN:
		{
			// assume code page 950
			if ((ch > 128) && (ch < 255))
			{
				result = TRUE;
			}

			break;
		}
	case CHAR_JAPANESE:
		{
			// assume code page 932
			if (((ch > 128) && (ch < 160)) || ((ch >= 224 ) && (ch < 253)))
			{
				result = TRUE;
			}

			break;
		}
	case CHAR_THAI:
		{
			// assume code page 874
			if ((ch > 128) && (ch < 255))
			{
				result = TRUE;
			}

			break;
		}
	case CHAR_RUSSIAN:
		{
			// assume code page 1251
			if ((ch > 128) && (ch < 255))
			{
				result = TRUE;
			}

			break;
		}
	}

	return result;
}

int DataFormatter::findcrlf(const unsigned char *datain, const int maxchar, const int charSet)

{
	int		count=0;
	int		i=0;
	char	ch;

	// get the first character to check
	ch = datain[0];
	while ((count < maxchar) && (ch != '\r') && (ch != '\n'))
	{
		if (isLeadChar(datain[i], charSet))
		{
			// skip the extra byte for double byte characters
			i++;
		}

		// move on to the next byte
		i++;

		// count the number of characters
		count++;

		// get the next character
		ch = datain[i];
	}

	return count;
}

int DataFormatter::MQEBC2ASC(unsigned char *out, const unsigned char *in, int len, int ccsidIn, int ccsidOut)

{
	int		ret=0;
	int		dirSize;
	char	fileName[512];
	FILE	*tbl;

	// this routine does not handle UCS-2 for now, so return if one of the pages is UCS-2
	if ((13488 == ccsidIn) || (17584 == ccsidIn) || (1200 == ccsidIn) || (1201 == ccsidIn))
	{
		return 0;
	}

	// check if we already tried to read this table
	if ((NULL == MQCcsidPtr) && (MQCcsidIn == ccsidIn) && (MQCcsidOut == ccsidOut))
	{
		// don't try to load the same table each time this routine is called
		return 0;
	}

	// check if we already have the right table loaded
	if ((NULL == MQCcsidPtr) || (MQCcsidIn != ccsidIn) || (MQCcsidOut != ccsidOut))
	{
		// check if there is already a table that must be released
		if (MQCcsidPtr != NULL)
		{
			// release the current table
			rfhFree(MQCcsidPtr);

			// clear the variables
			MQCcsidPtr = NULL;
			MQCcsidLen = ccsidOut;
		}

		// remember what table is being read
		MQCcsidIn = ccsidIn;
		MQCcsidOut = 0;

		// get the file path where MQ is installed
		dirSize = sizeof(fileName);
		findMQInstallDirectory((unsigned char *)fileName, dirSize);

		// create a table name using the two CCSID values and the install path
		sprintf(fileName + strlen(fileName), RFH_DIR_SEP "%4.4X%4.4X.tbl", ccsidIn, ccsidOut);

		// now try to open the table
		tbl = tablesUsable() ? fopen(fileName, "rb") : NULL;
		if (tbl != NULL)
		{
			// figure out how long the file is
			// read it into memory
			fseek(tbl, 0L, SEEK_END);
			MQCcsidLen = ftell(tbl);

			// return the pointer to the beginning of the data
			fseek(tbl, 0L, SEEK_SET);

			// only read the table if it is 256 bytes long
			if (256 == MQCcsidLen)
			{
				// allocate memory for the translation table
				MQCcsidPtr = (unsigned char *)rfhMalloc(MQCcsidLen, "TRANTBL ");

				// try to read the contents of the file into memory
				fread(MQCcsidPtr, 1, MQCcsidLen, tbl);
			}

			// done with the file - close the file handle
			fclose(tbl);
		}
	}

	// check if the table is loaded in memory and avoid tables that are not simple translations
	if ((MQCcsidPtr != NULL) && (256 == MQCcsidLen) && (MQCcsidIn == ccsidIn) && (MQCcsidOut == ccsidOut))
	{
		// have the right table loaded - do the translation
		while (ret < len)
		{
			out[ret] = MQCcsidPtr[in[ret]];
			ret++;
		}
	}

	// return the number of characters that were translated
	return ret;
}

int DataFormatter::MQEBC2UCS(RFH_UCS2 *out, const unsigned char *in, int len, int ccsidIn)

{
	int		ret=0;
	int		tabLen;
	int		dirSize;
	char	* endInstallDir;
	FILE	*tbl;
	char	fileName[512];

	// this routine does not handle UCS-2 input for now, so return if one of the pages is UCS-2
	if ((13488 == ccsidIn) || (17584 == ccsidIn) || (1200 == ccsidIn) || (1201 == ccsidIn))
	{
		return 0;
	}

	// check if we already tried to read this table
	if ((NULL == MQUcsPtr) && (MQUcsCcsid == ccsidIn))
	{
		// don't try to load the same table each time this routine is called
		return 0;
	}

	// check if we already have the right table loaded
	if ((NULL == MQUcsPtr) || (MQUcsCcsid != ccsidIn))
	{
		// we need to try and read the table into memory
		// check if there is already a table that must be released
		if (MQUcsPtr != NULL)
		{
			// release the current table
			rfhFree(MQUcsPtr);

			// clear the variables
			MQUcsPtr = NULL;
		}

		MQUcsCcsid = ccsidIn;

		// get the file path where MQ is installed
		dirSize = sizeof(fileName);
		findMQInstallDirectory((unsigned char *)fileName, dirSize);

		// remember where the install directory name ends
		endInstallDir = fileName + strlen(fileName);

		// create a table name using the install path plus the CCSID value and 17584 as the target
		sprintf(endInstallDir, RFH_DIR_SEP "%4.4X44B0.tbl", ccsidIn);

		// now try to open the table
		tbl = tablesUsable() ? fopen(fileName, "rb") : NULL;

		// check if we found the translate table
		if (NULL == tbl)
		{
			// didn't work - try 13488 instead
			sprintf(endInstallDir, RFH_DIR_SEP "%4.4X34B0.tbl", ccsidIn);

			// now try to open the table
			tbl = tablesUsable() ? fopen(fileName, "rb") : NULL;
		}

		if (tbl != NULL)
		{
			// figure out how long the file is
			// read it into memory
			fseek(tbl, 0L, SEEK_END);
			tabLen = ftell(tbl);

			// return the pointer to the beginning of the data
			fseek(tbl, 0L, SEEK_SET);

			// only read the table if it is 256 bytes long
			if (512 == tabLen)
			{
				// allocate memory for the translation table
				MQUcsPtr = (RFH_UCS2 *)rfhMalloc(tabLen, "TRANTBL2");

				// try to read the contents of the file into memory
				fread(MQUcsPtr, 1, tabLen, tbl);
			}

			// done with the file - close the file handle
			fclose(tbl);
		}
	}

	// check if the table is loaded in memory and avoid tables that are not simple translations
	if ((MQUcsPtr != NULL) && (MQUcsCcsid == ccsidIn))
	{
		// have the right table loaded - do the translation
		while (ret < len)
		{
			out[ret] = reverseBytes((short *)&MQUcsPtr[in[ret]]);
			ret++;
		}
	}

	return ret;
}
