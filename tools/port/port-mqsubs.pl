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
# The display, encoding, MQMD option and code page default constants move
# verbatim from the MFC-only rfhutil.h into rfhconst.h, which rfhutil.h now
# includes. Each edit counts its matches first and asserts the expected count.
{
    my $f = 'RFHUtil/rfhutil.h'; my ($t, $crlf) = load($f);
    my ($block, $defaults);

    my $re_block = qr/^(\/\/ Display format types\n.*?#define SEGMENT_LAST\t2\n)/ms;
    my $n = () = $t =~ /$re_block/g;
    edit($f, 'constants block', 1, $n);
    $t =~ s/$re_block/#include "rfhconst.h"\n/ and $block = $1;

    my $re_defaults = qr/^(\/\/ code page defaults\n.*?#define DEF_UNIX_ENCODING\t\t\t273\n)\n/ms;
    $n = () = $t =~ /$re_defaults/g;
    edit($f, 'defaults block', 1, $n);
    $t =~ s/$re_defaults// and $defaults = $1;

    $block .= "\n" . $defaults;
    $out{$f} = [$t, $crlf];

    my ($lt) = load('RFHUtil/mqsubs.cpp');
    my ($licence) = $lt =~ /\A(\/\*.*?\*\/\n)/s;
    die "RFHUtil/mqsubs.cpp: licence block not found\n" unless $licence;
    $out{'RFHUtil/rfhconst.h'} = [$licence
        . "\n// rfhconst.h - display, encoding, MQMD option and code page default constants,\n"
        . "// moved from rfhutil.h so that portable sources can use them without MFC.\n\n"
        . "#pragma once\n\n"
        . $block, $crlf];
}

# ---------------------------------------------------------------- mqsubs.h / mqsubs.cpp
{
    # findMQInstallDirectory reads the Windows registry and has no callers: DataArea
    # uses its own member function of the same name.
    my $f = 'RFHUtil/mqsubs.h'; my ($t, $crlf) = load($f);
    my ($re, $n);

    $re = qr/^void findMQInstallDirectory\(char \*dirName, int dirNameLen\);\n/m;
    $n = () = $t =~ /$re/g;
    edit($f, 'remove findMQInstallDirectory decl', 1, $n);
    $t =~ s/$re//;

    # self-contained: one include guard, and the declarations use BOOL
    $re = qr/^(#include "cmqc\.h"\n)/m;
    $n = () = $t =~ /$re/g;
    edit($f, 'include rfh_platform.h', 1, $n);
    $t =~ s/$re/#pragma once\n#include "rfh_platform.h"\n$1/;
    $out{$f} = [$t, $crlf];

    $f = 'RFHUtil/mqsubs.cpp'; ($t, $crlf) = load($f);

    $re = qr/^(#include "rfhutil\.h"\n)/m;
    $n = () = $t =~ /$re/g;
    edit($f, 'include rfhconst.h', 1, $n);
    $t =~ s/$re/$1#include "rfhconst.h"\t\/\/ direct: rfhutil.h is blocked in the portable build\n/;

    $re = qr/^#define MQINSKEY [^\n]*\n\n?/m;
    $n = () = $t =~ /$re/g;
    edit($f, 'remove MQINSKEY', 1, $n);
    $t =~ s/$re//;

    $re = qr/\n\/\*{20,}\/\n(?:\/\*[^\n]*\*\/\n){4}\/\*{20,}\/\n\nvoid findMQInstallDirectory\(char \*dirName, int dirNameLen\)\n.*?\n\}\n/s;
    $n = () = $t =~ /$re/g;
    edit($f, 'remove findMQInstallDirectory', 1, $n);
    $t =~ s/$re//;
    $out{$f} = [$t, $crlf];
}

for my $f (sort keys %out)
{
    my ($t, $crlf) = @{$out{$f}};
    $t =~ s/\n/\r\n/g if $crlf;
    spew($f, $t);
}

print "mqsubs ported\n";
