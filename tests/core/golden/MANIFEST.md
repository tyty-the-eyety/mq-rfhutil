# Golden message files

Reference RFHUtil message files for the tests in `tests/core/GoldenFileTests.cpp`, and the steps that
confirm them against Windows RFHUtil.

**Status: reference files, not yet confirmed by Windows.** They are not output of Windows RFHUtil.
`tools/golden/make_goldens.cpp` writes every byte from the structure layouts IBM documents for the MQ
MQMD, MQRFH2, MQDLH and MQMDE, using only `tests/core/mq_layout.h`; it does not use the core's file
reader or writer, so the files do not depend on the code they test. They become Windows-confirmed when
the owner has run the procedure below and `compare-windows-output` reports no unexpected difference.
Files the owner supplies are a separate set (see "Adding files").

How the files are used: the tests read each one through `rfh::fileio::read`, write it back and compare
the bytes, check its SHA-256 against this file (the hash lines below are the only record; the test reads
them), and regenerate the set with `rfh_make_goldens` and compare. No file is larger than 4 KB.

## Contents

| File | Size | What it is |
|---|---|---|
| `plain-crlf.txt` | 56 | ASCII text, CR LF line ends, no MQMD |
| `plain-lf.txt` | 53 | The same text with LF line ends |
| `binary-all-bytes.bin` | 512 | Bytes 0x00 to 0xFF twice, no MQMD |
| `ebcdic-500.bin` | 137 | EBCDIC text (CCSID 500), no MQMD |
| `mqmd-v2-text.bin` | 399 | MQMD v2 (little endian, CCSID 1208, MQSTR) and text |
| `mqmd-ebcdic-host.bin` | 399 | MQMD (Encoding 785, CCSID 500) and EBCDIC text |
| `mqmd-rfh2-text.bin` | 465 | MQMD, RFH2 with two folders, text |
| `rfh2-only.bin` | 127 | RFH2 and text, no MQMD |
| `mqmd-dlh-rfh2-text.bin` | 645 | MQMD, DLH naming RFH2, RFH2, text |
| `mqmd-bigendian.bin` | 385 | Big-endian MQMD (Encoding 273) and text. Known difference from Windows |
| `mqmd-mqmde-text.bin` | 456 | MQMD, MQMDE, text. Known difference from Windows |

`mqmd-ebcdic-host.bin` also differs from what Windows (and the core) write back, by one byte; see its entry.

## Confirming the files against Windows

Prerequisites: the Windows RFHUtil build you want to confirm, this directory copied to the Windows machine
(for example to `C:\golden\in`; keep the file names), an empty output directory (for example
`C:\golden\out`), and PowerShell.

Windows RFHUtil's defaults decide whether a saved file has an MQMD, so set the options for each file as
its entry says. The options are check-marked items in two menus (a check mark means on):

- **Read menu**: `Remove CrLf`, `Unix Text`, `Ignore Headers`, `Save Headers`, `Ignore MQMD`. All start off.
  Leave them all off unless an entry says otherwise.
- **Write menu**: `No Headers` starts off. **`Include MQMD` starts ON.** A file that has no MQMD must be
  saved with `Include MQMD` switched off, or Windows adds a 364-byte MQMD to it.
- **Main tab, `File Code Page`**: starts at 437. It is the code page assumed for a file with no MQMD, and
  a blank (or 0) field lets RFHUtil guess EBCDIC from the data.

For each file, in this order:

1. Press `Clear All` (Main tab), so nothing is left over from the previous file.
2. Set the Read and Write menu items and `File Code Page` as the entry says.
3. `File > Open` (or the Main tab `Open File` button) and choose the file from the input directory. The
   Main tab message line should read as the entry says.
4. `File > Save` (or the Main tab `Save File` button) and save into the output directory **with the same
   file name** (the Save dialog asks before it overwrites; it should not need to).
5. The message line should read as the entry says.

When all files are done, run one of these (the scripts print one line per file and a summary, and exit 0
if there is no unexpected difference, 1 if there is one, 2 on a usage error):

```
bash tools/golden/compare-windows-output.sh C:/golden/out            # Linux, or Git Bash on Windows
powershell -File tools\golden\compare-windows-output.ps1 C:\golden\out
```

Both take the golden directory as an optional second argument (default `tests/core/golden`). A line says
`same`, `DIFFERENT (expected: <reason>)` for the known differences below, or `DIFFERENT`. For a plain
`DIFFERENT`, find the first differing byte with `cmp -l golden out | head` (or `fc.exe /b`) and look it up
in the entry's byte ranges; please report the file, the offset and both values.

