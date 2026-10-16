#!/usr/bin/perl
# One-off fix for Plan 2B: keeps IBM's shared JSON and XML helpers within their
# buffers on malformed data, which crashes or corrupts memory on Windows too (see
# the upstream report). Well-formed data gives exactly the same results as before.
#   RFHUtil/JsonParse.{h,cpp}: grow the parse tree instead of overrunning it, stop
#     at JPARSE_MAX_DEPTH levels of nesting, and stop when a pass of the parser
#     consumes nothing (for example "{]", which loops forever).
#   RFHUtil/xmlsubs.cpp: an attribute value without its closing quote ends at the
#     end of the text, and attribute names and values are sized from the text.
# Run once from the repository root.
use strict; use warnings;

# Every file is checked before any is written, so a failed run changes nothing.
my @pending;
sub edit {
	my ($path, $marker, @changes) = @_;
	open my $in, '<:raw', $path or die "$path: $!";
	my $text = do { local $/; <$in> }; close $in;
	die "$path: the fix is already applied\n" if index($text, $marker) >= 0;
	my $crlf = ($text =~ /\r\n/);
	$text =~ s/\r\n/\n/g;
	for my $c (@changes) {
		my ($what, $old, $new) = @$c;
		my $n = () = $text =~ /\Q$old\E/g;
		die "$path: $what: matched $n time(s), expected 1\n" unless $n == 1;
		$text =~ s/\Q$old\E/$new/;
	}
	$text =~ s/\n/\r\n/g if $crlf;
	push @pending, [$path, $text, scalar @changes];
}

edit('RFHUtil/JsonParse.h', 'JPARSE_TOO_DEEP',
	['result codes', "#define JPARSE_INVALID_START_CHAR\t200\n",
		"#define JPARSE_INVALID_START_CHAR\t200\n"
		. "#define JPARSE_TOO_DEEP\t\t\t\t201\t// objects and arrays nested deeper than JPARSE_MAX_DEPTH\n"
		. "#define JPARSE_MALFORMED\t\t\t202\t// the parser could not move past a character\n\n"
		. "// Each level of nesting recurses; this keeps well within a 1 MB Windows stack.\n"
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
	['grow the tree', "\t// check for a name\n\tif (nameLen > 0)\n\t{\n\t\tnameOfs = names.insertName(name, nameLen);\n\t}\n",
		"\t// parse() sizes the tree from a count of braces, colons and commas, which\n"
		. "\t// malformed data can exceed; grow it rather than write past the end\n"
		. "\tif (numElements >= maxElements)\n\t{\n"
		. "\t\tint newMax = (maxElements > 0) ? maxElements * 2 : 64;\n"
		. "\t\tJSONSTRUCT * bigger = (JSONSTRUCT *)rfhMalloc(newMax * sizeof(JSONSTRUCT), \"JSONELEM\");\n"
		. "\t\tif (NULL == bigger)\n\t\t{\n\t\t\t// out of memory: reuse the last element rather than overrun\n"
		. "\t\t\tnumElements = maxElements - 1;\n\t\t}\n\t\telse\n\t\t{\n"
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
);

for my $p (@pending) {
	my ($path, $text, $count) = @$p;
	open my $out, '>:raw', $path or die "$path: $!";
	print $out $text; close $out;
	printf "%-22s %d change(s)\n", $path, $count;
}
