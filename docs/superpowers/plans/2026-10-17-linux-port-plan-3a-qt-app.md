# RFHUtil Linux Port, Plan 3A: The Qt6 Application (Main, Data and MQMD tabs) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Qt6 Widgets program, `rfhutil-qt`, in which a user can connect to a queue manager (bindings or client), write a message from a file with the MQMD tab's settings, read and browse messages, see them in the eight Data tab views and the MQMD tab, and save them to a file, with the Windows program's wording and behaviour.

**Architecture:**

- The program is a thin layer over the portable core. The core keeps owning MQ calls, message files, MQMD fields and rendering. The app owns widgets, threading, settings and the Windows flow rules that live in `DataArea`.
- One worker thread owns the `MqSession`. The UI submits jobs and receives results as signals. Everything that is not a widget (state, enable rules, flows, status log, settings) is plain C++/QObject code that tests drive directly.
- A few missing pieces are added to the core first, with GoogleTest tests: Windows status text, the put-side MQMD rules, queue information, a local queue manager list, and the byte-offset-to-text-position mapping for Go To.
- Windows details (captions, shortcuts, enable rules, messages) come from `docs/superpowers/specs/2026-10-17-qt-ui-inventory.md`, which cites the Windows source for every fact. The inventory is the source of truth. Where this plan says "per inventory 3.2", read that section.

**Tech Stack:** C++17, Qt 6.11 (Widgets, Test), CMake 3.21+ with AUTOMOC and AUTOUIC, GoogleTest for the core, IBM MQ 9.4 (bindings and client), the existing `rfhcore` library.

**Spec:** `docs/superpowers/specs/2026-10-15-linux-qt-port-design.md` (section 5). **Inventory:** `docs/superpowers/specs/2026-10-17-qt-ui-inventory.md`. **Previous plans:** Plans 1, 2A and 2B, complete apart from their Windows-gated tasks (2A Task 6, 2B Task 9).

**How this plan was verified:** Task 1's code was built and run in a scratch copy of the branch at `8a6a114` (Qt 6.11.2): the app target, the headless test and the full suite (615 tests) passed, the app starts under `QT_QPA_PLATFORM=offscreen`, and the project still configures with `-DRFHUTIL_BUILD_QT=OFF`. Tasks 2–16 give exact interfaces, behaviour sources and test lists; their code is written and compiled by the implementers, task by task, under review. Interface declarations here are the contract between tasks, so an implementer may add members but must not rename or remove them without reporting.

---

## Decisions made for this plan (overridable)

1. **Scope.** Plan 3A reaches the spec's acceptance criterion 2 (connect in both modes, Write Q from a file, Read Q, Browse first/next/prev, Data and MQMD tabs, Save File). All Main tab controls are drawn; those that need core work not in this plan are shown disabled with the tooltip "Not yet ported" (Plan 3B, listed at the end).
2. **Status wording.** Windows' hand-written texts (`2033 No messages in queue`), not the core's `MQGET cc=2 rc=2033 ...` text. The core gains `statusText`.
3. **Windows defects.** Fixed on purpose and listed as differences: the Cluster Open `Group` radio works; the purge error text prints `cc` and `rc` in the right order; Find also finds a match that ends exactly at the end of the data; Find Hex does not translate hex input to EBCDIC (hex is hex). Dropped: the dead `Ignore MQMD` option. Disabled until Plan 3B: `Alternate User Id` (it never worked on Windows) and the User Props radios other than `As Queue`.
4. **Shortcuts.** The Windows accelerators are Ctrl+letters that change meaning with the tab (inventory 7.1). The Qt app binds those Ctrl+letters per tab (not Tab, Enter or Backspace), and keeps the Alt mnemonics from the captions. The spec's "Alt+Q Read Q" list was the mnemonic view.
5. **Put path.** Write Q builds its MQMD with `mqmd_fields::toPutMqmd` (new; the port of `MQMDPAGE::setMessageMQMD`), not `toMqmd`, which stays the file-write path.
6. **Display code page.** The Data tab renders with `display.ansi` set from a setting (default 1252) and decodes the result with the core's `rfh_MultiByteToWideChar`, so it needs no Qt codec tables.
7. **Local queue managers.** New feature (Windows has none): the dropdown lists the queue managers found in `mqs.ini` (default `/var/mqm`) plus the history.

## Out of scope for 3A (Plan 3B and later)

Load Names (PCF queue listing), the full Set Conn Id dialog (TLS, security exit), Alternate User Id, Logical Order, Complete Msg, All Avail, Selector, user properties, the Write Q button on the Data tab (editing), the header tabs (RFH, CICS, IMS, DLQ, PS, ...), RPM packaging, trace menu, audit records.

---

## Background the engineer needs

- **Build.** `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`. Qt6 development files are installed (`qt6-qtbase-devel`). Qt tests are labelled `qt` and run with `QT_QPA_PLATFORM=offscreen` (set by the CMake test property); there is also a real Wayland display for a manual look.
- **Printing in Qt tests.** `qDebug()` prints nothing on this machine; use `printf` or `QTest::qWarn`.
- **Fake MQ.** `tests/core/MqSessionFakeTests.cpp` has an in-memory MQ (`fakeApi()` returns an `MqApi` whose entry points are fakes). Task 2 moves it to a shared header so core and Qt tests use one fake. `MqSession` has a constructor taking an injected `MqApi`.
- **Core API.** Read `docs/CODEMAP.md` sections 3 and 9. Key types: `rfh::ConnectOptions`, `PutOptions`, `GetOptions` (`mode`, `waitMs`, `cancel`, `convert...`, `matchMsgId`...), `MqSession` (`connect`, `put`, `get`, `depth`, `purge`, `closeQueue`, `disconnect`), `Message` (`mqmd`, `headers`, `body`), `Result` (`cc`, `rc`, `text`, `warnings`; a failed `get` with `rc == MQRC_NO_MSG_AVAILABLE` is `cc` 2), `fileio::read/write`, `mqmd_fields` (`MqmdFields`, `toMqmd`, `fromMqmd`, `clear`), `format::render/defaultsFor/RenderOptions::apply`.
- **Locale.** Qt sets the C library locale from the environment; the core expects `"C"` for numbers and characters. `rfhqt::useCLocaleForNumbersAndCharacters()` (Task 1) fixes that after the `QApplication` exists.
- **Threads.** `MqSession` is not thread-safe. Only the worker thread touches it. Widgets are touched only on the UI thread.
- **Text encodings.** The core works on bytes plus a CCSID; only the widgets convert to `QString`. `format::render` returns bytes in `options.display.ansi`.
- **Conventions.** Core code: tabs, Allman braces, `Result` rather than exceptions. Tests: four spaces. For the Qt layer use the core's style (tabs, Allman) for sources and four spaces in tests. Commits: `git commit -s`, Conventional Commit subject, and the trailers `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01JfjP6hazi7FDx4h8gULaS5`. Never push. MQ tests may create and delete only `RFHUTIL.TEST.*` queues on `TEST_QM`, and must leave none.
- **Index.** `tools/index/q` helps with big files (`q outline`, `q read`); use `rg` for exact names. `docs/CODEMAP.md` first.

## File structure

