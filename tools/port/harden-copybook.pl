#!/usr/bin/perl
# One-off fix for Plan 2B: keeps IBM's COBOL copy book parser (RFHUtil/Copybook.{h,cpp})
# within its buffers on malformed copy books and data, which crash or corrupt memory
# on Windows too (see the upstream report). Copy books and data that do not trigger
# one of these bugs give exactly the same view as before.
#   Parsing (parseCopyBook):
#     - buildNextLine counts the blank it adds between continued lines, so a long
#       statement of many short lines stops at the statement buffer instead of
#       overrunning it, and a line that starts with a NUL no longer reads before
#       its buffer;
#     - calcOffsets no longer divides by zero when a SYNC group's first child has no
#       alignment;
#     - getMaxLines saturates at MAX_COBOL_LINES instead of overflowing, which kept
#       the callers' buffer size an int (nested OCCURS multiply);
#     - offsets and lengths that a huge PIC or OCCURS overflows wrap, as they do on
#       Windows in practice, instead of being undefined;
#     - resetCopybook clears the whole variable table, including the part a large
#       copy book added, so a second copy book cannot follow stale links.
#   Formatting (formatData):
#     - a group OCCURS checks the buffer before each occurrence, as an elementary
#       OCCURS does, and stops when a child runs out of buffer;
#     - an OCCURS DEPENDING ON without a maximum shows no more occurrences than the
#       data holds;
#     - a group OCCURS stops before an occurrence that starts past the end of the
#       data (when the data is shorter than the copy book, or a REDEFINES is larger
#       than what it redefines), where Windows lists it with empty values, and the
#       nested call no longer measures the whole output for each occurrence;
#     - a field past the end of the data breaks out of its level only once: the
#       walk sets i = lastvar and goes on to its VarNext, which is not always 0, so
#       a second break walked the same chain again, for ever;
#     - updateLength's messages stop where displayDat's lines would;
#     - getVarValue and varReportLine read only the data being formatted, within
#       their work areas: a negative or huge length, a field outside the data and
#       a count field longer than its work area are clamped;
#     - checkData's numeric test copies only the bytes within the data;
#     - indentation is limited to the 31 blanks lineOfs holds;
#     - the trace lines are bounded by their buffer.
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

edit('RFHUtil/Copybook.h', 'MAX_COBOL_LINES',
	['line limit', "#define MAX_VAR_COUNT  12288\n",
		"#define MAX_VAR_COUNT  12288\n\n"
		. "// getMaxLines saturates here, which keeps the size of a caller's output buffer,\n"
		. "// (lines * 128) + 65536 + 8192, within an int.\n"
		. "#define MAX_COBOL_LINES  ((0x7FFFFFFF - 65536 - 8192) / 128)\n"],
	['data range members', "\tint dependCount;\n",
		"\tint dependCount;\n"
		. "\tconst unsigned char * dataStart;\t// formatData: the data being formatted,\n"
		. "\tconst unsigned char * dataEnd;\t\t// which getVarValue and varReportLine stay within\n"],
	['updateLength limit', "\t\t\t\t\t  char *printArea);\n",
		"\t\t\t\t\t  char *printArea,\n\t\t\t\t\t  const int maxArea);\n"],
);

my $helpers = <<'H';
#define RFHUTIL_TRACE_COBOL "RFHUTIL_TRACE_COBOL"

// Lengths and offsets that a malformed copy book overflows wrap, as they do on
// Windows in practice, instead of being undefined.
static int wrapAdd(const int a, const int b)
{
	return (int)((unsigned int)a + (unsigned int)b);
}

static int wrapMul(const int a, const int b)
{
	return (int)((unsigned int)a * (unsigned int)b);
}

// The bytes of a field that varReportLine shows as hex or packed decimal digits:
// two characters a byte and a terminator must fit in a work area of size bytes.
static int hexLength(const int len, const size_t size)
{
	if (len < 0)
	{
		return 0;
	}

	if (len > (int)((size - 1) / 2))
	{
		return (int)((size - 1) / 2);
	}

	return len;
}

// formatData's callers allocate maxArea + 8192 bytes. displayDat writes a line only
// while at least 4096 bytes are left below maxArea; updateLength's messages keep to
// the same limit.
static bool roomForMessage(const char *printArea, const int maxArea)
{
	return (int)strlen(printArea) <= (maxArea - 4096);
}
H

