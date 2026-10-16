# RFHUtil Linux Port, Plan 2A: Message Files and MQMD Fields Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Read and write RFHUtil message files byte-for-byte compatibly with Windows. Convert between the MQMD tab's fields and an MQMD, and separate MQ headers from message bodies, all in the portable core.

**Architecture:**

The Windows file and MQMD logic is ported into the core, and the core becomes its one owner:
- `mqsubs.cpp` becomes a shared source.
- `MqmdFields` replaces `MQMDPAGE::buildMQMD` and `processMQMD`.
- `walkHeaders` replaces the chain logic of `DataArea::parseMsgHeaders`.
- `fileio::read` and `fileio::write` replace `DataArea::ReadFileData` and `WriteFile`.

Windows behaviour is kept, including quirks, which tests pin. A short list of deliberate differences is spelled out below. `Message` gains raw `headers`, which `MqSession` separates on get and joins on put. The MFC MQMD tab then delegates to the core, so there is one implementation.

**Tech Stack:** C++17, CMake, GoogleTest 1.17, IBM MQ 9.4 C MQI, Perl 5 (one-off edit scripts).

**Spec:** `docs/superpowers/specs/2026-10-15-linux-qt-port-design.md`. **Previous plan:** `docs/superpowers/plans/2026-10-15-linux-port-plan-1-portable-core.md`, which is complete at `7febad7`.
Plan 2 was split at the owner's request. 2A, this plan, covers files, the MQMD and headers. 2B covers the eight display formatters.

**Verified before writing:** every code block, script and expected count in Tasks 1–5 was compiled and run on Fedora 44 against the branch at `7febad7`, including the live MQ test against `TEST_QM`. That gave 406 tests passing and the client test skipped. Task 6's MFC changes cannot be compiled here: its script was run, and the projects it edits pass `xmllint`.

---

## Background the engineer needs

- **Message file format.**
  - A file is the raw message image: an optional MQMD, then MQ headers, then the body.
  - Windows writes the MQMD as `sizeof(MQMD)` = 364 bytes, version 2, in native (x86, little-endian) byte order.
  - MQMD has no pointers, so the layout is the same on 64-bit Windows and Linux, which makes byte-for-byte compatibility possible.
- **Reading a file.**
  - The reader recognises an MQMD only when the file holds at least 364 bytes, even for a version 1 MQMD (324 bytes).
  - Without an MQMD, a DLQ, CICS, IMS, RFH1 or RFH2 header at the front is detected.
  - Otherwise, the data may be guessed to be EBCDIC.
- **Headers follow the format chain.** After the MQMD, each header names the format of what follows it (`MQHRF2`, `MQDEAD`, `MQCICS`, `MQIMS`, `MQHRF`, `MQHMDE`).
  - Lengths: DLQ is 172 bytes. CICS is 164 or 180 bytes depending on its version, plus an 8-byte program name when `LinkType` is program. IMS is 84 bytes. RFH1 and RFH2 use their `StrucLength`.
  - Integers in a header are byte-swapped when the incoming encoding is host. Ids and formats are translated from EBCDIC when the incoming code page is EBCDIC.
  - An invalid header ends the chain.
- **Encodings.** "Encoding" in the tab and in `DataArea` means `NUMERIC_PC` (0) or `NUMERIC_HOST` (1), not an `MQENC` value. `mqsubs.cpp` converts between them.
- **Windows quirks kept on purpose, each pinned by a test:**
  - Application fields that fill their whole MQMD field (32-character `ApplIdentityData` and so on) are dropped.
  - A short date/time text leaves zero bytes.
  - The format text stops at the first blank or control character.
  - Application-defined message types are not shown.
  - `MqmdFields::fromMqmd` leaves the float encoding unchanged.
  - An RFH1 needs name/value data.
  - The same header type twice in a row stops the chain.
  - `getFloatEncode` decodes wrongly; see the upstream bugs below.
- **Deliberate differences from Windows (owner decisions, documented in headers and in the spec):**
  1. A big-endian or EBCDIC MQMD is normalised with `translateMQMD`, which keeps MsgId, CorrelId and GroupId unchanged. Windows' `processMQMD` byte-reverses them.
  2. An MQMDE is read at its real offset, and it is byte-swapped only for host data. Windows uses `(MQMDE *)fileData + curOfs` and always swaps.
  3. `toMqmd` truncates over-long fields. Windows overruns into the next MQMD field (Format → Priority).
  4. Headers are written back exactly as read. Windows rebuilds them from the header tabs, which are not ported yet.
  5. The "keep headers when reading" (save RFH) option is not supported yet.
- **Upstream bugs found in this plan:**
  - Reported, not fixed:
    - `getFloatEncode` tests bits instead of `& MQENC_FLOAT_MASK`.
    - `processMQMD` reverses ids.
    - In `parseMsgHeaders`, the MQMDE pointer arithmetic is wrong, and it stores a raw `MQENC` value in its NUMERIC variable.
    - `processMQMDE` always swaps.
    - CICS version 2 checks the program-name length against the version 1 size.
  - These add to Plan 1's list (`case 037:`, two `char1208toWCS` bugs, `WCSto1252`).
- **Commits.** Use `git commit -s`, a Conventional Commit subject, and end the message with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Do not push; the owner pushes.

## File structure

| Path | Status | Responsibility |
|---|---|---|
| `RFHUtil/rfhconst.h` | new (by script) | Display/character/numeric/persistence/report constants and code-page defaults, moved verbatim from `rfhutil.h` |
| `RFHUtil/rfhutil.h`, `mqsubs.h`, `mqsubs.cpp` | modified (by script) | `rfhutil.h` includes `rfhconst.h`; `mqsubs` is self-contained and loses the dead `findMQInstallDirectory` |
| `core/message/rfh_mqmd_fields.{h,cpp}` | new | `MqmdFields` and `mqmd_fields::toMqmd` / `fromMqmd` / `clear` / `msgTypeText` |
| `core/message/rfh_header_chain.{h,cpp}` | new | `walkHeaders`, `detectLeadingHeader`, `looksLikeEbcdic` |
| `core/message/rfh_message.h` | modified | `Message` gains `headers` |
| `core/fileio/rfh_message_file.{h,cpp}` | new | `fileio::read` / `fileio::write` |
| `core/mq/rfh_mq_session.{h,cpp}` | modified | put joins headers and body; get separates them |
| `core/CMakeLists.txt`, `tests/CMakeLists.txt` | modified | New sources and tests |
| `tests/core/mq_layout.h` | new | Builds MQ structures byte by byte at IBM's offsets (golden data) |
| `tests/core/MqsubsTests.cpp`, `MqmdFieldsTests.cpp`, `HeaderChainTests.cpp`, `MessageFileTests.cpp` | new | Unit tests (Linux CMake only) |
| `tests/core/MqSessionTests.cpp` | modified | Live RFH2 header test |
| `RFHUtil/MQMDPAGE.{h,cpp}`, `RFHUtil/RFHUtil.vcxproj`, `Client/Client.vcxproj` | modified (by script) | The MFC tab delegates its file path to the core |
| `tools/port/port-mqsubs.pl`, `tools/port/port-mqmdpage.pl` | new | One-off scripted edits, which can be re-applied |

---

### Task 1: Make mqsubs portable and give the constants an MFC-free home

**Files:**
- Create: `tools/port/port-mqsubs.pl`
- Create (by the script): `RFHUtil/rfhconst.h`
- Modify (by the script): `RFHUtil/rfhutil.h`, `RFHUtil/mqsubs.h`, `RFHUtil/mqsubs.cpp`
- Modify: `core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/core/MqsubsTests.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/core/MqsubsTests.cpp`:

```cpp
// MqsubsTests.cpp — tests for the MQ helper functions in RFHUtil/mqsubs.cpp,
// which became portable in Plan 2A. Linux CMake only: the Windows UnitTests
// project has no MQ headers.

#include <gtest/gtest.h>

#include <string.h>

#include <cmqc.h>

#include "rfh_platform.h"
#include "rfhconst.h"
#include "comsubs.h"
#include "mqsubs.h"

TEST(Mqsubs_CcsidType, EbcdicCodePagesAreEbcdic)
{
    EXPECT_EQ(CHAR_EBCDIC, getCcsidType(37));
    EXPECT_EQ(CHAR_EBCDIC, getCcsidType(500));
    EXPECT_EQ(CHAR_EBCDIC, getCcsidType(1047));
    EXPECT_EQ(CHAR_EBCDIC, getCcsidType(20273));
}

TEST(Mqsubs_CcsidType, AsianCodePagesHaveTheirOwnTypes)
{
    EXPECT_EQ(CHAR_TRAD_CHIN, getCcsidType(950));
}

TEST(Mqsubs_CcsidType, AsciiAndUnicodeAreAscii)
{
    EXPECT_EQ(CHAR_ASCII, getCcsidType(819));
    EXPECT_EQ(CHAR_ASCII, getCcsidType(1252));
    EXPECT_EQ(CHAR_ASCII, getCcsidType(1208));
}

TEST(Mqsubs_Encoding, PcEncodingIs546)
{
    // integer reversed (2), decimal reversed (0x20), float IEEE reversed (0x200)
    EXPECT_EQ(546, getEncodingValue(NUMERIC_PC, NUMERIC_PC, FLOAT_PC));
}

TEST(Mqsubs_Encoding, HostEncodingIs785)
{
    // integer normal (1), decimal normal (0x10), float S/390 (0x300)
    EXPECT_EQ(785, getEncodingValue(NUMERIC_HOST, NUMERIC_HOST, FLOAT_390));
}

TEST(Mqsubs_Encoding, UnixEncodingIs273)
{
    // integer normal (1), decimal normal (0x10), float IEEE normal (0x100)
    EXPECT_EQ(273, getEncodingValue(NUMERIC_HOST, NUMERIC_HOST, FLOAT_UNIX));
}

TEST(Mqsubs_Encoding, IntegerAndDecimalDecodeBack)
{
    EXPECT_EQ(NUMERIC_PC, getIntEncode(546));
    EXPECT_EQ(NUMERIC_PC, getPDEncode(546));
    EXPECT_EQ(NUMERIC_HOST, getIntEncode(785));
    EXPECT_EQ(NUMERIC_HOST, getPDEncode(785));
}

TEST(Mqsubs_Encoding, FloatDecodeIsInvertedHistoric)
{
    // Upstream bug, kept for compatibility; reported upstream: getFloatEncode tests
    // single bits instead of comparing (encoding & MQENC_FLOAT_MASK). IEEE_NORMAL
    // decodes as FLOAT_PC and IEEE_REVERSED as FLOAT_UNIX, the opposite of
    // getEncodingValue, and S/390 (0x300, both bits) also decodes as FLOAT_PC.
    EXPECT_EQ(FLOAT_UNIX, getFloatEncode(getEncodingValue(NUMERIC_PC, NUMERIC_PC, FLOAT_PC)));
    EXPECT_EQ(FLOAT_PC, getFloatEncode(getEncodingValue(NUMERIC_HOST, NUMERIC_HOST, FLOAT_UNIX)));
    EXPECT_EQ(FLOAT_PC, getFloatEncode(785));
    EXPECT_EQ(FLOAT_TNS, getFloatEncode(0x411));
}

TEST(Mqsubs_ApplType, KnownTypesHaveNumberAndName)
{
    EXPECT_STREQ("6 Unix", getApplTypeDisplay(MQAT_UNIX));
    EXPECT_STREQ("28 Java", getApplTypeDisplay(MQAT_JAVA));
    EXPECT_STREQ("11 32-bit Windows", getApplTypeDisplay(MQAT_WINDOWS_NT));
}

TEST(Mqsubs_ApplType, UnknownTypeIsEmpty)
{
    EXPECT_STREQ("", getApplTypeDisplay(65536));
}

TEST(Mqsubs_TranslateMqmd, NativeMqmdIsAcceptedUnchanged)
{
    MQMD2 md = { MQMD2_DEFAULT };
    md.Version = MQMD_VERSION_2;
    md.CodedCharSetId = 1208;
    memcpy(md.MsgId, "ABCDEFGHIJKLMNOPQRSTUVWX", 24);

    MQMD2 copy = md;
    EXPECT_TRUE(translateMQMD(&copy));
    EXPECT_EQ(0, memcmp(&md, &copy, sizeof(md)));
}

TEST(Mqsubs_TranslateMqmd, BigEndianMqmdIsNormalisedAndIdsKept)
{
    MQMD2 md = { MQMD2_DEFAULT };
    md.Version = reverseBytes4(MQMD_VERSION_2);
    md.CodedCharSetId = reverseBytes4(500);
    md.Encoding = reverseBytes4(785);
    md.MsgSeqNumber = reverseBytes4(7);
    memcpy(md.MsgId, "ABCDEFGHIJKLMNOPQRSTUVWX", 24);

    EXPECT_TRUE(translateMQMD(&md));
    EXPECT_EQ(MQMD_VERSION_2, md.Version);
    EXPECT_EQ(500, md.CodedCharSetId);
    EXPECT_EQ(785, md.Encoding);
    EXPECT_EQ(7, md.MsgSeqNumber);
    EXPECT_EQ(0, memcmp(md.MsgId, "ABCDEFGHIJKLMNOPQRSTUVWX", 24)) << "byte-string ids are never reversed";
}

TEST(Mqsubs_TranslateMqmd, EbcdicMqmdTextFieldsBecomeAscii)
{
    MQMD2 md = { MQMD2_DEFAULT };
    md.Version = reverseBytes4(MQMD_VERSION_2);
    AsciiToEbcdic((unsigned char *)MQMD_STRUC_ID, 4, (unsigned char *)md.StrucId);
    AsciiToEbcdic((unsigned char *)"MQSTR   ", 8, (unsigned char *)md.Format);

    EXPECT_TRUE(translateMQMD(&md));
    EXPECT_EQ(0, memcmp(md.Format, "MQSTR   ", 8));
}

TEST(Mqsubs_TranslateMqmd, NonMqmdIsRejected)
{
    MQMD2 md = { MQMD2_DEFAULT };
    memcpy(md.StrucId, "XXXX", 4);
    EXPECT_FALSE(translateMQMD(&md));

    MQMD2 badVersion = { MQMD2_DEFAULT };
    badVersion.Version = 3;
    EXPECT_FALSE(translateMQMD(&badVersion));
}
```

In `tests/CMakeLists.txt`, change the core test executable line:

```cmake
add_executable(rfhutil_core_tests core/ResultTests.cpp core/MqsubsTests.cpp)
```

- [ ] **Step 2: Run the build to verify it fails**

```bash
cmake --build --preset linux-debug
```

Expected: FAIL. `MqsubsTests.cpp` cannot find `rfhconst.h`.

- [ ] **Step 3: Add and run the port script**

The script makes these edits:
- Moves the constant blocks from `rfhutil.h` into `rfhconst.h`. These are "Display format types" through `SEGMENT_LAST`, plus "code page defaults" and "encoding types". `rfhutil.h` then includes `rfhconst.h`.
- Makes `mqsubs.h` include `rfh_platform.h` for `BOOL`.
- Deletes `findMQInstallDirectory` and `MQINSKEY` from `mqsubs`. The function has no callers, because `DataArea` uses its own member function of the same name, and it reads the Windows registry.
- Applies nothing unless every edit matches.

Create `tools/port/port-mqsubs.pl`:

```perl
#!/usr/bin/perl
# port-mqsubs.pl - makes RFHUtil/mqsubs.cpp portable and gives the display and
# encoding constants a home that does not depend on MFC.
# Run after port-shared-sources.pl and port-windows-projects.pl.
# Usage: perl port-mqsubs.pl <repo-root>
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

my %out;	# path => [text, crlf]; written only after every edit succeeded

# ---------------------------------------------------------------- rfhutil.h -> rfhconst.h
# The display, character, numeric, persistence and report constants move verbatim
# from the MFC-only rfhutil.h into rfhconst.h, which rfhutil.h now includes.
{
    my $f = 'RFHUtil/rfhutil.h'; my ($t, $crlf) = load($f);
    my ($block, $defaults);
    edit($f, 'constants block', 1, scalar($t =~ s/^(\/\/ Display format types\n.*?#define SEGMENT_LAST\t2\n)/#include "rfhconst.h"\n/ms and do { $block = $1; 1 }));
    edit($f, 'defaults block', 1, scalar($t =~ s/^(\/\/ code page defaults\n.*?#define DEF_UNIX_ENCODING\t\t\t273\n)\n//ms and do { $defaults = $1; 1 }));
    $block .= "\n" . $defaults;
    $out{$f} = [$t, $crlf];

    my ($licence) = slurp('RFHUtil/mqsubs.cpp') =~ /\A(\/\*.*?\*\/\n)/s;
    die "RFHUtil/mqsubs.cpp: licence block not found\n" unless $licence;
    $out{'RFHUtil/rfhconst.h'} = [$licence
        . "\n// rfhconst.h - display, character, numeric, persistence and report constants,\n"
        . "// moved from rfhutil.h so that portable sources can use them without MFC.\n\n"
        . "#pragma once\n\n"
        . $block, 0];
}

# ---------------------------------------------------------------- mqsubs.h / mqsubs.cpp
{
    # findMQInstallDirectory reads the Windows registry and has no callers: DataArea
    # uses its own member function of the same name.
    my $f = 'RFHUtil/mqsubs.h'; my ($t, $crlf) = load($f);
    edit($f, 'remove findMQInstallDirectory decl', 1, scalar($t =~ s/^void findMQInstallDirectory\(char \*dirName, int dirNameLen\);\n//m));
    # self-contained: the declarations use BOOL
    edit($f, 'include rfh_platform.h', 1, scalar($t =~ s/^(#include "cmqc\.h"\n)/#include "rfh_platform.h"\n$1/m));
    $out{$f} = [$t, $crlf];

    $f = 'RFHUtil/mqsubs.cpp'; ($t, $crlf) = load($f);
    edit($f, 'include rfhconst.h', 1, scalar($t =~ s/^(#include "rfhutil\.h"\n)/$1#include "rfhconst.h"\n/m));
    edit($f, 'remove MQINSKEY', 1, scalar($t =~ s/^#define MQINSKEY [^\n]*\n//m));
    edit($f, 'remove findMQInstallDirectory', 1, scalar($t =~ s/\n\/\*{20,}\/\n(?:\/\*[^\n]*\*\/\n){4}\/\*{20,}\/\n\nvoid findMQInstallDirectory\(char \*dirName, int dirNameLen\)\n.*?\n\}\n//s));
    $out{$f} = [$t, $crlf];
}

for my $f (sort keys %out)
{
    my ($t, $crlf) = @{$out{$f}};
    $t =~ s/\n/\r\n/g if $crlf;
    spew($f, $t);
}

print "mqsubs ported\n";
```

