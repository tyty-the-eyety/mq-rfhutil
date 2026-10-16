#!/usr/bin/perl
# port-mqsubs.pl - makes RFHUtil/mqsubs.cpp portable and gives the display and
# encoding constants a home that does not depend on MFC.
# Run after port-shared-sources.pl and port-windows-projects.pl.
# Usage: perl port-mqsubs.pl <repo-root>
# Every edit asserts that it matched, and nothing is written unless all edits
# matched, so a drifted source tree fails loudly and stays untouched.
use strict;
use warnings;

my $root = shift or die "usage: $0 <repo-root>\n";
chdir $root or die "cannot chdir $root: $!\n";

sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "$f: $!\n"; local $/; my $t = <$h>; close $h; return $t; }
sub spew  { my ($f, $t) = @_; open(my $h, '>:raw', $f) or die "$f: $!\n"; print $h $t; close $h; }
sub load { my $t = slurp($_[0]); my $crlf = ($t =~ /\r\n/); $t =~ s/\r\n/\n/g; return ($t, $crlf); }
sub edit {
    my ($file, $label, $expected, $count) = @_;
    $count = 0 + ($count || 0);
    die "$file: edit '$label' matched $count time(s), expected $expected\n" unless $count == $expected;
}

my %out;	# path => [text, crlf]; written only after every edit succeeded

# ---------------------------------------------------------------- rfhutil.h -> rfhconst.h
# The display, character, numeric, persistence and report constants move verbatim
# from the MFC-only rfhutil.h into rfhconst.h, which rfhutil.h now includes.
{
    my $f = 'RFHUtil/rfhutil.h'; my ($t, $crlf) = load($f);
    my ($block, $defaults);
    edit($f, 'constants block', 1, scalar($t =~ s/^(\/\/ Display format types\n.*?#define SEGMENT_LAST\t2\n)/#include "rfhconst.h"\n/ms and do { $block = $1; 1 }));
    edit($f, 'defaults block', 1, scalar($t =~ s/^(\/\/ code page defaults\n.*?#define DEF_UNIX_ENCODING\t\t\t273\n)\n//ms and do { $defaults = $1; 1 }));
    $block .= "\n" . $defaults;
    $out{$f} = [$t, $crlf];

    my ($licence) = slurp('RFHUtil/mqsubs.cpp') =~ /\A(\/\*.*?\*\/\n)/s;
    die "RFHUtil/mqsubs.cpp: licence block not found\n" unless $licence;
    $out{'RFHUtil/rfhconst.h'} = [$licence
        . "\n// rfhconst.h - display, character, numeric, persistence and report constants,\n"
        . "// moved from rfhutil.h so that portable sources can use them without MFC.\n\n"
        . "#pragma once\n\n"
        . $block, 0];
}

# ---------------------------------------------------------------- mqsubs.h / mqsubs.cpp
{
    # findMQInstallDirectory reads the Windows registry and has no callers: DataArea
    # uses its own member function of the same name.
    my $f = 'RFHUtil/mqsubs.h'; my ($t, $crlf) = load($f);
    edit($f, 'remove findMQInstallDirectory decl', 1, scalar($t =~ s/^void findMQInstallDirectory\(char \*dirName, int dirNameLen\);\n//m));
    # self-contained: the declarations use BOOL
    edit($f, 'include rfh_platform.h', 1, scalar($t =~ s/^(#include "cmqc\.h"\n)/#include "rfh_platform.h"\n$1/m));
    $out{$f} = [$t, $crlf];

    $f = 'RFHUtil/mqsubs.cpp'; ($t, $crlf) = load($f);
    edit($f, 'include rfhconst.h', 1, scalar($t =~ s/^(#include "rfhutil\.h"\n)/$1#include "rfhconst.h"\n/m));
    edit($f, 'remove MQINSKEY', 1, scalar($t =~ s/^#define MQINSKEY [^\n]*\n//m));
    edit($f, 'remove findMQInstallDirectory', 1, scalar($t =~ s/\n\/\*{20,}\/\n(?:\/\*[^\n]*\*\/\n){4}\/\*{20,}\/\n\nvoid findMQInstallDirectory\(char \*dirName, int dirNameLen\)\n.*?\n\}\n//s));
    $out{$f} = [$t, $crlf];
}

for my $f (sort keys %out)
{
    my ($t, $crlf) = @{$out{$f}};
    $t =~ s/\n/\r\n/g if $crlf;
    spew($f, $t);
}

print "mqsubs ported\n";
