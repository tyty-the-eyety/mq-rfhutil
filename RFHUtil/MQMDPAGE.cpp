/*
Copyright (c) IBM Corporation 2000, 2018
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Contributors:
Jim MacNair - Initial Contribution
*/

// MQMDPAGE.cpp : implementation file
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "rfhutil.h"
#include "MQMDPAGE.h"
#include "comsubs.h"
#include "uisubs.h"
#include "mqsubs.h"
#include "rfh_id_display.h"
#include "rfh_platform.h"
#include "windows.h"
#include "cics.h"
#include "ThemeManager.h"
#define SECURITY_WIN32
#include <Security.h>  // For GetUserNameEx

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// this is the user-defined message
#define WM_SETPAGEFOCUS WM_APP+4

// WHICH ID should be set by the SetID function
#define ID_TYPE_MSG		0
#define ID_TYPE_CORREL	1
#define ID_TYPE_GROUP	2

/////////////////////////////////////////////////////////////////////////////
// MQMD property page

IMPLEMENT_DYNCREATE(MQMDPAGE, CPropertyPage)

MQMDPAGE::MQMDPAGE() : CPropertyPage(MQMDPAGE::IDD)

{
	//{{AFX_DATA_INIT(MQMDPAGE)
	m_mqmd_correl_id = _T("");
	m_mqmd_ccsid = _T("");
	m_mqmd_format = _T("");
	m_msg_id = _T("");
	m_mqmd_persist = 0;
	m_reply_qm = _T("");
	m_reply_queue = _T("");
	m_mqmd_appl_id = _T("");
	m_mqmd_appl_origin = _T("");
	m_mqmd_appl_name = _T("");
	m_mqmd_appl_type = _T("");
	m_mqmd_encoding = 0;
	m_mqmd_float_encoding = 0;
	m_mqmd_userid = _T("");
	m_mqmd_group_id = _T("");
	m_mqmd_backout_count = _T("");
	m_id_disp_ascii = ID_DISPLAY_HEX;
	m_mqmd_seq_no = _T("");
	m_mqmd_account_token = _T("");
	m_mqmd_date_time = _T("");
	m_mqmd_expiry = _T("");
	m_mqmd_msgtype = _T("");
	m_mqmd_pd_encoding = 0;
	m_report_coa = -1;
	m_report_cod = -1;
	m_report_except = -1;
	m_report_expire = -1;
	m_mqmd_segment_last = FALSE;
	m_mqmd_segment_yes = FALSE;
	m_mqmd_offset = _T("0");
	m_mqmd_orig_len = _T("");
	m_mqmd_feedback = _T("");
	m_mqmd_priority = _T("");
	m_mqmd_group_last = FALSE;
	m_mqmd_group_yes = FALSE;
	m_mqmd_segment_allowed = FALSE;
	m_report_nan = FALSE;
	m_report_pan = FALSE;
	m_report_activity = FALSE;
	m_report_discard = FALSE;
	m_report_pass_msgid = FALSE;
	m_report_pass_correlid = FALSE;
	m_report_expire_discard = FALSE;
	prev_disp_type = ID_DISPLAY_HEX;
	m_mqmd_backout_count = "0";;
	m_report_expire_discard = FALSE;
	m_mqmd_float_encoding = -1;
	//}}AFX_DATA_INIT

	msgTypeInit = 0;
	prev_disp_type = ID_DISPLAY_HEX;
	putApplTypeInit = TRUE;

	// clear the id fields
	memset(m_message_id, 0, sizeof(m_message_id));
	memset(m_correlid, 0, sizeof(m_correlid));
	memset(m_group_id, 0, sizeof(m_group_id));
	memset(m_saved_msg_id, 0, sizeof(m_saved_msg_id));
	memset(m_saved_correlid, 0, sizeof(m_saved_correlid));
	memset(m_saved_group_id, 0, sizeof(m_saved_group_id));
}

MQMDPAGE::~MQMDPAGE()
{
}

void MQMDPAGE::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(MQMDPAGE)
	DDX_Text(pDX, IDC_CORREL_ID, m_mqmd_correl_id);
	DDV_MaxChars(pDX, m_mqmd_correl_id, 48);
	DDX_Text(pDX, IDC_MQMD_CCSID, m_mqmd_ccsid);
	DDV_MaxChars(pDX, m_mqmd_ccsid, 5);
	DDX_Text(pDX, IDC_MQMD_FORMAT, m_mqmd_format);
	DDV_MaxChars(pDX, m_mqmd_format, 8);
	DDX_Text(pDX, IDC_MSG_ID, m_msg_id);
	DDV_MaxChars(pDX, m_msg_id, 48);
	DDX_Radio(pDX, IDC_PERSIST_NO, m_mqmd_persist);
	DDX_Text(pDX, IDC_REPLY_QM, m_reply_qm);
	DDV_MaxChars(pDX, m_reply_qm, 48);
	DDX_Text(pDX, IDC_REPLY_QUEUE, m_reply_queue);
	DDV_MaxChars(pDX, m_reply_queue, 48);
	DDX_Text(pDX, IDC_APPL_IDENTITY, m_mqmd_appl_id);
	DDV_MaxChars(pDX, m_mqmd_appl_id, 32);
	DDX_Text(pDX, IDC_APPL_ORIGIN, m_mqmd_appl_origin);
	DDV_MaxChars(pDX, m_mqmd_appl_origin, 4);
	DDX_Text(pDX, IDC_APPL_NAME, m_mqmd_appl_name);
	DDV_MaxChars(pDX, m_mqmd_appl_name, 28);
	DDX_Text(pDX, IDC_APPL_TYPE, m_mqmd_appl_type);
	DDX_Radio(pDX, IDC_MQMD_PC, m_mqmd_encoding);
	DDX_Text(pDX, IDC_USER_ID, m_mqmd_userid);
	DDV_MaxChars(pDX, m_mqmd_userid, 12);
	DDX_Text(pDX, IDC_GROUP_ID, m_mqmd_group_id);
	DDV_MaxChars(pDX, m_mqmd_group_id, 48);
	DDX_Text(pDX, IDC_BACKOUT_CNT, m_mqmd_backout_count);
	DDX_Radio(pDX, IDC_ID_DISP_ASCII, m_id_disp_ascii);
	DDX_Text(pDX, IDC_SEQNO, m_mqmd_seq_no);
	DDX_Text(pDX, IDC_ACCOUNT_TOKEN, m_mqmd_account_token);
	DDV_MaxChars(pDX, m_mqmd_account_token, 64);
	DDX_Text(pDX, IDC_PUT_DATE_TIME, m_mqmd_date_time);
	DDX_Text(pDX, IDC_EXPIRY, m_mqmd_expiry);
	DDV_MaxChars(pDX, m_mqmd_expiry, 9);
	DDX_CBString(pDX, IDC_MSGTYPE, m_mqmd_msgtype);
	DDV_MaxChars(pDX, m_mqmd_msgtype, 10);
	DDX_Radio(pDX, IDC_MQMD_PD_PC, m_mqmd_pd_encoding);
	DDX_Radio(pDX, IDC_REPORT_COA_NONE, m_report_coa);
	DDX_Radio(pDX, IDC_REPORT_COD_NONE, m_report_cod);
	DDX_Radio(pDX, IDC_REPORT_EXCEPT_NONE, m_report_except);
	DDX_Radio(pDX, IDC_REPORT_EXPIRE_NONE, m_report_expire);
	DDX_Check(pDX, IDC_SEGMENT_LAST, m_mqmd_segment_last);
	DDX_Check(pDX, IDC_SEGMENT_YES, m_mqmd_segment_yes);
	DDX_Text(pDX, IDC_OFFSET, m_mqmd_offset);
	DDX_Text(pDX, IDC_MQMD_ORIG_LEN, m_mqmd_orig_len);
	DDV_MaxChars(pDX, m_mqmd_orig_len, 9);
	DDX_Text(pDX, IDC_MQMD_FEEDBACK, m_mqmd_feedback);
	DDV_MaxChars(pDX, m_mqmd_feedback, 8);
	DDX_Text(pDX, IDC_MQMD_PRIOTITY, m_mqmd_priority);
	DDV_MaxChars(pDX, m_mqmd_priority, 8);
	DDX_Check(pDX, IDC_GROUP_LAST, m_mqmd_group_last);
	DDX_Check(pDX, IDC_GROUP_YES, m_mqmd_group_yes);
	DDX_Check(pDX, IDC_SEGMENT_ALLOWED, m_mqmd_segment_allowed);
	DDX_Check(pDX, IDC_REPORT_NAN, m_report_nan);
	DDX_Check(pDX, IDC_REPORT_PAN, m_report_pan);
	DDX_Check(pDX, IDC_REPORT_ACTIVITY, m_report_activity);
	DDX_Check(pDX, IDC_REPORT_DISCARD, m_report_discard);
	DDX_Check(pDX, IDC_REPORT_PASS_MSGID, m_report_pass_msgid);
	DDX_Check(pDX, IDC_REPORT_PASS_CORRELID, m_report_pass_correlid);
	DDX_Check(pDX, IDC_REPORT_EXPIRY_DISCARD, m_report_expire_discard);
	DDX_Radio(pDX, IDC_MQMD_FLOAT_PC, m_mqmd_float_encoding);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(MQMDPAGE, CPropertyPage)
	//{{AFX_MSG_MAP(MQMDPAGE)
	ON_WM_CTLCOLOR()
	ON_WM_ERASEBKGND()
	ON_WM_DRAWITEM()
	ON_BN_CLICKED(IDC_COPY_MSGID, OnCopyMsgid)
	ON_BN_CLICKED(IDC_RESET_IDS, OnResetIds)
	ON_BN_CLICKED(IDC_ID_DISP_ASCII, OnIdDispAscii)
	ON_BN_CLICKED(IDC_ID_DISP_EBCDIC, OnIdDispEbcdic)
	ON_BN_CLICKED(IDC_ID_DISP_HEX, OnHex)
	ON_EN_UPDATE(IDC_GROUP_ID, OnUpdateGroupId)
	ON_EN_UPDATE(IDC_CORREL_ID, OnUpdateCorrelId)
	ON_CBN_SETFOCUS(IDC_MSGTYPE, OnSetfocusMsgtype)
	ON_CBN_SELCHANGE(IDC_MSGTYPE, OnSelchangeMsgtype)
	ON_CBN_KILLFOCUS(IDC_MSGTYPE, OnKillfocusMsgtype)
	ON_EN_UPDATE(IDC_MQMD_CCSID, OnUpdateMqmdCcsid)
	ON_BN_CLICKED(IDC_MQMD_PC, OnMqmdPc)
	ON_BN_CLICKED(IDC_MQMD_HOST, OnMqmdHost)
	ON_BN_CLICKED(IDC_MQMD_UNIX, OnMqmdUnix)
	ON_CBN_SELCHANGE(IDC_APPL_TYPE, OnSelchangeApplType)
	ON_CBN_SETFOCUS(IDC_APPL_TYPE, OnSetfocusApplType)
	ON_EN_SETFOCUS(IDC_CORREL_ID, OnSetfocusCorrelId)
	ON_EN_KILLFOCUS(IDC_CORREL_ID, OnKillfocusCorrelId)
	ON_EN_CHANGE(IDC_CORREL_ID, OnChangeCorrelId)
	ON_EN_SETFOCUS(IDC_GROUP_ID, OnSetfocusGroupId)
	ON_EN_KILLFOCUS(IDC_GROUP_ID, OnKillfocusGroupId)
	ON_EN_SETFOCUS(IDC_MSG_ID, OnSetfocusMsgId)
	ON_EN_KILLFOCUS(IDC_MSG_ID, OnKillfocusMsgId)
	//}}AFX_MSG_MAP
	ON_MESSAGE(WM_SETPAGEFOCUS, OnSetPageFocus)
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, MQMDPAGE::GetToolTipText)
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, MQMDPAGE::GetToolTipText)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// MQMDPAGE message handlers