```bash
perl tools/port/port-mqsubs.pl .
grep -c "findMQInstallDirectory\|MQINSKEY" RFHUtil/mqsubs.cpp RFHUtil/mqsubs.h
```

Expected: `mqsubs ported`, then `0` for both files.

- [ ] **Step 4: Add mqsubs to the core library**

In `core/CMakeLists.txt`, add one line to `RFHUTIL_SHARED_SOURCES`, after `mqerror.cpp`:

```cmake
    ${PROJECT_SOURCE_DIR}/RFHUtil/mqsubs.cpp
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug
```

Expected: `100% tests passed, 0 tests failed out of 354`. That is 340 plus the 14 new tests, with the client test skipped.

- [ ] **Step 6: Commit**

```bash
git add tools/port/port-mqsubs.pl RFHUtil/rfhconst.h RFHUtil/rfhutil.h RFHUtil/mqsubs.h RFHUtil/mqsubs.cpp core/CMakeLists.txt tests/CMakeLists.txt tests/core/MqsubsTests.cpp
git commit -s -m "refactor(core): make mqsubs portable and move shared constants to rfhconst.h

The display, character, numeric, persistence, report and code page
constants move verbatim from the MFC-only rfhutil.h, which includes the
new header. mqsubs.cpp joins the shared sources; its unused,
registry-reading findMQInstallDirectory is removed. Tests pin the
getFloatEncode upstream bug.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: MQMD tab fields ⇄ MQMD

**Files:**
- Create: `core/message/rfh_mqmd_fields.h`, `core/message/rfh_mqmd_fields.cpp`
- Create: `tests/core/mq_layout.h`
- Modify: `core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/core/MqmdFieldsTests.cpp`

- [ ] **Step 1: Write the golden-data helper and the failing test**

`mq_layout.h` builds MQ structures byte by byte at IBM's documented offsets. It does not use the `cmqc.h` structs, so the expected bytes are independent of the code under test. Later tasks use its header builders too.

Create `tests/core/mq_layout.h`:

```cpp
// mq_layout.h - builds IBM MQ structures byte by byte, at the offsets IBM documents,
// for golden-file tests. It deliberately does not use the cmqc.h structs, so the
// expected bytes are independent of the code under test.
#pragma once

#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

namespace mq_layout
{
	enum class Order { Little, Big };

	class Bytes
	{
	public:
		explicit Bytes(size_t size, unsigned char fill = 0) : data(size, fill) {}

		void putLong(size_t offset, int32_t value, Order order = Order::Little)
		{
			uint32_t v = (uint32_t)value;
			for (int i = 0; i < 4; i++)
			{
				int shift = (order == Order::Little) ? 8 * i : 8 * (3 - i);
				data.at(offset + i) = (unsigned char)(v >> shift);
			}
		}

		// Blank-filled character field.
		void putChars(size_t offset, size_t length, const std::string & text)
		{
			for (size_t i = 0; i < length; i++)
			{
				data.at(offset + i) = (i < text.size()) ? (unsigned char)text[i] : ' ';
			}
		}

		void putBytes(size_t offset, const void * bytes, size_t length)
		{
			memcpy(&data.at(offset), bytes, length);
		}

		std::vector<unsigned char> data;
	};

	// MQMD version 2 (364 bytes) with the MQMD2_DEFAULT values, little-endian.
	inline Bytes defaultMqmd2(Order order = Order::Little)
	{
		Bytes b(364);
		b.putChars(0, 4, "MD  ");
		b.putLong(4, 2, order);				// Version
		b.putLong(8, 0, order);				// Report MQRO_NONE
		b.putLong(12, 8, order);			// MsgType MQMT_DATAGRAM
		b.putLong(16, -1, order);			// Expiry MQEI_UNLIMITED
		b.putLong(20, 0, order);			// Feedback MQFB_NONE
		b.putLong(24, 546, order);			// Encoding MQENC_NATIVE (x86)
		b.putLong(28, 0, order);			// CodedCharSetId MQCCSI_Q_MGR
		b.putChars(32, 8, "");				// Format MQFMT_NONE
		b.putLong(40, -1, order);			// Priority MQPRI_PRIORITY_AS_Q_DEF
		b.putLong(44, 2, order);			// Persistence MQPER_PERSISTENCE_AS_Q_DEF
		// MsgId 48, CorrelId 72: zeros
		b.putLong(96, 0, order);			// BackoutCount
		b.putChars(100, 48, "");			// ReplyToQ
		b.putChars(148, 48, "");			// ReplyToQMgr
		b.putChars(196, 12, "");			// UserIdentifier
		// AccountingToken 208: zeros
		b.putChars(240, 32, "");			// ApplIdentityData
		b.putLong(272, 0, order);			// PutApplType MQAT_NO_CONTEXT
		b.putChars(276, 28, "");			// PutApplName
		// PutDate 304 and PutTime 312: zeros in the C MQMD2_DEFAULT
		b.putChars(320, 4, "");				// ApplOriginData
		// GroupId 324: zeros
		b.putLong(348, 1, order);			// MsgSeqNumber
		b.putLong(352, 0, order);			// Offset
		b.putLong(356, 0, order);			// MsgFlags MQMF_NONE
		b.putLong(360, -1, order);			// OriginalLength MQOL_UNDEFINED
		return b;
	}

	inline std::vector<unsigned char> ebcdic(const std::string & ascii)
	{
		// the subset needed for structure ids and format names
		static const char * letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
		static const unsigned char codes[] = {
			0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xD1, 0xD2, 0xD3, 0xD4,
			0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
			0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0x40 };
		std::vector<unsigned char> out;
		for (char c : ascii)
		{
			out.push_back(codes[strchr(letters, c) - letters]);
		}
		return out;
	}

	// The fixed 36-byte part of an MQRFH2 followed by nameValues (whose length the
	// caller keeps a multiple of 4). Version 2, StrucLength 36 + nameValues.size().
	inline Bytes rfh2(Order order, int32_t encoding, int32_t ccsid, const std::string & format,
	                  const std::string & nameValues = std::string(), bool ebcdicText = false)
	{
		Bytes b(36 + nameValues.size());
		if (ebcdicText)
		{
			b.putBytes(0, ebcdic("RFH ").data(), 4);
			std::string padded = format + std::string(8 - format.size(), ' ');
			b.putBytes(20, ebcdic(padded).data(), 8);
		}
		else
		{
			b.putChars(0, 4, "RFH ");
			b.putChars(20, 8, format);
		}
		b.putLong(4, 2, order);
		b.putLong(8, (int32_t)(36 + nameValues.size()), order);
		b.putLong(12, encoding, order);
		b.putLong(16, ccsid, order);
		b.putLong(28, 0, order);			// Flags
		b.putLong(32, 1208, order);			// NameValueCCSID
		if (!nameValues.empty())
		{
			b.putBytes(36, nameValues.data(), nameValues.size());
		}
		return b;
	}

	// MQRFH (version 1): 32 fixed bytes plus nameValues.
	inline Bytes rfh1(Order order, int32_t encoding, int32_t ccsid, const std::string & format, const std::string & nameValues)
	{
		Bytes b(32 + nameValues.size());
		b.putChars(0, 4, "RFH ");
		b.putLong(4, 1, order);
		b.putLong(8, (int32_t)(32 + nameValues.size()), order);
		b.putLong(12, encoding, order);
		b.putLong(16, ccsid, order);
		b.putChars(20, 8, format);
		b.putLong(28, 0, order);
		if (!nameValues.empty())
		{
			b.putBytes(32, nameValues.data(), nameValues.size());
		}
		return b;
	}

	// MQDLH (172 bytes, version 1).
	inline Bytes dlh(Order order, int32_t encoding, int32_t ccsid, const std::string & format)
	{
		Bytes b(172);
		b.putChars(0, 4, "DLH ");
		b.putLong(4, 1, order);
		b.putLong(8, 2085, order);			// Reason
		b.putChars(12, 48, "ORIG.Q");
		b.putChars(60, 48, "QM1");
		b.putLong(108, encoding, order);
		b.putLong(112, ccsid, order);
		b.putChars(116, 8, format);
		b.putLong(124, 6, order);			// PutApplType
		b.putChars(128, 28, "app");
		b.putChars(156, 8, "20261016");
		b.putChars(164, 8, "12000000");
		return b;
	}

	// MQCIH: version 1 is 164 bytes, version 2 is 180; LinkType at 52.
	inline Bytes cih(Order order, int32_t version, int32_t linkType, int32_t encoding, int32_t ccsid, const std::string & format)
	{
		Bytes b(version == 1 ? 164 : 180);
		b.putChars(0, 4, "CIH ");
		b.putLong(4, version, order);
		b.putLong(8, (int32_t)b.data.size(), order);
		b.putLong(12, encoding, order);
		b.putLong(16, ccsid, order);
		b.putChars(20, 8, format);
		b.putLong(52, linkType, order);
		return b;
	}

	// MQIIH (84 bytes, version 1).
	inline Bytes iih(Order order, int32_t encoding, int32_t ccsid, const std::string & format)
	{
		Bytes b(84);
		b.putChars(0, 4, "IIH ");
		b.putLong(4, 1, order);
		b.putLong(8, 84, order);
		b.putLong(12, encoding, order);
		b.putLong(16, ccsid, order);
		b.putChars(20, 8, format);
		return b;
	}

	// MQMDE (72 bytes, version 2).
	inline Bytes mde(Order order, int32_t encoding, int32_t ccsid, const std::string & format,
	                 int32_t seq, int32_t offset, int32_t flags, int32_t originalLength, const std::string & groupId)
	{
		Bytes b(72);
		b.putChars(0, 4, "MDE ");
		b.putLong(4, 2, order);
		b.putLong(8, 72, order);
		b.putLong(12, encoding, order);
		b.putLong(16, ccsid, order);
		b.putChars(20, 8, format);
		b.putLong(28, 0, order);
		b.putBytes(32, groupId.data(), 24);
		b.putLong(56, seq, order);
		b.putLong(60, offset, order);
		b.putLong(64, flags, order);
		b.putLong(68, originalLength, order);
		return b;
	}

	inline std::vector<unsigned char> join(std::initializer_list<std::vector<unsigned char>> parts)
	{
		std::vector<unsigned char> out;
		for (const auto & p : parts)
		{
			out.insert(out.end(), p.begin(), p.end());
		}
		return out;
	}

	inline std::vector<unsigned char> text(const std::string & s)
	{
		return std::vector<unsigned char>(s.begin(), s.end());
	}

	namespace mqmd
	{
		const size_t Report = 8, MsgType = 12, Expiry = 16, Feedback = 20, Encoding = 24,
			CodedCharSetId = 28, Format = 32, Priority = 40, Persistence = 44, MsgId = 48,
			CorrelId = 72, BackoutCount = 96, ReplyToQ = 100, ReplyToQMgr = 148,
			UserIdentifier = 196, AccountingToken = 208, ApplIdentityData = 240,
			PutApplType = 272, PutApplName = 276, PutDate = 304, PutTime = 312,
			ApplOriginData = 320, GroupId = 324, MsgSeqNumber = 348, Offset = 352,
			MsgFlags = 356, OriginalLength = 360, Length2 = 364, Length1 = 324;
	}
}
```

Create `tests/core/MqmdFieldsTests.cpp`:

```cpp
// MqmdFieldsTests.cpp — tests for rfh::mqmd_fields, the port of
// MQMDPAGE::buildMQMD / processMQMD. Expected MQMD bytes come from mq_layout.h,
// which lays the structure out at IBM's documented offsets.

#include <gtest/gtest.h>

#include <string.h>

#include <cmqc.h>

#include "rfh_mqmd_fields.h"
#include "mq_layout.h"

using rfh::MqmdFields;
namespace L = mq_layout;
namespace O = mq_layout::mqmd;

namespace
{
	std::vector<unsigned char> bytesOf(const MQMD2 & md)
	{
		const unsigned char * p = reinterpret_cast<const unsigned char *>(&md);
		return std::vector<unsigned char>(p, p + sizeof(MQMD2));
	}

	MQMD2 built(const MqmdFields & f)
	{
		MQMD2 md = { MQMD2_DEFAULT };
		md.Version = MQMD_VERSION_2;
		rfh::mqmd_fields::toMqmd(f, md);
		return md;
	}
}

TEST(MqmdFields, StructureMatchesDocumentedLayout)
{
	ASSERT_EQ(L::mqmd::Length2, sizeof(MQMD2));
	ASSERT_EQ(L::mqmd::Length1, sizeof(MQMD1));
}

TEST(MqmdFields, DefaultFieldsGiveDocumentedDefaultsExceptTabValues)
{
	// An untouched MQMD tab sets persistence "No", PC encodings, backout 0,
	// sequence 1, original length -1, and blank text fields.
	L::Bytes expected = L::defaultMqmd2();
	expected.putLong(O::Persistence, 0);			// MQPER_NOT_PERSISTENT
	expected.putLong(O::Encoding, 546);

	EXPECT_EQ(expected.data, bytesOf(built(MqmdFields())));
}

TEST(MqmdFields, EveryFieldLandsAtItsOffset)
{
	MqmdFields f;
	f.format = "MQSTR";
	f.ccsid = "1208";
	f.msgType = "1 Request";
	f.expiry = "6000";
	f.feedback = "0";
	f.priority = "4";
	f.persistence = MQPERSIST_YES;
	f.backoutCount = "2";
	f.replyToQ = "REPLY.Q";
	f.replyToQMgr = "QM2";
	f.userId = "tyron";
	f.accountingToken = "0102";
	f.applIdentity = "ident";
	f.applOrigin = "ori";
	f.putApplName = "rfhutil";
	f.putApplType = "6 Unix";
	f.dateTime = "2026/10/16 13:45:07.89";
	f.seqNo = "3";
	f.offset = "100";
	f.originalLength = "500";
	f.encoding = NUMERIC_HOST;
	f.pdEncoding = NUMERIC_HOST;
	f.floatEncoding = FLOAT_390;
	f.reportCoa = REPORT_FULL;
	f.reportPan = true;
	f.groupYes = true;
	f.segmentLast = true;
	memcpy(f.msgId, "MSGID-123456789012345678", 24);
	memcpy(f.correlId, "CORRELID-123456789012345", 24);
	memcpy(f.groupId, "GROUPID-1234567890123456", 24);

	L::Bytes e = L::defaultMqmd2();
	e.putLong(O::Report, MQRO_COA_WITH_FULL_DATA | MQRO_PAN);
	e.putLong(O::MsgType, 1);
	e.putLong(O::Expiry, 6000);
	e.putLong(O::Feedback, 0);
	e.putLong(O::Encoding, 785);
	e.putLong(O::CodedCharSetId, 1208);
	e.putChars(O::Format, 8, "MQSTR");
	e.putLong(O::Priority, 4);
	e.putLong(O::Persistence, 1);
	e.putBytes(O::MsgId, "MSGID-123456789012345678", 24);
	e.putBytes(O::CorrelId, "CORRELID-123456789012345", 24);
	e.putLong(O::BackoutCount, 2);
	e.putChars(O::ReplyToQ, 48, "REPLY.Q");
	e.putChars(O::ReplyToQMgr, 48, "QM2");
	e.putChars(O::UserIdentifier, 12, "tyron");
	const unsigned char token[2] = { 0x01, 0x02 };
	e.putBytes(O::AccountingToken, token, 2);
	e.putChars(O::ApplIdentityData, 32, "ident");
	e.putLong(O::PutApplType, 6);
	e.putChars(O::PutApplName, 28, "rfhutil");
	e.putChars(O::PutDate, 8, "20261016");
	e.putChars(O::PutTime, 8, "13450789");
	e.putChars(O::ApplOriginData, 4, "ori");
	e.putBytes(O::GroupId, "GROUPID-1234567890123456", 24);
	e.putLong(O::MsgSeqNumber, 3);
	e.putLong(O::Offset, 100);
	e.putLong(O::MsgFlags, MQMF_MSG_IN_GROUP | MQMF_LAST_SEGMENT);
	e.putLong(O::OriginalLength, 500);

	EXPECT_EQ(e.data, bytesOf(built(f)));
}

