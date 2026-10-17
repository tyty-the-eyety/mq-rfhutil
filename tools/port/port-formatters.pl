#!/usr/bin/perl
# port-formatters.pl - one-off extraction for Plan 2B.
#
# Copies the Data tab display formatters from RFHUtil/DataArea.cpp to the end of
# RFHUtil/DataFormatter.cpp, which must hold only its prelude (ending with the
# marker line below). The methods are copied in their DataArea.cpp order and are
# unchanged apart from the rewrites listed here, each of which must match exactly
# the expected number of times. DataArea.cpp is not modified. Run once from the
# repository root.
use strict; use warnings;

my $marker = "// ---- methods moved from DataArea.cpp by tools/port/port-formatters.pl ----\n";

sub slurp {
	my ($path) = @_;
	open my $in, '<:raw', $path or die "$path: $!\n";
	my $text = do { local $/; <$in> };
	close $in;
	return $text;
}

my $source = slurp('RFHUtil/DataArea.cpp');
$source =~ s/\r\n/\n/g;

my $target = slurp('RFHUtil/DataFormatter.cpp');
my $crlf = ($target =~ /\r\n/);
$target =~ s/\r\n/\n/g;
die "RFHUtil/DataFormatter.cpp: does not end with the marker line, or already holds the methods\n"
	unless $target =~ /\Q$marker\E\n*\z/;

# The formatters and the helpers only they call.
my @methods = qw(getHexData xlateEbcdicChar getCharacterData getBothData getTranslatedData
	getXmlData getJsonData getFixData getNameValueData getParsedData checkForUnicodeMarker
	findcrlf isLeadChar MQEBC2ASC MQEBC2UCS);

