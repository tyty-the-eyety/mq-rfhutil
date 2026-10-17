#!/usr/bin/perl
# One-off fix for Plan 2B: keeps IBM's shared JSON and XML helpers within their
# buffers on malformed data, which crashes or corrupts memory on Windows too (see
# the upstream report). Well-formed data gives exactly the same results as before.
#   RFHUtil/JsonParse.{h,cpp}: grow the parse tree instead of overrunning it, stop
#     at JPARSE_MAX_DEPTH levels of nesting, and stop when a pass of the parser
#     consumes nothing (for example "{]", where every pass inserts another entry until the tree overruns).
#     Out of memory while growing the tree stops the parse with JPARSE_MALLOC_FAIL,
#     a second parse() starts from an empty tree, and the two array reads of a
#     comma no longer go past the end of the data. buildParsedArea (with
#     processElement and appendName) keeps every write, and its terminator, inside
#     the output buffer; output that fits is unchanged.
#   RFHUtil/xmlsubs.cpp: an attribute value without its closing quote ends at the
#     end of the text, attribute names and values are sized from the text, and
#     processAttributes takes the size of its output (RFHUtil/xmlsubs.h and the four
#     calls in RFHUtil/DataArea.cpp, which only Windows builds, follow).
#     Every comparison and peek ahead checks how much data remains (startsWith,
#     skipBlanksTo, skipQuotedTo): processDTD, processComment, processEntity,
#     checkIfXml and removeUnneededXML compared up to 18 bytes past the end of cut-off
#     data, and setEscChar copied 5 bytes after the ampersand whatever followed.
#     setEscChar and processTag stop at the terminator of their text (processTag
#     also kept its value's terminator past maxLen), findXmlValue no longer skips a
#     missing end bracket's terminator, and removeVarName no longer compares from
#     before the name when the end tag is longer. No signature changes.
# Run once from the repository root.
use strict; use warnings;

# Every file is checked before any is written, so a failed run changes nothing.
my @pending;
sub edit {
	my ($path, $marker, @changes) = @_;
	my $count = 0;
	open my $in, '<:raw', $path or die "$path: $!";
	my $text = do { local $/; <$in> }; close $in;
	die "$path: the fix is already applied\n" if index($text, $marker) >= 0;
	my $crlf = ($text =~ /\r\n/);
	$text =~ s/\r\n/\n/g;
	for my $c (@changes) {
		my ($what, $old, $new, $expect) = @$c;
		$expect //= 1;
		my $n = () = $text =~ /\Q$old\E/g;
		die "$path: $what: matched $n time(s), expected $expect\n" unless $n == $expect;
		$text =~ s/\Q$old\E/$new/g;
		$count += $expect;
	}
	$text =~ s/\n/\r\n/g if $crlf;
	push @pending, [$path, $text, $count];
}

edit('RFHUtil/JsonParse.h', 'JPARSE_TOO_DEEP',
	['result codes', "#define JPARSE_INVALID_START_CHAR\t200\n",
		"#define JPARSE_INVALID_START_CHAR\t200\n"
		. "#define JPARSE_TOO_DEEP\t\t\t\t201\t// objects and arrays nested deeper than JPARSE_MAX_DEPTH\n"
		. "#define JPARSE_MALFORMED\t\t\t202\t// the parser could not move past a character\n\n"
		. "// Each level of nesting recurses; this keeps well within a 1 MB Windows stack.\n"
		. "// Arrays count as a level too, so {\"a\":[{\"a\":[... reaches the limit after 128 steps.\n"
		. "#define JPARSE_MAX_DEPTH\t\t\t256\n"],
	['members', "\tJSONSTRUCT * jsonElems;\n\tint numElements;\n",
		"\tJSONSTRUCT * jsonElems;\n\tint numElements;\n"
		. "\tint maxElements;\t\t\t// capacity of jsonElems\n"
		. "\tint depth;\t\t\t\t\t// current object and array nesting\n"
		. "\tint stopReason;\t\t\t\t// JPARSE_OK, or why parsing stopped early\n"],
);

my $guard = <<'G';
	// stop before deep nesting exhausts the stack (each level recurses)
	DepthGuard guard(depth);
	if (depth > JPARSE_MAX_DEPTH)
	{
		stopReason = JPARSE_TOO_DEEP;
		return endPtr;
	}

