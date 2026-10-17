# RFHUtil Linux Port, Plan 3: The Qt6 Application (Main, Data and MQMD tabs) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Qt6 Widgets program, `rfhutil-qt`, in which a user can connect to a queue manager (bindings or client), write a message from a file using the MQMD tab, read and browse messages, see them in the eight Data tab views and on the MQMD tab, and save them to a file, with the Windows program's wording and behaviour. Every gap that the inventory finds between the core and the Windows program (inventory section 6, G1–G18) is closed by a core function with its own tests, or is wired in the app with a named test, and the MQMD put and get paths move from the MFC layer into the core.

**Architecture:**

- `qt/` holds the application as a static library `rfhqt` plus the `rfhutil-qt` executable, with Qt Test executables for each class. It is a thin layer over `rfhcore`: widgets, one worker thread, settings, and the Windows flow rules that live in `DataArea`.
- The gap tasks come first (Tasks 4–19). Each adds one function or option to `core/` with GoogleTest tests, before any widget uses it. The core stays free of UI: Task 3 adds a test that fails if `core/` mentions a dialog, an exit call or a UI toolkit.
- One worker `QThread` owns the `MqSession` (Task 20). The UI submits jobs and receives results as signals; a Get with a wait interval is cancellable.
- `Operations` (Task 23) reproduces the button flows of `General.cpp` and `DataArea` on top of the controller and the document model; the three tab classes only translate between widgets and plain structs.
- Task 30 (Windows, gated) makes `MQMDPAGE`'s put, get and ID-display paths delegate to the core, so one implementation remains (design section 8, criterion 5).

**Tech Stack:** C++17, Qt 6.11 (Widgets, Test), CMake 3.21+ with AUTOMOC and AUTOUIC, GoogleTest 1.17 for the core, IBM MQ 9.4 (bindings and client, loaded at run time), Perl 5 for the one gated edit script.

**Spec:** `docs/superpowers/specs/2026-10-15-linux-qt-port-design.md` (sections 5, 6, 8, 9). **Inventory:** `docs/superpowers/specs/2026-10-17-qt-ui-inventory.md`, cited below by section ("inv 3.2"); it gives the Windows source for every fact. **Previous plans:** Plan 1, Plan 2A (`2026-10-16-linux-port-plan-2a-message-files.md`) and Plan 2B (`2026-10-16-linux-port-plan-2b-data-views.md`), complete apart from their Windows-gated tasks (2A Task 6, 2B Task 9). This plan replaces the narrower draft `2026-10-17-linux-port-plan-3a-qt-app.md`, which deferred several gaps; Task 1 below is that draft's Task 1, unchanged.

**Owner decisions this plan follows:**
- This is a fork tested in the wild; nothing here is written for an upstream PR.
- Memory-safety bugs in IBM's shared sources are fixed with a test through a `tools/port/*.pl` script; IBM behaviour bugs are pinned by tests; output for valid input stays byte-identical to IBM's (CLAUDE.md, CODEMAP section 7).
- Windows changes that cannot be compiled here wait for the owner's Windows build (Task 30), as 2A Task 6 and 2B Task 9 do.
- The Windows status wording is kept (design section 5: "matching the Windows wording").
- Decided by the owner on 2026-10-17: the four Windows defects are fixed on purpose (the Cluster Open `Group` radio, the purge error text order, Find at the end of the data, Find Hex without EBCDIC translation); the local queue managers are listed from `mqs.ini`; passwords are kept in memory only, for now; and a golden message file set is created (Task 29).

**Verified before writing:** Task 1's code was built and run in a scratch copy of the branch at `8a6a114` on Fedora 44 with Qt 6.11.2: the app target, the headless Qt test and the full suite (615 tests) passed, the app starts under `QT_QPA_PLATFORM=offscreen`, and the project still configures with `-DRFHUTIL_BUILD_QT=OFF`. **Tasks 2–31 are not compiled.** They give exact interfaces (the contract between tasks), the Windows source to port with file and line, the test names with what each asserts, and the commands; the implementer writes and compiles the code under test and review. An implementer may add members to an interface but must report before renaming or removing one.

---

## Gap-to-task map (inventory section 6)

| Gap | What | Task |
|---|---|---|
| G1 | List local queue managers | 11 |
| G2 | Queue names (Load Names, PCF) | 12 |
| G3 | Queue type, depth, maximum length, resolved name | 9 |
| G4 | Browse position | 10 |
| G5 | Get options (group id, logical order, complete msg, all avail, selector, input shared, alternate user; QM platform/level) | 13 |
| G6 | Put options (logical order, syncpoint and commit, alternate user; after-put MQMD update) | 14 and 6 |
| G7 | Connection: (a) MQSERVER-style strings, (b) TLS, security exit, local address; (c) reconnect/heartbeat is not Phase 1 | 15, 16 |
| G8 | User properties mode | 17 |
| G9 | Cluster open options (no gap) | wired in 24 (Group radio), tested in 23 |
| G10 | Cancel for a waiting Get (no core gap) | 20 (controller), 24 (wait interval field and Cancel) |
| G11 | Put-side and get-side MQMD semantics; ID display | 6, 7, 8 |
| G12 | Windows status texts | 4 |
| G13 | OS user name | 5 |
| G14 | Offset to text position; the byte search | 18 (locate), 26 (search) |
| G15 | Purge as Windows does it | 19 |
| G16 | File options | 23 (mapping tests), 29 (golden round trips); Save Headers is an open point |
| G17 | Settings, trace, audit records | 21 (settings, trace); audit records are an open point |
| G18 | Flow rules in `DataArea` | 22 (enable rules), 23 (flows) |

## Acceptance-criteria map (design section 8)

| Criterion | Plan 3 tasks |
|---|---|
| 1. The `rfhutil-qt` RPM builds and installs | None: packaging is Plan 4. Tasks 1 and 27 make the executable that Plan 4 packages. |
| 2. Connect in both modes, Write Q from a file, Read Q, Browse first/next/prev; Data and MQMD tabs show the message; Save File writes it | 1, 4–7, 9, 10, 13–16, 20, 22–28 |
| 3. Golden files round-trip; Linux files match Windows | 29 (the golden set, its round-trip tests and the Windows comparison procedure), 23 (Save File and Open File use `fileio`), 28 (`saveFileRoundTrips`). The set is built from the MQ structure layouts, not by Windows: it is confirmed against Windows only when the owner runs the procedure of Task 29. |
| 4. Core unit, golden, integration and GUI smoke tests pass locally; Windows CI builds the MFC app | 2, 3 and every core task (GoogleTest), 20–27 (Qt Test), 28 (GUI smoke against `TEST_QM`). Windows CI is the owner's. |
| 5. Every moved `DataArea` function is deleted from `DataArea`, not duplicated | 29 (the `MQMDPAGE` put, get and ID-display paths), with 2A Task 6 and 2B Task 9. Until those run, `MQMDPAGE.cpp` still holds its own copies. |

## Windows-visible changes (all unverified here: MSVC cannot run on this machine)

| Task | Change | Why it is Windows-visible |
|---|---|---|
| 5 | `rfh_current_user()` in `core/platform/rfh_platform.{h,cpp}`, with a Windows branch (`GetUserNameEx`) | `rfh_platform.cpp` is compiled by `RFHUtil.vcxproj`, `Client.vcxproj` and `UnitTests.vcxproj` |
| 5 | `PlatformTests.cpp` gains a test | `UnitTests.vcxproj` builds that file |
| 6, 7, 8 | New functions in `core/message/rfh_mqmd_fields.{h,cpp}` and new `core/message/rfh_id_display.{h,cpp}` | `rfh_mqmd_fields.cpp` is compiled by both Windows projects only once 2A Task 6 has run (its script adds it); `rfh_id_display.cpp` is added to the projects only by Task 30, so until then it is Linux only |
| 29 | `tools/port/port-mqmdpage-putget.pl` edits `RFHUtil/MQMDPAGE.{h,cpp}` and both `.vcxproj`/`.filters` files | These are MFC files; the script is the only place Plan 3 edits IBM's files |
| 29 | Golden files, their generator, tests and a comparison script | None in the sources; confirming them needs a Windows run of the procedure in `tests/core/golden/MANIFEST.md` |
| 4, 9–19, 20–28 | `core/mq/*`, `core/format/*`, `qt/*` | Not compiled by any Windows project; Linux only |

The rest of Plan 3 does not touch Windows-compiled sources. No task edits an `RFHUtil/*` file by hand.

---

## Background the engineer needs

- **House rules, build and test commands, the index tool and the layout** are in `CLAUDE.md` and `docs/CODEMAP.md` (sections 2, 3, 5, 6, 7, 9); this plan does not restate them. `RFHUtil/DataArea.cpp` is 25,800 lines: use `tools/index/q outline` and `q read`, never open it whole.
- **Qt.** Qt 6.11.2 with `qtbase-devel`, Widgets, UiTools, Test, Designer, `uic` and `moc` are installed; nothing needs installing. Qt tests are labelled `qt` and run with `QT_QPA_PLATFORM=offscreen` (set by the CMake test property). `qDebug()` prints nothing on this machine: use `printf` in tests.
- **Fake MQ.** `tests/core/MqSessionFakeTests.cpp` has an in-memory MQ; Task 2 shares it. `MqSession(const MqApi &)` takes an injected API.
- **Core API in use.** `rfh::ConnectOptions`, `PutOptions`, `GetOptions` (`mode`, `waitMs`, `cancel`, `convert*`, `matchMsgId`/`matchCorrelId`), `MqSession`, `Message`, `Result` (`cc`, `rc`, `text`, `warnings`; a failed `get` with `rc == MQRC_NO_MSG_AVAILABLE` has `cc` 2), `fileio::read/write`, `mqmd_fields::MqmdFields/toMqmd/fromMqmd/clear`, `format::render/defaultsFor/RenderOptions::apply`. Read their headers.
- **Threads.** `MqSession` is not thread-safe: only the worker touches it. Widgets are touched only on the UI thread.
- **Locale.** Qt sets the C library locale from the environment; the core expects `"C"` for numbers and characters: `rfhqt::useCLocaleForNumbersAndCharacters()` (Task 1).
- **Text.** The core works on bytes plus a CCSID; only widgets convert to `QString`. `format::render` returns bytes in `options.display.ansi`; Task 26 decodes them with the core's `rfh_MultiByteToWideChar`.
- **Queue rule.** MQ tests may create and delete only `RFHUTIL.TEST.*` queues on `TEST_QM` and leave none (check with `echo "DISPLAY QLOCAL(RFHUTIL.*)" | runmqsc TEST_QM`, which should print `AMQ8147E`).
- **Standard task sequence.** Unless a task says otherwise: write the tests and run them to see them fail (the build error or assertion named in the task), implement, run the new tests, run `cmake --build --preset linux-debug -j8 && ctest --preset linux-debug` and expect `100% tests passed` (the previous total plus the tests the task adds; report the number), then commit with `git commit -s`, a Conventional Commit subject, and the trailers `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01JfjP6hazi7FDx4h8gULaS5`. Never push. Core code: tabs, Allman braces, `Result` not exceptions; tests: four spaces; Qt sources follow the core style.
- **Each task's report** states what was run, what was not, and anything Windows-visible.