TEST(MqmdFields, EmptyTextFieldsKeepTheGivenMqmdValues)
{
	MQMD2 md = { MQMD2_DEFAULT };
	md.Expiry = 77;
	md.Priority = 9;
	md.CodedCharSetId = 437;
	MqmdFields f;
	f.backoutCount.clear();
	rfh::mqmd_fields::toMqmd(f, md);
	EXPECT_EQ(77, md.Expiry);
	EXPECT_EQ(9, md.Priority);
	EXPECT_EQ(437, md.CodedCharSetId);
}

TEST(MqmdFields, UnknownMsgTypeIsIgnoredApplicationTypeIsKept)
{
	MqmdFields f;
	f.msgType = "5";
	EXPECT_EQ(MQMT_DATAGRAM, built(f).MsgType);
	f.msgType = "65536";
	EXPECT_EQ(65536, built(f).MsgType);
}

TEST(MqmdFields, FullLengthApplicationFieldsAreDroppedHistoric)
{
	// Windows tests "length < field size", so a value that fills the field is dropped.
	MqmdFields f;
	f.applIdentity = std::string(32, 'I');
	f.applOrigin = "ABCD";
	f.putApplName = std::string(27, 'N');
	MQMD2 md = built(f);
	EXPECT_EQ(0, memcmp(md.ApplIdentityData, std::string(32, ' ').data(), 32));
	EXPECT_EQ(0, memcmp(md.ApplOriginData, "    ", 4));
	EXPECT_EQ(0, memcmp(md.PutApplName, std::string(27, 'N').data(), 27));
}

TEST(MqmdFields, ShortDateTimeLeavesZeroBytesHistoric)
{
	MqmdFields f;
	f.dateTime = "2026/10";
	MQMD2 md = built(f);
	EXPECT_EQ(0, memcmp(md.PutDate, "202610\0\0", 8));
	EXPECT_EQ(0, memcmp(md.PutTime, "\0\0\0\0\0\0\0\0", 8));
}

TEST(MqmdFields, LongFieldsAreTruncated)
{
	MqmdFields f;
	f.format = "TOOLONGFORMAT";
	f.userId = "averyveryverylonguser";
	MQMD2 md = built(f);
	EXPECT_EQ(0, memcmp(md.Format, "TOOLONGF", 8));
	EXPECT_EQ(0, memcmp(md.UserIdentifier, "averyveryver", 12));
	EXPECT_EQ(-1, md.Priority) << "the format must not overrun into Priority";
}

TEST(MqmdFields, ShortFormatOverwritesOnlyItsBytes)
{
	MQMD2 md = { MQMD2_DEFAULT };
	memcpy(md.Format, "MQHRF2  ", 8);
	MqmdFields f;
	f.format = "MQ";
	rfh::mqmd_fields::toMqmd(f, md);
	EXPECT_EQ(0, memcmp(md.Format, "MQHRF2  ", 8)) << "Windows copies without blank filling";
}

TEST(MqmdFields, FromMqmdReadsEveryField)
{
	L::Bytes e = L::defaultMqmd2();
	e.putLong(O::Report, MQRO_EXCEPTION_WITH_DATA | MQRO_COD | MQRO_PASS_MSG_ID | MQRO_NAN);
	e.putLong(O::MsgType, 2);
	e.putLong(O::Expiry, 100);
	e.putLong(O::Encoding, 273);
	e.putLong(O::CodedCharSetId, 819);
	e.putChars(O::Format, 8, "MQSTR");
	e.putLong(O::Priority, 7);
	e.putLong(O::Persistence, 1);
	e.putBytes(O::MsgId, "abcdefghijklmnopqrstuvwx", 24);
	e.putChars(O::ReplyToQ, 48, "R.Q");
	e.putChars(O::UserIdentifier, 12, "me");
	const unsigned char token[3] = { 0xAB, 0x00, 0x7F };
	e.putBytes(O::AccountingToken, token, 3);
	e.putLong(O::PutApplType, 28);
	e.putChars(O::PutApplName, 28, "app");
	e.putChars(O::PutDate, 8, "20261016");
	e.putChars(O::PutTime, 8, "01020304");
	e.putLong(O::MsgSeqNumber, 5);
	e.putLong(O::MsgFlags, MQMF_SEGMENTATION_ALLOWED);
	e.putLong(O::OriginalLength, 42);

	MQMD2 md;
	memcpy(&md, e.data.data(), sizeof(md));
	MqmdFields f;
	ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));

	EXPECT_EQ(REPORT_DATA, f.reportException);
	EXPECT_EQ(REPORT_YES, f.reportCod);
	EXPECT_EQ(REPORT_NONE, f.reportCoa);
	EXPECT_TRUE(f.reportPassMsgId);
	EXPECT_TRUE(f.reportNan);
	EXPECT_FALSE(f.reportPan);
	EXPECT_EQ("2 Reply", f.msgType);
	EXPECT_EQ("100", f.expiry);
	EXPECT_EQ(NUMERIC_HOST, f.encoding);
	EXPECT_EQ(NUMERIC_HOST, f.pdEncoding);
	EXPECT_EQ("819", f.ccsid);
	EXPECT_EQ("MQSTR", f.format);
	EXPECT_EQ("7", f.priority);
	EXPECT_EQ(MQPERSIST_YES, f.persistence);
	EXPECT_EQ(0, memcmp(f.msgId, "abcdefghijklmnopqrstuvwx", 24));
	EXPECT_EQ("R.Q", f.replyToQ);
	EXPECT_EQ("me", f.userId);
	EXPECT_EQ("AB007F" + std::string(58, '0'), f.accountingToken);
	EXPECT_EQ("28 Java", f.putApplType);
	EXPECT_EQ("app", f.putApplName);
	EXPECT_EQ("2026/10/16 01:02:03.04", f.dateTime);
	EXPECT_EQ("5", f.seqNo);
	EXPECT_TRUE(f.segmentAllowed);
	EXPECT_FALSE(f.groupYes);
	EXPECT_EQ("42", f.originalLength);
}

TEST(MqmdFields, FromMqmdKeepsFloatEncodingAsOnWindows)
{
	MQMD2 md = { MQMD2_DEFAULT };
	MqmdFields f;
	f.floatEncoding = FLOAT_TNS;
	ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));
	EXPECT_EQ(FLOAT_TNS, f.floatEncoding);
}

TEST(MqmdFields, FromMqmdVersion1LeavesVersion2FieldsCleared)
{
	MQMD2 md = { MQMD2_DEFAULT };
	md.Version = MQMD_VERSION_1;
	md.MsgSeqNumber = 9;
	md.MsgFlags = MQMF_MSG_IN_GROUP;
	MqmdFields f;
	f.groupYes = true;
	f.seqNo = "4";
	ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));
	EXPECT_EQ("1", f.seqNo);
	EXPECT_FALSE(f.groupYes);
	EXPECT_EQ("-1", f.originalLength);
}

TEST(MqmdFields, FromMqmdRejectsOtherVersions)
{
	MQMD2 md = { MQMD2_DEFAULT };
	md.Version = 3;
	MqmdFields f;
	f.ccsid = "unchanged";
	EXPECT_FALSE(rfh::mqmd_fields::fromMqmd(md, f));
	EXPECT_EQ("unchanged", f.ccsid);
}

TEST(MqmdFields, FormatStopsAtFirstControlOrBlankHistoric)
{
	MQMD2 md = { MQMD2_DEFAULT };
	memcpy(md.Format, "MQ STR  ", 8);
	MqmdFields f;
	ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));
	EXPECT_EQ("MQ", f.format);
}

TEST(MqmdFields, ApplicationDefinedMsgTypeIsNotShownHistoric)
{
	EXPECT_EQ("", rfh::mqmd_fields::msgTypeText(65536));
	EXPECT_EQ("8 Datagram", rfh::mqmd_fields::msgTypeText(8));
}

TEST(MqmdFields, RoundTripThroughFieldsIsByteIdentical)
{
	L::Bytes e = L::defaultMqmd2();
	e.putLong(O::Report, MQRO_EXPIRATION_WITH_FULL_DATA | MQRO_ACTIVITY);
	e.putLong(O::MsgType, 4);
	e.putLong(O::Expiry, 12);
	e.putLong(O::Feedback, 3);
	e.putLong(O::CodedCharSetId, 1208);
	e.putChars(O::Format, 8, "MQHRF2");
	e.putLong(O::Priority, 0);
	e.putLong(O::Persistence, 0);
	e.putBytes(O::CorrelId, "123456789012345678901234", 24);
	e.putChars(O::ReplyToQMgr, 48, "QMX");
	e.putChars(O::ApplIdentityData, 32, "id");
	e.putLong(O::PutApplType, 11);
	e.putChars(O::PutApplName, 28, "prog");
	e.putChars(O::PutDate, 8, "20251231");
	e.putChars(O::PutTime, 8, "23595999");
	e.putChars(O::ApplOriginData, 4, "or");
	e.putLong(O::MsgSeqNumber, 1);
	e.putLong(O::OriginalLength, -1);

	MQMD2 in;
	memcpy(&in, e.data.data(), sizeof(in));
	MqmdFields f;
	ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(in, f));
	EXPECT_EQ(e.data, bytesOf(built(f)));
}
```

In `tests/CMakeLists.txt`, change the line to:

```cmake
add_executable(rfhutil_core_tests core/ResultTests.cpp core/MqsubsTests.cpp core/MqmdFieldsTests.cpp)
```

- [ ] **Step 2: Run the build to verify it fails**

```bash
cmake --build --preset linux-debug
```

Expected: FAIL. `rfh_mqmd_fields.h` is not found.

- [ ] **Step 3: Write MqmdFields**

Create `core/message/rfh_mqmd_fields.h`:

```cpp
// rfh_mqmd_fields.h - the fields of the MQMD tab and their conversion to and
// from an MQMD.
//
// MqmdFields holds what the MQMD tab shows and edits: text for numeric fields
// (so an empty field means "leave the MQMD default"), option values for radio
// groups, flags for check boxes, and the three identifiers as raw bytes.
// mqmd_fields::toMqmd is the port of MQMDPAGE::buildMQMD and fromMqmd is the
// field-setting part of MQMDPAGE::processMQMD; both keep the Windows results,
// including their quirks, so files and messages built from the same fields are
// byte-identical on both platforms.
#pragma once

#include <cmqc.h>

#include <string>

#include "rfhconst.h"

namespace rfh
{
	struct MqmdFields
	{
		// text fields, exactly as shown on the MQMD tab
		std::string format;			// up to 8 characters, e.g. "MQSTR"
		std::string ccsid;			// e.g. "1208"
		std::string msgType;		// e.g. "8 Datagram"; only the leading number is used
		std::string expiry;
		std::string feedback;
		std::string priority;
		std::string backoutCount = "0";
		std::string replyToQ;
		std::string replyToQMgr;
		std::string userId;
		std::string accountingToken;	// hex, two characters per byte
		std::string applIdentity;
		std::string applOrigin;
		std::string putApplName;
		std::string putApplType;	// e.g. "6 Unix"; only the leading number is used
		std::string dateTime;		// "YYYY/MM/DD HH:MM:SS.hh"
		std::string seqNo = "1";
		std::string offset;
		std::string originalLength = "-1";	// MQOL_UNDEFINED

		// radio groups
		int persistence = MQPERSIST_NO;		// MQPERSIST_NO / MQPERSIST_YES / MQPERSIST_AS_Q
		int encoding = NUMERIC_PC;			// NUMERIC_PC / NUMERIC_HOST
		int pdEncoding = NUMERIC_PC;
		int floatEncoding = FLOAT_PC;		// FLOAT_PC / FLOAT_UNIX / FLOAT_390 / FLOAT_TNS
		int reportException = REPORT_NONE;	// REPORT_NONE / REPORT_YES / REPORT_DATA / REPORT_FULL
		int reportExpiration = REPORT_NONE;
		int reportCoa = REPORT_NONE;
		int reportCod = REPORT_NONE;

		// check boxes
		bool reportPan = false;
		bool reportNan = false;
		bool reportActivity = false;
		bool reportDiscard = false;
		bool reportPassMsgId = false;
		bool reportPassCorrelId = false;
		bool reportPassDiscardAndExpiry = false;
		bool groupYes = false;
		bool groupLast = false;
		bool segmentYes = false;
		bool segmentLast = false;
		bool segmentAllowed = false;

		// identifiers, as raw bytes
		MQBYTE24 msgId = { 0 };
		MQBYTE24 correlId = { 0 };
		MQBYTE24 groupId = { 0 };
	};

	namespace mqmd_fields
	{
		// Fills mqmd from fields, as MQMDPAGE::buildMQMD does. mqmd must already hold
		// the values to keep for empty text fields (normally MQMD2_DEFAULT). Fields
		// longer than their MQMD field are truncated; Windows overruns into the next
		// MQMD field instead (Format, ReplyToQ, ReplyToQMgr).
		void toMqmd(const MqmdFields & fields, MQMD2 & mqmd);

		// Sets fields from a native-encoding MQMD of version 1 or 2, as
		// MQMDPAGE::processMQMD does after its byte-order and EBCDIC translation.
		// The float encoding is left unchanged, as on Windows. Returns false, leaving
		// fields unchanged, for any other version.
		bool fromMqmd(const MQMD2 & mqmd, MqmdFields & fields);

		// Resets fields as MQMDPAGE::clearMQMD does. The float encoding and the three
		// identifiers are left unchanged, as on Windows.
		void clear(MqmdFields & fields);

		// "8 Datagram" for 8, "" for types the MQMD tab does not name (including
		// application-defined types), as MQMDPAGE::setMsgType does.
		std::string msgTypeText(MQLONG msgType);
	}
}
```

Create `core/message/rfh_mqmd_fields.cpp`:

```cpp
// rfh_mqmd_fields.cpp - see rfh_mqmd_fields.h

#include "rfh_mqmd_fields.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "rfh_platform.h"
#include "comsubs.h"
#include "mqsubs.h"

namespace rfh
{
	namespace mqmd_fields
	{
		namespace
		{
			// Copies text into a blank-filled MQ character field, at most fieldLength bytes.
			void copyBlankFilled(MQCHAR * field, size_t fieldLength, const std::string & text)
			{
				memset(field, ' ', fieldLength);
				memcpy(field, text.data(), std::min(text.size(), fieldLength));
			}

			// An MQ character field as text, with trailing blanks and controls removed.
			std::string trimmedField(const MQCHAR * field, size_t fieldLength)
			{
				char temp[64];
				memset(temp, 0, sizeof(temp));
				memcpy(temp, field, std::min(fieldLength, sizeof(temp) - 1));
				Rtrim(temp);
				return temp;
			}

			std::string number(MQLONG value)
			{
				char temp[16];
				snprintf(temp, sizeof(temp), "%d", (int)value);
				return temp;
			}

			MQLONG reportBits(int choice, MQLONG yes, MQLONG data, MQLONG full)
			{
				switch (choice)
				{
				case REPORT_YES: return yes;
				case REPORT_DATA: return data;
				case REPORT_FULL: return full;
				default: return 0;
				}
			}

			int reportChoice(MQLONG report, MQLONG yes, MQLONG data, MQLONG full)
			{
				if ((report & yes) == 0)
				{
					return REPORT_NONE;
				}
				if ((report & full) == full)
				{
					return REPORT_FULL;
				}
				if ((report & data) == data)
				{
					return REPORT_DATA;
				}
				return REPORT_YES;
			}

			// MQMDPAGE::buildReportOptions
			MQLONG buildReportOptions(const MqmdFields & f)
			{
				MQLONG report = 0;
				report |= reportBits(f.reportException, MQRO_EXCEPTION, MQRO_EXCEPTION_WITH_DATA, MQRO_EXCEPTION_WITH_FULL_DATA);
				report |= reportBits(f.reportExpiration, MQRO_EXPIRATION, MQRO_EXPIRATION_WITH_DATA, MQRO_EXPIRATION_WITH_FULL_DATA);
				report |= reportBits(f.reportCoa, MQRO_COA, MQRO_COA_WITH_DATA, MQRO_COA_WITH_FULL_DATA);
				report |= reportBits(f.reportCod, MQRO_COD, MQRO_COD_WITH_DATA, MQRO_COD_WITH_FULL_DATA);
				if (f.reportPan) report |= MQRO_PAN;
				if (f.reportNan) report |= MQRO_NAN;
				if (f.reportPassCorrelId) report |= MQRO_PASS_CORREL_ID;
				if (f.reportPassMsgId) report |= MQRO_PASS_MSG_ID;
				if (f.reportActivity) report |= MQRO_ACTIVITY;
				if (f.reportDiscard) report |= MQRO_DISCARD_MSG;
				if (f.reportPassDiscardAndExpiry) report |= MQRO_PASS_DISCARD_AND_EXPIRY;
				return report;
			}

