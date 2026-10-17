# RFHUtil Linux Port, Plan 2B: Data Tab Views Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce the Windows Data tab's eight views of a message body (character, hex, both, XML, parsed, COBOL, JSON and fixed) in the portable core, with the same text Windows shows, from one implementation that the Windows application shares.

**Architecture:**

- `tools/port/port-formatters.pl` copies the formatters out of `RFHUtil/DataArea.cpp` into a new shared source, `RFHUtil/DataFormatter.{h,cpp}`. The class keeps `DataArea`'s member names, so the method bodies are IBM's own apart from a short, counted list of mechanical rewrites.
- `core/format/rfh_format.{h,cpp}` wraps it: `format::render(message, view, options, out)` gives a view's text, and `format::defaultsFor(...)` gives the Data tab settings Windows starts with after a get or a file read.
- IBM's COBOL copy book parser (`RFHUtil/Copybook.cpp`) becomes a shared source for the COBOL view.
- A guard-page allocator makes buffer overruns fault in tests, and a seeded fuzz test runs every view under it. It found memory bugs in the formatters and in IBM's shared JSON and XML helpers; Tasks 3 and 4 fix them.
- Task 9 (Windows, waiting for the owner's build) makes `DataArea` show its views through `DataFormatter` and removes its own copies, so there is one implementation.

**Tech Stack:** C++17, CMake, GoogleTest 1.17, iconv, Perl 5 (one-off edit scripts), IBM MQ conversion tables (optional).

**Spec:** `docs/superpowers/specs/2026-10-15-linux-qt-port-design.md` (section 4, `format::render`). **Previous plans:** Plan 1 (portable core, complete) and Plan 2A (`docs/superpowers/plans/2026-10-16-linux-port-plan-2a-message-files.md`, complete at `0e065a2` apart from its Task 6, which waits for the owner's Windows build).

**Owner decisions this plan follows:**
- Plan 2 was split: 2A did message files, the MQMD and headers; 2B, this plan, does the eight display formatters.
- Upstream behaviour bugs are pinned by tests and reported, not fixed.
- Memory-safety bugs in IBM's shared JSON and XML helpers are fixed and reported upstream (the owner's choice for this plan: "Fix them, report upstream"). They crash, hang or overrun memory on malformed messages, on Windows too, so a test cannot pin them.
- Windows changes that cannot be compiled here wait for the owner's Windows build (Task 9).

**Verified before writing:** every code block, script, edit and expected count in Tasks 1–9 was applied in order to a fresh export of the branch at `0e065a2` on Fedora 44 and built and tested after each task (`ctest`: 469, 478, 486, 490, 507, 518, 523, 525 and 525 tests, all passing, with the MQ client test skipped; these are the first-pass counts, and review changed them, see the Execution notes). Each failing-test step was checked to fail as described, and each memory fix was checked by putting IBM's original code back and seeing the memcheck test fault. Task 9 edits Windows files only: its script was run, the projects it edits pass `xmllint`, and `DataArea.cpp` keeps balanced braces and no references to what it removes; it has not been compiled.

**Code blocks are the first-pass versions.** Review then changed much of the work (the Execution notes list the commits). Where a block disagrees with the repository, the commits are authoritative; the "Superseded by review" paragraphs under Tasks 5, 7 and 8 name the differences that a reader would trip over. The API as built: `format::{View, DataFormats, RenderOptions (with apply(const DataFormats &)), defaultsFor, render}`, where both `defaultsFor` overloads return `DataFormats`, `DisplayCodepages` lives in `core/platform/rfh_codepage.h`, and `render` keeps its `DataFormatter` on the heap.

---

## Background the engineer needs

- **How Windows shows a view.** The Data tab (`MSGDATA::updateMsgdata`) calls one `DataArea::get...Data` method for the selected view and shows the buffer it fills (`m_data_ascii`, `m_data_hex`, ...) in an edit control.
  - The input is `DataArea::fileData`/`fileSize`: the message body after the MQ headers, which `parseMsgHeaders` removes.
  - `fileCcsid` is the MQMD's code page, even when an RFH2 names another one for the body. `m_char_format` (ASCII, EBCDIC, Japanese, ...) and the integer encoding come from the last header. This is Windows' behaviour and is kept.
  - The user's choices (CR LF, EDI, byte order mark, indent, and the character format radio buttons) are parameters of the get methods.
  - Each view is cached until the message changes (`resetDisplayData`).
- **The output is in the Windows ANSI code page**, not UTF-8: the formatters translate EBCDIC and DBCS data to `app->ansi` (Windows `GetACP()`). On Linux the core uses `rfh_get_acp()` from Plan 1: 1252 unless `RFHUTIL_ACP` says otherwise. A UI decodes the text from that code page.
- **IBM disabled the Unicode character view.** `getCharacterData` has `if (0)` where it would convert UCS-2, and the call to `getCharDataUcs` is commented out, so Windows shows UTF-16 and UTF-8 bodies byte by byte. That is kept (and `getCharDataUcs`, which is dead, is not moved).
- **MQ's conversion tables.** For EBCDIC code pages that Windows cannot convert, `DataArea` reads MQ's 512-byte tables (`<MQ install>\conv\table\XXXX34B0.tbl`, EBCDIC to UCS-2). Linux MQ ships the same tables in `/opt/mqm/lib/iconv`. `DataFormatter::conversionTableDir` names the directory; empty means no tables.
- **Upstream bugs found while porting.** All are reported in the upstream issue (Task 10).
  - Behaviour, kept (the first four are reported upstream):
    - `MQEBC2ASC` never uses its table: it sets `MQCcsidOut = 0` before comparing it with the requested code page.
    - `xlateEbcdicChar`'s MQ fallback converts the character back to the source EBCDIC code page instead of the target.
    - `CJsonParse` keeps only the last element of an array and attaches the following text to the next name.
    - `DataArea`'s fixed view never builds its display table, so it relies on the character or both view having been shown first. This is a documented difference, not a pinned bug: `DataFormatter` builds the table in its constructor, so the fixed view works on its own (`DataFormatter.FixedViewHasADisplayTableWithoutACharacterViewFirst`).
  - Memory, fixed:
    - The both view allocates 4 bytes per data byte plus 65, but needs 65 per 16. For messages over 1040 bytes it writes past its buffer.
    - The fixed view indexes its display table with a signed `char`, so translated bytes of 0x80 and above read before the table.
    - The EDI option of the character view skips blanks without checking for the end of the data.
    - `xmlsubs.cpp` compares up to 18 bytes ahead without checking the remaining length (about 30 places).
    - `extractAttribute` copies an attribute value until its closing quote, with no check for the end of the text, into 8 KB buffers that a 16 KB attribute list can overflow.
    - `CJsonParse` sizes its tree from a count of braces, colons and commas that malformed data can exceed. It loops forever when a pass consumes nothing (`{]` is enough) and recurses once per level of nesting.
- **Shared sources.** Files under `RFHUtil/` that the core compiles are listed in `core/CMakeLists.txt` (`RFHUTIL_SHARED_SOURCES`). On Linux they get `core/compat/stdafx.h`, a prelude that blocks `rfhutil.h`, and `-fsigned-char` (MSVC's `char` is signed). They keep their historic warnings. Windows compiles the same files in `RFHUtil.vcxproj` and `Client/Client.vcxproj`. Windows-only calls are replaced with `rfh_*` functions from `core/platform`, which call the Win32 API on Windows and iconv or POSIX on Linux.
- **Edit scripts.** The changes to IBM's files are made by one-off Perl scripts in `tools/port/`, like Plan 1's and 2A's. Every edit asserts how many times it matched, every file is checked before any is written, and a second run fails without changing anything.
- **Memory checking.** `libasan` is not installed and the build does not need it. Task 1 adds a small guard-page allocator, loaded with `LD_PRELOAD`: each block ends at a page boundary followed by an inaccessible page, so any overrun faults at once. `ctest -L memcheck` runs the parsing suites under it.
- **Conventions.**
  - Core code uses tabs and Allman braces. Tests use four spaces.
  - Commits use `git commit -s`, a Conventional Commit subject, and end with the two trailer lines shown in each task.
  - Do not push; the owner pushes.
  - Queue rules from 2A still apply, but this plan needs no queue manager.

## File structure

| Path | Status | Responsibility |
|---|---|---|
| `tests/tools/guardmalloc.cpp`, `tests/core/GuardMallocTests.cpp` | new | Guard-page allocator for the memcheck tests, and its self-test |
| `core/platform/rfh_platform.{h,cpp}`, `rfh_codepage.{h,cpp}` | modified | `UINT`, `_snprintf_s`, `RFH_DIR_SEP`, `rfh_WideCharToMultiByte`, `rfh_MultiByteToWideChar`, `rfh_mbclen` |
| `RFHUtil/JsonParse.{h,cpp}`, `RFHUtil/xmlsubs.cpp` | modified (by script) | Stay within their buffers on malformed data |
| `RFHUtil/DataFormatter.h`, `RFHUtil/DataFormatter.cpp` | new (prelude by hand, methods by script) | The formatters, shared by Windows and the core |
| `RFHUtil/Copybook.{h,cpp}` | modified (by script) | IBM's copy book parser becomes a shared source |
| `core/format/rfh_format.{h,cpp}` | new | `format::render`, `format::defaultsFor` |
| `RFHUtil/DataArea.{h,cpp}`, the two `.vcxproj` and `.vcxproj.filters` | modified (by script, Task 9) | `DataArea` shows its views through `DataFormatter` |
| `tools/port/port-parser-fixes.pl`, `port-formatters.pl`, `port-copybook.pl`, `port-dataarea-formatters.pl` | new | One-off scripted edits |
| `tests/core/DataFormatterTests.cpp`, `FormatTests.cpp`, `FormatFuzzTests.cpp` | new | Tests of the views |
| `tests/core/PlatformTests.cpp`, `tests/UnitTests/JsonParseTests.cpp`, `XmlsubsTests.cpp` | modified | New tests; the Windows UnitTests project runs these three files too |
| `core/CMakeLists.txt`, `tests/CMakeLists.txt` | modified | New sources, tests and memcheck tests |

---
### Task 1: A guard-page allocator for memory-checked tests

**Files:**
- Create: `tests/tools/guardmalloc.cpp`
- Modify: `tests/CMakeLists.txt`
- Test: `tests/core/GuardMallocTests.cpp`

The later tasks fix buffer overruns that often pass by luck under the normal allocator. This allocator makes them fault, so each fix has a test that fails without it.

- [ ] **Step 1: Write the self-test**

Create `tests/core/GuardMallocTests.cpp`:

```cpp
// GuardMallocTests.cpp - checks that the memcheck tests really are guarded.
//
// Run normally, these tests skip. The memcheck run (LD_PRELOAD of the guard
// allocator, see tests/CMakeLists.txt) proves an overrun faults, so a memcheck
// test that passes means the code under test stayed within its buffers.

#include <gtest/gtest.h>

#include <stdlib.h>
#include <string.h>

namespace
{
    bool guarded()
    {
        const char * preload = getenv("LD_PRELOAD");
        return (preload != NULL) && (strstr(preload, "guardmalloc") != NULL);
    }
}

TEST(GuardMalloc, WritePastTheEndFaults)
{
    if (!guarded())
    {
        GTEST_SKIP() << "runs only under the guard allocator (ctest -L memcheck)";
    }
    EXPECT_DEATH({
        volatile char * p = (volatile char *)malloc(16);
        p[16] = 'x';
    }, "");
}

TEST(GuardMalloc, ReadPastTheEndFaults)
{
    if (!guarded())
    {
        GTEST_SKIP() << "runs only under the guard allocator (ctest -L memcheck)";
    }
    EXPECT_DEATH({
        volatile char * p = (volatile char *)malloc(32);
        volatile char c = p[32];
        (void)c;
    }, "");
}

TEST(GuardMalloc, BlocksWithinTheirSizeAreUsable)
{
    char * p = (char *)malloc(4096);
    ASSERT_NE(nullptr, p);
    memset(p, 'a', 4096);
    char * q = (char *)realloc(p, 8192);
    ASSERT_NE(nullptr, q);
    EXPECT_EQ('a', q[4095]);
    free(q);

    int * zeros = (int *)calloc(100, sizeof(int));
    ASSERT_NE(nullptr, zeros);
    EXPECT_EQ(0, zeros[99]);
    free(zeros);
}
```

Add it to the core test executable.

In `tests/CMakeLists.txt`, replace:

```cmake
core/HeaderChainTests.cpp core/MessageFileTests.cpp)
```

with:

```cmake
core/HeaderChainTests.cpp core/MessageFileTests.cpp
    core/GuardMallocTests.cpp)
```

- [ ] **Step 2: Run it without the allocator**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ./build/linux-debug/tests/rfhutil_core_tests --gtest_filter='GuardMalloc.*'`
Expected: the two fault tests are SKIPPED ("runs only under the guard allocator") and `BlocksWithinTheirSizeAreUsable` passes. Nothing yet runs them guarded.

- [ ] **Step 3: Add the allocator and the memcheck tests**

Create `tests/tools/guardmalloc.cpp`:

```cpp
// guardmalloc.cpp - a guard-page allocator for tests, loaded with LD_PRELOAD.
//
// Every heap block ends at a page boundary and is followed by an inaccessible
// page, so reading or writing past the end of a block faults at once instead of
// corrupting a neighbour. Freed blocks are unmapped, so a use after free faults
// too (until the address is reused). Blocks are 16-byte aligned, which leaves up
// to 15 bytes after a size that is not a multiple of 16 unguarded.
//
// The memcheck tests in tests/CMakeLists.txt run the suites that parse message
// data under it. It is slow and uses a page or more per allocation, so it is for
// tests only.

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

namespace
{
    struct Header
    {
        char * base;        // start of the mapping
        size_t mapLength;   // length of the mapping, including the guard page
        size_t size;        // size the caller asked for
    };

    size_t pageSize()
    {
        static size_t page = 0;
        if (page == 0)
        {
            page = (size_t)sysconf(_SC_PAGESIZE);
        }
        return page;
    }

    Header * headerOf(void * p)
    {
        return (Header *)((char *)p - sizeof(Header));
    }

    void * allocate(size_t size, size_t alignment)
    {
        if (alignment < 16)
        {
            alignment = 16;
        }
        size_t usable = (size + 15) & ~(size_t)15;
        if (usable == 0)
        {
            usable = 16;
        }
        size_t page = pageSize();
        size_t dataPages = (usable + sizeof(Header) + alignment + page - 1) / page;
        size_t mapLength = (dataPages + 1) * page;
        char * base = (char *)mmap(NULL, mapLength, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED)
        {
            errno = ENOMEM;
            return NULL;
        }
        char * guard = base + dataPages * page;
        mprotect(guard, page, PROT_NONE);

        uintptr_t block = ((uintptr_t)guard - usable) & ~(uintptr_t)(alignment - 1);
        Header * header = headerOf((void *)block);
        header->base = base;
        header->mapLength = mapLength;
        header->size = size;
        return (void *)block;
    }
}

extern "C"
{
    void * malloc(size_t size)
    {
        return allocate(size, 16);
    }

    void free(void * p)
    {
        if (p != NULL)
        {
            Header * header = headerOf(p);
            munmap(header->base, header->mapLength);
        }
    }

    void * calloc(size_t count, size_t size)
    {
        if ((size != 0) && (count > SIZE_MAX / size))
        {
            errno = ENOMEM;
            return NULL;
        }
        return allocate(count * size, 16);      // mmap memory is already zero
    }

    void * realloc(void * p, size_t size)
    {
        if (p == NULL)
        {
            return malloc(size);
        }
        void * q = malloc(size);
        if (q != NULL)
        {
            size_t old = headerOf(p)->size;
            memcpy(q, p, (old < size) ? old : size);
            free(p);
        }
        return q;
    }

    void * memalign(size_t alignment, size_t size)
    {
        return allocate(size, alignment);
    }

    void * aligned_alloc(size_t alignment, size_t size)
    {
        return allocate(size, alignment);
    }

    int posix_memalign(void ** result, size_t alignment, size_t size)
    {
        *result = allocate(size, alignment);
        return (*result != NULL) ? 0 : ENOMEM;
    }

    void * valloc(size_t size)
    {
        return allocate(size, pageSize());
    }

    size_t malloc_usable_size(void * p)
    {
        return (p != NULL) ? headerOf(p)->size : 0;
    }
}
```

Append to `tests/CMakeLists.txt`:

```cmake

# Memory checking. The guard-page allocator (tools/guardmalloc.cpp) makes a heap
# overrun fault at once rather than corrupt memory silently. The memcheck tests
# (ctest -L memcheck) run the suites that parse message data under it.
add_library(rfh_guardmalloc SHARED tools/guardmalloc.cpp)

# rfh_add_memcheck_test(name target filter [VAR=value ...]) runs the tests that
# match filter under the guard allocator, with any extra environment given.
function(rfh_add_memcheck_test name target filter)
    add_test(NAME ${name} COMMAND ${target} --gtest_filter=${filter})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:rfh_guardmalloc>;${ARGN}"
        LABELS memcheck
        TIMEOUT 300)
    add_dependencies(${target} rfh_guardmalloc)
endfunction()

rfh_add_memcheck_test(memcheck.GuardMalloc rfhutil_core_tests "GuardMalloc.*")
```

- [ ] **Step 4: Run the tests**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug -L memcheck -V | grep -E "\[  PASSED|SKIPPED|tests passed"`
Expected: `memcheck.GuardMalloc` passes with `[  PASSED  ] 3 tests.`: under the allocator both fault tests ran and died as they should.
Run: `ctest --preset linux-debug`
Expected: `100% tests passed, 0 tests failed out of 469` (465 before, plus 3 tests and `memcheck.GuardMalloc`; the client test is skipped as before).

- [ ] **Step 5: Commit**

```bash
git add tests/tools/guardmalloc.cpp tests/core/GuardMallocTests.cpp tests/CMakeLists.txt
git commit -s -m "test(core): add a guard-page allocator for memory-checked tests

tests/tools/guardmalloc.cpp, loaded with LD_PRELOAD, ends every heap block
at a page boundary followed by an inaccessible page, so an overrun faults at
once. ctest -L memcheck runs chosen suites under it; GuardMallocTests proves
it is active there.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JfjP6hazi7FDx4h8gULaS5"
```

---

### Task 2: The Win32 text conversions the formatters use

**Files:**
- Modify: `core/platform/rfh_platform.h`, `core/platform/rfh_platform.cpp`, `core/platform/rfh_codepage.h`, `core/platform/rfh_codepage.cpp`
- Test: `tests/core/PlatformTests.cpp` (also built by the Windows UnitTests project)

The formatters call `WideCharToMultiByte` and `MultiByteToWideChar` on 16-bit UCS-2 (`wchar_t` is 32 bits on Linux), `_snprintf_s` with `_TRUNCATE`, and the copy book parser calls `_mbclen`. Windows keeps the Win32 calls; Linux reproduces them with iconv, reusing Plan 1's CCSID-to-iconv mapping.

- [ ] **Step 1: Write the failing tests**

In `tests/core/PlatformTests.cpp`, replace:

```cpp
#include <stdlib.h>
```

with:

```cpp
#include <stdlib.h>
#include <string.h>
```

Append to `tests/core/PlatformTests.cpp`:

```cpp

// WideCharToMultiByte / MultiByteToWideChar for 16-bit UCS-2 (the formatters' calls)

TEST(Platform_Codepage, WideToAnsiConvertsEachCodeUnit)
{
    const RFH_UCS2 in[] = { 'c', 'a', 'f', 0x00E9 };
    char out[8] = { 0 };
    EXPECT_EQ(4, rfh_WideCharToMultiByte(1252, 0, in, 4, out, sizeof(out), ".", NULL));
    EXPECT_EQ(0, memcmp(out, "caf\xe9", 4));
}

TEST(Platform_Codepage, WideToAnsiUsesTheDefaultCharacter)
{
    const RFH_UCS2 in[] = { 'a', 0x4E00, 'b' };    // a CJK ideograph has no 1252 form
    char out[8] = { 0 };
    BOOL usedDefault = FALSE;
    EXPECT_EQ(3, rfh_WideCharToMultiByte(1252, WC_DEFAULTCHAR, in, 3, out, sizeof(out), ".", &usedDefault));
    EXPECT_EQ(0, memcmp(out, "a.b", 3));
    EXPECT_TRUE(usedDefault);
}

TEST(Platform_Codepage, WideToAnsiThatDoesNotFitConvertsNothing)
{
    const RFH_UCS2 in[] = { 'a', 'b', 'c' };
    char out[2] = { 0 };
    EXPECT_EQ(0, rfh_WideCharToMultiByte(1252, 0, in, 3, out, sizeof(out), ".", NULL));
}

TEST(Platform_Codepage, AnsiToWideConvertsEachByte)
{
    RFH_UCS2 out[4] = { 0 };
    EXPECT_EQ(2, rfh_MultiByteToWideChar(1252, 0, "\xe9\x80", 2, out, 4));
    EXPECT_EQ(0x00E9, out[0]);
    EXPECT_EQ(0x20AC, out[1]);      // 0x80 is the euro sign in 1252
}

TEST(Platform_Codepage, EbcdicToWide)
{
    REQUIRE_CODEPAGE(500);
    RFH_UCS2 out[2] = { 0 };
    EXPECT_EQ(1, rfh_MultiByteToWideChar(500, 0, "\xc1", 1, out, 2));
    EXPECT_EQ((RFH_UCS2)'A', out[0]);
}

TEST(Platform_Codepage, WideConversionsWithAMissingCodePageConvertNothing)
{
    const RFH_UCS2 in[] = { 'a' };
    char out[4] = { 0 };
    RFH_UCS2 wide[4] = { 0 };
    EXPECT_EQ(0, rfh_WideCharToMultiByte(99999, 0, in, 1, out, sizeof(out), ".", NULL));
    EXPECT_EQ(0, rfh_MultiByteToWideChar(99999, 0, "a", 1, wide, 4));
}

TEST(Platform_Codepage, MbclenIsOneInASingleByteAnsiCodePage)
{
    if (rfh_get_acp() != 1252)
    {
        GTEST_SKIP() << "needs ANSI code page 1252";
    }
    const unsigned char text[] = { 0x81, 0x40, 0 };
    EXPECT_EQ(1, rfh_mbclen(text));
}

#ifndef _WIN32
TEST(Platform_Codepage, MbclenRecognisesShiftJisLeadBytes)
{
    const char * old = getenv("RFHUTIL_ACP");
    std::string saved = old ? old : "";
    bool had = (old != nullptr);

    setenv("RFHUTIL_ACP", "932", 1);
    const unsigned char lead[] = { 0x81, 0x40, 0 };
    const unsigned char single[] = { 'a', 0 };
    EXPECT_EQ(2, rfh_mbclen(lead));
    EXPECT_EQ(1, rfh_mbclen(single));

    if (had) setenv("RFHUTIL_ACP", saved.c_str(), 1); else unsetenv("RFHUTIL_ACP");
}
#endif

TEST(Platform_Snprintf, TruncateModeTerminatesAndReportsTruncation)
{
    char buf[6];
    EXPECT_EQ(-1, _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s", "abcdefgh"));
    EXPECT_STREQ("abcde", buf);
    EXPECT_EQ(3, _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d", 123));
    EXPECT_STREQ("123", buf);
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug`
Expected: the build fails with `error: 'rfh_WideCharToMultiByte' was not declared in this scope` (and the same for `rfh_MultiByteToWideChar`, `WC_DEFAULTCHAR` and the others).

- [ ] **Step 3: Add the declarations and `_snprintf_s`**

In `core/platform/rfh_platform.h`, replace:

```cpp
typedef const char * LPCTSTR;
typedef char16_t RFH_UCS2;		// wchar_t is 32 bits on Linux; MQ UCS-2 data is 16 bits
#endif
```

with:

```cpp
typedef const char * LPCTSTR;
typedef unsigned int UINT;
typedef char16_t RFH_UCS2;		// wchar_t is 32 bits on Linux; MQ UCS-2 data is 16 bits

// MSVC _snprintf_s with count _TRUNCATE: writes at most sizeOfBuffer - 1 characters
// and a terminator, and returns -1 if the output was truncated. _TRUNCATE is the
// only count supported.
#define _TRUNCATE ((size_t)-1)
int _snprintf_s(char * buffer, size_t sizeOfBuffer, size_t count, const char * format, ...)
	__attribute__((format(printf, 4, 5)));
#endif

// Directory separator for paths that the shared sources build.
#ifdef _WIN32
#define RFH_DIR_SEP "\\"
#else
#define RFH_DIR_SEP "/"
#endif
```

In `core/platform/rfh_platform.cpp`, replace:

```cpp
#include <ctype.h>
#include <stdio.h>
```

with:

```cpp
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
```

Append to `core/platform/rfh_platform.cpp`:

```cpp

#ifndef _WIN32
int _snprintf_s(char * buffer, size_t sizeOfBuffer, size_t count, const char * format, ...)
{
	if ((buffer == NULL) || (sizeOfBuffer == 0) || (count != _TRUNCATE))
	{
		return -1;
	}

	va_list args;
	va_start(args, format);
	int needed = vsnprintf(buffer, sizeOfBuffer, format, args);
	va_end(args);

	if ((needed < 0) || ((size_t)needed >= sizeOfBuffer))
	{
		return -1;
	}
	return needed;
}
#endif
```

Append to `core/platform/rfh_codepage.h`:

```cpp

#ifndef _WIN32
// Win32 flag values. Linux accepts both and ignores them: iconv produces
// precomposed characters.
#define WC_DISCARDNS	0x0010
#define WC_DEFAULTCHAR	0x0040
#endif

// WideCharToMultiByte for UCS-2 in host byte order. Converts inChars code units
// (inChars >= 0) to codepage and returns the bytes written, or 0 when the code page
// is not available or the output does not fit in outBytes (Windows returns 0 with
// ERROR_INSUFFICIENT_BUFFER). There is no size-query mode: outBytes must be
// positive. On Linux a character the code page cannot represent becomes
// defaultChar[0] ('?' when defaultChar is NULL) and sets *usedDefaultChar when that
// is non-NULL; Windows may choose a "best fit" character instead.
int rfh_WideCharToMultiByte(int codepage, unsigned int flags, const RFH_UCS2 * in, int inChars,
	char * out, int outBytes, const char * defaultChar, BOOL * usedDefaultChar);

// MultiByteToWideChar to UCS-2 in host byte order. Converts inBytes bytes
// (inBytes >= 0) and returns the code units written, or 0 when the code page is not
// available or the output does not fit in outChars. flags must be 0. On Linux an
// incomplete or invalid sequence ends the conversion, and the code units converted
// before it are returned.
int rfh_MultiByteToWideChar(int codepage, unsigned int flags, const char * in, int inBytes,
	RFH_UCS2 * out, int outChars);

// _mbclen: the length (1 or 2) of the multibyte character at text in the ANSI code
// page. Linux: 2 for a lead byte of a double-byte ANSI code page (932, 936, 949 or
// 950), otherwise 1.
int rfh_mbclen(const unsigned char * text);
```

- [ ] **Step 4: Implement the conversions**

All the edits below are in `core/platform/rfh_codepage.cpp`: the Windows versions, then the Linux key for host-order UCS-2 in the iconv converter cache, then the Linux versions before the final `#endif`.

In `core/platform/rfh_codepage.cpp`, replace:

```cpp
#ifdef _WIN32
```

with:

```cpp
#ifdef _WIN32
#include <mbstring.h>
```

In `core/platform/rfh_codepage.cpp`, replace:

```cpp
	return (int)mbstowcs(ucs, mbs, ucsChars);
}
```

with:

```cpp
	return (int)mbstowcs(ucs, mbs, ucsChars);
}

int rfh_WideCharToMultiByte(int codepage, unsigned int flags, const RFH_UCS2 * in, int inChars,
	char * out, int outBytes, const char * defaultChar, BOOL * usedDefaultChar)
{
	if ((inChars < 0) || (outBytes <= 0))
	{
		return 0;
	}
	return WideCharToMultiByte((UINT)codepage, flags, in, inChars, out, outBytes, defaultChar, usedDefaultChar);
}

int rfh_MultiByteToWideChar(int codepage, unsigned int flags, const char * in, int inBytes,
	RFH_UCS2 * out, int outChars)
{
	if ((inBytes < 0) || (outChars <= 0))
	{
		return 0;
	}
	return MultiByteToWideChar((UINT)codepage, flags, in, inBytes, out, outChars);
}

int rfh_mbclen(const unsigned char * text)
{
	return (int)_mbclen(text);
}
```

In `core/platform/rfh_codepage.cpp`, replace:

```cpp
namespace
{
	// iconv names to try for an IBM CCSID, most specific first.
```

with:

```cpp
namespace
{
	// converterFor key for UCS-2 in host byte order; CCSIDs are positive
	const int kHostUcs2 = -1;

	// iconv names to try for an IBM CCSID, most specific first.
```

In `core/platform/rfh_codepage.cpp`, replace:

```cpp
		std::vector<std::string> names;
		auto it = specific.find(ccsid);
```

with:

```cpp
		std::vector<std::string> names;
		if (ccsid == kHostUcs2)
		{
			names.push_back((__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) ? "UTF-16LE" : "UTF-16BE");
			return names;
		}
		auto it = specific.find(ccsid);
```

In `core/platform/rfh_codepage.cpp`, replace:

```cpp
	return count;
}

#endif
```

with:

```cpp
	return count;
}

int rfh_WideCharToMultiByte(int codepage, unsigned int flags, const RFH_UCS2 * in, int inChars,
	char * out, int outBytes, const char * defaultChar, BOOL * usedDefaultChar)
{
	(void)flags;
	if ((codepage <= 0) || (inChars < 0) || (outBytes <= 0))
	{
		return 0;
	}
	iconv_t cd = converterFor(kHostUcs2, codepage);
	if (cd == (iconv_t)-1)
	{
		return 0;
	}
	iconv(cd, NULL, NULL, NULL, NULL);

	// one code unit at a time, so that each unrepresentable one becomes the default
	char * outPtr = out;
	size_t outLeft = (size_t)outBytes;
	for (int i = 0; i < inChars; i++)
	{
		RFH_UCS2 unit = in[i];
		char * inPtr = (char *)&unit;
		size_t inLeft = sizeof(unit);
		if (iconv(cd, &inPtr, &inLeft, &outPtr, &outLeft) == (size_t)-1)
		{
			if (errno == E2BIG)
			{
				return 0;
			}
			// EILSEQ: not representable; EINVAL: half of a surrogate pair
			iconv(cd, NULL, NULL, NULL, NULL);
			if (outLeft == 0)
			{
				return 0;
			}
			*outPtr++ = (defaultChar != NULL) ? defaultChar[0] : '?';
			outLeft--;
			if (usedDefaultChar != NULL)
			{
				*usedDefaultChar = TRUE;
			}
		}
	}
	return (int)((size_t)outBytes - outLeft);
}

int rfh_MultiByteToWideChar(int codepage, unsigned int flags, const char * in, int inBytes,
	RFH_UCS2 * out, int outChars)
{
	if ((codepage <= 0) || (flags != 0) || (inBytes < 0) || (outChars <= 0))
	{
		return 0;
	}
	iconv_t cd = converterFor(codepage, kHostUcs2);
	if (cd == (iconv_t)-1)
	{
		return 0;
	}
	iconv(cd, NULL, NULL, NULL, NULL);

	char * inPtr = (char *)in;
	size_t inLeft = (size_t)inBytes;
	char * outPtr = (char *)out;
	size_t outLeft = (size_t)outChars * sizeof(RFH_UCS2);
	if ((iconv(cd, &inPtr, &inLeft, &outPtr, &outLeft) == (size_t)-1) && (errno == E2BIG))
	{
		return 0;
	}
	return (int)(((size_t)outChars * sizeof(RFH_UCS2) - outLeft) / sizeof(RFH_UCS2));
}

int rfh_mbclen(const unsigned char * text)
{
	unsigned char ch = text[0];
	switch (rfh_get_acp())
	{
	case 932:	// Shift-JIS
		return (((ch >= 0x81) && (ch <= 0x9F)) || ((ch >= 0xE0) && (ch <= 0xFC))) ? 2 : 1;
	case 936:	// GBK
	case 949:	// Unified Hangul
	case 950:	// Big5
		return ((ch >= 0x81) && (ch <= 0xFE)) ? 2 : 1;
	default:
		return 1;
	}
}

#endif
```

- [ ] **Step 5: Run the tests**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`
Expected: `100% tests passed, 0 tests failed out of 478` (9 new tests).

- [ ] **Step 6: Commit**

```bash
git add core/platform/rfh_platform.h core/platform/rfh_platform.cpp core/platform/rfh_codepage.h core/platform/rfh_codepage.cpp tests/core/PlatformTests.cpp
git commit -s -m "feat(core): add the Win32 text conversions the formatters use

rfh_WideCharToMultiByte and rfh_MultiByteToWideChar convert 16-bit UCS-2
(wchar_t is 32 bits on Linux) with Windows' results; Linux uses iconv
through Plan 1's CCSID mapping and replaces an unrepresentable character
with the default character. Also UINT, _snprintf_s with _TRUNCATE,
RFH_DIR_SEP and rfh_mbclen.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JfjP6hazi7FDx4h8gULaS5"
```

---

### Task 3: Keep IBM's JSON and XML helpers within their buffers

**Files:**
- Create: `tools/port/port-parser-fixes.pl`
- Modify (by the script): `RFHUtil/JsonParse.h`, `RFHUtil/JsonParse.cpp`, `RFHUtil/xmlsubs.cpp`
- Modify: `tests/CMakeLists.txt`
- Test: `tests/UnitTests/JsonParseTests.cpp`, `tests/UnitTests/XmlsubsTests.cpp` (also built by the Windows UnitTests project)

These are IBM's shared sources, also used by the Windows build. The fixes change nothing for well-formed data. For malformed data, a JSON parse that would hang or overrun returns `JPARSE_MALFORMED` or `JPARSE_TOO_DEEP`, and the JSON view then shows Windows' usual "Error parsing user data". An unterminated XML attribute value ends at the end of the text.

- [ ] **Step 1: Write the XML tests and the memcheck test**

In `tests/UnitTests/XmlsubsTests.cpp`, replace:

```cpp
#include "xmlsubs.h"
```

with:

```cpp
#include "xmlsubs.h"

#include <string>
#include <vector>
```

Append to `tests/UnitTests/XmlsubsTests.cpp`:

```cpp

// ---------------------------------------------------------------------------
// processAttributes on malformed data (Plan 2B: these overran memory before;
// ctest -L memcheck runs them under the guard allocator)
// ---------------------------------------------------------------------------

TEST(XmlsubsAttributes, ValuesAreListedUnderTheElement)
{
    char element[] = "root";
    char varName[] = "";
    char attrs[] = "a=\"1\" b='two'";
    char out[256] = "";
    processAttributes(element, varName, attrs, out);
    EXPECT_STREQ("root.(XML.attr)a='1'\r\nroot.(XML.attr)b='two'\r\n", out);
}

TEST(XmlsubsAttributes, UnterminatedQuoteEndsAtTheEndOfTheText)
{
    char element[] = "root";
    char varName[] = "";
    char attrs[] = "a=\"open";
    char out[256] = "";
    processAttributes(element, varName, attrs, out);
    EXPECT_STREQ("root.(XML.attr)a='open'\r\n", out);
}

TEST(XmlsubsAttributes, ValueLongerThan8KIsKept)
{
    char element[] = "r";
    char varName[] = "";
    std::string value(12000, 'v');
    std::string attrs = "a='" + value + "'";
    std::vector<char> out(20000, 0);
    processAttributes(element, varName, &attrs[0], out.data());
    EXPECT_EQ("r.(XML.attr)a='" + value + "'\r\n", std::string(out.data()));
}
```

Append to `tests/CMakeLists.txt`:

```cmake
rfh_add_memcheck_test(memcheck.Parsers rfhutil_unit_tests "JsonParse*:Xmlsubs*:XMLParse*")
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug -R memcheck.Parsers`
Expected: `memcheck.Parsers ... ***Exception: SegFault`. Run unguarded (`./build/linux-debug/tests/rfhutil_unit_tests --gtest_filter='XmlsubsAttributes.*'`), the process also dies after the first test.

- [ ] **Step 3: Write the JSON tests**

In `tests/UnitTests/JsonParseTests.cpp`, replace:

```cpp
#include "JsonParse.h"
```

with:

```cpp
#include "JsonParse.h"

#include <string>
```

Append to `tests/UnitTests/JsonParseTests.cpp`:

```cpp

// ---------------------------------------------------------------------------
// Malformed and deeply nested data (Plan 2B: these crashed, hung or overran
// memory before; ctest -L memcheck runs them under the guard allocator)
// ---------------------------------------------------------------------------

TEST(JsonParse, MisplacedBracketIsAnErrorNotAHang)
{
    const char *json = "{]";
    CJsonParse p;
    EXPECT_EQ(JPARSE_MALFORMED, p.parse(json, (int)strlen(json)));
}

TEST(JsonParse, DeepNestingIsAnErrorNotAStackOverflow)
{
    std::string json;
    for (int i = 0; i < 100000; i++)
    {
        json += "{\"k\":";
    }
    json += "1" + std::string(100000, '}');
    CJsonParse p;
    EXPECT_EQ(JPARSE_TOO_DEEP, p.parse(json.c_str(), (int)json.size()));
}

TEST(JsonParse, NestingUpToTheLimitParses)
{
    // levels of objects, each holding the next: {"k":{"k":...1}}
    auto nested = [](int levels) {
        std::string json;
        for (int i = 0; i < levels; i++)
        {
            json += "{\"k\":";
        }
        return json + "1" + std::string(levels, '}');
    };

    std::string atLimit = nested(JPARSE_MAX_DEPTH);
    CJsonParse p;
    EXPECT_EQ(JPARSE_OK, p.parse(atLimit.c_str(), (int)atLimit.size()));

    std::string over = nested(JPARSE_MAX_DEPTH + 1);
    CJsonParse q;
    EXPECT_EQ(JPARSE_TOO_DEEP, q.parse(over.c_str(), (int)over.size()));
}

TEST(JsonParse, NamesWithoutValuesDoNotOverrunTheTree)
{
    // parse() sizes the tree from the braces, colons and commas: 3 entries here,
    // but each bare name is an entry of its own
    const char *json = "{a b c d e f g h i j}";
    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json, (int)strlen(json)));

    std::string names;
    for (int child = p.getFirstChild(0); child > 0; child = p.getNextSibling(child))
    {
        names += p.getElemName(child);
    }
    EXPECT_EQ("abcdefghij", names);
}
```

- [ ] **Step 4: Run them to see them fail**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug`
Expected: the build fails with `error: 'JPARSE_MALFORMED' was not declared in this scope` (and `JPARSE_TOO_DEEP`, `JPARSE_MAX_DEPTH`).