G
my $progress = <<'P';
		// stop at once if an earlier step failed (out of memory, nesting too deep)
		if (stopReason != JPARSE_OK)
		{
			return endPtr;
		}

		// stop if the last pass consumed nothing, which malformed data can cause
		if (ptr == passStart)
		{
			stopReason = JPARSE_MALFORMED;
			return endPtr;
		}
		passStart = ptr;

P

edit('RFHUtil/JsonParse.cpp', 'stopReason',
	['constructor', "\tnumElements = 0;\n\tjsonElems = NULL;\n}",
		"\tnumElements = 0;\n\tjsonElems = NULL;\n\tmaxElements = 0;\n\tdepth = 0;\n\tstopReason = JPARSE_OK;\n}"],
	['capacity', "\tmemset(jsonElems, 0, memSize);\n",
		"\tmemset(jsonElems, 0, memSize);\n\tmaxElements = (int)(memSize / sizeof(JSONSTRUCT));\n"],
	['report the stop', "\t// start the parse process, starting with the top level element\n\tparseLevel(ptr, endPtr, root);\n",
		"\t// start the parse process, starting with the top level element\n\tdepth = 0;\n\tstopReason = JPARSE_OK;\n\tparseLevel(ptr, endPtr, root);\n\n"
		. "\tif (JPARSE_TOO_DEEP == stopReason)\n\t{\n\t\tlastError = \"Parse error - objects and arrays nested too deeply\";\n\t\treturn stopReason;\n\t}\n"
		. "\tif (JPARSE_MALLOC_FAIL == stopReason)\n\t{\n\t\tlastError = \"Parse error - out of memory\";\n\t\treturn stopReason;\n\t}\n"
		. "\tif (stopReason != JPARSE_OK)\n\t{\n\t\tlastError = \"Parse error - unexpected character\";\n\t\treturn stopReason;\n\t}\n"],
	['depth guard class', "const char * CJsonParse::parseLevel(const char *ptr, const char *endPtr, int parent)\n\n{\n",
		"namespace\n{\n\t// counts nesting for the life of one parseLevel or processArray call\n\tclass DepthGuard\n\t{\n\tpublic:\n"
		. "\t\tDepthGuard(int & depth) : m_depth(depth) { m_depth++; }\n\t\t~DepthGuard() { m_depth--; }\n\tprivate:\n\t\tint & m_depth;\n\t};\n}\n\n"
		. "const char * CJsonParse::parseLevel(const char *ptr, const char *endPtr, int parent)\n\n{\n" . $guard
		. "\tconst char *\tpassStart = NULL;\t\t// where the current pass of the loop began\n"],
	['object progress', "\twhile ((ptr < endPtr) && (ptr[0] != '}'))\n\t{\n",
		"\twhile ((ptr < endPtr) && (ptr[0] != '}'))\n\t{\n" . $progress],
	['array guard', "const char * CJsonParse::processArray(const char *ptr, const char *endPtr, int name, const char *namePtr, int nameLen, int parent)\n\n{\n",
		"const char * CJsonParse::processArray(const char *ptr, const char *endPtr, int name, const char *namePtr, int nameLen, int parent)\n\n{\n" . $guard
		. "\tconst char *\tpassStart = NULL;\t\t// where the current pass of the loop began\n"],
	['array progress', "\twhile ((ptr < endPtr) && (ptr[0] != ']'))\n\t{\n",
		"\twhile ((ptr < endPtr) && (ptr[0] != ']'))\n\t{\n" . $progress],
	['comma after an object in an array', "\t\t\t// check for a comma\n\t\t\tif (',' == ptr[0])\n",
		"\t\t\t// check for a comma\n\t\t\tif ((ptr < endPtr) && (',' == ptr[0]))\n"],
	['comma after a value in an array', "\t\t// check for a comma\n\t\tif (',' == ptr[0])\n",
		"\t\t// check for a comma\n\t\tif ((ptr < endPtr) && (',' == ptr[0]))\n"],
	['start from an empty tree', "\t// point to the JSON data\n\tptr = jsonIn;\n\tidx = 0;\n",
		"\t// a second parse starts from an empty tree (the names stay: Names has no\n"
		. "\t// reset, and a name found again is shared)\n"
		. "\tif (jsonElems != NULL)\n\t{\n\t\trfhFree(jsonElems);\n\t\tjsonElems = NULL;\n\t}\n"
		. "\tnumElements = 0;\n\tmaxElements = 0;\n\tdepth = 0;\n\tstopReason = JPARSE_OK;\n\n"
		. "\t// point to the JSON data\n\tptr = jsonIn;\n\tidx = 0;\n"],
	# buildParsedArea and what it calls: the output pointer never passes endPtr - 1,
	# so the terminator always fits and output that fits is unchanged
	['index length variable', "\tint\t\t\tsibCount;\n\tint\t\t\tsib;\n\tchar *\t\tnamePtr;\n",
		"\tint\t\t\tsibCount;\n\tint\t\t\tsib;\n\tint\t\t\twritten;\n\tchar *\t\tnamePtr;\n"],
	['index of a repeated name', "\t\t\t// insert an index\n\t\t\tsprintf(ptr, \"[%d]\", sibCount + 1);\n\n"
		. "\t\t\t// skip the inserted characters\n\t\t\twhile (ptr[0] != ']')\n\t\t\t{\n"
		. "\t\t\t\t// move on to the next character\n\t\t\t\tptr++;\n\t\t\t}\n\n"
		. "\t\t\t// skip the ending bracket\n\t\t\tptr++;\n",
		"\t\t\t// insert an index, but only if all of it fits below the end of the buffer\n"
		. "\t\t\twritten = snprintf(ptr, endPtr - ptr, \"[%d]\", sibCount + 1);\n\n"
		. "\t\t\t// skip the inserted characters\n"
		. "\t\t\tif ((written > 0) && (written < endPtr - ptr))\n\t\t\t{\n\t\t\t\tptr += written;\n\t\t\t}\n"],
	['index of 1', "\t\t\t\t// insert an index of 1\n\t\t\t\tstrcpy(ptr, \"[1]\");\n\t\t\t\tptr += 3;\n",
		"\t\t\t\t// insert an index of 1, if it fits\n\t\t\t\tif (endPtr - ptr > 3)\n\t\t\t\t{\n"
		. "\t\t\t\t\tstrcpy(ptr, \"[1]\");\n\t\t\t\t\tptr += 3;\n\t\t\t\t}\n"],
	['equal sign', "\t\t// insert an equal sign\n\t\tptr[0] = '=';\n\t\tptr++;\n",
		"\t\t// insert an equal sign, leaving room for the terminator\n\t\tif (ptr + 1 < endPtr)\n\t\t{\n\t\t\tptr[0] = '=';\n\t\t\tptr++;\n\t\t}\n"],
	['period', "\t\t\t// insert a period\n\t\t\tptr[0] = '.';\n\t\t\tptr++;\n",
		"\t\t\t// insert a period, leaving room for the terminator\n\t\t\tif (ptr + 1 < endPtr)\n\t\t\t{\n\t\t\t\tptr[0] = '.';\n\t\t\t\tptr++;\n\t\t\t}\n"],
	['no buffer', "\t// initialize the output data to a null string\n\toutput[0] = 0;\n",
		"\t// there must be room for the terminator\n\tif (maxLen < 1)\n\t{\n\t\treturn;\n\t}\n\n"
		. "\t// initialize the output data to a null string\n\toutput[0] = 0;\n"],
	['grow the tree', "\t// check for a name\n\tif (nameLen > 0)\n\t{\n\t\tnameOfs = names.insertName(name, nameLen);\n\t}\n",
		"\t// parse() sizes the tree from a count of braces, colons and commas, which\n"
		. "\t// malformed data can exceed; grow it rather than write past the end\n"
		. "\tif (numElements >= maxElements)\n\t{\n"
		. "\t\tint newMax = (maxElements > 0) ? maxElements * 2 : 64;\n"
		. "\t\tJSONSTRUCT * bigger = (JSONSTRUCT *)rfhMalloc(newMax * sizeof(JSONSTRUCT), \"JSONELEM\");\n"
		. "\t\tif (NULL == bigger)\n\t\t{\n\t\t\t// out of memory: link nothing and make the parse stop\n"
		. "\t\t\tstopReason = JPARSE_MALLOC_FAIL;\n\t\t\treturn 0;\n\t\t}\n\t\telse\n\t\t{\n"
		. "\t\t\tmemset(bigger, 0, newMax * sizeof(JSONSTRUCT));\n"
		. "\t\t\tif (jsonElems != NULL)\n\t\t\t{\n"
		. "\t\t\t\tmemcpy(bigger, jsonElems, numElements * sizeof(JSONSTRUCT));\n\t\t\t\trfhFree(jsonElems);\n\t\t\t}\n"
		. "\t\t\tjsonElems = bigger;\n\t\t\tmaxElements = newMax;\n\t\t}\n\t}\n\n"
		. "\t// check for a name\n\tif (nameLen > 0)\n\t{\n\t\tnameOfs = names.insertName(name, nameLen);\n\t}\n"],
);