			// MQMDPAGE::buildFlags
			MQLONG buildFlags(const MqmdFields & f)
			{
				MQLONG flags = MQMF_NONE;
				if (f.groupYes) flags |= MQMF_MSG_IN_GROUP;
				if (f.groupLast) flags |= MQMF_LAST_MSG_IN_GROUP;
				if (f.segmentYes) flags |= MQMF_SEGMENT;
				if (f.segmentLast) flags |= MQMF_LAST_SEGMENT;
				if (f.segmentAllowed) flags |= MQMF_SEGMENTATION_ALLOWED;
				return flags;
			}

			// MQMDPAGE::getPersistence
			MQLONG persistenceValue(int persistence)
			{
				switch (persistence)
				{
				case MQPERSIST_NO: return MQPER_NOT_PERSISTENT;
				case MQPERSIST_YES: return MQPER_PERSISTENT;
				default: return MQPER_PERSISTENCE_AS_Q_DEF;
				}
			}

			bool acceptedMsgType(int msgType)
			{
				return (MQMT_DATAGRAM == msgType) ||
				       (MQMT_REQUEST == msgType) ||
				       (MQMT_REPLY == msgType) ||
				       (MQMT_REPORT == msgType) ||
				       (MQMT_MQE_FIELDS_FROM_MQE == msgType) ||
				       (MQMT_MQE_FIELDS == msgType) ||
				       ((msgType >= MQMT_APPL_FIRST) && (msgType <= MQMT_APPL_LAST));
			}

		}

		void clear(MqmdFields & f)
		{
			MqmdFields cleared;
			cleared.floatEncoding = f.floatEncoding;	// clearMQMD leaves it alone
			memcpy(cleared.msgId, f.msgId, sizeof(cleared.msgId));
			memcpy(cleared.correlId, f.correlId, sizeof(cleared.correlId));
			memcpy(cleared.groupId, f.groupId, sizeof(cleared.groupId));
			f = cleared;
		}

		void toMqmd(const MqmdFields & f, MQMD2 & mqmd)
		{
			mqmd.Report = buildReportOptions(f);

			if (!f.msgType.empty())
			{
				int msgType = atoi(f.msgType.c_str());
				if (acceptedMsgType(msgType))
				{
					mqmd.MsgType = msgType;
				}
			}

			if (!f.expiry.empty())
			{
				mqmd.Expiry = atoi(f.expiry.c_str());
			}

			if (!f.feedback.empty())
			{
				mqmd.Feedback = atoi(f.feedback.c_str());
			}

			mqmd.Encoding = getEncodingValue(f.encoding, f.pdEncoding, f.floatEncoding);

			if (!f.ccsid.empty())
			{
				mqmd.CodedCharSetId = atoi(f.ccsid.c_str());
			}

			// Windows copies the format over whatever the MQMD held, without blank filling
			if (!f.format.empty())
			{
				memcpy(mqmd.Format, f.format.data(), std::min(f.format.size(), sizeof(mqmd.Format)));
			}

			mqmd.MsgFlags = buildFlags(f);

			if (!f.priority.empty())
			{
				mqmd.Priority = atoi(f.priority.c_str());
			}

			mqmd.Persistence = persistenceValue(f.persistence);

			memcpy(mqmd.MsgId, f.msgId, sizeof(mqmd.MsgId));
			memcpy(mqmd.CorrelId, f.correlId, sizeof(mqmd.CorrelId));
			memcpy(mqmd.GroupId, f.groupId, sizeof(mqmd.GroupId));

			if (!f.backoutCount.empty())
			{
				mqmd.BackoutCount = atoi(f.backoutCount.c_str());
			}

			copyBlankFilled(mqmd.ReplyToQ, sizeof(mqmd.ReplyToQ), f.replyToQ);
			copyBlankFilled(mqmd.ReplyToQMgr, sizeof(mqmd.ReplyToQMgr), f.replyToQMgr);
			copyBlankFilled(mqmd.UserIdentifier, sizeof(mqmd.UserIdentifier), f.userId);

			if (!f.dateTime.empty())
			{
				// "YYYY/MM/DD HH:MM:SS.hh"; a shorter text leaves zero bytes, as on Windows
				char temp[32];
				memset(temp, 0, sizeof(temp));
				memcpy(temp, f.dateTime.data(), std::min(f.dateTime.size(), sizeof(temp) - 1));
				memcpy(mqmd.PutDate, temp, 4);
				memcpy(mqmd.PutDate + 4, temp + 5, 2);
				memcpy(mqmd.PutDate + 6, temp + 8, 2);
				memcpy(mqmd.PutTime, temp + 11, 2);
				memcpy(mqmd.PutTime + 2, temp + 14, 2);
				memcpy(mqmd.PutTime + 4, temp + 17, 2);
				memcpy(mqmd.PutTime + 6, temp + 20, 2);
			}

			if (!f.accountingToken.empty())
			{
				unsigned int len = (unsigned int)std::min(f.accountingToken.size() / 2, sizeof(mqmd.AccountingToken));
				std::string hex = f.accountingToken;
				HexToAscii((unsigned char *)&hex[0], len, mqmd.AccountingToken);
			}

			// Windows drops these three when they fill the whole field (it tests "<", not "<=")
			memset(mqmd.ApplIdentityData, ' ', sizeof(mqmd.ApplIdentityData));
			if (!f.applIdentity.empty() && (f.applIdentity.size() < MQ_APPL_IDENTITY_DATA_LENGTH))
			{
				memcpy(mqmd.ApplIdentityData, f.applIdentity.data(), f.applIdentity.size());
			}

			memset(mqmd.ApplOriginData, ' ', sizeof(mqmd.ApplOriginData));
			if (!f.applOrigin.empty() && (f.applOrigin.size() < MQ_APPL_ORIGIN_DATA_LENGTH))
			{
				memcpy(mqmd.ApplOriginData, f.applOrigin.data(), f.applOrigin.size());
			}

			memset(mqmd.PutApplName, ' ', sizeof(mqmd.PutApplName));
			if (!f.putApplName.empty() && (f.putApplName.size() < MQ_APPL_NAME_LENGTH))
			{
				memcpy(mqmd.PutApplName, f.putApplName.data(), f.putApplName.size());
			}

			if (!f.putApplType.empty())
			{
				mqmd.PutApplType = atoi(f.putApplType.c_str());
			}

			if (!f.seqNo.empty())
			{
				mqmd.MsgSeqNumber = atoi(f.seqNo.c_str());
			}

			if (!f.offset.empty())
			{
				mqmd.Offset = atoi(f.offset.c_str());
			}

			if (!f.originalLength.empty())
			{
				mqmd.OriginalLength = atoi(f.originalLength.c_str());
			}
		}

		bool fromMqmd(const MQMD2 & mqmd, MqmdFields & f)
		{
			if ((MQMD_VERSION_1 != mqmd.Version) && (MQMD_VERSION_2 != mqmd.Version))
			{
				return false;
			}

			clear(f);

			f.reportException = reportChoice(mqmd.Report, MQRO_EXCEPTION, MQRO_EXCEPTION_WITH_DATA, MQRO_EXCEPTION_WITH_FULL_DATA);
			f.reportExpiration = reportChoice(mqmd.Report, MQRO_EXPIRATION, MQRO_EXPIRATION_WITH_DATA, MQRO_EXPIRATION_WITH_FULL_DATA);
			f.reportCoa = reportChoice(mqmd.Report, MQRO_COA, MQRO_COA_WITH_DATA, MQRO_COA_WITH_FULL_DATA);
			f.reportCod = reportChoice(mqmd.Report, MQRO_COD, MQRO_COD_WITH_DATA, MQRO_COD_WITH_FULL_DATA);
			f.reportPan = (mqmd.Report & MQRO_PAN) != 0;
			f.reportNan = (mqmd.Report & MQRO_NAN) != 0;
			f.reportActivity = (mqmd.Report & MQRO_ACTIVITY) != 0;
			f.reportDiscard = (mqmd.Report & MQRO_DISCARD_MSG) != 0;
			f.reportPassDiscardAndExpiry = (mqmd.Report & MQRO_PASS_DISCARD_AND_EXPIRY) != 0;
			f.reportPassMsgId = (mqmd.Report & MQRO_PASS_MSG_ID) != 0;
			f.reportPassCorrelId = (mqmd.Report & MQRO_PASS_CORREL_ID) != 0;

			f.msgType = msgTypeText(mqmd.MsgType);
			f.expiry = number(mqmd.Expiry);
			f.feedback = number(mqmd.Feedback);
			f.encoding = getIntEncode(mqmd.Encoding);
			f.pdEncoding = getPDEncode(mqmd.Encoding);
			f.ccsid = number(mqmd.CodedCharSetId);

			// Windows turns every byte at or below a blank into the end of the text
			char format[MQ_FORMAT_LENGTH + 1];
			memcpy(format, mqmd.Format, MQ_FORMAT_LENGTH);
			format[MQ_FORMAT_LENGTH] = 0;
			for (int i = 0; i < MQ_FORMAT_LENGTH; i++)
			{
				if ((unsigned char)format[i] <= ' ')
				{
					format[i] = 0;
				}
			}
			f.format = format;

			f.priority = number(mqmd.Priority);
			f.persistence = mqmd.Persistence;	// MQPERSIST_* equal MQPER_* numerically
			memcpy(f.msgId, mqmd.MsgId, sizeof(f.msgId));
			memcpy(f.correlId, mqmd.CorrelId, sizeof(f.correlId));
			memcpy(f.groupId, mqmd.GroupId, sizeof(f.groupId));
			f.backoutCount = number(mqmd.BackoutCount);
			f.replyToQ = trimmedField(mqmd.ReplyToQ, sizeof(mqmd.ReplyToQ));
			f.replyToQMgr = trimmedField(mqmd.ReplyToQMgr, sizeof(mqmd.ReplyToQMgr));
			f.userId = trimmedField(mqmd.UserIdentifier, sizeof(mqmd.UserIdentifier));

			unsigned char token[2 * sizeof(mqmd.AccountingToken) + 1] = { 0 };
			AsciiToHex(mqmd.AccountingToken, sizeof(mqmd.AccountingToken), token);
			f.accountingToken = (const char *)token;

			f.applIdentity = trimmedField(mqmd.ApplIdentityData, sizeof(mqmd.ApplIdentityData));
			f.putApplType = getApplTypeDisplay(mqmd.PutApplType);
			f.putApplName = trimmedField(mqmd.PutApplName, sizeof(mqmd.PutApplName));

			char dateTime[24];
			memcpy(dateTime, mqmd.PutDate, 4);
			dateTime[4] = '/';
			memcpy(dateTime + 5, mqmd.PutDate + 4, 2);
			dateTime[7] = '/';
			memcpy(dateTime + 8, mqmd.PutDate + 6, 2);
			dateTime[10] = ' ';
			memcpy(dateTime + 11, mqmd.PutTime, 2);
			dateTime[13] = ':';
			memcpy(dateTime + 14, mqmd.PutTime + 2, 2);
			dateTime[16] = ':';
			memcpy(dateTime + 17, mqmd.PutTime + 4, 2);
			dateTime[19] = '.';
			memcpy(dateTime + 20, mqmd.PutTime + 6, 2);
			dateTime[22] = 0;
			f.dateTime = dateTime;

			f.applOrigin = trimmedField(mqmd.ApplOriginData, sizeof(mqmd.ApplOriginData));

			if (MQMD_VERSION_2 == mqmd.Version)
			{
				f.seqNo = number(mqmd.MsgSeqNumber);
				f.offset = number(mqmd.Offset);
				f.groupYes = (mqmd.MsgFlags & MQMF_MSG_IN_GROUP) != 0;
				f.groupLast = (mqmd.MsgFlags & MQMF_LAST_MSG_IN_GROUP) != 0;
				f.segmentYes = (mqmd.MsgFlags & MQMF_SEGMENT) != 0;
				f.segmentLast = (mqmd.MsgFlags & MQMF_LAST_SEGMENT) != 0;
				f.segmentAllowed = (mqmd.MsgFlags & MQMF_SEGMENTATION_ALLOWED) != 0;
				f.originalLength = number(mqmd.OriginalLength);
			}

			return true;
		}

		std::string msgTypeText(MQLONG msgType)
		{
			switch (msgType)
			{
			case 1: return "1 Request";
			case 2: return "2 Reply";
			case 4: return "4 Report";
			case 8: return "8 Datagram";
			case 112: return "112 FR_MQE";
			case 113: return "113 MQE";
			default: return "";
			}
		}
	}
}
```

In `core/CMakeLists.txt`, make these two changes:
- Add `message/rfh_mqmd_fields.cpp` to `add_library(rfhcore STATIC ...)`, after `mq/rfh_mq_session.cpp`.
- Add `message/rfh_mqmd_fields.cpp` to the `-Wall;-Wextra` `set_source_files_properties` list, after `mq/rfh_mq_loader.cpp mq/rfh_mq_session.cpp`.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build --preset linux-debug && ctest --preset linux-debug
```

Expected: `100% tests passed, 0 tests failed out of 370`.

- [ ] **Step 5: Commit**

```bash
git add core/message/rfh_mqmd_fields.h core/message/rfh_mqmd_fields.cpp core/CMakeLists.txt tests/CMakeLists.txt tests/core/mq_layout.h tests/core/MqmdFieldsTests.cpp
git commit -s -m "feat(core): convert between MQMD tab fields and an MQMD

mqmd_fields::toMqmd and fromMqmd port MQMDPAGE::buildMQMD and the
field-setting part of processMQMD, keeping their Windows results.
Golden MQMDs are built byte by byte at IBM's documented offsets.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: MQ header chain

**Files:**
- Create: `core/message/rfh_header_chain.h`, `core/message/rfh_header_chain.cpp`
- Modify: `core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/core/HeaderChainTests.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/core/HeaderChainTests.cpp`:

```cpp
// HeaderChainTests.cpp — tests for rfh::walkHeaders, detectLeadingHeader and
// looksLikeEbcdic (ports of DataArea::parseMsgHeaders, checkDataForHeader and
// checkForEBCDIC). Headers are built with mq_layout.h at IBM's documented offsets.

#include <gtest/gtest.h>

#include <cmqc.h>

#include "rfhconst.h"
#include "rfh_header_chain.h"
#include "mq_layout.h"

using namespace rfh;
using mq_layout::Order;
namespace L = mq_layout;

namespace
{
	HeaderChain walk(const std::vector<unsigned char> & data, const char * format, int ccsid = 1208, int encoding = NUMERIC_PC)
	{
		return walkHeaders(data.data(), data.size(), format, ccsid, CHAR_ASCII, encoding);
	}
}

TEST(HeaderChain, NoHeadersLeavesTheBodyAlone)
{
	auto data = L::text("hello");
	HeaderChain c = walk(data, "MQSTR   ", 819);
	EXPECT_TRUE(c.headers.empty());
	EXPECT_EQ(0u, c.bodyOffset);
	EXPECT_EQ("MQSTR", c.bodyFormat);
	EXPECT_EQ(819, c.bodyCcsid);
	EXPECT_EQ(NUMERIC_PC, c.encoding);
}

TEST(HeaderChain, Rfh2BeforeTextIsFound)
{
	std::string folder = "<mcd><Msd>jms_text</Msd></mcd>  ";		// 32 bytes, a multiple of 4
	auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR", std::string("\0\0\0\x20", 4) + folder);
	auto data = L::join({ rfh.data, L::text("body") });

	HeaderChain c = walk(data, MQFMT_RF_HEADER_2, 819);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ(HeaderType::Rfh2, c.headers[0].type);
	EXPECT_EQ(0u, c.headers[0].offset);
	EXPECT_EQ(rfh.data.size(), c.headers[0].length);
	EXPECT_EQ(rfh.data.size(), c.bodyOffset);
	EXPECT_EQ("MQSTR", c.bodyFormat);
	EXPECT_EQ(1208, c.bodyCcsid);
	EXPECT_EQ(NUMERIC_PC, c.encoding);
	EXPECT_TRUE(c.warnings.empty());
}

TEST(HeaderChain, BigEndianRfh2IsReadWithHostEncoding)
{
	auto rfh = L::rfh2(Order::Big, 785, 500, "MQSTR");
	auto data = L::join({ rfh.data, L::text("x") });

	HeaderChain c = walk(data, MQFMT_RF_HEADER_2, 819, NUMERIC_HOST);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ(36u, c.bodyOffset);
	EXPECT_EQ(500, c.bodyCcsid);
	EXPECT_EQ(CHAR_EBCDIC, c.charFormat);
	EXPECT_EQ(NUMERIC_HOST, c.encoding);
}

TEST(HeaderChain, EbcdicRfh2NamesAreTranslated)
{
	auto rfh = L::rfh2(Order::Big, 785, 500, "MQSTR", std::string(), true);
	HeaderChain c = walk(rfh.data, MQFMT_RF_HEADER_2, 500, NUMERIC_HOST);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ("MQSTR", c.bodyFormat);
}