- [ ] **Step 5: Add and run the script**

Create `tools/port/port-parser-fixes.pl`:

```perl
#!/usr/bin/perl
# One-off fix for Plan 2B: keeps IBM's shared JSON and XML helpers within their
# buffers on malformed data, which crashes or corrupts memory on Windows too (see
# the upstream report). Well-formed data gives exactly the same results as before.
#   RFHUtil/JsonParse.{h,cpp}: grow the parse tree instead of overrunning it, stop
#     at JPARSE_MAX_DEPTH levels of nesting, and stop when a pass of the parser
#     consumes nothing (for example "{]", which loops forever).
#   RFHUtil/xmlsubs.cpp: an attribute value without its closing quote ends at the
#     end of the text, and attribute names and values are sized from the text.
# Run once from the repository root.
use strict; use warnings;

# Every file is checked before any is written, so a failed run changes nothing.
my @pending;
sub edit {
	my ($path, $marker, @changes) = @_;
	open my $in, '<:raw', $path or die "$path: $!";
	my $text = do { local $/; <$in> }; close $in;
	die "$path: the fix is already applied\n" if index($text, $marker) >= 0;
	my $crlf = ($text =~ /\r\n/);
	$text =~ s/\r\n/\n/g;
	for my $c (@changes) {
		my ($what, $old, $new) = @$c;
		my $n = () = $text =~ /\Q$old\E/g;
		die "$path: $what: matched $n time(s), expected 1\n" unless $n == 1;
		$text =~ s/\Q$old\E/$new/;
	}
	$text =~ s/\n/\r\n/g if $crlf;
	push @pending, [$path, $text, scalar @changes];
}

edit('RFHUtil/JsonParse.h', 'JPARSE_TOO_DEEP',
	['result codes', "#define JPARSE_INVALID_START_CHAR\t200\n",
		"#define JPARSE_INVALID_START_CHAR\t200\n"
		. "#define JPARSE_TOO_DEEP\t\t\t\t201\t// objects and arrays nested deeper than JPARSE_MAX_DEPTH\n"
		. "#define JPARSE_MALFORMED\t\t\t202\t// the parser could not move past a character\n\n"
		. "// Each level of nesting recurses; this keeps well within a 1 MB Windows stack.\n"
		. "#define JPARSE_MAX_DEPTH\t\t\t256\n"],
	['members', "\tJSONSTRUCT * jsonElems;\n\tint numElements;\n",
		"\tJSONSTRUCT * jsonElems;\n\tint numElements;\n"
		. "\tint maxElements;\t\t\t// capacity of jsonElems\n"
		. "\tint depth;\t\t\t\t\t// current object and array nesting\n"
		. "\tint stopReason;\t\t\t\t// JPARSE_OK, or why parsing stopped early\n"],
);

my $guard = <<'G';
	// stop before deep nesting exhausts the stack (each level recurses)
	DepthGuard guard(depth);
	if (depth > JPARSE_MAX_DEPTH)
	{
		stopReason = JPARSE_TOO_DEEP;
		return endPtr;
	}

G
my $progress = <<'P';
		// stop if the last pass consumed nothing, which malformed data can cause
		if (ptr == passStart)
		{
			stopReason = JPARSE_MALFORMED;
			return endPtr;
		}
		passStart = ptr;

P

edit('RFHUtil/JsonParse.cpp', 'stopReason',
	['constructor', "\tnumElements = 0;\n\tjsonElems = NULL;\n}",
		"\tnumElements = 0;\n\tjsonElems = NULL;\n\tmaxElements = 0;\n\tdepth = 0;\n\tstopReason = JPARSE_OK;\n}"],
	['capacity', "\tmemset(jsonElems, 0, memSize);\n",
		"\tmemset(jsonElems, 0, memSize);\n\tmaxElements = (int)(memSize / sizeof(JSONSTRUCT));\n"],
	['report the stop', "\t// start the parse process, starting with the top level element\n\tparseLevel(ptr, endPtr, root);\n",
		"\t// start the parse process, starting with the top level element\n\tdepth = 0;\n\tstopReason = JPARSE_OK;\n\tparseLevel(ptr, endPtr, root);\n\n"
		. "\tif (JPARSE_TOO_DEEP == stopReason)\n\t{\n\t\tlastError = \"Parse error - objects and arrays nested too deeply\";\n\t\treturn stopReason;\n\t}\n"
		. "\tif (stopReason != JPARSE_OK)\n\t{\n\t\tlastError = \"Parse error - unexpected character\";\n\t\treturn stopReason;\n\t}\n"],
	['depth guard class', "const char * CJsonParse::parseLevel(const char *ptr, const char *endPtr, int parent)\n\n{\n",
		"namespace\n{\n\t// counts nesting for the life of one parseLevel or processArray call\n\tclass DepthGuard\n\t{\n\tpublic:\n"
		. "\t\tDepthGuard(int & depth) : m_depth(depth) { m_depth++; }\n\t\t~DepthGuard() { m_depth--; }\n\tprivate:\n\t\tint & m_depth;\n\t};\n}\n\n"
		. "const char * CJsonParse::parseLevel(const char *ptr, const char *endPtr, int parent)\n\n{\n" . $guard
		. "\tconst char *\tpassStart = NULL;\t\t// where the current pass of the loop began\n"],
	['object progress', "\twhile ((ptr < endPtr) && (ptr[0] != '}'))\n\t{\n",
		"\twhile ((ptr < endPtr) && (ptr[0] != '}'))\n\t{\n" . $progress],
	['array guard', "const char * CJsonParse::processArray(const char *ptr, const char *endPtr, int name, const char *namePtr, int nameLen, int parent)\n\n{\n",
		"const char * CJsonParse::processArray(const char *ptr, const char *endPtr, int name, const char *namePtr, int nameLen, int parent)\n\n{\n" . $guard
		. "\tconst char *\tpassStart = NULL;\t\t// where the current pass of the loop began\n"],
	['array progress', "\twhile ((ptr < endPtr) && (ptr[0] != ']'))\n\t{\n",
		"\twhile ((ptr < endPtr) && (ptr[0] != ']'))\n\t{\n" . $progress],
	['grow the tree', "\t// check for a name\n\tif (nameLen > 0)\n\t{\n\t\tnameOfs = names.insertName(name, nameLen);\n\t}\n",
		"\t// parse() sizes the tree from a count of braces, colons and commas, which\n"
		. "\t// malformed data can exceed; grow it rather than write past the end\n"
		. "\tif (numElements >= maxElements)\n\t{\n"
		. "\t\tint newMax = (maxElements > 0) ? maxElements * 2 : 64;\n"
		. "\t\tJSONSTRUCT * bigger = (JSONSTRUCT *)rfhMalloc(newMax * sizeof(JSONSTRUCT), \"JSONELEM\");\n"
		. "\t\tif (NULL == bigger)\n\t\t{\n\t\t\t// out of memory: reuse the last element rather than overrun\n"
		. "\t\t\tnumElements = maxElements - 1;\n\t\t}\n\t\telse\n\t\t{\n"
		. "\t\t\tmemset(bigger, 0, newMax * sizeof(JSONSTRUCT));\n"
		. "\t\t\tif (jsonElems != NULL)\n\t\t\t{\n"
		. "\t\t\t\tmemcpy(bigger, jsonElems, numElements * sizeof(JSONSTRUCT));\n\t\t\t\trfhFree(jsonElems);\n\t\t\t}\n"
		. "\t\t\tjsonElems = bigger;\n\t\t\tmaxElements = newMax;\n\t\t}\n\t}\n\n"
		. "\t// check for a name\n\tif (nameLen > 0)\n\t{\n\t\tnameOfs = names.insertName(name, nameLen);\n\t}\n"],
);


edit('RFHUtil/xmlsubs.cpp', 'workLen',
	['unterminated quote', "\t\t\t\twhile (1)\n\t\t\t\t{\n\t\t\t\t\tif ((tempValue[0] == quotechar) && (tempValue[1] != quotechar))\n",
		"\t\t\t\twhile (1)\n\t\t\t\t{\n"
		. "\t\t\t\t\t// stop at the end of the text when the closing quote is missing\n"
		. "\t\t\t\t\tif (0 == tempValue[0])\n\t\t\t\t\t{\n\t\t\t\t\t\tbreak;\n\t\t\t\t\t}\n\n"
		. "\t\t\t\t\tif ((tempValue[0] == quotechar) && (tempValue[1] != quotechar))\n"],
	['no closing quote to skip', "\t\t\t\t// skip the trailing double quote or other character\n\t\t\t\ttempValue++;\n",
		"\t\t\t\t// skip the trailing double quote or other character\n"
		. "\t\t\t\tif (tempValue[0] != 0)\n\t\t\t\t{\n\t\t\t\t\ttempValue++;\n\t\t\t\t}\n"],
	['work areas', "\tchar\t*tempVarName = (char *)rfhMalloc(8192,\"XMLSUBVN\");\n\tchar\t*tempVarValue = (char *)rfhMalloc(8192,\"XMLSUBVV\");\n",
		"\t// a name or value is never longer than the attribute text\n"
		. "\tsize_t\tworkLen = strlen(tempVarAttr) + 8192;\n"
		. "\tchar\t*tempVarName = (char *)rfhMalloc(workLen,\"XMLSUBVN\");\n"
		. "\tchar\t*tempVarValue = (char *)rfhMalloc(workLen,\"XMLSUBVV\");\n"],
);

for my $p (@pending) {
	my ($path, $text, $count) = @$p;
	open my $out, '>:raw', $path or die "$path: $!";
	print $out $text; close $out;
	printf "%-22s %d change(s)\n", $path, $count;
}
```