BOOL MQMDPAGE::OnInitDialog() 

{
	CPropertyPage::OnInitDialog();
	
	// enable tooltips for this property page
	EnableToolTips(TRUE);

	// use the special CIdEdit subclass for the correlation id and group id edit boxes
	m_MsgIdEdit.SubclassDlgItem(IDC_MSG_ID, this);
	m_CorrelIdEdit.SubclassDlgItem(IDC_CORREL_ID, this);
	m_GroupIdEdit.SubclassDlgItem(IDC_GROUP_ID, this);

	// initialize the id fields to overtype
	m_MsgIdEdit.SetOvertype(TRUE);
	m_CorrelIdEdit.SetOvertype(TRUE);
	m_GroupIdEdit.SetOvertype(TRUE);

	// use the autocomplete version of a combo box
	m_mqmd_format_cb.SubclassDlgItem(IDC_MQMD_FORMAT, this);

	// load the drop down list for the format field with the standard MQ formats
	m_mqmd_format_cb.AddString(MQFMT_STRING);
	m_mqmd_format_cb.AddString(MQFMT_DEAD_LETTER_HEADER);
	m_mqmd_format_cb.AddString(MQFMT_CICS);
	m_mqmd_format_cb.AddString(MQFMT_IMS);
	m_mqmd_format_cb.AddString(MQFMT_IMS_VAR_STRING);
	m_mqmd_format_cb.AddString(MQFMT_PCF);
	m_mqmd_format_cb.AddString(MQFMT_ADMIN);
	m_mqmd_format_cb.AddString(MQFMT_COMMAND_1);
	m_mqmd_format_cb.AddString(MQFMT_COMMAND_2);
	m_mqmd_format_cb.AddString(MQFMT_DIST_HEADER);
	m_mqmd_format_cb.AddString(MQFMT_EMBEDDED_PCF);
	m_mqmd_format_cb.AddString(MQFMT_EVENT);
	m_mqmd_format_cb.AddString(MQFMT_REF_MSG_HEADER);
	m_mqmd_format_cb.AddString(MQFMT_RF_HEADER_1);
	m_mqmd_format_cb.AddString(MQFMT_RF_HEADER_2);
	m_mqmd_format_cb.AddString(MQFMT_TRIGGER);
	m_mqmd_format_cb.AddString(MQFMT_WORK_INFO_HEADER);
	m_mqmd_format_cb.AddString(MQFMT_XMIT_Q_HEADER);
	m_mqmd_format_cb.AddString(MQFMT_CHANNEL_COMPLETED);

	// Apply theme to dialog
	ThemeManager::GetInstance().ApplyThemeToDialog(this);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

HBRUSH MQMDPAGE::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	ThemeManager& theme = ThemeManager::GetInstance();
	
	if (theme.IsDarkMode()) {
		pDC->SetTextColor(theme.GetTextColor());
		
		switch (nCtlColor) {
			case CTLCOLOR_EDIT:
			case CTLCOLOR_LISTBOX:
				// Dark grey background for edit controls and combo boxes
				pDC->SetBkColor(theme.GetControlBackgroundColor());
				return (HBRUSH)theme.GetControlBackgroundBrush()->GetSafeHandle();
				
			case CTLCOLOR_STATIC:
				// Dialog background for static text
				pDC->SetBkColor(theme.GetBackgroundColor());
				return (HBRUSH)theme.GetBackgroundBrush()->GetSafeHandle();
				
			case CTLCOLOR_BTN:
				// Button background
				pDC->SetBkColor(theme.GetButtonBackgroundColor());
				return (HBRUSH)theme.GetControlBackgroundBrush()->GetSafeHandle();
				
			case CTLCOLOR_SCROLLBAR:
				// Scrollbar background
				return (HBRUSH)theme.GetControlBackgroundBrush()->GetSafeHandle();
				
			case CTLCOLOR_DLG:
				// Dialog background
				return (HBRUSH)theme.GetBackgroundBrush()->GetSafeHandle();
		}
	}
	
	return CPropertyPage::OnCtlColor(pDC, pWnd, nCtlColor);
}

BOOL MQMDPAGE::OnEraseBkgnd(CDC* pDC)
{
	ThemeManager& theme = ThemeManager::GetInstance();
	
	if (theme.IsDarkMode()) {
		CRect rect;
		GetClientRect(&rect);
		theme.DrawGradientBackground(pDC, rect);
		return TRUE;
	}
	
	return CPropertyPage::OnEraseBkgnd(pDC);
}

void MQMDPAGE::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	// Only handle buttons
	if (lpDrawItemStruct->CtlType != ODT_BUTTON) {
		CPropertyPage::OnDrawItem(nIDCtl, lpDrawItemStruct);
		return;
	}
	
	ThemeManager& theme = ThemeManager::GetInstance();
	
	// Get button text
	CWnd* pWnd = GetDlgItem(nIDCtl);
	if (!pWnd) {
		CPropertyPage::OnDrawItem(nIDCtl, lpDrawItemStruct);
		return;
	}
	
	CString buttonText;
	pWnd->GetWindowText(buttonText);
	
	// Use ThemeManager to draw the button
	CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
	CRect rect = lpDrawItemStruct->rcItem;
	
	theme.DrawThemedButton(pDC, rect, buttonText, lpDrawItemStruct->itemState);
}

////////////////////////////////////////////////////////////////////
//
// This page is called when a value is potentially changed and
// the MQMD tab is the current page.  This can happen when
// a value is changed using a menu item.
//
////////////////////////////////////////////////////////////////////

void MQMDPAGE::UpdatePageData()
{
	// make sure the maximum field lengths are correct
	if (pDoc->traceEnabled)
	{
		// trace entry to UpdatePageData
		pDoc->logTraceEntry("Entering MQMDPAGE:UpdatePageData()");
	}

	// create the id fields from the binary values
	SetId(&m_message_id, ID_TYPE_MSG);
	SetId(&m_correlid, ID_TYPE_CORREL);
	SetId(&m_group_id, ID_TYPE_GROUP);

	// initialize the id fields to overtype
	m_MsgIdEdit.SetOvertype(TRUE);
	m_CorrelIdEdit.SetOvertype(TRUE);
	m_GroupIdEdit.SetOvertype(TRUE);

	setMaxFieldLengths(m_id_disp_ascii);

	// the correlation id or group id may have changed due to
	// reading a new message or using a menu item
	OrigCorrelid = m_mqmd_correl_id;
	OrigGroupid = m_mqmd_group_id;

	// make sure the ccsid and encoding are up to date
	((CEdit *)GetDlgItem(IDC_MQMD_CCSID))->GetWindowText(m_mqmd_ccsid);

	// update the form data from the instance variables
	UpdateData(FALSE);
}


BOOL MQMDPAGE::OnSetActive() 
{
	char	traceInfo[256];		// work variable to build trace message

	// User has selected the MQMD tab
	UpdatePageData();

	if (pDoc->traceEnabled)
	{
		// trace entry to OnSetActive
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE:OnSetActive() pDoc->m_get_by_msgid=%d pDoc->m_setUserID=%d pDoc->m_set_all=%d pDoc->m_alt_userid=%d", pDoc->m_get_by_msgid, pDoc->m_setUserID, pDoc->m_set_all, pDoc->m_alt_userid);

		// trace exit from OnCopyMsgid
		pDoc->logTraceEntry(traceInfo);
	}

	((CEdit *)GetDlgItem(IDC_USER_ID))->SetReadOnly(TRUE);
	((CEdit *)GetDlgItem(IDC_ACCOUNT_TOKEN))->SetReadOnly(TRUE);
	((CEdit *)GetDlgItem(IDC_APPL_IDENTITY))->SetReadOnly(TRUE);
	((CEdit *)GetDlgItem(IDC_APPL_ORIGIN))->SetReadOnly(TRUE);
	((CEdit *)GetDlgItem(IDC_APPL_NAME))->SetReadOnly(TRUE);
	((CEdit *)GetDlgItem(IDC_APPL_TYPE))->EnableWindow(FALSE);
	((CEdit *)GetDlgItem(IDC_PUT_DATE_TIME))->SetReadOnly(TRUE);

/*	if (pDoc->m_get_by_msgid)
	{*/
		// the message id field can be changed
		((CEdit *)GetDlgItem(IDC_MSG_ID))->SetReadOnly(FALSE);
	/*}
	else
	{
		// set the message id to read only
		((CEdit *)GetDlgItem(IDC_MSG_ID))->SetReadOnly(TRUE);
	}*/

	if ((pDoc->m_setUserID) || (pDoc->m_set_all) || (pDoc->m_alt_userid))
	{
		((CEdit *)GetDlgItem(IDC_USER_ID))->SetReadOnly(FALSE);
		((CEdit *)GetDlgItem(IDC_ACCOUNT_TOKEN))->SetReadOnly(FALSE);

		if (pDoc->m_set_all)
		{
			((CEdit *)GetDlgItem(IDC_APPL_IDENTITY))->SetReadOnly(FALSE);
			((CEdit *)GetDlgItem(IDC_APPL_ORIGIN))->SetReadOnly(FALSE);
			((CEdit *)GetDlgItem(IDC_APPL_NAME))->SetReadOnly(FALSE);
			((CEdit *)GetDlgItem(IDC_APPL_TYPE))->EnableWindow(TRUE);
			((CEdit *)GetDlgItem(IDC_PUT_DATE_TIME))->SetReadOnly(FALSE);
//			((CEdit *)GetDlgItem(IDC_MSG_ID))->SetReadOnly(FALSE);
		}
	}

	BOOL ret = CPropertyPage::OnSetActive();
	
	// send a custom message to switch the focus to the QM combo box
	// this allows the tab keys to work
	PostMessage(WM_SETPAGEFOCUS, 0, 0L);

	return ret;
}