## File structure

| Path | Status | Responsibility |
|---|---|---|
| `qt/CMakeLists.txt`, `qt/tests/CMakeLists.txt` | new | The `rfhqt` library, `rfhutil-qt`, `rfhqt_add_test` |
| `qt/src/main.cpp`, `ProcessSetup.*`, `MainWindow.*` | new | Start-up, locale, window, tabs, menus |
| `qt/src/SessionController.*` | new | Worker thread, jobs, busy, cancel |
| `qt/src/AppSettings.*`, `StatusLog.*`, `FileTrace.*` | new | Settings, status log, trace |
| `qt/src/AppDocument.*`, `Operations.*` | new | State, enable rules, the Windows flows |
| `qt/src/MainTab.*`, `MqmdTab.*`, `DataTab.*`, `FindDialogs.*`, `qt/ui/*.ui` | new | The three tabs and the Find dialogs |
| `core/mq/rfh_status_text.*` | new | `statusText` |
| `core/platform/rfh_platform.*` | modified | `rfh_current_user` |
| `core/message/rfh_mqmd_fields.*` | modified | `toPutMqmd`, `applyAfterPut`, `fromGetMqmd` |
| `core/message/rfh_id_display.*` | new | ID display and edit rules |
| `core/mq/rfh_mq_session.*` | modified | `queueInfo`, `browsePosition`, `listQueues`, get/put/connect options, `commit`, purge options |
| `core/mq/rfh_queue_managers.*`, `rfh_connect_string.*` | new | `listLocalQueueManagers`, `parseServerString` |
| `core/format/rfh_format.*` | modified | `locate` |
| `tests/core/fake_mq.h`, `tests/core/CoreHasNoUiTests.cmake`, `tests/core/*Tests.cpp` | new/modified | Core tests |
| `qt/tests/*Tests.cpp` | new | Qt tests |
| `tests/core/golden/*`, `tests/core/GoldenFileTests.cpp`, `tools/golden/*` | new (Task 29) | Golden message files, their generator and tests, the Windows comparison script |
| `tools/port/port-mqmdpage-putget.pl` | new (Task 30) | Gated MFC edit |
| `docs/CODEMAP.md`, `CHANGELOG.md`, spec | modified | Task 31 |

---

### Task 1: The Qt scaffold, verified

**Maps to:** criteria 2 and 4. **Files:**
- Create: `qt/CMakeLists.txt`, `qt/src/ProcessSetup.{h,cpp}`, `qt/src/MainWindow.{h,cpp}`, `qt/src/main.cpp`, `qt/tests/CMakeLists.txt`, `qt/tests/MainWindowTests.cpp`
- Modify: `CMakeLists.txt`

This code was built and run (see "Verified before writing"). Placeholders stand in for the three working pages until Tasks 24–26.

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

- [ ] **Step 2: Add the test CMake and see the configure fail.** Create `qt/tests/CMakeLists.txt`:

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
Expected: an error that `MainWindow.h` or the `qt` directory is missing. The test cannot build yet.

- [ ] **Step 3: Write the sources.** `qt/src/ProcessSetup.h`:

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

- [ ] **Step 4: Add the app CMake and hook it into the project.** `qt/CMakeLists.txt`:

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
Expected: `100% tests passed, 0 tests failed out of 615` (614 plus `qt.rfhqt_mainwindow_tests`). `ctest --preset linux-debug -L qt` runs only the Qt test.
Run: `QT_QPA_PLATFORM=offscreen timeout 2 ./build/linux-debug/qt/rfhutil-qt; echo $?`
Expected: `124` (the app was still running when stopped).
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

**Maps to:** criterion 4. **Files:**
- Create: `tests/core/fake_mq.h`
- Modify: `tests/core/MqSessionFakeTests.cpp`

The fake (`struct Fake`, the `fake*` entry points, `fakeApi()`) is private to `MqSessionFakeTests.cpp`. Later tasks need it from the Qt tests and need more of it.

- [ ] **Step 1: Move it without changing behaviour.** Put the fake in `tests/core/fake_mq.h` as header-only code in `namespace rfh_test`: `fakeApi()` returns an `rfh::MqApi`, and the state (queues and their messages, open/get/close/disc counts, the flags of the last calls) lives in a `FakeMq & fakeMq()` with `reset()`. `MqSessionFakeTests.cpp` includes it. Run `ctest --preset linux-debug -R MqSessionFake`; the same tests pass as before.
- [ ] **Step 2: Add what later tasks need, with a test for each.** In `MqSessionFakeTests.cpp` add `FakeMq.AnEmptyQueueReportsNoMessageAvailable` (`MQRC_NO_MSG_AVAILABLE`, `cc` 2), `FakeMq.InquiryReturnsConfiguredAttributes` (per-queue `MQIA_Q_TYPE`, `MQIA_CURRENT_Q_DEPTH`, `MQIA_MAX_MSG_LENGTH`, `MQCA_BASE_Q_NAME`), `FakeMq.QueueManagerInquiryReturnsPlatformAndLevel` (`MQIA_PLATFORM`, `MQIA_COMMAND_LEVEL`), `FakeMq.AGetThatWaitsHonoursTheWaitInterval` (a get with `MQGMO_WAIT` sleeps in 50 ms slices up to `WaitInterval` before reporting no message), `FakeMq.PutAndGetRecordTheirOptions` (the last `MQPMO`/`MQGMO` flags are inspectable) and `FakeMq.CommitIsCounted`.
- [ ] **Step 3: Run all tests** (`ctest --preset linux-debug`): the previous total plus the six new tests.
- [ ] **Step 4: Commit:** `test(core): share the fake MQ between the core and Qt tests`.

---

### Task 3: A test that keeps the core free of UI

**Maps to:** criterion 4 (and the requirement that `core/` shows no dialogs). **Files:**
- Create: `tests/core/CoreHasNoUiTests.cmake`
- Modify: `tests/CMakeLists.txt`

A CTest script that scans `core/` and fails if any source or header mentions a UI toolkit or a process exit. It runs from the first commit, so no later task can add UI to the core unnoticed.

- [ ] **Step 1: Write the script and its self-test.** `CoreHasNoUiTests.cmake` globs `core/**/*.{h,cpp}` and checks the case-sensitive patterns `Q[A-Z][A-Za-z]+` includes (`#include <Q`), `QMessageBox`, `AfxMessageBox`, `MessageBox(`, `GetDlgItem`, `UpdateData`, `CWnd`, `CString`, `std::cin`, `\bexit\(`, `\babort\(`, `std::quick_exit`. The only allowed hits are listed in a short allow-list inside the script with a reason each (for example the guard-allocator-style `abort` is in `tests/`, not `core/`; if `core/` has a legitimate `abort`, list it). Add `-D SELFTEST=1`, which scans a fixture directory created in the build tree with one offending file and expects the script to report it.
- [ ] **Step 2: Register two tests** in `tests/CMakeLists.txt`: `core.NoUi` (runs the script on `${PROJECT_SOURCE_DIR}/core`, expects success) and `core.NoUiSelfTest` (runs the self-test, expects the script to fail on the fixture; use `WILL_FAIL`). Run `ctest -R core.NoUi`: both pass on today's `core/`. If the real scan reports a hit, report it and decide with the owner whether it is a real violation (do not widen the allow-list silently).
- [ ] **Step 3: Run all tests; commit:** `test(core): fail the build when the core mentions UI or a process exit`.

---

### Task 4: `statusText`, the Windows status wording (G12)

**Maps to:** criterion 2 (design section 5: status line wording). **Files:**
- Create: `core/mq/rfh_status_text.{h,cpp}`, `tests/core/StatusTextTests.cpp`
- Modify: `core/CMakeLists.txt`, `tests/CMakeLists.txt`

`DataArea::setErrorMsg` (`RFHUtil/DataArea.cpp:9315-9880`) has about 90 hand-written texts keyed by reason code (inv 3.1 lists samples and the operation words). Port it so the app shows `2033 No messages in queue`, not `MQGET cc=2 rc=2033 ...`.

Interface (contract):

```cpp
namespace rfh
{
	struct StatusContext
	{
		const char * operation = "";	// the Windows operation word: "Open", "Close", "Get", "Put", "StartBr",
										// "Get (Browse)", "GetNext", "BrowsePrevGet", "BrowsePrevGetMsg", "Disc", "Connect", ...
		MQLONG maxMsgLenQm = 0;			// for 2030 (0 = unknown)
		MQLONG maxMsgLenQueue = 0;
	};

	// The text Windows shows for a failed call. A plain success gives an empty string. A Result
	// without an MQ reason code (rc MQRC_NONE, cc MQCC_FAILED) gives its own text unchanged.
	std::string statusText(const Result & result, const StatusContext & context);
}
```

- [ ] **Step 1: Write the failing tests.** Read `setErrorMsg` completely; the test list covers every `case`. One test per group of reason codes, each asserting the exact text: `StatusText.NoMessagesInQueue` (`2033 No messages in queue`), `.NotAuthorized` (`2035 Not authorized (<op>)`), `.NoSuchQueue` (`2085 No such queue`), `.QueueManagerNotAvailable` (`2059 Queue manager not available (<op>) - may not be started`), `.ConnectionLost` (`2009 Connection to QM has been lost - Q closed`), `.QueueFull` (`2053 Queue full`), `.MessageTooLarge` (`2030 Message too large for queue (check q and qm settings) QMgr max=%d Queue max=%d`), `.UnknownReasonUsesTheReasonName` (`*Error cc=%d rc=%d (<name>) Cannot <op>`), `.UnknownReasonWithoutAName` (`*Error cc=%d rc=%d Cannot <op>`, `DataArea.cpp:9866-9876`), `.SuccessIsEmpty`, `.NonMqFailureKeepsItsText`, and `.EveryOperationWordIsAccepted`.
- [ ] **Step 2: Run them:** link error, `statusText` undefined.
- [ ] **Step 3: Implement** by porting the `switch`; keep the wording byte for byte, including odd spacing.
- [ ] **Step 4: Run all tests; commit:** `feat(core): add the Windows status texts for failed MQ calls`.

---

### Task 5: `rfh_current_user`, the operating-system user name (G13)