Run: `perl tools/port/port-parser-fixes.pl`
Expected:
```
RFHUtil/JsonParse.h    2 change(s)
RFHUtil/JsonParse.cpp  8 change(s)
RFHUtil/xmlsubs.cpp    3 change(s)
```
Run it again: it must print `RFHUtil/JsonParse.h: the fix is already applied` and change nothing.

- [ ] **Step 6: Run the tests**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`
Expected: `100% tests passed, 0 tests failed out of 486`, including `memcheck.Parsers`. IBM's existing JSON and XML tests are unchanged and still pass.

- [ ] **Step 7: Commit**

```bash
git add tools/port/port-parser-fixes.pl RFHUtil/JsonParse.h RFHUtil/JsonParse.cpp RFHUtil/xmlsubs.cpp tests/UnitTests/JsonParseTests.cpp tests/UnitTests/XmlsubsTests.cpp tests/CMakeLists.txt
git commit -s -m "fix(shared): keep the JSON and XML helpers within their buffers

CJsonParse grows its tree instead of writing past it, stops at
JPARSE_MAX_DEPTH (256) levels of nesting instead of exhausting the stack,
and stops when a pass consumes nothing ("{]" looped forever). In xmlsubs,
an attribute value without its closing quote ends at the end of the text,
and attribute work areas are sized from the text. Well-formed data parses
as before. These crash or corrupt memory on Windows too; reported upstream.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JfjP6hazi7FDx4h8gULaS5"
```

---

### Task 4: Share the formatters as DataFormatter

**Files:**
- Create: `RFHUtil/DataFormatter.h`, `RFHUtil/DataFormatter.cpp` (the prelude; the script appends the methods), `tools/port/port-formatters.pl`
- Modify: `core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/core/DataFormatterTests.cpp`

The script copies 15 methods from `DataArea.cpp` (the seven views and the helpers only they call) and leaves `DataArea.cpp` unchanged; Task 9 removes `DataArea`'s copies. The rewrites are mechanical (Win32 calls, 16-bit `wchar_t`, the MFC application object, the MQ table path) apart from three memory fixes that `DataFormatter.h` lists. `DataFormatter` copies its input, padded with zero bytes, so no caller can get the padding wrong.

- [ ] **Step 1: Write the failing test**

Create `tests/core/DataFormatterTests.cpp`:

```cpp
// DataFormatterTests.cpp - DataFormatter (RFHUtil/DataFormatter.cpp) used directly.
// The views themselves are tested through format::render in FormatTests.cpp.

