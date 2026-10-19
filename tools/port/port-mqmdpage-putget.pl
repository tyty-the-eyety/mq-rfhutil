#!/usr/bin/perl
# port-mqmdpage-putget.pl - one-off edits for Plan 3, Task 30: the MFC MQMD tab
# builds the MQMD of a put, reads the MQMD of a get and shows and edits the three
# ids through the portable core, so Windows and Linux share one implementation.
#
# - setMessageMQMD  -> rfh::mqmd_fields::toPutMqmd (Set Iden / Set All and the OS user
#                      name go in a PutContext);
# - updateMQMDafterPut -> rfh::mqmd_fields::applyAfterPut + setFields;
# - processMessageMQMD -> rfh::mqmd_fields::fromGetMqmd + setFields;
# - SetId (display) and setMsgId(int), setCorrelId(int), setGroupId() (edit) ->
#   rfh::id_display::toText / applyEdit;
# - the helpers only those functions used are removed (processReportOptions,
#   buildReportOptions, buildFlags, getPersistence, setMsgType, setPutApplType,
#   getPutApplType); processFlags stays for processMQMDE, setUserId and the
#   setMsgId/setCorrelId/setGroupId(MQMD2 *) copiers stay for DataArea;
# - core/message/rfh_id_display.{h,cpp} join the RFHUtil and Client projects.
#
# Run after port-mqmdpage.pl, from the repository root. Every edit asserts its match
# count, every file is checked before any is written, the result is checked for
# leftover references, and a second run is refused.
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

# Replaces the body of MQMDPAGE::name (signature kept) with new text.
sub body {
	my ($signature, $new) = @_;
	return sub {
		${$_[0]} =~ s/^(\Q$signature\E\n\n)\{\n.*?^\}\n/$1\{\n$new\}\n/msg;
	};
}

# Removes the definition of MQMDPAGE::name, the comment block just before it and the
# blank lines after it.
sub remove {
	my ($signature) = @_;
	return sub {
		${$_[0]} =~ s/^(?:\/{20,}\n(?:(?!\/{20,}\n)\/\/[^\n]*\n)*\/{20,}\n\n*)?\Q$signature\E\n\n\{\n.*?^\}\n\n*//msg;
	};
}