| Path | Status | Responsibility |
|---|---|---|
| `qt/CMakeLists.txt`, `qt/tests/CMakeLists.txt` | new | The `rfhqt` library, the `rfhutil-qt` executable, and `rfhqt_add_test` |
| `qt/src/main.cpp`, `ProcessSetup.{h,cpp}` | new | Startup, locale |
| `qt/src/MainWindow.{h,cpp}` | new | The window and its 15 tabs, menu, shortcuts, assembling the pages |
| `qt/src/SessionController.{h,cpp}` | new | Worker thread, jobs, busy state, cancel |
| `qt/src/AppSettings.{h,cpp}`, `StatusLog.{h,cpp}`, `FileTrace.{h,cpp}` | new | `QSettings` wrapper, the status log, the trace file |
| `qt/src/AppDocument.{h,cpp}` | new | Current message, tab data, browse state, enable rules |
| `qt/src/Operations.{h,cpp}` | new | The Windows flows: Read Q, Write Q, browse, close, purge, files |
| `qt/src/MainTab.{h,cpp}`, `qt/ui/MainTab.ui` | new | The Main tab |
| `qt/src/MqmdTab.{h,cpp}`, `qt/ui/MqmdTab.ui` | new | The MQMD tab |
| `qt/src/DataTab.{h,cpp}`, `qt/ui/DataTab.ui`, `FindDialogs.{h,cpp}` | new | The Data tab, Find, Find Hex, Go To |
| `core/mq/rfh_status_text.{h,cpp}` | new | `statusText` (Windows wording) |
| `core/message/rfh_mqmd_fields.{h,cpp}` | modified | `toPutMqmd`, `applyAfterPut` |
| `core/mq/rfh_mq_session.{h,cpp}` | modified | `queueInfo`, `browsePosition` |
| `core/mq/rfh_queue_managers.{h,cpp}` | new | `listLocalQueueManagers` |
| `core/format/rfh_format.{h,cpp}` | modified | `locate` |
| `tests/core/fake_mq.h` | new | The shared fake MQ |
| `tests/core/*Tests.cpp`, `qt/tests/*Tests.cpp` | new/modified | Tests |
| `docs/CODEMAP.md`, `CHANGELOG.md`, spec | modified | Documentation (Task 16) |

---

### Task 1: The Qt scaffold, verified

**Files:**
- Create: `qt/CMakeLists.txt`, `qt/src/ProcessSetup.{h,cpp}`, `qt/src/MainWindow.{h,cpp}`, `qt/src/main.cpp`, `qt/tests/CMakeLists.txt`, `qt/tests/MainWindowTests.cpp`
- Modify: `CMakeLists.txt`

This code was built and run (see "How this plan was verified"). Placeholders stand in for the three working pages until Tasks 11–13 replace them.

- [ ] **Step 1: Write the failing test.** Create `qt/tests/MainWindowTests.cpp`:

```cpp
// MainWindowTests.cpp - the application window and the process setup.

#include <QtTest>

#include <locale.h>

#include "MainWindow.h"
#include "ProcessSetup.h"

using rfhqt::MainWindow;

class MainWindowTests : public QObject
{
	Q_OBJECT

private slots:
	void tabsFollowTheWindowsOrderAndCaptions()
	{
		MainWindow window;
		const QStringList expected = {
			"Main", "Data", "MQMD", "PS", "Usr Prop", "RFH", "PubSub", "pscr",
			"jms", "usr", "other", "CICS", "IMS", "DLQ", "Conn" };
		QCOMPARE(window.tabs()->count(), expected.size());
		for (int i = 0; i < expected.size(); i++)
		{
			QCOMPARE(window.tabs()->tabText(i), expected[i]);
		}
	}

	void onlyMainDataAndMqmdAreEnabled()
	{
		MainWindow window;
		for (int i = 0; i < window.tabs()->count(); i++)
		{
			const bool phase1 = (i == MainWindow::TabMain) || (i == MainWindow::TabData) || (i == MainWindow::TabMqmd);
			QCOMPARE(window.tabs()->isTabEnabled(i), phase1);
		}
	}

	void disabledTabsSayTheyAreNotPortedYet()
	{
		MainWindow window;
		QCOMPARE(window.tabs()->tabToolTip(MainWindow::TabRfh), QString("Not yet ported"));
		QCOMPARE(window.tabs()->tabToolTip(MainWindow::TabConn), QString("Not yet ported"));
		QVERIFY(window.tabs()->tabToolTip(MainWindow::TabMain).isEmpty());
	}

	void numbersAndCharactersUseTheCLocaleAfterSetup()
	{
		setlocale(LC_NUMERIC, "");
		rfhqt::useCLocaleForNumbersAndCharacters();
		QCOMPARE(QString(setlocale(LC_NUMERIC, nullptr)), QString("C"));
		QCOMPARE(QString(setlocale(LC_CTYPE, nullptr)), QString("C"));
	}
};

QTEST_MAIN(MainWindowTests)
#include "MainWindowTests.moc"
```

- [ ] **Step 2: Add the test CMake and see the build fail.** Create `qt/tests/CMakeLists.txt`:

```cmake
# Qt Test executables. Each runs headless (QT_QPA_PLATFORM=offscreen) under ctest.

function(rfhqt_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE rfhqt Qt6::Test)
    add_test(NAME qt.${name} COMMAND ${name})
    set_tests_properties(qt.${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS qt)
endfunction()

rfhqt_add_test(rfhqt_mainwindow_tests MainWindowTests.cpp)
```

Run: `cmake --preset linux-debug`
Expected: a CMake error that the `qt` directory (added in Step 4) or `MainWindow.h` is missing. The test cannot build yet.

- [ ] **Step 3: Write the sources.** Create `qt/src/ProcessSetup.h`:

```cpp
// ProcessSetup.h - process-wide settings that the Qt application makes after creating
// its QApplication.
#pragma once

namespace rfhqt
{
	// Qt's QApplication sets the C library locale from the environment. The core and IBM's
	// shared sources format numbers with printf and parse text with the C library, and they
	// expect the "C" locale for numbers and character classes: a decimal comma would change
	// every number they print. Call this once, after the QApplication exists.
	void useCLocaleForNumbersAndCharacters();
}
```

`qt/src/ProcessSetup.cpp`:

```cpp
#include "ProcessSetup.h"

#include <locale.h>

namespace rfhqt
{
	void useCLocaleForNumbersAndCharacters()
	{
		setlocale(LC_NUMERIC, "C");
		setlocale(LC_CTYPE, "C");
	}
}
```

`qt/src/MainWindow.h`:

```cpp
// MainWindow.h - the application window: a tab widget in the original RFHUtil tab order.
// Only the Main, Data and MQMD tabs work in Phase 1; the others are shown disabled.
#pragma once

#include <QMainWindow>
#include <QStringList>

class QTabWidget;

namespace rfhqt
{
	class MainWindow : public QMainWindow
	{
		Q_OBJECT

	public:
		// The tabs, in the order of the Windows program (rfhutilView.cpp).
		enum Tab
		{
			TabMain, TabData, TabMqmd, TabPs, TabUsrProp, TabRfh, TabPubSub, TabPscr,
			TabJms, TabUsr, TabOther, TabCics, TabIms, TabDlq, TabConn,
			TabCount
		};

		explicit MainWindow(QWidget * parent = nullptr);

		QTabWidget * tabs() const { return tabWidget; }

		// The tab captions, exactly as Windows shows them (some are lower case).
		static const QStringList & tabCaptions();

		// True for the tabs that work in Phase 1.
		static bool isPhase1Tab(int index);

	private:
		QTabWidget * tabWidget;
	};
}
```

`qt/src/MainWindow.cpp`:

```cpp
#include "MainWindow.h"

#include <QLabel>
#include <QTabWidget>

namespace rfhqt
{
	const QStringList & MainWindow::tabCaptions()
	{
		static const QStringList captions = {
			"Main", "Data", "MQMD", "PS", "Usr Prop", "RFH", "PubSub", "pscr",
			"jms", "usr", "other", "CICS", "IMS", "DLQ", "Conn"
		};
		return captions;
	}

	bool MainWindow::isPhase1Tab(int index)
	{
		return (index == TabMain) || (index == TabData) || (index == TabMqmd);
	}

	MainWindow::MainWindow(QWidget * parent)
		: QMainWindow(parent), tabWidget(new QTabWidget(this))
	{
		setWindowTitle("RfhUtil");
		setCentralWidget(tabWidget);

		const QStringList & captions = tabCaptions();
		for (int i = 0; i < TabCount; i++)
		{
			// the real pages replace these placeholders in later tasks
			tabWidget->addTab(new QLabel(captions[i], tabWidget), captions[i]);
			if (!isPhase1Tab(i))
			{
				tabWidget->setTabEnabled(i, false);
				tabWidget->setTabToolTip(i, "Not yet ported");
			}
		}
	}
}
```

`qt/src/main.cpp`:

```cpp
#include <QApplication>

#include "MainWindow.h"
#include "ProcessSetup.h"

int main(int argc, char ** argv)
{
	QApplication app(argc, argv);
	rfhqt::useCLocaleForNumbersAndCharacters();

	rfhqt::MainWindow window;
	window.show();
	return app.exec();
}
```

- [ ] **Step 4: Add the CMake for the app and hook it into the project.** `qt/CMakeLists.txt`:

```cmake
# The Qt6 application: a static library of widgets and logic (so the tests can use it)
# and the rfhutil-qt executable. Built only when Qt6 Widgets is installed.

find_package(Qt6 COMPONENTS Widgets Test QUIET)
if(NOT Qt6_FOUND)
    message(STATUS "Qt6 Widgets and Test not found; the Qt application is not built "
                   "(Fedora: sudo dnf install qt6-qtbase-devel)")
    return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

add_library(rfhqt STATIC
    src/MainWindow.cpp
    src/ProcessSetup.cpp
)
target_include_directories(rfhqt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rfhqt PUBLIC Qt6::Widgets rfhcore)
target_compile_options(rfhqt PRIVATE -Wall -Wextra)

add_executable(rfhutil-qt src/main.cpp)
target_link_libraries(rfhutil-qt PRIVATE rfhqt)

if(RFHUTIL_BUILD_TESTS)
    add_subdirectory(tests)
endif()
```

In the top-level `CMakeLists.txt`, add after the `RFHUTIL_FETCH_GTEST` option:

```cmake
option(RFHUTIL_BUILD_QT "Build the Qt6 application when Qt6 Widgets is installed" ON)
```

and append at the end of the file (after the tests block, so `enable_testing()` has run):

```cmake

if(RFHUTIL_BUILD_QT)
    add_subdirectory(qt)
endif()
```

- [ ] **Step 5: Run the tests.**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug -j8 && ctest --preset linux-debug`
Expected: `100% tests passed, 0 tests failed out of 615` (614 plus `qt.rfhqt_mainwindow_tests`). `ctest --preset linux-debug -L qt` runs just the Qt test.
Run: `QT_QPA_PLATFORM=offscreen timeout 2 ./build/linux-debug/qt/rfhutil-qt; echo $?`
Expected: exit code 124 (the app was still running when stopped).
Run: `cmake -S . -B /tmp/noqt -DRFHUTIL_BUILD_QT=OFF`
Expected: configures without error.

- [ ] **Step 6: Commit.**

```bash
git add CMakeLists.txt qt
git commit -s -m "feat(qt): add the Qt6 application scaffold

A static rfhqt library, the rfhutil-qt executable and a headless Qt Test
helper (QT_QPA_PLATFORM=offscreen under ctest). The window has the 15 tabs
in the Windows order and captions; only Main, Data and MQMD are enabled,
the rest say Not yet ported. The C library locale is reset to C for numbers
and characters after QApplication starts. The Qt part is skipped when Qt6
Widgets is not installed or RFHUTIL_BUILD_QT is OFF.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JfjP6hazi7FDx4h8gULaS5"
```

---

### Task 2: Share the fake MQ between core and Qt tests

**Files:**
- Create: `tests/core/fake_mq.h`
- Modify: `tests/core/MqSessionFakeTests.cpp`, `tests/CMakeLists.txt` (include path only if needed)

Today the fake is private to `MqSessionFakeTests.cpp` (a `struct Fake` and `fakeApi()`). The Qt tests need it too, and later tasks extend it (queue attributes for `queueInfo`, `MQRC_NO_MSG_AVAILABLE`, a slow get that honours cancel).

- [ ] **Step 1: Move, do not change behaviour.** Move the fake (`Fake`, the `fake*` entry points, `fakeApi()`) into `tests/core/fake_mq.h` as an `inline`/header-only component in `namespace rfh_test`, with `fakeApi()` returning an `rfh::MqApi`. `MqSessionFakeTests.cpp` includes it. State (queues and their messages, connect/open counts) lives in an object the test can inspect and reset: expose `rfh_test::FakeMq & fakeMq()` (a function-local static) with `reset()`.
- [ ] **Step 2: Run the existing tests.** `ctest --preset linux-debug -R MqSessionFake` passes with the same count as before.
- [ ] **Step 3: Extend the fake for later tasks (additive).** Add: a per-queue map of attributes (`MQIA_Q_TYPE`, `MQIA_CURRENT_Q_DEPTH`, `MQIA_MAX_MSG_LENGTH`) answered by the fake `inq`; a way to make `get` return `MQRC_NO_MSG_AVAILABLE` when the queue is empty (check what it does today); and `FakeMq::getDelayMs` so a get sleeps in 50 ms slices, checking for MQ's wait (`MQGMO_WAIT` with `WaitInterval`) so cancel tests have something to cancel. Add a test in `tests/core/MqSessionFakeTests.cpp` for each new behaviour (for example `FakeMq.AnEmptyQueueReportsNoMessageAvailable`, `FakeMq.InquiryReturnsConfiguredAttributes`).
- [ ] **Step 4: Run all tests.** `ctest --preset linux-debug`; expect the previous total plus the new fake tests.
- [ ] **Step 5: Commit** with subject `test(core): share the fake MQ between the core and Qt tests`.

---

### Task 3: `statusText`, the Windows status wording

**Files:**
- Create: `core/mq/rfh_status_text.h`, `core/mq/rfh_status_text.cpp`
- Modify: `core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/core/StatusTextTests.cpp`

`DataArea::setErrorMsg` (`RFHUtil/DataArea.cpp:9315-9880`) has about 90 hand-written texts keyed by reason code. Inventory 3.1 lists the samples and the operation words. Port it so the app shows `2033 No messages in queue` and not `MQGET cc=2 rc=2033 ...`.

Interface (contract):

```cpp
namespace rfh
{
	struct StatusContext
	{
		const char * operation = "";	// the Windows operation word: "Open", "Close", "Get", "Put", "StartBr",
										// "Get (Browse)", "GetNext", "BrowsePrevGet", "BrowsePrevGetMsg", "Disc", "Connect", ...
		MQLONG maxMsgLenQm = 0;			// for 2030 "Message too large for queue" (0 = unknown)
		MQLONG maxMsgLenQueue = 0;
	};