BOOL MQMDPAGE::OnKillActive() 
{
	// User has selected a different tab
	// get the form data into the instance variables
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// trace entry to OnKillActive
		pDoc->logTraceEntry("Entering MQMDPAGE:OnKillActive()");
	}

	return CPropertyPage::OnKillActive();
}

void MQMDPAGE::OnCopyMsgid() 
{
	char	traceInfo[512];		// work variable to build trace message
	
	// get the form data into the instance variables
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::OnCopyMsgid() m_mqmd_correl_id=%s len=%d", (LPCTSTR)m_mqmd_correl_id, m_mqmd_correl_id.GetLength());

		// trace entry to OnCopyMsgid
		pDoc->logTraceEntry(traceInfo);
	}

	// copy the correlation id to the message id
	m_mqmd_correl_id = m_msg_id;
	OrigCorrelid = (LPCTSTR)m_msg_id;
	memcpy(m_correlid, m_message_id, sizeof(m_correlid));

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE::OnCopyMsgid() m_mqmd_correl_id=%s len=%d", (LPCTSTR)m_mqmd_correl_id, m_mqmd_correl_id.GetLength());

		// trace exit from OnCopyMsgid
		pDoc->logTraceEntry(traceInfo);
	}
	
	// update the form data from the instance variables
	UpdateData(FALSE);
}

void MQMDPAGE::OnResetIds() 

{
	// Get the current data into the instance variables
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// trace entry to OnIdDispAscii
		pDoc->logTraceEntry("Entering MQMDPAGE:OnResetIds()");
	}

	// reset the correlation id and message id to binary zeros
	memset(m_correlid, 0, sizeof(m_correlid));
	memset(m_message_id, 0, sizeof(m_message_id));
	memset(m_group_id, 0, sizeof(m_group_id));
	
	// empty the correlation id, group id and message id display values
	m_mqmd_correl_id.Empty();
	m_msg_id.Empty();
	m_mqmd_group_id.Empty();

	// initialize the id fields to overtype
	m_MsgIdEdit.SetOvertype(TRUE);
	m_CorrelIdEdit.SetOvertype(TRUE);
	m_GroupIdEdit.SetOvertype(TRUE);

	// get the correlation id, message id and group id values
	SetId(&m_message_id, ID_TYPE_MSG);
	SetId(&m_correlid, ID_TYPE_CORREL);
	SetId(&m_group_id, ID_TYPE_GROUP);

	// remember the current values so we can tell if the user has changed the values
	OrigMsgid = m_msg_id;
	OrigCorrelid = m_mqmd_correl_id;
	OrigGroupid = m_mqmd_group_id;

	if (pDoc->traceEnabled)
	{
		// trace exit from OnResetIds
		pDoc->logTraceEntry("Exiting MQMDPAGE:OnResetIds()");
	}

	// update the form data from the instance variables
	UpdateData(FALSE);
}

void MQMDPAGE::OnIdDispAscii() 

{
	// Get the current data into the instance variables
	if (pDoc->traceEnabled)
	{
		// trace entry to OnIdDispAscii
		pDoc->logTraceEntry("Entering MQMDPAGE:OnIdDispAscii()");
	}

	updateIdFields();

	if (pDoc->traceEnabled)
	{
		// trace exit from OnIdDispAscii
		pDoc->logTraceEntry("Exiting MQMDPAGE:OnIdDispAscii()");
	}
}

void MQMDPAGE::OnIdDispEbcdic() 
{
	// display the ID fields in EBCDIC
	if (pDoc->traceEnabled)
	{
		// trace entry to OnIdDispEbcdic
		pDoc->logTraceEntry("Entering MQMDPAGE:OnIdDispEbcdic()");
	}

	updateIdFields();

	if (pDoc->traceEnabled)
	{
		// trace exit from OnIdDispEbcdic
		pDoc->logTraceEntry("Exiting MQMDPAGE:OnIdDispEbcdic()");
	}
}

void MQMDPAGE::OnHex() 
{
	// Display the ID fields in hex
	if (pDoc->traceEnabled)
	{
		// trace entry to OnHex
		pDoc->logTraceEntry("Entering MQMDPAGE:OnHex()");
	}

	updateIdFields();

	if (pDoc->traceEnabled)
	{
		// trace exit from OnHex
		pDoc->logTraceEntry("Exiting MQMDPAGE:OnHex()");
	}
}

void MQMDPAGE::updateIdFields()

{
	char			traceInfo[512];		// work variable to build trace message

	// get the form data into the instance variables
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::updateIdFields() m_mqmd_correl_id=%s len=%d OrigCorrelid=%s len=%d", (LPCTSTR)m_mqmd_correl_id, m_mqmd_correl_id.GetLength(), (LPCTSTR)OrigCorrelid, OrigCorrelid.GetLength());

		// trace entry to updateIdFields
		pDoc->logTraceEntry(traceInfo);
	}

	// check if the display type changed
	if (prev_disp_type != m_id_disp_ascii)
	{
		// check if the correlation id was changed in the previous display mode
		if (OrigCorrelid.Compare(m_mqmd_correl_id) != 0)
		{
			setCorrelId(prev_disp_type);
		}

		// check if the group id was changed in the previous display mode
		if (OrigGroupid.Compare(m_mqmd_group_id) != 0)
		{
			setGroupId();
		}

		// remember the new display type
		prev_disp_type = m_id_disp_ascii;
	}

	// make sure the maximum field lengths are correct
	setMaxFieldLengths(m_id_disp_ascii);

	// get the correlation id, message id and group id values
	SetId(&m_message_id, ID_TYPE_MSG);
	SetId(&m_correlid, ID_TYPE_CORREL);
	SetId(&m_group_id, ID_TYPE_GROUP);

	// remember the new values, so we can tell 
	// which individual characters have been changed
	OrigCorrelid = m_mqmd_correl_id;
	OrigGroupid = m_mqmd_group_id;

	// update the form data from the instance variables
	UpdateData(FALSE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE::updateIdFields() m_mqmd_correl_id=%s len=%d OrigCorrelid=%s len=%d", (LPCTSTR)m_mqmd_correl_id, m_mqmd_correl_id.GetLength(), (LPCTSTR)OrigCorrelid, OrigCorrelid.GetLength());

		// trace exit from updateIdFields
		pDoc->logTraceEntry(traceInfo);
	}
}

void MQMDPAGE::setMaxFieldLengths(int dispType)

{
	UINT	fieldSize = 0;
	char	traceInfo[512];		// work variable to build trace message

	switch (m_id_disp_ascii)
	{
	case ID_DISPLAY_ASCII:
		fieldSize = MQ_CORREL_ID_LENGTH;
		break;
	case ID_DISPLAY_EBCDIC:
		fieldSize = MQ_CORREL_ID_LENGTH;
		break;
	case ID_DISPLAY_HEX:
		fieldSize = MQ_CORREL_ID_LENGTH * 2;
		break;
	}

	// Allow input in ASCII, EBCDIC or HEX
	((CEdit *)GetDlgItem(IDC_MSG_ID))->SetLimitText(fieldSize);
	((CEdit *)GetDlgItem(IDC_CORREL_ID))->SetLimitText(fieldSize);
	((CEdit *)GetDlgItem(IDC_GROUP_ID))->SetLimitText(fieldSize);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE::setMaxFieldLengths() dispType=%d fieldSize=%d", dispType, fieldSize);

		// trace exit from setMaxFieldLengths
		pDoc->logTraceEntry(traceInfo);
	}
}

void MQMDPAGE::OnUpdateGroupId() 

{
	char	traceInfo[512];		// work variable to build trace message

	// get the form data into the instance variables
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::OnUpdateGroupId() m_mqmd_group_id=%s len=%d OrigGroupid=%s len=%d", (LPCTSTR)m_mqmd_group_id, m_mqmd_group_id.GetLength(), (LPCTSTR)OrigGroupid, OrigGroupid.GetLength());

		// trace entry to OnUpdateGroupId
		pDoc->logTraceEntry(traceInfo);
	}

	if (ID_DISPLAY_HEX == m_id_disp_ascii)
	{
		// start by checking the length
		// This determines if the user has deleted text, in which case zeros will be appended to 
		// the end of the data until the length is 48.
		// append binary zeros to the end of the data
		while (m_mqmd_group_id.GetLength() < 48)
		{
			m_mqmd_group_id += "0";
		}

		// update the group id data area
		setGroupId();

		// update the original group id
		OrigGroupid = m_mqmd_group_id;
	}
	else
	{
		// check the length
		if (m_mqmd_group_id.GetLength() > MQ_GROUP_ID_LENGTH)
		{
			// cannot exceed 24 characters, so restore the
			// previous value
			m_mqmd_group_id.SetAt(MQ_GROUP_ID_LENGTH, 0);

			// let the user know by beeping
			Beep(1000, 300);

			// update the data in the control
			UpdateData(FALSE);

			// restore the cursor at the end of the current data
			// otherwise the cursor moves to the beginning of the data
			((CEdit *)GetDlgItem(IDC_GROUP_ID))->SetSel(MQ_GROUP_ID_LENGTH, MQ_GROUP_ID_LENGTH);
		}
		else
		{
			// update the group id value
			setGroupId();
		}
	}

	// update the form data from the instance variables
	UpdateData(FALSE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE:OnUpdateGroupId() m_mqmd_group_id=%s len=%d OrigGroupid=%s len=%d", (LPCTSTR)m_mqmd_group_id, m_mqmd_group_id.GetLength(), (LPCTSTR)OrigGroupid, OrigGroupid.GetLength());

		// trace exit from OnUpdateGroupId
		pDoc->logTraceEntry(traceInfo);
	}
}

