# Code map

For an agent that knows nothing about this repository. Facts were checked against the
code at `f415713` (branch `features/linux-qt-port`); names of symbols are stable, line
numbers are not, so use `tools/index/q sym NAME` or `q outline PATH` to get current ranges
(`tools/index/README.md`). Plans and the spec are the authority for decisions; this file
says where things are.

## 1. What this is

- **The fork.** IBM's RFHUtil is a Windows MFC tool for putting, getting and inspecting IBM MQ
  messages (Main, Data, MQMD, RFH, JMS, PubSub and other tabs). This repository is a fork of
  `ibm-messaging/mq-rfhutil` (upstream commit `09744ae` is the branch point). The fork exists
  for testing in the wild. It is not being upstreamed for now, and the Linux side may drift from
  IBM's code. `origin` still points at IBM's repository: nothing is pushed from here.
- **The goal.** A Qt6 Linux application (Fedora 44, IBM MQ 9.4 at `/opt/mqm`) on top of a portable
  C++17 core library `rfhcore`, while the Windows MFC application keeps building from the same shared
  sources. Phase 1 covers the Main, Data and MQMD tabs. Both connection modes (local bindings and
  client) are chosen at run time in one binary.
- **What exists.** The core (platform layer, MQ loader and session, MQMD field conversion, header chain,
  message files, the eight Data tab views) with 614 ctest tests. **There is no GUI yet**: the Qt
  application (`qt/` in the spec) is Plan 3 and has not been started.
- **Branch.** `features/linux-qt-port` (the spec says `feat/...`; it was renamed so upstream CI,
  which runs on `master` and `features/**`, would run). Not pushed.
- **Governing documents.** Spec: `docs/superpowers/specs/2026-10-15-linux-qt-port-design.md` (section 4 core
  API, section 9 risks and differences). Plans: `docs/superpowers/plans/` (1 portable core, 2A message
  files, 2B Data views; each ends with Execution notes). Release notes: `CHANGELOG.md`, `[Unreleased]`.

## 2. Layout

| Path | Whose | What |
|---|---|---|
| `core/platform/` | ours | Win32 type shims, code page conversion (iconv), trace sink, time: `rfh_platform`, `rfh_codepage`, `rfh_trace` |
| `core/compat/` | ours | Linux stand-ins: `stdafx.h`, and `core_prelude.h` which blocks IBM's `rfhutil.h` |
| `core/mq/` | ours | `Result`, `MqLoader` (dlopen), `MqSession` |
| `core/message/` | ours | `Message`, `MqmdFields`, `HeaderChain` / `walkHeaders` |
| `core/fileio/` | ours | RFHUtil message file read and write |
| `core/format/` | ours | `format::render` and `format::defaultsFor` |
| `core/CMakeLists.txt` | ours | `rfhcore` static library; lists `RFHUTIL_SHARED_SOURCES` |
| `RFHUtil/` | IBM's (about 86k lines of MFC C++, about 100 files) | The Windows application. Edited only where noted below. |
| `RFHUtil/DataFormatter.{h,cpp}`, `uisubs.{h,cpp}`, `rfhconst.h` | ours, new | Shared formatters (generated from `DataArea.cpp` by a script), MFC-only `processBackspace`, constants moved out of `rfhutil.h` |
| `Client/` | IBM's | `Client.vcxproj`: the client-only Windows build (`rfhutilc`); compiles the same `RFHUtil/` sources |
| `tests/UnitTests/` | IBM's, adapted | GoogleTest suites compiled by both `UnitTests.vcxproj` (Windows) and CMake (Linux); `prelude.h`, `rfhutil.h`, `stdafx.h` are test stubs |
| `tests/core/` | ours | Linux CMake tests (+ `PlatformTests.cpp`, `SharedSourcesTests.cpp`, which Windows also compiles) |
| `tests/tools/guardmalloc.cpp` | ours | Guard-page allocator (`LD_PRELOAD`) |
| `tests/docker/` | IBM's | `compose.yaml` + `config/test-qm.mqsc`: a queue manager container for client-mode tests |
| `tests/IntegrationTests/` | IBM's | Windows-only IDPWOS authentication tests |
| `tools/port/` | ours | Perl edit scripts for IBM's files (section 4) |
| `tools/index/` | ours | `q`, the code index (not part of any product build) |
| `mqperf/` | IBM's | Command-line MQ performance tools; own `Makefile`, builds on Linux, unchanged |
| `bin/`, `packages/`, `.readme/` | IBM's | Prebuilt Windows executables, vendored GoogleTest 1.8.1 NuGet package, README images. Binary or vendored: do not read. |
| `docs/` | IBM's | Upstream design notes (`ARCHITECTURE_ANALYSIS.md`, `BUILD_CONFIG.md`, `UNIT_TESTING.md`, ...) |
| `docs/superpowers/` | ours | Spec and plans |
| `CMakeLists.txt`, `CMakePresets.json` | ours | Top-level Linux build; exports `compile_commands.json` |