**Maps to:** criterion 2 (Set Iden / Set All with a blank User Id). **Windows-visible:** yes (see the table). **Files:**
- Modify: `core/platform/rfh_platform.{h,cpp}`, `tests/core/PlatformTests.cpp`

`setMessageMQMD` (`MQMDPAGE.cpp:2573-2600`) takes the OS user name (`GetUserNameEx`/`GetUserName`), first 12 characters, when the User Id field is blank.

Interface (contract):

```cpp
// The calling user's login name; empty if it cannot be determined. Windows: GetUserNameEx/GetUserName.
// Linux: getpwuid_r(geteuid()).
std::string rfh_current_user();
```

- [ ] **Step 1: Write the failing tests** in `PlatformTests.cpp` (compiled by the Windows UnitTests project too): `Platform_User.IsNotEmptyForTheLoggedInUser` (non-empty; on Linux equal to `getpwuid(geteuid())->pw_name`), `Platform_User.HonoursTheUnderlyingAccount` (Linux only: compare with `id -un` through `popen`), `Platform_User.NeverThrowsOrReturnsAnOverlongName` (the result has no NUL or CR/LF).
- [ ] **Step 2–4: Run (fails to link), implement both branches, run all tests; commit:** `feat(core): add rfh_current_user`. State in the report that the Windows branch was not compiled.

---

### Task 6: `toPutMqmd` and `applyAfterPut`, the MQMD a put sends (G11, put half)

**Maps to:** criterion 2 (Write Q). **Windows-visible:** functions only; adopted by `MQMDPAGE` in Task 30. **Files:**
- Modify: `core/message/rfh_mqmd_fields.{h,cpp}`, `tests/core/MqmdFieldsTests.cpp`

Every Windows put builds its MQMD with `MQMDPAGE::setMessageMQMD` (`MQMDPAGE.cpp:2408-2692`), whose rules differ from `buildMQMD` (which `toMqmd` ports for the file-write path): ids are copied only when non-zero; the report, flag, offset and group rules differ; user id, accounting token, Appl fields and Put Date/Time are taken only with Set Iden or Set All; a blank user id becomes the OS user (first 12 characters); a blank date becomes now; reply fields are blank padded, not truncated (inv 3.3, G11). After a put only MsgId, Seq No, Offset and GroupId go back to the page (`updateMQMDafterPut`, `MQMDPAGE.cpp:2694`).

Interface (contract), in `rfh::mqmd_fields`:

```cpp
struct PutContext
{
	bool setIdentity = false;		// Set Iden Context
	bool setAll = false;			// Set All Context
	std::string osUser;				// used when the User Id field is blank (first 12 characters); from rfh_current_user()
	const struct tm * now = nullptr;	// for a blank Put Date/Time; null means the real clock
};

void toPutMqmd(const MqmdFields & fields, const PutContext & context, MQMD2 & mqmd);

// What the MQMD tab takes from the MQMD MQ returned for a put: MsgId, Seq No, Offset, GroupId.
void applyAfterPut(const MQMD2 & returned, MqmdFields & fields);
```

