#!/usr/bin/perl
# port-dataarea-formatters.pl - one-off edits for Plan 2B, Task 9: DataArea shows
# the Data tab views through DataFormatter (RFHUtil/DataFormatter.cpp) and drops
# its own copies of the formatters, so Windows and the portable core share one
# implementation.
#
# - the seven view methods (getCharacterData ... getFixData) forward to a
#   DataFormatter made for the current message; resetDisplayData deletes it, as it
#   freed the views before;
# - the methods that moved to DataFormatter, the dead getCharDataUcs, and the
#   members and work areas only they used are removed;
# - DataFormatter.{h,cpp} join the RFHUtil and Client projects.
#
# Every edit asserts its match count, every file is checked before any is written,
# and the result is checked for leftover references. Run once from the repository
# root; a second run fails on the first edit.
use strict; use warnings;

my @pending;

sub load {
	my ($path) = @_;
	open my $in, '<:raw', $path or die "$path: $!\n";
	my $text = do { local $/; <$in> };
	close $in;
	my $crlf = ($text =~ /\r\n/);
	$text =~ s/\r\n/\n/g;
	return ($text, $crlf);
}

# port(path, [what, expected count, sub { edits ${$_[0]}, returns the count }], ...)
sub port {
	my ($path, @edits) = @_;
	my ($text, $crlf) = load($path);
	for my $e (@edits) {
		my ($what, $want, $code) = @$e;
		my $n = 0 + ($code->(\$text) || 0);
		die "$path: $what: matched $n time(s), expected $want\n" unless $n == $want;
	}
	push @pending, [$path, $text, $crlf, scalar @edits];
	return \$pending[-1][1];
}

# Replaces the body of DataArea::name (signature kept) with new text.
sub body {
	my ($name, $new) = @_;
	return sub {
		${$_[0]} =~ s/^([A-Za-z][^\n;]*?\bDataArea::\Q$name\E\s*\((?:[^)]|\n)*\)\s*\n)\{.*?\n\}\n/$1$new/msg;
	};
}

# Removes the definition of DataArea::name, the comment block just before it and
# the blank lines after it.
sub remove {
	my ($name) = @_;
	return sub {
		${$_[0]} =~ s/^(?:\/{20,}\n(?:(?!\/{20,}\n)\/\/[^\n]*\n)*\/{20,}\n\n*)?[A-Za-z][^\n;]*?\bDataArea::\Q$name\E\s*\((?:[^)]|\n)*\)\s*\n\{.*?\n\}\n\n*//msg;
	};
}

