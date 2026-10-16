# RFHUtil for Linux (Qt6): Phase 1 Design

- **Date:** 2026-10-15
- **Status:** Draft, awaiting review
- **Target:** Fedora 44, IBM MQ 9.4.x (`/opt/mqm`), Qt 6, C++17

## 1. Goal

Deliver a native Linux build of RFHUtil that keeps working on Windows, by
extracting a portable core library from the MFC code. Phase 1 ports the
**Main**, **Data**, and **MQMD** tabs. The rest of the application follows in
later phases on the same core.

### Requirements

1. **Both connection modes, chosen at runtime:** local bindings
   (`libmqm_r.so`) and client (`libmqic_r.so`) in one binary, selected per
   connection. This replaces the separate Windows `rfhutil` / `rfhutilc`
   builds.
2. **Byte-for-byte file compatibility:** message files written by Windows
   RFHUtil (with or without MQMD and MQ headers) can be read, and files written
   on Linux are identical to what Windows would write for the same message and
   options.
3. **Aimed at upstream:** work happens in a fork of `ibm-messaging/mq-rfhutil`.
   The MFC application must keep building and behaving the same. Contributions
   follow the upstream PR template: DCO sign-off, tests, and a CHANGELOG entry.
   This matches upstream roadmap item P5 ("CMake build system, cross-platform
   support").

### Out of scope for Phase 1

- **Other tabs:** RFH, JMS, PubSub (V6/V7), Usr Props, CICS, IMS, DLQ, Other,
  and Connection Settings. These show as disabled tabs.
- **Separate dialogs:** Save Q, Load Q, Move Q, Display Q, Capture/Write Pubs.
- **Theming:** the Windows `ThemeManager`. Qt follows the desktop theme.
- **mqperf:** these command-line tools already build on Linux and are
  unchanged.

## 2. Current state (observed 2026-10-15, upstream `09744ae`)

- **Size:** `RFHUtil/` is about 86k lines of MFC C++. 46 of its `.cpp` files
  use MFC types.
- **`DataArea`:** `DataArea.cpp` (25.8k lines, `class DataArea : public
  CObject`) holds the message state, every MQ call, display formatting, and
  file I/O. It has only 27 `CString` uses and 5 direct UI calls
  (`AfxMessageBox`, `GetDlgItem`, etc.), so it can be separated.
- **MQ loading:** `DataArea` picks `mqm.dll` or `mqic.dll` at runtime with
  `LoadLibrary` and `GetProcAddress` (16 entry points).
- **UI-to-data coupling:** the pages assign `pDoc->m_*` fields and call about
  40 `DataArea` operations: get, put, browse, close, purge, depth, queue names,
  read/write file, and the formatters. `WriteDataFile` opens a `CFileDialog`
  itself, and it builds the MQMD by calling the **page**
  (`MQMDPAGE::buildMQMD`). So some message logic currently lives in the UI.
- **Portable parts:** the parsers (`comsubs`, `xmlsubs`, `XMLParse`,
  `JsonParse`, `Copybook`, `mqsubs`, `Names`, `mqerror`), about 16k lines,
  depend only on `stdafx.h`, `rfhutil.h`, and a few `CString` / `_s`-function
  uses.
- **Tests:** existing GoogleTest suites under `tests/UnitTests` (Windows only):
  Comsubs, Encoding, FileFormat, HeaderUtil, JsonParse, MQError, Names,
  Reconnect, XMLParse, Xmlsubs.

## 3. Architecture

```
mq-rfhutil/
├── core/        librfhcore: portable C++17 static library, no MFC and no Qt
│   ├── platform/   string and _s-function shims, paths, dynamic loading (dlopen / LoadLibrary)
│   ├── mq/         MqLoader, MqSession
│   ├── message/    Message, HeaderChain, MqmdFields
│   ├── format/     character/hex/both/XML/JSON/parsed/COBOL/fixed renderers
│   └── fileio/     RFHUtil message file format
├── qt/          rfhutil-qt: Qt6 Widgets application
├── RFHUtil/     existing MFC app; DataArea delegates to core/
├── tests/core/  GoogleTest + golden files + MQ integration tests
├── packaging/   rfhutil.spec, .desktop file, icon
└── CMakeLists.txt, CMakePresets.json
```

### Rules

- **Dependencies point inward.** `qt/` → `core/` and `RFHUtil/` → `core/`.
  `core/` depends only on the MQ C headers (`cmqc.h`, `cmqxc.h`) and the C++
  standard library.
- **No UI in the core.** `core/` never shows dialogs, never exits, and never
  reads UI state. The `AfxMessageBox` calls and file dialogs move into the
  MFC layer.
- **One owner per concern.** `MqSession` owns the connection and object
  handles. `Message` owns the message bytes. Formatters and the file writer
  only read a `Message`. MQMD field conversion lives only in `MqmdFields`.
- **Incremental extraction.** Code moves one function at a time. After each
  step, `DataArea` calls into `core/` and the MFC build still compiles. Code
  that has been moved is deleted from `DataArea`, not duplicated.
- **Phase 1 boundary.** Only code reachable from the Main, Data, and MQMD
  pages moves. Everything else stays in `DataArea` until its phase.

## 4. Core API

All names are in namespace `rfh`.

```cpp
struct Result {
  MQLONG cc = MQCC_OK;
  MQLONG rc = MQRC_NONE;
  std::string text;                       // reason text from the mqerror table, plus context
  std::vector<std::string> warnings;      // e.g. a header that could not be parsed
};

struct MqApi {                            // entry points of the loaded MQ library
  decltype(&::MQCONNX) connx; /* disc, open, close, put, get, inq likewise */
  bool serverLibrary;                     // the loaded library can make local connections
  bool bindingsAvailable;                 // a local queue manager installation exists (<home>/bin/strmqm)
  std::string libraryPath;
};

class MqLoader {                          // one library per process: libmqm_r, or libmqic_r on client-only setups
 public:
  static Result load(MqApi & api, const std::string & installationPath = std::string());
};

enum class Mode { Bindings, Client };

struct ConnectOptions {
  Mode mode = Mode::Bindings;
  std::string qmName;                     // empty: default queue manager
  std::string channel, connName, ccdtUrl; // client mode; otherwise MQSERVER / MQCHLLIB
  MQLONG heartbeat = 60;
  MQLONG keepAlive = MQKAI_AUTO;
  std::string userId, password;           // sent only with a password; never copied or traced
  // TlsOptions and autoReconnect: deferred to the Connection Settings plan
};

enum class BindOption { AsQueueDefinition, OnOpen, NotFixed, OnGroup };
enum class CloseOption { None, Delete, Purge };
enum class GetMode { Destructive, BrowseFirst, BrowseNext, BrowsePrevious };

struct PutOptions {
  std::string remoteQm;
  BindOption bind = BindOption::AsQueueDefinition;
  bool newMsgId = true, newCorrelId = false, setIdentityContext = false, setAllContext = false;
};

struct GetOptions {
  GetMode mode = GetMode::Destructive;
  MQLONG waitMs = 0;                      // MQWI_UNLIMITED supported
  bool convert = false;
  MQLONG convertCcsid = MQCCSI_Q_MGR, convertEncoding = MQENC_NATIVE;
  bool matchMsgId = false, matchCorrelId = false;
  MQBYTE24 msgId, correlId;
  size_t initialBufferSize = 64 * 1024;
  const std::atomic<bool> * cancel = nullptr;
};

class MqSession {                         // one thread at a time; handles use MQCNO_HANDLE_SHARE_NO_BLOCK
 public:
  MqSession() = default;
  explicit MqSession(const MqApi &);      // test seam: a fake MQI
  Result connect(const ConnectOptions &);
  Result disconnect();
  bool isConnected() const;
  Result put(const std::string & queue, Message &, const PutOptions &);
  Result get(const std::string & queue, Message &, const GetOptions &);
  Result depth(const std::string & queue, MQLONG & count);
  Result purge(const std::string & queue, MQLONG & count);
  Result closeQueue(CloseOption = CloseOption::None);
  // queueNames: deferred to a later plan
};
// After a converted-message-too-big warning, a browse re-reads by matching the MsgId
// (owner decision); with duplicate MsgIds the browse can stick, as documented on
// GetOptions::convert in rfh_mq_session.h.

struct Message {                          // core/message/rfh_message.h
  MQMD2 mqmd = {MQMD2_DEFAULT};
  std::vector<unsigned char> headers;     // MQ headers (DLQ, CICS, IMS, RFH1, RFH2, MQMDE) as raw bytes
  std::vector<unsigned char> body;        // the data after them
};

struct MqmdFields {                       // core/message/rfh_mqmd_fields.h: what the MQMD tab shows
  std::string format, ccsid, msgType, expiry, feedback, priority, backoutCount = "0",
              replyToQ, replyToQMgr, userId, accountingToken, applIdentity, applOrigin,
              putApplName, putApplType, dateTime, seqNo = "1", offset, originalLength = "-1";
  int persistence, encoding, pdEncoding, floatEncoding;                  // radio groups
  int reportException, reportExpiration, reportCoa, reportCod;           // -1 = none selected
  bool reportPan, reportNan, reportActivity, reportDiscard, reportPassMsgId, reportPassCorrelId,
       reportPassDiscardAndExpiry, groupYes, groupLast, segmentYes, segmentLast, segmentAllowed;
  MQBYTE24 msgId, correlId, groupId;
};

namespace mqmd_fields {                   // replaces MQMDPAGE::buildMQMD, processMQMD's field setting, clearMQMD
  void toMqmd(const MqmdFields&, MQMD2&);             // start from MQMD2_DEFAULT
  bool fromMqmd(const MQMD2&, MqmdFields&);           // native-encoding version 1 or 2; false otherwise
  void clear(MqmdFields&);
  std::string msgTypeText(MQLONG msgType);            // "8 Datagram"
}

enum class HeaderType { DeadLetter, Cics, Ims, Rfh1, Rfh2, MdExtension };
struct HeaderInfo { HeaderType type; size_t offset; size_t length; };
struct HeaderChain {                      // core/message/rfh_header_chain.h
  std::vector<HeaderInfo> headers;
  size_t bodyOffset = 0;                  // total length of the headers
  std::string bodyFormat;
  int bodyCcsid = 0, charFormat = 0, encoding = 0;
  bool hasMdExtension = false;
  MdExtensionFields mdExtension = {};     // groupId, msgSeqNumber, offset, msgFlags, originalLength
  std::vector<std::string> warnings;
};
HeaderChain walkHeaders(const unsigned char * data, size_t length, const std::string & format,
                        int ccsid, int charFormat, int encoding);   // replaces DataArea::parseMsgHeaders' chain
LeadingHeader detectLeadingHeader(const unsigned char * data, size_t length);   // DataArea::checkDataForHeader
bool looksLikeEbcdic(const unsigned char * data, size_t length);                // DataArea::checkForEBCDIC

namespace fileio {                        // core/fileio/rfh_message_file.h
  struct ReadOptions { int fileCcsid = 0; bool ignoreHeaders = false, removeCrLf = false,
                       unixLineEnds = false; int ansiCcsid = 0; };
  struct ReadResult { Message message; bool hadMqmd = false; HeaderChain chain; int fileCcsid,
                      fileIntFormat, charFormat, numericFormat, pdFormat, floatFormat;
                      std::string dataSource; };
  Result read(const std::string & path, const ReadOptions &, MqmdFields & fields, ReadResult & out);
  struct WriteOptions { bool includeMqmd = false; bool dataOnly = false; };
  Result write(const std::string & path, const Message &, const WriteOptions &);
}

// Plan 2B (not yet implemented): format.
namespace format {
  Result render(const Message&, View, const RenderOptions&, std::string& out);  // View: Char|Hex|Both|Xml|Json|Parsed|Cobol|Fixed
}

// core/platform/rfh_trace.h: replaces logTraceEntry. 28 shared-source call sites already
// use this shape, so the MFC application object implements it directly: CRfhutilApp
// installs itself in its constructor and uninstalls itself in its destructor.
RfhTrace * rfhGetTrace();
void rfhSetTrace(RfhTrace *);
```

The headers in core/ are authoritative; this section summarises them.

### Data flow

- **Put:** Main tab (file or typed data) → `fileio::read` → `Message`.
  The MQMD tab fields go through `mqmd_fields::toMqmd`, then
  `MqSession::put`.
- **Get/browse:** `MqSession::get` → `Message`. The Data tab shows
  `format::render`, and the MQMD tab shows `mqmd_fields::fromMqmd`.
- **Save:** `fileio::write` produces the same byte layout as
  `DataArea::WriteDataFile` today.

### Errors

- **Results, not exceptions:** every operation returns a `Result`. No
  exceptions cross the core API. The core never calls UI functions or
  `exit`.
- **Partial facts are kept:** an unparseable header leaves its bytes in
  `HeaderChain` as raw bytes, with a warning. Nothing is dropped.
- **Missing MQ library:** a library that cannot be loaded returns a `Result`
  naming the library path that was tried and the `dlerror()` text.
- **Reconnect:** the existing reconnect/backoff logic moves into `MqSession`,
  turned off by default as it is today.
- **Credentials:** passwords never reach trace output or saved settings.

## 5. Qt6 application (Phase 1)

- **Window:** a `QMainWindow` with a `QTabWidget` in the original tab order.
  Only Main, Data, and MQMD are enabled. The others are disabled with the
  tooltip "Not yet ported".
- **Layouts:** Qt Designer `.ui` files with Qt layouts, so the window can be
  resized and works on HiDPI screens. They keep the original labels,
  groupings, and keyboard shortcuts (Alt+Q Read Q, Alt+W Write Q, Alt+B
  Browse Q, Alt+O Open File, Alt+S Save File, Alt+E Purge Q, Alt+N/R Browse
  Next/Prev).
- **Main tab:**
  - **Fields:** Queue Manager (editable dropdown filled from local QMs and
    history), Queue Name (with Load Names), Remote QM, File Name, COBOL
    copybook, Data Size, Queue depth, Queue Type.
  - **Options:** the Put/Get option checkboxes and the User Props, Cluster
    Open, and Close Options radio groups.
  - **Buttons:** Read Q, Write Q, Browse Q, Start/Next/Prev/End Browse,
    Close Q, Purge Q (with confirmation), Open File, Save File, Clear Data,
    Clear All, Load Names, Set Conn Id, Exit.
  - **Disabled until later phases:** Save Q, Load Q, Move Q, Display Q.
  - **New: Connection box.** Bindings or Client, with channel and
    connection name or a CCDT path. `MQSERVER` and `MQCHLLIB` are honoured
    when set.
- **Data tab:** read-only monospace view with the display modes (Character,
  Hex, Both, XML, JSON, Parsed, COBOL, Fixed), the code page/EBCDIC
  choice, and Find, Find Hex, and Go To.
- **MQMD tab:** every field that `MQMDPAGE` shows, edited through
  `mqmd_fields`. MsgId, CorrelId, and GroupId are edited in hex.
- **Threading:** one worker `QThread` owns the `MqSession`. The UI disables
  conflicting actions while a call is running. A Get with a wait interval can
  be cancelled, which stops it on the next wait slice; the core passes waits
  of 1 second or less to `MQGET`.
- **Status:** the status line shows the `Result` text, matching the Windows
  wording, e.g. `RC=2033 MQRC_NO_MSG_AVAILABLE`. The optional trace goes to
  `~/.local/state/rfhutil/trace.log`.
- **Settings:** recent QMs, queues, paths, and window geometry are stored in
  `QSettings` (`~/.config/rfhutil/rfhutil.conf`).

## 6. Testing

1. **Core unit tests** (GoogleTest, no MQ needed):
   - Port the existing `tests/UnitTests` suites so they build against `core/`
     on Linux and Windows.
   - Add tests for `mqmd_fields` (round trip and invalid input), `HeaderChain`
     (RFH1, RFH2, DLQ, CICS, IMS, and malformed lengths), and every
     `format::render` view.
2. **Golden file compatibility:**
   - `tests/core/golden/` holds message files saved by Windows RFHUtil:
     plain text, with MQMD, with RFH2, EBCDIC, and binary.
   - Tests check read → write → byte-identical, plus the expected rendering
     for each file.
   - **Source of the golden files:** a Windows CI job builds a small harness
     on the **pre-refactor** `DataArea::WriteDataFile` code path, with the file
     dialog bypassed, and writes the set from fixed inputs. The files are
     created before any extraction, so they record the original behaviour.
   - Files supplied by the user are committed only with explicit approval.
3. **MQ integration tests:**
   - **Setup:** the fixture creates `RFHUTIL.TEST.*` queues on a queue
     manager named by `RFHUTIL_TEST_QM` (default `TEST_QM`) and deletes them
     afterwards.
   - **Coverage:** put/get, browse first/next/prev, MsgId/CorrelId matching,
     purge, depth, and the 2033 and 2085 error paths.
   - **Modes:** bindings is always tested. Client is tested over a test
     SVRCONN channel only when one is configured; otherwise the run reports
     it as unverified.
   - **No queue manager:** the tests are skipped with a clear message.
4. **GUI smoke test** (Qt Test, `QT_QPA_PLATFORM=offscreen`): Write Q then
   Read Q against the test queue manager, then check the Data and MQMD tabs.
5. **Windows CI** (GitHub Actions `windows-latest`, MSVC, and the MQ
   redistributable client): the MFC app builds and the core tests pass. This
   is reported as unverified locally until CI has run.

## 7. Build, packaging, delivery

- **Build:** CMake 3.21 or newer with presets `linux-debug`, `linux-release`,
  and `windows-msvc`. `MQ_HOME` defaults to `/opt/mqm` (Windows: the
  `MQ_HOME` environment variable).
- **Linux build dependencies:** `qt6-qtbase-devel`, `gtest-devel`, `cmake`,
  `gcc-c++`. Installing them needs the user's approval.
- **RPM:** `packaging/rfhutil.spec` builds the `rfhutil-qt` package with a
  `.desktop` entry and an icon. It requires the IBM MQ client/runtime
  packages and does not bundle IBM libraries.
- **Git:** `origin` is the user's fork and `upstream` is
  `ibm-messaging/mq-rfhutil`.
  - **Branch:** work happens on `feat/linux-qt-port`.
  - **Commits:** small Conventional Commit-style commits with `git commit -s`.
  - **CHANGELOG:** one entry per user-visible change.
- **Upstream order:** PR 1 adds the core extraction, CMake, and tests (no
  Qt). PR 2 adds the Qt application and packaging.
- **This spec:** it is a planning document, dropped from the upstream PRs
  unless the maintainers want it.

## 8. Phase 1 acceptance criteria

1. The `rfhutil-qt` RPM builds and installs on Fedora 44 with the IBM MQ 9.4
   client/runtime.
2. In both bindings and client mode: connect, Write Q from a file, then Read Q
   and Browse first/next/prev. The Data and MQMD tabs show the message, and
   Save File writes it.
3. Every golden file round-trips byte for byte, and files written on Linux
   match Windows output for the same message and options.
4. The core unit, golden, integration, and GUI smoke tests pass locally.
   Windows CI builds the MFC app and passes the core tests.
5. Every `DataArea` function that was moved is deleted from `DataArea` rather
   than duplicated.

## 9. Risks

- **Hidden UI dependencies:** `DataArea` code may read page state
  indirectly, as `WriteDataFile` does through `MQMDPAGE`. Each one is found
  and turned into an explicit parameter during extraction.
- **No Windows build here:** MSVC cannot run on Fedora, so a Windows
  regression only shows up in CI. Keep each commit small enough to bisect.
- **Text encoding:** the Windows build uses the MBCS/`TCHAR` setting, while
  Linux uses UTF-8. The core works on bytes plus a CCSID, and only the GUI
  edge converts to display text.
- **Upstream acceptance is uncertain:** the core extraction is still useful
  to the fork if IBM declines it.
- **Code page display:** Linux uses '.' where Windows `WideCharToMultiByte`
  may choose a best-fit character, and Linux accepts IBM CCSIDs (e.g. 273)
  that Windows rejects. Both affect display only, never message or file
  bytes. 20924 (IBM-924) maps to IBM1047 on Linux because glibc has no
  IBM924; 8 Latin-9 code points differ.
- **Debug dump files:** Debug dump files on Linux go to fixed names in /tmp
  (rfhdump.txt, namedump.txt); move them to a per-user directory before
  packaging (Plan 4).
- **Message files:** big-endian MQMD ids are kept (Windows reverses them); an
  MQMDE is read at its real offset and swapped only for host data; over-long
  MQMD fields are truncated instead of overrunning; headers are written back as
  read until the header tabs are ported; the save-RFH read option is not
  supported yet. Also unsupported: the group/segment read branch. A header whose
  length runs past the data ends the chain with a warning. Reading a path that is
  not a regular file fails and leaves the MQMD tab untouched.
