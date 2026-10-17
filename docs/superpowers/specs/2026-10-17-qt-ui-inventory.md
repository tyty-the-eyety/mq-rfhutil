# RFHUtil Windows (MFC) UI inventory for the Qt6 port

Date: 2026-10-17. Source: `RFHUtil/` at branch `features/linux-qt-port` (read-only inspection; nothing was compiled or run, so
nothing here was observed on a running Windows build). Every fact carries a resource ID or `file:line`; "UNVERIFIED" marks
inferences, "AMBIGUOUS" marks things the source does not settle. Line numbers drift: re-find with `tools/index/q sym NAME`.

Conventions. `rc` = `RFHUtil/rfhutil.rc`. Dialog units (DLU): the pages are 530 x 380 DLU, font `MS Sans Serif` 8 (rc:297, 372, 489).
"Tab order" = order of the controls in the dialog template (rc), which is the Windows tab/z order; `Tab` column: `Y` = tab stop,
`N` = `NOT WS_TABSTOP` in the rc, blank = no explicit stop (the Main and Data radios: only the first of a group has `WS_TABSTOP`; the MQMD radios all have it).
PUSHBUTTON and EDITTEXT are tab stops by default in the rc compiler; CONTROL and COMBOBOX need `WS_TABSTOP` explicitly.
`Alt` = the mnemonic letter (`&`) in the caption. Constants come from `RFHUtil/rfhconst.h` unless stated.
`General` is the class of the Main tab, `MSGDATA` of the Data tab, `MQMDPAGE` of the MQMD tab, `DataArea` is the document
(`DataArea.cpp`, 25.8k lines).

## 1. Main window and tab list

### 1.1 Window

| Item | Value | Source |
|---|---|---|
| Frame class | `CMainFrame : CFrameWnd`; one view `CRfhutilView : CCtrlView` wrapping a `SysTabControl32`; every page is a modeless child dialog created with `Create(IDD_x, this)` and shown/hidden on tab change | MainFrm.cpp:63; rfhutilView.cpp:160 (ctor), 543-690 (`OnInitialUpdate`) |
| Title | `"RfhUtil V%d.%d.%d.%d"` (major.minor.revision.build); `" (Client - Readonly)"` appended in a `SAFE_MODE` build, `" (Client)"` in an `MQCLIENT` build | MainFrm.cpp:92-102 |
| Title after use | Set to the bare queue name after Read Q, Browse Q, Start Browse (on success), Purge Q (after Yes) and a successful Write Q (`DataArea::setWindowTitle`) | General.cpp:327, 411, 813, 918; DataArea.cpp:12630 |
| Initial size | `cs.cx = 850`, `cs.cy = 450` (pixels) in `PreCreateWindow`; `General::OnInitDialog` then resizes the frame to the Main page client size plus menu height and border. Final pixel size is not determinable from source | MainFrm.cpp:74-75; General.cpp:1117-1140 (AMBIGUOUS) |
| Style | `FWS_ADDTOTITLE` removed, `WS_SYSMENU` added; no toolbar, no status bar (no `CStatusBar`/`CToolBar` in `MainFrm.*`) | MainFrm.cpp:89-90 |
| Resizing | `CRfhutilView::OnSize` moves every page to the tab client rectangle (top = tab item bottom + 4); page controls do not rescale | rfhutilView.cpp:692-747 |
| Status line | There is none outside the pages. The "status line" is the read-only multiline edit `IDC_ERRMSG` on the Main tab (see 2.1) | rc:297 block |
| System menu | Only the standard MFC one (`WS_SYSMENU`); no custom system-menu commands in source (grep `AppendMenu`/`InsertMenu` finds none in the frame) | UNVERIFIED by running |

There are no common buttons outside the pages. `Exit` is `IDC_EXIT_BUTTON` on the Main page; `File > Exit` is `ID_APP_EXIT`;
`Alt+F4` is bound to `ID_APP_EXIT` in the accelerator table (rc:171-212, `VK_F4` at rc:192).

### 1.2 Tabs (insertion order = display order; rfhutilView.cpp:599-659)

| # | Caption (exact) | Page class (member) | Dialog | `PAGE_*` | Phase 1 (spec section 5) |
|---|---|---|---|---|---|
| 0 | `Main` | `General` (`m_general`) | `IDD_MAIN` | `PAGE_MAIN` 0 | enabled |
| 1 | `Data` | `MSGDATA` (`m_data`) | `IDD_DATA` | `PAGE_DATA` 1 | enabled |
| 2 | `MQMD` | `MQMDPAGE` (`m_mqmd`) | `IDD_MQMD` | `PAGE_MQMD` 2 | enabled |
| 3 | `PS` | `CPS` (`m_ps`) | `IDD_V7PUBSUB` | `PAGE_V7PUBSUB` 3 | disabled, tooltip "Not yet ported" |
| 4 | `Usr Prop` | `CProps` (`m_props`) | `IDD_V7PROPS` | `PAGE_V7PROPS` 4 | disabled |
| 5 | `RFH` | `RFH` (`m_rfh`) | `IDD_RFH` | `PAGE_RFH` 5 | disabled |
| 6 | `PubSub` | `PubSub` (`m_pubsub`) | `IDD_PUB` | `PAGE_PUBSUB` 6 | disabled |
| 7 | `pscr` | `pscr` (`m_pscr`) | `IDD_PSCR` | `PAGE_PSCR` 7 | disabled |
| 8 | `jms` | `jms` (`m_jms`) | `IDD_JMS` | `PAGE_JMS` 8 | disabled |
| 9 | `usr` | `Usr` (`m_usr`) | `IDD_USR` | `PAGE_USR` 9 | disabled |
| 10 | `other` | `other` (`m_other`) | `IDD_OTHER` | `PAGE_OTHER` 10 | disabled |
| 11 | `CICS` | `CCICS` (`m_cics`) | `IDD_CICS` | `PAGE_CICS` 11 | disabled |
| 12 | `IMS` | `CIms` (`m_ims`) | `IDD_IMS` | `PAGE_IMS` 12 | disabled |
| 13 | `DLQ` | `CDlq` (`m_dlq`) | `IDD_DLQ` | `PAGE_DLQ` 13 | disabled |
| 14 | `Conn` | `ConnSettings` (`m_conn_settings`) | `IDD_CONN_SETTINGS` | `PAGE_CONN` 14 | disabled |

Notes. Captions `pscr`, `jms`, `usr`, `other` are lower case in the source. `Alt+N` / `Alt+P` (`IDM_NEXT_TAB` / `IDM_PREV_TAB`) step
through the tabs and wrap from `DLQ` back to `Main`, skipping `Conn` (rfhutilView.cpp:2856-2965; `if (PAGE_DLQ == sel) sel = 0`).
Switching tabs calls `KillActive`/`SetActive` on the pages; a tab change is refused while a Find or Find Hex dialog is open
(rfhutilView.cpp:749-780). The Data tab re-renders its text on every activation (`MSGDATA::OnSetActive` -> `updateMsgdata`, MSGDATA.cpp:483);
the MQMD tab rebuilds the id edit boxes from the binary ids and re-applies the read-only rules (MQMDPAGE.cpp:396-455).

## 2. Controls of the Main, MQMD and Data tabs

Columns: `#` order in the template (tab order; statics and group boxes are not listed but see the label lists; the buttons of one radio group are merged into one row, IDs and tooltips in group order) | resource ID(s) |
type | caption verbatim (`&` = mnemonic) | group box (by rectangle containment) | Alt | Tab | rc flags | member (DDX, `m_...`) |
default | tooltip. The tooltip of every control is the STRINGTABLE entry with the same ID (`GetToolTipText` returns
`MAKEINTRESOURCE(nID)`, General.cpp:1039-1073, MQMDPAGE.cpp:815, MSGDATA.cpp:887); texts are in rc:1590-1800. Types: `edit`, `combo`,
`check`, `radio`, `button`. Defaults are the constructor/`OnInitDialog` values; "-" = none.

### 2.1 Main tab (`IDD_MAIN`, class `General`, DDX at General.cpp:172-206, ctor General.cpp:52-80)

Static labels (no ID): `Queue Manager Name (to connect to)`, `Queue Name`, `Remote Queue Manager Name (remote queues only)`,
`Selector` (ID `IDC_MAIN_FILTER_LABEL`), `File Code Page`, `File Name`, `Data Size`, `COBOL Copy Book File Name`,
`Queue Type`, `Queue depth`. Group boxes: `Put/Get Options`, `User Props`, `Cluster Open`, `Close Options`.

DDX limits: `m_q_name` max 255, `m_remote_qm` max 48, `m_file_codepage` max 5 (`DDV_MaxChars`). The QM combo is limited to
`MQ_Q_NAME_LENGTH` (48) characters in a bindings build (`LimitText`, General.cpp:471, 660, 1733) and not limited that way (255 per the
comment at General.cpp:1721-1731) in a client build; the Qt app has one binary, so the limit must follow the connection mode.
Combos `IDC_QM` and `IDC_Q_NAME` are subclassed `MyComboBox` (auto-complete) (General.cpp:1165).

|#|ID|type|caption|group|Alt|Tab|flags|member|default|tooltip|
|---|---|---|---|---|---|---|---|---|---|---|
|1|IDC_QM|combo||||Y|sorted,editable-dropdown|m_qm_name|initQMname|Name of the queue manager to connect to|
|2|IDC_Q_NAME|combo||||Y|sorted,editable-dropdown|m_q_name|initQname|Name of the queue to get or put a message from or to|
|3|IDC_REMOTE_QM|edit||||Y||m_remote_qm|initRemoteQMname|Name of the remote queue manager where the queue resides|
|4|IDC_MAIN_FILTER|edit||||Y||m_filter||Messages must match to be read, browsed or displayed.|
|5|IDC_MAIN_FILE_CODEPAGE|edit||||Y|digits|m_file_codepage|"437"|Code page to assume when reading a file|
|6|IDC_NEW_MSG_ID|check|New Msg Id|Put/Get Options||Y||m_new_msg_id|checked|Create new message id when putting a message|
|7|IDC_GET_MSG_ID|check|Get by Msg Id|Put/Get Options||Y||m_get_by_msgid||Get by Message Id (Set on MQMD page)|
|8|IDC_GET_BY_CORRELID|check|Get by Correlid|Put/Get Options||Y||m_get_by_correlid||Read message with matching correlation id (set on MQMD tab)|
|9|IDC_GET_BY_GROUP_ID|check|Get by Group Id|Put/Get Options||Y||m_get_by_groupid||Read message with matching group id (set on MQMD tab)|
|10|IDC_SET_USERID|check|Set Iden Context|Put/Get Options||Y||m_set_UserID||Allow user id and application identifier to be set on writes|
|11|IDC_SET_ALL|check|Set All Context|Put/Get Options||Y||m_set_all||Set all MQMD context fields when writing to queue|
|12|IDC_NEW_CORREL_ID|check|New Correl Id|Put/Get Options||Y||m_new_correl_id||Create new correlation id when putting a message|
|13|IDC_LOGICAL_ORDER|check|Logical Order|Put/Get Options||Y||m_logical_order||Maintain logical order within group or segment|
|14|IDC_COMPLETE_MSG|check|Complete Msg|Put/Get Options||Y||m_complete_msg||Assemble any message segments into a single logical message|
|15|IDC_ALL_AVAIL|check|All Avail|Put/Get Options||Y||m_all_avail||All segments and msgs in group must be available|
|16|IDC_MAIN_CONVERT|check|Convert|Put/Get Options||Y||m_convert||Use convert option on read and browse operations|
|17|IDC_ALT_USERID|check|Alternate User Id|Put/Get Options||Y||m_alt_userid||Use alternate userid in MQMD user id field|
|18|IDC_MAIN_PROPS_AS_QUEUE, IDC_MAIN_PROPS_NONE, IDC_MAIN_PROPS_YES, IDC_MAIN_PROPS_RFH2, IDC_MAIN_PROPS_COMPAT|radio|As Queue / None / Yes / RFH2 / Compat|User Props||Y||m_mq_props|selected (0)|Use the queue definition to convert message properties when reading a message / Ignore/discard any MQ user properties associated with the message / Use MQ user properties when putting or getting a message / Convert any MQ user properties to RFH2 folders / Compatibility mode for user properties|
|19|IDC_BIND_AS_Q, IDC_BIND_ON_OPEN, IDC_BIND_NOT_FIXED, IDC_BIND_GROUP|radio|As Queue / Bind Open / Not Fixed / Group|Cluster Open||Y||m_cluster_bind|selected (0)|Use bind as queue option when writing messages to queue / Send all writes to a cluster queue to the same physical queue / Spread writes to cluster queues among all instances / Send all messages in a logical group to same cluster queue manager|
|20|IDC_CLOSE_NONE, IDC_CLOSE_DELETE, IDC_CLOSE_PURGE|radio|None / Delete / Purge|Close Options||Y||m_close_options|selected (0)|Normal close / If temporary dynamic queue delete queue after close / If temporary dynamic queue purge messages and delete queue after close|
|21|IDC_READQ|button|Read &Q||Q|Y||||Read the next message from the specified queue|
|22|IDC_WRITEQ|button|&Write Q||W|Y||||Write a message to the queue|
|23|IDC_BROWSE|button|&Browse Q||B|Y||||Read the first message in the queue and leave it in the queue|
|24|IDC_STARTBR|button|S&tart Browse||T|Y||||Start a browse operation on a queue, reading the first message|
|25|IDC_BRNEXT|button|Browse &Next||N|Y|disabled||disabled|Read next message in queue using current browse operation|
|26|IDC_BRPREV|button|Browse P&rev||R|Y|disabled||disabled|Browse previous message|
|27|IDC_ENDBR|button|End Browse|||Y|disabled||disabled|End the currently active browse operation (Ctrl+M)|
|28|IDC_CLOSEQ|button|Close Q|||Y|disabled||disabled|Close the queue that is currently open (Ctrl+K)|
|29|IDC_MAIN_SAVEQ|button|Save Q|||Y||||Save messages from queue into one or more files (Ctrl+Y)|
|30|IDC_MAIN_LOADQ|button|&Load Q||L|Y||||Load messages from files into a queue|
|31|IDC_MAIN_MOVEQ|button|Move Q|||Y||||Move messages to another queue|
|32|IDC_PURGE|button|Purg&e Q||E|Y||||Read and discard all messages in the specified queue|
|33|IDC_MAIN_DISPLAYQ|button|&Display Q||D|Y||||Display contents of queue|
|34|ID_READ_FILE|button|&Open File||O|Y||||Read data from a file|
|35|IDC_WRITE_FILE|button|&Save File||S|Y||||Write current data buffer (and RFH if included) to a file|
|36|IDC_CLEAR_DATA|button|Clear Data|||Y||||Discard the current data buffer contents (Alt+X)|
|37|IDC_CLEAR_ALL|button|Clear All|||Y||||Clear the current data buffer and all other fields (MQMD, etc) (Alt+Y)|
|38|IDC_LOAD_QUEUE_NAMES|button|Load Names|||Y||||Load/Refresh queue name information into drop down list (Ctrl+H)|
|39|IDC_SET_CONN_USER|button|Set Conn &Id||I|Y||||Set MQ connection parameters (user/pwd and SSL)|
|40|IDC_FILE_NAME|edit||||N|readonly|m_file_name||Name of the file that data was read from|
|41|IDC_FILE_SIZE|edit||||N|readonly|m_file_size||Length of the data in the current data buffer (in bytes)|
|42|IDC_COPYBOOK_FILE_NAME|edit||||N|readonly|m_copybook_file_name||Name of the current COBOL copy book to map buffer to|
|43|IDC_ERRMSG|edit||||N|readonly,multiline|m_errmsg||Results of read, write and browse operations|
|44|IDC_Q_DEPTH|edit||||N|readonly|m_q_depth||Depth of the queue after the last operation|
|45|IDC_EXIT_BUTTON|button|Exit|||Y||||Exit from the program|
|46|IDC_QUEUE_TYPE|edit||||N|readonly|m_queue_type||Queue type (Local, remote, alias, etc)|