- [ ] **Step 1: Write the failing tests** (read `setMessageMQMD` fully; one test per rule, each asserting the MQMD bytes against a hand-built expectation with `tests/core/mq_layout.h`): `ToPutMqmd.ZeroIdsAreNotCopiedNonZeroOnesAre`, `.VersionTwoFieldsOnlyForVersionTwo`, `.ReportOptions`, `.BlankUserIdGetsTheOsUserOnlyWithSetIdentityOrSetAll`, `.ApplFieldsAndPutDateTimeOnlyWithSetAll`, `.BlankPutDateTimeBecomesNow` (inject `now`), `.ReplyFieldsAreBlankPadded`, `.ToMqmdIsUnchanged` (one existing expectation), `ApplyAfterPut.ChangesExactlyFourFields` (sentinel in every other field).
- [ ] **Step 2–4: Run (fails), implement (reuse `toMqmd`'s helpers where rules agree; do not change `toMqmd`), run all; commit:** `feat(core): build the put MQMD as the Windows MQMD tab does`.

---

### Task 7: `fromGetMqmd`, the MQMD tab after a get (G11, get half)

**Maps to:** criterion 2 (Read Q / Browse show the MQMD). **Files:**
- Modify: `core/message/rfh_mqmd_fields.{h,cpp}`, `tests/core/MqmdFieldsTests.cpp`

After a get or browse the page is cleared (`clearMQMD`, `MQMDPAGE.cpp:1688`) and refilled by `processMessageMQMD` (`MQMDPAGE.cpp:1530-1686`). It differs slightly from `fromMqmd` (the file path, `processMQMD`) for the Group Id of a version 1 MQMD and for the ASCII/EBCDIC/endian pre-translation `processMessage` does first (inv G11). Read both and list the differences in the header comment.

Interface (contract):

```cpp
// The port of MQMDPAGE::processMessageMQMD: the MQMD tab's values for an MQMD received from a queue.
// `received` is in host byte order, as MQ returns it.
void fromGetMqmd(const MQMD2 & received, MqmdFields & fields);
```

- [ ] **Step 1: Write the failing tests:** `FromGetMqmd.EveryFieldMatchesFromMqmdForAVersionTwoMqmd` (for a fully populated MQMD the two functions agree, except where the header comment lists a difference), `.VersionOneGroupIdIsHandledAsWindowsDoes`, `.IdsAreKeptAsReceived`, `.OverlongOrUnterminatedTextFieldsAreTruncatedNotOverrun`, `.ADateAndTimeAreShownAsWindowsFormatsThem`.
- [ ] **Step 2–4: Run, implement, run all; commit:** `feat(core): fill the MQMD tab after a get as Windows does`.

---

### Task 8: ID display and edit rules (G11, ID display)

**Maps to:** criterion 2 (MQMD tab). **Windows-visible:** no, until Task 30 adds the file to the projects. **Files:**
- Create: `core/message/rfh_id_display.{h,cpp}`, `tests/core/IdDisplayTests.cpp`
- Modify: `core/CMakeLists.txt`, `tests/CMakeLists.txt`

The MQMD tab shows the three ids (Message ID, Correl ID, Group Id) as Hex (default), Ascii or Ebcdic (inv 4.2): `SetId` (`MQMDPAGE.cpp:1787`), the pending-edit rule (`setMsgId`/`setCorrelId`/`setGroupId`, `MQMDPAGE.cpp:3548, 704, 3931`: in Ascii and Ebcdic only the characters that differ from the previously displayed text replace the binary byte, so an untouched non-printable byte survives), and the limits (48 in Hex, 24 otherwise).

Interface (contract):

```cpp
namespace rfh { namespace id_display {
	enum class Mode { Hex, Ascii, Ebcdic };

	size_t maxLength(Mode mode);						// 48 for Hex, 24 otherwise
	std::string toText(const MQBYTE24 id, Mode mode);	// Hex: 48 upper-case digits; Ascii: 24 characters, bytes below 0x20 as blanks;
														// Ebcdic: translated to ASCII, blanks for controls
	// Applies an edit. `typed` is the new text of the box and `previous` what it showed. Hex: pad with '0' to 48
	// and convert; Ascii/Ebcdic: pad with blanks to 24 (more than 24 characters is rejected) and replace only the
	// bytes whose character changed; Ebcdic converts the typed text from ASCII. Returns false, leaving `id`
	// unchanged and setting `error`, for a rejected edit (a non-hex digit in Hex mode; text that is too long).
	bool applyEdit(MQBYTE24 id, const std::string & typed, const std::string & previous, Mode mode, std::string & error);
	bool isHexDigit(char c);							// 0-9, A-F (a-f also accepted: callers upper-case)
	bool isZero(const MQBYTE24 id);						// all bytes zero: Get by Msg Id uses an id only when it is not
	void reset(MQBYTE24 id);							// Reset Ids
} }
```

- [ ] **Step 1: Write the failing tests:** `IdDisplay.HexIs48UpperCaseDigits`, `.AsciiShowsControlsAsBlanks`, `.EbcdicTranslates`, `.MaxLengths`, `.HexEditPadsWithZeros`, `.HexEditRejectsNonHex`, `.AsciiEditReplacesOnlyChangedCharacters` (an untouched byte 0x01 survives an edit elsewhere), `.AsciiEditTooLongIsRejected`, `.EbcdicEditConvertsFromAscii`, `.IsZero`, `.Reset`, and a round trip for every byte value in Hex mode.
- [ ] **Step 2–4: Run, implement (read the Windows functions, including `EbcdicToAscii` in `comsubs`), run all; commit:** `feat(core): add the MQMD tab's ID display and edit rules`.

---

### Task 9: `queueInfo` (G3)

**Maps to:** criterion 2 (Main tab: queue depth, type, resolved name; status texts using the maximum length). **Files:**
- Modify: `core/mq/rfh_mq_session.{h,cpp}`, `tests/core/MqSessionFakeTests.cpp`, `tests/core/MqSessionTests.cpp` (live)

Windows: `getQueueType` (`DataArea.cpp:18244`), `getCurrentDepth` (18200), `openQ` (maximum length `MQINQ` 12270, resolved name 12303).

Interface (contract):

```cpp
struct QueueInfo
{
	MQLONG type = 0;				// MQQT_LOCAL, MQQT_MODEL, MQQT_ALIAS, MQQT_REMOTE, MQQT_CLUSTER
	MQLONG depth = -1;				// -1 when unknown
	MQLONG maxMsgLength = 0;
	std::string resolvedName;		// the queue the name resolved to; empty when it is the same
};
// In class MqSession:
Result queueInfo(const std::string & queue, QueueInfo & info);	// opens the queue for inquiry, as depth() does
```

- [ ] **Step 1: Write the failing tests:** `MqSessionFake.QueueInfoReturnsTypeDepthAndMaximum`, `.QueueInfoReportsAResolvedAliasName`, `.QueueInfoOfAMissingQueueFails` (the fake reports 2085), `.QueueInfoDoesNotDisturbAnOpenBrowse`; live: `MqSession.QueueInfoOnARealQueueAfterAPut` (skips without a queue manager).
- [ ] **Step 2–4: Run, implement (keep `depth()`), run all and `ctest -L mq`; confirm no `RFHUTIL.*` queue remains; commit:** `feat(core): add queue information to MqSession`.

---

### Task 10: `browsePosition` (G4)

**Maps to:** criterion 2 (Browse first/next/prev captions "msg k of depth"). **Files:**
- Modify: `core/mq/rfh_mq_session.{h,cpp}`, `tests/core/MqSessionFakeTests.cpp`

Windows counts in `browseCount` (`DataArea.cpp:8180-8230, 8459-8700, 8895-9225`). The core already keeps a counter privately.

Interface (contract): `long MqSession::browsePosition() const;` — the 1-based position of the last browsed message; 0 when none (map the internal unknown to 0).

- [ ] **Step 1: Write the failing tests:** `MqSessionFake.BrowsePositionCountsFirstNextAndPrevious`, `.BrowsePositionIsZeroWithoutABrowse`, `.BrowsePositionResetsWhenTheQueueChanges`, `.BrowsePositionAfterADestructiveGetIsZero`.
- [ ] **Step 2–4: Run, implement, run all; commit:** `feat(core): expose the browse position`.

---

### Task 11: `listLocalQueueManagers` (G1)

**Maps to:** criterion 2 (Main tab queue manager dropdown). **Files:**
- Create: `core/mq/rfh_queue_managers.{h,cpp}`, `tests/core/QueueManagersTests.cpp`
- Modify: `core/CMakeLists.txt`, `tests/CMakeLists.txt`

Windows has no such function (inv G1: it lists only queue managers seen this session); the spec asks for local queue managers. Parse the `QueueManager:` stanzas of `<MQ data path>/mqs.ini` (default `/var/mqm`).

Interface (contract):

```cpp
namespace rfh { namespace mq {
	// Names from the QueueManager: stanzas of <mqDataPath>/mqs.ini (the Name= lines), in file order.
	// A missing file is a success with an empty list; an unreadable file is a failure.
	Result listLocalQueueManagers(const std::string & mqDataPath, std::vector<std::string> & names);
} }
```

- [ ] **Step 1: Write the failing tests** with fixture files in a temp directory: `QueueManagers.TwoStanzas`, `.OtherStanzasAreIgnored` (`DefaultQueueManager:`, `Log:` ...), `.MissingFileIsSuccessAndEmpty`, `.UnreadableFileFails`, `.EmptyFile`, `.CrlfLineEndings`, `.ExtraSpacesAndComments`, `.DuplicateNamesAreKeptOnce`. A live check against `/var/mqm/mqs.ini` that prints `TEST_QM` is listed (skip if the file is unreadable).
- [ ] **Step 2–4: Run, implement, run all; commit:** `feat(core): list the local queue managers from mqs.ini`.

---

### Task 12: `listQueues`, the PCF queue listing (G2)

**Maps to:** criterion 2 (Main tab Queue Name dropdown, Load Names). **Files:**
- Modify: `core/mq/rfh_mq_session.{h,cpp}`, `tests/core/MqSessionFakeTests.cpp`, `tests/core/MqSessionTests.cpp` (live)

Windows: `General::OnLoadQueueNames` (`General.cpp:1642`) → `getQueueNames` (`DataArea.cpp:25198`) → `loadNames` (21633) → `OpenTempQ` (18304: a dynamic reply queue from `SYSTEM.DEFAULT.MODEL.QUEUE`, `RFHUTIL.REPLY.QUEUE*`) + `openAdminQ` (18484) + `getListOfNames` (22968: PCF inquire-queue for local, remote and alias queues, optional cluster filter). PCF is not available on z/OS before V6 (message `MQ on 390 does not support PCF prior to V6`).

Interface (contract):

```cpp
struct QueueListOptions { bool local = true, remote = true, alias = true, cluster = false; };
// In class MqSession: lists queue names over PCF through short-lived handles (the reply queue is deleted when done).
Result listQueues(const QueueListOptions & options, std::vector<std::string> & names);
```

Hiding `SYSTEM.*` names is app logic (View > System Queues).

- [ ] **Step 1: Write the failing tests:** with the fake supporting the admin queue: `ListQueues.ReturnsLocalRemoteAndAliasNames`, `.ClusterFilter`, `.EmptyResultIsSuccess`, `.ReplyQueueIsClosedAndDeleted`, `.AFailureToOpenTheAdminQueueIsReported` (2035/2085 through the fake), `.MoreThanOneReplyMessage` (several PCF responses ending with the last-message flag), `.PlatformWithoutPcfIsReportedWithTheWindowsText`. Live: `MqSession.ListQueuesFindsATestQueue` (creates `RFHUTIL.TEST.LIST`, lists, deletes it).
- [ ] **Step 2–4: Run, implement (read the Windows PCF code; build the commands with `cmqcfc.h`), run all and `ctest -L mq`, check for leftover queues; commit:** `feat(core): list queue names over PCF`.

---

### Task 13: Get options and the queue manager's platform and level (G5)

**Maps to:** criterion 2 (Main tab Get options). **Files:**
- Modify: `core/mq/rfh_mq_session.{h,cpp}`, `tests/core/MqSessionFakeTests.cpp`, `tests/core/MqSessionTests.cpp`

Windows: `setMQoptions` (`DataArea.cpp:17816`; gated on queue manager platform and level: `platform != MQPL_MVS` or level >= 600), `processMQGet` (7612), `getMessage` (7947), `openQ` (12056-12180), `connect2QM` (11260-11395, which inquires the platform and level).

Interface (contract):

```cpp
// Added to GetOptions:
bool matchGroupId = false;  MQBYTE24 groupId = { 0 };		// MQMO_MATCH_GROUP_ID
bool logicalOrder = false;									// MQGMO_LOGICAL_ORDER
bool completeMsg = false;									// MQGMO_COMPLETE_MSG
bool allAvailable = false;									// MQGMO_ALL_MSGS_AVAILABLE | MQGMO_ALL_SEGMENTS_AVAILABLE, with the z/OS rules of inv 3.3
std::string selector;										// MQOD V4 SelectionString at open (needs MQ V7)
std::string alternateUser;									// MQOO_ALTERNATE_USER_AUTHORITY
bool inputShared = false;									// MQOO_INPUT_SHARED instead of MQOO_INPUT_AS_Q_DEF

// In class MqSession, filled by connect():
struct QueueManagerInfo { MQLONG platform = 0; MQLONG commandLevel = 0; MQLONG maxMsgLength = 0; };
const QueueManagerInfo & queueManagerInfo() const;
```

- [ ] **Step 1: Write the failing tests** (inspect the fake's recorded flags): one per option, `GetOptions.MatchGroupIdSetsTheMatchOption`, `.LogicalOrderCompleteMsgAndAllAvailableSetTheirFlags`, `.AllAvailableOnZosBeforeV6SetsNeither`, `.AllAvailableOnZosV6SetsOnlyAllMsgs`, `.SelectorGoesIntoTheOpenDescriptor`, `.SelectorFailsClearlyBelowV7`, `.AlternateUserSetsTheOpenOptionAndName`, `.InputSharedChangesTheOpenOption`, `.ExistingDefaultsAreUnchanged`; `MqSessionFake.ConnectRecordsPlatformLevelAndMaximum`; live: a selector get that matches one of two messages (V7+).
- [ ] **Step 2–4: Run, implement, run all and `ctest -L mq`; commit:** `feat(core): add the remaining Get options and record the queue manager level`.

---

### Task 14: Put options, syncpoint and commit (G6)

**Maps to:** criterion 2 (Write Q, group and segment messages). **Files:**
- Modify: `core/mq/rfh_mq_session.{h,cpp}`, `tests/core/MqSessionFakeTests.cpp`, `tests/core/MqSessionTests.cpp`

Windows: `putMessage` (`DataArea.cpp:6166-6529`; pmo flags 6290-6340; commit rules for group and segment messages 6465-6505), `updateMQMDafterPut` (`MQMDPAGE.cpp:2694`, done by `applyAfterPut` in Task 6).

Interface (contract):

```cpp
// Added to PutOptions:
bool logicalOrder = false;			// MQPMO_LOGICAL_ORDER
bool syncpoint = false;				// MQPMO_SYNCPOINT instead of MQPMO_NO_SYNCPOINT
std::string alternateUser;			// MQOO_ALTERNATE_USER_AUTHORITY at open

// In class MqSession:
Result commit();					// MQCMIT; a failure is reported through Result
```

- [ ] **Step 1: Write the failing tests:** `PutOptions.LogicalOrderSetsItsFlag`, `.SyncpointReplacesNoSyncpoint`, `.AlternateUserIsPassedAtOpen`, `.DefaultsAreUnchanged`, `MqSessionFake.CommitCallsMqcmit`, `.CommitWithoutAConnectionFails`; live: put two messages in syncpoint, check the depth is 0 from another connection, commit, depth 2.
- [ ] **Step 2–4: Run, implement (the commit rules themselves are flow logic and live in Task 23), run all and `ctest -L mq`; commit:** `feat(core): add logical order, syncpoint, alternate user and commit to puts`.

---

### Task 15: `parseServerString` (G7a)

**Maps to:** criterion 2 (client mode). **Files:**
- Create: `core/mq/rfh_connect_string.{h,cpp}`, `tests/core/ConnectStringTests.cpp`
- Modify: `core/CMakeLists.txt`, `tests/CMakeLists.txt`

Windows accepts an MQSERVER-style string in the queue manager box: `CHANNEL/TCP/host(port)` and `QM[CHANNEL/TCP/host(port)]` (`connect2QM`, `DataArea.cpp:10956-11060`).

Interface (contract):

```cpp
namespace rfh { namespace mq {
	// Fills mode, qmName, channel and connName from `text`. Returns false, with `error`, when `text` looks like
	// a connection string but is malformed; returns true and leaves `options` unchanged for a plain queue
	// manager name.
	bool parseServerString(const std::string & text, ConnectOptions & options, std::string & error);
} }
```

- [ ] **Step 1: Write the failing tests:** `ConnectString.PlainNameIsUntouched`, `.ChannelSlashTcpHostPort`, `.QmWithBracketedChannel`, `.HostWithoutPort`, `.IpV6Host`, `.MalformedGivesAnError` (missing `)`, empty channel, bad port), `.WhitespaceIsTrimmed`, `.PortOutOfRange`.
- [ ] **Step 2–4: Run, implement, run all; commit:** `feat(core): parse MQSERVER-style connection strings`.

---

### Task 16: Connection security options (G7b)

**Maps to:** criterion 2 (client mode over TLS). **Files:**
- Modify: `core/mq/rfh_mq_session.{h,cpp}` (or `rfh_mq_loader.*` if `MQSCO`/`MQCD` building lives there), `tests/core/MqSessionFakeTests.cpp`

Windows: `General::OnSetConnUser` (`General.cpp:2347`), `ConnUser.cpp`, SSL setup in `connect2QM` (`DataArea.cpp:10945`). Inventory 3.5 lists the dialog's fields. The core today uses user id and password only.

Interface (contract), added to `ConnectOptions`:

```cpp
std::string keyRepository;			// SSL key repository (Certificate Store Location)
std::string cipherSpec;				// SSL cipher
std::string sslPeerName;			// MQSCO/MQCD SSLPeerName
bool sslEnabled = false;
bool fipsRequired = false;
bool sslClientValidation = false;
int sslResetCount = 0;
std::string securityExit, securityData;
std::string localAddress;
```

Reconnect and heart beat (the `Conn` tab) are not Phase 1.

- [ ] **Step 1: Write the failing tests** against the fake's recorded `MQCD`/`MQSCO`: `ConnectOptions.SslFieldsReachTheChannelDefinition`, `.CipherSpecAndPeerNameAreSet`, `.KeyRepositoryGoesIntoTheSslConfig`, `.FipsRequiredAndResetCount`, `.SecurityExitAndDataAreSet`, `.LocalAddressIsSet`, `.NothingIsSetWhenSslIsOff`, `.PasswordIsNeverTraced` (the trace output of a connect with a password does not contain it), `.BindingsModeIgnoresTheClientOnlyFields`.
- [ ] **Step 2–4: Run, implement, run all; a live TLS test is listed as unverified unless the owner has a TLS channel; commit:** `feat(core): add TLS, security exit and local address to the connection options`.

---

### Task 17: `PropertiesMode` (G8)

**Maps to:** criterion 2 (Main tab User Props radios). **Files:**
- Modify: `core/mq/rfh_mq_session.{h,cpp}`, `tests/core/MqSessionFakeTests.cpp`

Windows: `processMQGet` (`DataArea.cpp:7690-7760`), `putMessage` (6360-6420), `browsePrev` (9070-9110). The None, RFH2 and Compat radios are effectively dead on a Windows Get (inv 3.3); `Yes` creates a message handle and puts the properties on the Usr Prop tab, which is not Phase 1.

Interface (contract), added to `GetOptions`:

```cpp
enum class PropertiesMode { AsQueue, None, Rfh2, Compat };	// no flag; MQGMO_NO_PROPERTIES; MQGMO_PROPERTIES_FORCE_MQRFH2; MQGMO_PROPERTIES_COMPATIBILITY
PropertiesMode properties = PropertiesMode::AsQueue;
```

`Yes` (message handle and a property list in `Message`) is not added: open point.

- [ ] **Step 1: Write the failing tests:** `GetOptions.PropertiesModesSetTheirFlags` (one case per mode), `.AsQueueSetsNoPropertiesFlag`, `.ABrowsePreviousKeepsTheMode`, `.ModesAreIgnoredBelowV7` (the platform/level from Task 13).
- [ ] **Step 2–4: Run, implement, run all; commit:** `feat(core): add the message properties mode to Get options`.

---

### Task 18: `format::locate` (G14, the position)

**Maps to:** criterion 2 (Data tab Go To and Find highlight). **Files:**
- Modify: `core/format/rfh_format.{h,cpp}`, `tests/core/FormatTests.cpp`

Go To takes a zero-based byte offset into the message body and shows that position in the current view (inv 5.5). Windows: `getLineNumber` (`DataArea.cpp:12793`), `getLineOffset` (12872), `MSGDATA::moveDisplay` (`MSGDATA.cpp:1142`).

Interface (contract), in `rfh::format`:

```cpp
struct TextPos
{
	int line = 0;		// zero-based line in the rendered text
	int column = 0;		// zero-based column in that line, in rendered characters
	int length = 0;		// characters to select: 2 for a byte in the Hex view, otherwise 1
};

// Where byte `offset` of message.body is in the text render() produces for `view`. Supported views:
// Character, Hex, Both. Returns false (and leaves pos alone) for other views or an offset outside the body.
bool locate(View view, const RenderOptions & options, const Message & message, size_t offset, TextPos & pos);
```

Windows' rules (inv 5.5) assume lines of 32 characters with an 8-digit offset prefix for Character, 16 bytes a line for Hex and Both; CR/LF on: the line is the number of LF bytes before the offset, column 0; EDI on: line 0, column 0. Where the inventory marks a rule UNVERIFIED, decide by rendering.

- [ ] **Step 1: Write the failing tests** that stay independent of the formulas: `Locate.EveryOffsetPointsAtItsCharacterInTheCharacterView` (render a body of 31, 32, 33, 47, 48, 100 bytes; for every offset, `text[line][column]` is the byte's character), `.EveryOffsetInTheHexView` (the first of the byte's two hex digits, `length` 2), `.EveryOffsetInTheBothView`, `.CrLfOnUsesTheLfCount`, `.EdiOnIsLineZeroColumnZero`, `.OutsideTheBodyIsFalse`, `.UnsupportedViewsAreFalse`.
- [ ] **Step 2–4: Run, implement (adjust until the exhaustive tests pass), run all; commit:** `feat(core): locate a byte offset in the Character, Hex and Both views`.

---

### Task 19: Purge options (G15)

**Maps to:** criterion 2 (Purge Q). **Files:**
- Modify: `core/mq/rfh_mq_session.{h,cpp}`, `tests/core/MqSessionFakeTests.cpp`, `tests/core/MqSessionTests.cpp`

Windows `purgeQueue` (`DataArea.cpp:10065-10244`): honours the selector, uses `MQGMO_SYNCPOINT_IF_PERSISTENT` with batched commits and `MQGMO_ACCEPT_TRUNCATED_MSG`. The core purge uses `MQGMO_NO_SYNCPOINT`, no selector, and returns a count only. The confirmation and the texts are app code (Task 23).

Interface (contract):

```cpp
struct PurgeOptions { std::string selector; bool syncpoint = false; MQLONG batchSize = 100; };
// In class MqSession; the existing purge(queue, count) stays and calls this with default options:
Result purge(const std::string & queue, MQLONG & count, const PurgeOptions & options);
```

- [ ] **Step 1: Write the failing tests:** `Purge.WithoutOptionsBehavesAsBefore`, `.SelectorOnlyRemovesMatchingMessages` (live, V7+), `.SyncpointCommitsInBatches` (the fake counts commits for 250 messages with batch 100: 3), `.TruncatedMessagesAreRemoved`, `.CountIsReturnedOnFailure` (partial count).
- [ ] **Step 2–4: Run, implement, run all and `ctest -L mq`; no leftover queues; commit:** `feat(core): add selector and syncpoint to purge`.

---

### Task 20: The session controller and a cancellable Get (G10)

**Maps to:** criterion 2 (threading, design section 5). **Files:**
- Create: `qt/src/SessionController.{h,cpp}`, `qt/tests/SessionControllerTests.cpp`
- Modify: `qt/CMakeLists.txt`, `qt/tests/CMakeLists.txt`

One worker `QThread` owns the `MqSession`. The UI submits a job (a function run on the worker) and gets the outcome as a signal on the UI thread. One job at a time; the UI disables conflicting actions while `busy()`. The core already has `GetOptions::waitMs` and `cancel`; Windows' Read Q never waits (inv G10), so the app adds a Wait field (Task 24) whose default 0 keeps parity.

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
		bool submit(const QString & name, Job job);		// false, and not run, while busy()
		void cancel();									// sets the flag GetOptions::cancel points at; a waiting get stops within ~1 s
		const std::atomic<bool> * cancelFlag() const;	// cleared when a job starts

	signals:
		void busyChanged(bool busy);
		void finished(const QString & name, const rfhqt::JobResult & result);	// on the UI thread
	};
}
Q_DECLARE_METATYPE(rfhqt::JobResult)
```

- [ ] **Step 1: Write the failing tests** (Qt Test, `fake_mq.h`): `JobRunsOnAnotherThread`, `ResultsArriveOnTheUiThread`, `SubmitWhileBusyIsRejected`, `BusyChangedEmitsTrueThenFalse` (`QSignalSpy`), `CancelStopsAWaitingGet` (a get with `MQWI_UNLIMITED` on an empty fake queue returns within 2.5 s of `cancel()`), `CancelFlagIsClearedWhenTheNextJobStarts`, `DestroyingWhileBusyCancelsAndJoins`, `TheSessionPersistsBetweenJobs`, `AJobThatThrowsBecomesAFailedResult` (catch `std::exception` in the worker).
- [ ] **Step 2–4: Run, implement, run all; commit:** `feat(qt): add the session controller with its worker thread`.

---

### Task 21: Settings, the status log and the trace file (G17)

**Maps to:** criterion 2. **Files:**
- Create: `qt/src/AppSettings.{h,cpp}`, `qt/src/StatusLog.{h,cpp}`, `qt/src/FileTrace.{h,cpp}`, `qt/tests/AppSettingsTests.cpp`, `qt/tests/StatusLogTests.cpp`, `qt/tests/FileTraceTests.cpp`

Interfaces (contract):

```cpp
class AppSettings		// wraps QSettings; the constructor takes a file path so tests use a temp file
{
public:
	explicit AppSettings(const QString & filePath = QString());	// default: ~/.config/rfhutil/rfhutil.conf
	QStringList recentQueueManagers() const;  void addQueueManager(const QString &);	// most recent first, no duplicates, max 30
	QStringList recentQueues(const QString & qm) const;  void addQueue(const QString & qm, const QString & queue);	// max 30 per queue manager
	QString lastDirectory(const QString & kind) const;  void setLastDirectory(const QString & kind, const QString & dir);	// "open", "save", "copybook"
	QByteArray windowGeometry() const;  void setWindowGeometry(const QByteArray &);
	int displayCodePage() const;  void setDisplayCodePage(int);	// default 1252
	bool showSystemQueues() const;  void setShowSystemQueues(bool);	// default false
};