#include <gtest/gtest.h>

#include <string.h>

#include "DataFormatter.h"
#include "rfhconst.h"

namespace
{
    const DisplayCodepages kDisplay = { 1252, 437 };

    void prepare(DataFormatter & formatter, const char * text)
    {
        formatter.setData((const unsigned char *)text, (unsigned int)strlen(text));
        formatter.fileCcsid = 819;
        formatter.display = kDisplay;
    }
}

TEST(DataFormatter, HexViewOfTwoBytes)
{
    DataFormatter formatter;
    prepare(formatter, "AB");
    formatter.getHexData();
    ASSERT_NE(nullptr, formatter.m_data_hex);
    EXPECT_STREQ("00000000 4142", (const char *)formatter.m_data_hex);
}

TEST(DataFormatter, SecondCallReusesTheOutput)
{
    DataFormatter formatter;
    prepare(formatter, "AB");
    formatter.getHexData();
    unsigned char * first = formatter.m_data_hex;
    formatter.getHexData();
    EXPECT_EQ(first, formatter.m_data_hex);
}

TEST(DataFormatter, SetDataCopiesAndPadsTheData)
{
    char text[] = "AB";
    DataFormatter formatter;
    formatter.setData((const unsigned char *)text, 2);
    text[0] = 'X';
    EXPECT_NE((unsigned char *)text, formatter.fileData);
    EXPECT_EQ((const unsigned char *)text, formatter.source());
    EXPECT_EQ(2u, formatter.fileSize);
    EXPECT_EQ('A', formatter.fileData[0]);
    for (unsigned int i = 0; i < kLookAheadPad; i++)
    {
        EXPECT_EQ(0, formatter.fileData[2 + i]);
    }
}

TEST(DataFormatter, FixedViewHasADisplayTableWithoutACharacterViewFirst)
{
    DataFormatter formatter;
    prepare(formatter, "8=FIX\x01" "9=1\x01");
    formatter.getFixData(CHAR_ASCII);
    ASSERT_NE(nullptr, formatter.m_data_fix);
    EXPECT_STREQ("8=FIX\r\n9=1\r\n", (const char *)formatter.m_data_fix);
}
```

In `tests/CMakeLists.txt`, replace:

```cmake
    core/GuardMallocTests.cpp)
```

with:

```cmake
    core/GuardMallocTests.cpp core/DataFormatterTests.cpp)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug`
Expected: `fatal error: DataFormatter.h: No such file or directory`.

- [ ] **Step 3: Write the header and the prelude**

Create `RFHUtil/DataFormatter.h`:

```cpp
/*
Copyright (c) IBM Corporation 2000, 2026
All rights reserved. This program and the accompanying materials
are made available under the terms of the Apache License, Version 2.0
which accompanies this distribution, and is available at
http://www.apache.org/licenses/LICENSE-2.0
*/

// DataFormatter.h: the Data tab display formatters, moved out of DataArea so that
// the Windows application and the portable core share one implementation. The
// members keep DataArea's names, so the method bodies are DataArea's own.
//
// Give it the data with setData and set the other inputs, call one get...Data
// method, then read its output member. Each
// output is a NUL-terminated buffer in the display code page (app->ansi), owned by
// the formatter and freed by its destructor. A formatter keeps its outputs, so a
// second call for the same view and settings returns at once, as in DataArea.
//
// Deliberate differences from DataArea, all to stay within buffers:
// - the data and every buffer the formatters allocate are followed by
//   kLookAheadPad zero bytes, because xmlsubs.cpp compares up to 18 bytes ahead
//   without checking the remaining length (DataArea's data has 2 zero bytes);
// - the both view allocates 65 bytes per 16 bytes of data; DataArea allocates
//   4 per byte plus 65, and writes past the end for messages over 1040 bytes;
// - the fixed view indexes its display table with an unsigned byte;
// - the EDI option of the character view stops skipping blanks at the end of the
//   data;
// - the display table is built when the formatter is made. DataArea fills it with
//   '.' and builds it in the first character or both view, which Windows always
//   shows first; the other views use it without building it.

#pragma once

#include <string>
#include <vector>

#include "rfh_platform.h"

// Zero bytes the formatters may read after their data and their work buffers.
const unsigned int kLookAheadPad = 32;

// The Windows ANSI and OEM code pages the formatters display in. On Windows these
// are GetACP() and GetOEMCP(), which CRfhutilApp holds as ansi and oem.
struct DisplayCodepages
{
	int ansi;
	int oem;
};

class DataFormatter
{
public:
	DataFormatter();
	~DataFormatter();

	DataFormatter(const DataFormatter &) = delete;
	DataFormatter & operator=(const DataFormatter &) = delete;

	// Copies the data to show, followed by kLookAheadPad zero bytes, and points
	// fileData and fileSize at the copy.
	void setData(const unsigned char * data, unsigned int size);

	// The data setData was given (not the copy), for callers that check it is
	// still the data they show.
	const unsigned char * source() const { return sourceData; }

	// Inputs.
	unsigned char *	fileData;			// set by setData
	unsigned int	fileSize;			// set by setData
	int				fileCcsid;			// the message's code page (the MQMD's)
	int				fileIntFormat;		// NUMERIC_PC or NUMERIC_HOST
	int				m_char_format;		// CHAR_* of the data (DataArea::m_char_format)
	DisplayCodepages display;			// default { rfh_get_acp(), 437 }
	std::string		conversionTableDir;	// MQ's conversion tables (Windows: conv\table under the
										// MQ install directory; Linux: /opt/mqm/lib/iconv); empty for none

	// Outputs.
	unsigned char *	m_data_ascii;
	RFH_UCS2 *		m_data_ascii_UCS;	// unused: IBM disabled the Unicode character view
	unsigned char *	m_data_hex;
	unsigned char *	m_data_both;
	unsigned char *	m_data_xml;
	unsigned char *	m_data_parsed;
	unsigned char *	m_data_json;
	unsigned char *	m_data_fix;
	std::string		m_error_msg;		// set by the JSON and fixed views when they fail

	void getCharacterData(const int charFormat, const int crlf, const int edi, const int BOM, BOOL indent);
	void getHexData();
	void getBothData(const int charFormat, const int BOM);
	void getXmlData(const int charFormat);
	void getParsedData(const int charFormat);
	void getJsonData(const int charFormat);
	void getFixData(const int charFormat);

private:
	unsigned char * getTranslatedData(int * length, const int charFormat, const int ccsid);
	void getNameValueData(const unsigned char * input, int inputLen, unsigned char * output, int maxLength, int ccsid, const int charFormat);
	int xlateEbcdicChar(int fromCcsid, int toCcsid, const char * input, char * output);
	int checkForUnicodeMarker(unsigned char * data);
	int findcrlf(const unsigned char *datain, const int maxchar, const int charSet);
	BOOL isLeadChar(unsigned char ch, const int charFormat);
	int MQEBC2ASC(unsigned char * out, const unsigned char * in, int len, int ccsidIn, int ccsidOut);
	int MQEBC2UCS(RFH_UCS2 *out, const unsigned char *in, int len, int ccsidIn);
	void findMQInstallDirectory(unsigned char * dir, int dirSize);

	void logTraceEntry(const char * traceInfo);
	void dumpTraceData(const char * label, const unsigned char * data, unsigned int length);
	void updateMsgText() {}				// DataArea shows m_error_msg; callers read it instead

	const DisplayCodepages * app;		// &display: the moved code reads app->ansi and app->oem

	BOOL traceEnabled;
	BOOL verboseTrace;

	// the settings each output was made with
	int saveEdi;
	int saveBOM;
	int saveParsedFormat;
	int saveXMLFormat;
	int saveBothFormat;
	int saveCharCrlf;
	int saveCharFormat;
	int saveJsonFormat;
	int saveFixFormat;
	BOOL saveIndentOn;

	char invalidCharTable[256];
	BOOL invalidCharTableNotBuilt;

	// MQ conversion tables, loaded when first needed
	int MQCcsidLen;
	int MQUcsCcsid;
	int MQCcsidOut;
	int MQCcsidIn;
	unsigned char * MQCcsidPtr;
	RFH_UCS2 * MQUcsPtr;

	std::vector<unsigned char> ownedData;	// the copy fileData points at
	const unsigned char * sourceData;

	// XML work areas (file-scope statics in DataArea.cpp)
	char xmlVarName[8192 + 1024];			// tag name
	char tempVarValue[2 * 8192 + 1024];		// tag value
	char tempVarName[8192 + 1];				// holding area for a tag name
	char tempEndVarName[8192 + 1];
	char tempVarAttr[2 * 8192 + 1];
	char xmlAttrStr[65536];
};
```

Create `RFHUtil/DataFormatter.cpp`:

```cpp
/*
Copyright (c) IBM Corporation 2000, 2026
All rights reserved. This program and the accompanying materials
are made available under the terms of the Apache License, Version 2.0
which accompanies this distribution, and is available at
http://www.apache.org/licenses/LICENSE-2.0
*/

// DataFormatter.cpp: the Data tab display formatters (see DataFormatter.h). The
// methods after the marker line were copied from DataArea.cpp by
// tools/port/port-formatters.pl, which lists its mechanical rewrites.

#include "stdafx.h"
#include <stdint.h>
#include <stdlib.h>
#include "comsubs.h"
#include "xmlsubs.h"
#include "mqsubs.h"
#include "rfhconst.h"
#include "JsonParse.h"
#include "XMLParse.h"
#include "rfh_codepage.h"
#include "rfh_trace.h"
#include "DataFormatter.h"
#include "cmqc.h"

// from DataArea.cpp
#define MAX_ASCII_CHAR	32
#define MAX_XML_INDENT_LEVELS	15
#define BAD_XML_MSG		"*****RFHUtil format problem - Data does not appear to be XML\r\n"
#define CDATA_START		"<![CDATA["
#define CDATA_END		"]]>"

DataFormatter::DataFormatter()
{
	fileData = NULL;
	fileSize = 0;
	sourceData = NULL;
	fileCcsid = 0;
	fileIntFormat = NUMERIC_PC;
	m_char_format = CHAR_ASCII;
	display.ansi = rfh_get_acp();
	display.oem = 437;
	app = &display;

	m_data_ascii = NULL;
	m_data_ascii_UCS = NULL;
	m_data_hex = NULL;
	m_data_both = NULL;
	m_data_xml = NULL;
	m_data_parsed = NULL;
	m_data_json = NULL;
	m_data_fix = NULL;

	traceEnabled = rfhGetTrace()->isTraceEnabled();
	verboseTrace = rfhGetTrace()->verboseTrace;

	// DataArea's initial values (its constructor and resetDataArea)
	saveEdi = 0;
	saveBOM = 0;
	saveParsedFormat = CHAR_ASCII;
	saveXMLFormat = CHAR_ASCII;
	saveBothFormat = CHAR_ASCII;
	saveCharCrlf = IGNORE_CRLF;
	saveCharFormat = CHAR_ASCII;
	saveJsonFormat = CHAR_ASCII;
	saveFixFormat = CHAR_ASCII;
	saveIndentOn = FALSE;

	// see DataFormatter.h: built here rather than in the first character view
	buildInvalidCharTable(invalidCharTable);
	invalidCharTableNotBuilt = FALSE;

	MQCcsidLen = 0;
	MQUcsCcsid = 0;
	MQCcsidOut = 0;
	MQCcsidIn = 0;
	MQCcsidPtr = NULL;
	MQUcsPtr = NULL;
}

DataFormatter::~DataFormatter()
{
	unsigned char * buffers[] = { m_data_ascii, m_data_hex, m_data_both, m_data_xml,
		m_data_parsed, m_data_json, m_data_fix, MQCcsidPtr };
	for (unsigned char * buffer : buffers)
	{
		if (buffer != NULL)
		{
			rfhFree(buffer);
		}
	}
	if (m_data_ascii_UCS != NULL)
	{
		rfhFree(m_data_ascii_UCS);
	}
	if (MQUcsPtr != NULL)
	{
		rfhFree(MQUcsPtr);
	}
}

void DataFormatter::setData(const unsigned char * bytes, unsigned int size)
{
	ownedData.assign(bytes, bytes + size);
	ownedData.resize(size + kLookAheadPad, 0);
	fileData = ownedData.data();
	fileSize = size;
	sourceData = bytes;
}

void DataFormatter::logTraceEntry(const char * traceInfo)
{
	rfhGetTrace()->logTraceEntry(traceInfo);
}

void DataFormatter::dumpTraceData(const char * label, const unsigned char * data, unsigned int length)
{
	rfhGetTrace()->dumpTraceData(label, data, length);
}

// DataArea finds MQ's install directory here and appends conv\table to it; the
// formatter is given the table directory itself.
void DataFormatter::findMQInstallDirectory(unsigned char * dir, int dirSize)
{
	strncpy((char *)dir, conversionTableDir.c_str(), dirSize - 1);
	dir[dirSize - 1] = 0;
}

// See DataFormatter.h: every buffer the formatters allocate is followed by
// kLookAheadPad zero bytes (rfhMalloc zero-fills).
#define rfhMalloc(length, tag) rfhMalloc((length) + kLookAheadPad, tag)

// ---- methods moved from DataArea.cpp by tools/port/port-formatters.pl ----
```

The file must end with the marker line: the script appends the methods after it.

- [ ] **Step 4: Add and run the script**

Create `tools/port/port-formatters.pl`:

```perl
#!/usr/bin/perl
# port-formatters.pl - one-off extraction for Plan 2B.
#
# Copies the Data tab display formatters from RFHUtil/DataArea.cpp to the end of
# RFHUtil/DataFormatter.cpp, which must hold only its prelude (ending with the
# marker line below). The methods are copied in their DataArea.cpp order and are
# unchanged apart from the rewrites listed here, each of which must match exactly
# the expected number of times. DataArea.cpp is not modified. Run once from the
# repository root.
use strict; use warnings;

my $marker = "// ---- methods moved from DataArea.cpp by tools/port/port-formatters.pl ----\n";

sub slurp {
	my ($path) = @_;
	open my $in, '<:raw', $path or die "$path: $!\n";
	my $text = do { local $/; <$in> };
	close $in;
	return $text;
}

my $source = slurp('RFHUtil/DataArea.cpp');
$source =~ s/\r\n/\n/g;

my $target = slurp('RFHUtil/DataFormatter.cpp');
my $crlf = ($target =~ /\r\n/);
$target =~ s/\r\n/\n/g;
die "RFHUtil/DataFormatter.cpp: does not end with the marker line, or already holds the methods\n"
	unless $target =~ /\Q$marker\E\n*\z/;

# The formatters and the helpers only they call.
my @methods = qw(getHexData xlateEbcdicChar getCharacterData getBothData getTranslatedData
	getXmlData getJsonData getFixData getNameValueData getParsedData checkForUnicodeMarker
	findcrlf isLeadChar MQEBC2ASC MQEBC2UCS);