Radio groups (DDX_Radio value = zero-based position of the checked button in the template; -1 would mean none checked, but
every group below starts with button 0 checked):

| Group (member) | Buttons -> value |
|---|---|
| User Props (`m_mq_props`) | As Queue 0 `MQ_PROPS_AS_QUEUE`, None 1, Yes 2, RFH2 3, Compat 4 (DataArea.h:69-73) |
| Cluster Open (`m_cluster_bind`) | As Queue 0 `BIND_AS_Q`, Bind Open 1 `BIND_ON_OPEN`, Not Fixed 2 `BIND_NOT_FIXED`, Group 3 `BIND_GROUP` (rfhconst.h:54-57) |
| Close Options (`m_close_options`) | None 0 `Q_CLOSE_NONE`, Delete 1 `Q_CLOSE_DELETE`, Purge 2 `Q_CLOSE_PURGE` (DataArea.h:46-48) |

Initial state and enable rules.
- Startup values: QM, Queue, Remote QM come from the command line or the last run (`app->initQMname`, `initQname`, `initRemoteQMname`;
  General.cpp:1175-1178); File Code Page `"437"` (`DEF_ASCII_CCSID`, General.cpp:1208); `New Msg Id` is checked (General.cpp:1295);
  the properties radio is forced to As Queue only when the queue manager library supports properties (`propertiesSupported`,
  General.cpp:1098-1108). `Selector` is read-only when `!propertiesSupported` (MQ client/server older than V7, General.cpp:1304).
- `General::UpdatePageData` (General.cpp:504-620) is the only enable/disable logic and runs after every operation:
  - `browseActive == 1`: DISABLED are Open File, Clear Data, Clear All, Read Q, Purge Q, Write Q, Browse Q, Start Browse, Close Q,
    Load Names, Display Q, Load Q, Save Q, Move Q, Logical Order, Complete Msg, All Avail, Convert, and all five User Props radios.
    ENABLED are Browse Next and End Browse; Browse Prev only while `browsePrevActive == 1`.
  - otherwise: all of those are enabled except Browse Next / Browse Prev / End Browse (disabled), and Close Q is enabled only while
    a connection is open (`isConnectionActive()`).
  - Never changed: Save File, Set Conn Id, Exit, the text fields and the other Put/Get checkboxes (New Msg Id, Get by Msg Id,
    Get by Correlid, Get by Group Id, Set Iden Context, Set All Context, New Correl Id, Alternate User Id) and the Cluster Open and
    Close Options radios.
  - `SAFE_MODE` build only: Write Q, Load Q, Move Q, Purge Q, Clear All and Read Q are hidden (General.cpp:1243-1290). Not ported.
- Read-only edits: `IDC_FILE_NAME`, `IDC_FILE_SIZE`, `IDC_COPYBOOK_FILE_NAME`, `IDC_ERRMSG`, `IDC_Q_DEPTH`, `IDC_QUEUE_TYPE`.
  `IDC_ERRMSG` has vertical and horizontal scroll bars (`WS_VSCROLL | WS_HSCROLL`), 265 x 77 DLU.
- Values shown after each operation (`UpdatePageData`): File Name `pDoc->fileName`; Data Size `%d` of `fileSize`; Queue depth `%d`
  of `m_q_depth` or empty when negative; Queue Type `m_queue_type` ("Local", "Model", "Alias", "Remote", "Cluster", DataArea.cpp:10354-);
  copy book name; the status text `m_msg_text`; `Set Iden Context` is cleared (`m_set_UserID = FALSE`) after Read Q, Browse Q,
  Browse Next/Prev, Purge Q (General.cpp:315, 405, 732, 770, 923).
- Dropdown behaviour. `IDC_QM` fills on `CBN_DROPDOWN` (`loadQMComboBox`: in a bindings build only queue managers already seen this session, i.e. those Load Names was run for; in a client build `MQSERVER`,
  channel-table entries and saved names; DataArea.cpp:25392-25611; there is no enumeration of local queue managers, see G1). `IDC_Q_NAME` fills from a cached name list
  per queue manager on dropdown and after a QM selection (`OnDropdownQName`, General.cpp:1492; list filled by Load Names); entries
  starting `SYSTEM.` are hidden unless `View > System Queues` (`m_show_system_queues`). Escape/cancel restores the previous text
  (`OnSelendcancelQm`/`QName`). Enter in either edit sends `VK_FINAL` to the combo (PreTranslateMessage, General.cpp:1411-1490).
- Edit rules: `IDC_MAIN_FILE_CODEPAGE` digits only; each change and kill-focus copies `atoi()` to `pDoc->m_file_codepage`
  (General.cpp:2666-2684; empty or 0 means "decide from the data"). Backspace in `IDC_MAIN_FILTER` goes through `processBackspace`
  (General.cpp:1434; MFC-only, not needed in Qt).
- The mapping of the DDX members to the document happens in `OnKillActive` (General.cpp:656-687: QM, queue, remote QM,
  `m_mq_props`, filter, `m_setUserID`, `m_get_by_msgid`, `m_set_all`, `m_alt_userid`) and inside each button handler (3.1).
  `m_new_msg_id` / `m_new_correl_id` are mirrored on click (General.cpp:2560-2574); `m_bind_option` and `m_close_option` on click
  (General.cpp:824-864, 2686-2727).
- Windows defect to preserve or fix on purpose: there is no handler for `IDC_BIND_GROUP`, so choosing `Group` does not update
  `pDoc->m_bind_option` until another Cluster Open radio was clicked first; the Group value reaches `openQ` only if `m_bind_option`
  was set by an earlier click (General.cpp message map 209-274 has `OnBindAsQ`, `OnBindNotFixed`, `OnBindOnOpen` only;
  DataArea.cpp:11926-11940 reads `m_bind_option`). Core `BindOption::OnGroup` exists; wire all four radios.

### 2.2 MQMD tab (`IDD_MQMD`, class `MQMDPAGE`, DDX at MQMDPAGE.cpp:122-187, ctor 52-116)