TEST(HeaderChain, DeadLetterThenRfh2ThenBody)
{
	auto dlh = L::dlh(Order::Little, 546, 1208, "MQHRF2");
	auto rfh = L::rfh2(Order::Little, 546, 819, "MQSTR");
	auto data = L::join({ dlh.data, rfh.data, L::text("payload") });

	HeaderChain c = walk(data, MQFMT_DEAD_LETTER_HEADER);
	ASSERT_EQ(2u, c.headers.size());
	EXPECT_EQ(HeaderType::DeadLetter, c.headers[0].type);
	EXPECT_EQ(172u, c.headers[0].length);
	EXPECT_EQ(HeaderType::Rfh2, c.headers[1].type);
	EXPECT_EQ(172u, c.headers[1].offset);
	EXPECT_EQ(208u, c.bodyOffset);
	EXPECT_EQ("MQSTR", c.bodyFormat);
	EXPECT_EQ(819, c.bodyCcsid);
}

TEST(HeaderChain, CicsVersion2WithProgramNameIncludesIt)
{
	auto cih = L::cih(Order::Little, 2, MQCLT_PROGRAM, 546, 500, "MQSTR");
	auto data = L::join({ cih.data, L::text("PROGNAME"), L::text("commarea") });

	HeaderChain c = walk(data, MQFMT_CICS);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ(188u, c.bodyOffset);		// 180 + the 8-byte program name
	EXPECT_EQ("MQSTR", c.bodyFormat);
	EXPECT_EQ(500, c.bodyCcsid);
	EXPECT_EQ(CHAR_ASCII, c.charFormat) << "CICS leaves the character format alone";
}

TEST(HeaderChain, CicsTransactionLinkHasNoProgramName)
{
	auto cih = L::cih(Order::Little, 1, MQCLT_TRANSACTION, 546, 1208, "MQSTR");
	auto data = L::join({ cih.data, L::text("data") });
	HeaderChain c = walk(data, MQFMT_CICS);
	EXPECT_EQ(164u, c.bodyOffset);
}

TEST(HeaderChain, ImsHeaderIsAlways84Bytes)
{
	auto iih = L::iih(Order::Little, 546, 37, "MQIMSVS");
	auto data = L::join({ iih.data, L::text("segment") });
	HeaderChain c = walk(data, MQFMT_IMS);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ(84u, c.bodyOffset);
	EXPECT_EQ("MQIMSVS", c.bodyFormat);
	EXPECT_EQ(37, c.bodyCcsid);
	EXPECT_EQ(NUMERIC_PC, c.encoding) << "IMS leaves the encoding alone";
}

TEST(HeaderChain, InvalidRfh2LengthEndsTheChain)
{
	auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR");
	rfh.putLong(8, 9999);
	auto data = L::join({ rfh.data, L::text("x") });

	HeaderChain c = walk(data, MQFMT_RF_HEADER_2, 819);
	EXPECT_TRUE(c.headers.empty());
	EXPECT_EQ(0u, c.bodyOffset);
	EXPECT_EQ("", c.bodyFormat);
	ASSERT_EQ(1u, c.warnings.size());
	EXPECT_EQ("***Invalid RFH Length field*** ", c.warnings[0]);
}

TEST(HeaderChain, Rfh1NeedsNameValueDataHistoric)
{
	// Windows rejects an RFH1 whose StrucLength is just the 32-byte fixed part.
	auto empty = L::rfh1(Order::Little, 546, 1208, "MQSTR", std::string());
	EXPECT_TRUE(walk(empty.data, MQFMT_RF_HEADER).headers.empty());

	auto withData = L::rfh1(Order::Little, 546, 1208, "MQSTR", "a=b ");
	HeaderChain c = walk(L::join({ withData.data, L::text("x") }), MQFMT_RF_HEADER);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ(36u, c.bodyOffset);
}

TEST(HeaderChain, SameHeaderTypeTwiceStopsAfterTheFirstHistoric)
{
	auto first = L::dlh(Order::Little, 546, 1208, "MQDEAD");
	auto second = L::dlh(Order::Little, 546, 1208, "MQSTR");
	HeaderChain c = walk(L::join({ first.data, second.data }), MQFMT_DEAD_LETTER_HEADER);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ(172u, c.bodyOffset);
	EXPECT_EQ("MQDEAD", c.bodyFormat);
}

TEST(HeaderChain, MdExtensionIsReadAtItsRealOffset)
{
	auto dlh = L::dlh(Order::Little, 546, 1208, "MQHMDE");
	auto mde = L::mde(Order::Little, 546, 819, "MQSTR", 3, 100, MQMF_SEGMENT, 400, "GROUP-ID-123456789012345");
	HeaderChain c = walk(L::join({ dlh.data, mde.data, L::text("x") }), MQFMT_DEAD_LETTER_HEADER);

	ASSERT_EQ(2u, c.headers.size());
	EXPECT_EQ(HeaderType::MdExtension, c.headers[1].type);
	EXPECT_EQ(244u, c.bodyOffset);
	EXPECT_EQ("MQSTR", c.bodyFormat);
	EXPECT_EQ(819, c.bodyCcsid);
	ASSERT_TRUE(c.hasMdExtension);
	EXPECT_EQ(3, c.mdExtension.msgSeqNumber);
	EXPECT_EQ(100, c.mdExtension.offset);
	EXPECT_EQ(MQMF_SEGMENT, c.mdExtension.msgFlags);
	EXPECT_EQ(400, c.mdExtension.originalLength);
	EXPECT_EQ(0, memcmp(c.mdExtension.groupId, "GROUP-ID-123456789012345", 24));
}

TEST(HeaderChain, TruncatedHeaderEndsTheChain)
{
	auto dlh = L::dlh(Order::Little, 546, 1208, "MQSTR");
	dlh.data.resize(100);
	HeaderChain c = walk(dlh.data, MQFMT_DEAD_LETTER_HEADER);
	EXPECT_TRUE(c.headers.empty());
	EXPECT_EQ("", c.bodyFormat);
	EXPECT_EQ(-1, c.encoding) << "the cleared DLQ tab has encoding -1";
}

TEST(LeadingHeader, AsciiPcRfh2)
{
	auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR");
	LeadingHeader h = detectLeadingHeader(rfh.data.data(), rfh.data.size());
	ASSERT_TRUE(h.found);
	EXPECT_EQ(std::string(MQFMT_RF_HEADER_2), h.format);
	EXPECT_EQ(NUMERIC_PC, h.encoding);
	EXPECT_EQ(CHAR_ASCII, h.charFormat);
	EXPECT_EQ(1208, h.ccsid);
}

TEST(LeadingHeader, EbcdicHostRfh2DefaultsToCcsid500)
{
	auto rfh = L::rfh2(Order::Big, 785, 1208, "MQSTR", std::string(), true);
	LeadingHeader h = detectLeadingHeader(rfh.data.data(), rfh.data.size());
	ASSERT_TRUE(h.found);
	EXPECT_EQ(NUMERIC_HOST, h.encoding);
	EXPECT_EQ(FLOAT_390, h.floatEncoding);
	EXPECT_EQ(CHAR_EBCDIC, h.charFormat);
	EXPECT_EQ(500, h.ccsid) << "1208 is not an EBCDIC code page, so the default is used";
}

TEST(LeadingHeader, UnixEncodingSelectsUnixFloats)
{
	auto rfh = L::rfh2(Order::Big, 273, 819, "MQSTR");
	LeadingHeader h = detectLeadingHeader(rfh.data.data(), rfh.data.size());
	ASSERT_TRUE(h.found);
	EXPECT_EQ(FLOAT_UNIX, h.floatEncoding);
	EXPECT_EQ(819, h.ccsid);
}

TEST(LeadingHeader, PlainTextHasNoHeader)
{
	auto data = L::text("just some text that is long enough");
	EXPECT_FALSE(detectLeadingHeader(data.data(), data.size()).found);
}

TEST(LooksLikeEbcdic, EbcdicTextIsRecognised)
{
	std::vector<unsigned char> data;
	for (int i = 0; i < 20; i++)
	{
		auto word = L::ebcdic("HELLO WORLD ");
		data.insert(data.end(), word.begin(), word.end());
	}
	EXPECT_TRUE(looksLikeEbcdic(data.data(), data.size()));
}

TEST(LooksLikeEbcdic, AsciiTextAndShortDataAreNot)
{
	std::string ascii;
	for (int i = 0; i < 20; i++) ascii += "hello world ";
	EXPECT_FALSE(looksLikeEbcdic((const unsigned char *)ascii.data(), ascii.size()));

	auto shortData = L::ebcdic("HELLO");
	EXPECT_FALSE(looksLikeEbcdic(shortData.data(), shortData.size()));
}
```

In `tests/CMakeLists.txt`, append `core/HeaderChainTests.cpp` to the `rfhutil_core_tests` source list.

- [ ] **Step 2: Run the build to verify it fails**

```bash
cmake --build --preset linux-debug
```

Expected: FAIL. `rfh_header_chain.h` is not found.

- [ ] **Step 3: Write the header chain**

Create `core/message/rfh_header_chain.h`:

```cpp
// rfh_header_chain.h - finds the MQ headers (DLQ, CICS, IMS, RFH1, RFH2, MQMDE)
// at the front of message data and where the body starts.
//
// walkHeaders is the port of the chain logic in DataArea::parseMsgHeaders and the
// length and next-format rules of the header parsers (CDlq::parseDLQheader,
// CCICS::parseCICSheader, CIms::parseIMSheader, RFH::parseRFH, RFH::parseRFH2).
// It records where each header is and what follows it; it does not decode header
// contents. Two deliberate differences from Windows, both reported upstream:
// an MQMDE is read from its real offset (Windows uses (MQMDE *)data + offset,
// which scales the offset by sizeof(MQMDE)), and its integers are byte-swapped
// only for host encoding (Windows always swaps them).
//
// detectLeadingHeader and looksLikeEbcdic are the ports of
// DataArea::checkDataForHeader and DataArea::checkForEBCDIC, used when a file has
// no MQMD to say what the data is.
#pragma once

#include <cmqc.h>

#include <stddef.h>

#include <string>
#include <vector>

namespace rfh
{
	enum class HeaderType { DeadLetter, Cics, Ims, Rfh1, Rfh2, MdExtension };

	struct HeaderInfo
	{
		HeaderType type;
		size_t offset;			// from the start of the data
		size_t length;
	};

	// The MQMD fields an MQMDE carries, already in native byte order.
	struct MdExtensionFields
	{
		MQBYTE24 groupId;
		MQLONG msgSeqNumber;
		MQLONG offset;
		MQLONG msgFlags;
		MQLONG originalLength;
	};

	struct HeaderChain
	{
		std::vector<HeaderInfo> headers;
		size_t bodyOffset = 0;		// total length of the headers
		std::string bodyFormat;		// format of the data after the last header, trailing blanks removed
		int bodyCcsid = 0;
		int charFormat = 0;			// CHAR_* for the body
		int encoding = 0;			// NUMERIC_PC or NUMERIC_HOST for the body
		bool hasMdExtension = false;
		MdExtensionFields mdExtension = {};
		std::vector<std::string> warnings;
	};

	// Walks the headers starting with the given format (normally MQMD.Format), the
	// data's ccsid and character format, and its integer encoding (NUMERIC_PC or
	// NUMERIC_HOST). An invalid header ends the chain, as on Windows: the body
	// then starts at that header and its format is empty.
	HeaderChain walkHeaders(const unsigned char * data, size_t length, const std::string & format,
	                        int ccsid, int charFormat, int encoding);

	// What a file without an MQMD starts with, judged as DataArea::checkDataForHeader
	// does: a DLQ, CICS, IMS, RFH1 or RFH2 header in ASCII or EBCDIC, either byte order.
	struct LeadingHeader
	{
		bool found = false;
		std::string format;			// e.g. MQFMT_RF_HEADER_2, blank padded
		int ccsid = 0;				// ccsid the MQMD tab would show
		int charFormat = 0;			// CHAR_ASCII or CHAR_EBCDIC
		int encoding = 0;			// NUMERIC_PC or NUMERIC_HOST
		int pdEncoding = 0;
		int floatEncoding = 0;		// FLOAT_PC, FLOAT_UNIX or FLOAT_390
	};
	LeadingHeader detectLeadingHeader(const unsigned char * data, size_t length);

	// True when the first 1024 bytes look more like EBCDIC text than ASCII, as
	// DataArea::checkForEBCDIC judges it. Needs at least 64 bytes.
	bool looksLikeEbcdic(const unsigned char * data, size_t length);
}
```

Create `core/message/rfh_header_chain.cpp`:

```cpp
// rfh_header_chain.cpp - see rfh_header_chain.h

#include "rfh_header_chain.h"

#include <string.h>

#include <algorithm>

#include <cmqxc.h>

#include "rfh_platform.h"
#include "rfhconst.h"
#include "comsubs.h"
#include "mqsubs.h"

namespace rfh
{
	namespace
	{
		// header names as parseMsgHeaders compares them: formats with trailing blanks removed
		const char * const kDeadLetter = "MQDEAD";
		const char * const kCics = "MQCICS";
		const char * const kIms = "MQIMS";
		const char * const kRfh1 = "MQHRF";
		const char * const kRfh2 = "MQHRF2";
		const char * const kMdExtension = "MQHMDE";

		const size_t kCicsProgramName = 8;		// CIH_MAX_PROG_NAME

		MQLONG readLong(const unsigned char * p, bool reverse)
		{
			MQLONG v;
			memcpy(&v, p, sizeof(v));
			return reverse ? reverseBytes4(v) : v;
		}

		// StrucId and Format are translated from EBCDIC when the incoming data is EBCDIC
		std::string chars(const unsigned char * p, size_t length, bool ebcdic)
		{
			char temp[16] = { 0 };
			if (ebcdic)
			{
				EbcdicToAscii(p, (unsigned int)length, (unsigned char *)temp);
			}
			else
			{
				memcpy(temp, p, length);
			}
			temp[length] = 0;
			return temp;
		}

		std::string trimmedFormat(const unsigned char * p, bool ebcdic)
		{
			std::string format = chars(p, MQ_FORMAT_LENGTH, ebcdic);
			char temp[MQ_FORMAT_LENGTH + 1];
			memcpy(temp, format.c_str(), format.size() + 1);
			Rtrim(temp);
			return temp;
		}

		struct State
		{
			std::string nextFormat;
			int ccsid;
			int charFormat;
			int encoding;
		};

		// Offsets shared by the CIH, IIH, RFH and RFH2 structures.
		const size_t kStrucLength = 8, kEncoding = 12, kCcsid = 16, kFormat = 20;

		// CDlq::parseDLQheader + CDlq::setNextFormat
		size_t deadLetter(const unsigned char * p, size_t n, State & s)
		{
			bool ebcdic = (CHAR_EBCDIC == getCcsidType(s.ccsid));
			bool reverse = (s.encoding != NUMERIC_PC);

			if ((n >= sizeof(MQDLH)) &&
			    (chars(p, 4, ebcdic) == std::string(MQDLH_STRUC_ID, 4)) &&
			    (readLong(p + 4, reverse) == MQDLH_VERSION_1))
			{
				MQLONG encoding = readLong(p + 108, reverse);
				s.ccsid = readLong(p + 112, reverse);
				s.nextFormat = trimmedFormat(p + 116, ebcdic);
				s.charFormat = getCcsidType(s.ccsid);
				s.encoding = getIntEncode(encoding);
				return sizeof(MQDLH);
			}

			// the cleared DLQ tab: no format, no code page, encoding -1
			s.nextFormat.clear();
			s.ccsid = 0;
			s.charFormat = getCcsidType(0);
			s.encoding = -1;
			return 0;
		}

		// CCICS::parseCICSheader + CCICS::setNextFormat (which only sets the format)
		size_t cics(const unsigned char * p, size_t n, State & s)
		{
			bool ebcdic = (CHAR_EBCDIC == getCcsidType(s.ccsid));
			bool reverse = (s.encoding != NUMERIC_PC);

			if (n >= MQCIH_LENGTH_1)
			{
				MQLONG version = readLong(p + 4, reverse);
				if ((chars(p, 4, ebcdic) == std::string(MQCIH_STRUC_ID, 4)) &&
				    ((version == MQCIH_VERSION_1) || (version == MQCIH_VERSION_2)))
				{
					size_t length = (version == MQCIH_VERSION_1) ? MQCIH_LENGTH_1 : MQCIH_LENGTH_2;

					// LinkType sits at the same offset in both versions; with MQCLT_PROGRAM the
					// 8-byte program name follows the header. Windows checks the space for it
					// against the version 1 length even for version 2.
					MQLONG linkType = readLong(p + 52, reverse);
					if ((MQCLT_PROGRAM == linkType) && (n >= MQCIH_LENGTH_1 + kCicsProgramName))
					{
						length += kCicsProgramName;
					}

					MQLONG ccsid = readLong(p + kCcsid, reverse);
					s.ccsid = ((ccsid < 100000) && (ccsid > -10000)) ? ccsid : 0;
					s.nextFormat = trimmedFormat(p + kFormat, ebcdic);
					return length;
				}
			}

			s.nextFormat.clear();
			s.ccsid = 0;
			return 0;
		}