my @found;
for my $m (@methods) {
	my @hits;
	while ($source =~ /^([A-Za-z][^\n;]*?\bDataArea::\Q$m\E\s*\((?:[^)]|\n)*\)\s*\n\{.*?\n\}\n)/msg) {
		push @hits, [$-[1], $1];
	}
	die "DataArea.cpp: $m: found " . scalar(@hits) . " definitions, expected 1\n" unless @hits == 1;
	push @found, $hits[0];
}
my $methods = join("\n", map { $_->[1] } sort { $a->[0] <=> $b->[0] } @found);
$methods =~ s/\bDataArea::(\w+)\s*\(/DataFormatter::$1(/g;

# [what, expected count, substitution]
my @rewrites = (
	['pointers traced as %8.8X', 10, sub { $_[0] =~ s/\(unsigned int\)\s*(m_data_\w+|newData|tempData|data)\b/(unsigned int)(uintptr_t)$1/g }],
	['application object declarations', 2, sub { $_[0] =~ s/^[ \t]*CRfhutilApp\s*\*\s*app\b[^;\n]*;[^\n]*\n//mg }],
	['application object lookups', 2, sub { $_[0] =~ s/^[ \t]*app\s*=\s*\(CRfhutilApp\s*\*\)\s*AfxGetApp\(\);[ \t]*\n//mg }],
	['wchar_t is 32 bits on Linux', 9, sub { $_[0] =~ s/\bwchar_t\b/RFH_UCS2/g }],
	['LPCWSTR casts', 4, sub { $_[0] =~ s/\(LPCWSTR\)/(const RFH_UCS2 *)/g }],
	['WideCharToMultiByte', 7, sub { $_[0] =~ s/\bWideCharToMultiByte\(/rfh_WideCharToMultiByte(/g }],
	['MultiByteToWideChar', 2, sub { $_[0] =~ s/\bMultiByteToWideChar\(/rfh_MultiByteToWideChar(/g }],
	['CP_ACP is the display ANSI code page', 1, sub { $_[0] =~ s/\bCP_ACP\b/app->ansi/g }],
	['CP_OEMCP is the display OEM code page', 1, sub { $_[0] =~ s/\bCP_OEMCP\b/app->oem/g }],
	['C-locale wcstombs', 2, sub { $_[0] =~ s/wcstombs\(\(char \*\)newData, (\(const RFH_UCS2 \*\)\([^)]*\)), /rfh_ucs2_to_c_locale($1, (char *)newData, /g }],
	['MQ table directory separator', 3, sub { $_[0] =~ s/"\\\\conv\\\\table\\\\/RFH_DIR_SEP "/g }],
	['no table directory, no tables', 3, sub { $_[0] =~ s/\btbl = fopen\(fileName, "rb"\);/tbl = conversionTableDir.empty() ? NULL : fopen(fileName, "rb");/g }],
	# memory safety (deliberate differences; see DataFormatter.h)
	['both view buffer', 1, sub { $_[0] =~ s/len = \(fileLength << 2\) \+ 65;/len = ((fileLength + 15) \/ 16) * 65 + 65;\t\t\/\/ 65 bytes per line of 16; Windows allocates (fileLength << 2) + 65/g }],
	['fixed view table index', 1, sub { $_[0] =~ s/m_data_fix\[len\] = invalidCharTable\[tempTxt\[0\]\];/m_data_fix[len] = invalidCharTable[(unsigned char)tempTxt[0]];/g }],
	['EDI blank skip', 1, sub { $_[0] =~ s/while \(data\[buffer \+ newline\] <= ' '\)/while ((newline < remaining) && (data[buffer + newline] <= ' '))/g }],
);
for my $r (@rewrites) {
	my ($what, $want, $code) = @$r;
	my $n = 0 + ($code->($methods) || 0);
	die "$what: matched $n time(s), expected $want\n" unless $n == $want;
	printf "%-40s %2d\n", $what, $n;
}

$target =~ s/\n*\z/\n\n/;
$target .= $methods;
$target =~ s/\n/\r\n/g if $crlf;
open my $out, '>:raw', 'RFHUtil/DataFormatter.cpp' or die "RFHUtil/DataFormatter.cpp: $!\n";
print $out $target;
close $out;
printf "%d methods copied to RFHUtil/DataFormatter.cpp\n", scalar @found;
```

Run: `perl tools/port/port-formatters.pl`
Expected:
```
pointers traced as %8.8X                 10
application object declarations           2
application object lookups                2
wchar_t is 32 bits on Linux               9
LPCWSTR casts                             4
WideCharToMultiByte                       7
MultiByteToWideChar                       2
CP_ACP is the display ANSI code page      1
CP_OEMCP is the display OEM code page     1
C-locale wcstombs                         2
MQ table directory separator              3
no table directory, no tables             3
both view buffer                          1
fixed view table index                    1
EDI blank skip                            1
15 methods copied to RFHUtil/DataFormatter.cpp
```
Run it again: it must print `RFHUtil/DataFormatter.cpp: does not end with the marker line, or already holds the methods`. `git diff --stat RFHUtil/DataArea.cpp` must be empty.

- [ ] **Step 5: Build it as a shared source**

In `core/CMakeLists.txt`, replace:

```cmake
    ${PROJECT_SOURCE_DIR}/RFHUtil/mqsubs.cpp
```

with:

```cmake
    ${PROJECT_SOURCE_DIR}/RFHUtil/mqsubs.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/DataFormatter.cpp
```

- [ ] **Step 6: Run the tests**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`
Expected: `100% tests passed, 0 tests failed out of 490` (4 new tests). The build shows one new warning, `DataFormatter.cpp: ... ISO C++ forbids converting a string constant to 'char*'`: IBM's code passes `""` to a `char *` parameter, a historic warning of the kind the shared sources keep.

- [ ] **Step 7: Commit**

```bash
git add RFHUtil/DataFormatter.h RFHUtil/DataFormatter.cpp tools/port/port-formatters.pl tests/core/DataFormatterTests.cpp core/CMakeLists.txt tests/CMakeLists.txt
git commit -s -m "feat(core): share the Data tab formatters as DataFormatter

tools/port/port-formatters.pl copies the seven views and their helpers
from DataArea.cpp into RFHUtil/DataFormatter.cpp, keeping IBM's bodies
apart from mechanical portability rewrites. Three memory fixes are
deliberate: the both view's buffer (it overran above 1040 bytes), the
fixed view's signed table index, and the EDI blank skip. DataArea.cpp is
unchanged until the Windows delegation.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JfjP6hazi7FDx4h8gULaS5"
```

---

### Task 5: Render the views of a message

**Files:**
- Create: `core/format/rfh_format.h`, `core/format/rfh_format.cpp`
- Modify: `core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/core/FormatTests.cpp`

`render` takes the code page and integer encoding from its options, not from `message.mqmd`: for a file without an MQMD they come from the file code page setting or EBCDIC detection, which `fileio::ReadResult` already reports. So each source has one owner of its defaults: `defaultsFor(message, warnings)` after a get (`processMessage` and `parseMsgHeaders`), `defaultsFor(readResult)` after a read. The COBOL view comes in Task 7.

**Superseded by review.** `defaultsFor` returns `DataFormats` (the five formats), not `RenderOptions`, and `RenderOptions::apply(const DataFormats &)` copies them over the user's choices, so the tests assign `DataFormats f = defaultsFor(...)` and `o.apply(f)` where the blocks below write `RenderOptions o = defaultsFor(...)`. `charFormat` is taken from the repaired code page. `DisplayCodepages` moved to `core/platform/rfh_codepage.h`, and `rfh_format.h` has a `static_assert` that `View` follows `DATA_*`. `render` allocates its `DataFormatter` on the heap (`DataFormatter formatter;` below is the first-pass form). Commits `650b2eb` and `7d2c91d` are authoritative.

- [ ] **Step 1: Write the failing tests**

Create `tests/core/FormatTests.cpp`:

```cpp
// FormatTests.cpp - format::render, the Data tab views, and their default settings.
//
// The expected text is the Windows Data tab's: offsets of 8 digits, CR LF line
// ends, 32 characters a line in the character view and 16 bytes a line in the hex
// and both views. ctest -L memcheck runs these under the guard allocator.

#include <gtest/gtest.h>

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "comsubs.h"
#include "mq_layout.h"
#include "rfh_format.h"

using namespace rfh;
using namespace rfh::format;
namespace L = mq_layout;
using L::Order;

namespace
{
    Message message(const std::string & body, int ccsid = 819, int encoding = 546)
    {
        Message m;
        m.mqmd.CodedCharSetId = ccsid;
        m.mqmd.Encoding = encoding;
        memcpy(m.mqmd.Format, MQFMT_STRING, MQ_FORMAT_LENGTH);
        m.body.assign(body.begin(), body.end());
        return m;
    }

    RenderOptions options(int ccsid = 819, int charFormat = CHAR_ASCII)
    {
        RenderOptions o;
        o.ccsid = ccsid;
        o.charFormat = charFormat;
        o.display = { 1252, 437 };
        return o;
    }

    std::string render(const Message & m, View view, const RenderOptions & o)
    {
        std::string out;
        Result r = format::render(m, view, o, out);
        EXPECT_TRUE(r.ok()) << r.text;
        return out;
    }

    std::string ebcdic(const std::string & ascii)
    {
        std::string out(ascii.size(), 0);
        AsciiToEbcdic((unsigned char *)ascii.data(), (int)ascii.size(), (unsigned char *)&out[0]);
        return out;
    }
}

// --- hex view -----------------------------------------------------------------

TEST(Format_Hex, SixteenBytesALineInGroupsOfFour)
{
    EXPECT_EQ("00000000 30313233 34353637 38394142 43444546\r\n"
              "00000016 4748494A",
              render(message("0123456789ABCDEFGHIJ"), View::Hex, options()));
}

TEST(Format_Hex, EmptyBodyIsEmpty)
{
    EXPECT_EQ("", render(message(""), View::Hex, options()));
}

// --- character view -----------------------------------------------------------

TEST(Format_Character, ThirtyTwoCharactersALine)
{
    EXPECT_EQ("00000000 abcdefghijklmnopqrstuvwxyz012345\r\n"
              "00000032 6789ABCD",
              render(message("abcdefghijklmnopqrstuvwxyz0123456789ABCD"), View::Character, options()));
}

TEST(Format_Character, ControlCharactersAreShownAsPeriods)
{
    EXPECT_EQ("00000000 a.b.c\xe9",
              render(message(std::string("a\x01" "b\0c\xe9", 6)), View::Character, options()));
}

TEST(Format_Character, CrLfStartsALineWhenHonoured)
{
    RenderOptions o = options();
    o.crlf = true;
    EXPECT_EQ("00000000 line one\r\n"
              "00000010 line two",
              render(message("line one\r\nline two"), View::Character, o));
}

TEST(Format_Character, CrLfIsAPeriodPairOtherwise)
{
    EXPECT_EQ("00000000 line one..line two",
              render(message("line one\r\nline two"), View::Character, options()));
}

TEST(Format_Character, EdiSegmentsEachHaveALine)
{
    RenderOptions o = options();
    o.edi = true;
    EXPECT_EQ("00000000 UNA:+.? '\r\n"
              "00000009 UNB+UNOC:3+X'\r\n"
              "00000022 UNH+1'",
              render(message("UNA:+.? 'UNB+UNOC:3+X'UNH+1'"), View::Character, o));
}

TEST(Format_Character, EbcdicIsTranslated)
{
    EXPECT_EQ("00000000 Hello, world!",
              render(message(ebcdic("Hello, world!"), 500, 785), View::Character, options(500, CHAR_EBCDIC)));
}

TEST(Format_Character, UnicodeIsShownAsItsBytes)
{
    // IBM disabled the UCS-2 branch of the character view (getCharacterData has
    // "if (0)" there), so Windows shows UTF-16 data byte by byte too
    EXPECT_EQ("00000000 H.i.",
              render(message(std::string("H\0i\0", 4), 1200), View::Character, options(1200)));
}

// --- both view ----------------------------------------------------------------

TEST(Format_Both, HexThenTextSixteenBytesALine)
{
    EXPECT_EQ("00000000 30313233 34353637 38394142 43444546 01234567 89ABCDEF \r\n"
              "00000016 4748494A                            GHIJ",
              render(message("0123456789ABCDEFGHIJ"), View::Both, options()));
}

TEST(Format_Both, EbcdicTextIsTranslatedButTheHexIsNot)
{
    EXPECT_EQ("00000000 C8859393 96                         Hello",
              render(message(ebcdic("Hello"), 500, 785), View::Both, options(500, CHAR_EBCDIC)));
}

TEST(Format_Both, LongMessageFitsItsBuffer)
{
    // Windows allocates 4 bytes per data byte plus 65 for 65 bytes per line of 16,
    // so it writes past the end above 1040 bytes (memcheck catches a regression)
    std::string body(5000, 'x');
    std::string out = render(message(body), View::Both, options());
    std::string lastLine = out.substr(out.rfind("\r\n") + 2);
    // a blank follows every 8 characters of text, so an 8-character last line ends in one
    EXPECT_EQ("00004992 78787878 78787878                   xxxxxxxx ", lastLine);
    EXPECT_EQ(312u * 65u + lastLine.size(), out.size());
}

// --- default settings ---------------------------------------------------------

TEST(Format_Defaults, AfterAGetTheyFollowTheLastHeader)
{
    Message m = message("data", 819, 546);
    memcpy(m.mqmd.Format, MQFMT_RF_HEADER_2, MQ_FORMAT_LENGTH);
    m.headers = L::rfh2(Order::Little, 785, 500, "MQSTR").data;

    std::vector<std::string> warnings;
    RenderOptions o = defaultsFor(m, warnings);
    EXPECT_EQ(819, o.ccsid) << "the code page is the MQMD's, as DataArea::fileCcsid";
    EXPECT_EQ(NUMERIC_PC, o.intFormat);
    EXPECT_EQ(CHAR_EBCDIC, o.charFormat);
    EXPECT_EQ(NUMERIC_HOST, o.numericFormat);
    EXPECT_EQ(NUMERIC_HOST, o.pdNumericFormat);
    EXPECT_TRUE(warnings.empty());
}

TEST(Format_Defaults, AfterAGetAReversedCodePageIsRepaired)
{
    std::vector<std::string> warnings;
    RenderOptions o = defaultsFor(message("x", reverseBytes4(500)), warnings);
    EXPECT_EQ(500, o.ccsid);
    EXPECT_TRUE(warnings.empty());
}

TEST(Format_Defaults, AfterAGetAnInvalidCodePageIsZeroWithAWarning)
{
    std::vector<std::string> warnings;
    RenderOptions o = defaultsFor(message("x", 100000), warnings);
    EXPECT_EQ(0, o.ccsid);
    ASSERT_EQ(1u, warnings.size());
    EXPECT_EQ("Invalid code page in MQMD - 100000\r\n", warnings[0]);
}

TEST(Format_Defaults, AfterAReadTheyAreTheReadResults)
{
    fileio::ReadResult read;
    read.fileCcsid = 37;
    read.fileIntFormat = NUMERIC_HOST;
    read.charFormat = CHAR_EBCDIC;
    read.numericFormat = NUMERIC_HOST;
    read.pdFormat = NUMERIC_PC;
    RenderOptions o = defaultsFor(read);
    EXPECT_EQ(37, o.ccsid);
    EXPECT_EQ(NUMERIC_HOST, o.intFormat);
    EXPECT_EQ(CHAR_EBCDIC, o.charFormat);
    EXPECT_EQ(NUMERIC_HOST, o.numericFormat);
    EXPECT_EQ(NUMERIC_PC, o.pdNumericFormat);
}
```

In `tests/CMakeLists.txt`, replace:

```cmake
    core/GuardMallocTests.cpp core/DataFormatterTests.cpp)
```

with:

```cmake
    core/GuardMallocTests.cpp core/DataFormatterTests.cpp
    core/FormatTests.cpp)
```

Append to `tests/CMakeLists.txt`:

```cmake
rfh_add_memcheck_test(memcheck.Format rfhutil_core_tests "DataFormatter.*:Format_*")
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug`
Expected: `fatal error: rfh_format.h: No such file or directory`.

- [ ] **Step 3: Write render and defaultsFor**

Create `core/format/rfh_format.h`:

```cpp
// rfh_format.h - the Data tab's views of a message body.
//
// render produces the text the Windows Data tab shows, with DataFormatter
// (RFHUtil/DataFormatter.cpp), the code the Windows Data tab uses. The text is in
// the display code page options.display.ansi, as Windows puts it in the edit
// control; a UI decodes it from that code page.
//
// Differences from Windows RFHUtil:
// - the COBOL view reads the copy book named by options.copybookPath instead of
//   asking for one, and fails when it cannot be read (Windows shows an empty view);
// - see DataFormatter.h for the differences in the views themselves.
#pragma once

#include <string>
#include <vector>

#include "DataFormatter.h"
#include "rfh_codepage.h"
#include "rfh_message.h"
#include "rfh_message_file.h"
#include "rfh_result.h"
#include "rfhconst.h"

namespace rfh
{
	namespace format
	{
		// The Data tab's display choices, in the order of DATA_CHARACTER ... DATA_FIX.
		enum class View { Character, Hex, Both, Xml, Parsed, Cobol, Json, Fixed };

		// The Data tab's settings. defaultsFor gives the values Windows starts with;
		// the user may then change them.
		struct RenderOptions
		{
			int ccsid = 0;						// code page of the body (DataArea::fileCcsid)
			int intFormat = NUMERIC_PC;			// integer encoding of the body (DataArea::fileIntFormat)
			int charFormat = CHAR_ASCII;		// CHAR_* the body is shown as
			int numericFormat = NUMERIC_PC;		// COBOL view: binary integers
			int pdNumericFormat = NUMERIC_PC;	// COBOL view: packed decimal
			bool crlf = false;					// character view: start a line after each CR LF
			bool edi = false;					// character view: a line per EDI segment
			bool bom = false;					// character and both views: honour a byte order mark
			bool indent = false;				// character view: indent XML; COBOL view: indent levels
			bool checkData = false;				// COBOL view: check the field contents
			DisplayCodepages display = { rfh_get_acp(), 437 };	// Windows GetACP() and GetOEMCP()
			std::string conversionTableDir;		// MQ's conversion tables; empty for none
			std::string copybookPath;			// COBOL view: the copy book file
		};

		// The settings after getting this message, as DataArea::processMessage and
		// parseMsgHeaders set them: the MQMD's code page and integer encoding, and
		// the character format and encoding of the data after the last header. A
		// code page that is out of range is byte-reversed if that makes it valid
		// (Windows' workaround for SIBus), and otherwise becomes 0 with a warning.
		RenderOptions defaultsFor(const Message & message, std::vector<std::string> & warnings);

		// The settings after reading a message file (the formats ReadResult reports).
		RenderOptions defaultsFor(const fileio::ReadResult & read);

		// Renders message.body (the headers are not shown, as on Windows) into out.
		// Returns a failure only when the view cannot be produced; problems Windows
		// reports in the status line are warnings.
		Result render(const Message & message, View view, const RenderOptions & options, std::string & out);
	}
}
```

Create `core/format/rfh_format.cpp`:

```cpp
// rfh_format.cpp - see rfh_format.h

#include "rfh_format.h"

#include <stdio.h>

#include "comsubs.h"
#include "mqsubs.h"
#include "rfh_header_chain.h"

namespace rfh
{
	namespace format
	{
		RenderOptions defaultsFor(const Message & message, std::vector<std::string> & warnings)
		{
			RenderOptions options;

			// DataArea::processMessage
			options.intFormat = ((message.mqmd.Encoding & MQENC_INTEGER_REVERSED) > 0) ? NUMERIC_PC : NUMERIC_HOST;
			MQLONG ccsid = message.mqmd.CodedCharSetId;
			options.ccsid = ccsid;
			if ((ccsid < 0) || (ccsid > 99999))
			{
				MQLONG reversed = reverseBytes4(ccsid);
				if ((reversed > 0) && (reversed < 99999))
				{
					options.ccsid = reversed;
				}
				else
				{
					char text[64];
					snprintf(text, sizeof(text), "Invalid code page in MQMD - %d\r\n", (int)ccsid);
					warnings.push_back(text);
					options.ccsid = 0;
				}
			}

			// DataArea::parseMsgHeaders: the data after the last header
			HeaderChain chain = walkHeaders(message.headers.data(), message.headers.size(), message.mqmd);
			options.charFormat = chain.charFormat;
			options.numericFormat = chain.encoding;
			options.pdNumericFormat = (NUMERIC_PC == chain.encoding) ? NUMERIC_PC : NUMERIC_HOST;
			return options;
		}

		RenderOptions defaultsFor(const fileio::ReadResult & read)
		{
			RenderOptions options;
			options.ccsid = read.fileCcsid;
			options.intFormat = read.fileIntFormat;
			options.charFormat = read.charFormat;
			options.numericFormat = read.numericFormat;
			options.pdNumericFormat = read.pdFormat;
			return options;
		}

		Result render(const Message & message, View view, const RenderOptions & options, std::string & out)
		{
			out.clear();

			DataFormatter formatter;
			formatter.setData(message.body.data(), (unsigned int)message.body.size());
			formatter.fileCcsid = options.ccsid;
			formatter.fileIntFormat = options.intFormat;
			formatter.m_char_format = options.charFormat;
			formatter.display = options.display;
			formatter.conversionTableDir = options.conversionTableDir;

			const unsigned char * text = NULL;
			switch (view)
			{
			case View::Character:
				formatter.getCharacterData(options.charFormat, options.crlf, options.edi, options.bom, options.indent);
				text = formatter.m_data_ascii;
				break;
			case View::Hex:
				formatter.getHexData();
				text = formatter.m_data_hex;
				break;
			case View::Both:
				formatter.getBothData(options.charFormat, options.bom);
				text = formatter.m_data_both;
				break;
			case View::Xml:
				formatter.getXmlData(options.charFormat);
				text = formatter.m_data_xml;
				break;
			case View::Parsed:
				formatter.getParsedData(options.charFormat);
				text = formatter.m_data_parsed;
				break;
			case View::Json:
				formatter.getJsonData(options.charFormat);
				text = formatter.m_data_json;
				break;
			case View::Fixed:
				formatter.getFixData(options.charFormat);
				text = formatter.m_data_fix;
				break;
			case View::Cobol:
				return Result::failure("the COBOL view is not supported yet");
			}

			Result result = Result::success();
			if (!formatter.m_error_msg.empty())
			{
				result.warnings.push_back(formatter.m_error_msg);
			}
			if (text != NULL)
			{
				// the edit control shows the text up to its terminator
				out.assign((const char *)text);
			}
			return result;
		}
	}
}
```

In `core/CMakeLists.txt`, replace:

```cmake
    fileio/rfh_message_file.cpp
    ${RFHUTIL_SHARED_SOURCES}
```

with:

```cmake
    fileio/rfh_message_file.cpp
    format/rfh_format.cpp
    ${RFHUTIL_SHARED_SOURCES}
```

In `core/CMakeLists.txt`, replace:

```cmake
        ${CMAKE_CURRENT_SOURCE_DIR}/fileio
```

with:

```cmake
        ${CMAKE_CURRENT_SOURCE_DIR}/fileio
        ${CMAKE_CURRENT_SOURCE_DIR}/format
```

In `core/CMakeLists.txt`, replace:

```cmake
        fileio/rfh_message_file.cpp
        PROPERTIES
```

with:

```cmake
        fileio/rfh_message_file.cpp
        format/rfh_format.cpp
        PROPERTIES
```

- [ ] **Step 4: Run the tests**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`
Expected: `100% tests passed, 0 tests failed out of 507` (16 tests and `memcheck.Format`). With IBM's original both-view buffer size (`len = (fileLength << 2) + 65;` in `DataFormatter.cpp`), `Format_Both.LongMessageFitsItsBuffer` aborts on heap corruption and `memcheck.Format` segfaults; that was checked while writing this plan.

- [ ] **Step 5: Commit**

```bash
git add core/format/rfh_format.h core/format/rfh_format.cpp tests/core/FormatTests.cpp core/CMakeLists.txt tests/CMakeLists.txt
git commit -s -m "feat(core): render the Data tab views of a message

format::render gives the text of the character, hex, both, XML, parsed,
JSON and fixed views through DataFormatter, in the display code page.
format::defaultsFor gives the settings Windows starts with: after a get
(the MQMD's code page, with Windows' reversed-code-page repair and invalid
code page warning, and the formats after the last header) and after a
file read (ReadResult's formats).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JfjP6hazi7FDx4h8gULaS5"
```

---

### Task 6: Pin the XML, parsed, JSON and fixed views

**Files:**
- Test: `tests/core/FormatTests.cpp`

These views already render (Task 5); the tests pin their Windows text, including IBM's JSON array behaviour (reported upstream) and the two fixes that reach them.

- [ ] **Step 1: Add the tests**

Append to `tests/core/FormatTests.cpp`:

```cpp

// --- XML view -----------------------------------------------------------------

TEST(Format_Xml, ElementsAreIndentedOneSpaceALevel)
{
    EXPECT_EQ("<a>\r\n"
              " <b x=\"1\">text</b>\r\n"
              " <c/>\r\n"
              "</a>",
              render(message("<a><b x=\"1\">text</b><c/></a>"), View::Xml, options()));
}

TEST(Format_Xml, EbcdicIsTranslated)
{
    EXPECT_EQ("<a>\r\n"
              " <b>cafe</b>\r\n"
              "</a>",
              render(message(ebcdic("<a><b>cafe</b></a>"), 500, 785), View::Xml, options(500, CHAR_EBCDIC)));
}

TEST(Format_Xml, DataThatIsNotXmlGetsTheWindowsMessage)
{
    EXPECT_EQ("*****RFHUtil format problem - Data does not appear to be XML\r\n"
              "*****No beginning bracket found\r\n",
              render(message("plain text"), View::Xml, options()));
}

// --- parsed view --------------------------------------------------------------

TEST(Format_Parsed, ElementsAreDottedPathsWithValues)
{
    EXPECT_EQ("a\r\n"
              "a.b='text'\r\n"
              "a.(XML.attr)x='1'\r\n"
              "a.c=''",
              render(message("<a><b x=\"1\">text</b><c/></a>"), View::Parsed, options()));
}

TEST(Format_Parsed, AnUnterminatedAttributeQuoteEndsAtTheEndOfTheTag)
{
    // xmlsubs.cpp's extractAttribute ran past the end of the text before Plan 2B
    std::string out = render(message("<a x=\"open>t</a>"), View::Parsed, options());
    EXPECT_NE(std::string::npos, out.find("a.(XML.attr)x='open"));
}

// --- JSON view ----------------------------------------------------------------

TEST(Format_Json, MembersBecomeDottedNameValueLines)
{
    EXPECT_EQ("a=1\r\n"
              "b.c=\"x\"",
              render(message("{\"a\":1,\"b\":{\"c\":\"x\"}}"), View::Json, options()));
}

TEST(Format_Json, ArraysAreShownAsIbmsParserLeavesThem)
{
    // CJsonParse keeps only the last array element and the text up to the next
    // name; this is IBM's behaviour (reported upstream), pinned here
    EXPECT_EQ("a=1\r\n"
              "b=\"x\"\r\n"
              ",\"c\".d=null",
              render(message("{\"a\":1,\"b\":[true,\"x\"],\"c\":{\"d\":null}}"), View::Json, options()));
}

TEST(Format_Json, DataThatIsNotJsonIsAWarningAndNoText)
{
    std::string out;
    Result r = format::render(message("not json"), View::Json, options(), out);
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(1u, r.warnings.size());
    EXPECT_EQ("Error parsing user data", r.warnings[0]);
    EXPECT_EQ("", out);
}

TEST(Format_Json, MalformedJsonIsAWarningNotAHang)
{
    std::string out;
    Result r = format::render(message("{]"), View::Json, options(), out);
    ASSERT_EQ(1u, r.warnings.size());
    EXPECT_EQ("Error parsing user data", r.warnings[0]);
}

// --- fixed (FIX protocol) view ------------------------------------------------

TEST(Format_Fixed, EachSohEndsALine)
{
    EXPECT_EQ("8=FIX.4.2\r\n"
              "35=D\r\n"
              "10=123\r\n",
              render(message("8=FIX.4.2\x01" "35=D\x01" "10=123\x01"), View::Fixed, options()));
}

TEST(Format_Fixed, EbcdicAccentedLettersAreShown)
{
    // 0x51 is e-acute in code page 500; DataArea indexed its display table with a
    // signed char, which for this byte reads before the table
    std::string body = ebcdic("58=caf") + "\x51" + "\x01";
    EXPECT_EQ("58=caf\xe9\r\n", render(message(body, 500, 785), View::Fixed, options(500, CHAR_EBCDIC)));
}
```

- [ ] **Step 2: Run the tests**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`
Expected: `100% tests passed, 0 tests failed out of 518` (11 new tests, which pass at once because they pin existing behaviour). Checked while writing this plan: with IBM's signed index (`invalidCharTable[tempTxt[0]]` in `getFixData`), `Format_Fixed.EbcdicAccentedLettersAreShown` fails with `"58=caf"`.

- [ ] **Step 3: Commit**

```bash
git add tests/core/FormatTests.cpp
git commit -s -m "test(core): pin the XML, parsed, JSON and fixed views

The XML view's indentation and not-XML message, the parsed view's dotted
names and attributes, the JSON view's name=value lines (including how
IBM's parser shows arrays) and errors, and the fixed view's lines,
including EBCDIC accented letters.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JfjP6hazi7FDx4h8gULaS5"
```

---

### Task 7: The COBOL view

**Files:**
- Create: `tools/port/port-copybook.pl`
- Modify (by the script): `RFHUtil/Copybook.h`, `RFHUtil/Copybook.cpp`
- Modify: `core/CMakeLists.txt`, `core/format/rfh_format.cpp`
- Test: `tests/core/FormatTests.cpp`

Windows asks for a copy book file the first time the COBOL view is shown (`DataArea::readCopybookFile`, a file dialog) and formats the data with IBM's `CCopybook`. The core takes the path from `RenderOptions::copybookPath`. `getCobolData` and `readCopybookFile` stay in `DataArea`, because they are UI.

**Superseded by review.** The COBOL view fails when the copy book cannot be opened or when IBM's parser reports an error in it, and when it needs more than `kMaxCobolBuffer` (256 MB); each failure has its own message. The copy book comes from `RenderOptions::copybookPath`. The test copy books are files with unique names. `tools/port/harden-copybook.pl` hardens IBM's parser (22 tests), and a group occurrence past the end of the data is not listed, which is documented in `rfh_format.h`. `defaultsFor` here returns `DataFormats`, as under Task 5. Commits `71e52de`, `8798ba8`, `3d403e4` and `6f0bf6d` are authoritative.

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/FormatTests.cpp`:

```cpp

// --- COBOL view ---------------------------------------------------------------

namespace
{
    // a copy book with display, character, packed decimal and binary fields
    std::string writeCopybook()
    {
        std::string path = testing::TempDir() + "rfhutil_customer.cpy";
        FILE * f = fopen(path.c_str(), "w");
        EXPECT_NE(nullptr, f);
        fputs("       01  CUSTOMER-RECORD.\n"
              "           05  CUST-ID          PIC 9(5).\n"
              "           05  CUST-NAME        PIC X(10).\n"
              "           05  BALANCE          PIC S9(5)V99 COMP-3.\n"
              "           05  TXN-COUNT        PIC S9(4) COMP.\n", f);
        fclose(f);
        return path;
    }

    RenderOptions cobolOptions(int numericFormat)
    {
        RenderOptions o = options();
        o.numericFormat = numericFormat;
        o.pdNumericFormat = NUMERIC_HOST;
        o.copybookPath = writeCopybook();
        return o;
    }

    const std::string kCustomer = std::string("00042SMITH     ") + std::string("\x01\x23\x45\x6C", 4);
}

TEST(Format_Cobol, FieldsAreListedWithTheirOffsetsAndValues)
{
    EXPECT_EQ("Level  Ofs  Len Type  Occ Variable Name                  Value\r\n"
              "01     0    21           CUSTOMER-RECORD\r\n"
              "05     0     5 NUMB      CUST-ID                        00042\r\n"
              "05     5    10 CHAR      CUST-NAME                      SMITH     \r\n"
              "05    15     4 PD        BALANCE                        0123456\r\n"
              "05    19     2 INT       TXN-COUNT                      0007\r\n"
              "Copy book size 21, Data area size 21",
              render(message(kCustomer + std::string("\x00\x07", 2)), View::Cobol, cobolOptions(NUMERIC_HOST)));
}

TEST(Format_Cobol, BinaryFieldsFollowTheIntegerEncoding)
{
    std::string out = render(message(kCustomer + std::string("\x07\x00", 2)), View::Cobol, cobolOptions(NUMERIC_PC));
    EXPECT_NE(std::string::npos, out.find("TXN-COUNT                      0007\r\n"));
}

TEST(Format_Cobol, IndentShiftsTheLowerLevels)
{
    RenderOptions o = cobolOptions(NUMERIC_HOST);
    o.indent = true;
    std::string out = render(message(kCustomer + std::string("\x00\x07", 2)), View::Cobol, o);
    EXPECT_NE(std::string::npos, out.find("\r\n01     0    21           CUSTOMER-RECORD\r\n 05     0     5 NUMB"));
}

TEST(Format_Cobol, WithoutACopybookTheViewFails)
{
    std::string out;
    Result r = format::render(message(kCustomer), View::Cobol, options(), out);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("the COBOL view needs a copy book file", r.text);
}

TEST(Format_Cobol, AnUnreadableCopybookFails)
{
    RenderOptions o = options();
    o.copybookPath = testing::TempDir() + "rfhutil_no_such_copybook.cpy";
    std::string out;
    Result r = format::render(message(kCustomer), View::Cobol, o, out);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(0u, r.text.find("copy book file " + o.copybookPath + " could not be read"));
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ./build/linux-debug/tests/rfhutil_core_tests --gtest_filter='Format_Cobol*'`
Expected: 5 FAILED. For example, `Format_Cobol.WithoutACopybookTheViewFails` gets `"the COBOL view is not supported yet"`.

- [ ] **Step 3: Make the copy book parser a shared source**

Create `tools/port/port-copybook.pl`:

```perl
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
```

Run: `perl tools/port/port-copybook.pl`
Expected:
```
RFHUtil/Copybook.h     2 edit(s)
RFHUtil/Copybook.cpp   6 edit(s)
```
Run it again: it must fail with `RFHUtil/Copybook.h: no MFC base class: matched 0 time(s), expected 1` and change nothing.

In `core/CMakeLists.txt`, replace:

```cmake
    ${PROJECT_SOURCE_DIR}/RFHUtil/DataFormatter.cpp
```

with:

```cmake
    ${PROJECT_SOURCE_DIR}/RFHUtil/DataFormatter.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/Copybook.cpp
```

- [ ] **Step 4: Render the COBOL view**

In `core/format/rfh_format.cpp`, replace:

```cpp
#include "comsubs.h"
```

with:

```cpp
#include "Copybook.h"
#include "comsubs.h"
```

In `core/format/rfh_format.cpp`, replace:

```cpp
		RenderOptions defaultsFor(const Message & message, std::vector<std::string> & warnings)
```

with:

```cpp
		namespace
		{
			// DataArea::getCobolData, with the copy book named by the options
			// instead of chosen in a file dialog
			Result renderCobol(const DataFormatter & formatter, const RenderOptions & options, std::string & out)
			{
				if (options.copybookPath.empty())
				{
					return Result::failure("the COBOL view needs a copy book file");
				}

				CCopybook copybook;
				copybook.resetCopybook();
				int rc = copybook.parseCopyBook(options.copybookPath.c_str());
				if (rc != 0)
				{
					char text[32];
					snprintf(text, sizeof(text), " could not be read (%d)", rc);
					return Result::failure("copy book file " + options.copybookPath + text);
				}

				if (formatter.fileSize > 0)
				{
					int maxSize = (copybook.getMaxLines() * 128) + 65536;
					std::vector<char> textArea(maxSize + 8192, 0);
					copybook.formatData(textArea.data(), maxSize, formatter.fileData, formatter.fileSize,
						options.charFormat, options.numericFormat, options.pdNumericFormat,
						formatter.fileCcsid, options.indent, options.checkData);
					out = "Level  Ofs  Len Type  Occ Variable Name                  Value\r\n";
					out += textArea.data();
				}
				return Result::success();
			}
		}

		RenderOptions defaultsFor(const Message & message, std::vector<std::string> & warnings)
```

In `core/format/rfh_format.cpp`, replace:

```cpp
			case View::Cobol:
				return Result::failure("the COBOL view is not supported yet");
```

with:

```cpp
			case View::Cobol:
				return renderCobol(formatter, options, out);
```

- [ ] **Step 5: Run the tests**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`
Expected: `100% tests passed, 0 tests failed out of 523`. The build shows `Copybook.cpp`'s three historic string-literal warnings.

- [ ] **Step 6: Commit**

```bash
git add tools/port/port-copybook.pl RFHUtil/Copybook.h RFHUtil/Copybook.cpp core/CMakeLists.txt core/format/rfh_format.cpp tests/core/FormatTests.cpp
git commit -s -m "feat(core): render the COBOL view with IBM's copy book parser

tools/port/port-copybook.pl makes RFHUtil/Copybook.cpp a shared source
(no CObject base, the portable trace, rfh_mbclen). render's COBOL view
parses the copy book named by the options, where Windows asks with a file
dialog, and fails when it cannot be read (Windows shows an empty view).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JfjP6hazi7FDx4h8gULaS5"
```

---

### Task 8: Fuzz every view under the guard allocator

**Files:**
- Modify: `tests/CMakeLists.txt`
- Test: `tests/core/FormatFuzzTests.cpp`

A seeded fuzz test found the memory bugs that Tasks 3 and 4 fix; it stays as a regression net. Run normally (500 messages) it checks that no view fails or hangs. The memcheck run (3000 messages, about 6 seconds) checks that none leaves its buffers. While this plan was written, 2,000 messages were enough to catch IBM's original `xmlsubs.cpp`; 1,000 were not.

**Superseded by review.** `defaultsFor` returns `DataFormats`, so the test applies the result with `RenderOptions::apply` instead of assigning it to a `RenderOptions` (see Task 5). The memcheck run uses 3000 messages (`FORMAT_FUZZ_ITERATIONS=3000`) and the plain run 500.

- [ ] **Step 1: Add the test**

Create `tests/core/FormatFuzzTests.cpp`:

```cpp
// FormatFuzzTests.cpp - every Data tab view over pseudo-random messages.
//
// The messages mix XML, JSON, EDI and FIX fragments with random bytes, so they
// reach the formatters' and IBM's parsers' error paths. Run normally, the test
// shows that no view crashes or hangs; under the guard allocator (ctest -L
// memcheck) it shows that none reads or writes outside its buffers. The seed is
// fixed, so a failure always reproduces. FORMAT_FUZZ_SEED picks another seed and
// FORMAT_FUZZ_ITERATIONS the number of messages (the memcheck run uses more).

#include <gtest/gtest.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "rfh_format.h"

using namespace rfh;
using namespace rfh::format;

namespace
{
    const char * const kFragments[] = {
        "<", ">", "</", "/>", "<a>", "</a>", "<b x=\"1\">", "=", "\"", "'", "<!--", "-->",
        "<!DOCTYPE x>", "<![CDATA[", "]]>", "&amp;", "&lt;", "<?xml version=\"1.0\"?>",
        "{", "}", "[", "]", ":", ",", "true", "null", "\"k\"", "1.5e3",
        "UNA:+.? '", "UNB+", "ISA*", "\x01", "8=FIX", "\r\n", "\n", " ", "\t",
        "\xff", "\xfe", "\xef\xbb\xbf", "x" };

    const View kViews[] = { View::Character, View::Hex, View::Both, View::Xml,
        View::Parsed, View::Cobol, View::Json, View::Fixed };

    const int kCcsids[] = { 819, 1208, 500, 37, 1200, 932, 1252 };

    class Random
    {
    public:
        explicit Random(unsigned int seed) : state(seed) {}
        unsigned int next()
        {
            state = state * 1103515245u + 12345u;
            return (state >> 16) & 0x7fff;
        }
    private:
        unsigned int state;
    };

    std::string writeCopybook()
    {
        std::string path = testing::TempDir() + "rfhutil_fuzz.cpy";
        FILE * f = fopen(path.c_str(), "w");
        if (f != NULL)
        {
            fputs("       01  REC.\n"
                  "           05  A   PIC X(3).\n"
                  "           05  B   PIC S9(5) COMP-3.\n"
                  "           05  C   PIC S9(4) COMP OCCURS 4.\n"
                  "           05  D   PIC 9(4)V99.\n", f);
            fclose(f);
        }
        return path;
    }
}

TEST(FormatFuzz, NoViewFailsOnArbitraryData)
{
    const char * seedText = getenv("FORMAT_FUZZ_SEED");
    Random random(seedText ? (unsigned int)atoi(seedText) : 12345u);
    std::string copybook = writeCopybook();
    const char * iterationsText = getenv("FORMAT_FUZZ_ITERATIONS");
    const int iterations = iterationsText ? atoi(iterationsText) : 500;

    for (int iteration = 0; iteration < iterations; iteration++)
    {
        std::string body;
        size_t target = random.next() % 3000;
        while (body.size() < target)
        {
            if (random.next() % 4 == 0)
            {
                body += (char)(random.next() & 0xff);
            }
            else
            {
                body += kFragments[random.next() % (sizeof(kFragments) / sizeof(kFragments[0]))];
            }
        }

        Message m;
        m.mqmd.CodedCharSetId = kCcsids[random.next() % (sizeof(kCcsids) / sizeof(kCcsids[0]))];
        m.mqmd.Encoding = (random.next() & 1) ? 546 : 785;
        m.body.assign(body.begin(), body.end());

        std::vector<std::string> warnings;
        RenderOptions o = defaultsFor(m, warnings);
        o.charFormat = (int)(random.next() % 9);
        o.crlf = random.next() & 1;
        o.edi = random.next() & 1;
        o.bom = random.next() & 1;
        o.indent = random.next() & 1;
        o.checkData = random.next() & 1;
        o.numericFormat = random.next() & 1;
        o.pdNumericFormat = random.next() & 1;
        o.copybookPath = copybook;
        if (random.next() & 1)
        {
            o.conversionTableDir = "/opt/mqm/lib/iconv";     // used when present
        }

        for (View view : kViews)
        {
            std::string out;
            Result r = render(m, view, o, out);
            ASSERT_TRUE(r.ok()) << "iteration " << iteration << ", view " << (int)view << ": " << r.text;
        }
    }
}
```

In `tests/CMakeLists.txt`, replace:

```cmake
    core/FormatTests.cpp)
```

with:

```cmake
    core/FormatTests.cpp core/FormatFuzzTests.cpp)
```

Append to `tests/CMakeLists.txt`:

```cmake
rfh_add_memcheck_test(memcheck.FormatFuzz rfhutil_core_tests "FormatFuzz.*" FORMAT_FUZZ_ITERATIONS=3000)
```

- [ ] **Step 2: Run the tests**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`
Expected: `100% tests passed, 0 tests failed out of 525`, with `memcheck.FormatFuzz` passing in about 6 seconds.

- [ ] **Step 3: Commit**

```bash
git add tests/core/FormatFuzzTests.cpp tests/CMakeLists.txt
git commit -s -m "test(core): fuzz every Data tab view under the guard allocator

FormatFuzzTests renders seeded pseudo-random messages, mixing XML, JSON,
EDI and FIX fragments with random bytes, through all eight views with
random settings. memcheck.FormatFuzz runs 3000 of them under the guard
allocator.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JfjP6hazi7FDx4h8gULaS5"
```

---

### Task 9: The Windows Data tab uses DataFormatter (Windows; needs the owner's Windows build)

**Files:**
- Create: `tools/port/port-dataarea-formatters.pl`
- Modify (by the script): `RFHUtil/DataArea.h`, `RFHUtil/DataArea.cpp`, `RFHUtil/RFHUtil.vcxproj`, `RFHUtil/RFHUtil.vcxproj.filters`, `Client/Client.vcxproj`, `Client/Client.vcxproj.filters`

**When to run this task:** only after the owner reports that Plan 1's Windows build works (RFHUtil, rfhutilc and UnitTests), and preferably after 2A's Task 6. Stacking untested Windows changes makes failures harder to place. If that report has not arrived, stop and report that this task is waiting.

**What changes:**
- `DataArea`'s seven view methods forward to a `DataFormatter` made for the current message. `resetDisplayData` deletes it, as it freed the views before, so views are cached exactly as they were. `formatter()` also rebuilds it if `fileData` or `fileSize` changed without a reset.
- `DataArea`'s copies of the moved methods are removed, with the dead `getCharDataUcs` and the members and work areas only they used. About 4,300 lines go.
- `DataFormatter.{h,cpp}` join both projects. `getCobolData` and `readCopybookFile` stay.

- [ ] **Step 1: Add and run the script**

Create `tools/port/port-dataarea-formatters.pl`:

```perl
#!/usr/bin/perl
# port-dataarea-formatters.pl - one-off edits for Plan 2B, Task 9: DataArea shows
# the Data tab views through DataFormatter (RFHUtil/DataFormatter.cpp) and drops
# its own copies of the formatters, so Windows and the portable core share one
# implementation.
#
# - the seven view methods (getCharacterData ... getFixData) forward to a
#   DataFormatter made for the current message; resetDisplayData deletes it, as it
#   freed the views before;
# - the methods that moved to DataFormatter, the dead getCharDataUcs, and the
#   members and work areas only they used are removed;
# - DataFormatter.{h,cpp} join the RFHUtil and Client projects.
#
# Every edit asserts its match count, every file is checked before any is written,
# and the result is checked for leftover references. Run once from the repository
# root; a second run fails on the first edit.
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

# Replaces the body of DataArea::name (signature kept) with new text.
sub body {
	my ($name, $new) = @_;
	return sub {
		${$_[0]} =~ s/^([A-Za-z][^\n;]*?\bDataArea::\Q$name\E\s*\((?:[^)]|\n)*\)\s*\n)\{.*?\n\}\n/$1$new/msg;
	};
}

# Removes the definition of DataArea::name, the comment block just before it and
# the blank lines after it.
sub remove {
	my ($name) = @_;
	return sub {
		${$_[0]} =~ s/^(?:\/{20,}\n(?:(?!\/{20,}\n)\/\/[^\n]*\n)*\/{20,}\n\n*)?[A-Za-z][^\n;]*?\bDataArea::\Q$name\E\s*\((?:[^)]|\n)*\)\s*\n\{.*?\n\}\n\n*//msg;
	};
}

sub line {
	my ($pattern) = @_;
	return sub { ${$_[0]} =~ s/^$pattern[^\n]*\n//mg };
}

my @moved = qw(getTranslatedData getNameValueData xlateEbcdicChar checkForUnicodeMarker
	findcrlf isLeadChar MQEBC2ASC MQEBC2UCS getCharDataUcs);
my @members = qw(MQCcsidLen MQUcsCcsid MQCcsidOut MQCcsidIn MQCcsidPtr MQUcsPtr
	saveEdi saveBOM saveParsedFormat saveXMLFormat saveBothFormat saveCharCrlf
	saveCharFormat saveJsonFormat saveFixFormat saveIndentOn invalidCharTable
	invalidCharTableNotBuilt m_data_ascii_UCS);
my @workAreas = qw(xmlVarName tempVarValue tempVarName tempEndVarName tempVarAttr xmlAttrStr);

my $view = sub {
	my ($call, $output, $error) = @_;
	return "{\n\tformatter()->$call;\n\t$output = m_formatter->$output;"
		. ($error ? "\n\tshowFormatterError();" : '') . "\n}\n";
};

my $helpers = <<'EOF';

//////////////////////////////////////////////////////////////
//
// The DataFormatter for the current message's Data tab views.
// It is made when the first view is shown and deleted by
// resetDisplayData, which runs whenever the message changes,
// so its views are kept and reused as DataArea's were.
//
//////////////////////////////////////////////////////////////

DataFormatter * DataArea::formatter()

{
	// a new message always passes through resetDisplayData; this also catches
	// data replaced without it
	if ((m_formatter != NULL) && ((m_formatter->source() != fileData) || (m_formatter->fileSize != fileSize)))
	{
		dropFormatter();
	}

	if (NULL == m_formatter)
	{
		CRfhutilApp *	app = (CRfhutilApp *)AfxGetApp();
		char			installDir[512] = { 0 };

		m_formatter = new DataFormatter();
		m_formatter->setData(fileData, fileSize);
		m_formatter->display.ansi = app->ansi;
		m_formatter->display.oem = app->oem;

		// MQ's conversion tables, which DataArea looked for under the install directory
		findMQInstallDirectory((unsigned char *)installDir, sizeof(installDir));
		if (installDir[0] != 0)
		{
			m_formatter->conversionTableDir = std::string(installDir) + "\\conv\\table";
		}
	}

	// the user may change these between views
	m_formatter->fileCcsid = fileCcsid;
	m_formatter->fileIntFormat = fileIntFormat;
	m_formatter->m_char_format = m_char_format;

	return m_formatter;
}

// Deletes the formatter and with it every view.
void DataArea::dropFormatter()

{
	delete m_formatter;
	m_formatter = NULL;
	m_data_ascii = NULL;
	m_data_hex = NULL;
	m_data_both = NULL;
	m_data_xml = NULL;
	m_data_parsed = NULL;
	m_data_json = NULL;
	m_data_fix = NULL;
}

// Shows an error the JSON or fixed view reported, as those views did in DataArea.
void DataArea::showFormatterError()

{
	if (!m_formatter->m_error_msg.empty())
	{
		m_error_msg = m_formatter->m_error_msg.c_str();
		m_formatter->m_error_msg.clear();
		updateMsgText();
	}
}
EOF

my $cpp = port('RFHUtil/DataArea.cpp',
	['include', 1, sub { ${$_[0]} =~ s/^(#include "jsonparse\.h"\n)/$1#include "DataFormatter.h"\n/mg }],
	['XML work areas', 1, sub { ${$_[0]} =~ s/^static char\txmlVarName\[8192 \+ 1024\];[^\n]*\n(?:static char\t\w+\[[^\]]*\];[^\n]*\n){5}\n//mg }],
	['constructor: view caches', 1, sub { ${$_[0]} =~ s/\tm_data_ascii_UCS = NULL;\n(\tm_data_hex = NULL;\n(?:\tm_data_\w+ = NULL;\n){5}\tbrowseActive = 0;\n\tbrowsePrevActive = 0;\n)\tsaveCharCrlf = IGNORE_CRLF;\n\tsaveCharFormat = CHAR_ASCII;\n\tsaveBothFormat = CHAR_ASCII;\n/$1\tm_formatter = NULL;\n/g }],
	['constructor: MQ tables', 1, sub { ${$_[0]} =~ s/\t\/\/ initialize the MQ CCSID table values\n\tMQCcsidIn = 0;\n\tMQCcsidOut = 0;\n\tMQCcsidLen = 0;\n\tMQCcsidPtr = NULL;\n\tMQUcsCcsid = 0;\n\tMQUcsPtr = NULL;\n\n//g }],
	['constructor: display table', 1, sub { ${$_[0]} =~ s/\tmemset\(invalidCharTable, '\.', sizeof\(invalidCharTable\)\);\n\tinvalidCharTableNotBuilt = TRUE;\n\n//g }],
	['freeAllocations: MQ tables', 1, sub { ${$_[0]} =~ s/\t\/\/ Was an MQ translation table used\?\n\tif \(MQCcsidPtr != NULL\)\n\t\{\n\t\t\/\/ release the allocated storage\n\t\trfhFree\(MQCcsidPtr\);\n\t\}\n\n\t\/\/ was an MQ to UCS-2 translation table used\?\n\tif \(MQUcsPtr != NULL\)\n\t\{\n\t\trfhFree\(MQUcsPtr\);\n\t\}\n\n//g }],
	['resetDisplayData: view settings', 1, sub { ${$_[0]} =~ s/(void DataArea::resetDisplayData\(\)\n\n\{\n)\tsaveCharCrlf = IGNORE_CRLF;\n\tsaveCharFormat = CHAR_ASCII;\n\tsaveBothFormat = CHAR_ASCII;\n\tsaveXMLFormat = CHAR_ASCII;\n\tsaveParsedFormat = CHAR_ASCII;\n\tsaveJsonFormat = CHAR_ASCII;\n\n/$1/g }],
	['resetDisplayData: views', 1, sub { ${$_[0]} =~ s/\tif \(m_data_ascii != NULL\)\n\t\{\n\t\trfhFree\(m_data_ascii\);\n.*?\tif \(m_data_fix != NULL\)\n\t\{\n\t\trfhFree\(m_data_fix\);\n\t\tm_data_fix = NULL;\n\t\}\n\}\n/\t\/\/ the views belong to the formatter\n\tdropFormatter();\n}\n/sg }],
	['getHexData', 1, body('getHexData', $view->('getHexData()', 'm_data_hex') . $helpers)],
	['getCharacterData', 1, body('getCharacterData', $view->('getCharacterData(charFormat, crlf, edi, BOM, indentOn)', 'm_data_ascii'))],
	['getBothData', 1, body('getBothData', $view->('getBothData(charFormat, BOM)', 'm_data_both'))],
	['getXmlData', 1, body('getXmlData', $view->('getXmlData(charFormat)', 'm_data_xml'))],
	['getJsonData', 1, body('getJsonData', $view->('getJsonData(charFormat)', 'm_data_json', 1))],
	['getFixData', 1, body('getFixData', $view->('getFixData(charFormat)', 'm_data_fix', 1))],
	['getParsedData', 1, body('getParsedData', $view->('getParsedData(charFormat)', 'm_data_parsed'))],
	map({ my $m = $_; ["remove $m", 1, remove($m)] } @moved),
);

port('RFHUtil/DataArea.h',
	['forward declaration', 1, sub { ${$_[0]} =~ s/^(class DataArea : public CObject\s*\n)/class DataFormatter;\n\n$1/mg }],
	map({ my $m = $_; ["declaration of $m", 1, line("\\t[A-Za-z][^\\n(]*\\b$m\\(")] } @moved),
	map({ my $m = $_; ["member $m", 1, line("\\t(?:int|BOOL|char|unsigned char \\*|wchar_t \\*)\\s*\\b$m\\b(?:\\[256\\])?;")] } @members),
	['formatter member', 1, sub { ${$_[0]} =~ s/^(\tCCopybook \*m_copybook;\n)/$1\tDataFormatter * m_formatter;\t\t\/\/ the Data tab views of the current message\n/mg }],
	['formatter methods', 1, sub { ${$_[0]} =~ s/^(\tvoid findMQInstallDirectory\(unsigned char \* dirName, unsigned long dirNameLen\);\n)/$1\tDataFormatter * formatter();\n\tvoid dropFormatter();\n\tvoid showFormatterError();\n/mg }],
);

# the projects: next to DataArea, in the same form as its entries
for my $project (['RFHUtil/RFHUtil.vcxproj', ''], ['Client/Client.vcxproj', '..\\RFHUtil\\']) {
	my ($path, $dir) = @$project;
	port($path,
		['source', 1, sub { ${$_[0]} =~ s/^(\s*)(<ClCompile Include="\Q${dir}\EDataArea\.cpp" \/>\n)/$1$2$1<ClCompile Include="${dir}DataFormatter.cpp" \/>\n/mg }],
		['header', 1, sub { ${$_[0]} =~ s/^(\s*)(<ClInclude Include="\Q${dir}\EDataArea\.h" \/>\n)/$1$2$1<ClInclude Include="${dir}DataFormatter.h" \/>\n/mg }],
	);
	port("$path.filters",
		['source', 1, sub { ${$_[0]} =~ s/^(\s*)(<ClCompile Include="\Q${dir}\EDataArea\.cpp">\n\s*<Filter>Source Files<\/Filter>\n\s*<\/ClCompile>\n)/$1$2$1<ClCompile Include="${dir}DataFormatter.cpp">\n$1  <Filter>Source Files<\/Filter>\n$1<\/ClCompile>\n/mg }],
		['header', 1, sub { ${$_[0]} =~ s/^(\s*)(<ClInclude Include="\Q${dir}\EDataArea\.h">\n\s*<Filter>Header Files<\/Filter>\n\s*<\/ClInclude>\n)/$1$2$1<ClInclude Include="${dir}DataFormatter.h">\n$1  <Filter>Header Files<\/Filter>\n$1<\/ClInclude>\n/mg }],
	);
}

# nothing left may use what was removed
(my $code = $$cpp) =~ s{//[^\n]*}{}g;
for my $id (@moved, @members, @workAreas) {
	my $n = () = $code =~ /\b\Q$id\E\b/g;
	die "RFHUtil/DataArea.cpp: $id is still used $n time(s)\n" if $n;
}

for my $p (@pending) {
	my ($path, $text, $crlf, $count) = @$p;
	$text =~ s/\n/\r\n/g if $crlf;
	open my $out, '>:raw', $path or die "$path: $!\n";
	print $out $text;
	close $out;
	printf "%-30s %2d edit(s)\n", $path, $count;
}
```

Run: `perl tools/port/port-dataarea-formatters.pl`
Expected:
```
RFHUtil/DataArea.cpp           24 edit(s)
RFHUtil/DataArea.h             31 edit(s)
RFHUtil/RFHUtil.vcxproj         2 edit(s)
RFHUtil/RFHUtil.vcxproj.filters  2 edit(s)
Client/Client.vcxproj           2 edit(s)
Client/Client.vcxproj.filters   2 edit(s)
```
Then check:
- `for f in RFHUtil/RFHUtil.vcxproj RFHUtil/RFHUtil.vcxproj.filters Client/Client.vcxproj Client/Client.vcxproj.filters; do xmllint --noout "$f" && echo "$f ok"; done` prints four `ok` lines.
- `git diff --stat` shows about 136 insertions and 4,369 deletions.
- A second run fails with `RFHUtil/DataArea.cpp: XML work areas: matched 0 time(s), expected 1`.

- [ ] **Step 2: Confirm Linux is unaffected**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`
Expected: `100% tests passed, 0 tests failed out of 525` (`DataArea` is not part of the Linux build).

- [ ] **Step 3: Commit**

```bash
git add tools/port/port-dataarea-formatters.pl RFHUtil/DataArea.h RFHUtil/DataArea.cpp RFHUtil/RFHUtil.vcxproj RFHUtil/RFHUtil.vcxproj.filters Client/Client.vcxproj Client/Client.vcxproj.filters
git commit -s -m "refactor(mfc): show the Data tab views through DataFormatter

DataArea's view methods forward to a DataFormatter made for the current
message and deleted by resetDisplayData, so Windows and the portable core
share one implementation. DataArea's copies of the formatters, the dead
getCharDataUcs and the state only they used are removed; DataFormatter
joins the RFHUtil and Client projects. Not compiled here: the owner
builds and checks it on Windows.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JfjP6hazi7FDx4h8gULaS5"
```

- [ ] **Step 4: Hand over the Windows checks to the owner**

Ask the owner to build `RFHUtil.sln` (RFHUtil, rfhutilc and UnitTests) and check:
1. It compiles without new errors. Likely problems: a missing include path for `core\platform` in one of the projects (C1083), or a leftover use of a removed member (C2065). Report the exact messages.
2. For a few messages (text, XML, JSON, FIX, EBCDIC from an MQMD with code page 500, and one of 2 KB or more), each of the eight views shows the same text as the previous build.
3. The views refresh after reading another file, after getting another message, and after an edit is written back (Allow Edit).
4. Find works in the XML, parsed, JSON and fixed views. It reads the view buffers, which now belong to the formatter.
5. The COBOL view still asks for a copy book and shows it.
6. The JSON view of `{]` shows "Error parsing user data" instead of hanging.

---

### Task 10: Documentation and the upstream report

**Files:**
- Modify: `CHANGELOG.md`, `docs/superpowers/specs/2026-10-15-linux-qt-port-design.md`, this plan's Execution notes
- Modify (not committed): the upstream issue draft. Its path is given in the task brief; the owner keeps a copy in `~/projects/mq-rfhutil-notes/upstream-issue-draft.md`.

- [ ] **Step 1: CHANGELOG**

Under the unreleased section, add:
- Added:
  - "The portable core renders the Data tab's eight views (`format::render`) with the Windows formatters, now shared as `RFHUtil/DataFormatter.cpp`, and gives the Windows default settings after a get or a file read (`format::defaultsFor`)."
  - "Memory-checked tests (`ctest -L memcheck`) using a guard-page allocator, and a fuzz test of every view."
- Fixed:
  - "IBM's JSON and XML helpers no longer overrun memory, recurse without limit or loop on malformed data."
  - "The both view no longer writes past its buffer for messages over 1040 bytes."
  - "Two smaller out-of-bounds reads in the fixed and character views."
- Add the Task 9 line only if Task 9 ran.

- [ ] **Step 2: Spec**

- In section 4, replace the "Plan 2B (not yet implemented)" `format` lines with a compact copy of the declarations in `core/format/rfh_format.h`: `View`, `RenderOptions`, both `defaultsFor` and `render`. Also name `DataFormatter` as the shared implementation.
- In section 9, add the differences in `rfh_format.h` and `DataFormatter.h`: the COBOL copy book comes from the options; the input and work buffers are padded; the both view is sized correctly; the fixed view's index is unsigned; the EDI skip is bounded; the display table is built when the formatter is made; and the two parser fixes.
- Keep the wording accurate to the code.

- [ ] **Step 3: Execution notes**

Append to this plan's Execution notes:
- the test count after each task;
- each review-fix commit, one line per task;
- whether Task 9 ran (otherwise record it as "pending: waiting for the owner's Windows build of Plan 1").

- [ ] **Step 4: Upstream issue draft**

Add the bugs listed under "Upstream bugs found while porting" to the draft, as separate entries numbered after the existing ten. For each, give the file, the function, the line number in IBM's current code (before this plan's changes), what is observed against what is expected, and the pinning test, or for the fixed ones the test that would fail without the fix. Do not commit the draft.

- [ ] **Step 5: Commit**

```bash
git add CHANGELOG.md docs/superpowers/specs/2026-10-15-linux-qt-port-design.md docs/superpowers/plans/2026-10-16-linux-port-plan-2b-data-views.md
git commit -s -m "docs: record Plan 2B in the changelog, spec and plan

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JfjP6hazi7FDx4h8gULaS5"
```

---

## Not in this plan

- Editing the Data tab and writing it back (upstream's P2.3 Character and Hex round trip). That belongs to the Qt Data tab, Plan 3.
- Showing UTF-8 and UTF-16 bodies as text. IBM disabled it, and Windows shows their bytes; the Qt app may decode them later as an improvement.
- Keeping a formatter between renders. `render` makes one per call; the Qt app can keep one per message, as Task 9's `DataArea` does, if large messages need it.
- Fixing the upstream behaviour bugs (`MQEBC2ASC`, `xlateEbcdicChar`'s fallback, JSON arrays); they are pinned and reported.

## Execution notes

Executed on `features/linux-qt-port` from `c8c4ea7` (this plan) with review after each task. The code blocks in Tasks 1-8 are the verified first-pass versions; where review changed them the commits below are authoritative.

Test counts (`ctest --preset linux-debug`; the baseline before this plan was 465, and the first-pass counts the plan quoted were 469, 478, 486, 490, 507, 518, 523 and 525):

| After | Tests |
|---|---|
| Task 1 | 469, then 479 after the hardening commit |
| Task 2 | 488, then 496 (`c5b9eb0` kept 496) |
| Task 3 | 504, 509, 511, then 512 (`18c5ff3`) |
| Task 4 | 516, 525, then 526 (`f320ff4`) |
| Task 5 | 543, then 552 |
| Task 6 | 563 |
| Task 7 | 567, 593, then 595 |
| Task 8 | 597 |

The final run is 597 tests, all passing; 8 are skipped (the guard allocator's tests and `DataFormatter.SetDataAllocatesExactlyTheDataAndItsPad`, which need the allocator preloaded and run under `ctest -L memcheck`, and the MQ client test). `ctest --preset linux-debug -L memcheck` runs 5 tests (`GuardMalloc`, `DataFormatter`, `Parsers`, `Format`, `FormatFuzz`), all passing.

Commits, oldest first:
- Task 1: `76ba44b` guard-page allocator; `3fe9671` hardening after review (huge sizes return NULL, mprotect failure, `pvalloc` and `free_sized`, a magic word aborts on foreign pointers, stronger death tests).
- Task 2: `daef2f9` Win32 text conversions; `7da70d3` (flush iconv, CP932 not IBM932, surrogate pairs, `usedDefaultChar`, the five undefined bytes of 1252, `MB_PRECOMPOSED`, `_snprintf_s`); `c5b9eb0` (an unmappable pair gives two default characters; `rfh_convert_char` reimplemented on the two new functions).
- Task 3: `0839066` JSON and XML fixes; `b02463f` (reads at `endPtr`, out of memory, `parse()` twice, `processAttributes` takes an output size and the four `DataArea.cpp` call sites pass it); `86bdb72` (`buildParsedArea` output overrun on valid JSON); `18c5ff3` (output that fits is identical to IBM's).
- Task 4: `8842a0f` DataFormatter; `160e20a` (bounded copies in the parsed view, the table directory length, an exact `setData` allocation, the UCS-2 fallback loop); `f320ff4` (the end tag work area has room for '/').
- Task 5: `650b2eb` render and `defaultsFor`; `7d2c91d` (`charFormat` from the repaired code page, `DataFormats` and `RenderOptions::apply`, `DisplayCodepages` moved to `rfh_codepage.h`, the `View` `static_assert`).
- Task 6: `73bad07` pinning tests.
- Task 7: `71e52de` COBOL view; `8798ba8` (`tools/port/harden-copybook.pl` on IBM's copy book parser, 22 tests); `3d403e4` (`renderCobol` buffer limit and distinct failure messages, unique test copy book files); `6f0bf6d` (`displayDat` hang fix and the documented occurrence difference).
- Task 8: `ad1895f` seeded fuzz test, plus `memcheck.FormatFuzz` (3000 messages).
- Task 9: pending, waiting for the owner's Windows build of Plan 1 (as 2A's Task 6 waits). Its script was dry-run on `ad1895f` in a scratch copy: `DataArea.cpp` 24 edits, `DataArea.h` 31, the two `.vcxproj` and two `.vcxproj.filters` files 2 each; `DataArea.cpp` goes from 25,801 to 21,574 lines; the four project files pass `xmllint`, braces balance and a second run refuses. Nothing of Task 9 is in the repository, and nothing was compiled.
- Task 10: documentation (this commit). The upstream issue draft (entries 11 onwards) is kept outside the repository.