	// The text Windows shows for a failed call, with its exact wording. A Result that is a
	// plain success gives an empty string. A Result without an MQ reason code (rc MQRC_NONE
	// and cc MQCC_FAILED) gives its own text unchanged.
	std::string statusText(const Result & result, const StatusContext & context);
}
```

- [ ] **Step 1: Write the failing tests** (`StatusTextTests.cpp`, GoogleTest, four-space indentation). Port the table row by row from `setErrorMsg`: one test per group of reason codes, asserting the exact text, including at least: `2033` → `2033 No messages in queue`; `2035` → `2035 Not authorized (<op>)`; `2085` → `2085 No such queue`; `2059` → `2059 Queue manager not available (<op>) - may not be started`; `2009` → `2009 Connection to QM has been lost - Q closed`; `2053` → `2053 Queue full`; `2030` with both maxima → `2030 Message too large for queue (check q and qm settings) QMgr max=%d Queue max=%d`; an unknown reason code → `*Error cc=%d rc=%d (<reason name>) Cannot <op>`, and with no reason name `*Error cc=%d rc=%d Cannot <op>` (`DataArea.cpp:9866-9876`). Add: success → empty; a non-MQ failure → its text unchanged; every `operation` word used by the Windows program is accepted. Read `setErrorMsg` completely; the test list must cover every `case`.
- [ ] **Step 2: Run them** (`ctest -R StatusText`): link error, `statusText` undefined.
- [ ] **Step 3: Implement** by porting the `switch`. Keep the Windows wording byte for byte (including odd spacing). Where Windows uses `%d` with the queue manager and queue maxima, take them from `StatusContext`.
- [ ] **Step 4: Run all tests.** Passing; report the count.
- [ ] **Step 5: Commit**: `feat(core): add the Windows status texts for failed MQ calls`.

---

### Task 4: `toPutMqmd`, the MQMD a put sends

**Files:**
- Modify: `core/message/rfh_mqmd_fields.h`, `core/message/rfh_mqmd_fields.cpp`
- Test: `tests/core/MqmdFieldsTests.cpp` (extend)

Every Windows put builds its MQMD with `MQMDPAGE::setMessageMQMD` (`MQMDPAGE.cpp:2408-2692`), whose rules differ from `buildMQMD` (the file-write path that `toMqmd` ports): ids are copied only when non-zero; report, flags, offset and group rules differ; user id, accounting token, Appl fields and Put Date/Time are taken only with Set Iden or Set All; a blank user id becomes the OS user (first 12 characters); a blank date becomes now; reply fields are blank padded, not truncated. Inventory 3.3, 4 and gap G11 describe it. After a put, the page takes only MsgId, Seq No, Offset and GroupId back from the returned MQMD (`updateMQMDafterPut`, `MQMDPAGE.cpp:2694`).

Interface (contract), in `rfh::mqmd_fields`:

```cpp
struct PutContext
{
	bool setIdentity = false;		// Set Iden Context
	bool setAll = false;			// Set All Context
	std::string osUser;				// used when the User Id field is blank (first 12 characters)
	// "now" for a blank Put Date/Time, injectable for tests; null means the real clock
	const struct tm * now = nullptr;
};

void toPutMqmd(const MqmdFields & fields, const PutContext & context, MQMD2 & mqmd);

// What the MQMD tab takes from the MQMD MQ returned for a put: MsgId, Seq No, Offset, GroupId.
void applyAfterPut(const MQMD2 & returned, MqmdFields & fields);
```

- [ ] **Step 1: Write the failing tests.** Read `setMessageMQMD` fully, then write one test per rule, each asserting the resulting MQMD bytes against a hand-built expectation (use `tests/core/mq_layout.h` offsets as the other MQMD tests do): zero ids are not copied while non-zero ones are; version-2 fields (group id, seq no, offset, original length) are set only for version 2; the report options; blank user id gets the first 12 characters of `osUser` with Set Iden and Set All but not otherwise; Appl identity/origin/name/type and Put Date/Time only with Set All (blank date and time become `now`); reply-to queue and queue manager names are blank padded; `toMqmd` is unchanged (a regression test using one existing expectation). `applyAfterPut` changes exactly four fields and leaves a sentinel in every other field.
- [ ] **Step 2: Run them**: fail to link.
- [ ] **Step 3: Implement** both functions, reusing `toMqmd`'s helpers where the rules agree; do not change `toMqmd`.
- [ ] **Step 4: Run all tests; report the count.**
- [ ] **Step 5: Commit**: `feat(core): build the put MQMD as the Windows MQMD tab does`.

---

### Task 5: Queue information, browse position and local queue managers

**Files:**
- Modify: `core/mq/rfh_mq_session.h`, `core/mq/rfh_mq_session.cpp`
- Create: `core/mq/rfh_queue_managers.h`, `core/mq/rfh_queue_managers.cpp`
- Modify: `core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/core/MqSessionFakeTests.cpp` (extend), `tests/core/QueueManagersTests.cpp`, `tests/core/MqSessionTests.cpp` (live)

Interface (contract):

```cpp
// rfh_mq_session.h
struct QueueInfo
{
	MQLONG type = 0;				// MQQT_LOCAL, MQQT_MODEL, MQQT_ALIAS, MQQT_REMOTE, MQQT_CLUSTER
	MQLONG depth = -1;				// -1 when unknown
	MQLONG maxMsgLength = 0;
	std::string resolvedName;		// the queue the name resolved to; empty when it is the same
};
// In class MqSession:
Result queueInfo(const std::string & queue, QueueInfo & info);	// opens the queue for inquiry, as depth() does
long browsePosition() const;		// 1-based position of the last browsed message; 0 when none

// rfh_queue_managers.h
namespace rfh { namespace mq {
	// Names from the QueueManager: stanzas of <mqDataPath>/mqs.ini. A missing file is a
	// success with an empty list; an unreadable one is a failure. Order as in the file.
	Result listLocalQueueManagers(const std::string & mqDataPath, std::vector<std::string> & names);
} }
```

Read `getCurrentDepth`, `getQueueType` and `openQ` (inventory G3) for the exact inquiries (`MQIA_Q_TYPE`, `MQIA_CURRENT_Q_DEPTH`, `MQIA_MAX_MSG_LENGTH`, `MQCA_...` resolved name). `browsePosition` exposes the counter `MqSession` already keeps (it may be -1/unknown inside; map unknown to 0).

- [ ] **Step 1: Write the failing tests.** `QueueInfoReturnsTypeDepthAndMaximum`, `QueueInfoReportsAResolvedAliasName`, `QueueInfoOfAMissingQueueFails` (fake reports 2085); `BrowsePositionCountsFirstNextAndPrevious`, `BrowsePositionIsZeroWithoutABrowse`; for the queue manager list a fixture `mqs.ini` written to a temp directory with two `QueueManager:` stanzas (`Name=QM1`, `Prefix=...`, plus other stanzas such as `DefaultQueueManager:` that must not appear), a missing file, an empty file, CRLF line endings and a stanza with extra spaces. A live test in `MqSessionTests.cpp` runs `queueInfo` on a real `RFHUTIL.TEST.*` queue after a put (skips without a queue manager, as the others do).
- [ ] **Step 2: Run them**: fail to build.
- [ ] **Step 3: Implement.** Keep `depth()` as it is.
- [ ] **Step 4: Run all tests; run the live tests** (`ctest -L mq`); the `RFHUTIL.TEST.*` queues must be gone afterwards (`echo "DISPLAY QLOCAL(RFHUTIL.*)" | runmqsc TEST_QM`).
- [ ] **Step 5: Commit**: `feat(core): add queue information, the browse position and a local queue manager list`.

---

### Task 6: `format::locate`, the byte offset to a text position

**Files:**
- Modify: `core/format/rfh_format.h`, `core/format/rfh_format.cpp`
- Test: `tests/core/FormatTests.cpp` (extend)

Go To takes a zero-based byte offset into the message body and shows that position in the current view. The mapping depends on the formatters' layout, so it lives next to them (inventory 5.5; gap G14). Windows: `getLineNumber`/`getLineOffset` (`DataArea.cpp:12793, 12872`).

Interface (contract), in `rfh::format`:

```cpp
struct TextPos
{
	int line = 0;		// zero-based line in the rendered text
	int column = 0;		// zero-based column in that line, in rendered characters
	int length = 0;		// characters to select: 2 for a byte in the Hex view, otherwise 1
};

