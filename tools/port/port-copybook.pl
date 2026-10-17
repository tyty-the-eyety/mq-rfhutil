#!/usr/bin/perl
# port-copybook.pl - one-off edits for Plan 2B that make IBM's COBOL copy book parser
# (RFHUtil/Copybook.{h,cpp}) a shared source, as port-shared-sources.pl did for
# the Plan 1 sources. Every edit asserts its match count; every file is checked
# before any is written. Run once from the repository root.
use strict; use warnings;

my @pending;
sub port {
	my ($path, @edits) = @_;
	open my $in, '<:raw', $path or die "$path: $!";
	my $text = do { local $/; <$in> }; close $in;
	my $crlf = ($text =~ /\r\n/);
	$text =~ s/\r\n/\n/g;
	for my $e (@edits) {
		my ($what, $want, $code) = @$e;
		my $n = 0 + ($code->(\$text) || 0);
		die "$path: $what: matched $n time(s), expected $want\n" unless $n == $want;
	}
	$text =~ s/\n/\r\n/g if $crlf;
	push @pending, [$path, $text, scalar @edits];
}

port('RFHUtil/Copybook.h',
	['no MFC base class', 1, sub { ${$_[0]} =~ s/^class CCopybook : public CObject  \n/class CCopybook\n/m }],
	['member declaration', 1, sub { ${$_[0]} =~ s/\tint CCopybook::displayDat\(/\tint displayDat(/ }],
);

port('RFHUtil/Copybook.cpp',
	['include case', 1, sub { ${$_[0]} =~ s/^#include "names\.h"\n/#include "Names.h"\n/m }],
	['portable includes', 1, sub { ${$_[0]} =~ s/^(#include "rfhutil\.h"\n)/$1#include "rfhconst.h"\t\/\/ direct: rfhutil.h is blocked in the portable build\n#include "rfh_codepage.h"\n#include "rfh_trace.h"\n/m }],
	['trace pointer type', 1, sub { ${$_[0]} =~ s/CRfhutilApp\t\*app=\(CRfhutilApp \*\)AfxGetApp\(\);/RfhTrace\t*app=rfhGetTrace();/g }],
	['trace lookup', 1, sub { ${$_[0]} =~ s/\(\(CRfhutilApp \*\)AfxGetApp\(\)\)->logTraceEntry\(traceInfo\);/rfhGetTrace()->logTraceEntry(traceInfo);/g }],
	['pointer casts in trace', 2, sub { ${$_[0]} =~ s/\(unsigned int\)(dataIn|line)\b/(unsigned int)(uintptr_t)$1/g }],
	['multibyte character length', 1, sub { ${$_[0]} =~ s/\b_mbclen\(/rfh_mbclen(/g }],
);

for my $p (@pending) {
	my ($path, $text, $count) = @$p;
	open my $out, '>:raw', $path or die "$path: $!";
	print $out $text; close $out;
	printf "%-22s %d edit(s)\n", $path, $count;
}