void MQMDPAGE::OnSetfocusMsgtype() 
{
	// Has the combo box been initialized?
	if (0 == msgTypeInit)
	{
		// remember that we have already initialized the combo box
		msgTypeInit = 1;

		// insert the known types into the dropdown list	
		((CComboBox *)GetDlgItem(IDC_MSGTYPE))->AddString("1 Request");
		((CComboBox *)GetDlgItem(IDC_MSGTYPE))->AddString("2 Reply");
		((CComboBox *)GetDlgItem(IDC_MSGTYPE))->AddString("4 Report");
		((CComboBox *)GetDlgItem(IDC_MSGTYPE))->AddString("8 Datagram");
		((CComboBox *)GetDlgItem(IDC_MSGTYPE))->AddString("112 FR_MQE");
		((CComboBox *)GetDlgItem(IDC_MSGTYPE))->AddString("113 MQE");
		((CComboBox *)GetDlgItem(IDC_MSGTYPE))->LimitText(11);
	}
}

void MQMDPAGE::OnSelchangeMsgtype() 

{
	int		i;
	char	tempStr[512];

	// get the form data into the instance variables
	tempStr[0] = 0;
	i = ((CComboBox *)GetDlgItem(IDC_MSGTYPE))->GetCurSel();
	((CComboBox *)GetDlgItem(IDC_MSGTYPE))->GetLBText(i, tempStr);
}

void MQMDPAGE::OnKillfocusMsgtype()

{
	// get the form data into the instance variables
	UpdateData(TRUE);
}

BOOL MQMDPAGE::GetToolTipText(UINT id, NMHDR *pTTTStruct, LRESULT *pResult)

{
	// need to handle both ANSI and UNICODE versions of the message
	TOOLTIPTEXTA* pTTTA = (TOOLTIPTEXTA*)pTTTStruct;
	TOOLTIPTEXTW* pTTTW = (TOOLTIPTEXTW*)pTTTStruct;
	CString strTipText;

	UINT nID = pTTTStruct->idFrom;

	if (((pTTTStruct->code == TTN_NEEDTEXTA) && (pTTTA->uFlags & TTF_IDISHWND)) ||
		((pTTTStruct->code == TTN_NEEDTEXTW) && (pTTTW->uFlags & TTF_IDISHWND)))
	{
		// idFrom is actually the HWND of the tool
		nID = ::GetDlgCtrlID((HWND)nID);
		if(nID != 0)
		{
			if (pTTTStruct->code == TTN_NEEDTEXTA)
			{
				pTTTA->lpszText = MAKEINTRESOURCE(nID);
				pTTTA->hinst = AfxGetResourceHandle();
			}
			else
			{
//				pTTTW->lpszText = MAKEINTRESOURCE(nID);
//				pTTTW->hinst = AfxGetResourceHandle();
			}

			pResult = 0;
			return(TRUE);
		}
	}

	return(FALSE);
}

BOOL MQMDPAGE::PreTranslateMessage(MSG* pMsg) 
{
	if (pMsg->message != WM_KEYDOWN)
		return CPropertyPage::PreTranslateMessage(pMsg);

	// check for a backspace key
	if (VK_BACK == pMsg->wParam)
	{
		// get the control which currently has the focus
		CWnd * curFocus = GetFocus();
		if (curFocus != NULL)
		{
			// figure out which control has the focus
			int id = curFocus->GetDlgCtrlID();

			// check if this is an edit box control
			if ((IDC_MQMD_FORMAT == id) || 
				(IDC_MQMD_CCSID == id) || 
				(IDC_USER_ID == id) || 
				(IDC_PUT_DATE_TIME == id) || 
				(IDC_ACCOUNT_TOKEN == id) || 
				(IDC_MQMD_PRIOTITY == id) || 
				(IDC_EXPIRY == id) || 
				(IDC_MQMD_FEEDBACK == id) || 
				(IDC_OFFSET == id) || 
				(IDC_APPL_IDENTITY == id) || 
				(IDC_APPL_ORIGIN == id) || 
				(IDC_APPL_NAME == id) || 
				(IDC_REPLY_QM == id) || 
				(IDC_SEQNO == id) ||
				(IDC_MQMD_ORIG_LEN == id) ||
				(IDC_REPLY_QUEUE == id))
			{
				processBackspace(curFocus);
				return TRUE;
			}
		}
	}

	if (IsDialogMessage(pMsg))
		return TRUE;
	else
		return CPropertyPage::PreTranslateMessage(pMsg);
}

void MQMDPAGE::OnUpdateMqmdCcsid() 
{
	UpdateData(TRUE);

	pDoc->freeHeader(m_mqmd_format);

	// update the form data from the instance variables
	UpdateData(FALSE);
}

void MQMDPAGE::OnMqmdPc() 
{
	UpdateData(TRUE);

	pDoc->freeHeader(m_mqmd_format);

	// update the form data from the instance variables
	UpdateData(FALSE);
}

void MQMDPAGE::OnMqmdHost() 
{
	UpdateData(TRUE);

	pDoc->freeHeader(m_mqmd_format);

	// update the form data from the instance variables
	UpdateData(FALSE);
}

void MQMDPAGE::OnMqmdUnix() 
{
	// delete any existing header data areas
	// get the form data into the instance variables
	UpdateData(TRUE);

	pDoc->freeHeader(m_mqmd_format);

	// update the form data from the instance variables
	UpdateData(FALSE);
}

void MQMDPAGE::OnSelchangeApplType() 

{
	// User has selected a new application type
	int		i;
	char	tempStr[32];

	// get the form data into the instance variables
	tempStr[0] = 0;
	i = ((CComboBox *)GetDlgItem(IDC_MSGTYPE))->GetCurSel();
	((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->GetLBText(i, tempStr);
	m_mqmd_appl_type = tempStr;
}

void MQMDPAGE::OnSetfocusApplType()
 
{
	// The user has selected the application type combo box
	// load the drop down list
	if (putApplTypeInit)
	{
		putApplTypeInit = FALSE;
		// insert the known types into the dropdown list	
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_NO_CONTEXT));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_CICS));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_ZOS));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_IMS));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_OS2));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_DOS));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_UNIX));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_QMGR));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_OS400));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_WINDOWS));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_CICS_VSE));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_WINDOWS_NT));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_VMS));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_GUARDIAN));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_VOS));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_IMS_BRIDGE));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_XCF));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_CICS_BRIDGE));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_NOTES_AGENT));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_USER));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_BROKER));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_JAVA));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_DQM));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_CHANNEL_INITIATOR));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_WLM));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_BATCH));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_RRS_BATCH));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_SIB));
		((CComboBox *)GetDlgItem(IDC_APPL_TYPE))->AddString(getApplTypeDisplay(MQAT_SYSTEM_EXTENSION));
	}
}

void MQMDPAGE::CopyMsgIdToCorrelId()

{
	OnCopyMsgid();
}

void MQMDPAGE::ResetIds()

{
	OnResetIds();
}

LRESULT MQMDPAGE::OnSetPageFocus(WPARAM wParam, LPARAM lParam)

{
	// set the focus to the queue manager name combo box
	((CEdit *)GetDlgItem(IDC_MQMD_FORMAT))->SetFocus();

	return 0;
}

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

//////////////////////////////////////////////////////////////
//
// Routine to process a the message flags in an MQMD
//
//////////////////////////////////////////////////////////////

void MQMDPAGE::processFlags(int flags)

{
	m_mqmd_group_yes = (flags & MQMF_MSG_IN_GROUP) > 0;
	m_mqmd_group_last = (flags & MQMF_LAST_MSG_IN_GROUP) > 0;
	m_mqmd_segment_yes = (flags & MQMF_SEGMENT) > 0;
	m_mqmd_segment_last = (flags & MQMF_LAST_SEGMENT) > 0;
	m_mqmd_segment_allowed = (flags & MQMF_SEGMENTATION_ALLOWED) > 0;
}

void MQMDPAGE::processMessageMQMD(MQMD2 * mqmd)

{
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
}

void MQMDPAGE::clearMQMD()

{
	if (pDoc->traceEnabled)
	{
		// trace entry to clearMQMD
		pDoc->logTraceEntry("Entering MQMDPAGE::clearMQMD()");
	}

	m_mqmd_encoding = 0;
	m_mqmd_pd_encoding = 0;

	// reset MQMD variables
	m_mqmd_persist = 0;			// set to no in view
	m_mqmd_ccsid = "";		// set to default US NT code page

	// Reset MQMD format field to default values
	m_mqmd_format.Empty();
	m_mqmd_format_cb.SetWindowText((LPCTSTR)m_mqmd_format);

	// reset the reply to queue fields
	m_reply_qm.Empty();
	m_reply_queue.Empty();

	// reset miscellaneous MQMD fields
	m_mqmd_appl_name.Empty();
	m_mqmd_appl_type.Empty();
	m_mqmd_appl_id.Empty();
	m_mqmd_appl_origin.Empty();
	m_mqmd_orig_len.Format("%d", MQOL_UNDEFINED);

	// reset the report flags
	m_report_coa = -1;
	m_report_cod = -1;
	m_report_except = -1;
	m_report_expire = -1;
	m_report_nan = FALSE;
	m_report_pan = FALSE;
	m_report_pass_correlid = FALSE;
	m_report_activity = FALSE;
	m_report_discard = FALSE;
	m_report_pass_msgid = FALSE;
	m_report_expire_discard = FALSE;

	// clear the backout counter
	m_mqmd_backout_count = "0";

	// clear the message sequence number
	m_mqmd_seq_no = "1";

	// clear the message type
	m_mqmd_msgtype.Empty();

	// clear the user id
	m_mqmd_userid.Empty();

	// clear the accounting token
	m_mqmd_account_token.Empty();

	// clear the put date and time
	m_mqmd_date_time.Empty();

	// clear the expiry time
	m_mqmd_expiry.Empty();

	// clear the offset, feedback and priority fields
	m_mqmd_offset.Empty();
	m_mqmd_feedback.Empty();
	m_mqmd_priority.Empty();

	// clear the group and segment flags
	m_mqmd_group_yes = FALSE;
	m_mqmd_group_last = FALSE;
	m_mqmd_segment_yes = FALSE;
	m_mqmd_segment_last = FALSE;
	m_mqmd_segment_allowed = FALSE;

	// reset the display encoding
	pDoc->m_numeric_format = NUMERIC_PC;
	pDoc->m_pd_numeric_format = NUMERIC_PC;

	// clear the message, correlation and group ids
	memset(m_message_id, 0, sizeof(m_message_id));
	memset(m_correlid, 0, sizeof(m_correlid));
	memset(m_group_id, 0, sizeof(m_group_id));
	m_mqmd_correl_id.Empty();
	m_msg_id.Empty();
	m_mqmd_group_id.Empty();

	// get the instance variables into the form
	UpdateData(FALSE);

	if (pDoc->traceEnabled)
	{
		// trace exit from clearMQMD
		pDoc->logTraceEntry("Exiting MQMDPAGE::clearMQMD()");
	}
}