Static labels (no ID): `Code Page`, `MQ Message Format`, `Message ID`, `Correl ID`, `Reply To Queue`, `Reply To Queue Manager`,
`Application Identity`, `Appl Origin`, `Appl Type`, `Put Application Name`, `Backout Count`, `Group Id`, `User Id`, `Seq No`,
`Accounting Token`, `Put Date/Time`, `Expiry`, `MsgType`, `Offset`, `Orig Len`, `Feedback`, `Priority`, and the row labels
`Except`, `Expire`, `COA`, `COD` inside Report Options. Group boxes: `Int Fmt`, `PD Fmt`, `Float Fmt`, `Persistent Msg`, `Id Display`,
`Group`, `Segment`, `Report Options`. The MQMD tab has no `&` mnemonics at all. (The Priority edit's ID is spelled `IDC_MQMD_PRIOTITY` in the source.)

DDX limits (`DDV_MaxChars`): correl id 48, ccsid 5, format 8, msg id 48, reply QM 48, reply queue 48, appl identity 32, appl origin 4,
appl name 28, user id 12, group id 48, account token 64, expiry 9, msgtype 10, orig len 9, feedback 8, priority 8. (Msg/correl/group id
48 is the hex length; in ASCII/EBCDIC display they are limited to 24, see 4.2.) `IDC_MSGTYPE` is additionally `LimitText(11)` (MQMDPAGE.cpp:792).

|#|ID|type|caption|group|Alt|Tab|flags|member|default|tooltip|
|---|---|---|---|---|---|---|---|---|---|---|
|1|IDC_MQMD_FORMAT|combo||||Y|editable-dropdown|m_mqmd_format||Format field in the MQ message descriptor (8 chars)|
|2|IDC_USER_ID|edit||||Y|readonly|m_mqmd_userid||User id field from the MQ message descriptor (12 chars)|
|3|IDC_MQMD_CCSID|edit||||Y|digits|m_mqmd_ccsid||Code page of either RFH if present or user data|
|4|IDC_BACKOUT_CNT|edit||||N|readonly|m_mqmd_backout_count|"0"|Backout count from the last message read|
|5|IDC_MQMD_PRIOTITY|edit||||Y||m_mqmd_priority||MQSeries message priority|
|6|IDC_MQMD_ORIG_LEN|edit||||Y||m_mqmd_orig_len|"-1" after clear|Original length field in MQMD|
|7|IDC_PUT_DATE_TIME|edit||||Y|readonly|m_mqmd_date_time||Date and time the message was originally written|
|8|IDC_EXPIRY|edit||||Y|digits|m_mqmd_expiry||Time interval until message is considered expired|
|9|IDC_MSGTYPE|combo||||Y|sorted,editable-dropdown|m_mqmd_msgtype||Type of system that created the message|
|10|IDC_MQMD_FEEDBACK|edit||||Y||m_mqmd_feedback||Feedback field from last MQ request|
|11|IDC_MQMD_PC|radio|PC|Int Fmt||Y||m_mqmd_encoding|selected (0)|Integer values in PC (reversed) format|
|12|IDC_MQMD_HOST|radio|Host|Int Fmt||Y||||Integer data is in host format|
|13|IDC_MQMD_PD_PC|radio|PC|PD Fmt||Y||m_mqmd_pd_encoding|selected (0)|Format of decimal data is PC (reversed)|
|14|IDC_MQMD_PD_HOST|radio|Host|PD Fmt||Y||||Format of decimal data is host|
|15|IDC_MQMD_FLOAT_PC|radio|PC|Float Fmt||Y||m_mqmd_float_encoding|none (-1)|Floating point numbers are in PC (Intel) format|
|16|IDC_MQMD_FLOAT_UNIX|radio|Unix|Float Fmt||Y||||Floating point numbers are in Unix format (Big-Endian IEEE)|
|17|IDC_MQMD_FLOAT_390|radio|390|Float Fmt||Y||||Floating point numbers are in 390 format|
|18|IDC_MQMD_FLOAT_TNS|radio|TNS|Float Fmt||Y||||Floating point numbers are in TNS format|
|19|IDC_MSG_ID|edit||||Y||m_msg_id||MQ message id (24 bytes) of last message read|
|20|IDC_CORREL_ID|edit||||Y||m_mqmd_correl_id||MQ correlation id (24 bytes)|
|21|IDC_OFFSET|edit||||Y|digits|m_mqmd_offset|"0"|Offset of data segment within logical message|
|22|IDC_GROUP_ID|edit||||Y||m_mqmd_group_id||MQ group id (24 bytes)|
|23|IDC_APPL_TYPE|combo||||Y|editable-dropdown|m_mqmd_appl_type||Application type field from MQ message descriptor|
|24|IDC_REPLY_QM|edit||||Y||m_reply_qm||Queue manager where reply to queue resides|
|25|IDC_REPLY_QUEUE|edit||||Y||m_reply_queue||Reply to queue name|
|26|IDC_PERSIST_NO|radio|No|Persistent Msg||Y||m_mqmd_persist|selected (0)|Message is non-persistent|
|27|IDC_PERSIST_YES|radio|Yes|Persistent Msg||||||Message is persistent|
|28|IDC_PERSIST_AS_QUEUE|radio|As Queue|Persistent Msg||||||Message will be persistent or not based on queue default|
|29|IDC_ID_DISP_ASCII|radio|Ascii|Id Display||Y||m_id_disp_ascii|Hex (2)|Display id fields as ASCII characters|
|30|IDC_ID_DISP_EBCDIC|radio|Ebcdic|Id Display||||||Display id fields as EBCDIC characters|
|31|IDC_ID_DISP_HEX|radio|Hex|Id Display||||||Display id fields in hex|
|32|IDC_GROUP_YES|check|Yes|Group||Y||m_mqmd_group_yes||Message is in a group (not last)|
|33|IDC_GROUP_LAST|check|Last|Group||||m_mqmd_group_last||Last message in a group|
|34|IDC_SEGMENT_YES|check|Yes|Segment||Y||m_mqmd_segment_yes||Message contains a segment of data|
|35|IDC_SEGMENT_LAST|check|Last|Segment||||m_mqmd_segment_last||Message contains last data segment|
|36|IDC_SEGMENT_ALLOWED|check|Allowed|Segment||||m_mqmd_segment_allowed||Segmentation of message by queue managers is allowed|
|37|IDC_REPORT_EXCEPT_NONE, IDC_REPORT_EXCEPT, IDC_REPORT_EXCEPT_DATA, IDC_REPORT_EXCEPT_FULL|radio|No / Yes / Data / Full|Report Options||Y||m_report_except|none (-1)|No exception report message requested; Request report message returned when exception occurs; Report message with 100 bytes data when exception occurs; Report message with all data returned when exception occurs|
|38|IDC_REPORT_EXPIRE_NONE, IDC_REPORT_EXPIRE, IDC_REPORT_EXPIRE_DATA, IDC_REPORT_EXPIRE_FULL|radio|No / Yes / Data / Full|Report Options||Y||m_report_expire|none (-1)|No report message requested if message expires; Report message requested if message expires; Report message with 100 bytes requested if message expires; Report message with all data requested if message expires|
|39|IDC_REPORT_COA_NONE, IDC_REPORT_COA, IDC_REPORT_COA_DATA, IDC_REPORT_COA_FULL|radio|No / Yes / Data / Full|Report Options||Y||m_report_coa|none (-1)|No report message upon message arrival at destination QM; Report message only when message arrives at destination QM; Report message with 100 bytes when message arrives at QM; Report message with all data when message arrives at QM|
|40|IDC_REPORT_COD_NONE, IDC_REPORT_COD, IDC_REPORT_COD_DATA, IDC_REPORT_COD_FULL|radio|No / Yes / Data / Full|Report Options||Y||m_report_cod|none (-1)|No report message requested when message is processed; Report message requested when message is processed; Report message with 100 bytes when message is processed; Report message with all data when message is processed|
|41|IDC_REPORT_PAN|check|PAN|Report Options||Y||m_report_pan||Positive action notification report message requested|
|42|IDC_REPORT_NAN|check|NAN|Report Options||Y||m_report_nan||Negative action notification report message requested|
|43|IDC_REPORT_ACTIVITY|check|Activity|Report Options||Y||m_report_activity||Send report from each channel that handles this message|
|44|IDC_REPORT_PASS_MSGID|check|Pass MsgId|Report Options||Y||m_report_pass_msgid||Use original message ID from request in report message|
|45|IDC_REPORT_PASS_CORRELID|check|Pass Correl|Report Options||Y||m_report_pass_correlid||Include correlation id of input message on report message|
|46|IDC_REPORT_DISCARD|check|Discard|Report Options||Y||m_report_discard||Discard message when arrives at destination|
|47|IDC_REPORT_EXPIRY_DISCARD|check|Discard/Expiry|Report Options||Y||m_report_expire_discard||Discard message upon arrival or expiry|
|48|IDC_RESET_IDS|button|Reset Ids|||Y||||Reset the correlation, message and group ids to binary zeros (Alt + J)|
|49|IDC_COPY_MSGID|button|Copy Msg Id to Correl Id|||Y||||Copy the message id to the correlation id (Alt + C)|
|50|IDC_APPL_IDENTITY|edit||||N|readonly|m_mqmd_appl_id||Application identify field from MQ message descriptor|
|51|IDC_APPL_ORIGIN|edit||||N|readonly|m_mqmd_appl_origin||Application origin field (4 characters)|
|52|IDC_APPL_NAME|edit||||N|readonly|m_mqmd_appl_name||Name of application that wrote the message|
|53|IDC_SEQNO|edit||||Y||m_mqmd_seq_no|"1"|Sequence number within group|
|54|IDC_ACCOUNT_TOKEN|edit||||N|readonly|m_mqmd_account_token||Accounting token field (system dependent)|

Radio groups (value = position in the template):

| Group (member) | Buttons -> value -> MQ value on put |
|---|---|
| Int Fmt (`m_mqmd_encoding`) | PC 0 `NUMERIC_PC` (MQENC_INTEGER_REVERSED), Host 1 `NUMERIC_HOST` (MQENC_INTEGER_NORMAL) (`getEncodingValue`, mqsubs.cpp:297) |
| PD Fmt (`m_mqmd_pd_encoding`) | PC 0 (MQENC_DECIMAL_REVERSED), Host 1 (MQENC_DECIMAL_NORMAL) |
| Float Fmt (`m_mqmd_float_encoding`) | PC 0 `FLOAT_PC` (MQENC_FLOAT_IEEE_REVERSED), Unix 1 (MQENC_FLOAT_IEEE_NORMAL), 390 2 (MQENC_FLOAT_S390), TNS 3 (MQENC_FLOAT_TNS). Starts at -1 (none) and `clearMQMD` never resets it; any value not 1/2/3 builds as PC |
| Persistent Msg (`m_mqmd_persist`) | No 0 `MQPERSIST_NO` -> MQPER_NOT_PERSISTENT; Yes 1 -> MQPER_PERSISTENT; As Queue 2 -> MQPER_PERSISTENCE_AS_Q_DEF (`getPersistence`, MQMDPAGE.cpp:2859) |
| Id Display (`m_id_disp_ascii`) | Ascii 0 `ID_DISPLAY_ASCII`, Ebcdic 1, Hex 2 (DataArea.h:50-52); default Hex. UI-only state, no MQMD value |
| Report Except / Expire / COA / COD (`m_report_except`, `_expire`, `_coa`, `_cod`) | No 0 `REPORT_NONE`, Yes 1 `REPORT_YES`, Data 2 `REPORT_DATA`, Full 3 `REPORT_FULL`; -1 (no button) after construction and `clearMQMD`; 0 after a message is shown (`processReportOptions`, MQMDPAGE.cpp:1388) |

Report radio -> MQRO flag (`buildReportOptions`, MQMDPAGE.cpp:2204): Except Yes/Data/Full = `MQRO_EXCEPTION` / `_WITH_DATA` / `_WITH_FULL_DATA`;
Expire = `MQRO_EXPIRATION*`; COA = `MQRO_COA*`; COD = `MQRO_COD*`. Check boxes: PAN `MQRO_PAN`, NAN `MQRO_NAN`, Activity `MQRO_ACTIVITY`,
Pass MsgId `MQRO_PASS_MSG_ID`, Pass Correl `MQRO_PASS_CORREL_ID`, Discard `MQRO_DISCARD_MSG`, Discard/Expiry `MQRO_PASS_DISCARD_AND_EXPIRY`.
Group/Segment check boxes -> `MsgFlags` (`buildFlags`, MQMDPAGE.cpp:2358): Group Yes `MQMF_MSG_IN_GROUP`, Group Last `MQMF_LAST_MSG_IN_GROUP`,
Segment Yes `MQMF_SEGMENT`, Segment Last `MQMF_LAST_SEGMENT`, Segment Allowed `MQMF_SEGMENTATION_ALLOWED`.

Read-only rules (re-applied every time the tab is activated, `OnSetActive`, MQMDPAGE.cpp:396-455; they are NOT live when a Main checkbox
changes while the MQMD tab is hidden, so they take effect on the next activation):
- Backout Count: always read-only (rc style).
- No context option on: User Id, Accounting Token, Appl Identity, Appl Origin, Put Application Name and Put Date/Time are read-only, Appl Type is disabled.
- `Set Iden Context` (`m_setUserID`), `Set All Context` (`m_set_all`) or `Alternate User Id` (`m_alt_userid`) on: User Id and Accounting Token become editable;
- `Set All Context` on: additionally Appl Identity, Appl Origin, Put Application Name, Put Date/Time (editable) and Appl Type (enabled);
- Message ID is always editable (the `m_get_by_msgid` condition is commented out, MQMDPAGE.cpp:420-444).
`IDC_MQMD_FORMAT` receives focus first on activation (`WM_SETPAGEFOCUS`, MQMDPAGE.cpp:452, 1083).

### 2.3 Data tab (`IDD_DATA`, class `MSGDATA`, DDX at MSGDATA.cpp:80-96, ctor 44-74)

Static label `IDC_STATIC_MESSAGE_DATA` (initial text `Message Data`) is set by `updateMsgdata` to `pDoc->fileSource`
(e.g. `Message Data (12) from C:\x.dat`, `Message Data (12) msg 1 of 5 from Q`, DataArea.cpp:1306, 8112, 8397-8401, 8679, 9169). Group boxes: `Data Format`,
`&Integer Format`, `Packed &Dec`, `Char Format (Alt)`. The `IDC_MSG_DATA` edit is multiline, read-only, with both scroll bars and
no `ES_AUTOHSCROLL`, so Windows wraps long lines at the edge (UNVERIFIED by running); it uses a fixed font (Courier New, rfhutil.cpp:376)
or a per-script font, and tab stops at 36 and 72 (character/hex) and 36, 72, 108 (both) (MSGDATA.cpp:66-74).
The CR/LF, Indent, EDI and Validate check boxes lie on the lower edge of the `Char Format (Alt)` rectangle; they are shown as members of it
by the rectangle test but are separate options.

|#|ID|type|caption|group|Alt|Tab|flags|member|default|tooltip|
|---|---|---|---|---|---|---|---|---|---|---|
|1|IDC_MSG_DATA|edit||||Y|readonly,multiline|m_msg_data||Message data|
|2|IDC_DATA_FORMAT|radio|Charac&ter|Data Format|T|Y||m_data_format|selected (0)|Display data as characters (Ctrl+T)|
|3|IDC_HEX|radio|&Hex|Data Format|H|||||Display data in hex (Ctrl+H)|
|4|IDC_BOTH|radio|&Both|Data Format|B|||||Display data as both characters and in hex (Ctrl+B)|
|5|IDC_XML|radio|XM&L|Data Format|L|||||Display XML data in formatted  mode (Ctrl+L)|
|6|IDC_PARSED|radio|PARSED|Data Format||||||Display XML data in ESQL hierarchial format (Ctrl+Q)|
|7|IDC_COBOL|radio|COBOL|Data Format||||||Display data mapped to COBOL copy book (Ctrl+K)|
|8|IDC_JSON|radio|JS&ON|Data Format|O|||||Display JSON data in formatted  mode (Alt+O)|
|9|IDC_FIX|radio|&FIX|Data Format|F|||||Display FIX data in formatted  mode (Alt+F)|
|10|IDC_NUMERIC_PC, IDC_HOST|radio|PC (Intel) / HOST (390)|Integer Format||Y||m_numeric_format|selected (0)|For COBOL mapping, assume integer/float data in PC format / For COBOL mapping, assume integer/float data in host format|
|11|IDC_PD_DATA_INTEL, IDC_PD_DATA_HOST|radio|PC (Intel) / HOST (390)|Packed Dec||Y||m_pd_numeric_format|selected (0)|For COBOL mappings, assume packed decimal in PC format / For COBOL mappings, assume packed decimal in host format|
|12|IDC_ASCII|radio|&Ascii|Char Format (Alt)|A|Y||m_character_format|selected (0)|Character data in message is ASCII (Alt+A)|
|13|IDC_EBCDIC|radio|&Ebcdic|Char Format (Alt)|E|||||Character data in message is EBCDIC (Alt+E)|
|14|IDC_CHINESE|radio|&Simp Chinese|Char Format (Alt)|S|||||Character data contains Simplified Chinese characters (Alt+S)|
|15|IDC_KOREAN|radio|&Korean|Char Format (Alt)|K|||||Character data contains Korean characters (Alt+K)|
|16|IDC_TRAD_CHINESE|radio|&Trad Chinese|Char Format (Alt)|T|||||Character data contains traditional Chinese characters (Alt+T)|
|17|IDC_JAPAN|radio|&Japanese|Char Format (Alt)|J|||||Character data contains Japanese characters (Alt+J)|
|18|IDC_THAI|radio|T&hai|Char Format (Alt)|H|||||Interpret characters as Thai characters (Alt+H)|
|19|IDC_RUSSIAN|radio|Russian|Char Format (Alt)|||||||
|20|IDC_CRLF|check|CR/LF|Char Format (Alt)||Y||m_crlf||For character display, break lines as per message data|
|21|IDC_INDENT|check|Indent|||Y||m_data_indent||Indent successive levels on COBOL copybook display|
|22|IDC_EDI|check|EDI|||Y||m_edi||For character display, format lines as EDI segments|
|23|IDC_DATA_VALIDATE|check|Validate|||Y||m_checkData||Validate numeric fields in COBOL copy book|
|24|IDC_DATA_BRNEXT|button|Browse&Next||N|Y||||Read next message in queue using current browse operation|
|25|IDC_DATA_BRPREV|button|B&rowsePrev||R|Y||||Browse previous message|
|26|IDC_COPYBOOK|button|Cop&ybook||Y|Y||||Change or set the COBOL copy book to use for mappings (Alt+Y)|
|27|IDC_DATA_EDITABLE|check|Allow Edit|||Y||m_data_editable||Allow editing of message data (Character and Hex modes only)|
|28|IDC_DATA_WRITEQ|button|Write Q|||Y|disabled||disabled|Write the edited message data to the queue|
|29|IDC_DATA_BOM|check|BOM|||Y|disabled,hidden|m_BOM|hidden+disabled|File has Byte Order Mark (BOM)|

Radio groups (value = position in the template) and their `format::` counterparts (section 5):

| Group (member) | Buttons -> value |
|---|---|
| Data Format (`m_data_format`) | Character 0 `DATA_CHARACTER`, Hex 1, Both 2, XML 3, PARSED 4, COBOL 5, JSON 6, FIX 7 `DATA_FIX` (rfhconst.h:23-30) = `format::View` Character..Fixed |
| Integer Format (`m_numeric_format`) | PC (Intel) 0 `NUMERIC_PC`, HOST (390) 1 `NUMERIC_HOST` |
| Packed Dec (`m_pd_numeric_format`) | PC (Intel) 0, HOST (390) 1 |
| Char Format (Alt) (`m_character_format`) | Ascii 0 `CHAR_ASCII`, Ebcdic 1, Simp Chinese 2 `CHAR_CHINESE`, Korean 3, Trad Chinese 4 `CHAR_TRAD_CHIN`, Japanese 5, Thai 6, Russian 7 `CHAR_RUSSIAN`. `CHAR_UNICODE` (8) has no button (rfhconst.h:33-41) |

State and enable rules.
- `updateMsgdata` (MSGDATA.cpp:288) first reads the three radio members back from the document (`pDoc->m_numeric_format`, `m_pd_numeric_format`,
  `m_char_format`), so after a Get or file read the radios show what `format::defaultsFor` computes; clicking a radio writes the document value first.
- On activation the radios for Trad Chinese, Simp Chinese, Japanese, Korean, Thai and Russian are disabled when the matching Windows font/code page
  was not found (`app->chinaBig5OK`, `chinaSimpOK`, `japanOK`, `koreaOK`, `thaiOK`, `russianOK`, MSGDATA.cpp:502-532). The Qt equivalent is `rfh_is_codepage_installed`.
- `Browse&Next` / `B&rowsePrev` are enabled only while `browseActive == 1` (Prev also needs `browsePrevActive == 1`), recomputed on every render (MSGDATA.cpp:388-410).
- `Allow Edit`, `Write Q`: see 5.6. `BOM`: disabled and hidden for good (`EnableWindow(FALSE)` in `OnInitDialog`, MSGDATA.cpp:870; rc `NOT WS_VISIBLE | WS_DISABLED`):
  `m_BOM` is always FALSE; the option exists in `RenderOptions::bom` only.
- Mnemonic clashes inside this tab: `T` (Charac&ter, &Trad Chinese) and `H` (&Hex, T&hai). `Alt+J` Japanese is also the MQMD `Reset Ids` accelerator.
- The `COBOL` radio with no copy book name asks for one first (5.4). `IDC_RUSSIAN` has no tooltip string and no mnemonic.

## 3. Behaviour of the buttons and options (Main tab)

### 3.1 Mechanics shared by every button

- Pattern of every handler in General.cpp: `UpdateData(TRUE)`, copy the Put/Get options into `DataArea` (`pDoc->m_...`), call a `DataArea` method,
  `UpdateData(FALSE)`, `UpdatePageData()` (3.4). The core equivalent is: read the widgets into `GetOptions`/`PutOptions`, call `MqSession`, render.
- Connection on demand. Read Q, Write Q, Browse Q, Purge Q, queue depth call `checkConnection(QM)` (DataArea.cpp:12489): if connected to another queue
  manager name, close the queue and disconnect; if not connected, `connect2QM` (10636). There is no Connect button on the Main tab (the `MQ` menu has
  `MQConn`/`MQDisc`, rfhutilView.cpp:2462-2506, which print `Connected to <qm>` via `explicitConnect`, DataArea.cpp:11777). Start Browse always calls `connect2QM`.
  An empty QM name connects to the default queue manager. Client builds also accept an MQSERVER-style string in the QM combo (gap G7).
- Status log (`IDC_ERRMSG`, `DataArea::updateMsgText`, DataArea.cpp:10301-10352): operations append text (several lines joined with CR LF) to `m_error_msg`;
  `updateMsgText` then writes `"%H.%M.%S " + text + CR LF + previous log` to `m_msg_text`, so the newest entry is on top, each entry is stamped with the
  local time; the log is cut at `MAX_ERR_MSG_LEN` 8191 bytes back to a line boundary (DataArea.cpp:69, 10324-10335) and trailing CR/LF are removed. The log is
  not cleared by Clear Data / Clear All (only the pending `m_error_msg` is).
- Error text is built by `DataArea::setErrorMsg(cc, rc, operation)` (DataArea.cpp:9315-9880): a `switch` on the reason code with hand-written texts such as
  `2033 No messages in queue`, `2035 Not authorized (<op>)`, `2085 No such queue`, `2059 Queue manager not available (<op>) - may not be started`,
  `2009 Connection to QM has been lost - Q closed`, `2053 Queue full`, `2030 Message too large for queue (check q and qm settings) QMgr max=%d Queue max=%d`;
  the default is `*Error cc=%d rc=%d (<reason name text>) Cannot <op>` or, with no name, `*Error cc=%d rc=%d Cannot <op>` (9866-9876). Operation words used:
  `Open`, `Close`, `Get`, `Put`, `StartBr`/`Get (Browse)`, `GetNext`, `BrowsePrevGet`, `BrowsePrevGetMsg`, `Disc`, `Connect`, `MQCRTMH`, `MQDLTMH`.
  This differs from the core's `Result::fromMq` text (`MQGET cc=2 rc=2033 MQRC_NO_MSG_AVAILABLE (...)`, rfh_result.cpp:29-53): see gap G12.
- Hourglass: `BeginWaitCursor`/`EndWaitCursor` around Read Q, Write Q, Browse Next/Prev, Purge, Load Names, file open/save.
- MQ not installed: at startup `*****MQ not installed` (bindings) or `*****MQ client not installed` (client) is shown in the log (General.cpp:1192-1205).

### 3.2 Buttons

Key = accelerator (7) / mnemonic. "Windows MQ" = the MQ calls and flags actually issued.

| Button (ID) | Handler -> DataArea | Windows MQ calls and flags | Enables/disables | Status text (success) | Failure |
|---|---|---|---|---|---|
| `Read &Q` (`IDC_READQ`) Ctrl+Q, Alt+Q | `General::OnReadq` (General.cpp:276) -> `getMessage(QM, Q, READQ)` (DataArea.cpp:7947) | `MQOPEN` `MQOO_FAIL_IF_QUIESCING \| MQOO_INQUIRE \| MQOO_INPUT_SHARED` (+ `MQOO_ALTERNATE_USER_AUTHORITY` if Alternate User Id; if Selector non-empty `MQOD` V4 `SelectionString`; retried without `MQOO_INQUIRE` on 2045/2035, DataArea.cpp:12056-12180); `MQGET` `MQGMO_FAIL_IF_QUIESCING \| MQGMO_NO_SYNCPOINT` + option flags in 3.3, no wait (waitTime 0, DataArea.cpp:8075), `MQMO_*` match from the MQMD tab ids; buffer grown on 2080 (`processMQGet`, 7612); `MQINQ` depth and queue type on the open handle (18200, 18244). Queue stays open until Close Q or another operation | none beyond 3.4 | `Msg read from <Q> length=<n>` + ` (data <n> dlq <d> rfh <r> cics <c> ims <i>)` when any header was found (DataArea.cpp:8140-8145). Also: Set Iden Context cleared, File Name cleared, window title = queue, MQMD tab and Data tab refreshed | `*Queue Name required*` (empty queue); otherwise `setErrorMsg(..., "Get")` e.g. `2033 No messages in queue` |
| `&Write Q` (`IDC_WRITEQ`) Ctrl+W, Alt+W | `OnWriteq` (General.cpp:334) -> `putMessage(QM, RemoteQM, Q)` (DataArea.cpp:6166) | `MQOPEN` `MQOO_OUTPUT \| MQOO_FAIL_IF_QUIESCING \| MQOO_INQUIRE` + bind flag + `MQOO_SET_ALL_CONTEXT` (Set All) else `MQOO_SET_IDENTITY_CONTEXT` (Set Iden) + `MQOO_ALTERNATE_USER_AUTHORITY`; `ObjectQMgrName` = Remote QM; `MQPUT` with `MQPMO` V2 and flags in 3.3, `MQPMO_SYNCPOINT` when any Group/Segment Yes/Last box is set else `MQPMO_NO_SYNCPOINT`; message = headers built by the RFH/CICS/IMS/DLQ tabs + Data buffer; MQMD from `setMessageMQMD` (MQMDPAGE.cpp:2408), not from `buildMQMD`; `MQCMIT` after the last segment/group message (DataArea.cpp:6465-6505) | none | `Message sent to <Q> length=<n>` (n includes headers); queue depth refreshed; MQMD tab gets new Msg Id, Seq No, Offset (`updateMQMDafterPut`, MQMDPAGE.cpp:2694); title = queue | `*Queue Name required* ` (note trailing blank); `setErrorMsg(..., "Put")`; a warning (cc 1) is reported and the put still counts as sent |
| `&Browse Q` (`IDC_BROWSE`) Ctrl+B, Alt+B | `OnBrowse` (372) -> `getMessage(..., BROWSEQ)` | open `MQOO_FAIL_IF_QUIESCING \| MQOO_INQUIRE \| MQOO_BROWSE`; `MQGET` `MQGMO_FAIL_IF_QUIESCING \| MQGMO_BROWSE_FIRST` + 3.3 flags; then `closeQ(Q_CLOSE_NONE)` (connection stays) | none (no browse session) | `Msg browse from <Q> length=<n>[ (data ...)]` | as Read Q |
| `S&tart Browse` (`IDC_STARTBR`) Ctrl+T, Alt+T | `OnStartbr` (780) -> `startBrowse(QM, Q, false, -1)` (DataArea.cpp:8346) -> `readFirstMessage` (8180) | `connect2QM`; open as Browse Q; `MQGET` `MQGMO_FAIL_IF_QUIESCING \| MQGMO_BROWSE_FIRST` + match + 3.3 flags; browse cursor kept on the open handle | `browseActive=1`: see 2.1 (Next and End enabled, most others disabled); Prev disabled | `First message read from <Q> length=<n>[ (data ...)]`; Data caption `Message Data (<n>) msg 1 of <depth> from <Q>`; title = queue | empty queue: `No messages in <Q> queue` and disconnect; other: `setErrorMsg(..., "Get (Browse)")` and disconnect; no browse is started |
| `Browse &Next` (`IDC_BRNEXT`; also `Browse&Next` on Data) Ctrl+N, Alt+N | `OnBrnext` (709) -> `browseNext(false)` (DataArea.cpp:8459) | `MQGET` `MQGMO_FAIL_IF_QUIESCING \| MQGMO_BROWSE_NEXT` + 3.3 flags; no match options; if the connection/queue was lost while browsing, the browse is restarted (reconnect logic, 8491-8530) | enables Browse Prev (`browsePrevActive=1`) | `Next message (<k>) read from <Q> length=<n>[ (data ...)]`; caption `... msg <k> of <depth> ...` | `No more messages in queue` for 2033, else `setErrorMsg(..., "GetNext")`; in both cases depth refreshed, queue closed, connection dropped, browse ends (buttons revert) |
| `Browse P&rev` (`IDC_BRPREV`; `B&rowsePrev` on Data) Ctrl+R, Alt+R | `OnBrprev` (747) -> `browsePrev()` (DataArea.cpp:8895) | re-browses from `BROWSE_FIRST` up to the message before the saved MsgId (`findPrevMsgId`, 8700-8895) | `browsePrevActive=0` again when the first message is shown | `Previous message (<k>) read from <Q> length=<n>[ (data ...)]` | `setErrorMsg(..., "BrowsePrevGet"/"BrowsePrevGetMsg")` and disconnect |
| `End Browse` (`IDC_ENDBR`) Ctrl+M, Alt+V | `OnEndbr` (689) -> `endBrowse(false)` (DataArea.cpp:8425) | refresh depth, `MQCLOSE` (`MQCO_NONE`), `MQDISC` | browse state cleared: Read/Write/Browse/Start/etc enabled again | `Browse operation ended` | - |
| `Close Q` (`IDC_CLOSEQ`) Ctrl+K | `OnCloseq` (1617) -> `closeQ(m_close_options)` then `discQM()` | `MQCLOSE` with `MQCO_NONE` (None), `MQCO_DELETE` (Delete), `MQCO_DELETE_PURGE` (Purge) (DataArea.cpp:12397-12467); any open unit of work is rolled back first; then `MQDISC` | Close Q disabled again (no connection); the button is disabled during a browse, so this never ends one | none on success | `setErrorMsg(..., "Close")` |
| `Purg&e Q` (`IDC_PURGE`) Ctrl+E, Alt+E | `OnPurgeQ` (866) -> `getQueueDepth`, confirm, `purgeQueue(QM, Q, depthBox)` (DataArea.cpp:10065) | open as Read Q (honours Selector); loop `MQGET` `MQGMO_FAIL_IF_QUIESCING \| MQGMO_SYNCPOINT_IF_PERSISTENT \| MQGMO_ACCEPT_TRUNCATED_MSG` with a zero-length buffer, `MQMO_NONE`; commit every `maxUOW-1` (max 2000) messages and at the end; depth refreshed every 100; `closeQ(NONE)` at the end | none | `<n> messages purged from <Q>`; or `Queue empty - no messages purged from <Q>` | empty queue name: `*Queue Name required` (no stars after, no trailing blank); `Commit failed (rc=2003) - the MQ log may be too small`; `<n> messages purged from <Q> before error rc=<cc> cc=<rc>` (the two numbers are swapped in the source format, DataArea.cpp:10218: AMBIGUOUS whether intended) |
| `&Open File` (`ID_READ_FILE`) Ctrl+O, Alt+O | `OnFileOpen` (421) -> `ReadDataFile` (DataArea.cpp:928) -> `ReadFileData` (994) | none (file) | disabled while browsing (2.1); also File > Open is disabled while browsing (`OnUpdateOpen`, rfhutilView.cpp:1670) | text of `fileio::read` (`<n> bytes read from file <path>` ...); caption `Message Data (<n>) from <path>`; File Name/Data Size updated | `Could not open file <path>` (DataArea.cpp:1046-1049) |
| `&Save File` (`IDC_WRITE_FILE`) Ctrl+S, Alt+S | `OnWriteFile` (449) -> `WriteDataFile` (DataArea.cpp:1703) -> `WriteFile` (1778) | none (file) | none | text of `fileio::write` (`<n> bytes (data <d> mqmd 364 rfh2 <r>) written to file <path>`) | write errors in the log |
| `Clear Data` (`IDC_CLEAR_DATA`) Alt+X | `OnClearData` (636) -> `clearFileData` (DataArea.cpp:10597) | none | none | none | - |
| `Clear All` (`IDC_CLEAR_ALL`) Ctrl+J, Alt+Y | `OnClearAll` (646) -> `DeleteContents` (DataArea.cpp:1996) | none | none | none | - |
| `Load Names` (`IDC_LOAD_QUEUE_NAMES`) Ctrl+H | `OnLoadQueueNames` (1642) -> `getQueueNames(QM)` (DataArea.cpp:25198) | closes the open queue and disconnects first; `connect2QM`; dynamic reply queue `RFHUTIL.REPLY.QUEUE*` (`OpenTempQ`); PCF inquire-queue commands to `SYSTEM.ADMIN.COMMAND.QUEUE` for local, remote and alias queues (`loadNames` 21633, `getListOfNames` 22968, `buildPCFRequestQueues`), cluster queues included when `m_show_cluster_queues` (`View > Cluster Queues`, see below); disconnect at the end | Close Q state refreshed | none; the Queue Name dropdown shows the new list the next time it is opened | `Error getting queue names` (after the MQ error text, if any); `QMgr must be V7 or above`; `MQ on 390 does not support PCF prior to V6`; `Open for temporary reply queue failed rc=<rc>`; `Open for admin queue failed rc=<rc>` |
| `Set Conn &Id` (`IDC_SET_CONN_USER`) Alt+I | `OnSetConnUser` (2347) | none; opens modal `IDD_USERID` `Set Connection Parameters` (3.5); OK copies values into the document, used by the next `connect2QM` (the current connection is not touched) | - | none | Cancel keeps the old values |
| `Exit` (`IDC_EXIT_BUTTON`) Alt+F4 | `OnExitButton` (1029) | `pDoc->ClearData()`, `CloseAllDocuments(TRUE)`, `PostQuitMessage(0)`; no explicit disconnect in the handler (whether `~DataArea` disconnects: UNVERIFIED); no confirmation | - | - | - |
| `Save Q`, `&Load Q`, `Move Q`, `&Display Q` | `OnMainSaveq` 1887, `OnMainLoadq` 2097, `OnMoveQ` 2587, `OnMainDisplayq` 1774 | other dialogs | out of scope for Phase 1: show disabled | | |

Purge confirmation (General.cpp:866-931): after the empty-queue-name check and a depth query (`getQueueDepth`; the result is not shown), `AfxMessageBox(
"Purge all messages from queue %s?", MB_YESNO | MB_APPLMODAL | MB_ICONQUESTION)`; only `IDYES` purges. The message names the queue, not the depth or the QM.

### 3.3 Put/Get options (Main tab check boxes and radios) -> MQ flags

| Option | Get / browse (`setMQoptions`, DataArea.cpp:17816; `processMQGet`, 7612) | Put (`putMessage`, 6166; `openQ`, 11811) |
|---|---|---|
| New Msg Id (default on) | - | `MQPMO_NEW_MSG_ID` |
| New Correl Id | - | `MQPMO_NEW_CORREL_ID` |
| Get by Msg Id | `MQMO_MATCH_MSG_ID`, MsgId = the MQMD tab value, only when that value is not all zero | - |
| Get by Correlid | `MQMO_MATCH_CORREL_ID`, same rule | - |
| Get by Group Id | `MQMO_MATCH_GROUP_ID`, same rule (`getMessage`, `readFirstMessage`; not Browse Next) | - |
| Set Iden Context | (clears itself after every get) | `MQOO_SET_IDENTITY_CONTEXT` at open, `MQPMO_SET_IDENTITY_CONTEXT`; User Id and Accounting Token come from the MQMD tab (current OS user, 12 chars, when User Id is blank, MQMDPAGE.cpp:2570-2600) |
| Set All Context | - | `MQOO_SET_ALL_CONTEXT`, `MQPMO_SET_ALL_CONTEXT` (wins over Set Iden at open); also copies Appl Identity/Origin/Name/Type and Put Date/Time (now, when blank) from the MQMD tab |
| Logical Order | `MQGMO_LOGICAL_ORDER` | `MQPMO_LOGICAL_ORDER` |
| Complete Msg | `MQGMO_COMPLETE_MSG` | - |
| All Avail | `MQGMO_ALL_MSGS_AVAILABLE \| MQGMO_ALL_SEGMENTS_AVAILABLE` (z/OS before V6: neither; z/OS V6+: only `ALL_MSGS_AVAILABLE`) | - |
| Convert | `MQGMO_CONVERT`; MQMD `CodedCharSetId` = MQMD tab Code Page when non-empty (else queue manager default) and `Encoding` = 546 (`DEF_PC_ENCODING`) if Int Fmt is PC else 273 (`DEF_UNIX_ENCODING`) (DataArea.cpp:8011-8030) | - |
| Alternate User Id | `MQOO_ALTERNATE_USER_AUTHORITY`, `AlternateUserId` = `pDoc->m_user_id` | same |
| Selector (edit) | `MQOD` V4 `SelectionString` at open (read, browse, purge); needs MQ V7 | - |
| User Props: As Queue / None / Yes / RFH2 / Compat | As Queue = no flag (`MQGMO_PROPERTIES_AS_Q_DEF`). Yes = message handle + `MQGMO_PROPERTIES_IN_HANDLE` (values land on the Usr Prop tab). None/RFH2/Compat would add `MQGMO_NO_PROPERTIES` / `MQGMO_PROPERTIES_FORCE_MQRFH2` / `MQGMO_PROPERTIES_COMPATIBILITY`, but in `processMQGet` those `else if` branches sit inside `if (... MQ_PROPS_YES == m_mq_props ...)` (DataArea.cpp:7690-7760) and can never run; `browsePrev` has the same branches but sets `opts.Options` (the MQCMHO) instead of `gmo.Options` for None and RFH2 (9090-9106) | Yes with at least one user property: message handle (`MQCRTMH`, `MQSETMP`) and `MQPMO` V3 `OriginalMsgHandle`; the other values do nothing on put |
| Cluster Open: As Queue / Bind Open / Not Fixed / Group | - | none / `MQOO_BIND_ON_OPEN` / `MQOO_BIND_NOT_FIXED` / `MQOO_BIND_ON_GROUP` (DataArea.cpp:11926-11940); see the `Group` defect in 2.1 |
| Close Options: None / Delete / Purge | - | `MQCLOSE` `MQCO_NONE` / `MQCO_DELETE` / `MQCO_DELETE_PURGE`, used only by the Close Q button and `MQ > MQClose` (`explicitCloseQ`, which also logs `Queue closed`); every internal close uses None |

### 3.4 What `UpdatePageData` shows after each operation

`General::UpdatePageData` (General.cpp:504) copies from the document: copy book name, file name, `Data Size` (`fileSize`), the status log, `Queue depth`, `Queue Type`,
and `Set Iden Context`; it then applies the enable rules in 2.1. Queue depth/type come from `MQINQ` on the open handle after each get/put (`getCurrentDepth`, `getQueueType`);
the "queue name resolved" message `Queue name resolved to <name up to 48 chars>` is logged by `openQ` when an alias/remote name resolves to a different queue (DataArea.cpp:12303).

### 3.5 Set Conn Id dialog (`IDD_USERID`, ConnUser.cpp; rc:1040)

Caption `Set Connection Parameters`. Controls in template (tab) order: `User id` (`IDC_CONN_USERID`), `Password` (`IDC_CONN_PASSWORD`, `ES_PASSWORD`), `Security Exit Name`,
`Security Exit Data`, `Local Address`, `Certificate Store Location` (`IDC_CONN_SSL_KEYR`), `&SSL` (check), `SS&L Client Validation` (check), `&FIPS Required` (check), `SSL Cipher Algorithm`
(editable combo), `SSL Reset Count` (digits), `SSL Peer Name (server cert DN, optional)`, buttons `OK`, `Cancel`, `R&eset`, then check box `Save password` (`IDC_SAVE_PASSWORD`) and button `Br&owse`.
Ctrl+E/L/O/S accelerators exist for Reset/validate/Browse/SSL (`IDR_CONNUSER_ACCELERTOR`, rc:224). The dialog's behaviour was not read beyond `General::OnSetConnUser`; the
core consumes only user id and password (`ConnectOptions`, rfh_mq_session.h:30-35). The Windows password is kept in the registry only when `Save password` is on (`initSavePassword`, UNVERIFIED).

### 3.6 File-related options (menu `Read` and `Write`, not on the Main tab)

| Menu item (ID) | Document flag | Default | Effect | Core |
|---|---|---|---|---|
| Read > `Remo&ve CrLf` (`IDM_READ_NOCRLF`) | `m_read_nocrlf` | off | remove CR and LF from the file data on read | `ReadOptions::removeCrLf` |
| Read > `&Unix Text` (`IDM_READ_UNIX`) | `m_read_unix` | off | bare LF becomes CR LF on read | `ReadOptions::unixLineEnds` |
| Read > `&Ignore Headers` (`IDM_READ_IGNORE_HEADER`) | `m_read_ignore_header` | off | do not look for MQ headers in the file | `ReadOptions::ignoreHeaders` |
| Read > `Save Headers` (`IDM_READ_SAVE_RFH`) | `m_save_rfh` | off | keep the current RFH/MQMD tab contents instead of clearing them (DataArea.cpp:1115-1130, 1229-1246) | not supported by the core (rfh_message_file.h) |
| Read > `Ignore MQMD` (`IDM_READ_IGNORE_MQMD`) | `m_read_ignore_MQMD` | off | declared (DataArea.h:470) and toggled, but `ReadFileData` never reads it: a dead option on Windows | no counterpart (drop it) |
| Write > `No &Headers` (`IDM_WRITE_DATAONLY`) | `m_dataonly` | off | write the data without RFH/CICS/IMS/DLQ headers | `WriteOptions::dataOnly` |
| Write > `Include MQMD` (`IDM_WRITE_INCLUDE_MQMD`) | `m_write_include_MQMD` | ON (DataArea.cpp:373) | write a 364-byte MQMD built by `buildMQMD(&mqmd, TRUE, TRUE)` first | `WriteOptions::includeMqmd` |
| Main tab `File Code Page` (`IDC_MAIN_FILE_CODEPAGE`) | `m_file_codepage` | 437 | CCSID assumed for the file data; 0/empty = take the MQMD tab Code Page, else ANSI code page, else 1252 (DataArea.cpp:1062-1080) | `ReadOptions::fileCcsid` (0 = decide), `ansiCcsid` |

The Read/Write items are check-marked menu items (`OnUpdateRead...`/`OnUpdateWrite...`, rfhutilView.cpp:1297-1350). `View > System Queues` (`IDM_VIEW_SYSTEM_QUEUES`, `m_show_system_queues`) only changes the Queue Name dropdown filter; `View > Cluster Queues` (`IDM_VIEW_CLUSTER`) toggles `m_show_cluster_queues` and immediately reloads the queue names (error text `Error getting queue names - reason <rc>`), but its own comment says it does not work because PCF replies carry no cluster queue entries (rfhutilView.cpp:2162-2214).
File dialogs: Open: `CFileDialog(TRUE)`, filter `All (*.*)|XML (*.XML)|Text (*.TXT)`, initial directory = last used (`app->initFilePath`); Save: `CFileDialog(FALSE,
OFN_CREATEPROMPT | OFN_OVERWRITEPROMPT)`, no filter, remembers the directory afterwards (`setMostRecentFilePath`). Copy book: title `COBOL copy book file`,
filter `All (*.*)|COBOL files (*.cbl;*.cpy)|Text (*.TXT)` (DataArea.cpp:9981-10056).

## 4. MQMD tab

### 4.1 Fields, choices and validation

Validation on Windows is only: `DDV_MaxChars` (2.2), `ES_NUMBER` (digits only: Code Page, Expiry, Offset), and the id edit behaviour below. Numeric text is
converted with `atoi` when the MQMD is built, so non-numeric text becomes 0 and an empty field leaves the MQMD default (`buildMQMD` MQMDPAGE.cpp:1991,
`setMessageMQMD` 2408). Priority, Seq No, Feedback, Orig Len and Backout Count accept any characters (no `ES_NUMBER`).

| Field (ID) | Widget | Choices / format | Notes |
|---|---|---|---|
| MQ Message Format (`IDC_MQMD_FORMAT`) | editable dropdown (`MyComboBox`, auto-complete), 8 chars | `MQFMT_STRING`, `_DEAD_LETTER_HEADER`, `_CICS`, `_IMS`, `_IMS_VAR_STRING`, `_PCF`, `_ADMIN`, `_COMMAND_1`, `_COMMAND_2`, `_DIST_HEADER`, `_EMBEDDED_PCF`, `_EVENT`, `_REF_MSG_HEADER`, `_RF_HEADER_1`, `_RF_HEADER_2`, `_TRIGGER`, `_WORK_INFO_HEADER`, `_XMIT_Q_HEADER`, `_CHANNEL_COMPLETED` in that order (MQMDPAGE.cpp:249-267: MQSTR, MQDEAD, MQCICS, MQIMS, MQIMSVS, MQPCF, MQADMIN, MQCMD1, MQCMD2, MQHDIST, MQHEPCF, MQEVENT, MQHREF, MQHRF, MQHRF2, MQTRIG, MQHWIH, MQXMIT, MQCHCOM). The strings added are the 8-character blank-padded MQ constants (`"MQSTR   "`) | Any text up to 8 characters may be typed. On put the format is blank-padded to 8 (`setMessageMQMD`) |
| User Id (`IDC_USER_ID`) | edit, 12, read-only by default | free text | editable with Set Iden / Set All / Alternate User Id (2.2). On put with context set and the field blank, the current OS user name is used (Windows: `GetUserNameEx` domain\user cut to 12) |
| Code Page (`IDC_MQMD_CCSID`) | edit, digits, 5 | CCSID, e.g. 437, 819, 1208 | each change (`EN_UPDATE`) and the Int/PD Fmt radios call `pDoc->freeHeader(format)` (drops header data areas, MQMDPAGE.cpp:896-935). Empty = leave MQMD default |
| Backout Count (`IDC_BACKOUT_CNT`) | edit, read-only | integer text | built into the MQMD by `buildMQMD`, not by `setMessageMQMD` |
| Priority (`IDC_MQMD_PRIOTITY`) | edit, 8 | integer | |
| Orig Len (`IDC_MQMD_ORIG_LEN`) | edit, 9 | integer; `-1` (= `MQOL_UNDEFINED`) after Clear | |
| Put Date/Time (`IDC_PUT_DATE_TIME`) | edit, read-only | `YYYY/MM/DD HH:MM:SS.hh` (`formatDateTime`) | editable with Set All Context only; if blank then, the local time at put is used |
| Expiry (`IDC_EXPIRY`) | edit, digits, 9 | MQ unit: tenths of a second (not stated in the source); only digits can be typed, so `-1` (unlimited) can be shown but not entered | |
| MsgType (`IDC_MSGTYPE`) | editable dropdown, `CBS_SORT`, 10 chars (`LimitText(11)`), list filled on first focus | `1 Request`, `2 Reply`, `4 Report`, `8 Datagram`, `112 FR_MQE`, `113 MQE` (values 1, 2, 4, 8, 112 `MQMT_MQE_FIELDS_FROM_MQE`, 113 `MQMT_MQE_FIELDS`). Sorted as text: 1, 112, 113, 2, 4, 8 | only the leading number counts (`atoi`); it is applied only when it equals one of the six values or lies in `MQMT_APPL_FIRST..MQMT_APPL_LAST`, otherwise the MQMD keeps its default. A received type outside the list shows an empty box (`setMsgType`, MQMDPAGE.cpp:938; core `msgTypeText`) |
| Feedback (`IDC_MQMD_FEEDBACK`) | edit, 8 | integer | |
| Int Fmt / PD Fmt / Float Fmt | radios | see 2.2 | |
| Message ID (`IDC_MSG_ID`) | edit, id edit (`CIdEdit`) | hex 48 / ASCII-EBCDIC 24 chars, see 4.2 | binary `m_message_id[24]` |
| Correl ID (`IDC_CORREL_ID`) | same | same | `m_correlid[24]` |
| Offset (`IDC_OFFSET`) | edit, digits | default "0" | used only when a flag is set (`setMessageMQMD`: with Segment Yes/Last or any flag) |
| Group Id (`IDC_GROUP_ID`) | same as ids | same | `m_group_id[24]`; used only if non-zero (then `MsgFlags` becomes `MQMF_LAST_MSG_IN_GROUP` when no flag box is set) |
| Seq No (`IDC_SEQNO`) | edit | default "1" | |
| Appl Type (`IDC_APPL_TYPE`) | editable dropdown, disabled unless Set All | `0 None`, `1 CICS`, `2 OS/390 or z/OS`, `3 IMS`, `4 OS/2`, `5 PC Dos`, `6 Unix`, `7 QMGR`, `8 OS/400`, `9 Windows`, `10 CICS/VSE`, `11 32-bit Windows`, `12 VMS`, `13 Guardian or Non Stop`, `14 VOS`, `19 IMS Bridge`, `20 XCF`, `21 CICS Bridge`, `22 Notes`, `25 User`, `26 Broker`, `28 Java`, `29 DQM`, `30 Chan Init`, `31 WLM`, `32 Batch`, `33 RRS Batch`, `34 SIBus`, `35 System` (list built on first focus, MQMDPAGE.cpp:995-1034 from `getApplTypeDisplay`, mqsubs.cpp:118). `36 Multicast` and `37 AMQP` are known to `getApplTypeDisplay` but not in the list | only the leading number counts (`atoi`). A received type with no name shows its number; a type <= 0 that has no name shows `-1 Other/Unknown` (`setPutApplType`, MQMDPAGE.cpp:1046). `OnSelchangeApplType` reads the selection index from `IDC_MSGTYPE` instead of `IDC_APPL_TYPE` (MQMDPAGE.cpp:981-993, defect) |
| Reply To Queue Manager / Reply To Queue | edit, 48 / 48 | | blank padded on put |
| Application Identity / Appl Origin / Put Application Name | edit 32 / 4 / 28, read-only unless Set All | | `buildMQMD` copies them only when shorter than the field (strict `<`, so a full-length value is dropped: MQMDPAGE.cpp:2154-2170) |
| Accounting Token (`IDC_ACCOUNT_TOKEN`) | edit, 64 hex characters, read-only unless Set Iden/All/Alt User | 32 bytes shown as 64 hex digits (`AsciiToHex`) | parsed with `HexToAscii` on `len/2` bytes, **no validation** of the characters |

### 4.2 The three ids (Message ID, Correl ID, Group Id)

- Representation: the page keeps the binary values in `m_message_id`, `m_correlid`, `m_group_id` (24 bytes each) and the edit boxes show them according to
  `Id Display` (`SetId`, MQMDPAGE.cpp:1787): **Hex** (default): 48 upper-case hex digits; **Ascii**: 24 characters, bytes below 0x20 shown as blanks;
  **Ebcdic**: the bytes translated EBCDIC to ASCII (`EbcdicToAscii`, comsubs), also with blanks for controls. Limits follow the mode (`setMaxFieldLengths`, 670):
  48 in Hex, 24 otherwise.
- Editing (`CIdEdit`, IdEdit.cpp): overtype mode is the default (typing replaces the character to the right of the caret; `Insert` toggles); `Delete` keeps
  the caret; Backspace moves left and deletes. In Hex mode only `0-9`, `A-F` (lower case is upper-cased) are accepted, anything else gives `Beep(300,200)` and is rejected
  (`SetHexOnly`, set on focus, MQMDPAGE.cpp:3478-3530, 3648-3680, 3865-3900). Ctrl+V replaces from the caret (or the selection) with up to 48 characters of the clipboard text in
  place, without hex filtering on the normal clipboard path (IdEdit.cpp:158-240; `editIDField` would turn non-hex into `0`, MQMDPAGE.cpp:3709: UNVERIFIED which paths call it).
- Updating the binary value (`OnUpdateCorrelId` 3548, `OnUpdateGroupId` 704, kill-focus `setMsgId(dispType)` 3931): Hex: pad with `0` to 48 characters and convert with
  `HexToAscii`; Ascii/Ebcdic: pad with blanks to 24, more than 24 characters are cut with `Beep(1000,300)` (Correl and Group; the Message ID is limited by the edit
  limit alone) and **only the characters that differ from the previously displayed text replace the binary byte**, so an untouched non-printable byte is preserved
  (`setCorrelId`, `setGroupId`, `setMsgId`). Ebcdic mode converts the typed text ASCII to EBCDIC.
- Changing Id Display first stores pending edits using the old mode, then redraws the three boxes from the binary values (`updateIdFields`, MQMDPAGE.cpp:608).
- `Reset Ids` (`IDC_RESET_IDS`, Alt+J): zeroes all three binary ids and empties the three boxes. `Copy Msg Id to Correl Id` (`IDC_COPY_MSGID`, Alt+C): copies the Message ID
  display text and bytes into Correl ID (MQMDPAGE.cpp:472-551).
- Get by Msg Id / Correlid / Group Id use these bytes only when they are not all zero (3.3). After a put the page shows the new Message ID, Seq No, Offset and, for group/segment
  messages, the Group Id (`updateMQMDafterPut`, 2694). After a get/browse the page is cleared (`clearMQMD` 1688) and refilled (`processMessageMQMD` 1530).
- Menu `Ids` items work on the same buffers: `Save/Restore Msg Id | Correl Id | Group Id` (`m_saved_msg_id` ..., MQMDPAGE.cpp:3083-3185) and `Copy Msg Id to Correl Id`.

### 4.3 Defaults after construction / `clearMQMD` (MQMDPAGE.cpp:52-116, 1688-1785)

Text fields empty except Backout Count `"0"`, Seq No `"1"`, Offset `"0"` (empty after clear), Orig Len `"-1"` after clear; Persistent = No; Int and PD Fmt = PC; Float Fmt none (-1);
Id Display = Hex; all Report radios none (-1); all check boxes off; ids zero. These equal `MqmdFields` defaults except Offset (`""` in the core; `"0"` in the Windows constructor)
and the Float Fmt (`FLOAT_PC` in the core, none on Windows).

### 4.4 Windows field -> `mqmd_fields::MqmdFields` (core/message/rfh_mqmd_fields.h)

| Windows control (member) | `MqmdFields` member | Remark |
|---|---|---|
| MQ Message Format (`m_mqmd_format`) | `format` | |
| User Id (`m_mqmd_userid`) | `userId` | |
| Code Page (`m_mqmd_ccsid`) | `ccsid` | |
| Backout Count (`m_mqmd_backout_count`) | `backoutCount` | |
| Priority (`m_mqmd_priority`) | `priority` | |
| Orig Len (`m_mqmd_orig_len`) | `originalLength` | |
| Put Date/Time (`m_mqmd_date_time`) | `dateTime` | |
| Expiry (`m_mqmd_expiry`) | `expiry` | |
| MsgType (`m_mqmd_msgtype`) | `msgType` | `msgTypeText()` gives the combo text |
| Feedback (`m_mqmd_feedback`) | `feedback` | |
| Int Fmt (`m_mqmd_encoding`) | `encoding` | `NUMERIC_PC` / `NUMERIC_HOST` |
| PD Fmt (`m_mqmd_pd_encoding`) | `pdEncoding` | |
| Float Fmt (`m_mqmd_float_encoding`) | `floatEncoding` | -1 (none) cannot be stored in the core default `FLOAT_PC`; harmless (both build as PC) |
| Message ID / Correl ID / Group Id (binary members) | `msgId` / `correlId` / `groupId` | raw bytes; the hex/ASCII/EBCDIC text conversion and edit rules (4.2) are not in the core: use `HexToAscii`, `AsciiToHex`, `AsciiToEbcdic`, `EbcdicToAscii` from `RFHUtil/comsubs.h` (a shared source), or app code |
| Offset (`m_mqmd_offset`) | `offset` | |
| Seq No (`m_mqmd_seq_no`) | `seqNo` | |
| Appl Type (`m_mqmd_appl_type`) | `putApplType` | `"6 Unix"`; leading number used |
| Reply To Queue Manager / Reply To Queue | `replyToQMgr` / `replyToQ` | |
| Persistent Msg (`m_mqmd_persist`) | `persistence` | `MQPERSIST_NO/YES/AS_Q` = 0/1/2 |
| Group Yes / Last | `groupYes` / `groupLast` | |
| Segment Yes / Last / Allowed | `segmentYes` / `segmentLast` / `segmentAllowed` | |
| Report Except / Expire / COA / COD radios | `reportException` / `reportExpiration` / `reportCoa` / `reportCod` | `REPORT_NONE..REPORT_FULL`, -1 = none |
| PAN, NAN, Activity, Discard | `reportPan`, `reportNan`, `reportActivity`, `reportDiscard` | |
| Pass MsgId / Pass Correl / Discard/Expiry | `reportPassMsgId` / `reportPassCorrelId` / `reportPassDiscardAndExpiry` | |
| Application Identity / Appl Origin / Put Application Name | `applIdentity` / `applOrigin` / `putApplName` | |
| Accounting Token | `accountingToken` | hex text, 2 characters per byte |
| **Id Display radios (`m_id_disp_ascii`)** | **none** | UI-only state; keep in the Qt page |
| **Reset Ids, Copy Msg Id to Correl Id buttons** | **none** | actions on `msgId`, `correlId`, `groupId`; trivial in the app |
| **Saved ids (`m_saved_*`), edit-tracking (`OrigMsgid`, `OrigCorrelid`, `OrigGroupid`)** | **none** | UI state |
| (no Windows control) | none | every `MqmdFields` member has a Windows control |

Semantic differences that matter for the page: `toMqmd` is the port of `buildMQMD` (used for **file writes**, DataArea.cpp:1818) but a **put** uses `setMessageMQMD`
(DataArea.cpp:6290, MQMDPAGE.cpp:2408) with different rules (gap G11). `buildMQMD` (and so `toMqmd`) leaves the Windows overruns out (core truncates).

## 5. Data tab

### 5.1 Display modes and the options each one uses (`updateMsgdata`, MSGDATA.cpp:288-432)

Every radio/check box click calls `updateMsgdata`, which re-renders the whole text from the document (`pDoc->fileData`, the message body without MQ headers) and
puts it into `IDC_MSG_DATA`; the focus then returns to the text box so the scroll keys work. There is no caching in the page.

| Data Format radio (value) | DataArea call (arguments from the page) | `format::View` | Options that change the output |
|---|---|---|---|
| Character (0 `DATA_CHARACTER`) | `getCharacterData(charFormat, crlf, edi, BOM, indent)` | `Character` | `charFormat`, `crlf`, `edi`, `bom` (always off), `indent` |
| Hex (1 `DATA_HEX`) | `getHexData()` | `Hex` | none (character format is not used) |
| Both (2 `DATA_BOTH`) | `getBothData(charFormat, BOM)` | `Both` | `charFormat`, `bom` |
| XML (3 `DATA_XML`) | `getXmlData(charFormat)` | `Xml` | `charFormat` |
| PARSED (4 `DATA_PARSED`) | `getParsedData(charFormat)` | `Parsed` | `charFormat` |
| COBOL (5 `DATA_COBOL`) | `getCobolData(charFormat, numericFormat, pdNumericFormat, indent, checkData)` | `Cobol` | `charFormat`, `numericFormat`, `pdNumericFormat`, `indent`, `checkData`, copy book |
| JSON (6 `DATA_JSON`) | `getJsonData(charFormat)` | `Json` | `charFormat` |
| FIX (7 `DATA_FIX`) | `getFixData(charFormat)` | `Fixed` | `charFormat` |

Re-render triggers (what a click re-renders): Data Format radios, Char Format radios and `CR/LF`, `EDI`, `BOM` always re-render; `Indent` re-renders only when the current view is
COBOL or Character (MSGDATA.cpp:1112); `Validate` and the two Integer Format/Packed Dec radio pairs only when the view is COBOL (MSGDATA.cpp:691-728, 819-856, 1127). The Qt app may
simply re-render on every change since the core output depends only on the options in the last column.

### 5.2 Option -> `format::RenderOptions` / `DataFormats` (core/format/rfh_format.h)

| Windows control (member) | Field | Notes |
|---|---|---|
| Data Format radios (`m_data_format`) | `format::View` argument of `render` | values equal `DATA_*` (static_assert in rfh_format.h) |
| Char Format radios (`m_character_format` = document `m_char_format`) | `DataFormats::charFormat` (`CHAR_ASCII..CHAR_RUSSIAN`) | set by `defaultsFor` after each get/read, then changed by the user |
| Integer Format radios (`m_numeric_format`) | `DataFormats::numericFormat` | COBOL view only (binary integers/floats) |
| Packed Dec radios (`m_pd_numeric_format`) | `DataFormats::pdNumericFormat` | COBOL view only |
| `CR/LF` (`m_crlf`) | `RenderOptions::crlf` | tooltip "For character display, break lines as per message data" |
| `EDI` (`m_edi`) | `RenderOptions::edi` | tooltip "For character display, format lines as EDI segments" |
| `BOM` (`m_BOM`) | `RenderOptions::bom` | control hidden and disabled on Windows: always false |
| `Indent` (`m_data_indent`) | `RenderOptions::indent` | tooltip "Indent successive levels on COBOL copybook display"; also applies to the Character view's XML indentation (rfh_format.h comment) |
| `Validate` (`m_checkData`) | `RenderOptions::checkData` | tooltip "Validate numeric fields in COBOL copy book" |
| Copy book name (Main `IDC_COPYBOOK_FILE_NAME`, `Cop&ybook`) | `RenderOptions::copybookPath` | the core fails when the file cannot be opened (Windows shows an empty COBOL view) |
| (no control) | `DataFormats::ccsid`, `DataFormats::intFormat` | from the message (`defaultsFor`), no widget; `intFormat` is what Windows keeps as `fileIntFormat` |
| (no control) | `RenderOptions::display` (`ansi`, `oem`), `conversionTableDir` | not user options on Windows; `display.ansi` decides the code page of the returned text (see 5.3) |
| Allow Edit (`m_data_editable`), `IDC_DATA_WRITEQ` | none | out of Phase 1 (5.6) |
| Data caption (`IDC_STATIC_MESSAGE_DATA`) | none | `ReadResult::dataSource` for files; queue captions are built by the app (3.2) |

After every get or file read the page radios take the message's values: call `options.apply(format::defaultsFor(...))` (CODEMAP section 8.8).

### 5.3 Character sets (Char Format (Alt))

- A radio click sets `pDoc->m_char_format`, picks the font (`setDisplayFont`: Courier New for Ascii/Ebcdic; GB2312, Korean, Shift-JIS, Big5, Thai or Russian fonts for the others,
  rfhutil.cpp:376-486) and re-renders (OnAscii/OnEbcdic/OnChinese/OnKorean/OnTradChinese/OnJapan/OnThai/OnRussian, MSGDATA.cpp:590-628, 928-1038). It does not change the message
  bytes, only how the body bytes are interpreted. Ebcdic translates the bytes to ASCII before display (DataFormatter).
- Trad Chinese, Simp Chinese, Japanese, Korean, Thai and Russian radios are disabled when the system has no suitable font/code page (2.3).
- The text `render` returns is in the code page `options.display.ansi` (default `rfh_get_acp()`, 1252 unless `RFHUTIL_ACP`) and the Qt page must decode it from that code page
  (CODEMAP 8.2). The multi-byte lead-byte tests in `DataFormatter::isLeadChar` assume code pages 936 (Simp Chinese), 949 (Korean), 950 (Trad Chinese), 932 (Japanese), 874 (Thai),
  1251 (Russian) (DataFormatter.cpp:3837-3895). Whether `render` already converts those views, or the app must set `display.ansi` to that code page, was not determined: AMBIGUOUS, test it.

### 5.4 Browse buttons and Copybook on this tab

- `Browse&Next` (`IDC_DATA_BRNEXT`, Alt+N) / `B&rowsePrev` (`IDC_DATA_BRPREV`, Alt+R): `browseNext(false)` / `browsePrev()`, wait cursor, `updateMsgText`, re-render (MSGDATA.cpp:743-793);
  the same status text as on the Main tab (3.2). Enabled only during a browse (5.1).
- `Cop&ybook` (`IDC_COPYBOOK`, Alt+Y): `readCopybookFile` (file dialog `COBOL copy book file`; log `copybook file read (<path>)` or `<rc> error reading copy book file`), then re-render
  (MSGDATA.cpp:659). Selecting the `COBOL` radio with no copy book name does the same first; if the dialog is cancelled the previous Data Format radio is restored (MSGDATA.cpp:188-215).
- Edit menu, Data tab: `Top` / `Bottom` (`Ctrl+Home` / `Ctrl+End`) scroll `IDC_MSG_DATA` to the first/last line (rfhutilView.cpp:3265-3306); `Cut`, `Copy`, `Paste`, `Sel All`
  (`ID_EDIT_*`); `Copy`/`Select All` act on the data box. Cut/Paste can only change text while Allow Edit is on (UNVERIFIED; the box is read-only otherwise).

### 5.5 Find, Find Hex, Go To (Search menu, `Ctrl+F`, `Ctrl+Z`, `Ctrl+G`)

Enable rules (`OnUpdateSearchFind...`, rfhutilView.cpp:1737, 1786, 1873): only on the Data tab, with `fileSize > 0`, and with neither Find nor Find Hex open. Find: views Character, Hex,
Both, XML, PARSED, JSON, FIX (not COBOL). Find Hex and Go To: Character, Hex, Both only. While a Find/Hex Finder dialog is open the other tabs, File > Open/Save and the other
Search items are blocked (rfhutilView.cpp:749-780, 1634-1686).

**Find** (`CFindReplaceDialog`, modeless, `FR_HIDEWHOLEWORD | FR_HIDEMATCHCASE | FR_DOWN`, rfhutilView.cpp:1717): the standard Windows Find dialog with `Find what`, Direction Up/Down (Down
selected) and `Find Next` / `Cancel`; no Match-case or Whole-word option (hidden). Case sensitivity: always case sensitive (byte / `memcmp` comparison). Each `Find Next` (`findHelper`, 1905):
- First, if the text or the display type changed since the last search, all search positions are reset (`resetXMLfind`, `resetParsedFind`, `resetFind`; the JSON and FIX positions are not reset there: UNVERIFIED).
- Character, Hex, Both: `DataArea::findNext(bytes, len, down)` (DataArea.cpp:12649) searches the **raw message bytes** (`fileData`), not the rendered text. The typed text is used as bytes (at most
  255); when the character format is EBCDIC it is first translated ASCII to EBCDIC. The search starts one byte after the previous hit (state `findStartOffset`, -1 = none: down starts at 0,
  up starts at `fileSize - 1 - len`), not at the caret or selection. It does **not wrap**: when there is no further match the state is reset (the next Find Next starts again from the top/bottom),
  **no message is shown**, and the display moves to the very beginning (`moveDisplay(0, 0, 0)`: line 0, column 0, no selection). Off-by-one: the loop bound is `fileSize - len` exclusive, so
  a match starting at `fileSize - len` (ending exactly at the end of the data) is never found (DataArea.cpp:12649-12785: read, not run).
- XML, PARSED, JSON, FIX: searches the **rendered text** (`m_data_xml`, `m_data_parsed`, `m_data_json`, `m_data_fix`) with `memcmp`, case sensitive, same continue-after-previous-hit and no-wrap
  behaviour (`getXMLLineNumber` 12939, `getParsedLineNumber` 13482, `getJsonLineNumber` 13249, `getFixLineNumber` 13094); on a miss the position resets and the display moves to line 0 and
  selects one character at offset 0 (`moveDisplay(0, 0, 1)`).
- Highlight (`MSGDATA::moveDisplay`, MSGDATA.cpp:1142): scrolls so the hit line is the first visible line (`LineScroll`), then selects `count` characters starting at the hit column, sets the caret
  and focuses the data box. `count` is 2 in the Hex view (the first byte's two hex digits) and 1 elsewhere; **only the first byte of a match is selected, not the whole match.**

**Find Hex** (`CHexFind`, `IDD_HEXFIND`, caption `Hex Finder`, modeless; rfhutilView.cpp:1770, 2069; HexFind.cpp): an upper-casing edit (`IDC_HEX_VALUE`, `ES_UPPERCASE`, max 16 characters = 8 bytes),
radios `Up` (0) and `Down` (1, default) in the group box `Direction`, buttons `Find` (default) and `Cancel`, and an error label `IDC_HEX_EDIT_ERRMSG`. `Find` validates, and on an error
writes the text in the label and plays `Beep(500,500)`: `No data entered` (empty), `Odd number of characters`, `Invalid hex character(s)` (anything but `0-9A-F`; `Please enter some data` cannot be
reached). Valid input is converted to bytes and passed to the same `findNext` as above (raw message bytes; for the EBCDIC character format the bytes are translated ASCII to EBCDIC first,
which looks like a defect for hex input: UNVERIFIED). No match: `moveDisplay(0, 0, 0)`, no message. The dialog stays open for repeated `Find`; `Cancel`/Esc closes it.

**Go To** (`CGoto`, `IDD_GOTO`, caption `Go to offset`, modal; rfhutilView.cpp:1815, Goto.cpp): label `Enter offset (0 - <fileSize-1>)`, a digits-only edit (`IDC_GOTO_OFFSET`), `OK` (default)
and `Cancel`; error label `IDC_GOTO_ERRMSG`. `OK` rejects, leaving the dialog open, with `Offset must be a number` (non-digit) or `Offset too large or < 0` (value > `fileSize - 1`).
The value is a **zero-based byte offset into the message data buffer** shown in `Data Size` (the body without MQ headers), not a line number. It is converted to a rendered-text position
for the current view by `getLineNumber` / `getLineOffset` (DataArea.cpp:12793, 12872):
- Character, CR/LF and EDI off: line = `ofs / 32`, column = `ofs % 32 + 9`. CR/LF on: line = number of LF bytes before `ofs`, column 0. EDI on: line 0, column 0.
- Hex: line = `ofs / 16`; with `d = (ofs % 16) * 2` hex digits into the line, column = `d + 9` if `d < 8`, `d + 10` if `d < 16`, `d + 11` if `d < 24`, else `d + 12`.
- Both: line = `ofs / 16`; column = `ofs % 16 + 9` if that is `< 8`, else `+ 10` (this addresses the character column, not the hex column: UNVERIFIED against the current render layout).
The match is highlighted with `moveDisplay` as above (2 characters in Hex, otherwise 1). The BOM option is passed but unused. These mappings assume the Windows layout of the formatters
(`DataFormatter`): lines of 32 characters with an 8-digit offset prefix for Character, 16 bytes a line for Hex and Both. They are not exposed by the core (gap G14).

### 5.6 Allow Edit and Write Q (Data tab; out of scope in Phase 1)

`Allow Edit` (`IDC_DATA_EDITABLE`, member `m_data_editable`): `OnDataEditable` (MSGDATA.cpp:1633) makes `IDC_MSG_DATA` writable (`SetReadOnly(FALSE)`), enables `Write Q` and focuses the text box (the
page then bypasses dialog key handling while the box has the focus, so mnemonics do not steal keystrokes, MSGDATA.cpp:1241). Switching it on while the view is not Character or Hex shows
`AfxMessageBox("Note: Editing is only supported in Character and Hex display modes.\nSwitch to Character or Hex format before writing.", MB_OK | MB_ICONINFORMATION)`. The read-only state is re-applied after
each re-render. `Write Q` (`OnDataWriteQ`, 1782) refuses with message boxes `Write from the Data tab is only supported in Character and Hex display modes.` (`MB_OK | MB_ICONWARNING`) and
`No queue name available.\nPlease read or browse a message from the General tab first.`; otherwise it turns the edited text back into bytes (`parseEditedDataToFileData`, 1670: Character = the text
bytes, re-encoded to EBCDIC when the character format is EBCDIC; Hex = strip the `DDDDDDDD ` offset prefix of each line and all blanks, then hex pairs, error `*Invalid hex character in edited data`;
else `*Data editing is only supported in Character and Hex display modes`), replaces the data buffer, and calls `putMessage` with the current queue (`currentQM`/`currentQ`/`currentRemoteQM`, falling
back to the Main tab values). In Phase 1 keep the box read-only; hide or disable `Allow Edit` and `Write Q` (the BOM box stays hidden).

## 6. Gaps: what the Qt app needs that the core API does not provide

Checked against `core/mq/rfh_mq_session.h`, `core/fileio/rfh_message_file.h`, `core/format/rfh_format.h`, `core/message/rfh_mqmd_fields.h`, `core/platform/*.h`.
"App" = can be done in the Qt layer with what exists (including the shared `comsubs`/`mqsubs` helpers).

| # | Need | Windows function (file:line) | Smallest proposal |
|---|---|---|---|
| G1 | List local queue managers (Queue Manager dropdown, bindings mode). **Not a Windows function**: the bindings build only lists queue managers already in its in-memory name cache (`queueNamesRoot`, one entry per QM for which Load Names ran in this session, `getQueueNamesPtr` DataArea.cpp:23583, listed by `getFirstQMname`/`getNextQMname` 24199-); the client build lists `MQSERVER`, channel-table entries and the saved `lastClientQM[]` (30 entries, rfhutil.cpp:2033-2070). Enumerating local QMs is new in the spec | `DataArea::loadQMComboBox` DataArea.cpp:25392-25611 | Core: `Result mq::listLocalQueueManagers(const std::string & mqDataPath, std::vector<std::string> & names);` parsing the `QueueManager:` stanzas of `<MQ data path>/mqs.ini` (default `/var/mqm`); or run `dspmq` in the app. Nothing in core exists. History (last used names) is app logic via `QSettings` |
| G2 | Queue names for the Queue Name dropdown ("Load Names") | `General::OnLoadQueueNames` General.cpp:1642 -> `DataArea::getQueueNames` 25198 -> `loadNames` 21633 -> `OpenTempQ` 18304 (dynamic reply queue from `SYSTEM.DEFAULT.MODEL.QUEUE`, name `RFHUTIL.REPLY.QUEUE*`) + `openAdminQ` 18484 + `getListOfNames` 22968 (PCF inquire-queue, local/remote/alias, optional cluster filter) | Core: `struct QueueListOptions { bool local = true, remote = true, alias = true, cluster = false; };` `Result MqSession::listQueues(const QueueListOptions & options, std::vector<std::string> & names);` using PCF over the existing `open/put/get` entry points on short-lived handles like `depth()`. Hiding `SYSTEM.*` names is app logic (`View > System Queues`). PCF is not available on z/OS before V6 (message `MQ on 390 does not support PCF prior to V6`) |
| G3 | Queue type (`Local`/`Model`/`Alias`/`Remote`/`Cluster`), depth after each get/put, max message length, resolved queue name (`Queue name resolved to ...`) | `getQueueType` 18244, `setQueueType` 10354, `getCurrentDepth` 18200, `openQ` 11811 (max length `MQINQ` at 12270, resolved name at 12303) | Core: `struct QueueInfo { MQLONG type; MQLONG depth; MQLONG maxMsgLength; std::string resolvedName; };` `Result MqSession::queueInfo(const std::string & queue, QueueInfo & info);` (today only `depth()` exists, which opens its own handle). The text mapping for `type` is app logic |
| G4 | Browse position for `Next message (k)` and `msg k of depth` captions | `browseCount` in `startBrowse`/`readFirstMessage`/`browseNext`/`browsePrev` (DataArea.cpp:8180-8230, 8459-8700, 8895-9225) | Core: `long MqSession::browsePosition() const;` (the member `browsePosition` exists but is private and may be -1/unknown). Or count in the app |
| G5 | Get options not in `GetOptions`: Get by Group Id (`MQMO_MATCH_GROUP_ID`), Logical Order (`MQGMO_LOGICAL_ORDER`), Complete Msg (`MQGMO_COMPLETE_MSG`), All Avail (`MQGMO_ALL_MSGS_AVAILABLE \| MQGMO_ALL_SEGMENTS_AVAILABLE`), Selector (MQOD V4 `SelectionString`), `MQOO_INPUT_SHARED` instead of the core's `MQOO_INPUT_AS_Q_DEF`, Alternate User Id | `setMQoptions` DataArea.cpp:17816 (gated on queue manager platform/level: `platform != MQPL_MVS` or level >= 600), `processMQGet` 7612, `getMessage` 7947, `openQ` 12056-12180 | Core: add to `GetOptions`: `bool matchGroupId; MQBYTE24 groupId; bool logicalOrder, completeMsg, allAvailable; std::string selector; std::string alternateUser; bool inputShared;`. The QM platform/level check needs `MqSession` to remember `MQIA_PLATFORM`/`MQIA_COMMAND_LEVEL` from connect (Windows `connect2QM` inquires them, DataArea.cpp:11260-11395) |
| G6 | Put options not in `PutOptions`: Logical Order (`MQPMO_LOGICAL_ORDER`), syncpoint plus commit for group/segment messages (core always `MQPMO_NO_SYNCPOINT`), Alternate User Id, user properties; and after the put the MQMD tab takes MsgId, Seq No, Offset, GroupId from the returned MQMD | `putMessage` DataArea.cpp:6166-6529 (pmo flags 6290-6340, commit rules 6465-6505), `updateMQMDafterPut` MQMDPAGE.cpp:2694 | Core: `PutOptions::logicalOrder`, `PutOptions::syncpoint` (+ `Result MqSession::commit()`), `PutOptions::alternateUser`. After put apply only the four fields above from `message.mqmd`, not a full `fromMqmd` |
| G7 | Connection: (a) the QM field accepts an MQSERVER-style string `CHANNEL/TCP/host(port)` and `QM[CHANNEL/TCP/host(port)]`; the dropdown lists `MQSERVER` and channel-table entries; (b) `Set Conn Id` also sets SSL/TLS (key repository, cipher spec, peer name, FIPS, reset count, client validation), security exit and data, local address; (c) automatic reconnect/heart beat (`Conn` tab) | `connect2QM` DataArea.cpp:10636-11300 (parsing 10956-11060, SSL 10945), `loadQMComboBox` client branch 25392-25520, `listClientTable`, `General::OnSetConnUser` General.cpp:2347, `MQConnection` (MQConnection.h/.cpp) | (a) App: parse into `ConnectOptions` (`channel`, `connName`); optional core helper `bool parseServerString(const std::string &, ConnectOptions &)`. (b) Core: add to `ConnectOptions` `std::string keyRepository, cipherSpec, sslPeerName, securityExit, securityData, localAddress; bool fipsRequired, sslClientAuth; MQLONG sslResetCount;`. (c) not Phase 1; `heartbeat`/`keepAlive` already exist. The spec's new Connection box (bindings or client, channel, connection name or CCDT) maps to `ConnectOptions` already |
| G8 | User properties (`User Props` radios, `Usr Prop` tab) | `processMQGet` DataArea.cpp:7690-7760 (`MQCRTMH`, `MQGMO_PROPERTIES_IN_HANDLE`, `extractMsgProps`), `putMessage` 6360-6420 (`setMsgProps`), `browsePrev` 9070-9110 | Core: `enum class PropertiesMode { AsQueue, None, Yes, Rfh2, Compat }` in `GetOptions` giving `MQGMO_NO_PROPERTIES` / `MQGMO_PROPERTIES_FORCE_MQRFH2` / `MQGMO_PROPERTIES_COMPATIBILITY`; `Yes` needs a property list in `Message` (not Phase 1). Today only `AsQueue` is possible. The Windows None/RFH2/Compat radios are effectively dead on Get (3.3), so showing only `As Queue` enabled loses nothing observable |
| G9 | Cluster open options (As Queue / Bind Open / Not Fixed / Group) | `openQ` DataArea.cpp:11926-11940 | **No gap**: `PutOptions::bind` has all four (`BindOption`). Wire `Group` (the Windows handler is missing, 2.1) |
| G10 | Cancel for a waiting Get | none: Windows Read Q/Browse never wait (`waitTime` 0, DataArea.cpp:8075); a wait exists only in other features (`loadMsgs`) | **No gap**: `GetOptions::waitMs` and `cancel` exist. Keep the `Read Q` default at `waitMs = 0` for parity |
| G11 | Put-side and get-side MQMD semantics of the page | `MQMDPAGE::setMessageMQMD` MQMDPAGE.cpp:2408-2692 (used by every put, DataArea.cpp:6290; differs from `buildMQMD`: ids copied only when non-zero, report/flags/offset/group rules, user id, accounting token, Appl fields and Put Date/Time only with Set Iden/Set All, blank user id = OS user, blank date = now, reply fields blank padded not truncated) and `processMessageMQMD` 1530-1686 | Core: `void mqmd_fields::toPutMqmd(const MqmdFields &, bool setIdentity, bool setAll, const std::string & osUser, MQMD2 &)` (CODEMAP section 10 already lists moving these into the core in Plan 3). `toMqmd` is the file-write path (`buildMQMD`) and must not be used for put. `fromMqmd` (`processMQMD`) differs slightly from `processMessageMQMD` for the Group Id of version 1 MQMDs and for Windows' ASCII/EBCDIC/endian pre-translation done in `processMessage` |
| G12 | Windows status texts and the log | `setErrorMsg` DataArea.cpp:9315-9880 (about 90 hand-written texts, some with `maxMsgLenQM`/`maxMsgLenQ`), success texts in 3.2, `updateMsgText` 10301 | The core text (`Result::fromMq`: `MQGET cc=2 rc=2033 MQRC_NO_MSG_AVAILABLE (...)`) does not match the Windows wording the spec asks for (`2033 No messages in queue`). Core: `std::string statusText(const Result &, const char * operation, MQLONG maxMsgLenQM, MQLONG maxMsgLenQ);` porting `setErrorMsg`, plus `maxMsgLength` from G3 and the queue manager (`MQIA_MAX_MSG_LENGTH`). The timestamped, newest-first, 8191-byte log is app logic |
| G13 | OS user name for Set Iden / Set All with a blank User Id | `setMessageMQMD` MQMDPAGE.cpp:2573-2600 (`GetUserNameEx`/`GetUserName`, first 12 characters) | App (`getpwuid(geteuid())`) passed as `osUser` to G11, or `rfh_current_user()` in `core/platform` |
| G14 | Byte offset to rendered-text position (Go To, Find highlight) and the byte search | `getLineNumber` DataArea.cpp:12793, `getLineOffset` 12872, `findNext` 12649, `MSGDATA::moveDisplay` MSGDATA.cpp:1142 | Core: `struct TextPos { int line; int column; int length; };` `bool format::locate(format::View, const RenderOptions &, const Message &, size_t offset, TextPos &);` so the mapping stays next to the formatters it depends on (5.5). The byte search (`std::search` on `message.body`, EBCDIC via `AsciiToEbcdic`) and the text searches for XML/Parsed/JSON/Fix are app code (`QPlainTextEdit::find` is enough for those four; keep case-sensitive, no wrap, no message to match Windows) |
| G15 | Purge as Windows does it | `purgeQueue` DataArea.cpp:10065-10244 (selector honoured, `MQGMO_SYNCPOINT_IF_PERSISTENT`, batched commits, `MQGMO_ACCEPT_TRUNCATED_MSG`) | Core `purge` uses `MQGMO_NO_SYNCPOINT`, no selector and a count only. Acceptable; add a `selector` parameter with G5. Texts in 3.2 are app code. The confirmation is app code (`QMessageBox::question`, `Purge all messages from queue %s?`) |
| G16 | File options | `ReadFileData` DataArea.cpp:994-1321 | **Covered** by `ReadOptions`/`WriteOptions`, except `Save Headers` (not supported, rfh_message_file.h) and `Ignore MQMD` (a dead option on Windows: drop it). The Main tab `File Code Page` text maps to `ReadOptions::fileCcsid` (0/empty = decide) |
| G17 | Read-only mode, Windows registry settings (last QM/queue, paths, password), audit records (`createAuditRecord`), trace menu | rfhutil.cpp, `DataArea::getMessage` etc. | App: `QSettings` (spec 5), trace via `rfhSetTrace`. Not core |
| G18 | Flow rules that live in `DataArea`, not in `MqSession` | `checkConnection` 12489 (connect on demand; reconnect when the QM name changes), `endBrowse` 8425, `browseNext` failure path (queue closed and disconnected, browse over), `Close Q` = `closeQ` + `discQM` 1617, `Browse Q` = open, one get, close | App: reproduce with `connect/closeQueue/disconnect` and track `browseActive` / `browsePrevActive` to drive 2.1's enable table. `MqSession::get` with `BrowsePrevious` fails after a destructive get (core comment): keep Browse Prev disabled then, as Windows' state machine does after any non-browse operation |

Core behaviour differences worth a decision (not gaps):
- Core `Result` for `get` returns `MQRC_NO_MSG_AVAILABLE` as a failed `Result` (cc 2) like Windows' `cc == 2`, so `Start Browse`/`Browse Next` can branch on `rc == MQRC_NO_MSG_AVAILABLE`
  exactly as Windows does (`readFirstMessage`, `browseNext`).
- Core `put` takes headers separately (`Message::headers`); Phase 1 has no header tabs, so Write Q sends the Data tab buffer as `body` with the MQMD `Format` as typed (G11).
- Windows converts with `CodedCharSetId` = MQMD tab Code Page and `Encoding` 546/273 (3.3); map to `GetOptions::convertCcsid` / `convertEncoding`
  (`DEF_PC_ENCODING`, `DEF_UNIX_ENCODING`, rfhconst.h:82-84), leaving `MQCCSI_Q_MGR` when the Code Page box is empty.
- Windows Get by Msg Id/Correlid/Group Id is applied only when the MQMD tab id is non-zero; core `matchMsgId` has no such rule: the app must decide.

## 7. Keyboard shortcuts and menu

### 7.1 Accelerator table `IDR_MAINFRAME` (rc:171-212; dispatch in rfhutilView.cpp:2594-3470)

The table is global to the frame. Most commands are context sensitive: `CRfhutilView` looks at the current tab and does something different on Main, Data and others, and
ignores the key on tabs that do not use it. Entries written as `"^X"` are ASCII control characters (Ctrl+X); `Ctrl+H` is also the Backspace code, `Ctrl+I` the Tab code (`"\t"`) and
`Ctrl+M` the Enter code: Windows keeps those entries, and only `processBackspace` in some edits and `IsDialogMessage` in the pages guard against them (UNVERIFIED how Tab and Enter reach the
frame). The Qt app should bind Ctrl+letters only, not Tab/Enter/Backspace.

| Key | Command | Main tab | Data tab | MQMD tab |
|---|---|---|---|---|
| Ctrl+Q | `ID_READQ` | Read Q | PARSED view | - |
| Ctrl+W | `ID_WRITEQ` | Write Q | Write edited data to queue (`MSGDATA::WriteQ`) | - |
| Ctrl+B | `ID_BROWSEQ` | Browse Q | Both view | - |
| Ctrl+T | `ID_STARTBR` | Start Browse | Character view | - |
| Ctrl+N | `ID_BRNEXT` | Browse Next (if browsing) | Browse Next (re-renders) | - |
| Ctrl+R | `ID_BRPREV` | Browse Prev (if allowed) | Browse Prev | - |
| Ctrl+M | `ID_ENDBROWSE` | End Browse | End Browse | - |
| Ctrl+K | `ID_CLOSEQ` | Close Q | COBOL view | - |
| Ctrl+E | `ID_PURGEQ` | Purge Q | EBCDIC | - |
| Ctrl+D | `ID_DISPLAYQ` | Display Q | toggle Packed Dec PC/HOST | - |
| Ctrl+L | `ID_LOADQ` | Load Q | XML view | - |
| Ctrl+Y | `ID_SAVEQ` | Save Q | read a copy book (`ReadCopybook`) | - |
| Ctrl+H | `ID_LOADNAMES` | Load Names | Hex view | - |
| Ctrl+I (Tab) | `ID_SET_CONN_USER` | Set Conn Id | toggle Integer Format PC/HOST | - |
| Ctrl+J | `ID_CLEARALL` | Clear All | - | - |
| Ctrl+O | `IDC_OPEN` | File > Open (all tabs; disabled while browsing) | same | same |
| Ctrl+S | `ID_FILE_SAVE` | Save File via `CRfhutilDoc::OnFileSave` (rfhutilDoc.cpp:607): `UpdateAllFields()`, then the Save dialog and `WriteFile`; unlike the menu `File > Save` (`IDM_FILE_WRITE` -> `WriteDataFile`) it does not remember the directory | same | same |
| Ctrl+F / Ctrl+Z / Ctrl+G | `IDM_SEARCH_FIND` / `IDM_SEARCH_FIND_HEX` / `IDM_SEARCH_GOTO` | disabled | Find / Find Hex / Go To (5.5) | disabled |
| Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V | `ID_EDIT_SELECT_ALL`, `_COPY`, `_CUT`, `_PASTE` (Ctrl+X, Ctrl+V are VIRTKEY entries) | focused edit | data box | focused edit |
| Ctrl+P / Ctrl+U | `ID_FILE_PRINT` / `ID_FILE_PAGE_SETUP` | printing of the Data text (not in menu); out of scope | | |
| Alt+X | `IDC_CLEAR_DATA` | Clear Data | Clear Data | |
| Alt+Y | `ID_COPYBOOK` | Clear All (sic: `OnCopybook` calls `ClearAll` on Main) | Copy book dialog | - |
| Alt+C | `ID_COPYMSGID2CORRELID` | - | - | Copy Msg Id to Correl Id |
| Alt+J | `ID_RESETIDS` | - | - | Reset Ids |
| Alt+A, Alt+E, Alt+S, Alt+T, Alt+K, Alt+H | `ID_ASCII`, `ID_EBCDIC`, `ID_SIMP_CHINESE`, `ID_TRAD_CHINESE`, `ID_KOREAN`, `IDM_THAI` | - | Char Format radios | - |
| Alt+O, Alt+F | `IDM_JSON`, `IDM_FIX` | - | JSON / FIX view | - |
| Alt+N / Alt+P | `IDM_NEXT_TAB` / `IDM_PREV_TAB` | next / previous tab (wraps, skips `Conn`) | same | same |
| Alt+V | `ID_ENDBROWSE` | End Browse | End Browse | - |
| Alt+F4 | `ID_APP_EXIT` | Exit | | |
| F6 | `ID_NEXT_PANE` | not handled in source | | |
| Alt+G, L, M, R, U, W | `IDM_PS_*`, `ID_PS_WRITE_MSGS` | PS tab only (not ported) | | |

Dialog mnemonics (the `&` letters in 2.x) are separate from the table. The table has priority over a mnemonic with the same letter in the frame's message order (UNVERIFIED), so on
Windows these mnemonics are shadowed: Main `Alt+N` (next tab, not Browse Next), `Alt+R`, `Alt+W`, `Alt+L` (PS commands: no effect on Main), `Alt+O` (JSON), `Alt+E` (EBCDIC),
`Alt+S` (Simp Chinese), `Alt+T` (Trad Chinese); Data `Alt+N` also. Free, hence reliable, Main mnemonics: `Alt+Q` Read Q, `Alt+B` Browse Q, `Alt+D` Display Q, `Alt+I` Set Conn Id.
The spec's Alt+Q/W/B/O/S/E/N/R list is the mnemonic list; Windows' reliable keys for the shadowed ones are the Ctrl variants above (tooltips show them).

### 7.2 Menu bar `IDR_MAINFRAME` (rc:78-166)

| Popup | Items (caption verbatim; ID) |
|---|---|
| `&File` | `&Open\tCtrl+O` (`IDC_OPEN`); `&Save\tCtrl+S` (`IDM_FILE_WRITE`); separator; `Recent File` (`ID_FILE_MRU_FILE1`, grayed; the MRU list is the last read files); separator; `E&xit` (`ID_APP_EXIT`) |
| `&Edit` | `Cu&t\tCtrl+X`; `&Copy\tCtrl+C`; `&Paste\tCtrl+V`; `Sel &All\tCtrl+A` (`ID_EDIT_*`); `Top\tCtrl+Home` (`ID_EDIT_TOP`); `Bottom\tCtrl+End` (`ID_EDIT_BOTTOM`) (the accelerator table has no entry for them; the captions only advertise the edit control's own keys) |
| `&Search` | `&Find\tCtrl+F` (`IDM_SEARCH_FIND`); `Find He&x` (`IDM_SEARCH_FIND_HEX`); `&Go to\tCtrl+G` (`IDM_SEARCH_GOTO`); all `INACTIVE` in the template and enabled by the update handlers (5.5) |
| `&Read` | `Remo&ve CrLf` (`IDM_READ_NOCRLF`); `&Unix Text` (`IDM_READ_UNIX`); `&Ignore Headers` (`IDM_READ_IGNORE_HEADER`); `Save Headers` (`IDM_READ_SAVE_RFH`); `Ignore MQMD` (`IDM_READ_IGNORE_MQMD`) |
| `&Write` | `No &Headers` (`IDM_WRITE_DATAONLY`); `Include MQMD` (`IDM_WRITE_INCLUDE_MQMD`) |
| `&View` | `System Queues` (`IDM_VIEW_SYSTEM_QUEUES`); `Cluster Queues` (`IDM_VIEW_CLUSTER`); separator; submenu `&Theme`: `&Light Mode` / `&Dark Mode` / `&System Default` (`ID_VIEW_THEME_*`) |
| `&Ids` | `Save Msg Id`, `Save Correl Id`, `Save Group Id`, `Save CICS Facility`, `Save IMS Trans Id`; separator; `Restore Msg Id`, `Restore Correl Id`, `Restore Group Id`, `Restore CICS Facility`, `Restore IMS Trans Id`; separator; `Copy Msg Id to Correl Id` (`IDM_SAVE_*`, `IDM_RESTORE_*`, `IDM_COPY_MSG_ID`) |
| `&MQ` | `MQ&Conn` (`IDM_MQ_MQCONN`); `MQ&Disc` (`IDM_MQ_MQDISC`); separator; `MQOpen (&Get)` (`IDM_MQ_MQOPEN_GET`) and `MQOpen (&Put)` (`IDM_MQ_MQOPEN_PUT`), both removed in a `SAFE_MODE` build; `MQOpen (Browse)` (`IDM_MQ_MQOPEN_BROWSE`); `MQClos&e` (`IDM_MQ_MQCLOSE`); separator; `End Browse` (`IDM_MQ_ENDBR`) |
| `&Help` | `&About rfhutil...` (`ID_APP_ABOUT`) |

The Phase 1 spec does not list a menu. The Phase 1 relevant items are File Open/Save/Exit, Edit, Search, Read, Write, View > System Queues and the explicit MQ commands (`explicitConnect`
prints `Connected to <qm>`, `explicitOpen` prints `Opened queue <q>[ on <rqm>] for put|read|browse|read/browse`, `explicitCloseQ` prints `Queue closed`, DataArea.cpp:11777-11820, 12469).
`About` dialog `IDD_ABOUTBOX`: caption `About rfhutil`, text `Copyright (C) IBM Corporation 2000-2026`, a version label (`IDC_ABOUT_VERSION`) and `OK` (rc:286-295).
`About` and the system menu add nothing else (no custom system-menu commands were found in the sources read).