my @found;
for my $m (@methods) {
	my @hits;
	while ($source =~ /^([A-Za-z][^\n;]*?\bDataArea::\Q$m\E\s*\((?:[^)]|\n)*\)\s*\n\{.*?\n\}\n)/msg) {
		push @hits, [$-[1], $1];
	}
	die "DataArea.cpp: $m: found " . scalar(@hits) . " definitions, expected 1\n" unless @hits == 1;
	push @found, $hits[0];
}
my $methods = join("\n", map { $_->[1] } sort { $a->[0] <=> $b->[0] } @found);
$methods =~ s/\bDataArea::(\w+)\s*\(/DataFormatter::$1(/g;

# getNameValueData strips a trailing '/' or '\' from the tag name with
# tempVarName[strlen(tempVarName) - 1], which for an empty name ("< />") reads, and
# may clear, the byte before the buffer. The guard skips an empty name; the copies
# in getParsedData's commented-out code are left alone.
my @emptyName = map { [ "if ('$_' == tempVarName[strlen(tempVarName) - 1])",
	"if ((tempVarName[0] != 0) && ('$_' == tempVarName[strlen(tempVarName) - 1]))" ] } ('/', '\\\\');
sub guardEmptyName {
	my $n = 0;
	$_[0] =~ s{(\nvoid DataFormatter::getNameValueData\(.*?\n\}\n)}{
		my $body = $1;
		for my $e (@emptyName) { my ($old, $new) = @$e; $n += ($body =~ s/\Q$old\E/$new/g); }
		$body
	}se;
	return $n;
}

# [what, expected count, substitution]
my @rewrites = (
	['pointers traced as %8.8X', 10, sub { $_[0] =~ s/\(unsigned int\)\s*(m_data_\w+|newData|tempData|data)\b/(unsigned int)(uintptr_t)$1/g }],
	['application object declarations', 2, sub { $_[0] =~ s/^[ \t]*CRfhutilApp\s*\*\s*app\b[^;\n]*;[^\n]*\n//mg }],
	['application object lookups', 2, sub { $_[0] =~ s/^[ \t]*app\s*=\s*\(CRfhutilApp\s*\*\)\s*AfxGetApp\(\);[ \t]*\n//mg }],
	['wchar_t is 32 bits on Linux', 9, sub { $_[0] =~ s/\bwchar_t\b/RFH_UCS2/g }],
	['LPCWSTR casts', 4, sub { $_[0] =~ s/\(LPCWSTR\)/(const RFH_UCS2 *)/g }],
	['WideCharToMultiByte', 7, sub { $_[0] =~ s/\bWideCharToMultiByte\(/rfh_WideCharToMultiByte(/g }],
	['MultiByteToWideChar', 2, sub { $_[0] =~ s/\bMultiByteToWideChar\(/rfh_MultiByteToWideChar(/g }],
	['no Unicode character view output (IBM disabled the view)', 1, sub { $_[0] =~ s/^[ \t]*m_data_ascii_UCS = FALSE;[ \t]*\n//mg }],
	['CP_ACP is the display ANSI code page', 1, sub { $_[0] =~ s/\bCP_ACP\b/app->ansi/g }],
	['CP_OEMCP is the display OEM code page', 1, sub { $_[0] =~ s/\bCP_OEMCP\b/app->oem/g }],
	['C-locale wcstombs', 2, sub { $_[0] =~ s/wcstombs\(\(char \*\)newData, (\(const RFH_UCS2 \*\)\([^)]*\)), /rfh_ucs2_to_c_locale($1, (char *)newData, /g }],
	['MQ table directory separator', 3, sub { $_[0] =~ s/"\\\\conv\\\\table\\\\/RFH_DIR_SEP "/g }],
	['no usable table directory, no tables', 3, sub { $_[0] =~ s/\btbl = fopen\(fileName, "rb"\);/tbl = tablesUsable() ? fopen(fileName, "rb") : NULL;/g }],
	# memory safety (deliberate differences; see DataFormatter.h)
	['both view buffer', 1, sub { $_[0] =~ s/len = \(fileLength << 2\) \+ 65;/len = ((fileLength + 15) \/ 16) * 65 + 65;\t\t\/\/ 65 bytes per line of 16; Windows allocates (fileLength << 2) + 65/g }],
	['fixed view table index', 1, sub { $_[0] =~ s/m_data_fix\[len\] = invalidCharTable\[tempTxt\[0\]\];/m_data_fix[len] = invalidCharTable[(unsigned char)tempTxt[0]];/g }],
	['tag name copy stops at the work area', 2, sub { $_[0] =~ s/tempVarName\[j\+\+\] = tempData\[buffer\+\+\];/if ((size_t)j < sizeof(tempVarName) - 1) { tempVarName[j++] = tempData[buffer]; } buffer++;/g }],
	['attribute copy stops at the work area', 2, sub { $_[0] =~ s/tempVarAttr\[attrLen\+\+\] = tempData\[buffer\+\+\];/if ((size_t)attrLen < sizeof(tempVarAttr) - 1) { tempVarAttr[attrLen++] = tempData[buffer]; } buffer++;/g }],
	['end tag name terminator stops at the work area', 2, sub { $_[0] =~ s/tempEndVarName\[k\+\+\] = 0;/if ((size_t)k < sizeof(tempEndVarName) - 1) { tempEndVarName[k++] = 0; }/g }],
	['end tag name copy stops at the work area', 2, sub { $_[0] =~ s/tempEndVarName\[k\+\+\] = tempData\[buffer\+\+\];/if ((size_t)k < sizeof(tempEndVarName) - 1) { tempEndVarName[k++] = tempData[buffer]; } buffer++;/g }],
	['value copy stops at the work area', 2, sub { $_[0] =~ s/while \(\(buffer < remaining\) && \(tempData\[buffer\] != '<'\)\)(\s*\{\s*\/\/ check if we have non-whitespace characters)/while ((buffer < remaining) && (tempData[buffer] != '<') && (templen < (sizeof(tempVarValue) - 1)))$1/g }],
	['UCS-2 fallback loop stays within the data', 1, sub { $_[0] =~ s/for \(i=0; i<\(int\)fileSize; i\+\+\)(\s*\{\s*\/\/ translate a character from UCS to code page 1252\s*charCount = WCSto1252\()&ucsChar\[i\]/for (i=0; i<(int)((fileSize - extraOffset) \/ 2); i++)$1(const RFH_UCS2 *)(fileData + extraOffset) + i/g }],
	['empty tag name is not read before the name', 2, sub { guardEmptyName($_[0]) }],
	['EDI blank skip', 1, sub { $_[0] =~ s/while \(data\[buffer \+ newline\] <= ' '\)/while ((newline < remaining) && (data[buffer + newline] <= ' '))/g }],
);
for my $r (@rewrites) {
	my ($what, $want, $code) = @$r;
	my $n = 0 + ($code->($methods) || 0);
	die "$what: matched $n time(s), expected $want\n" unless $n == $want;
	printf "%-40s %2d\n", $what, $n;
}

$target =~ s/\n*\z/\n\n/;
$target .= $methods;
$target =~ s/\n/\r\n/g if $crlf;
open my $out, '>:raw', 'RFHUtil/DataFormatter.cpp' or die "RFHUtil/DataFormatter.cpp: $!\n";
print $out $target;
close $out;
printf "%d methods copied to RFHUtil/DataFormatter.cpp\n", scalar @found;