void MQMDPAGE::SetId(MQBYTE24 * id, int idType)

{
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
}

/////////////////////////////////////////////
//
// Routine to set the group id.
// The format can be in ascii, ebcdic or hex, 
// depending on the charType value.
//
/////////////////////////////////////////////

void MQMDPAGE::setGroupId()

{
	std::string	error;

	// the edited text of the box becomes the binary id; a rejected edit leaves it unchanged
	rfh::id_display::applyEdit(rfh::id_display::Field::GroupId, m_group_id, (LPCTSTR)m_mqmd_group_id, (LPCTSTR)OrigGroupid, (rfh::id_display::Mode)prev_disp_type, error);
}

/////////////////////////////////////////////////////////////////////////
//
// Routine to create an MQMD based on the instance variables.
//
/////////////////////////////////////////////////////////////////////////

void MQMDPAGE::buildMQMD(MQMD2 *mqmd, BOOL m_setUserID, BOOL m_set_all)

{
	// the tab's fields, shared with the Linux version
	rfh::mqmd_fields::toMqmd(getFields(), *mqmd);
}

void MQMDPAGE::setMessageMQMD(MQMD2 * mqmd, BOOL m_setUserID, BOOL m_set_all)

{
	rfh::PutContext	context;

	// Set Iden Context and Set All Context, and whether the OS user name could be looked up
	context.setIdentity = (m_setUserID != FALSE);
	context.setAll = (m_set_all != FALSE);
	context.osUser = rfh_current_user();

	// the tab's fields, shared with the Linux version
	rfh::mqmd_fields::toPutMqmd(getFields(), context, *mqmd);
}

void MQMDPAGE::updateMQMDafterPut(MQMD2 * mqmd, MQLONG msgLen)

{
	rfh::MqmdFields	fields = getFields();

	// Seq No, Offset, Msg Id and Group Id from the MQMD that MQPUT returned
	rfh::mqmd_fields::applyAfterPut(*mqmd, msgLen, fields);
	setFields(fields);
}

bool MQMDPAGE::setGroupActive(BOOL m_logical_order)

{
	bool result=false;

	if ((m_logical_order && !m_mqmd_group_last) || (m_mqmd_segment_yes && !m_mqmd_segment_last))
	{
		result = true;
	}

	return result;
}

bool MQMDPAGE::getSegmentActive()

{
	if (m_mqmd_segment_yes && !m_mqmd_segment_last)
	{
		 return true;
	}
	else
	{
		return false;
	}
}

///////////////////////////////////////////
//
// This routine is called to update the 
// format field.  It is used when a file
// is read that contains an MQ header but
// does not contain an MQMD at the front
// of the file.
//
///////////////////////////////////////////

void MQMDPAGE::setFormat(const char *fmt)

{
	char	traceInfo[512];		// work variable to build trace message

	m_mqmd_format = fmt;
	m_mqmd_format_cb.SetWindowText((LPCTSTR)m_mqmd_format);
	
	// update the form data from the instance variables
	// this prevents any old values from turning up
	UpdateData(FALSE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "MQMDPAGE::setFormat() format=%s", fmt);

		// trace exit from setFormat
		pDoc->logTraceEntry(traceInfo);
	}
}

///////////////////////////////////////////
//
// This routine is called to update the 
// ccsid field.  It is used when a file
// is read that contains an MQ header but
// does not contain an MQMD at the front
// of the file.
//
///////////////////////////////////////////

void MQMDPAGE::setCcsid(int ccsid)

{
	char	traceInfo[512];		// work variable to build trace message

	m_mqmd_ccsid.Format("%d", ccsid);
	
	// update the form data from the instance variables
	// this prevents any old values from turning up
	UpdateData(FALSE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "MQMDPAGE::setCcsid() ccsid=%d m_mqmd_ccsid=%s", ccsid, (LPCTSTR)m_mqmd_ccsid);

		// trace exit from setCcsid
		pDoc->logTraceEntry(traceInfo);
	}
}

///////////////////////////////////////////
//
// This routine is called to update the 
// encoding fields.  It is used when a file
// is read that contains an MQ header but
// does not contain an MQMD at the front
// of the file.
//
///////////////////////////////////////////

void MQMDPAGE::setEncoding(int encoding, int pdEncoding, int floatEncoding)

{
	char	traceInfo[512];		// work variable to build trace message

	m_mqmd_encoding = encoding;
	m_mqmd_pd_encoding = pdEncoding;
	m_mqmd_float_encoding = floatEncoding;

	// update the form data from the instance variables
	// this prevents any old values from turning up
	UpdateData(FALSE);
	
	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "MQMDPAGE::setEncoding() encoding=%d pdEncoding=%d floatEncoding=%d", encoding, pdEncoding, floatEncoding);

		// trace exit from setEncoding
		pDoc->logTraceEntry(traceInfo);
	}
}

/////////////////////////////////////////////////////////////////////////
//
// Routine to look for an MQMD when a file is read
//
/////////////////////////////////////////////////////////////////////////

int MQMDPAGE::getMQMD(unsigned char *msgData, int msgLen, int *ccsid, int *encoding)

{
	int				v1r;
	int				v2r;
	MQMD			*mqmd;
	unsigned char	ebcdicID[4];
	unsigned char	asciiID[4];
	unsigned char	msgData8[32];
	char			traceInfo[512];		// work variable to build trace message

	if (pDoc->traceEnabled && (msgData != NULL))
	{
		// get the first 8 characters of the message in hex
		memset(msgData8, 0, sizeof(msgData8));
		AsciiToHex(msgData, 8, msgData8);

		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::getMQMD() msgData=%s msgLen=%d", msgData8, msgLen);

		// trace entry to getMQMD
		pDoc->logTraceEntry(traceInfo);
	}

	if ((msgLen < sizeof(MQMD2)) || (NULL == msgData))
	{
		// total message is too short to have an MQMD
		return 0;
	}

	// point to the file data
	mqmd = (MQMD *)msgData;

	// check for the MQMD eye catcher
	memcpy(asciiID, MQMD_STRUC_ID, 4);
	AsciiToEbcdic(asciiID, 4, ebcdicID);
	if ((memcmp(mqmd->StrucId, asciiID, 4) != 0) && (memcmp(mqmd->StrucId, ebcdicID, 4) != 0))
	{
		// did not find the eyecatcher - return
		return 0;
	}

	// check if the MQMD is in ebcdic
	if (memcmp(mqmd->StrucId, ebcdicID, 4) == 0)
	{
		// convert the character fields to ASCII
		EbcdicToAscii((unsigned char *)&mqmd->Format, sizeof(mqmd->Format), (unsigned char *)&mqmd->Format);
		EbcdicToAscii((unsigned char *)&mqmd->ReplyToQ, sizeof(mqmd->ReplyToQ), (unsigned char *)&mqmd->ReplyToQ);
		EbcdicToAscii((unsigned char *)&mqmd->ReplyToQMgr, sizeof(mqmd->ReplyToQMgr), (unsigned char *)&mqmd->ReplyToQMgr);
		EbcdicToAscii((unsigned char *)&mqmd->UserIdentifier, sizeof(mqmd->UserIdentifier), (unsigned char *)&mqmd->UserIdentifier);
		EbcdicToAscii((unsigned char *)&mqmd->ApplIdentityData, sizeof(mqmd->ApplIdentityData), (unsigned char *)&mqmd->ApplIdentityData);
		EbcdicToAscii((unsigned char *)&mqmd->PutApplName, sizeof(mqmd->PutApplName), (unsigned char *)&mqmd->PutApplName);
		EbcdicToAscii((unsigned char *)&mqmd->PutDate, sizeof(mqmd->PutDate), (unsigned char *)&mqmd->PutDate);
		EbcdicToAscii((unsigned char *)&mqmd->PutTime, sizeof(mqmd->PutTime), (unsigned char *)&mqmd->PutTime);
		EbcdicToAscii((unsigned char *)&mqmd->ApplOriginData, sizeof(mqmd->ApplOriginData), (unsigned char *)&mqmd->ApplOriginData);
	}

	// check if the MQMD is in a different encoding sequence
	v1r = reverseBytes4(MQMD_VERSION_1);
	v2r = reverseBytes4(MQMD_VERSION_2);
	if ((v1r == mqmd->Version) || (v2r == mqmd->Version))
	{
		// it appears the mqmd was captured on a big-endian system
		// therefore, we will reverse the byte ordering in the mqmd
		mqmd->Version = reverseBytes4(mqmd->Version); 
		mqmd->Report = reverseBytes4(mqmd->Report);
		mqmd->MsgType = reverseBytes4(mqmd->MsgType); 
		mqmd->Expiry = reverseBytes4(mqmd->Expiry);
		mqmd->Feedback = reverseBytes4(mqmd->Feedback); 
		mqmd->Encoding = reverseBytes4(mqmd->Encoding);
		mqmd->CodedCharSetId = reverseBytes4(mqmd->CodedCharSetId); 
		mqmd->Priority = reverseBytes4(mqmd->Priority);
		mqmd->Persistence = reverseBytes4(mqmd->Persistence);
		mqmd->BackoutCount = reverseBytes4(mqmd->BackoutCount);
		mqmd->PutApplType = reverseBytes4(mqmd->PutApplType);

		// deal with the message id, correl id, accounting token
		reverseBytes24(mqmd->MsgId, mqmd->MsgId);
		reverseBytes24(mqmd->CorrelId, mqmd->CorrelId);
		reverseBytes32(mqmd->AccountingToken, mqmd->AccountingToken);

		// check if this is a version 1 or version 2 mqmd
		if (mqmd->Version!= MQMD_VERSION_1)
		{
			// deal with the group id
			reverseBytes24(mqmd->GroupId, mqmd->GroupId);
			mqmd->MsgSeqNumber = reverseBytes4(mqmd->MsgSeqNumber);
			mqmd->Offset = reverseBytes4(mqmd->Offset);
			mqmd->MsgFlags = reverseBytes4(mqmd->MsgFlags);
			mqmd->OriginalLength = reverseBytes4(mqmd->OriginalLength);
		}
	}

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE::getMQMD() return=%d", sizeof(MQMD2));

		// trace exit from getMQMD
		pDoc->logTraceEntry(traceInfo);
	}

	return sizeof(MQMD2);
}