`Get-FileHash -Algorithm SHA256 FILE` (PowerShell) or `certutil -hashfile FILE SHA256` (cmd) checks that a
copied file arrived intact against the hash in its entry.

Windows rebuilds RFH2 and DLH headers from its header tabs when it saves (the core writes headers back
exactly as read). Whether Windows reproduces the RFH2 and DLH bytes of the golden files exactly is
UNVERIFIED; if it does not, a plain `DIFFERENT` on `mqmd-rfh2-text.bin`, `rfh2-only.bin` or
`mqmd-dlh-rfh2-text.bin` at an offset inside the header is the finding to report.

## Layouts used

All numbers are 4-byte signed integers in the byte order of their file (little endian unless noted);
character fields are blank filled. MQMD version 2 is 364 bytes:

| Offset | Field | Offset | Field |
|---|---|---|---|
| 0 | StrucId `MD  ` | 196 | UserIdentifier (12) |
| 4 | Version (2) | 208 | AccountingToken (32) |
| 8 | Report | 240 | ApplIdentityData (32) |
| 12 | MsgType | 272 | PutApplType |
| 16 | Expiry | 276 | PutApplName (28) |
| 20 | Feedback | 304 | PutDate (8, YYYYMMDD) |
| 24 | Encoding | 312 | PutTime (8, HHMMSSTH) |
| 28 | CodedCharSetId | 320 | ApplOriginData (4) |
| 32 | Format (8) | 324 | GroupId (24) |
| 40 | Priority | 348 | MsgSeqNumber |
| 44 | Persistence | 352 | Offset |
| 48 | MsgId (24) | 356 | MsgFlags |
| 72 | CorrelId (24) | 360 | OriginalLength |
| 96 | BackoutCount | 364 | end; the next structure or the data starts here |
| 100 | ReplyToQ (48) | | |
| 148 | ReplyToQMgr (48) | | |

Fields not named in an entry have the MQMD2 default: Report 0, Feedback 0, BackoutCount 0, PutApplType 0,
MsgSeqNumber 1, Offset 0, MsgFlags 0, OriginalLength -1, and blank or zero ids and text. Where an entry
lists "common MQMD values": Version 2, MsgType 8 (datagram), Expiry -1, Priority 0, Persistence 0,
PutDate `20261017`, PutTime `09301500`.

MQRFH2 (here 92 bytes): 0 StrucId `RFH `, 4 Version 2, 8 StrucLength 92, 12 Encoding, 16 CodedCharSetId,
20 Format (8), 28 Flags 0, 32 NameValueCCSID 1208, then the folders from 36: a 4-byte length and that many
bytes of XML, blank padded to a multiple of 4. Folder 1 at 36: length 28 at 36, `<mcd><Msd>xml</Msd></mcd>`
plus 3 blanks at 40 to 67. Folder 2: length 20 at 68, `<usr><k>v</k></usr>` plus 1 blank at 72 to 91.

MQDLH (172 bytes): 0 StrucId `DLH `, 4 Version 1, 8 Reason 2085, 12 DestQName (48) `ORIG.Q`, 60 DestQMgrName (48)
`QM1`, 108 Encoding, 112 CodedCharSetId, 116 Format (8), 124 PutApplType 6, 128 PutApplName (28) `app`,
156 PutDate `20261016`, 164 PutTime `12000000`.

MQMDE (72 bytes): 0 StrucId `MDE `, 4 Version 2, 8 StrucLength 72, 12 Encoding, 16 CodedCharSetId, 20 Format (8),
28 Flags 0, 32 GroupId (24), 56 MsgSeqNumber, 60 Offset, 64 MsgFlags, 68 OriginalLength.

## The files

### `plain-crlf.txt`
- SHA-256: `0a4f33d8284cf60971e8dd0227438b99f92f419bcb83d91ab1b48074d3a918f2`
- Size: 56 bytes
- Bytes: 0-55: `Golden message file.` CR LF `The quick brown fox jumps.` CR LF `End.` CR LF (20 + 2, 26 + 2, 4 + 2). No MQMD, no header.
- Read menu: all off. `File Code Page` 437. Write menu: **`Include MQMD` off**, `No Headers` off.
- Open message: `56 bytes read from file <path>`. Save message: `56 bytes written to file <path>`.
- Expected: `same`.