edit('RFHUtil/xmlsubs.cpp', 'workLen',
	['unterminated quote', "\t\t\t\twhile (1)\n\t\t\t\t{\n\t\t\t\t\tif ((tempValue[0] == quotechar) && (tempValue[1] != quotechar))\n",
		"\t\t\t\twhile (1)\n\t\t\t\t{\n"
		. "\t\t\t\t\t// stop at the end of the text when the closing quote is missing\n"
		. "\t\t\t\t\tif (0 == tempValue[0])\n\t\t\t\t\t{\n\t\t\t\t\t\tbreak;\n\t\t\t\t\t}\n\n"
		. "\t\t\t\t\tif ((tempValue[0] == quotechar) && (tempValue[1] != quotechar))\n"],
	['no closing quote to skip', "\t\t\t\t// skip the trailing double quote or other character\n\t\t\t\ttempValue++;\n",
		"\t\t\t\t// skip the trailing double quote or other character\n"
		. "\t\t\t\tif (tempValue[0] != 0)\n\t\t\t\t{\n\t\t\t\t\ttempValue++;\n\t\t\t\t}\n"],
	['work areas', "\tchar\t*tempVarName = (char *)rfhMalloc(8192,\"XMLSUBVN\");\n\tchar\t*tempVarValue = (char *)rfhMalloc(8192,\"XMLSUBVV\");\n",
		"\t// a name or value is never longer than the attribute text\n"
		. "\tsize_t\tworkLen = strlen(tempVarAttr) + 8192;\n"
		. "\tchar\t*tempVarName = (char *)rfhMalloc(workLen,\"XMLSUBVN\");\n"
		. "\tchar\t*tempVarValue = (char *)rfhMalloc(workLen,\"XMLSUBVV\");\n"],
	['bounded append helper', "////////////////////////////////////////////////////\n//\n// Routine to parse any attribute values that\n",
		"// Append text to a string of destSize bytes, cutting it off when the room runs\n"
		. "// out; the terminator is always kept\n"
		. "static void appendBounded(char *dest, size_t destSize, const char *text)\n\n{\n"
		. "\tsize_t\tused = 0;\n\tsize_t\tlen;\n\n"
		. "\twhile ((used < destSize) && (dest[used] != 0))\n\t{\n\t\tused++;\n\t}\n\n"
		. "\t// no room, or no terminator within the buffer\n"
		. "\tif (used + 1 >= destSize)\n\t{\n\t\treturn;\n\t}\n\n"
		. "\tlen = strlen(text);\n\tif (len > destSize - used - 1)\n\t{\n\t\tlen = destSize - used - 1;\n\t}\n\n"
		. "\tmemcpy(dest + used, text, len);\n\tdest[used + len] = 0;\n}\n\n"
		. "////////////////////////////////////////////////////\n//\n// Routine to parse any attribute values that\n"],
	['output size parameter', "void processAttributes(char *xmlVarName, char *varName, char *tempVarAttr, char *xmlAttrStr)\n",
		"void processAttributes(char *xmlVarName, char *varName, char *tempVarAttr, char *xmlAttrStr, size_t attrStrSize)\n"],
	['bounded appends',
		"\t\t\t\tstrcat(xmlAttrStr, xmlVarName);\n\t\t\t\tstrcat(xmlAttrStr, \".\");\n"
		. "\t\t\t}\n\n\t\t\t// check for the variable name\n\t\t\tif ((varName != NULL) && (varName[0] != 0))\n\t\t\t{\n"
		. "\t\t\t\tstrcat(xmlAttrStr, varName);\n\t\t\t\tstrcat(xmlAttrStr, \".\");\n\t\t\t}\n\n"
		. "\t\t\tstrcat(xmlAttrStr, \"(XML.attr)\");\n\t\t\tstrcat(xmlAttrStr, tempVarName);\n"
		. "\t\t\tstrcat(xmlAttrStr, \"='\");\n\t\t\tstrcat(xmlAttrStr, tempVarValue);\n"
		. "\t\t\tstrcat(xmlAttrStr, \"'\");\n\t\t\tstrcat(xmlAttrStr, \"\\r\\n\");\n",
		"\t\t\t\tappendBounded(xmlAttrStr, attrStrSize, xmlVarName);\n\t\t\t\tappendBounded(xmlAttrStr, attrStrSize, \".\");\n"
		. "\t\t\t}\n\n\t\t\t// check for the variable name\n\t\t\tif ((varName != NULL) && (varName[0] != 0))\n\t\t\t{\n"
		. "\t\t\t\tappendBounded(xmlAttrStr, attrStrSize, varName);\n\t\t\t\tappendBounded(xmlAttrStr, attrStrSize, \".\");\n\t\t\t}\n\n"
		. "\t\t\tappendBounded(xmlAttrStr, attrStrSize, \"(XML.attr)\");\n\t\t\tappendBounded(xmlAttrStr, attrStrSize, tempVarName);\n"
		. "\t\t\tappendBounded(xmlAttrStr, attrStrSize, \"='\");\n\t\t\tappendBounded(xmlAttrStr, attrStrSize, tempVarValue);\n"
		. "\t\t\tappendBounded(xmlAttrStr, attrStrSize, \"'\");\n\t\t\tappendBounded(xmlAttrStr, attrStrSize, \"\\r\\n\");\n"],
	# look-ahead: every comparison and peek checks what remains (see the header)
	[q~look-ahead helpers~,
		q~#define ENTITY_START	"<!ENTITY"
~,
		q~#define ENTITY_START	"<!ENTITY"

// Every comparison or peek ahead in this file checks how much data remains, so
// cut-off or malformed data is never read past its end. When enough data remains
// the results are the same as before.

// true only if the n bytes at ptr lie before end and match lit
static bool startsWith(const void *ptr, const void *end, const char *lit, size_t n)

{
	const unsigned char *	p = (const unsigned char *)ptr;
	const unsigned char *	e = (const unsigned char *)end;

	return (p <= e) && ((size_t)(e - p) >= n) && (memcmp(p, lit, n) == 0);
}

// skipBlanks (comsubs.cpp), stopping at end as well
static const char * skipBlanksTo(const char *str, const char *end)

{
	while ((str < end) && (str[0] <= ' ') && (str[0] != 0))
	{
		str++;
	}

	return str;
}

// skipQuotedString (comsubs.cpp) reads the byte at its eod, so it is given the
// last byte of the data, and nothing when no data remains
static const char * skipQuotedTo(const char *start, const char *end)

{
	if (start >= end)
	{
		return start;
	}

	return skipQuotedString(start, end - 1);
}
~],
	[q~DTD start~,
		q~if (memcmp(dtd, "<!DOCTYPE", 9) == 0)~,
		q~if (startsWith(dtd, dtd + remaining, "<!DOCTYPE", 9))~],
	[q~DTD bracket~,
		q~if ('[' == dtdend[0])~,
		q~if ((dtdend < dtd + remaining) && ('[' == dtdend[0]))~, 2],
	[q~DTD entity~,
		q~if (memcmp(dtdend, "<!ENTITY", 8) == 0)~,
		q~if (startsWith(dtdend, dtd + remaining, "<!ENTITY", 8))~],
	[q~DTD quoted bracket~,
		q~if (memcmp(dtdend, "\"]\"", 3) != 0)~,
		q~if (!startsWith(dtdend, dtd + remaining, "\"]\"", 3))~],
	[q~Unicode DTD start~,
		q~if (memcmp(dtd, "<\0!\0D\0O\0C\0T\0Y\0P\0E\0", 18) == 0)~,
		q~if (startsWith(dtd, dtd + remaining, "<\0!\0D\0O\0C\0T\0Y\0P\0E\0", 18))~],
	[q~Unicode DTD bracket~,
		q~(memcmp(dtdend, "[\0", 2) != 0) &&~ . " \n",
		q~!startsWith(dtdend, dtd + remaining, "[\0", 2) &&~ . "\n"],
	[q~Unicode DTD end~,
		q~(memcmp(dtdend, ">\0", 2) != 0)~,
		q~!startsWith(dtdend, dtd + remaining, ">\0", 2)~, 2],
	[q~Unicode DTD closing bracket~,
		q~(memcmp(dtdend, "]\0", 2) != 0)~,
		q~!startsWith(dtdend, dtd + remaining, "]\0", 2)~],
	[q~comment start~,
		q~if (memcmp(xml, "<!--", 4) == 0)~,
		q~if (startsWith(xml, xml + remaining, "<!--", 4))~],
	[q~comment end~,
		q~(memcmp(xmlend, "-->", 3) != 0)~,
		q~!startsWith(xmlend, xml + remaining, "-->", 3)~],
	[q~Unicode comment start~,
		q~if (memcmp(xml, "<\0!\0-\0-\0", 8) == 0)~,
		q~if (startsWith(xml, xml + remaining, "<\0!\0-\0-\0", 8))~],
	[q~Unicode comment end~,
		q~(memcmp(xmlend, "-\0-\0>\0", 6) != 0)~,
		q~!startsWith(xmlend, xml + remaining, "-\0-\0>\0", 6)~],
	[q~entity start~,
		q~if (memcmp(dtdend, ENTITY_START, sizeof(ENTITY_START) - 1) == 0)~,
		q~if (startsWith(dtdend, eod, ENTITY_START, sizeof(ENTITY_START) - 1))~],
	[q~entity blanks~,
		q~dtdend = skipBlanks((char *) dtdend);~,
		q~dtdend = skipBlanksTo(dtdend, eod);~, 6],
	[q~parameter entity~,
		q~if ('%' == dtdend[0])~,
		q~if ((dtdend < eod) && ('%' == dtdend[0]))~],
	[q~entity type~,
		q~if ((memcmp(dtdend, "SYSTEM", 6) == 0) || (memcmp(dtdend, "PUBLIC", 6) == 0))~,
		q~if (startsWith(dtdend, eod, "SYSTEM", 6) || startsWith(dtdend, eod, "PUBLIC", 6))~],
	[q~entity quoted strings~,
		q~dtdend = skipQuotedString(dtdend, eod);~,
		q~dtdend = skipQuotedTo(dtdend, eod);~, 3],
	[q~removed comment~,
		q~(memcmp(dataIn + j, "<!--", 4) == 0)~,
		q~startsWith(dataIn + j, dataIn + len, "<!--", 4)~],
	[q~removed Unicode comment~,
		q~(memcmp(dataIn + j, "<\0!\0-\0-\0", 8) == 0)~,
		q~startsWith(dataIn + j, dataIn + len, "<\0!\0-\0-\0", 8)~],
	[q~removed DTD~,
		q~(memcmp(dataIn + j, "<!DOCTYPE", 9) == 0)~,
		q~startsWith(dataIn + j, dataIn + len, "<!DOCTYPE", 9)~],
	[q~removed Unicode DTD~,
		q~(memcmp(dataIn + j, "<\0!\0D\0O\0C\0T\0Y\0P\0E\0", 18) == 0)~,
		q~startsWith(dataIn + j, dataIn + len, "<\0!\0D\0O\0C\0T\0Y\0P\0E\0", 18)~],
	[q~CDATA start~,
		q~if (memcmp(data + offset, "<![CDATA", 8) == 0)~,
		q~if (startsWith(data + offset, data + len, "<![CDATA", 8))~],
	[q~checked comment~,
		q~(memcmp(data + offset, "<!--", 4) == 0)~,
		q~startsWith(data + offset, data + len, "<!--", 4)~],
	[q~checked Unicode comment~,
		q~(memcmp(data + offset, "<\0!\0-\0-\0", 8) == 0)~,
		q~startsWith(data + offset, data + len, "<\0!\0-\0-\0", 8)~],
	[q~checked DTD~,
		q~(memcmp(data + offset, "<!DOCTYPE", 9) == 0)~,
		q~startsWith(data + offset, data + len, "<!DOCTYPE", 9)~],
	[q~checked Unicode DTD~,
		q~(memcmp(data + offset, "<\0!\0D\0O\0C\0T\0Y\0P\0E\0", 18) == 0)~,
		q~startsWith(data + offset, data + len, "<\0!\0D\0O\0C\0T\0Y\0P\0E\0", 18)~],
	[q~CDATA end~,
		q~(memcmp(data + offset, CDATA_END, 3) != 0)~,
		q~!startsWith(data + offset, data + len, CDATA_END, 3)~],
	[q~value without an end bracket~,
		q~		// skip the end bracket
		tempptr++;
~,
		q~		// skip the end bracket, if there is one (findEndBracket stops at the terminator)
		if ('>' == tempptr[0])
		{
			tempptr++;
		}
~],
	[q~end tag longer than the name~,
		q~	tempPtr = xmlVarName + templen2 - templen;
	if (strcmp(tempPtr, VarName + 1) == 0)
~,
		q~	// an end tag longer than the whole name cannot match, and must not point before it
	tempPtr = xmlVarName + ((templen <= templen2) ? templen2 - templen : 0);
	if ((templen <= templen2) && (strcmp(tempPtr, VarName + 1) == 0))
~],
	[q~entity text~,
		q~	// get the data in a temporary buffer
	memcpy(tempBuf, bufferin + 1, 5);
	tempBuf[5] = 0;
~,
		q~	// get the data in a temporary buffer: up to 5 characters after the ampersand,
	// stopping at the terminator (the comparisons below fail there either way)
	memset(tempBuf, 0, sizeof(tempBuf));
	for (i = 0; (i < 5) && (bufferin[i] != 0); i++)
	{
		tempBuf[i] = bufferin[i + 1];
	}
	tempBuf[5] = 0;
~],
	[q~tag name~,
		q~if (memcmp(ptr, tagName + 1, len - 2) != 0) ~,
		q~if (strncmp(ptr, tagName + 1, len - 2) != 0)~],
	[q~tag value~,
		q~	while ((ptr[0] != '<') && (i < maxLen))
~,
		q~	// stop at the terminator, and leave room for the value's own
	while ((ptr[0] != '<') && (ptr[0] != 0) && (i < maxLen - 1))
~],
);