# a header line, removed
sub line {
	my ($re) = @_;
	return sub { ${$_[0]} =~ s/^$re[^\n]*\n//mg };
}

my @removed = qw(processReportOptions buildReportOptions buildFlags getPersistence setMsgType setPutApplType getPutApplType);

my $setMessageMQMD = <<'EOF';
	rfh::PutContext	context;

	// Set Iden Context and Set All Context, and whether the OS user name could be looked up
	context.setIdentity = (m_setUserID != FALSE);
	context.setAll = (m_set_all != FALSE);
	context.osUser = rfh_current_user();

	// the tab's fields, shared with the Linux version
	rfh::mqmd_fields::toPutMqmd(getFields(), context, *mqmd);
EOF

my $afterPut = <<'EOF';
	rfh::MqmdFields	fields = getFields();

	// Seq No, Offset, Msg Id and Group Id from the MQMD that MQPUT returned
	rfh::mqmd_fields::applyAfterPut(*mqmd, msgLen, fields);
	setFields(fields);
EOF

my $processMessage = <<'EOF';
	rfh::MqmdFields	fields;

	// get the form data into the instance variables
	UpdateData(TRUE);

	// clear any previous data
	clearMQMD();

	// the tab's fields, shared with the Linux version
	fields = getFields();
	rfh::mqmd_fields::fromGetMqmd(*mqmd, fields);
	setFields(fields);

	// initialize the id fields to overtype
	m_MsgIdEdit.SetOvertype(TRUE);
	m_CorrelIdEdit.SetOvertype(TRUE);
	m_GroupIdEdit.SetOvertype(TRUE);

	// update the form data from the instance variables
	// this prevents any old values from turning up
	UpdateData(FALSE);
EOF

my $setId = <<'EOF';
	CString		text;

	// the id as the box shows it in the current display mode
	text = rfh::id_display::toText(*id, (rfh::id_display::Mode)m_id_disp_ascii).c_str();

	switch (idType)
	{
	case ID_TYPE_MSG:
		{
			m_msg_id = text;
			break;
		}
	case ID_TYPE_CORREL:
		{
			m_mqmd_correl_id = text;
			break;
		}
	case ID_TYPE_GROUP:
		{
			m_mqmd_group_id = text;
			break;
		}
	}
EOF

sub edit_id {
	my ($field, $typed, $previous, $id, $mode) = @_;
	return <<"EOF";
	std::string	error;

	// the edited text of the box becomes the binary id; a rejected edit leaves it unchanged
	rfh::id_display::applyEdit(rfh::id_display::Field::$field, $id, (LPCTSTR)$typed, (LPCTSTR)$previous, (rfh::id_display::Mode)$mode, error);
EOF
}

my $cpp = port('RFHUtil/MQMDPAGE.cpp',
	['includes', 1, sub { ${$_[0]} =~ s/^(#include "mqsubs\.h"\n)/$1#include "rfh_id_display.h"\n#include "rfh_platform.h"\n/mg }],
	['setMessageMQMD', 1, body('void MQMDPAGE::setMessageMQMD(MQMD2 * mqmd, BOOL m_setUserID, BOOL m_set_all)', $setMessageMQMD)],
	['updateMQMDafterPut', 1, body('void MQMDPAGE::updateMQMDafterPut(MQMD2 * mqmd, MQLONG msgLen)', $afterPut)],
	['processMessageMQMD', 1, body('void MQMDPAGE::processMessageMQMD(MQMD2 * mqmd)', $processMessage)],
	['SetId', 1, body('void MQMDPAGE::SetId(MQBYTE24 * id, int idType)', $setId)],
	['setGroupId', 1, body('void MQMDPAGE::setGroupId()', edit_id('GroupId', 'm_mqmd_group_id', 'OrigGroupid', 'm_group_id', 'prev_disp_type'))],
	['setCorrelId', 1, body('void MQMDPAGE::setCorrelId(int dispType)', edit_id('CorrelId', 'm_mqmd_correl_id', 'OrigCorrelid', 'm_correlid', 'dispType'))],
	['setMsgId', 1, body('void MQMDPAGE::setMsgId(int dispType)', edit_id('MsgId', 'm_msg_id', 'OrigMsgid', 'm_message_id', 'dispType'))],
	['processReportOptions', 1, remove('void MQMDPAGE::processReportOptions(int report)')],
	['buildReportOptions', 1, remove('int MQMDPAGE::buildReportOptions()')],
	['buildFlags', 1, remove('int MQMDPAGE::buildFlags()')],
	['getPersistence', 1, remove('int MQMDPAGE::getPersistence()')],
	['setMsgType', 1, remove('void MQMDPAGE::setMsgType(int msgType)')],
	['setPutApplType', 1, remove('void MQMDPAGE::setPutApplType(int putType)')],
	['getPutApplType', 1, remove('int MQMDPAGE::getPutApplType(const char *applType)')],
);

port('RFHUtil/MQMDPAGE.h',
	map({ my $m = $_; ["declaration of $m", 1, line("\\t[A-Za-z][^\\n(]*\\b$m\\(")] } @removed),
);

# the projects: next to rfh_mqmd_fields, in the same form as its entries
for my $project (['RFHUtil/RFHUtil.vcxproj'], ['Client/Client.vcxproj']) {
	my ($path) = @$project;
	port($path,
		['source', 1, sub { ${$_[0]} =~ s/^(\s*)(<ClCompile Include="\.\.\\core\\message\\rfh_mqmd_fields\.cpp" \/>\n)/$1$2$1<ClCompile Include="..\\core\\message\\rfh_id_display.cpp" \/>\n/mg }],
		['header', 1, sub { ${$_[0]} =~ s/^(\s*)(<ClInclude Include="\.\.\\core\\message\\rfh_mqmd_fields\.h" \/>\n)/$1$2$1<ClInclude Include="..\\core\\message\\rfh_id_display.h" \/>\n/mg }],
	);
}

# nothing left may use what was removed
(my $code = $$cpp) =~ s{//[^\n]*}{}g;
for my $id (@removed) {
	my $n = () = $code =~ /\b\Q$id\E\b/g;
	die "RFHUtil/MQMDPAGE.cpp: $id is still used $n time(s)\n" if $n;
}

# braces still balance
my $open = () = $code =~ /\{/g;
my $close = () = $code =~ /\}/g;
die "RFHUtil/MQMDPAGE.cpp: $open open and $close close braces\n" unless $open == $close;

for my $p (@pending) {
	my ($path, $text, $crlf, $count) = @$p;
	$text =~ s/\n/\r\n/g if $crlf;
	my $tmp = "$path.port-tmp";
	open my $out, '>:raw', $tmp or die "$tmp: $!\n";
	print $out $text;
	close $out or die "$tmp: $!\n";
	rename $tmp, $path or die "$path: $!\n";
	printf "%-30s %2d edit(s)\n", $path, $count;
}