### `plain-lf.txt`
- SHA-256: `9d745d4e5ae621956c0bdc15743477f3646e4f89f92da1ccc6c87a49f91fbe65`
- Size: 53 bytes
- Bytes: 0-52: the same three lines, each ended by LF alone (53 bytes). No MQMD, no header.
- Read menu: all off (**`Unix Text` off**). `File Code Page` 437. Write menu: **`Include MQMD` off**.
- Open message: `53 bytes read from file <path>`. Save message: `53 bytes written to file <path>`.
- Expected: `same`.
- Second run, for the Unix option: switch **`Unix Text` on**, open `plain-lf.txt` again (message `53 bytes read`;
  the data grows to 56 bytes), save as `plain-crlf.txt` in a second output directory. That file must be
  identical to the golden `plain-crlf.txt` (the test `Golden.PlainLfWithUnixTextIsThePlainCrlfText` states the
  same for the core). Switch `Unix Text` off again afterwards.

### `binary-all-bytes.bin`
- SHA-256: `110009dcee21620b166f3abfecb5eff7a873be729d1c2d53822e7acc5f34eb9b`
- Size: 512 bytes
- Bytes: 0-255: 0x00 to 0xFF in order; 256-511: 0x00 to 0xFF again. No MQMD, no header.
- Read menu: all off. `File Code Page` 437. Write menu: **`Include MQMD` off**.
- Open message: `512 bytes read from file <path>`. Save message: `512 bytes written to file <path>`.
- Expected: `same`. (With a blank `File Code Page` RFHUtil takes this data for EBCDIC; that changes only the
  display, but keep 437 so the run matches the entry.)

### `ebcdic-500.bin`
- SHA-256: `8db12a4ad9920d08a74dd55f833fc6495e3ef01d796ba8f2130bc802512c9c58`
- Size: 137 bytes
- Bytes: 0-136: the text `The quick brown fox jumps over the lazy dog, 0123456789. THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG: it is EBCDIC text in code page 500.`
  in EBCDIC code page 500 (for example `T` is 0xE3, `h` 0x88, blank 0x40, `0` 0xF0). No MQMD, no header.
- Read menu: all off. **`File Code Page` blank (or 0)**, so RFHUtil guesses EBCDIC; after Open the MQMD tab
  shows Code Page 500 and the Data tab shows the text readable in Character view. Write menu: **`Include MQMD` off**.
- Open message: `137 bytes read from file <path>`. Save message: `137 bytes written to file <path>`.
- Expected: `same`. Put `File Code Page` back to 437 afterwards.

### `mqmd-v2-text.bin`
- SHA-256: `fb0f0d01b6e80dedbe38ce81d97e6178913145b702edf2d92762ff5942de2efe`
- Size: 399 bytes
- Bytes: 0-363 MQMD v2; 364-398 the text `Text after a version 2 MQMD, caf` followed by 0xC3 0xA9 (e acute in UTF-8) and `.` (35 bytes).
  MQMD values: common values except MsgType 1 (request), Expiry 6000, Priority 5, Persistence 1;
  Encoding 546, CodedCharSetId 1208, Format `MQSTR`; MsgId bytes 0x01 to 0x18; CorrelId bytes 0x21 to 0x38;
  ReplyToQ `REPLY.QUEUE`; ReplyToQMgr `QM1`; UserIdentifier `tyron`; ApplIdentityData `golden`; PutApplName `rfhgolden`.
- Read menu: all off. Write menu: **`Include MQMD` on (the default)**, `No Headers` off.
- Open message: `35 bytes read from file <path>` (Windows does not count the MQMD). The MQMD tab shows the values above.
  Save message: `399 bytes (data 35 mqmd 364) written to file <path>`.
- Expected: `same`.

### `mqmd-ebcdic-host.bin`
- SHA-256: `f552cf7708d0a4abc84340ce3d1f1a0652eb59ef321d032885b6c254bed2cc71`
- Size: 399 bytes
- Bytes: 0-363 MQMD v2 (little endian; common values; **Encoding 785**; CodedCharSetId 500; Format `MQSTR`);
  364-398 the text `HELLO FROM A HOST MQMD, 0123456789.` (35 bytes) in EBCDIC 500 (`H` is 0xC8).
- Read menu: all off. Write menu: **`Include MQMD` on**. After Open the MQMD tab shows Code Page 500 and Int Fmt Host.
- Open message: `35 bytes read from file <path>`. Save message: `399 bytes (data 35 mqmd 364) written to file <path>`.
- Expected: `DIFFERENT (expected: ...)`, in exactly one byte: file offset 25 (0-based, the second byte of Encoding)
  is 0x03 in the golden and 0x02 in Windows' file, because Encoding 785 (0x311) comes back as 529 (0x211): reading an
  MQMD does not set the float format, so the MQMD tab writes IEEE-reversed floats. This is from reading the Windows
  source (`MQMDPAGE::processMQMD`, `DataArea::ReadFileData`) and is UNVERIFIED on Windows. Any other difference is a finding.