// Where byte `offset` of message.body is in the text render() produces for `view`.
// Supported views: Character, Hex, Both. Returns false (and leaves pos alone) for the other
// views or an offset outside the body.
bool locate(View view, const RenderOptions & options, const Message & message, size_t offset, TextPos & pos);
```

Rules from the inventory (5.5), re-derived from the formatters rather than copied: Character without CR/LF or EDI: line `offset / 32`, column `offset % 32 + 9`; with CR/LF on, the line is the number of LF bytes before the offset and the column is `0` as Windows does; with EDI on, line 0 and column 0; Hex: line `offset / 16`, column from the digit position with the extra blanks after every 4 bytes; Both: line `offset / 16`, column in the character area. Where the inventory marks a rule UNVERIFIED, decide by rendering and checking.

- [ ] **Step 1: Write the failing tests.** For each view, render a known body (use sizes around the line lengths: 31, 32, 33, 47, 48 bytes) and assert that `locate` points at the character the render puts that byte on: take the rendered text, split into lines, and check `text[line][column]` equals the expected character (for the Hex view the first of the byte's two hex digits), for every offset of the body. This keeps the test independent of the formulas. Also: outside the body returns false; unsupported views return false.
- [ ] **Step 2: Run, implement** with the simple formulas, adjusting until the exhaustive check passes, **Step 3:** run all tests, **Step 4: Commit**: `feat(core): locate a byte offset in the Character, Hex and Both views`.

---

### Task 7: The session controller (worker thread)

**Files:**
- Create: `qt/src/SessionController.h`, `qt/src/SessionController.cpp`
- Modify: `qt/CMakeLists.txt`, `qt/tests/CMakeLists.txt`
- Test: `qt/tests/SessionControllerTests.cpp`

One worker `QThread` owns the `MqSession`. The UI submits a job (a function run on the worker) and gets the outcome as a signal on the UI thread. Only one job runs at a time; the UI disables conflicting actions while `busy()`.

Interface (contract):

```cpp
namespace rfhqt
{
	struct JobResult
	{
		rfh::Result result;
		rfh::Message message;		// for gets
		rfh::QueueInfo queue;		// filled when the job asked for it
		bool hasQueueInfo = false;
	};

	class SessionController : public QObject
	{
		Q_OBJECT
	public:
		using Job = std::function<JobResult(rfh::MqSession &)>;

		explicit SessionController(QObject * parent = nullptr);						// real MQ
		explicit SessionController(const rfh::MqApi & api, QObject * parent = nullptr);	// injected, for tests
		~SessionController() override;		// cancels, waits for the running job, disconnects, joins the thread

		bool busy() const;
		// Runs `job` on the worker. Returns false, and does not run it, while busy().
		bool submit(const QString & name, Job job);
		// Sets the flag that GetOptions::cancel points at; a waiting get stops within ~1 s.
		void cancel();
		const std::atomic<bool> * cancelFlag() const;	// pass to GetOptions::cancel; cleared when a job starts

	signals:
		void busyChanged(bool busy);
		void finished(const QString & name, const rfhqt::JobResult & result);	// on the UI thread
	};
}
Q_DECLARE_METATYPE(rfhqt::JobResult)
```

- [ ] **Step 1: Write the failing tests** (`SessionControllerTests`, Qt Test, using `fake_mq.h`): `JobRunsOnAnotherThread`; `ResultsArriveOnTheUiThread`; `SubmitWhileBusyIsRejected`; `BusyChangedEmitsTrueThenFalse` (use `QSignalSpy`); `CancelStopsAWaitingGet` (a get with `MQWI_UNLIMITED` on an empty fake queue returns within 2.5 s of `cancel()`, and its `Result` is the core's cancelled result); `CancelFlagIsClearedWhenTheNextJobStarts`; `DestroyingWhileBusyCancelsAndJoins` (no crash, no leak under `ctest -L qt`); `TheSessionPersistsBetweenJobs` (a connect job, then a put job that relies on the connection); `AJobThatThrowsBecomesAFailedResult` (catch `std::exception` and report a failed `Result` with the message, so the worker never dies).
- [ ] **Step 2: Run, implement, run all tests, commit**: `feat(qt): add the session controller with its worker thread`.

---

### Task 8: Settings, the status log and the trace file

**Files:**
- Create: `qt/src/AppSettings.{h,cpp}`, `qt/src/StatusLog.{h,cpp}`, `qt/src/FileTrace.{h,cpp}`
- Test: `qt/tests/AppSettingsTests.cpp`, `qt/tests/StatusLogTests.cpp`, `qt/tests/FileTraceTests.cpp`

Interfaces (contract):

```cpp
class AppSettings		// wraps QSettings; the constructor takes a file path so tests use a temp file
{
public:
	explicit AppSettings(const QString & filePath = QString());	// default: ~/.config/rfhutil/rfhutil.conf
	QStringList recentQueueManagers() const;  void addQueueManager(const QString &);	// most recent first, no duplicates (case-sensitive), max 30
	QStringList recentQueues(const QString & qm) const;  void addQueue(const QString & qm, const QString & queue);	// max 30 per queue manager
	QString lastDirectory(const QString & kind) const;  void setLastDirectory(const QString & kind, const QString & dir);	// kind: "open", "save", "copybook"
	QByteArray windowGeometry() const;  void setWindowGeometry(const QByteArray &);
	int displayCodePage() const;  void setDisplayCodePage(int);	// default 1252
	bool showSystemQueues() const;  void setShowSystemQueues(bool);	// default false
};

class StatusLog			// the Main tab's status box (inventory 3.1)
{
public:
	// Adds `text` (several lines may be joined with CR LF) stamped with the local time as
	// "%H.%M.%S " and CR LF, newest entry on top. The whole log is cut at 8191 bytes back to a
	// line boundary and trailing CR/LF are removed. `now` is injectable for tests.
	void add(const QString & text, const QTime & now = QTime::currentTime());
	QString text() const;
	void clear();
};

class FileTrace : public RfhTrace	// core/platform/rfh_trace.h; installs itself with rfhSetTrace
{
public:
	explicit FileTrace(const QString & path = QString());	// default: $XDG_STATE_HOME/rfhutil/trace.log, else ~/.local/state/rfhutil/trace.log
	bool isTraceEnabled() override;  void logTraceEntry(const char *) override;  void dumpTraceData(const char *, const unsigned char *, unsigned int) override;
	void setEnabled(bool);
};
```

- [ ] **Step 1: Write the failing tests.** Settings: round trips, most-recent-first order, duplicate moves to the top, the 30 limit, per-queue-manager lists, defaults, persistence across two `AppSettings` objects on the same file. StatusLog: one entry has the `HH.MM.SS ` prefix; two entries are newest first; a long log is cut at a line boundary under 8191 bytes; trailing CR/LF removed; multi-line text keeps its line breaks; `clear`. FileTrace: nothing written while disabled; creates the directory; appends entries with a timestamp; `dumpTraceData` writes a hex dump; honours `XDG_STATE_HOME`.
- [ ] **Step 2–4: Run, implement, run all, commit**: `feat(qt): add the settings, the status log and the trace file`.

---

### Task 9: The document model and the enable rules

**Files:**
- Create: `qt/src/AppDocument.{h,cpp}`
- Test: `qt/tests/AppDocumentTests.cpp`

`AppDocument` holds what the Windows `DataArea` holds for the three tabs, as plain state with change signals, and the **enable rules** of `General::UpdatePageData` (inventory 2.1 and 3.4), as a pure function. Everything is testable without a widget.

Interface (contract):

```cpp
struct BrowseState { bool active = false; bool previousAllowed = false; };

