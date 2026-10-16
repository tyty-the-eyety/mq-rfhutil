#!/usr/bin/perl
# port-windows-projects.pl - wires the portable core into the Windows build.
# Usage: perl port-windows-projects.pl <repo-root>
# Every edit asserts that it matched, so a drifted source tree fails loudly.
use strict;
use warnings;

my $root = shift or die "usage: $0 <repo-root>\n";
chdir $root or die "cannot chdir $root: $!\n";

sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "$f: $!\n"; local $/; my $t = <$h>; close $h; return $t; }
sub spew  { my ($f, $t) = @_; open(my $h, '>:raw', $f) or die "$f: $!\n"; print $h $t; close $h; }
sub load { my $t = slurp($_[0]); my $crlf = ($t =~ /\r\n/); $t =~ s/\r\n/\n/g; return ($t, $crlf); }
sub save { my ($f, $t, $crlf) = @_; $t =~ s/\n/\r\n/g if $crlf; spew($f, $t); }
sub edit {
    my ($file, $label, $expected, $count) = @_;
    $count = 0 + $count;   # s/// returns "" on no match
    die "$file: edit '$label' matched $count time(s), expected $expected\n" unless $count == $expected;
}

# ---------------------------------------------------------------- RFHUtil.vcxproj
{
    my $f = 'RFHUtil/RFHUtil.vcxproj'; my ($t, $crlf) = load($f);
    edit($f, 'include dirs', 4, scalar($t =~ s/<AdditionalIncludeDirectories>\$\(MQ_HOME\)\\tools\\c\\include;/<AdditionalIncludeDirectories>..\\core\\platform;\$(MQ_HOME)\\tools\\c\\include;/g));
    edit($f, 'sources', 1, scalar($t =~ s/^(\s*)<ClCompile Include="comsubs\.cpp" \/>\n/$1<ClCompile Include="comsubs.cpp" \/>\n$1<ClCompile Include="uisubs.cpp" \/>\n$1<ClCompile Include="..\\core\\platform\\rfh_platform.cpp" \/>\n$1<ClCompile Include="..\\core\\platform\\rfh_codepage.cpp" \/>\n$1<ClCompile Include="..\\core\\platform\\rfh_trace.cpp" \/>\n/m));
    edit($f, 'headers', 1, scalar($t =~ s/^(\s*)<ClInclude Include="comsubs\.h" \/>\n/$1<ClInclude Include="comsubs.h" \/>\n$1<ClInclude Include="uisubs.h" \/>\n$1<ClInclude Include="..\\core\\platform\\rfh_platform.h" \/>\n$1<ClInclude Include="..\\core\\platform\\rfh_codepage.h" \/>\n$1<ClInclude Include="..\\core\\platform\\rfh_trace.h" \/>\n/m));
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- RFHUtil.vcxproj.filters
{
    my $f = 'RFHUtil/RFHUtil.vcxproj.filters'; my ($t, $crlf) = load($f);
    my $entry = sub {
        my ($tag, $filter, $indent, @names) = @_;
        return join('', map { "$indent<$tag Include=\"$_\">\n$indent  <Filter>$filter</Filter>\n$indent</$tag>\n" } @names);
    };
    my @plat = ('..\core\platform\rfh_platform', '..\core\platform\rfh_codepage', '..\core\platform\rfh_trace');
    edit($f, 'filter headers', 1, scalar($t =~ s{^([ \t]*)(<ClInclude Include="comsubs\.h">\n.*?</ClInclude>\n)}{$1 . $2 . $entry->('ClInclude', 'Header Files', $1, 'uisubs.h', map { "$_.h" } @plat)}mse));
    edit($f, 'filter sources', 1, scalar($t =~ s{^([ \t]*)(<ClCompile Include="comsubs\.cpp">\n.*?</ClCompile>\n)}{$1 . $2 . $entry->('ClCompile', 'Source Files', $1, 'uisubs.cpp', map { "$_.cpp" } @plat)}mse));
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- Client.vcxproj (rfhutilc.exe)
{
    my $f = 'Client/Client.vcxproj'; my ($t, $crlf) = load($f);
    edit($f, 'include dirs', 6, scalar($t =~ s/<AdditionalIncludeDirectories>\$\(MQ_HOME\)\\tools\\c\\include<\/AdditionalIncludeDirectories>/<AdditionalIncludeDirectories>..\\core\\platform;\$(MQ_HOME)\\tools\\c\\include<\/AdditionalIncludeDirectories>/g));
    edit($f, 'sources', 1, scalar($t =~ s/^(\s*)<ClCompile Include="\.\.\\RFHUtil\\comsubs\.cpp" \/>\n/$1<ClCompile Include="..\\RFHUtil\\comsubs.cpp" \/>\n$1<ClCompile Include="..\\RFHUtil\\uisubs.cpp" \/>\n$1<ClCompile Include="..\\core\\platform\\rfh_platform.cpp" \/>\n$1<ClCompile Include="..\\core\\platform\\rfh_codepage.cpp" \/>\n$1<ClCompile Include="..\\core\\platform\\rfh_trace.cpp" \/>\n/m));
    edit($f, 'headers', 1, scalar($t =~ s/^(\s*)<ClInclude Include="\.\.\\RFHUtil\\comsubs\.h" \/>\n/$1<ClInclude Include="..\\RFHUtil\\comsubs.h" \/>\n$1<ClInclude Include="..\\RFHUtil\\uisubs.h" \/>\n$1<ClInclude Include="..\\core\\platform\\rfh_platform.h" \/>\n$1<ClInclude Include="..\\core\\platform\\rfh_codepage.h" \/>\n$1<ClInclude Include="..\\core\\platform\\rfh_trace.h" \/>\n/m));
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- Client.vcxproj.filters
{
    my $f = 'Client/Client.vcxproj.filters'; my ($t, $crlf) = load($f);
    my $entry = sub {
        my ($tag, $filter, $indent, @names) = @_;
        return join('', map { "$indent<$tag Include=\"$_\">\n$indent  <Filter>$filter</Filter>\n$indent</$tag>\n" } @names);
    };
    my @plat = ('..\core\platform\rfh_platform', '..\core\platform\rfh_codepage', '..\core\platform\rfh_trace');
    edit($f, 'filter headers', 1, scalar($t =~ s{^([ \t]*)(<ClInclude Include="\.\.\\RFHUtil\\comsubs\.h">\n.*?</ClInclude>\n)}{$1 . $2 . $entry->('ClInclude', 'Header Files', $1, '..\RFHUtil\uisubs.h', map { "$_.h" } @plat)}mse));
    edit($f, 'filter sources', 1, scalar($t =~ s{^([ \t]*)(<ClCompile Include="\.\.\\RFHUtil\\comsubs\.cpp">\n.*?</ClCompile>\n)}{$1 . $2 . $entry->('ClCompile', 'Source Files', $1, '..\RFHUtil\uisubs.cpp', map { "$_.cpp" } @plat)}mse));
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- UnitTests.vcxproj
{
    my $f = 'tests/UnitTests/UnitTests.vcxproj'; my ($t, $crlf) = load($f);
    edit($f, 'include dirs', 2, scalar($t =~ s/<AdditionalIncludeDirectories>\$\(ProjectDir\);\.\.\\\.\.\\RFHUtil;/<AdditionalIncludeDirectories>\$(ProjectDir);..\\..\\RFHUtil;..\\..\\core\\platform;/g));
    edit($f, 'core sources', 1, scalar($t =~ s/^(\s*)<ClCompile Include="\.\.\\\.\.\\RFHUtil\\mqerror\.cpp" \/>\n/$1<ClCompile Include="..\\..\\RFHUtil\\mqerror.cpp" \/>\n$1<ClCompile Include="..\\..\\core\\platform\\rfh_platform.cpp" \/>\n$1<ClCompile Include="..\\..\\core\\platform\\rfh_codepage.cpp" \/>\n$1<ClCompile Include="..\\..\\core\\platform\\rfh_trace.cpp" \/>\n/m));
    edit($f, 'platform tests', 1, scalar($t =~ s/^(\s*)<ClCompile Include="MQErrorTests\.cpp" \/>\n/$1<ClCompile Include="MQErrorTests.cpp" \/>\n$1<ClCompile Include="..\\core\\PlatformTests.cpp" \/>\n$1<ClCompile Include="..\\core\\SharedSourcesTests.cpp" \/>\n/m));
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- rfhutil.h / rfhutil.cpp
{
    my $f = 'RFHUtil/rfhutil.h'; my ($t, $crlf) = load($f);
    edit($f, 'include trace', 1, scalar($t =~ s/^(#include "DataArea\.h"[^\n]*\n)/$1#include "rfh_trace.h"\n/m));
    edit($f, 'base class', 1, scalar($t =~ s/^class CRfhutilApp : public CWinApp$/class CRfhutilApp : public CWinApp, public RfhTrace/m));
    edit($f, 'override dumpTraceData', 1, scalar($t =~ s/^(\tvoid dumpTraceData\(const char \* label, const unsigned char \* data, unsigned int length\))(;\n)/$1 override$2/m));
    edit($f, 'override isTraceEnabled', 1, scalar($t =~ s/^(\tBOOL isTraceEnabled\(\))(;\n)/$1 override$2/m));
    edit($f, 'override logTraceEntry', 1, scalar($t =~ s/^(\tvoid logTraceEntry\(LPCTSTR traceInfo\))(;\n)/$1 override$2/m));
    edit($f, 'verboseTrace member', 1, scalar($t =~ s/^\tBOOL verboseTrace;\n//m));
    save($f, $t, $crlf);

    $f = 'RFHUtil/rfhutil.cpp'; ($t, $crlf) = load($f);
    edit($f, 'install trace', 1, scalar($t =~ s/^(\tverboseTrace = FALSE;\n)/$1\n\t\/\/ the shared core sources trace through this application object\n\trfhSetTrace(this);\n/m));
    # compile-time guard: a member of CRfhutilApp's own would hide RfhTrace::verboseTrace
    my $guard = <<'GUARD';
#include <type_traits>

// CRfhutilApp must use the verboseTrace it inherits from RfhTrace: a member of its
// own would hide it, and the shared sources would read the always-FALSE base copy.
static_assert(std::is_same<decltype(&CRfhutilApp::verboseTrace), BOOL RfhTrace::*>::value,
              "CRfhutilApp must not shadow RfhTrace::verboseTrace");
GUARD
    # placed after the last #include of the top include block (the wincrypt pair)
    edit($f, 'verboseTrace guard', 1, scalar($t =~ s/^(#pragma comment\(lib, "Crypt32\.lib"\)[^\n]*\n)/$1\n$guard/m));
    # uninstall the sink and clear the closed trace file before members are destroyed
    edit($f, 'destructor', 1, scalar($t =~ s{^(CRfhutilApp::~CRfhutilApp\(\)\n\{\n)(.*?)(^\}\n)}{
        my ($open, $body, $close) = ($1, $2, $3);
        edit($f, 'uninstall trace', 1, scalar($body =~ s/^(\tchar\ttraceInfo\[128\];\n)/\t\/\/ stop the shared core sources tracing through this object\n\trfhSetTrace(NULL);\n\n$1/m));
        edit($f, 'clear traceFile', 1, scalar($body =~ s/^(\t\tfclose\(traceFile\);\n)/$1\t\ttraceFile = NULL;\n/m));
        $open . $body . $close }mse));
    save($f, $t, $crlf);
}

print "windows projects ported\n";