		// CIms::parseIMSheader + CIms::setNextFormat (which only sets the format)
		size_t ims(const unsigned char * p, size_t n, State & s)
		{
			bool ebcdic = (CHAR_EBCDIC == getCcsidType(s.ccsid));
			bool reverse = (s.encoding != NUMERIC_PC);

			if ((n >= MQIIH_LENGTH_1) &&
			    (chars(p, 4, ebcdic) == std::string(MQIIH_STRUC_ID, 4)) &&
			    (readLong(p + 4, reverse) == MQIIH_VERSION_1))
			{
				s.ccsid = readLong(p + kCcsid, reverse);
				s.nextFormat = trimmedFormat(p + kFormat, ebcdic);
				return MQIIH_LENGTH_1;
			}

			s.nextFormat.clear();
			s.ccsid = 0;
			return 0;
		}

		// RFH::parseRFH / RFH::parseRFH2 + setNextFormatRfh1 / setNextFormatRfh2.
		// Neither checks the version; RFH1 needs a length above the fixed part,
		// RFH2 accepts exactly the fixed part.
		size_t rfh(const unsigned char * p, size_t n, State & s, bool v2, std::vector<std::string> & warnings)
		{
			bool ebcdic = (CHAR_EBCDIC == getCcsidType(s.ccsid));
			bool reverse = (NUMERIC_HOST == s.encoding);
			size_t fixed = v2 ? MQRFH_STRUC_LENGTH_FIXED_2 : MQRFH_STRUC_LENGTH_FIXED;

			if ((n >= fixed) && (chars(p, 4, ebcdic) == std::string(MQRFH_STRUC_ID, 4)))
			{
				MQLONG length = readLong(p + kStrucLength, reverse);
				bool lengthOk = v2 ? ((length >= (MQLONG)fixed) && ((size_t)length <= n))
				                   : ((length > (MQLONG)fixed) && ((size_t)length <= n));
				if (lengthOk)
				{
					MQLONG encoding = readLong(p + kEncoding, reverse);
					s.ccsid = readLong(p + kCcsid, reverse);
					s.nextFormat = trimmedFormat(p + kFormat, ebcdic);
					s.charFormat = getCcsidType(s.ccsid);
					s.encoding = getIntEncode(encoding);
					return (size_t)length;
				}

				warnings.push_back("***Invalid RFH Length field*** ");
			}

			// the cleared RFH tab
			s.nextFormat.clear();
			s.ccsid = 0;
			s.charFormat = getCcsidType(0);
			s.encoding = NUMERIC_PC;
			return 0;
		}

		// MQMDPAGE::processMQMDE, at the header's real offset and swapping only for host data
		size_t mdExtension(const unsigned char * p, size_t n, State & s, HeaderChain & chain)
		{
			if (n < MQMDE_LENGTH_2)
			{
				s.nextFormat.clear();
				return 0;
			}

			bool reverse = (NUMERIC_HOST == s.encoding);
			MQLONG encoding = readLong(p + kEncoding, reverse);
			s.ccsid = readLong(p + kCcsid, reverse);
			s.nextFormat = trimmedFormat(p + kFormat, false);
			s.charFormat = getCcsidType(s.ccsid);
			s.encoding = getIntEncode(encoding);

			chain.hasMdExtension = true;
			memcpy(chain.mdExtension.groupId, p + 32, sizeof(MQBYTE24));
			chain.mdExtension.msgSeqNumber = readLong(p + 56, reverse);
			chain.mdExtension.offset = readLong(p + 60, reverse);
			chain.mdExtension.msgFlags = readLong(p + 64, reverse);
			chain.mdExtension.originalLength = readLong(p + 68, reverse);
			return MQMDE_LENGTH_2;
		}
	}

	HeaderChain walkHeaders(const unsigned char * data, size_t length, const std::string & format,
	                        int ccsid, int charFormat, int encoding)
	{
		HeaderChain chain;

		char trimmed[MQ_FORMAT_LENGTH + 1] = { 0 };
		memcpy(trimmed, format.data(), std::min(format.size(), (size_t)MQ_FORMAT_LENGTH));
		Rtrim(trimmed);

		State s = { trimmed, ccsid, charFormat, encoding };
		size_t ofs = 0;

		// The same loop structure as parseMsgHeaders: each pass tries every header
		// type in a fixed order, and a header followed by one of its own kind ends
		// the chain.
		auto step = [&](const char * name, HeaderType type, size_t (*parse)(const unsigned char *, size_t, State &, HeaderChain &)) -> bool
		{
			if (s.nextFormat != name)
			{
				return false;
			}

			size_t len = parse(data + ofs, length - ofs, s, chain);
			if (len > 0)
			{
				chain.headers.push_back({ type, ofs, len });
				ofs += len;
			}
			return s.nextFormat != name;
		};

		bool found = true;
		while (found)
		{
			found = false;
			found |= step(kDeadLetter, HeaderType::DeadLetter,
			              [](const unsigned char * p, size_t n, State & st, HeaderChain &) { return deadLetter(p, n, st); });
			found |= step(kCics, HeaderType::Cics,
			              [](const unsigned char * p, size_t n, State & st, HeaderChain &) { return cics(p, n, st); });
			found |= step(kIms, HeaderType::Ims,
			              [](const unsigned char * p, size_t n, State & st, HeaderChain &) { return ims(p, n, st); });
			found |= step(kRfh1, HeaderType::Rfh1,
			              [](const unsigned char * p, size_t n, State & st, HeaderChain & c) { return rfh(p, n, st, false, c.warnings); });
			found |= step(kRfh2, HeaderType::Rfh2,
			              [](const unsigned char * p, size_t n, State & st, HeaderChain & c) { return rfh(p, n, st, true, c.warnings); });
			found |= step(kMdExtension, HeaderType::MdExtension,
			              [](const unsigned char * p, size_t n, State & st, HeaderChain & c) { return mdExtension(p, n, st, c); });
		}

		chain.bodyOffset = ofs;
		chain.bodyFormat = s.nextFormat;
		chain.bodyCcsid = s.ccsid;
		chain.charFormat = s.charFormat;
		chain.encoding = s.encoding;
		return chain;
	}

	LeadingHeader detectLeadingHeader(const unsigned char * data, size_t length)
	{
		LeadingHeader h;
		if (length < 8)
		{
			return h;
		}

		char id[5] = { 0 };
		char ebcdicId[5] = { 0 };
		memcpy(id, data, 4);
		EbcdicToAscii((const unsigned char *)id, 4, (unsigned char *)ebcdicId);
		MQLONG version;
		memcpy(&version, data + 4, sizeof(version));
		MQLONG versionReversed = reverseBytes4(version);

		// DataArea::assignMQMDEncoding and assignMQMDccsid
		auto assign = [&](const char * strucId, size_t encodingOffset, size_t ccsidOffset, MQLONG headerVersion)
		{
			MQLONG encodingField;
			MQLONG ccsidField;
			memcpy(&encodingField, data + encodingOffset, sizeof(encodingField));
			memcpy(&ccsidField, data + ccsidOffset, sizeof(ccsidField));

			if (version == headerVersion)
			{
				h.encoding = NUMERIC_PC;
				h.pdEncoding = NUMERIC_PC;
				h.floatEncoding = FLOAT_PC;
			}
			else
			{
				h.encoding = NUMERIC_HOST;
				h.pdEncoding = NUMERIC_HOST;
				h.floatEncoding = (reverseBytes4(encodingField) == DEF_UNIX_ENCODING) ? FLOAT_UNIX : FLOAT_390;
			}

			int ccsid = (h.encoding != NUMERIC_PC) ? reverseBytes4(ccsidField) : ccsidField;
			if (memcmp(id, strucId, 4) == 0)
			{
				h.charFormat = CHAR_ASCII;
				h.ccsid = (CHAR_ASCII == getCcsidType(ccsid)) ? ccsid : DEF_ASCII_CCSID;
			}
			else
			{
				h.charFormat = CHAR_EBCDIC;
				h.ccsid = (CHAR_EBCDIC == getCcsidType(ccsid)) ? ccsid : DEF_EBCDIC_CCSID;
			}
		};

		auto idIs = [&](const char * strucId) { return (memcmp(id, strucId, 4) == 0) || (memcmp(ebcdicId, strucId, 4) == 0); };
		auto versionIs = [&](MQLONG v) { return (version == v) || (versionReversed == v); };

		// Later matches win, as in checkDataForHeader.
		if ((length >= sizeof(MQDLH)) && idIs(MQDLH_STRUC_ID) && versionIs(MQDLH_VERSION_1))
		{
			h.found = true;
			h.format = MQFMT_DEAD_LETTER_HEADER;
			assign(MQDLH_STRUC_ID, 108, 112, MQDLH_VERSION_1);
		}
		if ((length >= MQCIH_LENGTH_1 + 8) && idIs(MQCIH_STRUC_ID) && (versionIs(MQCIH_VERSION_1) || versionIs(MQCIH_VERSION_2)))
		{
			h.found = true;
			h.format = MQFMT_CICS;
			assign(MQCIH_STRUC_ID, 12, 16, MQCIH_VERSION_1);
		}
		if ((length >= MQIIH_LENGTH_1 + 8) && idIs(MQIIH_STRUC_ID) && versionIs(MQIIH_VERSION_1))
		{
			h.found = true;
			h.format = MQFMT_IMS;
			assign(MQIIH_STRUC_ID, 12, 16, MQIIH_VERSION_1);
		}
		if ((length >= MQRFH_STRUC_LENGTH_FIXED) && idIs(MQRFH_STRUC_ID) && versionIs(MQRFH_VERSION_1))
		{
			h.found = true;
			h.format = MQFMT_RF_HEADER;
			assign(MQRFH_STRUC_ID, 12, 16, MQRFH_VERSION_1);
		}
		if ((length >= MQRFH_STRUC_LENGTH_FIXED_2) && idIs(MQRFH_STRUC_ID) && versionIs(MQRFH_VERSION_2))
		{
			h.found = true;
			h.format = MQFMT_RF_HEADER_2;
			assign(MQRFH_STRUC_ID, 12, 16, MQRFH_VERSION_2);
		}

		return h;
	}

	bool looksLikeEbcdic(const unsigned char * data, size_t length)
	{
		size_t maxChars = std::min(length, (size_t)1024);
		if (maxChars < 64)
		{
			return false;
		}

		int aChars = 0, eChars = 0, eSpaces = 0, aSpaces = 0;
		for (size_t i = 0; i < maxChars; i++)
		{
			unsigned char c = data[i];
			if (64 == c)
			{
				eSpaces++;
			}
			else
			{
				if (32 == c) aSpaces++;
				if ((c >= 'A') && (c <= 'z')) aChars++;
				if ((c >= '0') && (c <= '9')) aChars++;
				if ((c >= 240) && (c <= 250)) eChars++;
				if ((c >= 193) && (c <= 233)) eChars++;
				if ((c >= 129) && (c <= 169)) eChars++;
			}
		}

		return (eChars + eSpaces) > (aChars + aSpaces + 32);
	}
}
```

The offsets used here were checked against `cmqc.h` with `offsetof`:
- MQCIH: `LinkType` at 52, CCSID at 16, Format at 20.
- MQDLH: Encoding, CCSID and Format at 108, 112 and 116; size 172.
- MQMDE: GroupId, MsgSeqNumber, Offset, MsgFlags and OriginalLength at 32, 56, 60, 64 and 68; size 72.
- MQIIH and MQRFH2: Format at 20.

In `core/CMakeLists.txt`, add `message/rfh_header_chain.cpp` to `add_library` after `message/rfh_mqmd_fields.cpp`, and to the `-Wall;-Wextra` list.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build --preset linux-debug && ctest --preset linux-debug
```

Expected: `100% tests passed, 0 tests failed out of 389`.

- [ ] **Step 5: Commit**

```bash
git add core/message/rfh_header_chain.h core/message/rfh_header_chain.cpp core/CMakeLists.txt tests/CMakeLists.txt tests/core/HeaderChainTests.cpp
git commit -s -m "feat(core): find MQ headers in message data

walkHeaders ports the chain logic of DataArea::parseMsgHeaders and the
length and next-format rules of the DLQ, CICS, IMS, RFH1 and RFH2
parsers; detectLeadingHeader and looksLikeEbcdic port
checkDataForHeader and checkForEBCDIC. An MQMDE is read at its real
offset and swapped only for host data (Windows bugs, reported upstream).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Message files

**Files:**
- Create: `core/fileio/rfh_message_file.h`, `core/fileio/rfh_message_file.cpp`
- Modify: `core/message/rfh_message.h`, `core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/core/MessageFileTests.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/core/MessageFileTests.cpp`:

```cpp
// MessageFileTests.cpp — tests for rfh::fileio (ports of DataArea::ReadFileData and
// DataArea::WriteFile). The golden files are built with mq_layout.h at IBM's
// documented offsets, independent of the code under test.

#include <gtest/gtest.h>

#include <stdio.h>
#include <string.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <cmqc.h>

#include "rfhconst.h"
#include "rfh_message_file.h"
#include "mq_layout.h"

using namespace rfh;
using mq_layout::Order;
namespace L = mq_layout;
namespace O = mq_layout::mqmd;

namespace
{
	class MessageFile : public ::testing::Test
	{
	protected:
		std::string path(const char * name)
		{
			return ::testing::TempDir() + "rfhutil-" + name;
		}

		std::string save(const char * name, const std::vector<unsigned char> & bytes)
		{
			std::string p = path(name);
			std::ofstream(p, std::ios::binary).write((const char *)bytes.data(), (std::streamsize)bytes.size());
			return p;
		}

		std::vector<unsigned char> load(const std::string & p)
		{
			std::ifstream in(p, std::ios::binary);
			return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}

		fileio::ReadOptions options()
		{
			fileio::ReadOptions o;
			o.ansiCcsid = 1252;
			return o;
		}

		// MQMD v2 for a string message, as Windows RFHUtil writes it for the tab values
		// below (persistence "No", PC encodings).
		L::Bytes stringMqmd(const char * format = "MQSTR")
		{
			L::Bytes b = L::defaultMqmd2();
			b.putLong(O::CodedCharSetId, 1208);
			b.putChars(O::Format, 8, format);
			b.putLong(O::Persistence, 0);
			b.putLong(O::Priority, 0);
			b.putLong(O::Expiry, -1);
			b.putChars(O::PutDate, 8, "20261016");
			b.putChars(O::PutTime, 8, "10111213");
			return b;
		}
	};
}

TEST_F(MessageFile, PlainTextIsAllBody)
{
	std::string p = save("plain.txt", L::text("hello"));
	MqmdFields f;
	fileio::ReadResult r;
	Result res = fileio::read(p, options(), f, r);

	ASSERT_TRUE(res.ok()) << res.text;
	EXPECT_EQ("5 bytes read from file " + p, res.text);
	EXPECT_FALSE(r.hadMqmd);
	EXPECT_TRUE(r.message.headers.empty());
	EXPECT_EQ(L::text("hello"), r.message.body);
	EXPECT_EQ(1252, r.fileCcsid);
	EXPECT_EQ(CHAR_ASCII, r.charFormat);
}

TEST_F(MessageFile, MissingFileReportsItsName)
{
	MqmdFields f;
	fileio::ReadResult r;
	Result res = fileio::read(path("does-not-exist"), options(), f, r);
	EXPECT_FALSE(res.ok());
	EXPECT_EQ("Could not open file " + path("does-not-exist"), res.text);
}

TEST_F(MessageFile, MqmdIsStrippedAndFillsTheTab)
{
	auto bytes = L::join({ stringMqmd().data, L::text("body") });
	std::string p = save("mqmd.msg", bytes);
	MqmdFields f;
	fileio::ReadResult r;
	Result res = fileio::read(p, options(), f, r);

	ASSERT_TRUE(res.ok());
	EXPECT_EQ("4 bytes read from file " + p, res.text) << "Windows does not count the MQMD";
	EXPECT_TRUE(r.hadMqmd);
	EXPECT_EQ(L::text("body"), r.message.body);
	EXPECT_EQ("MQSTR", f.format);
	EXPECT_EQ("1208", f.ccsid);
	EXPECT_EQ("2026/10/16 10:11:12.13", f.dateTime);
	EXPECT_EQ(1208, r.fileCcsid);
}

TEST_F(MessageFile, Version1MqmdStrips324Bytes)
{
	L::Bytes md = stringMqmd();
	md.putLong(4, 1);
	std::vector<unsigned char> v1(md.data.begin(), md.data.begin() + O::Length1);
	auto bytes = L::join({ v1, L::text(std::string(60, 'x')) });
	std::string p = save("v1.msg", bytes);
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_TRUE(r.hadMqmd);
	EXPECT_EQ(60u, r.message.body.size());
}

TEST_F(MessageFile, ShortVersion1MqmdIsNotRecognisedHistoric)
{
	// Windows requires sizeof(MQMD2) bytes even for a version 1 MQMD.
	L::Bytes md = stringMqmd();
	md.putLong(4, 1);
	std::vector<unsigned char> v1(md.data.begin(), md.data.begin() + O::Length1);
	auto bytes = L::join({ v1, L::text("abc") });
	std::string p = save("v1short.msg", bytes);
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_FALSE(r.hadMqmd);
	EXPECT_EQ(bytes.size(), r.message.body.size());
}