my $odoCap = <<'O';
				// prevent runaway loops
				if ((newCount > fvar[i].VarOccurMax) && (fvar[i].VarOccurMax > 0))
				{
					newCount = fvar[i].VarOccurMax;
				}

				// without a maximum, show no more occurrences than the data holds
				if ((fvar[i].VarOccurMax <= 0) && (fvar[i].VarLen > 0) &&
					(newCount > (endData - (dataIn + fvar[i].VarOffset)) / fvar[i].VarLen))
				{
					newCount = (int)((endData - (dataIn + fvar[i].VarOffset)) / fvar[i].VarLen);
				}
O

my $groupLoop = <<'G';
			while ((j <= occurCount) && (0 == rc))
			{
				// stop before an occurrence that starts past the end of the data
				if ((j > 1) && ((dataIn + fvar[i].VarOffset + dataOffset) > endData))
				{
					break;
				}

				// check the buffer before each occurrence, as for an elementary item
				if ((printLoc - printArea) > (maxArea - 4096))
				{
					strcat(printLoc, "***** Exceeded memory buffer\r\n");
					i = lastvar;
					rc = 1;			// break out of all the loops
					break;
				}

				// output the parent variable to the display
G

my $oldClamp = <<'C';
				// make sure we don't go past the end of the data
				if (msgdata + len > endData)
				{
					// limit the length to the available data
					len = endData - msgdata;

					// but never less than zero
					if (len < 0)
					{
						len = 0;
					}
				}

C

my $newClamp = <<'C';
				// a negative length shows nothing
				if (len < 0)
				{
					len = 0;
				}

				// make sure we don't go past the end of the data (before checkData
				// copies it), or start before it
				if ((msgdata < dataStart) || (msgdata + len > endData))
				{
					// limit the length to the available data
					len = endData - msgdata;

					// but never less than zero
					if ((len < 0) || (msgdata < dataStart))
					{
						len = 0;
					}
				}

				switch (fvar[varnum].VarType)
C

my $varValue = <<'V';
	len = fvar[varnum].VarLen;
	ptr = dataIn + fvar[varnum].VarOffset;

	// read only the data being formatted, and no more than workArea holds
	if ((ptr < dataStart) || (ptr > dataEnd) || (len < 0))
	{
		len = 0;
	}
	else
	{
		if (len > dataEnd - ptr)
		{
			len = (int)(dataEnd - ptr);
		}
	}

	if (USE_DISPLAY == fvar[varnum].VarUsage)
	{
		if (len > (int)sizeof(workArea) - 1)
		{
			len = (int)sizeof(workArea) - 1;
		}
	}
	else
	{
		// shown as hex in the trace
		len = hexLength(len, sizeof(workArea));
	}

V

my $maxChildren = <<'M';
	child = fvar[varnum].VarChild;
	if (0 == child)
	{
		maxLines = (occur > MAX_COBOL_LINES) ? MAX_COBOL_LINES : occur;
	}
	else
	{
		// saturate at MAX_COBOL_LINES instead of overflowing
		while (child > 0)
		{
			lines = getMaxChildren(child);
			maxLines = (lines > MAX_COBOL_LINES - maxLines) ? MAX_COBOL_LINES : maxLines + lines;
			child = fvar[child].VarNext;
		}

		if (maxLines < MAX_COBOL_LINES)
		{
			maxLines++;
		}

		maxLines = (maxLines > MAX_COBOL_LINES / occur) ? MAX_COBOL_LINES : maxLines * occur;
	}
M

edit('RFHUtil/Copybook.cpp', 'MAX_COBOL_LINES',
	['helpers', "#define RFHUTIL_TRACE_COBOL \"RFHUTIL_TRACE_COBOL\"\n", $helpers],
	['data range initialised', "\toffsetError = FALSE;\n\ttraceEnabled = FALSE;\n}\n",
		"\toffsetError = FALSE;\n\ttraceEnabled = FALSE;\n\tdataStart = NULL;\n\tdataEnd = NULL;\n}\n"],
	['trace lines bounded', "sprintf(traceInfo,", "snprintf(traceInfo, sizeof(traceInfo),", 33],

	# calcOffsets
	['elementary end offset wraps',
		"(*newOfs) = startOfs + (length * fvar[varnum].VarOccur) + (delta * (fvar[varnum].VarOccur - 1));",
		"(*newOfs) = wrapAdd(wrapAdd(startOfs, wrapMul(length, fvar[varnum].VarOccur)), wrapMul(delta, fvar[varnum].VarOccur - 1));"],
	['occurrence offset wraps', "\t\t\t\toffset += (occur - 1) * fvar[child].VarLen;\n",
		"\t\t\t\toffset = wrapAdd(offset, wrapMul(occur - 1, fvar[child].VarLen));\n"],
	['group length wraps', "\t\tfvar[varnum].VarLen = offset - startOfs;\n",
		"\t\tfvar[varnum].VarLen = wrapAdd(offset, wrapMul(-1, startOfs));\n"],
	['slack needs an aligned first child',
		"if ((0 == fvar[varnum].VarRedefine) && (fvar[varnum].VarSync > 1) && (fvar[varnum].VarOccur > 1))",
		"if ((0 == fvar[varnum].VarRedefine) && (fvar[varnum].VarSync > 1) && (fvar[varnum].VarOccur > 1) && (fvar[firstChild].VarSync > 1))"],
	['slack offset wraps', "\t\t\toffset += slack * (fvar[varnum].VarOccur - 1);\n",
		"\t\t\toffset = wrapAdd(offset, wrapMul(slack, fvar[varnum].VarOccur - 1));\n"],

	# displayDat
	['count without a maximum', "\t\t\t\t// prevent runaway loops\n"
		. "\t\t\t\tif ((newCount > fvar[i].VarOccurMax) && (fvar[i].VarOccurMax > 0))\n"
		. "\t\t\t\t{\n\t\t\t\t\tnewCount = fvar[i].VarOccurMax;\n\t\t\t\t}\n", $odoCap],
	['parent length wraps', "\t\t\t\t\tfvar[parent].VarLen += (newCount - occurCount) * fvar[i].VarLen;\n",
		"\t\t\t\t\tfvar[parent].VarLen = wrapAdd(fvar[parent].VarLen, wrapMul(newCount - occurCount, fvar[i].VarLen));\n"],
	['group occurrences checked', "\t\t\twhile (j <= occurCount)\n\t\t\t{\n\t\t\t\t// output the parent variable to the display\n", $groupLoop],
	['nested call starts at the end of the output', "\t\t\t\t\trc = displayDat(printArea + strlen(printArea),\n"
		. "\t\t\t\t\t\t\t\t\tmaxArea - strlen(printArea),\n",
		"\t\t\t\t\trc = displayDat(printLoc + strlen(printLoc),\n"
		. "\t\t\t\t\t\t\t\t\tmaxArea - (int)((printLoc + strlen(printLoc)) - printArea),\n"],
	['occurrence offsets wrap', "occurOfs + (j - 1) * (fvar[i].VarLen + fvar[i].VarEndSlack),",
		"wrapAdd(occurOfs, wrapMul(j - 1, wrapAdd(fvar[i].VarLen, fvar[i].VarEndSlack))),", 2],
	['nested offset wraps', "\t\t\t\t\t\t\t\t\toccurOfs + dataOffset,\n", "\t\t\t\t\t\t\t\t\twrapAdd(occurOfs, dataOffset),\n"],
	['data offsets wrap', "\t\t\t\tdataOffset += fvar[i].VarLen + fvar[i].VarEndSlack;\n",
		"\t\t\t\tdataOffset = wrapAdd(dataOffset, wrapAdd(fvar[i].VarLen, fvar[i].VarEndSlack));\n", 2],
	['walk break counter', "\tint\t\t\tparent;\n\tchar\t\t*printLoc;\n",
		"\tint\t\t\tparent;\n\tint\t\t\tlevelBreaks=0;\t// times the walk broke out of this level\n\tchar\t\t*printLoc;\n"],
	['walk ends at the end of the data', "\t\t\t// break out of this level\n\t\t\ti = lastvar;\n\t\t}\n",
		"\t\t\t// break out of this level. lastvar's VarNext need not end the chain;\n"
		. "\t\t\t// breaking out again would walk the same chain again, for ever.\n"
		. "\t\t\tif (++levelBreaks > 1)\n\t\t\t{\n\t\t\t\tbreak;\n\t\t\t}\n\n"
		. "\t\t\ti = lastvar;\n\t\t}\n"],
	['group data offset wraps', "\t\t\tdataOffset += fvar[i].VarLen;\n", "\t\t\tdataOffset = wrapAdd(dataOffset, fvar[i].VarLen);\n"],

	# updateLength and formatData
	['updateLength limit', "\t\t\t\t\t\t\t char *printArea)\n{\n",
		"\t\t\t\t\t\t\t char *printArea,\n\t\t\t\t\t\t\t const int maxArea)\n{\n"],
	['updateLength limit passed down', "updateLength(fvar[i].VarChild, dataIn, dataLen, numFormat, pdNumFormat, printArea);",
		"updateLength(fvar[i].VarChild, dataIn, dataLen, numFormat, pdNumFormat, printArea, maxArea);"],
	['updateLength messages limited',
		"\t\t\t\tsprintf(printArea + strlen(printArea), \"***** RFHUtil format error - Occurs value past end of input buffer\\n\");\n",
		"\t\t\t\tif (roomForMessage(printArea, maxArea))\n\t\t\t\t{\n"
		. "\t\t\t\t\tsprintf(printArea + strlen(printArea), \"***** RFHUtil format error - Occurs value past end of input buffer\\n\");\n"
		. "\t\t\t\t}\n"],
	['updateLength messages limited',
		"\t\t\t\tsprintf(printArea + strlen(printArea), \"***** RFHUtil format error - Occurs value exceeds maximum - occurCount=%d maximum=%d\\r\\n\",\n"
		. "\t\t\t\t\t\tocc,fvar[i].VarOccurMax);\n",
		"\t\t\t\tif (roomForMessage(printArea, maxArea))\n\t\t\t\t{\n"
		. "\t\t\t\t\tsprintf(printArea + strlen(printArea), \"***** RFHUtil format error - Occurs value exceeds maximum - occurCount=%d maximum=%d\\r\\n\",\n"
		. "\t\t\t\t\t\t\tocc,fvar[i].VarOccurMax);\n"
		. "\t\t\t\t}\n"],
	['updateLength messages limited',
		"\t\t\t\tsprintf(printArea + strlen(printArea), \"***** RFHUtil format error - Occurs value below minimum - value=%d minimum=%d\\r\\n\",\n"
		. "\t\t\t\t\t\tocc,fvar[i].VarOccur);\n",
		"\t\t\t\tif (roomForMessage(printArea, maxArea))\n\t\t\t\t{\n"
		. "\t\t\t\t\tsprintf(printArea + strlen(printArea), \"***** RFHUtil format error - Occurs value below minimum - value=%d minimum=%d\\r\\n\",\n"
		. "\t\t\t\t\t\t\tocc,fvar[i].VarOccur);\n"
		. "\t\t\t\t}\n"],
	['extra length wraps', "\t\t\textraLen = (occ - fvar[i].VarOccur) * fvar[i].VarLen;\n",
		"\t\t\textraLen = wrapMul(wrapAdd(occ, wrapMul(-1, fvar[i].VarOccur)), fvar[i].VarLen);\n"],
	['parent lengths wrap', "\t\t\t\tfvar[j].VarLen += extraLen;\n", "\t\t\t\tfvar[j].VarLen = wrapAdd(fvar[j].VarLen, extraLen);\n"],
	['data range set', "\t\t// no data - nothing to do\n\t\treturn 0;\n\t}\n",
		"\t\t// no data - nothing to do\n\t\treturn 0;\n\t}\n\n"
		. "\t// the data that getVarValue and varReportLine may read\n"
		. "\tdataStart = dataIn;\n\tdataEnd = dataIn + dataLen;\n"],
	['updateLength limit passed', "updateLength(1, dataIn, dataLen, numFormat, pdNumFormat, printArea);",
		"updateLength(1, dataIn, dataLen, numFormat, pdNumFormat, printArea, maxArea);"],

	# buildNextLine
	['empty line', "\t\tif (cobolLine[len] == '\\n')\n", "\t\tif ((len >= 0) && (cobolLine[len] == '\\n'))\n"],
	['statement buffer', "\t\t\t\t\tif (i < remaining)\n",
		"\t\t\t\t\t// line holds maxSize + MAX_LINE_LEN + 1 bytes (buildTree's cobolStmt);\n"
		. "\t\t\t\t\t// remaining does not count the blanks between lines\n"
		. "\t\t\t\t\tif ((i < remaining) && (((int)strlen(line) + i) < (maxSize + MAX_LINE_LEN + 1)))\n"],
	['statement buffer', "\t\t\t\t\t\t// Insert a blank as a delimiter\n\t\t\t\t\t\tstrcat(line,\" \");\n",
		"\t\t\t\t\t\t// Insert a blank as a delimiter, if it fits\n"
		. "\t\t\t\t\t\tif (((int)strlen(line) + 1) >= (maxSize + MAX_LINE_LEN + 1))\n"
		. "\t\t\t\t\t\t{\n\t\t\t\t\t\t\treturn 1;\n\t\t\t\t\t\t}\n\n"
		. "\t\t\t\t\t\tstrcat(line,\" \");\n"],

	# varReportLine
	['occurrence number', "\tchar\t\t\tocc[10];\n", "\tchar\t\t\tocc[16];\n"],
	['display length checked first', "\t\t\t\tswitch (fvar[varnum].VarType)\n", $newClamp],
	['display length checked first', $oldClamp, ""],
	['multibyte characters within the data', "\t\t\t\t\t\t\tlen = j;\n",
		"\t\t\t\t\t\t\t// but not past the end of the data\n"
		. "\t\t\t\t\t\t\tif (j > endData - msgdata)\n"
		. "\t\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\tj = (int)(endData - msgdata);\n\t\t\t\t\t\t\t}\n\n"
		. "\t\t\t\t\t\t\tlen = j;\n"],
	['hex fields within the data and dataText',
		"\t\t\t\tif (msgdata + len > endData)\n\t\t\t\t{\n\t\t\t\t\tdataText[0] = 0;\n\t\t\t\t}\n\t\t\t\telse\n\t\t\t\t{\n",
		"\t\t\t\tif ((msgdata < dataStart) || (msgdata + len > endData))\n\t\t\t\t{\n\t\t\t\t\tdataText[0] = 0;\n\t\t\t\t}\n\t\t\t\telse\n\t\t\t\t{\n"
		. "\t\t\t\t\t// no more than dataText holds\n"
		. "\t\t\t\t\tlen = hexLength(len, sizeof(dataText));\n\n", 6],
	['indent within lineOfs', "\t\tdepth = fvar[varnum].VarDepth - 1;\n",
		"\t\tdepth = fvar[varnum].VarDepth - 1;\n"
		. "\t\tif (depth > (int)sizeof(lineOfs) - 1)\n\t\t{\n\t\t\tdepth = (int)sizeof(lineOfs) - 1;\n\t\t}\n\n"],

	# resetCopybook
	['whole table cleared', "    memset(fvar,'\\0',(sizeof(COPY_STRUCT) * (MAX_VAR_COUNT + 1)));\n\tmaxvar=0;",
		"    memset(fvar,'\\0',(sizeof(COPY_STRUCT) * (maxvarcount + 1)));\n\tmaxvar=0;"],

	# getPackedValue
	['packed value wraps', "int CCopybook::getPackedValue(const unsigned char *dataIn, int length, int numFormat)\n\n{\n\tint\tvalue=0;\n",
		"int CCopybook::getPackedValue(const unsigned char *dataIn, int length, int numFormat)\n\n{\n\tunsigned int\tvalue=0;\t// wraps on overflow\n"],
	['packed value wraps', "\tvalue = value * sign;\n\n\treturn value;\n", "\tvalue = value * (unsigned int)sign;\n\n\treturn (int)value;\n"],

	# getVarValue
	['count within the data and workArea', "\tlen = fvar[varnum].VarLen;\n\tptr = dataIn + fvar[varnum].VarOffset;\n", $varValue],

	# getMaxChildren
	['line count saturates', "\tint\t\toccur=1;\n", "\tint\t\toccur=1;\n\tint\t\tlines;\n"],
	['line count saturates', "\tchild = fvar[varnum].VarChild;\n\tif (0 == child)\n\t{\n\t\tmaxLines = occur;\n\t}\n"
		. "\telse\n\t{\n\t\twhile (child > 0)\n\t\t{\n\t\t\tmaxLines += getMaxChildren(child);\n\t\t\tchild = fvar[child].VarNext;\n\t\t}\n\n"
		. "\t\tmaxLines++;\n\t\tmaxLines *= occur;\n\t}\n", $maxChildren],
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