class StatusLog			// the Main tab's status box (inv 3.1)
{
public:
	// Adds `text` stamped "%H.%M.%S " with a CR LF, newest on top; the whole log is cut at 8191 bytes back to a
	// line boundary and trailing CR/LF are removed. `now` is injectable for tests.
	void add(const QString & text, const QTime & now = QTime::currentTime());
	QString text() const;
	void clear();
};

class FileTrace : public RfhTrace	// core/platform/rfh_trace.h; installed with rfhSetTrace
{
public:
	explicit FileTrace(const QString & path = QString());	// default: $XDG_STATE_HOME/rfhutil/trace.log, else ~/.local/state/rfhutil/trace.log
	bool isTraceEnabled() override;  void logTraceEntry(const char *) override;  void dumpTraceData(const char *, const unsigned char *, unsigned int) override;
	void setEnabled(bool);
};
```

Windows registry settings (last QM and queue, paths, password) become `AppSettings`; **the password is never stored** (open point). Audit records (`createAuditRecord`) and the trace menu are not ported (open point).

- [ ] **Step 1: Write the failing tests:** settings round trips, most-recent-first, duplicate moves to the top, the 30 limit, per-queue-manager lists, defaults, persistence across two `AppSettings` on one file; StatusLog stamp format, newest first, the 8191 cut at a line boundary, trailing CR/LF removed, multi-line text, `clear`; FileTrace writes nothing while disabled, creates the directory, appends timestamped entries, hex dump for `dumpTraceData`, honours `XDG_STATE_HOME`.
- [ ] **Step 2–4: Run, implement, run all; commit:** `feat(qt): add the settings, the status log and the trace file`.

---

### Task 22: The document model and the enable rules (G18, part 1)

**Maps to:** criterion 2. **Files:**
- Create: `qt/src/AppDocument.{h,cpp}`, `qt/tests/AppDocumentTests.cpp`

`AppDocument` holds what `DataArea` holds for the three tabs, plus the enable rules of `General::UpdatePageData` (`General.cpp:504-620`; inv 2.1 and 3.4) as a pure function.

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
	rfh::Message message;
	rfh::MqmdFields mqmd;
	rfh::format::RenderOptions render;
	std::string fileSource;				// "Message Data" or "Message Data (N) from <path>"
	QString queueManager, queueName, remoteQm, copybookPath;
	rfh::QueueInfo queueInfo;
	BrowseState browse;
	bool connected = false;

	static MainEnables enablesFor(const BrowseState & browse, bool connected);	// pure

	void clearData();					// Clear Data (inv 3.2)
	void clearAll();					// Clear All: also resets the MQMD values and the file name
	void setMessage(const rfh::Message & m);
signals:
	void messageChanged();
	void stateChanged();
};
```

