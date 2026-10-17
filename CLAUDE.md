# CLAUDE.md (fork only, not part of any upstream change)

Linux Qt6 port of IBM RFHUtil. This file is for agents working in this fork; leave it out of anything filed upstream.

## Start here
1. Read `docs/CODEMAP.md` first (layout, core API, scripts, build, policies, "where do I look for").
2. Use `tools/index/q` before reading source. It refreshes itself and returns pointers, never whole files:
   - `q where TOPIC`: which doc section or file covers a topic
   - `q sym NAME`, `q outline PATH`: definitions and a file's table of contents, with line ranges
   - `q search WORDS...`: ranked search (`getParsedData` is found by `parsed data`); `--in GLOB`, `--kind code|test|doc`
   - `q refs NAME`: exact word uses (ripgrep, not semantic); `q tests-for NAME`: tests whose body mentions NAME
   - `q read PATH START END`: exactly those lines (max 400 without `--force`)
3. Read only the returned ranges, not whole files. `RFHUtil/DataArea.cpp` is 25,800 lines: never open it whole.
4. Before changing code, still read the exact lines you will change (`q read`). An index line is a pointer, not the code.
5. If `q` prints `[index may be stale ...]`, run without `--no-refresh` or `q rebuild`.

## Rules
- `git commit -s`, Conventional Commit subjects, and end each message with the two trailer lines you were given
  (`Co-Authored-By: ...` and `Claude-Session: ...`). **Never push.** Do not install anything (dnf, pip, brew, npm) without asking.
- Build and test: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`
  (expect 614 tests, 8 skipped). `ctest -L memcheck` runs the parsers under the guard allocator.
- Windows-only files (MFC, `DataArea.cpp`, `.vcxproj`) cannot be compiled here. Say what is unverified.

## Policies (details and reasons: CODEMAP section 7)
- Memory-safety bugs in IBM's shared sources are fixed with a test, through a `tools/port/*.pl` script.
- IBM behaviour bugs are pinned by a test, not changed.
- Output for valid input stays byte-identical to IBM's.
- Big-endian MQMD ids are not reversed (`translateMQMD`); this differs from Windows on purpose.
- Windows changes wait for the owner's Windows build (Plan 2A Task 6, Plan 2B Task 9); do not run those scripts.
- Edit IBM's files with the scripts in `tools/port/`, not by hand; no `#ifdef _WIN32` in shared sources (use `core/platform/`).
- `core/`: tabs, Allman braces, `Result` not exceptions. `tests/`: four spaces.
- `DataArea.cpp` still holds its own copies of the view code until Task 9; the core uses `DataFormatter.cpp`.
- MQ tests may create and delete only `RFHUTIL.TEST.*` queues and must leave none behind.