### `mqmd-rfh2-text.bin`
- SHA-256: `95e21d32aa797b9f799a1f238f862a7205580fb5af120e38570a8ce92be66c84`
- Size: 465 bytes
- Bytes: 0-363 MQMD (common values; Encoding 546; CodedCharSetId 1208; Format `MQHRF2`); 364-455 RFH2
  (92 bytes; Encoding 546, CodedCharSetId 1208, Format `MQSTR`; two folders, see Layouts); 456-464 the text `<a>hi</a>`.
- Read menu: all off. Write menu: **`Include MQMD` on**, `No Headers` off.
- Open message: `101 bytes read (data 9 dlq 0 rfh1 0, rfh2 92 cics 0 ims 0) from file <path>`.
  Save message: `465 bytes (data 9 mqmd 364 rfh2 92) written to file <path>`.
- Expected: `same`, if Windows regenerates the RFH2 as it is in the golden (UNVERIFIED, see above).

### `rfh2-only.bin`
- SHA-256: `293bc2d7152f168d25fad461ae530fe82d3f3bb17b5128c4b87a5c9bd774f3f1`
- Size: 127 bytes
- Bytes: 0-91 RFH2 (as above); 92-126 the text `Body after a leading RFH2, no MQMD.` (35 bytes). No MQMD.
- Read menu: all off. `File Code Page` 437. Write menu: **`Include MQMD` off**, `No Headers` off.
- Open message: `127 bytes read (data 35 dlq 0 rfh1 0, rfh2 92 cics 0 ims 0) from file <path>`.
  Save message: `127 bytes (data 35 rfh2 92) written to file <path>`.
- Expected: `same`, if Windows regenerates the RFH2 as it is in the golden (UNVERIFIED).

### `mqmd-dlh-rfh2-text.bin`
- SHA-256: `3398506e2a93899fb51b7dc4e195da0f7fc1d1e460f8ec8b0433422262f429d5`
- Size: 645 bytes
- Bytes: 0-363 MQMD (common values; Encoding 546; CodedCharSetId 1208; Format `MQDEAD`); 364-535 DLH (172 bytes;
  Encoding 546, CodedCharSetId 1208, Format `MQHRF2`); 536-627 RFH2 (92 bytes; Encoding 546, CodedCharSetId 819,
  Format `MQSTR`); 628-644 the text `Dead letter body.` (17 bytes).
- Read menu: all off. Write menu: **`Include MQMD` on**, `No Headers` off.
- Open message: `281 bytes read (data 17 dlq 172 rfh1 0, rfh2 92 cics 0 ims 0) from file <path>`.
  Save message: `645 bytes (data 17 mqmd 364 rfh2 92 dlq 172) written to file <path>`.
- Expected: `same`, if Windows regenerates the DLH and RFH2 as they are in the golden (UNVERIFIED).

### `mqmd-bigendian.bin`
- SHA-256: `1a7e6f7d21ccc651bf2fc1083b29e114f202b067352aeac5dba4ccf4f3539c68`
- Size: 385 bytes
- Bytes: 0-363 MQMD **big endian** (every number most significant byte first, so Version is 00 00 00 02;
  common values except Expiry 3000, Priority 4, Persistence 1; Encoding 273; CodedCharSetId 819; Format `MQSTR`;
  MsgId bytes 0x01 to 0x18, CorrelId 0x21 to 0x38, GroupId 0x41 to 0x58, AccountingToken 0x61 to 0x80);
  364-384 the text `Big-endian MQMD body.` (21 bytes).
- Read menu: all off. Write menu: **`Include MQMD` on**.
- Open message: `21 bytes read from file <path>`. Save message: `385 bytes (data 21 mqmd 364) written to file <path>`.
- Expected: `DIFFERENT (expected: ...)`, because the written MQMD is little endian (Windows always writes the MQMD in the
  byte order of the machine) and Encoding is rebuilt (273 becomes 529, as for `mqmd-ebcdic-host.bin`). The reported
  Windows defect is in the ids: Windows byte-reverses MsgId, CorrelId, GroupId and AccountingToken of a big-endian MQMD
  (`processMQMD`), so by eye its MsgId at offset 48 reads 0x18 down to 0x01, where the core (and the golden) keep
  0x01 up to 0x18; likewise CorrelId (72), GroupId (324) and AccountingToken (208). The core keeps them by the owner's
  decision (docs/CODEMAP.md section 7). Record what Windows wrote; do not treat the difference as a failure.