- [ ] **Step 1: Write the failing tests:** `enablesFor` for all combinations of browse active / not, `previousAllowed`, connected / not, against inv 2.1 (browsing: Browse Next and End Browse on, Browse Prev only when allowed, everything else off; otherwise all on except Browse Next, Browse Prev and End Browse, and Close Q only while connected); `clearData` empties body and headers but keeps the MQMD; `clearAll` also resets the MQMD values to `mqmd_fields::clear` and the file name; each signal fires once per change.
- [ ] **Step 2–4: Run, implement, run all; commit:** `feat(qt): add the document model and the Main tab enable rules`.

---

### Task 23: The operations, the Windows flows (G18, part 2; G9, G16)

**Maps to:** criteria 2 and 3. **Files:**
- Create: `qt/src/Operations.{h,cpp}`, `qt/tests/OperationsTests.cpp`

`Operations` implements what each Main tab button does (inv 3.1–3.4), on top of `SessionController`, `AppDocument`, `StatusLog` and the core. No widgets.

Interface (contract):

```cpp
struct MainOptions		// the Main tab's check boxes and radios (inv 3.3)
{
	rfh::ConnectOptions connect;					// mode, queue manager, channel, connection name, CCDT, security (Tasks 15, 16)
	rfh::PutOptions put;							// new msg id, correl id, set identity/all, bind (all four, G9), logical order, syncpoint, alternate user
	rfh::GetOptions get;							// match ids (applied only when the MQMD tab id is not zero), convert, wait interval, selector, properties, logical order...
	rfh::CloseOption closeOption = rfh::CloseOption::None;
	int fileCodePage = 0;							// 0 / empty = decide
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
	void closeQ();   void purgeQ();
	void openFile(const QString & path);   void saveFile(const QString & path);
	void clearData();   void clearAll();
	void cancelGet();

signals:
	void confirmPurge(const QString & queue);		// the UI answers with purgeConfirmed()
	void statusChanged();
public slots:
	void purgeConfirmed(bool yes);
};
```

Flow rules, each its own test: connect on demand and reconnect when the queue manager name changes (`checkConnection`, `DataArea.cpp:12489`); Read Q = open, one destructive get, queue info refresh; Write Q builds the MQMD with `toPutMqmd` (with `PutContext` from Set Iden / Set All and `rfh_current_user()`), sends the body with the MQMD `Format` as typed, applies `applyAfterPut`, and honours the syncpoint and commit rules for group and segment messages (`DataArea.cpp:6465-6505`); Browse Q = open, one browse, close; Start Browse turns the browse state on; Browse Next; Browse Prev only after a browse next/prev as in inv 2.1; a failed Browse Next closes, disconnects and ends the browse; End Browse closes and disconnects; Close Q = close then disconnect; Purge asks `Purge all messages from queue %s?` through `confirmPurge`, does nothing on No, and shows the status with `cc` and `rc` in the right order (a fix); Get by Msg Id / Correl Id / Group Id apply only when the MQMD tab id is not all zero (inv 3.3); Convert sets `convertCcsid` from the MQMD tab's Code Page (else the queue manager's) and `convertEncoding` to 546 for PC integers or 273; failures go through `statusText` with the right operation word; success texts per inv 3.2; Set Iden Context clears after a get (inv 3.4); after a get `defaultsFor(message)` is applied with `RenderOptions::apply` and the MQMD values come from `fromGetMqmd`; the Cluster Open `Group` option reaches `PutOptions::bind` (a fix of a Windows defect); open file uses `fileio::read` with the Main options and shows `Message Data (N) from <path>`, applying `defaultsFor(ReadResult)`; save file uses `fileio::write` and shows its text; a Get with a wait interval is cancellable through `cancelGet`; queue name resolution message `Queue name resolved to <name>`.

- [ ] **Step 1: Write the failing tests** against the fake MQ with a real `SessionController` (inject the fake), waiting for `finished` with `QSignalSpy`: one test per rule, asserting the document state, the status log text and the calls the fake saw (open, get, close and disc counts and flags).
- [ ] **Step 2–4: Run, implement flow by flow, run all and `ctest -L mq`; commit:** `feat(qt): implement the Main tab operations with the Windows flow rules`.

---

### Task 24: The Main tab (G9, G10)

**Maps to:** criterion 2. **Files:**
- Create: `qt/src/MainTab.{h,cpp}`, `qt/ui/MainTab.ui`, `qt/tests/MainTabTests.cpp`
- Modify: `qt/CMakeLists.txt`, `qt/src/MainWindow.cpp`

Build `MainTab.ui` from inv 2.1: every control with its caption (verbatim, with the `&` mnemonic), group boxes, tab order, defaults and read-only flags, in Qt layouts so the window resizes and works on HiDPI. Controls whose function is in the core after Tasks 12–17 are enabled: Alternate User Id, Logical Order, Complete Msg, All Avail, Selector, Load Names, the User Props radios (As Queue, None, RFH2, Compat), and Set Conn Id (the full dialog of inv 3.5, user id and password held in memory only). `Yes` of User Props, Save Q, Load Q, Move Q and Display Q are disabled with the tooltip "Not yet ported". New: a Connection box (Bindings or Client; channel and connection name or a CCDT path; `MQSERVER` and `MQCHLLIB` are honoured by MQ when none is given), a Wait (seconds) field defaulting to 0 and a Cancel button enabled while a Get waits (G10). The QM combo is filled from `listLocalQueueManagers` plus the history; the queue combo from the history and Load Names (hiding `SYSTEM.` names unless View > System Queues); all four Cluster Open radios (including Group) select their `BindOption`. The control texts and the status line follow Windows; the Windows defects fixed on purpose are listed in "Owner decisions" of Task 31.

`MainTab` reads the widgets into `MainOptions`, calls `Operations`, and reflects `AppDocument` (file name, data size, queue depth, queue type, status log) and `enablesFor` after each change.

- [ ] **Step 1: Write the failing tests:** captions and mnemonics match inv 2.1 (iterate a `QStringList` copied from the inventory); read-only fields are read-only; defaults (New Msg Id on; the rest off; Cluster Open As Queue; Close None); `options()` round-trips every control; each of the four Cluster Open radios selects its `BindOption`; the browse enable rules reflect on the buttons; clicking Read Q calls `Operations::readQ` (test double or fake-MQ spy); the Purge confirmation shows a `QMessageBox` (answer it from a single-shot timer); the status box shows the log; Load Names fills the queue combo and hides `SYSTEM.*` by default; the Wait field and Cancel button: Cancel is enabled only while a Get waits; the Ctrl+ shortcuts from inv 7.1 for the Main tab trigger the buttons (Ctrl+Q, W, B, T, N, R, M, K, E, J, O, S, H, I and so on) and do not fire from another tab.
- [ ] **Step 2: Run them, build the `.ui` and the class, run all tests, then look at it:** run `./build/linux-debug/qt/rfhutil-qt` on the Wayland desktop if a display is available, or grab a PNG with `QWidget::grab()` in a test and view it. **Step 3: Commit:** `feat(qt): add the Main tab`.

---

### Task 25: The MQMD tab (G11 ID display in use)

**Maps to:** criterion 2. **Files:**
- Create: `qt/src/MqmdTab.{h,cpp}`, `qt/ui/MqmdTab.ui`, `qt/tests/MqmdTabTests.cpp`

Build from inv 2.2 and 4: every field `MQMDPAGE` shows, edited through `mqmd_fields::MqmdFields` (inv 4.4 maps each Windows field to a member; fields with no counterpart are shown disabled). The three ids use Task 8's `id_display` (Hex, Ascii, Ebcdic; the edit rules; Reset Ids; Copy Msg Id to Correl Id). Report options are `QButtonGroup`s with ids 0-3 so `checkedId()` -1 round-trips as "none". Combo and list choices carry the exact texts and values of inv 4.1.

Interface (contract):

```cpp
class MqmdTab : public QWidget
{
	Q_OBJECT
public:
	explicit MqmdTab(AppDocument &, QWidget * parent = nullptr);
	rfh::MqmdFields fields() const;				// the widgets as MqmdFields (what Write Q needs)
	void setFields(const rfh::MqmdFields &);	// after a get
signals:
	void fieldsEdited();
};
```

- [ ] **Step 1: Write the failing tests:** `RoundTripEveryMemberOfMqmdFields` (a fully populated `MqmdFields` through `setFields` and `fields()`, compared member by member with a failure message naming the member); defaults equal `mqmd_fields::clear`; hex id fields accept `0-9A-F`, upper-case what is typed and reject other characters with the Windows beep behaviour replaced by a status message; the Id Display switch redraws the three boxes from the bytes and stores pending edits first; Ascii/Ebcdic edits replace only changed characters; Reset Ids and Copy Msg Id; Report radios round trip -1; each combo's texts and values match inv 4.1; after `AppDocument::messageChanged` the tab shows the message's MQMD (`fromGetMqmd`); the read-only rules of `MQMDPAGE.cpp:396-455`; editing emits `fieldsEdited`.
- [ ] **Step 2–3: Run, build, run all tests, look at it; commit:** `feat(qt): add the MQMD tab`.

---

### Task 26: The Data tab, Find, Find Hex and Go To (G14, the search)

