#!/usr/bin/perl
# port-shared-sources.pl - one-off edits that make the shared RFHUtil sources portable.
# Usage: perl port-shared-sources.pl <repo-root>
# Every edit asserts that it matched, so a drifted source tree fails loudly.
use strict;
use warnings;

my $root = shift or die "usage: $0 <repo-root>\n";
chdir $root or die "cannot chdir $root: $!\n";

sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "$f: $!\n"; local $/; my $t = <$h>; close $h; return $t; }
sub spew  { my ($f, $t) = @_; open(my $h, '>:raw', $f) or die "$f: $!\n"; print $h $t; close $h; }

# Applies a substitution and dies unless it changed the text at least $min times.
sub edit {
    my ($file, $textref, $label, $min, $code) = @_;
    my $count = $code->($textref);
    die "$file: edit '$label' matched $count time(s), expected at least $min\n" if $count < $min;
}

# The sources use CRLF line endings; work on LF and restore CRLF on write.
sub load { my $t = slurp($_[0]); my $crlf = ($t =~ /\r\n/); $t =~ s/\r\n/\n/g; return ($t, $crlf); }
sub save { my ($f, $t, $crlf) = @_; $t =~ s/\n/\r\n/g if $crlf; spew($f, $t); }

# Trace access and 32-bit pointer formatting are shared by several files.
sub portTrace {
    my ($f, $t) = @_;
    edit($f, $t, 'trace pointer type', 1, sub { ${$_[0]} =~ s/CRfhutilApp\s*\*(\s*)app;/RfhTrace *$1app;/g });
    edit($f, $t, 'trace lookup', 1, sub { ${$_[0]} =~ s/\(CRfhutilApp \*\)AfxGetApp\(\)/rfhGetTrace()/g });
}
sub portPointerTrace {
    my ($f, $t, $min) = @_;
    edit($f, $t, 'pointer casts in %8.8X trace', $min, sub {
        my $n = 0;
        ${$_[0]} =~ s{^(.*%8\.8[Xx].*)$}{ my $l = $1; $n += ($l =~ s/\(unsigned int\)(?!\(uintptr_t\))/(unsigned int)(uintptr_t)/g); $l }gme;
        $n;
    });
}
sub includeAfter {
    my ($f, $t, $anchor, $add) = @_;
    edit($f, $t, "include after $anchor", 1, sub { ${$_[0]} =~ s/^(#include "\Q$anchor\E"\n)/$1$add/m });
}
sub includeAtTop {
    my ($f, $t, $add) = @_;
    edit($f, $t, 'include after license', 1, sub { ${$_[0]} =~ s{^(\*/\n)}{$1\n$add}m });
}

# ---------------------------------------------------------------- comsubs.h
{
    my $f = 'RFHUtil/comsubs.h'; my ($t, $crlf) = load($f);
    includeAtTop($f, \$t, "#include \"rfh_platform.h\"\n\n");
    edit($f, \$t, 'MY_TIME_T', 1, sub { $t =~ s/typedef unsigned __int64 MY_TIME_T;/typedef unsigned long long MY_TIME_T;/ });
    edit($f, \$t, 'processBackspace decl', 1, sub { $t =~ s/^void processBackspace\(CWnd \*wnd\);\n//m });
    edit($f, \$t, 'wchar_t', 10, sub { $t =~ s/\bwchar_t\b/RFH_UCS2/g });
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- comsubs.cpp
my $backspace;
{
    my $f = 'RFHUtil/comsubs.cpp'; my ($t, $crlf) = load($f);
    edit($f, \$t, 'dump file', 1, sub { $t =~ s/#define DUMP_FILE_NAME "c:\\\\rfhdump\.txt"/#define DUMP_FILE_NAME rfh_dump_path("rfhdump.txt")/ });
    includeAfter($f, \$t, 'comsubs.h', "#include \"rfh_trace.h\"\n#include \"rfh_codepage.h\"\n");
    edit($f, \$t, 'code page globals', 1, sub { $t =~ s/\n\tBOOL\tccsidInstalled;[^\n]*\n\tint\t\tccsidInsCheck;[^\n]*\n/\n/ });

    edit($f, \$t, 'GetTime', 1, sub { $t =~ s/MY_TIME_T GetTime\(\)\n\n\{\n.*?\n\}\n/MY_TIME_T GetTime()\n\n{\n\treturn (MY_TIME_T)rfh_hrtime_now();\n}\n/s });
    edit($f, \$t, 'DiffTime', 1, sub { $t =~ s/double DiffTime\(MY_TIME_T start, MY_TIME_T end\)\n\n\{\n.*?\n\}\n/double DiffTime(MY_TIME_T start, MY_TIME_T end)\n\n{\n\t\/\/ difference in the high frequency counter, scaled to microseconds\n\tint64_t diff = (int64_t)((end - start) * 1000000);\n\n\treturn (double)diff \/ (double)rfh_hrtime_frequency();\n}\n/s });

    edit($f, \$t, 'EbcdicCharToAsciiChar', 1, sub { $t =~ s/int EbcdicCharToAsciiChar\(int fromCcsid, int toCcsid, const char \* input, char \* output\)\n\n\{\n.*?\n\}\n/int EbcdicCharToAsciiChar(int fromCcsid, int toCcsid, const char * input, char * output)\n\n{\n\t\/\/ translate one character, returning the number of bytes produced\n\treturn rfh_convert_char(fromCcsid, toCcsid, input, output, 2);\n}\n/s });

    edit($f, \$t, 'translateEbcdicData locals', 1, sub { $t =~ s/\twchar_t\t\t\tucsChar\[4\];[^\n]*\n(\tunsigned char\ttempTxt\[8\];[^\n]*\n)\tchar\t\t\tdefChar='\.';[^\n]*\n/$1/ });
    edit($f, \$t, 'translateEbcdicData body', 1, sub { $t =~ s/\t\t\t\/\/ start by trying to do a proper translation, using Windows\n\t\t\ttcount = MultiByteToWideChar\(ebcdicCP, 0, \(char \*\)in \+ i, 1, ucsChar, 1\);\n\n\t\t\t\/\/ did the function succeed\n\t\t\tif \(tcount > 0\)\n\t\t\t\{\n\t\t\t\ttcount = WideCharToMultiByte\(asciiCcsid, 0, ucsChar, 1, \(char \*\)tempTxt, 2, &defChar, NULL\);\n\t\t\t\}\n/\t\t\t\/\/ start by trying to do a proper translation using the code page services\n\t\t\ttcount = rfh_convert_char(ebcdicCP, asciiCcsid, (const char *)in + i, (char *)tempTxt, 2);\n/ });

    edit($f, \$t, 'isCodePageInstalled', 1, sub { $t =~ s/BOOL CALLBACK EnumCodePagesProc\(LPTSTR lpCodePageString\)\n\n\{\n.*?\n\}\n\nBOOL isCodePageInstalled\(const int ccsid\)\n\n\{\n.*?\n\}\n/BOOL isCodePageInstalled(const int ccsid)\n\n{\n\treturn rfh_is_codepage_installed(ccsid);\n}\n/s });

    edit($f, \$t, 'processBackspace move', 1, sub { $t =~ s/\n(void processBackspace\(CWnd \*wnd\)\n\n\{\n.*?\n\}\n)\n/\n/s and do { $backspace = $1; 1 } });

    edit($f, \$t, 'roundLength2', 1, sub { $t =~ s/length = wcslen\(\(wchar_t \*\)ptr\);/length = (int)rfh_ucs2len((const RFH_UCS2 *)ptr);/ });
    edit($f, \$t, 'wcstombs', 1, sub { $t =~ s/charCount = wcstombs\(\(char \*\)mbsptr, \(const wchar_t \*\)ucsptr, mbslen\);/charCount = rfh_ucs2_to_c_locale((const RFH_UCS2 *)ucsptr, (char *)mbsptr, mbslen);/ });
    edit($f, \$t, 'mbstowcs', 1, sub { $t =~ s/charCount = mbstowcs\(\(wchar_t \*\)ucsptr, \(char \*\)mbsptr, ucslen\/2\);/charCount = rfh_c_locale_to_ucs2((char *)mbsptr, (RFH_UCS2 *)ucsptr, ucslen\/2);/ });
    edit($f, \$t, 'GetACP', 1, sub { $t =~ s/\bGetACP\(\)/rfh_get_acp()/g });
    edit($f, \$t, 'wchar_t', 5, sub { $t =~ s/\bwchar_t\b/RFH_UCS2/g });
    edit($f, \$t, '%I64d', 4, sub { $t =~ s/%I64d/%" PRId64 "/g });
    portTrace($f, \$t);
    portPointerTrace($f, \$t, 3);
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- uisubs (MFC only)
{
    # new files follow the repository's LF storage
    my $crlf = 0;
    save('RFHUtil/uisubs.h', "// uisubs.h - MFC-only helpers moved out of comsubs so comsubs stays portable.\n\n#pragma once\n\nvoid processBackspace(CWnd *wnd);\n", $crlf);
    save('RFHUtil/uisubs.cpp', "// uisubs.cpp - MFC-only helpers moved out of comsubs.cpp so comsubs stays portable.\n\n#include \"stdafx.h\"\n#include \"uisubs.h\"\n\n$backspace", $crlf);
    for my $f (qw(CICS.cpp General.cpp MQMDPAGE.cpp pscr.cpp Ims.cpp Dlq.cpp PubSub.cpp jms.cpp RFH.cpp PS.cpp)) {
        my $p = "RFHUtil/$f"; my ($t, $c) = load($p);
        includeAfter($p, \$t, 'comsubs.h', "#include \"uisubs.h\"\n");
        save($p, $t, $c);
    }
}

# ---------------------------------------------------------------- xmlsubs
{
    my $f = 'RFHUtil/xmlsubs.h'; my ($t, $crlf) = load($f);
    includeAtTop($f, \$t, "#include \"rfh_platform.h\"\n\n");
    edit($f, \$t, 'replaceChars decl', 1, sub { $t =~ s/void replaceChars\(const CString &value, char \*valStr\);/void replaceChars(const char * value, char *valStr);/ });
    edit($f, \$t, 'wchar_t', 2, sub { $t =~ s/\bwchar_t\b/RFH_UCS2/g });
    save($f, $t, $crlf);

    $f = 'RFHUtil/xmlsubs.cpp'; ($t, $crlf) = load($f);
    edit($f, \$t, 'replaceChars def', 1, sub { $t =~ s/void replaceChars\(const CString &value, char \*valStr\)\n\n\{\n\tint j = insertEscChars\(valStr, \(LPCTSTR\)value, value\.GetLength\(\)\);\n\}/void replaceChars(const char * value, char *valStr)\n\n{\n\tinsertEscChars(valStr, value, (int)strlen(value));\n}/ });
    edit($f, \$t, '_strupr', 3, sub { $t =~ s/\b_strupr\(/rfh_strupr(/g });
    edit($f, \$t, 'wchar_t', 2, sub { $t =~ s/\bwchar_t\b/RFH_UCS2/g });
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- JsonParse / XMLParse
for my $base (qw(JsonParse XMLParse)) {
    my $f = "RFHUtil/$base.h"; my ($t, $crlf) = load($f);
    includeAtTop($f, \$t, "#include <string>\n#include \"rfh_platform.h\"\n\n");
    edit($f, \$t, 'lastError member', 1, sub { $t =~ s/\tCString lastError;/\tstd::string lastError;/ });
    save($f, $t, $crlf);

    $f = "RFHUtil/$base.cpp"; ($t, $crlf) = load($f);
    includeAfter($f, \$t, 'rfhutil.h', "#include \"rfh_trace.h\"\n");
    edit($f, \$t, 'lastError length', 1, sub { $t =~ s/len = lastError\.GetLength\(\);/len = (int)lastError.size();/ });
    edit($f, \$t, 'lastError copy', 1, sub { $t =~ s/memcpy\(errtxt, \(LPCTSTR\)lastError, len\);/memcpy(errtxt, lastError.c_str(), len);/ });
    portTrace($f, \$t);
    portPointerTrace($f, \$t, 2);
    if ($base eq 'JsonParse') {
        # trace argument on the continuation line of a two-line sprintf
        edit($f, \$t, 'processArray trace cast', 1, sub { $t =~ s/^(\t+)\(unsigned int\)ptr, name, names\.getNameAddr/$1(unsigned int)(uintptr_t)ptr, name, names.getNameAddr/m });
    }
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- Names.cpp
{
    my $f = 'RFHUtil/Names.cpp'; my ($t, $crlf) = load($f);
    edit($f, \$t, 'windows.h', 1, sub { $t =~ s/#include <windows\.h>/#include "rfh_platform.h"/ });
    edit($f, \$t, 'dump file', 1, sub { $t =~ s/#define DUMP_FILE_NAME "c:\\\\namedump\.txt"/#define DUMP_FILE_NAME rfh_dump_path("namedump.txt")/ });
    edit($f, \$t, 'SYSTEMTIME', 1, sub { $t =~ s/\tSYSTEMTIME\tstime;/\tRfhUtcTime\tstime;/ });
    edit($f, \$t, 'GetSystemTime', 1, sub { $t =~ s/GetSystemTime\(&stime\);/stime = rfh_utc_now();/ });
    edit($f, \$t, 'time fields', 7, sub {
        my $n = 0;
        my %map = (wYear => 'year', wMonth => 'month', wDay => 'day', wHour => 'hour', wMinute => 'minute', wSecond => 'second', wMilliseconds => 'millisecond');
        $n += ($t =~ s/stime\.(wYear|wMonth|wDay|wHour|wMinute|wSecond|wMilliseconds)\b/'stime.' . $map{$1}/ge);
        $n;
    });
    portPointerTrace($f, \$t, 1);
    save($f, $t, $crlf);
}

print "shared sources ported\n";