TEST_F(MessageFile, BigEndianMqmdIsNormalisedAndIdsKept)
{
	L::Bytes md = L::defaultMqmd2(Order::Big);
	md.putLong(O::CodedCharSetId, 500, Order::Big);
	md.putLong(O::Encoding, 785, Order::Big);
	md.putChars(O::Format, 8, "MQSTR");
	md.putBytes(O::MsgId, "ABCDEFGHIJKLMNOPQRSTUVWX", 24);
	std::string p = save("big.msg", L::join({ md.data, L::text("X") }));
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	ASSERT_TRUE(r.hadMqmd);
	EXPECT_EQ("500", f.ccsid);
	EXPECT_EQ(NUMERIC_HOST, f.encoding);
	EXPECT_EQ(0, memcmp(f.msgId, "ABCDEFGHIJKLMNOPQRSTUVWX", 24));
	EXPECT_EQ(CHAR_EBCDIC, r.charFormat);
}

TEST_F(MessageFile, MqmdThenRfh2ThenBody)
{
	auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR");
	auto bytes = L::join({ stringMqmd("MQHRF2").data, rfh.data, L::text("body") });
	std::string p = save("rfh2.msg", bytes);
	MqmdFields f;
	fileio::ReadResult r;
	Result res = fileio::read(p, options(), f, r);

	ASSERT_TRUE(res.ok());
	EXPECT_EQ("40 bytes read (data 4 dlq 0 rfh1 0, rfh2 36 cics 0 ims 0) from file " + p, res.text);
	EXPECT_EQ(rfh.data, r.message.headers);
	EXPECT_EQ(L::text("body"), r.message.body);
	ASSERT_EQ(1u, r.chain.headers.size());
	EXPECT_EQ("MQSTR", r.chain.bodyFormat);
}

TEST_F(MessageFile, LeadingRfh2WithoutMqmdIsDetected)
{
	auto rfh = L::rfh2(Order::Little, 546, 819, "MQSTR");
	std::string p = save("lead.msg", L::join({ rfh.data, L::text("data") }));
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_FALSE(r.hadMqmd);
	EXPECT_EQ("MQHRF2", f.format);
	EXPECT_EQ(rfh.data, r.message.headers);
	EXPECT_EQ(L::text("data"), r.message.body);
}

TEST_F(MessageFile, IgnoreHeadersKeepsThemInTheBody)
{
	auto rfh = L::rfh2(Order::Little, 546, 819, "MQSTR");
	auto bytes = L::join({ rfh.data, L::text("data") });
	std::string p = save("ignore.msg", bytes);
	fileio::ReadOptions o = options();
	o.ignoreHeaders = true;
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, o, f, r).ok());
	EXPECT_TRUE(r.message.headers.empty());
	EXPECT_EQ(bytes, r.message.body);
}

TEST_F(MessageFile, EbcdicTextIsRecognised)
{
	std::vector<unsigned char> bytes;
	for (int i = 0; i < 20; i++)
	{
		auto w = L::ebcdic("HELLO WORLD ");
		bytes.insert(bytes.end(), w.begin(), w.end());
	}
	std::string p = save("ebcdic.txt", bytes);
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_EQ("500", f.ccsid);
	EXPECT_EQ(NUMERIC_HOST, f.encoding);
	EXPECT_EQ(CHAR_EBCDIC, r.charFormat);
}

TEST_F(MessageFile, FileCodePageSettingDisablesEbcdicGuess)
{
	std::vector<unsigned char> bytes;
	for (int i = 0; i < 20; i++)
	{
		auto w = L::ebcdic("HELLO WORLD ");
		bytes.insert(bytes.end(), w.begin(), w.end());
	}
	std::string p = save("ebcdic2.txt", bytes);
	fileio::ReadOptions o = options();
	o.fileCcsid = 819;
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, o, f, r).ok());
	EXPECT_EQ(819, r.fileCcsid);
	EXPECT_EQ(CHAR_ASCII, r.charFormat);
}

TEST_F(MessageFile, BigEndianByteOrderMarkSelectsHostEncoding)
{
	std::string p = save("bom.txt", { 0xFE, 0xFF, 0x00, 'A' });
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_EQ(NUMERIC_HOST, r.fileIntFormat);
	EXPECT_EQ(NUMERIC_PC, r.numericFormat) << "the display encoding comes from the header walk";
}

TEST_F(MessageFile, LineEndOptions)
{
	std::string p = save("lines.txt", L::text("a\nb\r\nc"));
	MqmdFields f;
	fileio::ReadResult r;

	fileio::ReadOptions noCrLf = options();
	noCrLf.removeCrLf = true;
	ASSERT_TRUE(fileio::read(p, noCrLf, f, r).ok());
	EXPECT_EQ(L::text("abc"), r.message.body);

	fileio::ReadOptions unix = options();
	unix.unixLineEnds = true;
	ASSERT_TRUE(fileio::read(p, unix, f, r).ok());
	EXPECT_EQ(L::text("a\r\nb\r\nc"), r.message.body);
}

TEST_F(MessageFile, ReadThenWriteIsByteIdentical)
{
	// The compatibility guarantee: a file Windows wrote, read into the MQMD tab and
	// written back with its MQMD rebuilt from the tab, is unchanged.
	auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR");
	auto golden = L::join({ stringMqmd("MQHRF2").data, rfh.data, L::text("round trip") });
	std::string in = save("golden.msg", golden);

	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(in, options(), f, r).ok());

	Message out = r.message;
	out.mqmd = MQMD2{ MQMD2_DEFAULT };
	out.mqmd.Version = MQMD_VERSION_2;
	mqmd_fields::toMqmd(f, out.mqmd);

	fileio::WriteOptions w;
	w.includeMqmd = true;
	std::string outPath = path("golden-out.msg");
	Result res = fileio::write(outPath, out, w);
	ASSERT_TRUE(res.ok()) << res.text;
	EXPECT_EQ("410 bytes (data 10 mqmd 364 rfh2 36) written to file " + outPath, res.text);
	EXPECT_EQ(golden, load(outPath));
}

TEST_F(MessageFile, WriteDataOnlyLeavesOutHeaders)
{
	Message m;
	memcpy(m.mqmd.Format, "MQHRF2  ", 8);
	m.headers = L::rfh2(Order::Little, 546, 1208, "MQSTR").data;
	m.body = L::text("abc");
	fileio::WriteOptions w;
	w.dataOnly = true;
	std::string p = path("dataonly.msg");
	Result res = fileio::write(p, m, w);
	ASSERT_TRUE(res.ok());
	EXPECT_EQ("3 bytes written to file " + p, res.text);
	EXPECT_EQ(L::text("abc"), load(p));
}

TEST_F(MessageFile, WriteToMissingDirectoryFails)
{
	Message m;
	Result res = fileio::write("/nonexistent-dir/x.msg", m, fileio::WriteOptions());
	EXPECT_FALSE(res.ok());
	EXPECT_EQ("Unable to open output file - path not found", res.text);
}
```

In `tests/CMakeLists.txt`, append `core/MessageFileTests.cpp` to the `rfhutil_core_tests` source list.

- [ ] **Step 2: Run the build to verify it fails**

```bash
cmake --build --preset linux-debug
```

Expected: FAIL. `rfh_message_file.h` is not found.

- [ ] **Step 3: Give Message its headers**

Replace `core/message/rfh_message.h`:

```cpp
// rfh_message.h - one MQ message: its descriptor, its MQ headers and its body.
//
// headers holds the MQ headers that follow the MQMD (DLQ, CICS, IMS, RFH1, RFH2,
// MQMDE) as raw bytes, exactly as they arrived or were read from a file; body is
// the data after them. On the wire and in files the two are simply concatenated.
// Until the header tabs are ported, headers are carried unchanged rather than
// rebuilt from tab fields as Windows RFHUtil does.
#pragma once

#include <cmqc.h>

#include <vector>

namespace rfh
{
	struct Message
	{
		MQMD2 mqmd = { MQMD2_DEFAULT };
		std::vector<unsigned char> headers;
		std::vector<unsigned char> body;

		Message() { mqmd.Version = MQMD_VERSION_2; }
	};
}
```

- [ ] **Step 4: Write the file reader and writer**

Create `core/fileio/rfh_message_file.h`:

```cpp
// rfh_message_file.h - RFHUtil message files.
//
// A message file is the raw message image: optionally an MQMD (364 bytes, version
// 2, native byte order), then the MQ headers, then the body. read is the port of
// DataArea::ReadFileData and write the port of DataArea::WriteFile, without the
// file dialogs; both keep the Windows results and status text.
//
// Differences from Windows RFHUtil:
// - an MQMD in big-endian or EBCDIC form is normalised with translateMQMD, which
//   keeps MsgId, CorrelId and GroupId as they are (Windows' processMQMD
//   byte-reverses them; reported upstream);
// - headers are written back exactly as read, because the header tabs that would
//   rebuild them are not ported yet;
// - the "keep headers when reading" (save RFH) option is not supported yet.
#pragma once

#include <string>

#include "rfh_header_chain.h"
#include "rfh_message.h"
#include "rfh_mqmd_fields.h"
#include "rfh_result.h"

namespace rfh
{
	namespace fileio
	{
		struct ReadOptions
		{
			int fileCcsid = 0;			// the "file code page" setting; 0 decides from the data
			bool ignoreHeaders = false;	// do not look for MQ headers in the data
			bool removeCrLf = false;	// remove carriage returns and line feeds from the body
			bool unixLineEnds = false;	// turn bare line feeds in the body into CR LF
			int ansiCcsid = 0;			// last-resort code page; 0 uses rfh_get_acp()
		};

		struct ReadResult
		{
			Message message;			// mqmd is meaningful only when hadMqmd
			bool hadMqmd = false;
			HeaderChain chain;			// headers found, relative to the data after the MQMD
			int fileCcsid = 0;			// code page of the data (DataArea::fileCcsid)
			int fileIntFormat = 0;		// integer encoding of the data (DataArea::fileIntFormat), e.g. from a byte order mark
			int charFormat = 0;			// CHAR_* for displaying the body
			int numericFormat = 0;		// NUMERIC_PC or NUMERIC_HOST for displaying the body
			int pdFormat = 0;
			int floatFormat = 0;
		};

		// Reads path. fields is the MQMD tab: its code page and encoding are the
		// defaults, and it is cleared and refilled from the file as on Windows.
		// The Result text is the Windows status message, e.g. "12 bytes read from file x".
		Result read(const std::string & path, const ReadOptions & options, MqmdFields & fields, ReadResult & out);

		struct WriteOptions
		{
			bool includeMqmd = false;	// write message.mqmd first (as version 2, 364 bytes)
			bool dataOnly = false;		// leave out the headers
		};

		// Writes message to path. The Result text is the Windows status message,
		// e.g. "400 bytes (data 0 mqmd 364 rfh2 36) written to file x".
		Result write(const std::string & path, const Message & message, const WriteOptions & options);
	}
}
```

Create `core/fileio/rfh_message_file.cpp`:

```cpp
// rfh_message_file.cpp - see rfh_message_file.h

#include "rfh_message_file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "rfh_codepage.h"
#include "rfh_platform.h"
#include "rfhconst.h"
#include "comsubs.h"
#include "mqsubs.h"
#include "xmlsubs.h"

namespace rfh
{
	namespace fileio
	{
		namespace
		{
			std::string number(int value)
			{
				char temp[16];
				snprintf(temp, sizeof(temp), "%d", value);
				return temp;
			}

			// FileHandler::changeUnixFile: a bare LF becomes CR LF, CR LF pairs stay
			void unixToCrLf(std::vector<unsigned char> & data)
			{
				std::vector<unsigned char> out;
				out.reserve(data.size() * 2);
				for (size_t i = 0; i < data.size(); i++)
				{
					if ((data[i] == '\r') && (i + 1 < data.size()) && (data[i + 1] == '\n'))
					{
						out.push_back(data[i]);
						out.push_back(data[++i]);
					}
					else
					{
						if (data[i] == '\n')
						{
							out.push_back('\r');
						}
						out.push_back(data[i]);
					}
				}
				data.swap(out);
			}

			// FileHandler::openOutputFile's messages, mapped from errno
			std::string openOutputError(int err)
			{
				switch (err)
				{
				case EBUSY: return "Unable to open output file - sharing violation";
				case ENOLCK: return "Unable to open output file - file locked";
				case ENOSPC: return "Unable to open output file - disk full";
				case ENOENT: case ENOTDIR: return "Unable to open output file - path not found";
				case EACCES: case EPERM: case EROFS: return "Unable to open output file - access denied";
				default: return "Unable to open output file - Error " + number(err);
				}
			}

			struct HeaderLengths
			{
				size_t rfh1 = 0, rfh2 = 0, dlq = 0, cics = 0, ims = 0;
			};

			HeaderLengths lengthsOf(const HeaderChain & chain)
			{
				HeaderLengths l;
				for (const HeaderInfo & h : chain.headers)
				{
					switch (h.type)
					{
					case HeaderType::Rfh1: l.rfh1 += h.length; break;
					case HeaderType::Rfh2: l.rfh2 += h.length; break;
					case HeaderType::DeadLetter: l.dlq += h.length; break;
					case HeaderType::Cics: l.cics += h.length; break;
					case HeaderType::Ims: l.ims += h.length; break;
					case HeaderType::MdExtension: break;	// not counted, as on Windows
					}
				}
				return l;
			}
		}

		Result read(const std::string & path, const ReadOptions & options, MqmdFields & fields, ReadResult & out)
		{
			out = ReadResult();

			if (path.find('\xEE') != std::string::npos)
			{
				return Result::failure("Invalid file name (" + path + ")");
			}

			FILE * input = fopen(path.c_str(), "rb");
			if (input == NULL)
			{
				return Result::failure("Could not open file " + path);
			}

			// default code page and encoding: the setting, then the MQMD tab, then the system
			int ccsid = options.fileCcsid;
			if (0 == ccsid)
			{
				ccsid = atoi(fields.ccsid.c_str());
				if (0 == ccsid)
				{
					ccsid = (options.ansiCcsid != 0) ? options.ansiCcsid : rfh_get_acp();
					if (0 == ccsid)
					{
						ccsid = 1252;
					}
				}
			}
			int encoding = (fields.encoding < 0) ? NUMERIC_PC : fields.encoding;

			// ClearData + setEncoding(PC, PC, PC) + clearMQMD
			fields.floatEncoding = FLOAT_PC;
			mqmd_fields::clear(fields);

			std::vector<unsigned char> data;
			fseek(input, 0L, SEEK_END);
			long size = ftell(input);
			fseek(input, 0L, SEEK_SET);
			if (size > 0)
			{
				data.resize((size_t)size);
				size_t got = fread(data.data(), 1, data.size(), input);
				data.resize(got);
			}
			fclose(input);

			// a UTF-16 byte order mark decides the encoding
			if ((data.size() >= 2) && (0xFF == data[0]) && (0xFE == data[1]))
			{
				encoding = NUMERIC_PC;
			}
			else if ((data.size() >= 2) && (0xFE == data[0]) && (0xFF == data[1]))
			{
				encoding = NUMERIC_HOST;
			}

			out.fileCcsid = ccsid;
			out.fileIntFormat = encoding;
			out.numericFormat = NUMERIC_PC;
			out.charFormat = getCcsidType(ccsid);

			// an MQMD at the front (MQMDPAGE::processMQMD; at least sizeof(MQMD2) bytes)
			if (data.size() >= sizeof(MQMD2))
			{
				MQMD2 md;
				memcpy(&md, data.data(), sizeof(md));
				if (translateMQMD(&md) && mqmd_fields::fromMqmd(md, fields))
				{
					size_t mqmdLength = (MQMD_VERSION_1 == md.Version) ? sizeof(MQMD1) : sizeof(MQMD2);
					out.hadMqmd = true;
					out.message.mqmd = md;
					data.erase(data.begin(), data.begin() + mqmdLength);
					out.fileCcsid = atoi(fields.ccsid.c_str());
					out.fileIntFormat = fields.encoding;
					out.numericFormat = fields.encoding;
					out.charFormat = getCcsidType(md.CodedCharSetId);
				}
			}

			if (!out.hadMqmd && !options.ignoreHeaders)
			{
				LeadingHeader lead = detectLeadingHeader(data.data(), data.size());
				if (lead.found)
				{
					char format[MQ_FORMAT_LENGTH + 1] = { 0 };
					memcpy(format, lead.format.data(), std::min(lead.format.size(), (size_t)MQ_FORMAT_LENGTH));
					Rtrim(format);
					fields.format = format;
					fields.encoding = lead.encoding;
					fields.pdEncoding = lead.pdEncoding;
					fields.floatEncoding = lead.floatEncoding;
					fields.ccsid = number(lead.ccsid);
					out.charFormat = lead.charFormat;
				}
				else if ((0 == options.fileCcsid) && looksLikeEbcdic(data.data(), data.size()))
				{
					out.charFormat = CHAR_EBCDIC;
					out.numericFormat = NUMERIC_HOST;
					fields.ccsid = number(DEF_EBCDIC_CCSID);
					fields.encoding = NUMERIC_HOST;
					fields.pdEncoding = NUMERIC_HOST;
					fields.floatEncoding = FLOAT_390;
				}
			}

			if (!options.ignoreHeaders)
			{
				out.chain = walkHeaders(data.data(), data.size(), fields.format,
				                        atoi(fields.ccsid.c_str()), out.charFormat, fields.encoding);
				out.message.headers.assign(data.begin(), data.begin() + out.chain.bodyOffset);
				data.erase(data.begin(), data.begin() + out.chain.bodyOffset);

				out.charFormat = out.chain.charFormat;
				out.numericFormat = out.chain.encoding;

				// an MQMDE carries version 2 MQMD fields onto the MQMD tab
				if (out.chain.hasMdExtension)
				{
					const MdExtensionFields & e = out.chain.mdExtension;
					memcpy(fields.groupId, e.groupId, sizeof(fields.groupId));
					fields.seqNo = number(e.msgSeqNumber);
					fields.offset = number(e.offset);
					fields.originalLength = number(e.originalLength);
					fields.groupYes = (e.msgFlags & MQMF_MSG_IN_GROUP) != 0;
					fields.groupLast = (e.msgFlags & MQMF_LAST_MSG_IN_GROUP) != 0;
					fields.segmentYes = (e.msgFlags & MQMF_SEGMENT) != 0;
					fields.segmentLast = (e.msgFlags & MQMF_LAST_SEGMENT) != 0;
					fields.segmentAllowed = (e.msgFlags & MQMF_SEGMENTATION_ALLOWED) != 0;
				}
			}

			if (NUMERIC_PC == out.numericFormat)
			{
				out.pdFormat = NUMERIC_PC;
				out.floatFormat = FLOAT_PC;
			}
			else
			{
				out.pdFormat = NUMERIC_HOST;
				out.floatFormat = FLOAT_390;
			}

			Result r;
			HeaderLengths l = lengthsOf(out.chain);
			size_t headerBytes = l.rfh1 + l.rfh2 + l.dlq + l.cics + l.ims;
			char text[1024];
			if (headerBytes > 0)
			{
				snprintf(text, sizeof(text), "%d bytes read (data %d dlq %d rfh1 %d, rfh2 %d cics %d ims %d) from file %s",
				         (int)(data.size() + headerBytes), (int)data.size(), (int)l.dlq, (int)l.rfh1, (int)l.rfh2,
				         (int)l.cics, (int)l.ims, path.c_str());
			}
			else
			{
				snprintf(text, sizeof(text), "%d bytes read from file %s", (int)data.size(), path.c_str());
			}
			r.text = text;
			r.warnings = out.chain.warnings;

			if (options.removeCrLf)
			{
				data.push_back(0);
				int len = removeCrLf(data.data(), (unsigned int)(data.size() - 1));
				data.resize((size_t)len);
			}
			else if (options.unixLineEnds)
			{
				unixToCrLf(data);
			}

			out.message.body.swap(data);
			return r;
		}