**Maps to:** criterion 2. **Files:**
- Create: `qt/src/DataTab.{h,cpp}`, `qt/ui/DataTab.ui`, `qt/src/FindDialogs.{h,cpp}`, `qt/tests/DataTabTests.cpp`, `qt/tests/FindDialogsTests.cpp`

Build from inv 2.3 and 5. A read-only monospace `QPlainTextEdit` (no wrapping) shows `format::render` output for the chosen view with the options (character format radios, numeric formats, CR/LF, EDI, Indent, Check data; the BOM box stays hidden as on Windows). Rendering runs on the UI thread unless measuring shows a 10 MB body takes over about 200 ms in a view, in which case it goes to the worker with the busy cursor: measure and report the times. The bytes are in `display.ansi` (from `AppSettings`, default 1252); decode them with `rfh_MultiByteToWideChar`, not Qt codecs. The COBOL view asks for a copy book when none is set (`QFileDialog`, title `COBOL copy book file`, filter `All (*.*);;COBOL files (*.cbl *.cpy);;Text (*.TXT)`) and passes `copybookPath`. `Allow Edit` and the Data tab's `Write Q` are disabled ("Not yet ported"). The Browse Next / Prev buttons call the same operations as the Main tab.

**Find** (Ctrl+F): modeless `Find what`, Direction Up/Down (Down), `Find Next`/`Cancel`; no match-case or whole-word options. Character, Hex and Both search the raw message bytes (typed text as bytes, at most 255; translated ASCII to EBCDIC when the character format is EBCDIC), starting one byte after the previous hit, no wrap, no message on a miss (the position resets and the view moves to the top), and show the hit with `format::locate` (selecting the first byte only: 2 characters in Hex, otherwise 1), scrolling so the hit line is the first visible line. XML, Parsed, JSON and Fixed search the rendered text with the same rules. **Fix of a Windows defect:** a match ending exactly at the end of the data is found.
**Find Hex** (Ctrl+Z): `Hex Finder`, an upper-casing edit of up to 16 characters, Direction Up/Down, `Find`/`Cancel`, errors `No data entered`, `Odd number of characters`, `Invalid hex character(s)`; the hex is not translated to EBCDIC (a fix). Character, Hex and Both only.
**Go To** (Ctrl+G): modal `Go to offset`, label `Enter offset (0 - <size-1>)`, a digits-only edit, errors `Offset must be a number` and `Offset too large or < 0`; Character, Hex and Both only; uses `format::locate`.
Enable rules: only on the Data tab, with data present, and not while another Find dialog is open. The Ctrl+ shortcuts on the Data tab (inv 7.1) choose views and options (Ctrl+Q Parsed, Ctrl+B Both, Ctrl+T Character, Ctrl+K COBOL, Ctrl+E EBCDIC, Ctrl+L XML, Ctrl+H Hex, ...): implement exactly the table.

- [ ] **Step 1: Write the failing tests.** `DataTabTests`: each view equals a `format::render` done in the test, decoded; the CP1252 byte 0xE9 shows as `é`; a DBCS body with `display.ansi = 932` decodes; a view or option change re-renders once; COBOL with no copy book asks (stub the dialog) and with one shows the table; render failures show their text; Allow Edit and Write Q are disabled; the shortcuts select views only while the Data tab is current. `FindDialogsTests`: a Find sequence over three hits goes hit 1, 2, 3, then a miss (view at the top, no message box), then hit 1 again; a hit ending at the last byte is found; Direction Up; the EBCDIC translation of the typed text; the Hex view highlight is 2 characters on the first byte; Find Hex validation messages and a successful find of `4142` in `xxABxx`; Go To validation messages and the cursor line and column for offsets 0, 31, 32, 33 (Character) and 0, 15, 16 (Hex), each checked against `locate`.
- [ ] **Step 2–3: Run, build, run all, look at it; commit:** `feat(qt): add the Data tab with Find, Find Hex and Go To`.

---

### Task 27: Assemble the window

**Maps to:** criterion 2. **Files:**
- Modify: `qt/src/MainWindow.{h,cpp}`, `qt/src/main.cpp`, `qt/tests/MainWindowTests.cpp`

Create the shared objects once in `MainWindow` (`AppSettings`, `AppDocument`, `StatusLog`, `SessionController`, `Operations`, `FileTrace`) and give the three pages to the tab widget, replacing the placeholders. Add: the File menu (Open Ctrl+O, Save Ctrl+S, Exit), the MQ menu (Connect and Disconnect as in `rfhutilView.cpp:2462-2506`, printing `Connected to <qm>`), the Read and Write file-option menu items with check marks and the defaults of inv 3.6 (Include MQMD on, the others off; `Save Headers` is shown disabled), View > System Queues, and Search > Find / Find Hex / Go To. Menu items follow the same enable rules as the buttons (File > Open is disabled while browsing). Save and restore the window geometry through `AppSettings`. `Alt+N`/`Alt+P` step through the tabs and wrap from DLQ to Main, skipping Conn (inv 1.2). The window title is `RfhUtil` and becomes the bare queue name after the operations that set it (inv 1.1). Show `*****MQ not installed` in the status log at start-up when the MQ library cannot be loaded.

- [ ] **Step 1: Write the failing tests:** the menus exist with the right items and check marks; unchecking Include MQMD changes `WriteOptions::includeMqmd` for the next Save; Alt+N from Main goes to Data and from DLQ wraps to Main; geometry persists across two window lifetimes (temp settings file); the title changes after a Read Q on the fake; the no-MQ message appears when the loader reports it (inject the failure).
- [ ] **Step 2–3: Run, implement, run all; commit:** `feat(qt): assemble the window with its menus, shortcuts and settings`.

---

### Task 28: The GUI smoke tests against a real queue manager

**Maps to:** criteria 2, 3 and 4. **Files:**
- Create: `qt/tests/GuiSmokeTests.cpp`
- Modify: `qt/tests/CMakeLists.txt`

The spec's GUI smoke test (design section 6, item 4). It uses the real `MqSession` (bindings) on `TEST_QM` and queues named `RFHUTIL.TEST.GUI.*`, created and deleted by the fixture. It is skipped, with a clear message, when no queue manager is reachable (reuse the skip logic of `tests/core/MqSessionTests.cpp`); label it `qt` and `mq`.

