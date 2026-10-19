#!/usr/bin/perl
# port-mqmdpage.pl - makes the MFC MQMD tab use the portable core for the file path:
# MQMDPAGE::buildMQMD and MQMDPAGE::processMQMD delegate to rfh::mqmd_fields
# through two adapters (getFields / setFields), and the RFHUtil and Client
# projects compile core/message/rfh_mqmd_fields.cpp.
# Run after port-mqsubs.pl.
# Usage: perl port-mqmdpage.pl <repo-root>
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

my %out;

# ---------------------------------------------------------------- MQMDPAGE.h
{
    my $f = 'RFHUtil/MQMDPAGE.h'; my ($t, $crlf) = load($f);
    edit($f, 'include', 1, scalar($t =~ s/^(#include "MyComboBox\.h"[^\n]*\n)/$1#include "rfh_mqmd_fields.h"\n/m));
    edit($f, 'adapters', 1, scalar($t =~ s/^(\tvoid buildMQMD\(MQMD2 \* mqmd, BOOL m_setUserID, BOOL m_set_all\);\n)/$1\trfh::MqmdFields getFields();\t\t\t\t\t\t\/\/ the tab's values for the portable core\n\tvoid setFields(const rfh::MqmdFields & fields);\t\/\/ shows values from the portable core\n/m));
    $out{$f} = [$t, $crlf];
}

# ---------------------------------------------------------------- MQMDPAGE.cpp
my $adapters = <<'EOF';
//////////////////////////////////////////////////////////////
//
// getFields / setFields - the MQMD tab's values as the portable
// core's rfh::MqmdFields, so that building and reading an MQMD
// share one implementation (core/message/rfh_mqmd_fields.cpp)
// with the Linux version.
//
//////////////////////////////////////////////////////////////

rfh::MqmdFields MQMDPAGE::getFields()

{
	rfh::MqmdFields	f;

	// the format combo box holds the current text
	m_mqmd_format_cb.GetWindowText(m_mqmd_format);

	f.format = (LPCTSTR)m_mqmd_format;
	f.ccsid = (LPCTSTR)m_mqmd_ccsid;
	f.msgType = (LPCTSTR)m_mqmd_msgtype;
	f.expiry = (LPCTSTR)m_mqmd_expiry;
	f.feedback = (LPCTSTR)m_mqmd_feedback;
	f.priority = (LPCTSTR)m_mqmd_priority;
	f.backoutCount = (LPCTSTR)m_mqmd_backout_count;
	f.replyToQ = (LPCTSTR)m_reply_queue;
	f.replyToQMgr = (LPCTSTR)m_reply_qm;
	f.userId = (LPCTSTR)m_mqmd_userid;
	f.accountingToken = (LPCTSTR)m_mqmd_account_token;
	f.applIdentity = (LPCTSTR)m_mqmd_appl_id;
	f.applOrigin = (LPCTSTR)m_mqmd_appl_origin;
	f.putApplName = (LPCTSTR)m_mqmd_appl_name;
	f.putApplType = (LPCTSTR)m_mqmd_appl_type;
	f.dateTime = (LPCTSTR)m_mqmd_date_time;
	f.seqNo = (LPCTSTR)m_mqmd_seq_no;
	f.offset = (LPCTSTR)m_mqmd_offset;
	f.originalLength = (LPCTSTR)m_mqmd_orig_len;

	f.persistence = m_mqmd_persist;
	f.encoding = m_mqmd_encoding;
	f.pdEncoding = m_mqmd_pd_encoding;
	f.floatEncoding = m_mqmd_float_encoding;
	f.reportException = m_report_except;
	f.reportExpiration = m_report_expire;
	f.reportCoa = m_report_coa;
	f.reportCod = m_report_cod;

	f.reportPan = (m_report_pan != FALSE);
	f.reportNan = (m_report_nan != FALSE);
	f.reportActivity = (m_report_activity != FALSE);
	f.reportDiscard = (m_report_discard != FALSE);
	f.reportPassMsgId = (m_report_pass_msgid != FALSE);
	f.reportPassCorrelId = (m_report_pass_correlid != FALSE);
	f.reportPassDiscardAndExpiry = (m_report_expire_discard != FALSE);
	f.groupYes = (m_mqmd_group_yes != FALSE);
	f.groupLast = (m_mqmd_group_last != FALSE);
	f.segmentYes = (m_mqmd_segment_yes != FALSE);
	f.segmentLast = (m_mqmd_segment_last != FALSE);
	f.segmentAllowed = (m_mqmd_segment_allowed != FALSE);

	memcpy(f.msgId, m_message_id, sizeof(f.msgId));
	memcpy(f.correlId, m_correlid, sizeof(f.correlId));
	memcpy(f.groupId, m_group_id, sizeof(f.groupId));

	return f;
}

void MQMDPAGE::setFields(const rfh::MqmdFields & f)

{
	m_mqmd_format = f.format.c_str();
	m_mqmd_format_cb.SetWindowText((LPCTSTR)m_mqmd_format);
	m_mqmd_ccsid = f.ccsid.c_str();
	m_mqmd_msgtype = f.msgType.c_str();
	m_mqmd_expiry = f.expiry.c_str();
	m_mqmd_feedback = f.feedback.c_str();
	m_mqmd_priority = f.priority.c_str();
	m_mqmd_backout_count = f.backoutCount.c_str();
	m_reply_queue = f.replyToQ.c_str();
	m_reply_qm = f.replyToQMgr.c_str();
	m_mqmd_userid = f.userId.c_str();
	m_mqmd_account_token = f.accountingToken.c_str();
	m_mqmd_appl_id = f.applIdentity.c_str();
	m_mqmd_appl_origin = f.applOrigin.c_str();
	m_mqmd_appl_name = f.putApplName.c_str();
	m_mqmd_appl_type = f.putApplType.c_str();
	m_mqmd_date_time = f.dateTime.c_str();
	m_mqmd_seq_no = f.seqNo.c_str();
	m_mqmd_offset = f.offset.c_str();
	m_mqmd_orig_len = f.originalLength.c_str();

	m_mqmd_persist = f.persistence;
	m_mqmd_encoding = f.encoding;
	m_mqmd_pd_encoding = f.pdEncoding;
	m_mqmd_float_encoding = f.floatEncoding;
	m_report_except = f.reportException;
	m_report_expire = f.reportExpiration;
	m_report_coa = f.reportCoa;
	m_report_cod = f.reportCod;

	m_report_pan = f.reportPan;
	m_report_nan = f.reportNan;
	m_report_activity = f.reportActivity;
	m_report_discard = f.reportDiscard;
	m_report_pass_msgid = f.reportPassMsgId;
	m_report_pass_correlid = f.reportPassCorrelId;
	m_report_expire_discard = f.reportPassDiscardAndExpiry;
	m_mqmd_group_yes = f.groupYes;
	m_mqmd_group_last = f.groupLast;
	m_mqmd_segment_yes = f.segmentYes;
	m_mqmd_segment_last = f.segmentLast;
	m_mqmd_segment_allowed = f.segmentAllowed;

	memcpy(m_message_id, f.msgId, sizeof(m_message_id));
	memcpy(m_correlid, f.correlId, sizeof(m_correlid));
	memcpy(m_group_id, f.groupId, sizeof(m_group_id));
	SetId(&m_message_id, ID_TYPE_MSG);
	SetId(&m_correlid, ID_TYPE_CORREL);
	SetId(&m_group_id, ID_TYPE_GROUP);
}

EOF

my $processMQMD = <<'EOF';
int MQMDPAGE::processMQMD(const unsigned char * data, int size)

{
	MQMD2			mqmd;
	rfh::MqmdFields	fields;

	// an MQMD needs at least sizeof(MQMD) bytes, even for version 1
	if (size < sizeof(MQMD))
	{
		return 0;
	}

	// normalise a big-endian or EBCDIC MQMD; MsgId, CorrelId and GroupId are byte
	// strings and are kept as they are
	memcpy(&mqmd, data, sizeof(mqmd));
	if (!translateMQMD(&mqmd) || ((MQMD_VERSION_1 != mqmd.Version) && (MQMD_VERSION_2 != mqmd.Version)))
	{
		return 0;
	}

	if (pDoc->traceEnabled)
	{
		pDoc->dumpTraceData("mqmd data", data, sizeof(MQMD));
	}

	// the tab's fields, shared with the Linux version
	clearMQMD();
	fields = getFields();
	rfh::mqmd_fields::fromMqmd(mqmd, fields);
	setFields(fields);

	// display state for the data that follows
	pDoc->m_numeric_format = getIntEncode(mqmd.Encoding);
	pDoc->fileIntFormat = pDoc->m_numeric_format;
	pDoc->m_pd_numeric_format = m_mqmd_pd_encoding;
	pDoc->m_char_format = getCcsidType(mqmd.CodedCharSetId);

	UpdateData(FALSE);

	return (MQMD_VERSION_1 == mqmd.Version) ? sizeof(MQMD1) : sizeof(MQMD2);
}
EOF

my $buildMQMD = <<'EOF';
void MQMDPAGE::buildMQMD(MQMD2 *mqmd, BOOL m_setUserID, BOOL m_set_all)

{
	// the tab's fields, shared with the Linux version
	rfh::mqmd_fields::toMqmd(getFields(), *mqmd);
}
EOF

{
    my $f = 'RFHUtil/MQMDPAGE.cpp'; my ($t, $crlf) = load($f);
    edit($f, 'replace processMQMD', 1, scalar($t =~ s/^int MQMDPAGE::processMQMD\(const unsigned char \* data, int size\)\n.*?^\}\n/$adapters$processMQMD/ms));
    edit($f, 'replace buildMQMD', 1, scalar($t =~ s/^void MQMDPAGE::buildMQMD\(MQMD2 \*mqmd, BOOL m_setUserID, BOOL m_set_all\)\n.*?^\}\n/$buildMQMD/ms));
    $out{$f} = [$t, $crlf];
}

# ---------------------------------------------------------------- projects
{
    my $f = 'RFHUtil/RFHUtil.vcxproj'; my ($t, $crlf) = load($f);
    edit($f, 'include dirs', 4, scalar($t =~ s/<AdditionalIncludeDirectories>\.\.\\core\\platform;/<AdditionalIncludeDirectories>..\\core\\platform;..\\core\\message;.;/g));
    edit($f, 'source', 1, scalar($t =~ s/^(\s*)(<ClCompile Include="\.\.\\core\\platform\\rfh_trace\.cpp" \/>\n)/$1$2$1<ClCompile Include="..\\core\\message\\rfh_mqmd_fields.cpp" \/>\n/m));
    edit($f, 'header', 1, scalar($t =~ s/^(\s*)(<ClInclude Include="\.\.\\core\\platform\\rfh_trace\.h" \/>\n)/$1$2$1<ClInclude Include="..\\core\\message\\rfh_mqmd_fields.h" \/>\n$1<ClInclude Include="rfhconst.h" \/>\n/m));
    $out{$f} = [$t, $crlf];

    $f = 'Client/Client.vcxproj'; ($t, $crlf) = load($f);
    edit($f, 'include dirs', 6, scalar($t =~ s/<AdditionalIncludeDirectories>\.\.\\core\\platform;/<AdditionalIncludeDirectories>..\\core\\platform;..\\core\\message;..\\RFHUtil;/g));
    edit($f, 'source', 1, scalar($t =~ s/^(\s*)(<ClCompile Include="\.\.\\core\\platform\\rfh_trace\.cpp" \/>\n)/$1$2$1<ClCompile Include="..\\core\\message\\rfh_mqmd_fields.cpp" \/>\n/m));
    edit($f, 'header', 1, scalar($t =~ s/^(\s*)(<ClInclude Include="\.\.\\core\\platform\\rfh_trace\.h" \/>\n)/$1$2$1<ClInclude Include="..\\core\\message\\rfh_mqmd_fields.h" \/>\n$1<ClInclude Include="..\\RFHUtil\\rfhconst.h" \/>\n/m));
    $out{$f} = [$t, $crlf];
}

for my $f (sort keys %out)
{
    my ($t, $crlf) = @{$out{$f}};
    $t =~ s/\n/\r\n/g if $crlf;
    spew($f, $t);
}

print "MQMD page delegates to the core\n";