### `mqmd-mqmde-text.bin`
- SHA-256: `83a44912d3a598ccf5f50f23aef5f69eed23abecaf78121059b6a17a8d1ffe83`
- Size: 456 bytes
- Bytes: 0-363 MQMD (common values; Encoding 546; CodedCharSetId 1208; Format `MQHMDE`; group fields at their defaults);
  364-435 MQMDE (72 bytes; Encoding 546, CodedCharSetId 1208, Format `MQSTR`; GroupId bytes 0x81 to 0x98 at 396,
  MsgSeqNumber 2 at 420, Offset 10 at 424, MsgFlags 8 (message in group) at 428, OriginalLength 42 at 432);
  436-455 the text `Body after an MQMDE.` (20 bytes).
- Read menu: all off. Write menu: **`Include MQMD` on**.
- Open message: `20 bytes read from file <path>` (the MQMDE is removed from the data and not counted). The core then
  shows Seq No 2, Offset 10, Orig Len 42 and Group checked on the MQMD tab, taken from the MQMDE.
  Save message in the core: `456 bytes (data 20 mqmd 364) written to file <path>`.
- Expected: `DIFFERENT (expected: ...)`, for two reasons, both from reading the Windows source and UNVERIFIED on Windows:
  1. `DataArea::parseMsgHeaders` passes the tab's `NUMERIC_PC` (0) to `MQMDPAGE::processMQMDE`, which hands it to
     `getIntEncode`; that reads 0 as an MQ encoding without the reversed-integer bit and answers host, so Windows swaps
     the MQMDE integers although the file is little endian. The MQMD tab then shows byte-reversed values
     (Seq No 33554432 in place of 2) and the code page and encoding for the body come from the swapped fields.
  2. A comment in `parseMsgHeaders` says RFHUtil never produces an MQMDE, and no tab builds one: Windows does not write the
     MQMDE back, so its file would be 384 bytes (MQMD and text) where the golden and the core's file are 456.
  The core reads the MQMDE at its real offset (Windows uses an offset scaled by the structure size, which only matters when
  another header precedes it, as here it does not), swaps only host data, and writes it back unchanged.
  Record what Windows shows and writes.

## Known differences from Windows, in one place

| File | Windows | Core (and so the golden) |
|---|---|---|
| `mqmd-bigendian.bin` | Reverses MsgId, CorrelId, GroupId and AccountingToken of a big-endian MQMD | Keeps them (owner's decision, CODEMAP section 7) |
| `mqmd-mqmde-text.bin` | Swaps the MQMDE integers whatever the encoding; reads it at an offset scaled by its size after another header; does not write it back | Reads it at its real offset, swaps only host data, writes it back unchanged (`rfh_header_chain.h`) |
| `mqmd-ebcdic-host.bin` | Writes Encoding 529 for a read 785 (float format not read from the MQMD) | The same; the golden holds the z/OS value 785 |

## Adding files the owner supplies

Files written by Windows RFHUtil are committed only with the owner's explicit approval. Then:

1. Put each in `tests/core/golden/windows/` (a sub-directory: the generator never writes there and the checks of the
   generated set ignore it), named `<what-it-is>.bin` or `.txt`, no larger than 4 KB. Never overwrite a generated file
   with one from Windows; if they differ, that is a finding, and the generated file stays as the reference.
2. Add a `### \`windows/<name>\`` entry to this file: what it is, how it was made (RFHUtil build or version,
   menu options, `File Code Page`, steps), size, byte ranges and its SHA-256 (`sha256sum FILE`).
3. In `GoldenFileTests.cpp`, add a row to the table of expectations written by hand from the layouts (not from a read),
   and the `Golden.<name>ReadsAsExpected`, `...RoundTripsByteForByte` and `...MatchesItsManifestHash` tests.
4. Do not change `make_goldens.cpp` for them. If the Windows file shows that a generated file is wrong, change the
   generator and this file together, run `build/linux-debug/tests/rfh_make_goldens tests/core/golden`, and update
   the sizes and hashes here.

## Regenerating

`cmake --build --preset linux-debug` builds `rfh_make_goldens`; `build/linux-debug/tests/rfh_make_goldens tests/core/golden`
rewrites the files (it only writes, and exits non-zero on any I/O error). The test `GoldenGenerator.RegeneratesIdenticalFiles`
runs it into a temporary directory and compares. A changed file changes its SHA-256 here and fails its
`Golden.<name>MatchesItsManifestHash` test until this file is updated on purpose.