- [ ] **Step 1: Write the tests:** `writeThenReadShowsTheMessageOnTheDataAndMqmdTabs` (load a file through the Main tab, set Format and Persistence on the MQMD tab, Write Q, Clear Data, Read Q: the status log has no failure, the Character view contains the text, the Hex view the right bytes, the MQMD tab shows the Format and Persistence and a new Message Id); `browseWalksFirstNextAndPrevious` (three messages; Start Browse, Next twice, Prev, End Browse; the Data tab and the enable rules at each step); `saveFileRoundTrips` (Save File after a Read Q, then `fileio::read` returns the same body and MQMD fields); `aMissingQueueShowsTheWindowsText` (`2085 No such queue`); `getByMsgIdFindsTheRightMessage`; `purgeEmptiesTheQueue` (confirmation answered Yes); `aWaitingGetCanBeCancelled` (Wait 30, Cancel after one second: back within 3 s); `loadNamesListsTheTestQueues`; `clientModeConnectsOverTheChannel` (skipped unless the client test's environment is set; see `tests/core/MqSessionTests.cpp`).
- [ ] **Step 2: Run** `ctest --preset linux-debug -L qt`, then confirm nothing remains: `echo "DISPLAY QLOCAL(RFHUTIL.*)" | runmqsc TEST_QM` prints `AMQ8147E`. **Step 3: Commit:** `test(qt): add the GUI smoke tests against a real queue manager`.

---

### Task 29: The golden message files and the Windows comparison

**Maps to:** criteria 3 and 4. **Windows-visible:** no source changes. **Independent of Qt: it may run at any time**, and the owner asked for it first. **Files:**
- Create: `tools/golden/make_goldens.cpp`, `tools/golden/compare-windows-output.sh`, `tools/golden/compare-windows-output.ps1`, `tests/core/golden/*.bin|*.txt`, `tests/core/golden/MANIFEST.md`, `tests/core/GoldenFileTests.cpp`
- Modify: `tests/CMakeLists.txt`

The spec (design section 6, item 2) wants message files "saved by Windows RFHUtil" and tests that read them, write them back byte for byte, and check the views. Files written by Windows can only come from a Windows run, so this task builds the set the other way, as the owner chose in Plan 2A ("from the MQ spec plus your files"): `make_goldens.cpp` writes each file byte by byte from the documented MQ structure layouts, using only `tests/core/mq_layout.h` (never the core's `fileio`), so the files do not depend on the code they test. The set is then **confirmed against Windows** by the procedure in `MANIFEST.md`; until the owner has run it, the files are reference files, not Windows output. Files supplied by the owner are committed only with explicit approval.

The set (each file is deterministic; the generator takes the output directory as its argument):

| File | Content | Why |
|---|---|---|
| `plain-crlf.txt` | ASCII text, CR LF line ends, no MQMD | Plain text read and write |
| `plain-lf.txt` | The same text with LF line ends | The Unix line end option |
| `binary-all-bytes.bin` | Bytes 0x00–0xFF twice, no MQMD | Binary data survives |
| `ebcdic-500.bin` | EBCDIC text (CCSID 500), no MQMD | EBCDIC detection |
| `mqmd-v2-text.bin` | 364-byte MQMD v2 (little endian, CCSID 1208, Format MQSTR) + text | The MQMD path |
| `mqmd-ebcdic-host.bin` | MQMD (encoding 785, CCSID 500) + EBCDIC text | Host-endian data with an MQMD. Known difference: the float part of the encoding is not read from the file, so 785 is written back as 529 (the core and, by the source, Windows) |
| `mqmd-rfh2-text.bin` | MQMD + RFH2 (with name/value data) + text | The header chain |
| `rfh2-only.bin` | RFH2 + text, no MQMD | A leading header without an MQMD |
| `mqmd-dlh-rfh2-text.bin` | MQMD + DLH naming RFH2 + RFH2 + text | A two-header chain |
| `mqmd-bigendian.bin` | Big-endian MQMD (encoding 273) + text | Known difference: Windows swaps the bytes inside each 4-byte word of the ids and the accounting token (the core keeps them) |
| `mqmd-mqmde-text.bin` | MQMD + MQMDE + text | Known difference: Windows reads the MQMDE at the wrong offset |

- [ ] **Step 1: Write the failing tests** (`GoldenFileTests.cpp`; the table of expectations is written by hand from the layouts, not copied from a read): for every file, `Golden.<name>ReadsAsExpected` (`hadMqmd`, the header types in order, the body bytes, `fileCcsid`, `charFormat`, `fileIntFormat`), `Golden.<name>RoundTripsByteForByte` (read with the default options, write with `includeMqmd = hadMqmd`, compare the bytes; for the two known-difference files the test asserts the core's documented behaviour instead and says so in a comment), `Golden.<name>MatchesItsManifestHash` (the SHA-256 in `MANIFEST.md`, so a changed file is noticed), and for the plain text files the Character, Hex and Both views as text the implementer derives by hand from the layout rules (32 characters or 16 bytes a line, 8-digit decimal offsets, CR LF line ends; see `Format_*` tests), asserted exactly. Add `GoldenGenerator.RegeneratesIdenticalFiles`, a CTest that runs the built `rfh_make_goldens` into a temporary directory and compares it with `tests/core/golden/`.
- [ ] **Step 2: Run them:** the build fails (`rfh_make_goldens` and the files do not exist).
- [ ] **Step 3: Write the generator, run it into `tests/core/golden/`,** record the SHA-256 of each file in `MANIFEST.md` together with a one-line description of what each byte range is (so a human can check the layout), and add the targets to `tests/CMakeLists.txt`.
- [ ] **Step 4: Write the comparison tools and the procedure.** `MANIFEST.md` has, per file, the exact steps to produce the Windows counterpart in RFHUtil on Windows (Read menu options, Write menu options, `Include MQMD` on or off, `Save File`), because Windows' defaults decide whether the output has an MQMD. `compare-windows-output.sh DIR` (and the `.ps1` twin using `fc /b` or `Compare-Object` on bytes) compares each file of `DIR` with the golden of the same name, prints `same`, `DIFFERENT (expected: <reason>)` for the two known-difference files, or `DIFFERENT` for any other, and exits non-zero on an unexpected difference. A test (`Golden.CompareScriptReportsDifferences`, run through `bash`) checks the script on a temporary directory with one identical, one modified and one known-difference file.
- [ ] **Step 5: Run all tests;** report the count. Confirm `git diff --check` is clean and that no golden message file (`MANIFEST.md` excluded) is larger than 4 KB.
- [ ] **Step 6: Commit:** `test(core): add the golden message files, their generator and the Windows comparison`. The commit message says the files are not yet confirmed by Windows.

---

### Task 30: The MFC MQMD tab delegates its put, get and ID paths to the core (Windows; needs the owner's Windows build)

**Maps to:** criterion 5. **Windows-visible:** yes, entirely. **Files:**
- Create: `tools/port/port-mqmdpage-putget.pl`
- Modify (by the script): `RFHUtil/MQMDPAGE.h`, `RFHUtil/MQMDPAGE.cpp`, `RFHUtil/RFHUtil.vcxproj` (+ `.filters`), `Client/Client.vcxproj` (+ `.filters`)

**When to run this task:** only after the owner reports that Plan 1's Windows build works, after 2A Task 6 has run (it moved `buildMQMD` and `processMQMD`), and preferably after 2B Task 9. If that report has not arrived, stop and report that this task is waiting.

**What changes:**
- `MQMDPAGE::setMessageMQMD` becomes `rfh::mqmd_fields::toPutMqmd(getFields(), context, *mqmd)` with the context built from the `DataArea` flags and `rfh_current_user()`; `updateMQMDafterPut` becomes `applyAfterPut` + `setFields`; `processMessageMQMD` becomes `fromGetMqmd` + `setFields`, followed by the `DataArea` display state it already set.
- `SetId`, `setMsgId`, `setCorrelId` and `setGroupId` call `rfh::id_display`; the page's own copies of the report, flag and persistence helpers that only they used are deleted (design section 8, criterion 5).
- Both projects compile `core/message/rfh_id_display.cpp`; the script adds it, and the include directory, the way 2A's script added `rfh_mqmd_fields.cpp`.

- [ ] **Step 1: Write the script** in the style of `tools/port/port-mqmdpage.pl` and `harden-copybook.pl`: every edit asserts its match count; every file is checked before any is written; writes go to a temporary file and are renamed into place; a marker check refuses a second run; CRLF is preserved. Run it on a scratch copy of the branch first: the edit counts, `xmllint` on the four project files, brace balance of `MQMDPAGE.cpp`, a refused second run, and a leftover-reference check (nothing may still use a deleted helper). Record the figures.
- [ ] **Step 2: Confirm Linux is unaffected:** build and run all tests (`MQMDPAGE` is not part of the Linux build).
- [ ] **Step 3: Commit** `refactor(mfc): MQMDPAGE builds and reads put and get MQMDs through the core`; the message states that the change was not compiled.
- [ ] **Step 4: Hand over the Windows checks to the owner:** build `RFHUtil.sln` (RFHUtil, rfhutilc, UnitTests) and report exact compiler messages; then, for a few messages: Write Q with Set Iden, Set All and neither, and compare the MQMD that arrives (use the Read Q/Browse on the other build) with the previous build; Read Q and Browse fill every MQMD tab field as before; the three ids in Hex, Ascii and Ebcdic modes display and edit as before (including that an untouched control byte survives an edit); Reset Ids and Copy Msg Id; File > Save still writes the same bytes.

---

### Task 31: Documentation

**Maps to:** all criteria (records them). **Files:**
- Modify: `docs/CODEMAP.md`, `CHANGELOG.md`, `docs/superpowers/specs/2026-10-15-linux-qt-port-design.md`, this plan's Execution notes, `CLAUDE.md` if the commands change; mark `2026-10-17-linux-port-plan-3a-qt-app.md` as superseded by this plan

- [ ] **Step 1: CODEMAP.** The Qt layer (`qt/`): modules, the controller and threading rule, the flow of a button press, the test layout and labels (`qt`, `mq`, `memcheck`), how to run the app, new "where do I look for" rows (Windows flow rules, enable rules, shortcuts, Find, status text, ID display), and the status section.
- [ ] **Step 2: CHANGELOG.** Added: the Qt application with the three tabs, `statusText`, `rfh_current_user`, `toPutMqmd`, `fromGetMqmd`, `id_display`, `queueInfo`, `browsePosition`, `listLocalQueueManagers`, `listQueues`, the new Get, Put and Connect options, `parseServerString`, `commit`, purge options, `locate`, the Qt tests, the core UI-free test. List the deliberate differences from Windows: the Cluster Open `Group` radio works, the purge error text order, Find also finds a match at the very end of the data, Find Hex does not translate to EBCDIC, the dropped `Ignore MQMD` option, Ctrl+ accelerators as the Windows table (not the spec's Alt list), the display code page setting, the Wait field, the local queue manager list, passwords never stored.
- [ ] **Step 3: Spec.** Section 5: the Ctrl+ table as built; section 4: the new core declarations; section 9: the differences above.
- [ ] **Step 4: Execution notes** below: counts after each task, review-fix commits, what is verified and what is not (no Windows build, TLS and client mode only if configured, no HiDPI check).
- [ ] **Step 5: Run everything:** the full `ctest`, `ctest -L qt`, `ctest -L mq`, `ctest -L memcheck`. Commit: `docs: record Plan 3 in the code map, changelog and spec`.

---

## Not in this plan

- Packaging: the RPM, desktop entry and icon, and moving the debug dump files out of `/tmp` (Plan 4; design section 7, acceptance criterion 1).
- Save Q, Load Q, Move Q, Display Q, Capture and Write Pubs, theming, `mqperf`.
- The header tabs (RFH, CICS, IMS, DLQ, PS, PubSub, jms, usr, other, Usr Prop, Conn) and editing on the Data tab (Allow Edit, Write Q); with them `Message::headers` can be built by the app and the `Yes` user-properties mode becomes possible.
- Automatic reconnect and heart beat (G7c).
- Windows CI and the Windows-written golden files (criteria 3 and 4).

## Execution notes

(Filled in during execution.)

### Open points for the owner

Facts only. Decided on 2026-10-17 and no longer open: the four Windows defects are fixed on purpose (the Cluster Open `Group` radio, which has no click handler on Windows; the purge error text with `cc` and `rc` swapped; Find missing a match that ends exactly at the end of the data; Find Hex translating hex input to EBCDIC), with `Ignore MQMD` dropped as dead; the local queue managers are listed from `mqs.ini`; passwords are kept in memory only, for now (Windows stores the Set Conn Id password in the registry when "Save password" is on, `initSavePassword`, UNVERIFIED); and the golden set is created (Task 29).
- The golden files are built from the MQ structure layouts, not written by Windows. They become Windows-confirmed only when the owner runs the procedure in `tests/core/golden/MANIFEST.md` on a Windows build and the comparison script reports no unexpected difference. Files the owner supplies are committed only with explicit approval.
- Not ported although Windows has them: `SAFE_MODE` read-only mode, `Save Headers` (the core does not support it), audit records (`createAuditRecord`) and the trace menu.
- Task 30, and with it criterion 5, waits for the owner's Windows build; it must run after 2A Task 6, and preferably after 2B Task 9. All of Task 5's Windows branch and Task 30 are uncompiled until then.
- Features beyond Windows that the plan adds: the local queue manager list from `mqs.ini` (decided; default `/var/mqm`, and MQ may be installed with another data path), and, not yet decided, the Connection box, the Wait field with Cancel and the display code page setting.
- Shortcuts: the spec lists Alt+Q-style keys, the Windows accelerator table is Ctrl+letters that change meaning per tab; the plan follows the table and keeps the Alt mnemonics.
- User properties: the `Yes` mode needs a property list in `Message` and the Usr Prop tab; the other three modes are core flags only. On Windows the None, RFH2 and Compat radios do nothing on a Get.
- Load Names needs permission to create a dynamic reply queue from `SYSTEM.DEFAULT.MODEL.QUEUE` and to put to the command queue; on a queue manager that refuses, Load Names fails with the Windows text for the reason code. PCF is not available on z/OS before V6.
- `Get by Msg Id` and the related options apply only when the MQMD tab id is not all zero (a Windows rule the core does not have); the app implements it.
- TLS and client mode can be tested here only if the owner configures a channel; `TEST_QM` is tested in bindings mode. The live TLS test of Task 16 is unverified without it.
- The window title on Windows is `RfhUtil V<major>.<minor>.<revision>.<build>`; there is no version source in this fork, so the plan uses `RfhUtil`.
- The display code page default is 1252 (the Windows ANSI default); a DBCS user must set it, and `rfh_mbclen` and the COBOL view follow the process code page, not the setting (2B `rfh_format.h`).
- No visual check on HiDPI or a high-contrast theme is possible here; the tasks use offscreen grabs and a look on the Wayland desktop.
- `fromGetMqmd` and `toPutMqmd` differ from `fromMqmd`/`toMqmd` where Windows' put and get paths differ from its file paths; the plan keeps both, as 2A recorded.
- Remaining decisions deferred from 2B: `PubSub.cpp:2007`, upstream PR shape, licence headers and the trailers; none is touched here.