struct MainEnables		// what UpdatePageData enables (true = enabled)
{
	bool openFile, clearData, clearAll, readQ, purgeQ, writeQ, browseQ, startBrowse, closeQ, loadNames;
	bool browseNext, browsePrev, endBrowse;
	bool convert;		// the Get options that are off while browsing
};

class AppDocument : public QObject
{
	Q_OBJECT
public:
	rfh::Message message;				// the current message (headers and body)
	rfh::MqmdFields mqmd;				// the MQMD tab's values
	rfh::format::RenderOptions render;	// the Data tab's settings
	std::string fileSource;				// "Message Data" or "Message Data (N) from <path>"
	QString queueManager, queueName, remoteQm;
	QString copybookPath;
	rfh::QueueInfo queueInfo;			// last known
	BrowseState browse;
	bool connected = false;

	static MainEnables enablesFor(const BrowseState & browse, bool connected);	// pure

	void clearData();					// Clear Data: empties the message (inventory 3.2)
	void clearAll();					// Clear All: also resets the MQMD tab values and file name
	void setMessage(const rfh::Message & m);	// emits messageChanged
signals:
	void messageChanged();
	void stateChanged();
};
```

- [ ] **Step 1: Write the failing tests.** `enablesFor`: all four combinations of browse active/not, with and without `previousAllowed`, and connected/not, asserted against the table in inventory 2.1 (browsing: Browse Next and End Browse on, Browse Prev only when allowed, everything else off; otherwise all on except Browse Next, Browse Prev and End Browse, with Close Q only while connected). `clearData` empties the body and headers but keeps the MQMD; `clearAll` also clears MQMD values to the state `mqmd_fields::clear` gives; signals fire once per change.
- [ ] **Step 2–4: Run, implement, run all, commit**: `feat(qt): add the document model and the Main tab enable rules`.

---

### Task 10: The operations (Windows flows)

**Files:**
- Create: `qt/src/Operations.{h,cpp}`
- Test: `qt/tests/OperationsTests.cpp`

`Operations` implements what each Main tab button does, using the `SessionController`, `AppDocument`, `StatusLog` and the core, with the Windows flow rules (inventory 3.1–3.4; gap G18). It has no widgets: the Main tab calls it and displays its state.

Interface (contract):

```cpp
struct MainOptions		// the Main tab's check boxes and radios (inventory 3.3)
{
	// connection
	rfh::Mode mode = rfh::Mode::Bindings;
	QString channel, connName, ccdtPath;
	// put
	bool newMsgId = true, newCorrelId = false, setIdentityContext = false, setAllContext = false;
	rfh::BindOption bind = rfh::BindOption::AsQueueDefinition;
	// get
	bool getByMsgId = false, getByCorrelId = false, convert = false;
	// close
	rfh::CloseOption closeOption = rfh::CloseOption::None;
	// files
	int fileCodePage = 0;					// 0 / empty = decide
	rfh::fileio::ReadOptions read;  rfh::fileio::WriteOptions write;
};

class Operations : public QObject
{
	Q_OBJECT
public:
	Operations(AppDocument &, SessionController &, StatusLog &, QObject * parent = nullptr);
	void setOptions(const MainOptions &);