sub line {
	my ($pattern) = @_;
	return sub { ${$_[0]} =~ s/^$pattern[^\n]*\n//mg };
}

my @moved = qw(getTranslatedData getNameValueData xlateEbcdicChar checkForUnicodeMarker
	findcrlf isLeadChar MQEBC2ASC MQEBC2UCS getCharDataUcs);
my @members = qw(MQCcsidLen MQUcsCcsid MQCcsidOut MQCcsidIn MQCcsidPtr MQUcsPtr
	saveEdi saveBOM saveParsedFormat saveXMLFormat saveBothFormat saveCharCrlf
	saveCharFormat saveJsonFormat saveFixFormat saveIndentOn invalidCharTable
	invalidCharTableNotBuilt m_data_ascii_UCS);
my @workAreas = qw(xmlVarName tempVarValue tempVarName tempEndVarName tempVarAttr xmlAttrStr);

my $view = sub {
	my ($call, $output, $error) = @_;
	return "{\n\tformatter()->$call;\n\t$output = m_formatter->$output;"
		. ($error ? "\n\tshowFormatterError();" : '') . "\n}\n";
};

my $helpers = <<'EOF';

//////////////////////////////////////////////////////////////
//
// The DataFormatter for the current message's Data tab views.
// It is made when the first view is shown and deleted by
// resetDisplayData, which runs whenever the message changes,
// so its views are kept and reused as DataArea's were.
//
//////////////////////////////////////////////////////////////

DataFormatter * DataArea::formatter()

{
	// a new message always passes through resetDisplayData; this also catches
	// data replaced without it
	if ((m_formatter != NULL) && ((m_formatter->source() != fileData) || (m_formatter->fileSize != fileSize)))
	{
		dropFormatter();
	}

	if (NULL == m_formatter)
	{
		CRfhutilApp *	app = (CRfhutilApp *)AfxGetApp();
		char			installDir[512] = { 0 };

		m_formatter = new DataFormatter();
		m_formatter->setData(fileData, fileSize);
		DisplayCodepages codepages = { app->ansi, app->oem };
		m_formatter->setDisplay(codepages);

		// MQ's conversion tables, which DataArea looked for under the install directory
		findMQInstallDirectory((unsigned char *)installDir, sizeof(installDir));
		if (installDir[0] != 0)
		{
			m_formatter->conversionTableDir = std::string(installDir) + "\\conv\\table";
		}
	}

	// the user may change these between views
	m_formatter->fileCcsid = fileCcsid;
	m_formatter->fileIntFormat = fileIntFormat;
	m_formatter->m_char_format = m_char_format;

	return m_formatter;
}

// Deletes the formatter and with it every view.
void DataArea::dropFormatter()

{
	delete m_formatter;
	m_formatter = NULL;
	m_data_ascii = NULL;
	m_data_hex = NULL;
	m_data_both = NULL;
	m_data_xml = NULL;
	m_data_parsed = NULL;
	m_data_json = NULL;
	m_data_fix = NULL;
}

// Shows an error the JSON or fixed view reported, as those views did in DataArea.
void DataArea::showFormatterError()

{
	if (!m_formatter->m_error_msg.empty())
	{
		m_error_msg = m_formatter->m_error_msg.c_str();
		m_formatter->m_error_msg.clear();
		updateMsgText();
	}
}
EOF

my $cpp = port('RFHUtil/DataArea.cpp',
	['include', 1, sub { ${$_[0]} =~ s/^(#include "jsonparse\.h"\n)/$1#include "DataFormatter.h"\n/mg }],
	['XML work areas', 1, sub { ${$_[0]} =~ s/^static char\txmlVarName\[8192 \+ 1024\];[^\n]*\n(?:static char\t\w+\[[^\]]*\];[^\n]*\n){5}\n//mg }],
	['constructor: view caches', 1, sub { ${$_[0]} =~ s/\tm_data_ascii_UCS = NULL;\n(\tm_data_hex = NULL;\n(?:\tm_data_\w+ = NULL;\n){5}\tbrowseActive = 0;\n\tbrowsePrevActive = 0;\n)\tsaveCharCrlf = IGNORE_CRLF;\n\tsaveCharFormat = CHAR_ASCII;\n\tsaveBothFormat = CHAR_ASCII;\n/$1\tm_formatter = NULL;\n/g }],
	['constructor: MQ tables', 1, sub { ${$_[0]} =~ s/\t\/\/ initialize the MQ CCSID table values\n\tMQCcsidIn = 0;\n\tMQCcsidOut = 0;\n\tMQCcsidLen = 0;\n\tMQCcsidPtr = NULL;\n\tMQUcsCcsid = 0;\n\tMQUcsPtr = NULL;\n\n//g }],
	['constructor: display table', 1, sub { ${$_[0]} =~ s/\tmemset\(invalidCharTable, '\.', sizeof\(invalidCharTable\)\);\n\tinvalidCharTableNotBuilt = TRUE;\n\n//g }],
	['freeAllocations: MQ tables', 1, sub { ${$_[0]} =~ s/\t\/\/ Was an MQ translation table used\?\n\tif \(MQCcsidPtr != NULL\)\n\t\{\n\t\t\/\/ release the allocated storage\n\t\trfhFree\(MQCcsidPtr\);\n\t\}\n\n\t\/\/ was an MQ to UCS-2 translation table used\?\n\tif \(MQUcsPtr != NULL\)\n\t\{\n\t\trfhFree\(MQUcsPtr\);\n\t\}\n\n//g }],
	['resetDisplayData: view settings', 1, sub { ${$_[0]} =~ s/(void DataArea::resetDisplayData\(\)\n\n\{\n)\tsaveCharCrlf = IGNORE_CRLF;\n\tsaveCharFormat = CHAR_ASCII;\n\tsaveBothFormat = CHAR_ASCII;\n\tsaveXMLFormat = CHAR_ASCII;\n\tsaveParsedFormat = CHAR_ASCII;\n\tsaveJsonFormat = CHAR_ASCII;\n\n/$1/g }],
	['resetDisplayData: views', 1, sub { ${$_[0]} =~ s/\tif \(m_data_ascii != NULL\)\n\t\{\n\t\trfhFree\(m_data_ascii\);\n.*?\tif \(m_data_fix != NULL\)\n\t\{\n\t\trfhFree\(m_data_fix\);\n\t\tm_data_fix = NULL;\n\t\}\n\}\n/\t\/\/ the views belong to the formatter\n\tdropFormatter();\n}\n/sg }],
	['getHexData', 1, body('getHexData', $view->('getHexData()', 'm_data_hex') . $helpers)],
	['getCharacterData', 1, body('getCharacterData', $view->('getCharacterData(charFormat, crlf, edi, BOM, indentOn)', 'm_data_ascii'))],
	['getBothData', 1, body('getBothData', $view->('getBothData(charFormat, BOM)', 'm_data_both'))],
	['getXmlData', 1, body('getXmlData', $view->('getXmlData(charFormat)', 'm_data_xml'))],
	['getJsonData', 1, body('getJsonData', $view->('getJsonData(charFormat)', 'm_data_json', 1))],
	['getFixData', 1, body('getFixData', $view->('getFixData(charFormat)', 'm_data_fix', 1))],
	['getParsedData', 1, body('getParsedData', $view->('getParsedData(charFormat)', 'm_data_parsed'))],
	map({ my $m = $_; ["remove $m", 1, remove($m)] } @moved),
);

port('RFHUtil/DataArea.h',
	['forward declaration', 1, sub { ${$_[0]} =~ s/^(class DataArea : public CObject\s*\n)/class DataFormatter;\n\n$1/mg }],
	map({ my $m = $_; ["declaration of $m", 1, line("\\t[A-Za-z][^\\n(]*\\b$m\\(")] } @moved),
	map({ my $m = $_; ["member $m", 1, line("\\t(?:int|BOOL|char|unsigned char \\*|wchar_t \\*)\\s*\\b$m\\b(?:\\[256\\])?;")] } @members),
	['formatter member', 1, sub { ${$_[0]} =~ s/^(\tCCopybook \*m_copybook;\n)/$1\tDataFormatter * m_formatter;\t\t\/\/ the Data tab views of the current message\n/mg }],
	['formatter methods', 1, sub { ${$_[0]} =~ s/^(\tvoid findMQInstallDirectory\(unsigned char \* dirName, unsigned long dirNameLen\);\n)/$1\tDataFormatter * formatter();\n\tvoid dropFormatter();\n\tvoid showFormatterError();\n/mg }],
);

# the projects: next to DataArea, in the same form as its entries
for my $project (['RFHUtil/RFHUtil.vcxproj', ''], ['Client/Client.vcxproj', '..\\RFHUtil\\']) {
	my ($path, $dir) = @$project;
	port($path,
		['source', 1, sub { ${$_[0]} =~ s/^(\s*)(<ClCompile Include="\Q${dir}\EDataArea\.cpp" \/>\n)/$1$2$1<ClCompile Include="${dir}DataFormatter.cpp" \/>\n/mg }],
		['header', 1, sub { ${$_[0]} =~ s/^(\s*)(<ClInclude Include="\Q${dir}\EDataArea\.h" \/>\n)/$1$2$1<ClInclude Include="${dir}DataFormatter.h" \/>\n/mg }],
	);
	port("$path.filters",
		['source', 1, sub { ${$_[0]} =~ s/^(\s*)(<ClCompile Include="\Q${dir}\EDataArea\.cpp">\n\s*<Filter>Source Files<\/Filter>\n\s*<\/ClCompile>\n)/$1$2$1<ClCompile Include="${dir}DataFormatter.cpp">\n$1  <Filter>Source Files<\/Filter>\n$1<\/ClCompile>\n/mg }],
		['header', 1, sub { ${$_[0]} =~ s/^(\s*)(<ClInclude Include="\Q${dir}\EDataArea\.h">\n\s*<Filter>Header Files<\/Filter>\n\s*<\/ClInclude>\n)/$1$2$1<ClInclude Include="${dir}DataFormatter.h">\n$1  <Filter>Header Files<\/Filter>\n$1<\/ClInclude>\n/mg }],
	);
}

# nothing left may use what was removed
(my $code = $$cpp) =~ s{//[^\n]*}{}g;
for my $id (@moved, @members, @workAreas) {
	my $n = () = $code =~ /\b\Q$id\E\b/g;
	die "RFHUtil/DataArea.cpp: $id is still used $n time(s)\n" if $n;
}

for my $p (@pending) {
	my ($path, $text, $crlf, $count) = @$p;
	$text =~ s/\n/\r\n/g if $crlf;
	open my $out, '>:raw', $path or die "$path: $!\n";
	print $out $text;
	close $out;
	printf "%-30s %2d edit(s)\n", $path, $count;
}