///////////////////////////////////////////////////////////////////
//
// Routine to extract the MQMD fields from an ImqMessage object.
//
///////////////////////////////////////////////////////////////////

/*void MQMDPAGE::extractMQMD(MQMD *mqmd, ImqMessage &msg)

{
	ImqBin	msgid;				// MQ Bin object to hold message id
	ImqBin	mqcorrelid;			// MQ Bin object to hold correlation id
	ImqBin	mqgroupid;			// MQ Bin object to hold group id
	ImqBin	acctToken;			// MQMD accounting token
	ImqString	putDate;		// MQMD put date
	ImqString	putTime;		// MQMD put time

	// copy the integer fields
	mqmd->Version			= MQMD_VERSION_2;
	mqmd->Report			= msg.report();
	mqmd->MsgType			= msg.messageType();
	mqmd->Expiry			= msg.expiry();
	mqmd->Feedback			= msg.feedback();
	mqmd->Encoding			= msg.encoding();
	mqmd->CodedCharSetId	= msg.characterSet();
	mqmd->Priority			= msg.priority();
	mqmd->Persistence		= msg.persistence();
	mqmd->BackoutCount		= msg.backoutCount();
	mqmd->PutApplType		= msg.putApplicationType();
	mqmd->MsgSeqNumber		= msg.sequenceNumber();
	mqmd->Offset			= msg.offset();
	mqmd->MsgFlags			= msg.messageFlags();
	mqmd->OriginalLength	= msg.originalLength();

	// copy the character information
	memcpy(&mqmd->Format, msg.format(), MQ_FORMAT_LENGTH);
	memcpy(&mqmd->ReplyToQMgr, msg.replyToQueueManagerName(), MQ_Q_MGR_NAME_LENGTH);
	memcpy(&mqmd->ReplyToQ, msg.replyToQueueName(), MQ_Q_NAME_LENGTH);
	memcpy(&mqmd->UserIdentifier, msg.userId(), MQ_USER_ID_LENGTH);
	memcpy(&mqmd->ApplIdentityData, msg.applicationIdData(), MQ_APPL_IDENTITY_DATA_LENGTH);
	memcpy(&mqmd->PutApplName, msg.putApplicationName(), MQ_APPL_NAME_LENGTH);
	memcpy(&mqmd->PutDate, msg.putDate(), MQ_DATE_LENGTH);
	memcpy(&mqmd->PutTime, msg.putTime(), MQ_TIME_LENGTH);

	// extract the binary fields
	ImqMessageTracker msg_track(msg);
    msgid = msg_track.messageId( );
	if (msgid.dataLength() > 0)
	{
		msgid.copyOut(mqmd->MsgId, sizeof(mqmd->MsgId));
	}

	mqcorrelid = msg_track.correlationId( );
	if (mqcorrelid.dataLength() > 0)
	{
		mqcorrelid.copyOut(mqmd->CorrelId, sizeof(mqmd->CorrelId));
	}

	mqgroupid = msg_track.groupId();
	if (mqgroupid.dataLength() > 0)
	{
		mqgroupid.copyOut(mqmd->GroupId, sizeof(mqmd->GroupId));
	}

	acctToken = msg_track.accountingToken();
	if (acctToken.dataLength() > 0)
	{
		acctToken.copyOut(mqmd->AccountingToken, sizeof(mqmd->AccountingToken));
	}

	// get the instance variables into the form
	UpdateData(FALSE);
}*/

// support routines for menu items

void MQMDPAGE::saveMsgId()

{
	// make sure we have current info
	UpdateData(TRUE);

	memcpy(m_saved_msg_id, m_message_id, sizeof(m_saved_msg_id));

	if (pDoc->traceEnabled)
	{
		// trace entry to restoreGroupId
		pDoc->logTraceEntry("Entering MQMDPAGE:saveMsgId()");
	}
}

void MQMDPAGE::saveCorrelId()

{
	// make sure we have current info
	UpdateData(TRUE);

	memcpy(m_saved_correlid, m_correlid, sizeof(m_saved_correlid));

	if (pDoc->traceEnabled)
	{
		// trace entry to restoreGroupId
		pDoc->logTraceEntry("Entering MQMDPAGE:saveCorrelId()");
	}
}

void MQMDPAGE::saveGroupId()

{
	// make sure we have current info
	UpdateData(TRUE);

	memcpy(m_saved_group_id, m_group_id, sizeof(m_saved_group_id));

	if (pDoc->traceEnabled)
	{
		// trace entry to restoreGroupId
		pDoc->logTraceEntry("Entering MQMDPAGE:saveGroupId()");
	}
}

void MQMDPAGE::restoreMsgId()

{
	// make sure we have current info
	UpdateData(TRUE);

	memcpy(m_message_id, m_saved_msg_id, sizeof(m_message_id));

	if (pDoc->traceEnabled)
	{
		// trace entry to restoreGroupId
		pDoc->logTraceEntry("Entering MQMDPAGE:restoreMsgId()");
	}
}

void MQMDPAGE::restoreCorrelId()

{
	char	traceInfo[512];		// work variable to build trace message
	
	// get the form data into the instance variables
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::restoreCorrelId() m_mqmd_correl_id=%s len=%d", (LPCTSTR)m_mqmd_correl_id, m_mqmd_correl_id.GetLength());

		// trace entry to restoreCorrelId
		pDoc->logTraceEntry(traceInfo);
	}

	memcpy(m_correlid, m_saved_correlid, sizeof(m_correlid));

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE::restoreCorrelId() m_mqmd_correl_id=%s len=%d", (LPCTSTR)m_mqmd_correl_id, m_mqmd_correl_id.GetLength());

		// trace exit from restoreCorrelId
		pDoc->logTraceEntry(traceInfo);
	}
}

void MQMDPAGE::restoreGroupId()

{
	// make sure we have current info
	UpdateData(TRUE);

	memcpy(m_group_id, m_saved_group_id, sizeof(m_group_id));

	if (pDoc->traceEnabled)
	{
		// trace entry to restoreGroupId
		pDoc->logTraceEntry("Entering MQMDPAGE:restoreGroupId()");
	}
}

//////////////////////////////////////////////////////////
//
// This routine is used to update the correlation id from
// the CICS page.  In certain cases a CICS header requires
// the correlation id to be set to a specific value.
//
////////////////////////////////////////////////////////////

void MQMDPAGE::changeCorrelId(MQBYTE24 * correlid)

{
	char	traceInfo[512];		// work variable to build trace message

	if (pDoc->traceEnabled)
	{
		// trace entry to changeCorrelId
		pDoc->logTraceEntry("Entering MQMDPAGE:changeCorrelId()");
	}

	memcpy(m_correlid, correlid, sizeof(m_correlid));
	SetId(&m_correlid, ID_TYPE_CORREL);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE:changeCorrelId() m_mqmd_correl_id=%s", (LPCTSTR)m_mqmd_correl_id);

		// trace exit from changeCorrelId
		pDoc->logTraceEntry(traceInfo);
	}
}

void MQMDPAGE::updateFields(const char *newFormat, int newCcsid, int newEncoding, int newPdEncoding, int newFloatEncoding)

{
	char	traceInfo[512];		// work variable to build trace message

	// make sure we have current info
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::updateFields() m_mqmd_format=%s m_mqmd_ccsid=%s m_mqmd_encoding=%d m_mqmd_pd_encoding=%d m_mqmd_float_encoding=%d", (LPCTSTR)m_mqmd_format, (LPCTSTR)m_mqmd_ccsid, m_mqmd_encoding, m_mqmd_pd_encoding, m_mqmd_float_encoding);

		// trace entry to updateFields
		pDoc->logTraceEntry(traceInfo);
	}

	m_mqmd_format = newFormat;
	m_mqmd_format_cb.SetWindowText((LPCTSTR)m_mqmd_format);
	m_mqmd_ccsid.Format("%d", newCcsid);
	m_mqmd_encoding = newEncoding;
	m_mqmd_pd_encoding = newPdEncoding;
	m_mqmd_float_encoding = newFloatEncoding;

	// update the dialog controls
	UpdateData(FALSE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE::updateFields() m_mqmd_format=%s m_mqmd_ccsid=%s m_mqmd_encoding=%d m_mqmd_pd_encoding=%d m_mqmd_float_encoding=%d", (LPCTSTR)m_mqmd_format, (LPCTSTR)m_mqmd_ccsid, m_mqmd_encoding, m_mqmd_pd_encoding, m_mqmd_float_encoding);

		// trace exit from updateFields
		pDoc->logTraceEntry(traceInfo);
	}
}

//////////////////////////////////////////////////////////
//
// This routine returns the value of the MQMD format field
// as a null terminated string, with the format value
// padded to 8 characters.
//
////////////////////////////////////////////////////////////

void MQMDPAGE::getFormat(char *mqformat)