edit('RFHUtil/xmlsubs.h', 'attrStrSize',
	['declaration', "void processAttributes(char *xmlVarName, char *varName, char *tempVarAttr, char *xmlAttrStr);",
		"void processAttributes(char *xmlVarName, char *varName, char *tempVarAttr, char *xmlAttrStr, size_t attrStrSize);"],
);

# Windows only; not built here. The four calls pass xmlAttrStr, a static array.
edit('RFHUtil/DataArea.cpp', 'tempVarAttr, xmlAttrStr, sizeof(xmlAttrStr)',
	['processAttributes calls', ", tempVarAttr, xmlAttrStr);", ", tempVarAttr, xmlAttrStr, sizeof(xmlAttrStr));", 4],
);

# Write each file beside its target and rename it into place, so a failure part-way
# leaves no half-written file; the temporary files are removed on failure.
my @temps;
for my $p (@pending) {
	my ($path, $text, $count) = @$p;
	my $tmp = "$path.tmp";
	my $ok = open my $out, '>:raw', $tmp;
	if ($ok) { push @temps, $tmp; $ok = print $out $text; $ok = close($out) && $ok; }
	unless ($ok) { my $err = $!; unlink @temps; die "$tmp: $err\n"; }
	my $mode = (stat $path)[2] & 07777;
	chmod $mode, $tmp;
}
for my $p (@pending) {
	my ($path, $text, $count) = @$p;
	rename "$path.tmp", $path or die "$path: $!\n";
	printf "%-22s %d change(s)\n", $path, $count;
}