	void readQ();   void writeQ();   void browseQ();
	void startBrowse();   void browseNext();   void browsePrev();   void endBrowse();
	void closeQ();   void purgeQ();					// purge asks the confirmation through a signal, see below
	void openFile(const QString & path);   void saveFile(const QString & path);
	void clearData();   void clearAll();

signals:
	void confirmPurge(const QString & queue);			// the UI answers with purgeConfirmed(true/false)
	void statusChanged();								// the log changed
public slots:
	void purgeConfirmed(bool yes);
};
```

Flow rules to implement and test (each is a test named after it): connect on demand, reconnect when the queue manager name changes (G18, `checkConnection`); Read Q = open, one destructive get, depth refresh; Write Q builds the MQMD with `toPutMqmd` (with `PutContext` from Set Iden/Set All and the OS user from `getpwuid(geteuid())`), sends the body with the MQMD `Format` as typed, then `applyAfterPut`; Browse Q = open, one browse, close; Start Browse = browse first and the state machine on; Browse Next; Browse Prev allowed only after a browse next/prev as in the inventory; a failed Browse Next closes, disconnects and ends the browse; End Browse closes and disconnects; Close Q = close then disconnect; Purge = confirmation (`Purge all messages from queue %s?`), no purge when answered No, status text with `cc`/`rc` in the right order; Get by Msg Id / Correl Id apply only when the MQMD tab's id is not all zero (inventory 3.3); Convert sets `convertCcsid` from the MQMD tab's Code Page when non-empty (else the queue manager's) and `convertEncoding` to 546 for PC integers or 273; every failure goes through `statusText` with the right operation word; the status log gets success texts per inventory 3.2; after a get, `defaultsFor(message)` is applied with `RenderOptions::apply` and the MQMD tab values come from `fromMqmd`; open file uses `fileio::read` with the Main options, applies `defaultsFor(ReadResult)` and shows `Message Data (N) from <path>`; save file uses `fileio::write` and shows its text; Set Iden Context clears after a get (inventory 3.4); the title/queue name bookkeeping.

- [ ] **Step 1: Write the failing tests** against the fake MQ with a real `SessionController` (inject the fake `MqApi`), waiting for `finished` with `QSignalSpy`. One test per rule above, asserting the document state, the status log text and the calls the fake saw (open/get/close/disc counts and flags).
- [ ] **Step 2: Run them**, then implement flow by flow. **Step 3:** run all tests and the live tests (`ctest -L mq`). **Step 4: Commit**: `feat(qt): implement the Main tab operations with the Windows flow rules`.

---

### Task 11: The Main tab

**Files:**
- Create: `qt/src/MainTab.{h,cpp}`, `qt/ui/MainTab.ui`
- Modify: `qt/CMakeLists.txt`, `qt/src/MainWindow.cpp`
- Test: `qt/tests/MainTabTests.cpp`

Build `MainTab.ui` in Qt Designer's XML from inventory 2.1: every control with its caption (verbatim, with the `&` mnemonic), group boxes, tab order, defaults and read-only flags. Layouts are Qt layouts (not pixel positions), so the window resizes and works on HiDPI. The controls Phase 3A cannot honour are present and disabled with the tooltip "Not yet ported": Alternate User Id, Logical Order, Complete Msg, All Avail, Selector, the User Props radios other than As Queue, Load Names, Save Q, Load Q, Move Q, Display Q, and Set Conn Id until Plan 3B. The Connection box is new: a Bindings/Client choice, channel and connection name or a CCDT path (`MQSERVER` and `MQCHLLIB` are honoured by MQ itself when none is given). The QM combo is editable and filled from `listLocalQueueManagers` plus the history; the queue combo from the history for the chosen queue manager.

`MainTab` reads the widgets into `MainOptions`, calls `Operations`, and reflects `AppDocument` (file name, data size, queue depth, queue type, status log) and `enablesFor` after every change.

- [ ] **Step 1: Write the failing tests** (`MainTabTests`): captions and mnemonics match the inventory table (iterate it: a QStringList in the test, copied from inventory 2.1); read-only fields are read-only; defaults (New Msg Id on, others off, Cluster Open As Queue, Close None); the unported controls are disabled with the tooltip; typing options and calling `options()` gives the matching `MainOptions`; each of the four Cluster Open radios (including Group) selects its `BindOption`; the enable rules for browse active/inactive reflect on the buttons; clicking Read Q calls `Operations::readQ` (use a test double or spy on the fake MQ); the Purge confirmation shows a `QMessageBox` (test with a single-shot timer that accepts/rejects it); the status box shows the log; the Ctrl+ shortcuts from inventory 7.1 for the Main tab trigger the buttons (Ctrl+Q Read Q, Ctrl+W Write Q, Ctrl+B Browse Q, Ctrl+T Start Browse, Ctrl+N, Ctrl+R, Ctrl+M End Browse, Ctrl+K Close Q, Ctrl+E Purge Q, Ctrl+J Clear All, Ctrl+O Open, Ctrl+S Save) and not when another tab is current.
- [ ] **Step 2: Run, build the `.ui` and the class, run all tests, then look at it** (`./build/linux-debug/qt/rfhutil-qt` on the Wayland desktop, if a display is available; otherwise grab a screenshot with `QWidget::grab()` in a test and view the PNG). **Commit**: `feat(qt): add the Main tab`.

---

### Task 12: The MQMD tab

**Files:**
- Create: `qt/src/MqmdTab.{h,cpp}`, `qt/ui/MqmdTab.ui`
- Test: `qt/tests/MqmdTabTests.cpp`

Build from inventory 2.2 and 4: every field `MQMDPAGE` shows, edited through `mqmd_fields::MqmdFields` (inventory 4.4 maps every Windows field to a member, and flags fields with no counterpart: show those disabled). Message Id, Correl Id and Group Id are edited in hex with the Windows validation (inventory 4.2). Report options are button groups whose ids map `checkedId()` -1 to "none" (my notes: use `QButtonGroup` ids 0–3 so -1 round-trips). Combo and list choices carry the exact texts and values from the inventory. Code Page and Encoding follow the Windows rules (digits only; PC/HOST choice).

Interface (contract):

```cpp
class MqmdTab : public QWidget
{
	Q_OBJECT
public:
	explicit MqmdTab(AppDocument &, QWidget * parent = nullptr);
	rfh::MqmdFields fields() const;			// the widgets as MqmdFields (what Write Q needs)
	void setFields(const rfh::MqmdFields &);	// the widgets from MqmdFields (after a get)
signals:
	void fieldsEdited();
};
```

- [ ] **Step 1: Write the failing tests.** Round trip: for a `MqmdFields` with every member set to a non-default value, `setFields` then `fields()` returns the same (compare member by member, with a failure message naming the member). Defaults equal `mqmd_fields::clear`'s values. Hex id fields: accept `0-9A-F`, upper-case what is typed, reject odd lengths and other characters with the Windows message (read it from the inventory), pad as Windows does; an id longer than 48 hex digits is refused. Report radio groups: -1/none round trip. Choice texts and values match inventory 4.1 for each combo. After `AppDocument::messageChanged` from a get, the tab shows the message's MQMD (`fromMqmd`). Read-only rules (inventory MQMDPAGE.cpp:396-455) apply. Editing emits `fieldsEdited`.
- [ ] **Step 2–3: Run, build, run all tests; look at it; commit**: `feat(qt): add the MQMD tab`.

---

### Task 13: The Data tab and its dialogs

**Files:**
- Create: `qt/src/DataTab.{h,cpp}`, `qt/ui/DataTab.ui`, `qt/src/FindDialogs.{h,cpp}`
- Test: `qt/tests/DataTabTests.cpp`, `qt/tests/FindDialogsTests.cpp`

Build from inventory 2.3 and 5. A read-only monospace `QPlainTextEdit` (a fixed-pitch font; no line wrapping, as Windows) shows `format::render` output for the chosen view (Character, Hex, Both, XML, Parsed, COBOL, JSON, Fixed) with the options (Char format radios, numeric formats, CR/LF, EDI, Indent, Check data; the BOM box stays hidden as on Windows). The render runs on the UI thread for messages up to a threshold and is cheap enough for the 100 MB limit's smaller cases; if rendering a big body takes visible time, run it in a worker job (decide by measuring: render a 10 MB body in each view, report the times, and put anything over about 200 ms on a worker with the busy cursor). The result bytes are in `display.ansi` (from `AppSettings`, default 1252); decode them with the core's `rfh_MultiByteToWideChar` (not Qt codecs), tolerating the code page's DBCS.

Behaviour to implement from the inventory: the view changes re-render; the COBOL view asks for a copy book when none is set (`QFileDialog`, filter `All (*.*);;COBOL files (*.cbl *.cpy);;Text (*.TXT)`, title `COBOL copy book file`), then passes `copybookPath`; status lines for render warnings and failures (the COBOL failure texts); `Allow Edit` and `Write Q` are shown disabled ("Not yet ported"); the Browse Next / Prev buttons on this tab call the same operations as the Main tab.

**Find** (Ctrl+F): a small modeless dialog `Find what`, Direction Up/Down (Down selected), `Find Next`/`Cancel`; no match-case or whole-word options. Character, Hex and Both search the raw message bytes (typed text as bytes, at most 255, EBCDIC translation of the typed text when the character format is EBCDIC) starting one byte after the previous hit, no wrap, **no message on a miss** (the position resets and the view moves to the top), and the hit is shown with `format::locate` (selecting the first byte only: 2 characters in Hex, 1 elsewhere), scrolling so the hit line is the first visible line. XML, Parsed, JSON and Fixed search the rendered text with the same rules, selecting one character. **Difference from Windows (fix):** a match that ends exactly at the end of the data is found.
**Find Hex** (Ctrl+Z): `Hex Finder`, an upper-casing edit of up to 16 characters, Direction Up/Down, `Find`/`Cancel`, error label with `No data entered`, `Odd number of characters`, `Invalid hex character(s)`; the hex is not translated to EBCDIC (difference from Windows). Character, Hex and Both only.
**Go To** (Ctrl+G): modal `Go to offset`, label `Enter offset (0 - <size-1>)`, a digits-only edit, errors `Offset must be a number` and `Offset too large or < 0`; Character, Hex and Both only; uses `format::locate`.
Enable rules: only on the Data tab, with data present, and not while another Find dialog is open.
The Ctrl+ shortcuts on the Data tab (inventory 7.1) choose views and options (Ctrl+Q Parsed, Ctrl+B Both, Ctrl+T Character, Ctrl+K COBOL, Ctrl+E EBCDIC, Ctrl+L XML, Ctrl+H Hex, and so on); read the table and implement exactly those.

- [ ] **Step 1: Write the failing tests.** `DataTabTests`: each view shows the same text as `format::render` for a fixed message (compare with a render done in the test, decoded); the CP1252 decode of 0xE9 gives `é`; a DBCS body with `display.ansi = 932` decodes; changing view/option re-renders once; the COBOL view with no copy book asks (stub the dialog) and with one shows the table; render failures show their text; the Allow Edit and Write Q controls are disabled; shortcuts select the views only while the Data tab is current. `FindDialogsTests`: the Find sequence on a message with three hits goes hit 1, 2, 3, then miss (view at top, no message box), then hit 1 again; a hit ending at the last byte is found; Direction Up; the EBCDIC typed-text translation; the Hex view highlight is 2 characters on the first byte; Find Hex validation messages and a successful find of `4142` in `xxABxx`; Go To validation messages and the cursor line/column for offsets 0, 31, 32, 33 in the Character view and 0, 15, 16 in Hex (cross-checked against `locate`).
- [ ] **Step 2–3: Run, build, run all, look at it, commit**: `feat(qt): add the Data tab with Find, Find Hex and Go To`.

---

### Task 14: Assemble the window

**Files:**
- Modify: `qt/src/MainWindow.{h,cpp}`, `qt/src/main.cpp`
- Test: `qt/tests/MainWindowTests.cpp` (extend)

Create the shared objects once in `MainWindow` (`AppSettings`, `AppDocument`, `StatusLog`, `SessionController`, `Operations`, `FileTrace`) and give the three pages to the tab widget, replacing the placeholders. Add: the File menu (Open Ctrl+O, Save Ctrl+S, Exit), the MQ menu (Connect/Disconnect as in `rfhutilView.cpp:2462-2506`, printing `Connected to <qm>`), the Read and Write file-option menu items with check marks and the defaults from inventory 3.6 (Include MQMD on, the others off), View > System Queues, and Search > Find / Find Hex / Go To. Menu items follow the same enable rules as the buttons (for example File > Open is disabled while browsing). Save and restore the window geometry through `AppSettings`. `Alt+N`/`Alt+P` step through the tabs and wrap from DLQ to Main, skipping Conn (inventory 1.2). The window title is `RfhUtil` and becomes the bare queue name after the operations that set it (inventory 1.1). Show `*****MQ not installed` in the status log at start-up when the MQ library cannot be loaded (bindings: `MqLoader` reports it).

- [ ] **Step 1: Write the failing tests**: the menus exist with the right items and check marks; unchecking Include MQMD changes `WriteOptions::includeMqmd` for the next Save; Alt+N from Main goes to Data; from DLQ wraps to Main; geometry persists across two window lifetimes (temp settings file); the title changes after a Read Q on the fake; the no-MQ message appears when the loader reports it (inject the failure).
- [ ] **Step 2–3: Run, implement, run all, commit**: `feat(qt): assemble the window with its menus, shortcuts and settings`.

---

### Task 15: The GUI smoke test against a real queue manager

**Files:**
- Create: `qt/tests/GuiSmokeTests.cpp`
- Modify: `qt/tests/CMakeLists.txt`

The spec's GUI smoke test (section 6, item 4). It uses the real `MqSession` (bindings) against `TEST_QM` and queues named `RFHUTIL.TEST.GUI.*`, created and deleted by the fixture. It is skipped, with a clear message, when no queue manager is reachable (reuse the skip logic of `tests/core/MqSessionTests.cpp`); label it `qt` and `mq`.

- [ ] **Step 1: Write the test.** `writeThenReadShowsTheMessageOnTheDataAndMqmdTabs`: start a `MainWindow` with a real controller, set the Main tab (`TEST_QM`, the test queue), load a file with known text through the Main tab's Open File path, set a Format and a Persistence on the MQMD tab, Write Q, clear the data, Read Q, and check: the status log has no failure, the Data tab's Character view contains the text, the Hex view the right bytes, the MQMD tab shows the Format and Persistence and a new Message Id. `browseWalksFirstNextAndPrevious`: put three messages, Start Browse, Browse Next twice, Browse Prev, End Browse; check the Data tab at each step and the enable rules. `saveFileRoundTrips`: Save File after a Read Q writes a file that `fileio::read` reads back with the same body and MQMD fields. `aMissingQueueShowsTheWindowsText`: Read Q on a queue that does not exist shows `2085 No such queue` in the status box. `clientModeConnectsOverTheChannel`: skipped unless `RFHUTIL_TEST_CLIENT` is set (the existing client test's variables: see `tests/core/MqSessionTests.cpp`), uses the Connection box.
- [ ] **Step 2: Run** (`ctest --preset linux-debug -L qt`), then confirm no `RFHUTIL.*` objects remain: `echo "DISPLAY QLOCAL(RFHUTIL.*)" | runmqsc TEST_QM` prints `AMQ8147E`.
- [ ] **Step 3: Commit**: `test(qt): add the GUI smoke tests against a real queue manager`.

---

### Task 16: Documentation

**Files:**
- Modify: `docs/CODEMAP.md`, `CHANGELOG.md`, `docs/superpowers/specs/2026-10-15-linux-qt-port-design.md`, this plan's Execution notes, `CLAUDE.md` if the commands change

- [ ] **Step 1: CODEMAP.** Add the Qt layer (`qt/`): modules, the controller and threading rule, the flow of a button press, the test layout and labels, how to run the app, the "where do I look for" rows for the new code (Windows flow rules, enable rules, shortcuts, Find, status text), and update the status section.
- [ ] **Step 2: CHANGELOG.** Added: the Qt application with the three tabs, `statusText`, `toPutMqmd`, `queueInfo`, `listLocalQueueManagers`, `locate`, the Qt tests. Changed: none for Windows. List the deliberate differences from Windows (inventory decisions 3 and 4 above).
- [ ] **Step 3: Spec.** Section 5: replace the Alt-shortcut list with the Ctrl+ table as built; add the new core declarations to section 4; section 9: the differences (status text kept, the four fixed Windows defects, disabled controls, the display-code-page setting).
- [ ] **Step 4: Execution notes** in this plan: counts after each task, review-fix commits, what is verified and what is not (no Windows build, no screenshots of the app on HiDPI, MQ client mode only if configured).
- [ ] **Step 5: Run everything.** Full ctest, `ctest -L qt`, `ctest -L mq`, `ctest -L memcheck`. Commit: `docs: record Plan 3A in the code map, changelog and spec`.

---

## Acceptance criteria for Plan 3A

1. `cmake --build` produces `rfhutil-qt`; it starts on this machine's Wayland session and headless.
2. In bindings mode against `TEST_QM`: Write Q from a file, Read Q, Start Browse / Next / Prev / End Browse, Close Q and Purge Q work; the Data tab shows all eight views; the MQMD tab shows the message's MQMD; Save File writes it back, with the Windows status wording.
3. Client mode works through the Connection box when a channel is configured (reported unverified otherwise).
4. Every new core function has GoogleTest tests; every Qt class has Qt Test tests; the GUI smoke tests pass against `TEST_QM`; the existing 614 tests still pass; `ctest -L memcheck` still passes.
5. No `RFHUTIL.*` queue is left behind.

## Not in this plan (Plan 3B and later)

- Load Names (PCF queue listing, `listQueues`), the full Set Conn Id dialog with TLS and security exit fields (`ConnectOptions` additions), Alternate User Id, Logical Order, Complete Msg, All Avail, Selector, group id matching, `PutOptions::logicalOrder` and syncpoint, user properties.
- Editing on the Data tab (Allow Edit, Write Q).
- The header tabs (RFH, CICS, IMS, DLQ, PS, PubSub, ...), after which `Message::headers` can be built by the app.
- Packaging (RPM, desktop entry, icon), moving the debug dump files out of `/tmp`.
- The Windows-gated tasks (2A Task 6, 2B Task 9) remain with the owner's Windows build.

## Execution notes

(Filled in during execution.)