{
	char	traceInfo[512];		// work variable to build trace message

	// get the form data into the instance variables
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::getFormat() m_mqmd_format=%s", (LPCTSTR)m_mqmd_format);

		// trace entry to getFormat
		pDoc->logTraceEntry(traceInfo);
	}

	// initialize the result to blanks
	memset(mqformat, ' ', MQ_FORMAT_LENGTH);
	m_mqmd_format_cb.GetWindowText(m_mqmd_format);
	if (m_mqmd_format.GetLength() > 0)
	{
		memcpy(mqformat, (LPCTSTR)m_mqmd_format, m_mqmd_format.GetLength());
	}

	// terminate the string
	mqformat[MQ_FORMAT_LENGTH] = 0;
}

int MQMDPAGE::getCcsid()

{
	char	traceInfo[512];		// work variable to build trace message

	// get the form data into the instance variables
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE::getCcsid() m_mqmd_ccsid=%s", (LPCTSTR)m_mqmd_ccsid);

		// trace exit from getCcsid
		pDoc->logTraceEntry(traceInfo);
	}

	return atoi((LPCTSTR)m_mqmd_ccsid);
}

int MQMDPAGE::getEncoding()

{
	char	traceInfo[512];		// work variable to build trace message

	// get the form data into the instance variables
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE::getEncoding() m_mqmd_encoding=%d", m_mqmd_encoding);

		// trace exit from getEncoding
		pDoc->logTraceEntry(traceInfo);
	}

	return m_mqmd_encoding;
}

int MQMDPAGE::getPdEncoding()

{
	char	traceInfo[512];		// work variable to build trace message

	// get the form data into the instance variables
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE::getPdEncoding() m_mqmd_pd_encoding=%d", m_mqmd_pd_encoding);

		// trace exit from getPdEncoding
		pDoc->logTraceEntry(traceInfo);
	}

	return m_mqmd_pd_encoding;
}

int MQMDPAGE::getFloatEncoding()

{
	char	traceInfo[512];		// work variable to build trace message

	// get the form data into the instance variables
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE::getFloatEncoding() m_mqmd_float_encoding=%d", m_mqmd_float_encoding);

		// trace exit from getPdEncoding
		pDoc->logTraceEntry(traceInfo);
	}

	return m_mqmd_float_encoding;
}

/////////////////////////////////////////////
//
// Routine to set the correlation id.
// The format can be in ascii or ebcdic, 
// depending on the charType value.
//
/////////////////////////////////////////////

void MQMDPAGE::setCorrelId(int dispType)

{
	std::string	error;

	// the edited text of the box becomes the binary id; a rejected edit leaves it unchanged
	rfh::id_display::applyEdit(rfh::id_display::Field::CorrelId, m_correlid, (LPCTSTR)m_mqmd_correl_id, (LPCTSTR)OrigCorrelid, (rfh::id_display::Mode)dispType, error);
}

void MQMDPAGE::OnSetfocusCorrelId() 

{
	char	traceInfo[512];		// work variable to build trace message
	
	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::OnSetfocusCorrelId() m_mqmd_correl_id=%s len=%d OrigCorrelid=%s len=%d", (LPCTSTR)m_mqmd_correl_id, m_mqmd_correl_id.GetLength(), (LPCTSTR)OrigCorrelid, OrigCorrelid.GetLength());

		// trace entry to OnSetfocusCorrelId
		pDoc->logTraceEntry(traceInfo);
	}

	// remember the original values of the correl id
	// in case they are edited by the user
	OrigCorrelid = m_mqmd_correl_id;

	// set the group id custom edit control hexonly option to match the id display
	if (ID_DISPLAY_HEX == m_id_disp_ascii)
	{
		// Only accept hex characters
		m_CorrelIdEdit.SetHexOnly(TRUE);
	}
	else
	{
		// normal characters allowed
		m_CorrelIdEdit.SetHexOnly(FALSE);
	}

	// place the cursor at the beginning of the edit box
	m_CorrelIdEdit.SetSel(0, 0);
}

void MQMDPAGE::OnKillfocusCorrelId() 

{
	// Focus is changing to another control
	char	traceInfo[512];		// work variable to build trace message
	
	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::OnKillfocusCorrelId() m_mqmd_correl_id=%s len=%d OrigCorrelid=%s len=%d", (LPCTSTR)m_mqmd_correl_id, m_mqmd_correl_id.GetLength(), (LPCTSTR)OrigCorrelid, OrigCorrelid.GetLength());

		// trace entry to OnKillfocusCorrelId
		pDoc->logTraceEntry(traceInfo);
	}
}

//////////////////////////////////////////////////////
//
// This routine will attempt to preserve characters
// such as binary zeros which have been transformed
// to blanks.  Therefore, it will compare the current
// data with the previous data on a character by 
// character basis, looking for only changed 
// characters.  Any characters which are the same
// will be replaced with their original values.
// This logic is only required if the display type
// is ASCII or EBCDIC.
//
// The msg, correlation and group id edit boxes use a
// special subclass.  The subclass will only allow
// valid hex characters if the hex display option 
// is selected.  The subclass also forces 
// the edit box into overtype mode.  
//
//////////////////////////////////////////////////////

void MQMDPAGE::OnUpdateCorrelId() 

{
	int		size;
	char	text[64];
	char	traceInfo[512];		// work variable to build trace message

	// get the current text in the edit control
	size = ((CEdit *)GetDlgItem(IDC_CORREL_ID))->GetWindowText(text, sizeof(text) - 1);

	// get the form data into the instance variables
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::OnUpdateCorrelId() m_mqmd_correl_id=%s len=%d OrigCorrelid=%s len=%d", (LPCTSTR)m_mqmd_correl_id, m_mqmd_correl_id.GetLength(), (LPCTSTR)OrigCorrelid, OrigCorrelid.GetLength());

		// trace entry to OnUpdateCorrelId
		pDoc->logTraceEntry(traceInfo);
	}

	if (ID_DISPLAY_HEX == m_id_disp_ascii)
	{
		// start by checking the length
		// This determines if the user has deleted text, in which case zeros will be appended to 
		// the end of the data until the length is 48.
		// append binary zeros to the end of the data
		while (m_mqmd_correl_id.GetLength() < (MQ_CORREL_ID_LENGTH*2))
		{
			m_mqmd_correl_id += "0";
		}

		// check if the length is greater than 48
		if (m_mqmd_correl_id.GetLength() < (MQ_CORREL_ID_LENGTH*2))
		{
			// truncate the result
			m_mqmd_correl_id.SetAt(MQ_CORREL_ID_LENGTH*2, 0);
		}
	}
	else
	{
		while (m_mqmd_correl_id.GetLength() < MQ_CORREL_ID_LENGTH)
		{
			// append blanks to the end of the field
			m_mqmd_correl_id += " ";
		}

		// check the length
		if (m_mqmd_correl_id.GetLength() > MQ_CORREL_ID_LENGTH)
		{
			// cannot exceed 24 characters, so truncate to 24 characters
			m_mqmd_correl_id.SetAt(MQ_CORREL_ID_LENGTH, 0);

			// let the user know by beeping
			Beep(1000, 300);

			// update the data in the control
			UpdateData(FALSE);

			// restore the cursor at the end of the current data
			// otherwise the cursor moves to the end of the data
			((CEdit *)GetDlgItem(IDC_CORREL_ID))->SetSel(MQ_CORREL_ID_LENGTH, MQ_CORREL_ID_LENGTH);
		}
	}

	// update the correlation id data area
	setCorrelId(m_id_disp_ascii);

	// update the original correlation id
	OrigCorrelid = m_mqmd_correl_id;

	// update the form data from the instance variables
	UpdateData(FALSE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE:OnUpdateCorrelId() m_mqmd_correl_id=%s len=%d OrigCorrelid=%s len=%d", (LPCTSTR)m_mqmd_correl_id, m_mqmd_correl_id.GetLength(), (LPCTSTR)OrigCorrelid, OrigCorrelid.GetLength());

		// trace exit from OnUpdateCorrelId
		pDoc->logTraceEntry(traceInfo);
	}
}

void MQMDPAGE::OnChangeCorrelId() 

{
	char	traceInfo[512];		// work variable to build trace message
	
	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::OnChangeCorrelId() m_mqmd_correl_id=%s len=%d OrigCorrelid=%s len=%d", (LPCTSTR)m_mqmd_correl_id, m_mqmd_correl_id.GetLength(), (LPCTSTR)OrigCorrelid, OrigCorrelid.GetLength());

		// trace entry to OnUpdateCorrelId
		pDoc->logTraceEntry(traceInfo);
	}
}

void MQMDPAGE::OnSetfocusGroupId() 

{
	char	traceInfo[512];		// work variable to build trace message
	
	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::OnSetfocusGroupId() m_mqmd_group_id=%s len=%d OrigGroupid=%s len=%d", (LPCTSTR)m_mqmd_group_id, m_mqmd_group_id.GetLength(), (LPCTSTR)OrigGroupid, OrigGroupid.GetLength());

		// trace entry to OnSetfocusGroupId
		pDoc->logTraceEntry(traceInfo);
	}

	// remember the original values of the correl id and group id
	// in case they are edited by the user
	OrigGroupid = m_mqmd_group_id;

	// set the group id custom edit control hexonly option to match the id display
	if (ID_DISPLAY_HEX == m_id_disp_ascii)
	{
		m_GroupIdEdit.SetHexOnly(TRUE);
	}
	else
	{
		m_GroupIdEdit.SetHexOnly(FALSE);
	}

	// place the cursor at the beginning of the edit box
	m_GroupIdEdit.SetSel(0, 0);
}

void MQMDPAGE::OnKillfocusGroupId() 

{
	// Focus is leaving this control
	char	traceInfo[512];		// work variable to build trace message
	
	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::OnKillfocusGroupId() m_mqmd_group_id=%s len=%d OrigGroupid=%s len=%d", (LPCTSTR)m_mqmd_group_id, m_mqmd_group_id.GetLength(), (LPCTSTR)OrigGroupid, OrigGroupid.GetLength());

		// trace entry to OnKillfocusGroupId
		pDoc->logTraceEntry(traceInfo);
	}
}

//////////////////////////////////////////////////////
//
// This routine is called after a paste operation has
// been performed.  It will make sure the total length
// is still correct (either 24 or 48 characters).  It
// will also convert any invalid hex characters to
// zeros and convert any lower case letters to upper
// case.
//
// The input id parameter is a dialog id.
//
//////////////////////////////////////////////////////