**Shared sources.** These nine IBM files are compiled both by the Windows projects and by
`rfhcore`; they are listed in `RFHUTIL_SHARED_SOURCES` in `core/CMakeLists.txt`: `comsubs`, `xmlsubs`,
`JsonParse`, `XMLParse`, `Names`, `mqerror`, `mqsubs`, `DataFormatter`, `Copybook` (all `.cpp`). They stay in
`RFHUtil/` so the Visual Studio projects keep their paths, and they exist in both builds so Linux runs
the code Windows runs instead of a copy. On Linux they get `-include core/compat/core_prelude.h` and
`-fsigned-char` (MSVC's `char` is signed); the rest of `rfhcore` is built with `-Wall -Wextra`.
Which Windows project compiles what today: all of the six original ones are in `RFHUtil.vcxproj`,
`Client.vcxproj` and `UnitTests.vcxproj`; `mqsubs` and `Copybook` are in the first two only; **`DataFormatter.cpp`
and everything under `core/` except the platform layer are in no Windows project yet**.
Windows-only code (MFC tabs, `DataArea`, dialogs) cannot be compiled on this machine.

## 3. The core, module by module

Everything is in namespace `rfh` (platform functions are global `rfh_*`). Core code never shows UI,
never exits, and returns `rfh::Result` instead of throwing. Dependencies point inward:
platform <- mq, message <- fileio <- format. The headers are authoritative; each has a long comment
listing its differences from Windows.

**core/platform** (`rfh_platform.h`, `rfh_codepage.h`, `rfh_trace.h`). Depends on libc, iconv, pthread.
- `rfh_platform.h`: Win32 names on Linux (`BOOL`, `UINT`, `LPCTSTR`, `_snprintf_s`, `RFH_DIR_SEP`), `RFH_UCS2` (16-bit
  code unit: `char16_t` on Linux because `wchar_t` is 32-bit), `rfh_strupr`, `rfh_ucs2len`, `rfh_hrtime_now`,
  `rfh_utc_now`, `rfh_dump_path` (debug dump files go to `/tmp` on Linux).
- `rfh_codepage.h`: `rfh_get_acp()` (1252, or `RFHUTIL_ACP`), `RfhAcpScope` (thread-local override),
  `DisplayCodepages {ansi, oem}`, `rfh_convert_char`, `rfh_WideCharToMultiByte`, `rfh_MultiByteToWideChar`,
  `rfh_mbclen`, C-locale UCS-2 helpers. On Windows these call Win32; on Linux iconv.
- `rfh_trace.h`: `RfhTrace` sink, `rfhGetTrace()` (never null), `rfhSetTrace()`. The MFC `CRfhutilApp` implements it.

**core/mq** (`rfh_result.h`, `rfh_mq_loader.h`, `rfh_mq_session.h`). Depends on MQ headers only (library loaded at run time).
- `Result {cc, rc, text, warnings}`; `Result::failure`, `Result::fromMq("MQGET", cc, rc)`, `reasonName(rc)`.
- `MqLoader::load(MqApi &, installationPath)`: loads `libmqm_r.so` (or `libmqic_r.so`) once per process;
  `MqApi` holds the seven entry points, `serverLibrary`, `bindingsAvailable`. Test seam: `detail::libraryCandidates` / `loadFrom`.
- `MqSession`: owns one connection handle and one open queue handle. `connect(ConnectOptions)` (bindings or client, channel/CCDT,
  credentials sent only with a password), `put`, `get` (destructive, `BrowseFirst/Next/Previous`, wait, convert, cancel flag),
  `depth`, `purge`, `closeQueue`, `disconnect`. One thread at a time. `MqSession(const MqApi &)` injects a fake MQI.
  `put` joins `message.headers` + `message.body`; `get` splits them using `walkHeaders`.

**core/message** (`rfh_message.h`, `rfh_mqmd_fields.h`, `rfh_header_chain.h`). Depends on MQ headers and `rfhconst.h`.
- `Message {MQMD2 mqmd; vector headers; vector body}`: owns the message bytes. Headers are raw bytes (header tabs are not ported).
- `MqmdFields` + `mqmd_fields::{toMqmd, fromMqmd, applyMdExtension, clear, msgTypeText}`: the MQMD tab's text/radio/checkbox
  state to and from an `MQMD2`. Port of `MQMDPAGE::buildMQMD` / `processMQMD`.
- `walkHeaders(data, len, format, ccsid, charFormat, encoding)` and `walkHeaders(data, len, MQMD2)`: finds DLQ, CICS, IMS, RFH1,
  RFH2, MQMDE headers and the body offset; returns a `HeaderChain` (offsets, `bodyFormat`, `bodyCcsid`, `charFormat`,
  `encoding`, MQMDE fields, warnings). `detectLeadingHeader` and `looksLikeEbcdic` classify a file that has no MQMD.

**core/fileio** (`rfh_message_file.h`). Depends on message, platform.
- `fileio::read(path, ReadOptions, MqmdFields &, ReadResult &)` and `fileio::write(path, Message, WriteOptions)`. A file is an optional
  364-byte MQMD (version 2, native order), then headers, then body. `ReadResult` carries the message, the chain and the display
  formats (`fileCcsid`, `charFormat`, `numericFormat`, ...). Result text reproduces Windows' status line.

**core/format** (`rfh_format.h`). Depends on message, fileio, and the shared `DataFormatter` and `Copybook`.
- `View {Character, Hex, Both, Xml, Parsed, Cobol, Json, Fixed}` (values equal `DATA_*` in `rfhconst.h`).
- `DataFormats {ccsid, intFormat, charFormat, numericFormat, pdNumericFormat}` come from the message; `RenderOptions : DataFormats`
  adds the user's choices (`crlf, edi, bom, indent, checkData, display, conversionTableDir, copybookPath`) and `apply(DataFormats)`.
- `defaultsFor(Message, warnings)` / `defaultsFor(ReadResult)`: the Data tab settings after a get / a file read.
- `render(Message, View, RenderOptions, out)`: shows `message.body` (never the headers) in the Windows ANSI code page
  (`options.display.ansi`), not UTF-8. Fails for a body over `kMaxRenderBody` (100 MB). Nothing is cached.

**How a message flows.**
- *Get/browse:* `MqSession::get` -> `Message` (headers split off by `walkHeaders`) -> `format::defaultsFor(message, warnings)` ->
  `options.apply(...)` -> `format::render(message, view, options, out)` (Data tab) and `mqmd_fields::fromMqmd` (MQMD tab).
- *File read:* `fileio::read` -> `ReadResult` (+ fills `MqmdFields`) -> `defaultsFor(ReadResult)` -> `apply` -> `render`.
- *Put:* `fileio::read` or typed data -> `Message`; MQMD tab fields -> `mqmd_fields::toMqmd` -> `message.mqmd` -> `MqSession::put`.
- *Save:* `fileio::write(path, message, WriteOptions)`, byte layout equal to `DataArea::WriteFile`.

## 4. IBM's shared sources and the edit scripts

IBM's files are changed by scripts, not by hand, so the port can be re-applied after IBM changes a file and so reviewers can
see exactly what changed. Each script asserts how many times every edit matched, checks every file before writing any, and
refuses a second run (verified for all seven by running them against a copy of `HEAD`). Applied in this order:

| Order | Script | Base | What it changes |
|---|---|---|---|
| 1 | `port-shared-sources.pl` | `09744ae` | `comsubs`, `xmlsubs`, `JsonParse`, `XMLParse`, `Names` (+ `.h`): Win32/MFC calls to `core/platform`; moves `processBackspace` into new `uisubs.{h,cpp}`; adds `#include "uisubs.h"` to the MFC pages that used it |
| 2 | `port-windows-projects.pl` | `09744ae` | `RFHUtil.vcxproj`, `Client.vcxproj`, `UnitTests.vcxproj` (+ filters), `rfhutil.h/.cpp` (`CRfhutilApp` implements `RfhTrace`) |
| 3 | `port-mqsubs.pl` | Plan 1 end (`7febad7`) | `mqsubs.{h,cpp}`: portable, drops `findMQInstallDirectory`; constants move from `rfhutil.h` to new `rfhconst.h` |
| 4 | `port-parser-fixes.pl` | `0e065a2` | `JsonParse.{h,cpp}`, `xmlsubs.{h,cpp}`: memory-safety fixes; `processAttributes` takes an output size, so 4 call sites in `DataArea.cpp` change |
| 5 | `port-formatters.pl` | `0e065a2` | Copies 15 view methods from `DataArea.cpp` into `DataFormatter.cpp` after a hand-written prelude; `DataArea.cpp` untouched |
| 6 | `port-copybook.pl` | `0e065a2` | `Copybook.{h,cpp}`: becomes a shared source (no MFC base class, trace via `rfhGetTrace`, `rfh_mbclen`) |
| 7 | `harden-copybook.pl` | after 6 | `Copybook.{h,cpp}`: overruns, divide by zero and endless loops on malformed copy books or short data |

Not in the repository yet: `port-mqmdpage.pl` (Plan 2A Task 6) and `port-dataarea-formatters.pl` (Plan 2B Task 9; the
corrected copy is in `~/projects/mq-rfhutil-notes/`). Both change Windows-only files (see section 7).

**Base commits.** Branch point: `09744ae` (upstream master); the first commit of the branch is `a44d490` (the spec).
Plan 1 ends at `7febad7`; Plan 2A ends at `0e065a2`, which is the base for the Plan 2B scripts.

**Reproducing a file ("restore, re-run, compare").** For a file a script owns end to end:
`git show 0e065a2:RFHUtil/xmlsubs.cpp > RFHUtil/xmlsubs.cpp` (do this for every file the script touches), run
`perl tools/port/port-parser-fixes.pl .` from the repository root, then `git diff HEAD -- RFHUtil/xmlsubs.cpp` must be empty.
Checked on a scratch copy: it reproduces `JsonParse.*`, `xmlsubs.*`, `DataArea.cpp` (step 4) and `Copybook.*` (steps 6 and 7
in order). It does not apply to `DataFormatter.cpp`: its prelude and later fixes (`160e20a`, `98269d3`, `5474e88`, `4c13e56`) were
edited by hand, and this was not reproduced. If a script drifts from `HEAD`, the script is wrong, not the file.

## 5. Build and test

- **Configure, build, test** (from the repository root):
  `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`. Presets: `linux-debug`,
  `linux-release` (Unix Makefiles, `build/<preset>`). `build/linux-debug/compile_commands.json` is exported for clangd.
  Requires CMake 3.21+, GCC with C++17, GoogleTest (`-DRFHUTIL_FETCH_GTEST=ON` downloads 1.17.0), IBM MQ SDK headers (`MQ_HOME`,
  default `/opt/mqm`; headers only, the library is loaded at run time).
- **Executables** (`tests/CMakeLists.txt`): `rfhutil_unit_tests` (IBM's suites + platform + shared-source tests), `rfhutil_core_tests`
  (everything else, no queue manager), `rfhutil_loader_tests` and `rfhutil_mq_tests` (label `mq`), `rfhutil_mq_fake_tests` (fake MQI).
- **Expected result:** 614 tests, 100% pass, 8 skipped (6 `GuardMalloc` tests and
  `DataFormatter.SetDataAllocatesExactlyTheDataAndItsPad`, which need the allocator preloaded and run under memcheck;
  `MqClient.PutThenGetOverClientChannel`).
- **Labels.** `ctest -L mq` (36 tests: loader and session tests against a real queue manager, plus the client test).
  `ctest -L memcheck` (5 tests: `memcheck.GuardMalloc`, `.DataFormatter`, `.Parsers`, `.Format`, `.FormatFuzz`).
- **Queue manager.** `MqSessionTests.cpp` uses `MQ_BINDINGS_QMGR` (default `TEST_QM`; the spec's `RFHUTIL_TEST_QM` is not what
  the code reads). Its fixture creates `RFHUTIL.TEST.Q`, `.EMPTY`, `.EXCL` with `runmqsc` and deletes them; the user must be in the
  `mqm` group. **Rule:** tests may create and delete only `RFHUTIL.TEST.*` queues, and must leave none behind
  (`echo "DISPLAY QLOCAL(RFHUTIL.TEST.*)" | /opt/mqm/bin/runmqsc TEST_QM` must report AMQ8147E). MQ tests skip when MQ is
  unavailable; `RFHUTIL_REQUIRE_MQ=1` turns skips into failures. Client mode runs when `MQ_CHANNEL` is set
  (`MQ_HOST`, `MQ_PORT`, `MQ_QMGR`, `MQ_USER`, `MQ_PASSWORD`, `MQ_CLIENT_QUEUE`; `tests/docker/compose.yaml` provides a queue manager).
- **Guard allocator.** `tests/tools/guardmalloc.cpp` builds the shared library `rfh_guardmalloc`; `LD_PRELOAD` places each block at
  the end of a page followed by an inaccessible page, so an overrun faults at once (16-byte alignment: up to 15 bytes unguarded;
  `DataFormatter` pads 32 bytes, so small overruns of its work buffers are hidden). `rfh_add_memcheck_test(name target filter ...)`
  in `tests/CMakeLists.txt` registers a suite under it. `FORMAT_FUZZ_SEED` and `FORMAT_FUZZ_ITERATIONS` control `FormatFuzz`.
- **AddressSanitizer.** The system GCC has no libasan. The recipe that works here (checked on a trivial program, not on the whole
  tree) uses Homebrew's compiler:
  `g++-15 -B/usr/lib64 -fsanitize=address,undefined ...`, for example
  `cmake -S . -B build/asan -DCMAKE_CXX_COMPILER=g++-15 -DCMAKE_CXX_FLAGS="-B/usr/lib64 -fsanitize=address,undefined"` (that cmake line is untested).
- **Code index.** `tools/index/q` (see its README); tests `python3 -m unittest discover tools/index/tests`, smoke test `tools/index/smoke.sh`.
- **Windows.** `RFHUtil.sln` (Release|x64 and others) is the owner's build; CI is `.github/workflows/build-and-test.yml`
  (windows-2022, `UnitTests` for Win32 and x64, on `master` and `features/**`). It cannot run here.

## 6. Conventions

- **Style.** `core/` uses tabs and Allman braces; `tests/core/` uses four spaces. IBM's files keep their own style. All files
  in the working tree have LF endings (the scripts also accept CRLF). New files in `core/`, `tests/` and `tools/` carry no
  licence header; `DataFormatter.{h,cpp}` carry IBM's.
- **No `#ifdef _WIN32` in shared sources**; platform differences live in `core/platform/`. Use `RFH_UCS2` for 16-bit text.
- **Commits.** `git commit -s` (DCO); Conventional Commit subjects (`feat(core):`, `fix(shared):`, `test(core):`, `docs:`, `build:`);
  trailers `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` and the `Claude-Session:` line you were given.
  Small commits that bisect. One CHANGELOG entry per user-visible change.
- **Never push.** The owner creates the fork and pushes. Do not install packages (`dnf`, `pip`, `brew`, `npm`) without asking.
- **Windows-only files cannot be compiled here** (MFC, `DataArea.cpp`, `.vcxproj`). Check project files with `xmllint`; say what
  is unverified.
- **Results, not exceptions;** passwords are never traced or copied; every operation that touches MQ names the call in its `Result`.

## 7. Policies and decisions that govern changes

1. **Memory-safety bugs in IBM's shared sources are fixed, with a test.** Reason: they crash, hang or overrun on malformed input
   on Windows too, so a test cannot pin them (owner: "Fix them, report upstream", 2B). Fix them in a `tools/port` script, never by hand.
2. **IBM behaviour bugs are pinned, not changed.** A test records the current behaviour (e.g. `case 037:` in `matchEbcdicToAscii`,
   `getFloatEncode`, JSON arrays, `setEscChar`'s dead `#` branch). Reason: Windows compatibility is the product.
3. **Output for valid input stays byte-identical to IBM's.** A fix that changes valid output is wrong (e.g. `18c5ff3`
   restored identical JSON output when it fits). Reason: the files and views must match Windows.
4. **Big-endian ids are not reversed.** `translateMQMD` keeps MsgId, CorrelId, GroupId (and AccountingToken) as they are; Windows' `processMQMD`
   reverses them. Owner decision, 2A. An MQMDE is read at its real offset and swapped only for host data.
5. **Windows changes wait for the owner's Windows build.** 2A Task 6 (`port-mqmdpage.pl`, MFC MQMD tab delegates to the core) and 2B Task 9
   (`port-dataarea-formatters.pl`, `DataArea` shows its views through `DataFormatter`, about 4,300 lines go) are gated on the owner reporting that
   Plan 1's Windows build works (RFHUtil, rfhutilc, UnitTests). The scripts and the plan text exist; do not run them, and do not
   stack other untested Windows edits. Reason: failures would be impossible to place. The Windows build is the owner's.
6. **Fork, not upstream.** Do not shape work for an upstream PR. If changes are filed later: split them, leave `tools/port/` and
   `docs/superpowers/` out, one licence file based on theirs, and a document of fixes and differences (spec section 9 and
   `~/projects/mq-rfhutil-notes/upstream-issue-draft.md`, which is not in the repository).
7. **One owner per concern** (spec section 3): `MqSession` owns the connection; `Message` owns bytes; formatters and the writer only read
   it; MQMD conversion lives only in `mqmd_fields`. Code that moves is deleted from `DataArea`, not duplicated (Task 9 will do this).
8. **Partial facts are kept:** an unparseable header stays as raw bytes with a warning; warnings accumulate in `Result.warnings`.
9. **Ask first** before creating the GitHub fork, pushing, installing anything, or pulling containers.

## 8. Known quirks and deliberate differences from Windows

Spec section 9 ("Risks") lists them all; the headers `rfh_header_chain.h`, `rfh_message_file.h`, `rfh_format.h`, `rfh_codepage.h`
and `DataFormatter.h` repeat the ones that belong to them. The ones most likely to trip you up:

1. **Two copies of the view code.** `DataArea.cpp` still holds its own `getCharacterData` ... `getFixData` (until Task 9); the core uses
   `DataFormatter.cpp`. A fix to a view must be considered for both. Windows currently runs the `DataArea` copy.
2. **Text is in a Windows code page, not UTF-8.** `render` output is in `options.display.ansi` (1252 unless `RFHUTIL_ACP`); a UI must decode it.
   Characters a code page cannot show become `.` or `?` (Windows may pick a best fit); `rfh_MultiByteToWideChar` stops at an invalid
   byte. Display only; message and file bytes are never affected.
3. **UTF-16 and UTF-8 bodies show byte by byte** because IBM disabled the Unicode character view (`if (0)` in `getCharacterData`).
4. **The COBOL view needs `copybookPath`** and fails where Windows shows a partial result (unopenable copy book, parser error, buffer over
   256 MB `kMaxCobolBuffer`); it lists fewer occurrences when data is short. `DataArea::getCobolData` keeps its own logic that `renderCobol` duplicates.
5. **Limits.** `render` rejects bodies over 100 MB (`kMaxRenderBody`); the formatters' `int` arithmetic wraps above about 178 MB.
   `processAttributes` needs an output size argument; nesting over 256 levels (`JPARSE_MAX_DEPTH`) is rejected in the JSON parser.
6. **Header handling is raw.** Headers are written back exactly as read; the "keep headers" (save RFH) option and the group/segment read branch are not
   supported; a header longer than the data ends the chain with a warning; an MQMDE under 72 bytes ends the chain.
7. **Spec and code disagree in places.** Spec says `RFHUTIL_TEST_QM` and branch `feat/...`; the code reads `MQ_BINDINGS_QMGR`, the branch is
   `features/...`. The spec's "aimed at upstream" is superseded by section 7.6. Debug dumps go to `/tmp/rfhdump.txt` and `/tmp/namedump.txt`.
8. **The Qt app must** set `LC_NUMERIC`/`LC_CTYPE` to "C" after `QCoreApplication` (shared sources use `toupper`/`atof`) and never include
   `RFHUtil/rfhutil.h` or `StdAfx.h`; call `options.apply(defaultsFor(...))` after each get or read; map MQMD report radio groups with -1 = none selected.

## 9. Where do I look for...

Use `tools/index/q sym NAME` for the current range.

| Question | Where |
|---|---|
| How does the Data tab decide the starting character type for a message from a queue? | `format::defaultsFor(Message, warnings)` in `core/format/rfh_format.cpp` (`typeCcsid` -> `getCcsidType` in `RFHUtil/mqsubs.cpp`, then `walkHeaders(...).charFormat`). Windows: `DataArea::processMessage` then `parseMsgHeaders` in `DataArea.cpp` |
| Which code pages are ASCII, EBCDIC, Japanese, Chinese, Korean, Thai? | `getCcsidType` in `RFHUtil/mqsubs.cpp` (default `CHAR_ASCII`); `matchEbcdicToAscii` in `RFHUtil/comsubs.cpp` picks the ANSI code page for an EBCDIC one (pinned CCSID 37 quirk) |
| How is a header chain walked? | `walkHeaders` in `core/message/rfh_header_chain.cpp`; the differences are in the comment of `rfh_header_chain.h`; tests `HeaderChain.*` in `tests/core/HeaderChainTests.cpp` |
| How is a file without an MQMD classified? | `detectLeadingHeader`, `looksLikeEbcdic` (`rfh_header_chain.cpp`); `fileio::read` (`core/fileio/rfh_message_file.cpp`) |
| What is the message file layout? | `rfh_message_file.h` comment; `fileio::read` / `fileio::write`; `tests/core/MessageFileTests.cpp` (`MessageFile.*`) |
| MQMD tab fields <-> MQMD | `mqmd_fields::toMqmd` / `fromMqmd` (`core/message/rfh_mqmd_fields.cpp`); Windows `MQMDPAGE::buildMQMD` / `processMQMD`; `MqmdFields.*` tests |
| Big-endian or EBCDIC MQMD normalising | `translateMQMD`, `getIntEncode`, `getFloatEncode` in `RFHUtil/mqsubs.cpp`; `Mqsubs_*` tests |
| Display/encoding constants (`NUMERIC_PC`, `CHAR_EBCDIC`, `DATA_COBOL`, `FLOAT_390`) | `RFHUtil/rfhconst.h` |
| Connect, put, get, browse, purge | `MqSession` in `core/mq/rfh_mq_session.cpp` (`getOnce`, `browseTo`); tests `MqBindingsTest.*`, `MqSessionFakeTest.*` |
| How is the MQ library found and loaded? | `MqLoader::load`, `detail::libraryCandidates`, `detail::loadFrom` in `core/mq/rfh_mq_loader.cpp` |
| MQ failure text (`RC=2033 ...`) | `Result::fromMq`, `reasonName` in `core/mq/rfh_result.cpp`; `RFHUtil/mqerror.cpp` |
| How do I render a view? | `format::render` (`core/format/rfh_format.cpp`) -> `DataFormatter::getXxxData` (`RFHUtil/DataFormatter.cpp`) |
| How does the COBOL view get from `render` to the copy book parser, and what limits apply? | `renderCobol` (anonymous namespace in `rfh_format.cpp`) -> `CCopybook::parseCopyBook` / `getMaxLines` / `formatData` in `RFHUtil/Copybook.cpp`; limits `kMaxCobolBuffer`, `kMaxRenderBody` in `rfh_format.h` |
| Which tests cover the copy book parser? | `Format_Cobol` (9), `Format_CobolIbm` (7), `Format_CobolMalformed` (17) in `tests/core/FormatTests.cpp`; helper `tests/core/copybook_file.h`; run under memcheck as `memcheck.Format` |
| Where is the both view's buffer size, or the fixed view's display table? | `DataFormatter::getBothData`, `getFixData`; `invalidCharTable` and `setDisplay` in `DataFormatter.{h,cpp}`; `buildInvalidCharTable` in `comsubs.cpp` |
| What writes `xmlAttrStr` in the parsed view? | member of `DataFormatter` (`DataFormatter.h`); cleared in the constructor, `getNameValueData` and `getParsedData`, then filled by `processAttributes` (`RFHUtil/xmlsubs.cpp`, `appendBounded`) and copied out with `strcpy`; tests `DataFormatter.ParsedViewBounds*`, `XmlsubsAttributes.*` |
| Where do the 32 bytes of look-ahead padding come from? | `kLookAheadPad` and `DataFormatter::setData` in `DataFormatter.{h,cpp}` |
| JSON/XML parsers and their limits | `CJsonParse` (`RFHUtil/JsonParse.cpp`, `JPARSE_MAX_DEPTH`); `CXMLParse` (`XMLParse.cpp`); helpers in `xmlsubs.cpp` (`processTag`, `findXmlValue`, `removeVarName`, `extractAttribute`, `setEscChar`) |
| Where is the guard-page allocator and its ctest entries? | `tests/tools/guardmalloc.cpp`; self-tests `GuardMalloc.*` in `tests/core/GuardMallocTests.cpp`; registered by `rfh_add_memcheck_test` in `tests/CMakeLists.txt` as `memcheck.*` (`ctest -L memcheck`) |
| Fuzz testing | `tests/core/FormatFuzzTests.cpp` (`FormatFuzz.*`, `FORMAT_FUZZ_SEED`, `FORMAT_FUZZ_ITERATIONS`) |
| Code page conversion on Linux | `core/platform/rfh_codepage.cpp` (`rfh_convert_char`, `rfh_WideCharToMultiByte`, `rfh_MultiByteToWideChar`); tests `Platform_Codepage.*` |
| Which ANSI code page is used? | `rfh_get_acp`, `RfhAcpScope` (`rfh_codepage.h`); `DisplayCodepages` in `RenderOptions::display` |
| Trace output | `core/platform/rfh_trace.{h,cpp}`; Windows `CRfhutilApp` (`RFHUtil/rfhutil.h`); `Platform_Trace.*` |
| Win32 type shims on Linux (`BOOL`, `_snprintf_s`, `RFH_UCS2`) | `core/platform/rfh_platform.h` |
| Why does `rfhutil.h` not break the Linux build? | `core/compat/core_prelude.h` defines its include guard; `core/compat/stdafx.h` replaces `StdAfx.h` |
| What edit script produced a change in an IBM file? | section 4; header comment of each `tools/port/*.pl`; `git log -- tools/port` |
| Which files does a Windows project compile? | `RFHUtil/RFHUtil.vcxproj`, `Client/Client.vcxproj`, `tests/UnitTests/UnitTests.vcxproj` (`ClCompile Include`) |
| Windows tab classes | Main `General.cpp` (`IDD_MAIN`), Data `MSGDATA.cpp` (`updateMsgdata`), MQMD `MQMDPAGE.cpp`, RFH `RFH.cpp`, DLQ `Dlq.cpp`, CICS `CICS.cpp`, IMS `Ims.cpp`, JMS `jms.cpp`, pub/sub `PubSub.cpp`, `PS.cpp`, `pscr.cpp`, user props `Usr.cpp`, connection settings `ConnSettings.cpp`; sheet `MyPropertySheet.cpp` |
| Windows message state, MQ calls, file I/O | `DataArea` (`DataArea.cpp`, 25.8k lines): `ReadFileData`, `WriteFile`, `WriteDataFile`, `processMessage`, `parseMsgHeaders`, `checkDataForHeader`, `checkForEBCDIC` |
| Reconnect/backoff logic | `MQConnection::Reconnect` (`RFHUtil/MQConnection.h`); `tests/UnitTests/ReconnectTests.cpp` |
| Dark mode and theming | `RFHUtil/ThemeManager.cpp`; `docs/DARK_MODE_SYSTEM_CONTROLS_SOLUTION.md` |
| Test helpers for MQ structures | `tests/core/mq_layout.h` (builds MQ structures byte by byte at IBM's offsets); `tests/core/gtest_compat.h` (`GTEST_SKIP` for googletest 1.8.1) |
| Windows test stubs | `tests/UnitTests/prelude.h`, `rfhutil.h`, `stdafx.h` |
| Client-mode test queue manager | `tests/docker/compose.yaml`, `tests/docker/config/test-qm.mqsc`; `MqClient.*` in `MqSessionTests.cpp` |
| What the Qt app must offer | spec section 5; Plan 3 does not exist yet |
| Release notes and what is unreleased | `CHANGELOG.md` `[Unreleased]` |
| Upstream bugs found by the port | spec sections 9 and Plan 1/2A/2B "Background"; the draft issue is outside the repository |

## 10. Current status and what is next

- **Done.** Plan 1 (portable core, complete at `7febad7`; client mode verified 2026-10-16 against an MQ 10.0.1.0 container), Plan 2A (message files,
  MQMD fields, headers; `0e065a2`), Plan 2B (Data tab views, guard allocator, memory-safety fixes; Tasks 1-8 and 10, HEAD `f415713`).
  614 tests pass, 8 skipped; `ctest -L memcheck` passes 5 of 5.
- **Gated on the owner's Windows build of Plan 1** (RFHUtil, rfhutilc, UnitTests): Plan 2A Task 6 and Plan 2B Task 9 (section 7.5). Windows
  CI (Plan 1 Task 6) is also the owner's. Unverified on Windows: everything under `core/` and all edited shared sources;
  behaviours still assumed: unmappable surrogate pair gives two default characters, code page 1258 flush, the used-default flag, `mbclen`.
- **Next.** Plan 3: the Qt6 application (Main, Data, MQMD tabs; `qt/`); it also moves the put/get MQMD paths (`setMessageMQMD`,
  `processMessageMQMD`) and ID display into the core. Plan 4: RPM packaging, `.desktop` file, Linux CI. Plan 3 is not written.
- **Open owner decisions** (facts only, from the 2B "Open points"): when to run Task 6 and Task 9; whether `CopybookTests` joins `UnitTests.vcxproj`;
  licence headers on new files; the `Co-Authored-By` / DCO name; `PubSub.cpp:2007` passes `sizeof(tempCommand)` (32) for a 64-byte buffer, so queue
  manager names of 32 or more characters are now cut to 31; remaining UBSan findings in IBM's code (`Names.cpp`, `Copybook.cpp`, `findDelim`,
  `removeVarName`); filing the upstream issue draft (moot while this is a fork).