		Result write(const std::string & path, const Message & message, const WriteOptions & options)
		{
			FILE * output = fopen(path.c_str(), "wb");
			if (output == NULL)
			{
				return Result::failure(openOutputError(errno));
			}

			size_t mqmdLength = 0;
			bool ok = true;
			if (options.includeMqmd)
			{
				MQMD2 md = message.mqmd;
				md.Version = MQMD_VERSION_2;
				ok = ok && (fwrite(&md, 1, sizeof(md), output) == sizeof(md));
				mqmdLength = sizeof(md);
			}

			const std::vector<unsigned char> noHeaders;
			const std::vector<unsigned char> & headers = options.dataOnly ? noHeaders : message.headers;
			if (!headers.empty())
			{
				ok = ok && (fwrite(headers.data(), 1, headers.size(), output) == headers.size());
			}
			if (!message.body.empty())
			{
				ok = ok && (fwrite(message.body.data(), 1, message.body.size(), output) == message.body.size());
			}
			if (fclose(output) != 0)
			{
				ok = false;
			}
			if (!ok)
			{
				return Result::failure("Unable to write output file " + path + " - " + strerror(errno));
			}

			// the breakdown names each header kind, as Windows does
			HeaderChain chain = walkHeaders(headers.data(), headers.size(),
			                                std::string(message.mqmd.Format, MQ_FORMAT_LENGTH),
			                                message.mqmd.CodedCharSetId, getCcsidType(message.mqmd.CodedCharSetId),
			                                getIntEncode(message.mqmd.Encoding));
			HeaderLengths l = lengthsOf(chain);

			std::string breakdown;
			if (headers.size() + mqmdLength > 0)
			{
				breakdown = " (data " + number((int)message.body.size());
				if (mqmdLength > 0) breakdown += " mqmd " + number((int)mqmdLength);
				if (l.rfh1 > 0) breakdown += " rfh1 " + number((int)l.rfh1);
				if (l.rfh2 > 0) breakdown += " rfh2 " + number((int)l.rfh2);
				if (l.dlq > 0) breakdown += " dlq " + number((int)l.dlq);
				if (l.cics > 0) breakdown += " cics " + number((int)l.cics);
				if (l.ims > 0) breakdown += " ims " + number((int)l.ims);
				breakdown += ")";
			}

			Result r;
			r.text = number((int)(message.body.size() + headers.size() + mqmdLength)) + " bytes" + breakdown + " written to file " + path;
			return r;
		}
	}
}
```

In `core/CMakeLists.txt`, make these changes:
- Add `fileio/rfh_message_file.cpp` to `add_library`, after `message/rfh_header_chain.cpp`.
- Add `fileio/rfh_message_file.cpp` to the `-Wall;-Wextra` list.
- Add this line to the PUBLIC include directories, after the `message` line:

```cmake
        ${CMAKE_CURRENT_SOURCE_DIR}/fileio
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug
```

Expected: `100% tests passed, 0 tests failed out of 405`. `MessageFile.ReadThenWriteIsByteIdentical` is the compatibility guarantee: a Windows-layout file read into the tab and written back with its MQMD rebuilt from the tab is unchanged.

- [ ] **Step 6: Commit**

```bash
git add core/fileio core/message/rfh_message.h core/CMakeLists.txt tests/CMakeLists.txt tests/core/MessageFileTests.cpp
git commit -s -m "feat(core): read and write RFHUtil message files

fileio::read and fileio::write port DataArea::ReadFileData and
WriteFile, without the file dialogs: an optional MQMD, the MQ headers
and the body, with the Windows status messages. Message carries the
headers as raw bytes. A file read and written back is byte-identical.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Headers on the wire

**Files:**
- Modify: `core/mq/rfh_mq_session.cpp`, `core/mq/rfh_mq_session.h`
- Test: `tests/core/MqSessionTests.cpp`

- [ ] **Step 1: Write the failing test**

In `tests/core/MqSessionTests.cpp`, add `#include "mq_layout.h"` after `#include "rfh_mq_session.h"`. Then add this test before `TEST_F(MqBindingsTest, PutThenGetReturnsSameMessage)`:

```cpp
TEST_F(MqBindingsTest, Rfh2HeaderIsSeparatedFromTheBody)
{
    auto rfh = mq_layout::rfh2(mq_layout::Order::Little, 546, 1208, "MQSTR");
    Message out = textMessage("after the header");
    memcpy(out.mqmd.Format, MQFMT_RF_HEADER_2, MQ_FORMAT_LENGTH);
    out.headers = rfh.data;
    ASSERT_TRUE(session.put(kQueue, out, PutOptions()).ok());

    Message in;
    Result r = session.get(kQueue, in, GetOptions());
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(rfh.data, in.headers);
    EXPECT_EQ("after the header", bodyText(in));
    EXPECT_EQ(0, memcmp(in.mqmd.Format, MQFMT_RF_HEADER_2, MQ_FORMAT_LENGTH));
}
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cmake --build --preset linux-debug && ./build/linux-debug/tests/rfhutil_mq_tests --gtest_filter='*Rfh2Header*'
```

Expected: FAIL. The header is not sent, and `in.headers` is empty.

- [ ] **Step 3: Join headers on put and separate them on get**

In `core/mq/rfh_mq_session.cpp`, add these includes after `#include <vector>`:

```cpp

#include "rfh_header_chain.h"
#include "mqsubs.h"
```

In `put()`, replace the size check's condition with:

```cpp
		if (message.headers.size() + message.body.size() > (size_t)std::numeric_limits<MQLONG>::max())
```

In `put()`, replace the `api.put(...)` call with:

```cpp
		// on the wire the MQ headers simply precede the body
		std::vector<unsigned char> * payload = &message.body;
		std::vector<unsigned char> joined;
		if (!message.headers.empty())
		{
			joined.reserve(message.headers.size() + message.body.size());
			joined.insert(joined.end(), message.headers.begin(), message.headers.end());
			joined.insert(joined.end(), message.body.begin(), message.body.end());
			payload = &joined;
		}

		api.put(hConn, hObj, &message.mqmd, &pmo, (MQLONG)payload->size(),
		        payload->empty() ? nullptr : payload->data(), &cc, &rc);
```

In `getOnce()`, replace `message.body.assign(buffer.begin(), buffer.begin() + std::min<size_t>((size_t)dataLength, buffer.size()));` with:

```cpp
				size_t received = std::min<size_t>((size_t)dataLength, buffer.size());

				// split the MQ headers named by the MQMD format chain from the body
				HeaderChain chain = walkHeaders(buffer.data(), received, std::string(md.Format, MQ_FORMAT_LENGTH),
				                                md.CodedCharSetId, getCcsidType(md.CodedCharSetId), getIntEncode(md.Encoding));
				message.headers.assign(buffer.begin(), buffer.begin() + chain.bodyOffset);
				message.body.assign(buffer.begin() + chain.bodyOffset, buffer.begin() + received);
```

In `core/mq/rfh_mq_session.h`, replace the comments above `put` and `get` with:

```cpp
		// Puts message to queue: message.headers followed by message.body. On success
		// message.mqmd holds the descriptor MQ returned (new MsgId, put date and time, ...).
```

```cpp
		// Gets or browses one message from queue into message. The MQ headers named by
		// the MQMD format chain (see walkHeaders) go to message.headers and the rest to
		// message.body. A browse continues on the same queue handle; switching queue or
		// putting closes it and resets the browse position. BrowsePrevious re-browses
		// from the first message, so each step costs one MQGET per position (counted
		// under the current match criteria), and lands on a different message if other
		// applications change the queue meanwhile. A destructive get leaves the browse
		// position unknown: BrowseNext continues, but BrowsePrevious fails until
		// BrowseFirst.
```

- [ ] **Step 4: Run all tests to verify they pass**

```bash
cmake --build --preset linux-debug && ctest --preset linux-debug
echo "DISPLAY QLOCAL(RFHUTIL.TEST.*)" | /opt/mqm/bin/runmqsc TEST_QM | grep AMQ8147E
```

Expected:
- `100% tests passed, 0 tests failed out of 406`, with the client test skipped.
- `AMQ8147E: IBM MQ object RFHUTIL.TEST.* not found.`

- [ ] **Step 5: Commit**

```bash
git add core/mq/rfh_mq_session.cpp core/mq/rfh_mq_session.h tests/core/MqSessionTests.cpp
git commit -s -m "feat(core): carry MQ headers separately through MqSession

put sends message.headers followed by message.body; get separates the
headers named by the MQMD format chain from the body.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The MFC MQMD tab delegates to the core (Windows; needs the owner's Windows build)

**Files:**
- Create: `tools/port/port-mqmdpage.pl`
- Modify (by the script): `RFHUtil/MQMDPAGE.h`, `RFHUtil/MQMDPAGE.cpp`, `RFHUtil/RFHUtil.vcxproj`, `Client/Client.vcxproj`

**When to run this task:** run it only after the owner reports that Plan 1's Windows build works (RFHUtil, rfhutilc and UnitTests). Stacking untested Windows changes makes failures harder to locate. If that report has not arrived, stop and report that this task is waiting.

**What changes:**
- `MQMDPAGE::buildMQMD` becomes a call to `rfh::mqmd_fields::toMqmd(getFields(), *mqmd)`.
- `MQMDPAGE::processMQMD` becomes `translateMQMD`, then `clearMQMD()`, `getFields()`, `fromMqmd` and `setFields`, followed by the `DataArea` display state it already set.
- The two new adapters, `getFields` and `setFields`, map the tab's members to and from `MqmdFields`.

`MQMDPAGE.cpp` goes from 472 lines to 138 for these functions. The report, flag and persistence helpers stay, because `setMessageMQMD` and `processMessageMQMD` (the put and get paths) still use them. They move to the core with Plan 3's MQ tabs. Both projects compile `core/message/rfh_mqmd_fields.cpp`.

The script also adds two include directories, because that core file includes `rfhconst.h` from `RFHUtil/`:
- in RFHUtil.vcxproj: `..\core\message;.`
- in Client.vcxproj: `..\core\message;..\RFHUtil`

- [ ] **Step 1: Add and run the script**

Create `tools/port/port-mqmdpage.pl`:

```perl
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
```

```bash
perl tools/port/port-mqmdpage.pl .
for f in RFHUtil/RFHUtil.vcxproj Client/Client.vcxproj; do xmllint --noout "$f" && echo "$f ok"; done
git diff --stat RFHUtil/MQMDPAGE.cpp
```

Expected:
- `MQMD page delegates to the core`
- Both projects `ok`
- `1 file changed, 138 insertions(+), 472 deletions(-)`

- [ ] **Step 2: Confirm Linux is unaffected**

```bash
cmake --build --preset linux-debug && ctest --preset linux-debug
```

Expected: `100% tests passed, 0 tests failed out of 406`.

- [ ] **Step 3: Commit**

```bash
git add tools/port/port-mqmdpage.pl RFHUtil/MQMDPAGE.h RFHUtil/MQMDPAGE.cpp RFHUtil/RFHUtil.vcxproj Client/Client.vcxproj
git commit -s -m "refactor(windows): MQMD tab builds and reads MQMDs through the core

MQMDPAGE::buildMQMD and processMQMD delegate to rfh::mqmd_fields through
getFields/setFields, so the file path has one implementation on both
platforms. A big-endian MQMD now keeps its ids unchanged, as on Linux.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 4: Hand over the Windows checks to the owner**

Ask the owner to build RFHUtil and rfhutilc (Release|x64) and check the following:
1. Read a message file saved earlier, with an MQMD. The MQMD tab must show the same values as before.
2. Save it again with "include MQMD". The file must be byte-identical (`fc /b old new`).
3. Write and read a message on a queue (put and get paths, unchanged).
4. Watch the build for C2039/C3861 (missing `getFields`, `translateMQMD` or `getIntEncode`) and C1083 (`rfh_mqmd_fields.h` or `rfhconst.h` not found).

---

### Task 7: Documentation and the upstream report

**Files:**
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/specs/2026-10-15-linux-qt-port-design.md`
- Modify: `docs/superpowers/plans/2026-10-16-linux-port-plan-2a-message-files.md` (this plan)

- [ ] **Step 1: CHANGELOG**

In the `## [Unreleased]` section, add to `### Added`:

```markdown
- Message files: `fileio::read` / `fileio::write` read and write RFHUtil message
  files (optional MQMD, MQ headers, body) byte-for-byte compatibly with Windows.
- `mqmd_fields` converts between the MQMD tab's fields and an MQMD; `walkHeaders`
  finds DLQ, CICS, IMS, RFH1, RFH2 and MQMDE headers. `MqSession` carries MQ
  headers separately from the body.
```

Add to `### Changed`:

```markdown
- The display and encoding constants moved from `rfhutil.h` to `rfhconst.h`;
  `mqsubs.cpp` is shared with the portable core and its unused
  `findMQInstallDirectory` was removed.
```

If Task 6 ran, also add:

```markdown
- The MQMD tab builds and reads file MQMDs through the portable core. A big-endian
  MQMD's MsgId, CorrelId and GroupId are no longer byte-reversed.
```

- [ ] **Step 2: Spec**

In section 4 of the spec, replace the `mqmd_fields`, `HeaderChain` and `fileio` lines that are marked "Plan 2 (not yet implemented)" with the signatures from `core/message/rfh_mqmd_fields.h`, `rfh_header_chain.h` and `core/fileio/rfh_message_file.h`. Copy the declarations only.

Keep `format::render` marked as Plan 2B.

Add these bullets to section 9:
- "Message files: big-endian MQMD ids are kept (Windows reverses them); an MQMDE is read at its real offset and swapped only for host data; over-long MQMD fields are truncated instead of overrunning; headers are written back as read until the header tabs are ported; the save-RFH read option is not supported yet."

- [ ] **Step 3: Execution notes**

Append `## Execution notes` to this plan. Record any deviations made during review, the final test count, and whether Task 6 ran.

- [ ] **Step 4: Add the new upstream bugs to the issue draft**

These are a draft for the owner; nothing is filed. Add to the draft issue the bugs listed in "Background" (marked "Reported, not fixed"), with file and line from the current code and the pinning test or the behaviour difference.

- [ ] **Step 5: Run everything and commit**

```bash
cmake --build --preset linux-debug && ctest --preset linux-debug
git add CHANGELOG.md docs/superpowers
git commit -s -m "docs: document message files and the MQMD field conversion

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Not in this plan

- **Plan 2B:** the eight display formatters (character, hex, both, XML, JSON, parsed, fixed, COBOL) behind the Data tab.
- **Plan 3:** the Qt6 application. It also moves the put and get MQMD paths (`setMessageMQMD` / `processMessageMQMD`) and the ID display (`SetId`) into the core.
- **Header tabs (RFH, DLQ, CICS, IMS, JMS, PubSub):** these parse and rebuild header contents. Until they are ported, headers are carried as raw bytes.