void MQMDPAGE::editIDField(int id)

{
	int		i;
	int		maxHexLen=MQ_CORREL_ID_LENGTH*2;
	char	traceInfo[512];		// work variable to build trace message
	char	ch;

	if (pDoc->traceEnabled)
	{
		// create the trace line
		switch (id)
		{
			case IDC_CORREL_ID:
			{
				sprintf(traceInfo, "Entering MQMDPAGE::editIDField() id=%d m_mqmd_correl_id=%s", id, (LPCTSTR)m_mqmd_correl_id);
				break;
			}
			case IDC_GROUP_ID:
			{
				sprintf(traceInfo, "Entering MQMDPAGE::editIDField() id=%d m_mqmd_group_id=%s", id, (LPCTSTR)m_mqmd_group_id);
				break;
			}
			case IDC_MSG_ID:
			{
				sprintf(traceInfo, "Entering MQMDPAGE::editIDField() id=%d m_msg_id=%s", id, (LPCTSTR)m_msg_id);
				break;
			}
			default:
			{
				sprintf(traceInfo, "*****Entering MQMDPAGE::editIDField() invalid id=%d", id);
				break;
			}
		}

		// trace entry to editIDField
		pDoc->logTraceEntry(traceInfo);
	}

	// figure out which id we are working with
	switch (id)
	{
		case IDC_CORREL_ID:
		{
			if (ID_DISPLAY_HEX == m_id_disp_ascii)
			{
				// start by checking the length
				// This determines if the user has shortened the text, in which case zeros will be appended to 
				// the end of the data until the length is 48.
				// append binary zeros to the end of the data
				while (m_mqmd_correl_id.GetLength() < maxHexLen)
				{
					m_mqmd_correl_id += "0";
				}

				// now check if the text is now too long
				if (m_mqmd_correl_id.GetLength() > maxHexLen)
				{
					m_mqmd_group_id.SetAt(maxHexLen, 0);
				}

				// set all characters to upper case
				m_mqmd_correl_id.MakeUpper();

				// finally edit the text, replacing any invalid hex characters with zeros
				for (i=0; i<maxHexLen; i++)
				{
					ch = m_mqmd_correl_id.GetAt(i);
					if ((ch < '0') || (ch > '9'))
					{
						if ((ch < 'A') || (ch > 'F'))
						{
							m_mqmd_correl_id.SetAt(i, '0');
						}
					}
				}
			}
			else
			{
				// This determines if the user has shortened the text, in which case zeros will be appended to 
				// the end of the data until the length is 24.
				// append binary zeros to the end of the data
				while (m_mqmd_correl_id.GetLength() < MQ_CORREL_ID_LENGTH)
				{
					m_mqmd_correl_id += " ";
				}

				// now check if the text is now too long
				if (m_mqmd_correl_id.GetLength() > MQ_CORREL_ID_LENGTH)
				{
					m_mqmd_group_id.SetAt(MQ_CORREL_ID_LENGTH, 0);
				}
			}

			// finally update the actual correlation id area
			setCorrelId(m_id_disp_ascii);

			break;
		}
		case IDC_GROUP_ID:
		{
			break;
		}
		case IDC_MSG_ID:
		{
			break;
		}
		default:
		{
			// handle invalid requests by simply returning
			return;
		}
	}

	if (pDoc->traceEnabled)
	{
		// create the trace line
		switch (id)
		{
			case IDC_CORREL_ID:
			{
				sprintf(traceInfo, "Exiting MQMDPAGE::editIDField() m_mqmd_correl_id=%s", (LPCTSTR)m_mqmd_correl_id);
				break;
			}
			case IDC_GROUP_ID:
			{
				sprintf(traceInfo, "Exiting MQMDPAGE::editIDField() m_mqmd_group_id=%s", (LPCTSTR)m_mqmd_group_id);
				break;
			}
			case IDC_MSG_ID:
			{
				sprintf(traceInfo, "Exiting MQMDPAGE::editIDField() m_msg_id=%s", (LPCTSTR)m_msg_id);
				break;
			}
		}

		// trace exit from editIDField
		pDoc->logTraceEntry(traceInfo);
	}
}

/////////////////////////////////////////////////////////////
//
// This routine is used to the directly set message id field
// in the MQMD.  This is used when performing a get by
// message id operation.
//
/////////////////////////////////////////////////////////////

void MQMDPAGE::setMsgId(char *msgId)

{
	// set the message id in the MQMD object
	memcpy(m_message_id, msgId, MQ_MSG_ID_LENGTH);
}

void MQMDPAGE::OnSetfocusMsgId() 

{
	char	traceInfo[512];		// work variable to build trace message
	
	// update the form data from the instance variables
	UpdateData(FALSE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::OnSetfocusMsgId() m_msg_id=%s len=%d", (LPCTSTR)m_msg_id, m_msg_id.GetLength());

		// trace entry to OnSetfocusCorrelId
		pDoc->logTraceEntry(traceInfo);
	}

	// remember the original values of the message id
	// in case they are edited by the user
	OrigMsgid = m_msg_id;

	// set the group id custom edit control hexonly option to match the id display
	if (ID_DISPLAY_HEX == m_id_disp_ascii)
	{
		// Only accept hex characters
		m_MsgIdEdit.SetHexOnly(TRUE);
	}
	else
	{
		// normal characters allowed
		m_MsgIdEdit.SetHexOnly(FALSE);
	}

	// place the cursor at the beginning of the edit box
	m_MsgIdEdit.SetSel(0, 0);
}

void MQMDPAGE::OnKillfocusMsgId() 

{
	// Focus is leaving this control
	char	traceInfo[512];		// work variable to build trace message
	
	// make sure that we have the current values in the instance variables
	UpdateData(TRUE);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Entering MQMDPAGE::OnKillfocusMsgId() m_msg_id=%s len=%d", (LPCTSTR)m_msg_id, m_msg_id.GetLength());

		// trace entry to OnKillfocusMsgId
		pDoc->logTraceEntry(traceInfo);
	}

	setMsgId(m_id_disp_ascii);
}

/////////////////////////////////////////////
//
// Routine to set the message id.
// The format can be in ascii or ebcdic, 
// depending on the charType value.
//
/////////////////////////////////////////////

void MQMDPAGE::setMsgId(int dispType)

{
	std::string	error;

	// the edited text of the box becomes the binary id; a rejected edit leaves it unchanged
	rfh::id_display::applyEdit(rfh::id_display::Field::MsgId, m_message_id, (LPCTSTR)m_msg_id, (LPCTSTR)OrigMsgid, (rfh::id_display::Mode)dispType, error);
}

void MQMDPAGE::setUserId(MQMD2 * mqmd)

{
	int		len;		// length of the user id field

	len = m_mqmd_userid.GetLength();

	// check if the length is greater than the maximum allowed
	if (len > MQ_USER_ID_LENGTH)
	{
		len = MQ_USER_ID_LENGTH;
	}

	// initialize the user id field to binary zeros
	memset(mqmd->UserIdentifier, 0, MQ_USER_ID_LENGTH);

	// is there anything to copy?
	if (len > 0)
	{
		// copy the user id from the 
		memcpy(mqmd->UserIdentifier, (LPCTSTR)m_mqmd_userid, len);
	}
}

void MQMDPAGE::setMsgId(MQMD2 *mqmd)

{
	// copy the message id to the mqmd
	memcpy(mqmd->MsgId, m_message_id, MQ_MSG_ID_LENGTH);
}

void MQMDPAGE::setCorrelId(MQMD2 *mqmd)
{
	// copy the correlation id to the mqmd
	memcpy(mqmd->CorrelId, m_correlid, MQ_CORREL_ID_LENGTH);
}

void MQMDPAGE::setGroupId(MQMD2 *mqmd)
{
	// copy the group id to the mqmd
	memcpy(mqmd->GroupId, m_group_id, MQ_GROUP_ID_LENGTH);
}

/////////////////////////////////////////////////////////////////
//
// This routine will capture data in an MQMD extension header.
// RFHUtil will never generate an MQMDE header.  However it
// will process one if it is present in the input message data.
//
/////////////////////////////////////////////////////////////////

int MQMDPAGE::processMQMDE(MQMDE *mqmde, int size, int ccsid, int encoding)

{
	int		flags;
	char	traceInfo[512];		// work variable to build trace message

	// check that there is enough data to be an MQMDE
	if (size < MQMDE_LENGTH_2)
	{
		// return zero length
		return 0;
	}

	// capture the additional fields in an MQMD extension header
	memcpy(m_group_id, mqmde->GroupId, MQ_GROUP_ID_LENGTH);

	// check the encoding of the data
	if (getIntEncode(encoding) != NUMERIC_PC)
	{
		mqmde->MsgSeqNumber = reverseBytes4(mqmde->MsgSeqNumber);
		mqmde->MsgFlags = reverseBytes4(mqmde->MsgFlags);
		mqmde->Offset = reverseBytes4(mqmde->Offset);
		mqmde->OriginalLength = reverseBytes4(mqmde->OriginalLength);
		mqmde->CodedCharSetId = reverseBytes4(mqmde->CodedCharSetId);
		mqmde->Encoding = reverseBytes4(mqmde->Encoding);
	}

	// get the group sequence number
	m_mqmd_seq_no.Format("%d", mqmde->MsgSeqNumber);

	// get the message flags and process them
	flags = mqmde->MsgFlags;
	processFlags(flags);

	// get the offset
	m_mqmd_offset.Format("%d", mqmde->Offset);

	// get the original length field
	m_mqmd_orig_len.Format("%d", mqmde->OriginalLength);

	if (pDoc->traceEnabled)
	{
		// create the trace line
		sprintf(traceInfo, "Exiting MQMDPAGE::processMQMDE() m_mqmd_seq_no=%s flags=%d m_mqmd_offset=%s m_mqmd_orig_len=%d", (LPCTSTR)m_mqmd_seq_no, flags, (LPCTSTR)m_mqmd_offset, mqmde->OriginalLength);

		// trace exit from processMQMDE
		pDoc->logTraceEntry(traceInfo);
	}

	return MQMDE_LENGTH_2;
}
