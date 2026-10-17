// GoldenFileTests.cpp - the golden message files in tests/core/golden/.
//
// The files are written by tools/golden/make_goldens.cpp from the documented MQ
// structure layouts (tests/core/mq_layout.h only), never by the core's fileio, so
// they do not depend on the code they test. This file reads them back through
// fileio, writes them out again, checks their SHA-256 against MANIFEST.md and
// regenerates them. Everything expected below is written by hand from the layouts
// and from DataArea::ReadFileData; none of it is copied from a read.
//
// Two files differ from Windows on purpose: mqmd-bigendian.bin (Windows swaps the
// bytes inside each 4-byte word of the ids of a big-endian MQMD, the core keeps them)
// and mqmd-mqmde-text.bin
// (Windows swaps the MQMDE integers whatever the encoding). A third, mqmd-ebcdic-host.bin,
// does not round-trip because reading an MQMD does not set the float format on Windows
// either, so the Encoding is rebuilt without the S/390 float bits. For those three the
// round-trip test asserts the core's documented behaviour.

#include <gtest/gtest.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <cmqc.h>

#include "mq_layout.h"
#include "rfh_format.h"
#include "rfh_message_file.h"
#include "rfhconst.h"

using namespace rfh;
namespace L = mq_layout;
namespace O = mq_layout::mqmd;
using Bytes = std::vector<unsigned char>;

// set by tests/CMakeLists.txt
#ifndef RFH_GOLDEN_DIR
#error "RFH_GOLDEN_DIR is not defined"
#endif

namespace
{
    const std::string kGoldenDir = RFH_GOLDEN_DIR;
    const std::string kGenerator = RFH_MAKE_GOLDENS;
    const std::string kCompareScript = RFH_COMPARE_SCRIPT;

    const char * const kFiles[] = {
        "plain-crlf.txt", "plain-lf.txt", "binary-all-bytes.bin", "ebcdic-500.bin",
        "mqmd-v2-text.bin", "mqmd-ebcdic-host.bin", "mqmd-rfh2-text.bin", "rfh2-only.bin",
        "mqmd-dlh-rfh2-text.bin", "mqmd-bigendian.bin", "mqmd-mqmde-text.bin" };

    Bytes load(const std::string & path)
    {
        std::ifstream in(path, std::ios::binary);
        return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    void save(const std::string & path, const Bytes & bytes)
    {
        std::ofstream(path, std::ios::binary).write((const char *)bytes.data(), (std::streamsize)bytes.size());
    }

    Bytes golden(const std::string & name)
    {
        return load(kGoldenDir + "/" + name);
    }

    std::string scratchDir()
    {
        std::string pattern = ::testing::TempDir() + "rfhgoldenXXXXXX";
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back(0);
        EXPECT_NE(nullptr, mkdtemp(buffer.data()));
        return buffer.data();
    }

    void removeTree(const std::string & dir)
    {
        if (DIR * d = opendir(dir.c_str()))
        {
            while (dirent * e = readdir(d))
            {
                if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0)
                {
                    unlink((dir + "/" + e->d_name).c_str());
                }
            }
            closedir(d);
        }
        rmdir(dir.c_str());
    }

    std::vector<std::string> listFiles(const std::string & dir)
    {
        std::vector<std::string> names;
        if (DIR * d = opendir(dir.c_str()))
        {
            while (dirent * e = readdir(d))
            {
                struct stat info;
                if (stat((dir + "/" + e->d_name).c_str(), &info) == 0 && S_ISREG(info.st_mode))
                {
                    names.push_back(e->d_name);
                }
            }
            closedir(d);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    // Runs a shell command; returns its output and sets status to the exit code.
    std::string run(const std::string & command, int & status)
    {
        std::string out;
        FILE * pipe = popen((command + " 2>&1").c_str(), "r");
        if (pipe == nullptr)
        {
            status = -1;
            return out;
        }
        char buffer[512];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
        {
            out.append(buffer, n);
        }
        int raw = pclose(pipe);
        status = WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
        return out;
    }

    // --- SHA-256 (FIPS 180-4), small and public-domain style ------------------------

    std::string sha256Hex(const Bytes & message)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
        uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };

        Bytes padded(message);
        padded.push_back(0x80);
        while (padded.size() % 64 != 56)
        {
            padded.push_back(0);
        }
        uint64_t bits = (uint64_t)message.size() * 8;
        for (int i = 7; i >= 0; i--)
        {
            padded.push_back((unsigned char)(bits >> (8 * i)));
        }

        auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
        for (size_t block = 0; block < padded.size(); block += 64)
        {
            uint32_t w[64];
            for (int i = 0; i < 16; i++)
            {
                const unsigned char * p = &padded[block + 4 * i];
                w[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
            }
            for (int i = 16; i < 64; i++)
            {
                uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            uint32_t v[8];
            memcpy(v, h, sizeof(v));
            for (int i = 0; i < 64; i++)
            {
                uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
                uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
                uint32_t t1 = v[7] + s1 + ch + k[i] + w[i];
                uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
                uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
                uint32_t t2 = s0 + maj;
                memmove(v + 1, v, 7 * sizeof(uint32_t));
                v[4] += t1;
                v[0] = t1 + t2;
            }
            for (int i = 0; i < 8; i++)
            {
                h[i] += v[i];
            }
        }
        char hex[65];
        for (int i = 0; i < 8; i++)
        {
            snprintf(hex + 8 * i, 9, "%08x", h[i]);
        }
        return hex;
    }

    // MANIFEST.md is the single record of the hashes: each file has a heading
    // "### `name`" followed, before the next heading, by a line "- SHA-256: `hex`".
    std::map<std::string, std::string> manifestHashes()
    {
        std::map<std::string, std::string> hashes;
        std::ifstream in(kGoldenDir + "/MANIFEST.md");
        std::string line, current;
        while (std::getline(in, line))
        {
            if (line.rfind("### `", 0) == 0 && line.size() > 6 && line.back() == '`')
            {
                current = line.substr(5, line.size() - 6);
            }
            else if (line.rfind("#", 0) == 0)
            {
                current.clear();
            }
            else if (!current.empty() && line.rfind("- SHA-256: `", 0) == 0 && line.back() == '`')
            {
                hashes[current] = line.substr(12, line.size() - 13);
            }
        }
        return hashes;
    }

    // --- what each file is, written by hand from the layouts ------------------------

    struct Expect
    {
        std::string file;
        int fileCcsidSetting;           // the File Code Page setting the read uses (Windows default 437)
        bool hadMqmd;
        std::vector<std::pair<HeaderType, size_t>> headers;     // type and length, in order
        Bytes body;
        int fileCcsid;
        int charFormat;
        int fileIntFormat;
        // What the Data tab uses for the body, from the end of DataArea::parseMsgHeaders:
        // m_numeric_format is the encoding of the data after the last header; when it is
        // PC, packed decimal is PC and floats are FLOAT_PC, otherwise both are host and
        // floats are FLOAT_390 (also after DataArea::checkForEBCDIC).
        int numericFormat;
        int pdFormat;
        int floatFormat;
    };

    Bytes allBytesTwice()
    {
        Bytes b;
        for (int pass = 0; pass < 2; pass++)
        {
            for (int i = 0; i < 256; i++)
            {
                b.push_back((unsigned char)i);
            }
        }
        return b;
    }

    const char kEbcdicText[] =
        "The quick brown fox jumps over the lazy dog, 0123456789. "
        "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG: it is EBCDIC text in code page 500.";

    // The RFH2 of mqmd-rfh2-text.bin and rfh2-only.bin: 36 fixed bytes, then two
    // folders, each a 4-byte length and text padded with blanks to a multiple of 4:
    // (4 + 28) + (4 + 20) = 56, so StrucLength is 92.
    const size_t kRfh2Length = 92;

    const std::map<std::string, Expect> & expectations()
    {
        static const std::map<std::string, Expect> table = [] {
            std::map<std::string, Expect> t;
            auto add = [&t](Expect e) { t[e.file] = e; };
            const int kPc = NUMERIC_PC, kHost = NUMERIC_HOST, kF390 = FLOAT_390;

            // The numeric, packed decimal and float formats follow parseMsgHeaders, written by
            // hand from the Windows source. The core agrees for every file except
            // mqmd-mqmde-text.bin: Windows takes the encoding after an MQMDE from the swapped,
            // raw MQENC field (a number that is not NUMERIC_PC), so its formats there are
            // not PC; the row is the core's documented behaviour (PC).
            //
            // no MQMD: the code page is the File Code Page setting (437), the encoding PC
            add({ "plain-crlf.txt", 437, false, {}, L::text("Golden message file.\r\nThe quick brown fox jumps.\r\nEnd.\r\n"),
                  437, CHAR_ASCII, kPc, kPc, kPc, FLOAT_PC });
            add({ "plain-lf.txt", 437, false, {}, L::text("Golden message file.\nThe quick brown fox jumps.\nEnd.\n"),
                  437, CHAR_ASCII, kPc, kPc, kPc, FLOAT_PC });
            add({ "binary-all-bytes.bin", 437, false, {}, allBytesTwice(), 437, CHAR_ASCII, kPc, kPc, kPc, FLOAT_PC });

            // a blank File Code Page lets RFHUtil guess EBCDIC from the data; the file code
            // page then stays the system default (1252 here), as in DataArea::checkForEBCDIC
            add({ "ebcdic-500.bin", 0, false, {}, L::ebcdic(kEbcdicText), 1252, CHAR_EBCDIC, kPc, kHost, kHost, kF390 });

            // an MQMD gives the file code page, encoding and character set
            add({ "mqmd-v2-text.bin", 437, true, {}, L::text("Text after a version 2 MQMD, caf\xc3\xa9."),
                  1208, CHAR_ASCII, kPc, kPc, kPc, FLOAT_PC });
            add({ "mqmd-ebcdic-host.bin", 437, true, {}, L::ebcdic("HELLO FROM A HOST MQMD, 0123456789."),
                  500, CHAR_EBCDIC, kHost, kHost, kHost, kF390 });
            add({ "mqmd-rfh2-text.bin", 437, true, { { HeaderType::Rfh2, kRfh2Length } }, L::text("<a>hi</a>"),
                  1208, CHAR_ASCII, kPc, kPc, kPc, FLOAT_PC });
            // no MQMD: the leading RFH2 is found, but the file code page stays the setting
            add({ "rfh2-only.bin", 437, false, { { HeaderType::Rfh2, kRfh2Length } }, L::text("Body after a leading RFH2, no MQMD."),
                  437, CHAR_ASCII, kPc, kPc, kPc, FLOAT_PC });
            add({ "mqmd-dlh-rfh2-text.bin", 437, true,
                  { { HeaderType::DeadLetter, 172 }, { HeaderType::Rfh2, kRfh2Length } }, L::text("Dead letter body."),
                  1208, CHAR_ASCII, kPc, kPc, kPc, FLOAT_PC });
            // encoding 273 has the integer-normal bit: host (big-endian) integers
            add({ "mqmd-bigendian.bin", 437, true, {}, L::text("Big-endian MQMD body."), 819, CHAR_ASCII, kHost, kHost, kHost, kF390 });
            add({ "mqmd-mqmde-text.bin", 437, true, { { HeaderType::MdExtension, 72 } }, L::text("Body after an MQMDE."),
                  1208, CHAR_ASCII, kPc, kPc, kPc, FLOAT_PC });
            return t;
        }();
        return table;
    }

    fileio::ReadOptions readOptions(const Expect & e)
    {
        fileio::ReadOptions o;
        o.fileCcsid = e.fileCcsidSetting;
        o.ansiCcsid = 1252;
        return o;
    }

    struct ReadBack
    {
        MqmdFields fields;
        fileio::ReadResult result;
    };

    bool readGolden(const Expect & e, ReadBack & r)
    {
        Result res = fileio::read(kGoldenDir + "/" + e.file, readOptions(e), r.fields, r.result);
        EXPECT_TRUE(res.ok()) << res.text;
        return res.ok();
    }

    void checkReadsAsExpected(const char * file)
    {
        const Expect & e = expectations().at(file);
        ReadBack r;
        ASSERT_TRUE(readGolden(e, r));
        EXPECT_EQ(e.hadMqmd, r.result.hadMqmd);
        ASSERT_EQ(e.headers.size(), r.result.chain.headers.size());
        size_t headerBytes = 0;
        for (size_t i = 0; i < e.headers.size(); i++)
        {
            EXPECT_EQ((int)e.headers[i].first, (int)r.result.chain.headers[i].type) << "header " << i;
            EXPECT_EQ(e.headers[i].second, r.result.chain.headers[i].length) << "header " << i;
            headerBytes += e.headers[i].second;
        }
        EXPECT_EQ(headerBytes, r.result.message.headers.size());
        EXPECT_EQ(e.body, r.result.message.body);
        EXPECT_EQ(e.fileCcsid, r.result.fileCcsid);
        EXPECT_EQ(e.charFormat, r.result.charFormat);
        EXPECT_EQ(e.fileIntFormat, r.result.fileIntFormat);
        EXPECT_EQ(e.numericFormat, r.result.numericFormat);
        EXPECT_EQ(e.pdFormat, r.result.pdFormat);
        EXPECT_EQ(e.floatFormat, r.result.floatFormat);
    }

    // Reads the golden, writes it with includeMqmd = hadMqmd, returns the bytes written.
    Bytes readAndWriteBack(const char * file)
    {
        const Expect & e = expectations().at(file);
        ReadBack r;
        if (!readGolden(e, r))
        {
            return Bytes();
        }
        std::string dir = scratchDir();
        std::string out = dir + "/out";
        fileio::WriteOptions w;
        w.includeMqmd = r.result.hadMqmd;
        Result res = fileio::write(out, r.result.message, w);
        EXPECT_TRUE(res.ok()) << res.text;
        Bytes written = load(out);
        removeTree(dir);
        return written;
    }

    void checkRoundTrip(const char * file)
    {
        EXPECT_EQ(golden(file), readAndWriteBack(file));
    }

    void checkHash(const char * file)
    {
        std::map<std::string, std::string> hashes = manifestHashes();
        ASSERT_EQ(1u, hashes.count(file)) << "MANIFEST.md has no SHA-256 for " << file;
        Bytes bytes = golden(file);
        ASSERT_FALSE(bytes.empty());
        EXPECT_LE(bytes.size(), 4096u) << "golden files stay small";
        EXPECT_EQ(hashes[file], sha256Hex(bytes)) << file << " changed; regenerate it or update MANIFEST.md deliberately";
    }

    // A message for the Data tab views: the body of a plain text golden file.
    std::string view(const char * file, format::View which)
    {
        const Expect & e = expectations().at(file);
        ReadBack r;
        EXPECT_TRUE(readGolden(e, r));
        format::RenderOptions options;
        options.apply(format::defaultsFor(r.result));
        options.display = { 1252, 437 };
        std::string out;
        Result res = format::render(r.result.message, which, options, out);
        EXPECT_TRUE(res.ok()) << res.text;
        return out;
    }
}

// The three tests every file has. The round trip is separate because two files
// differ from Windows.
#define GOLDEN_COMMON(Name, file) \
    TEST(Golden, Name##ReadsAsExpected) { checkReadsAsExpected(file); } \
    TEST(Golden, Name##MatchesItsManifestHash) { checkHash(file); }

#define GOLDEN_ALL(Name, file) \
    GOLDEN_COMMON(Name, file) \
    TEST(Golden, Name##RoundTripsByteForByte) { checkRoundTrip(file); }

GOLDEN_ALL(PlainCrlf, "plain-crlf.txt")
GOLDEN_ALL(PlainLf, "plain-lf.txt")
GOLDEN_ALL(BinaryAllBytes, "binary-all-bytes.bin")
GOLDEN_ALL(Ebcdic500, "ebcdic-500.bin")
GOLDEN_ALL(MqmdV2Text, "mqmd-v2-text.bin")
GOLDEN_COMMON(MqmdEbcdicHost, "mqmd-ebcdic-host.bin")
// The core writes headers back exactly as read, so these three round-trip byte for byte
// here. Windows rebuilds RFH2 and DLH headers from its header tabs when it saves, so the
// same files are identical on Windows only if it regenerates the headers byte for byte:
// UNVERIFIED (MANIFEST.md).
GOLDEN_ALL(MqmdRfh2Text, "mqmd-rfh2-text.bin")
GOLDEN_ALL(Rfh2Only, "rfh2-only.bin")
GOLDEN_ALL(MqmdDlhRfh2Text, "mqmd-dlh-rfh2-text.bin")
GOLDEN_COMMON(MqmdBigendian, "mqmd-bigendian.bin")
GOLDEN_COMMON(MqmdMqmdeText, "mqmd-mqmde-text.bin")

// --- facts about the heuristics, from the layouts and DataArea::checkForEBCDIC ------

TEST(Golden, BinaryAllBytesLooksLikeEbcdicWhenTheFileCodePageIsBlank)
{
    // 512 bytes, 0x00-0xFF twice. DataArea::checkForEBCDIC counts, per pass of 256:
    // 'A'..'z' 58 + digits 10 = 68 ascii characters, 1 blank (0x20) and 1 EBCDIC blank (0x40);
    // 0xF0..0xFA 11 + 0xC1..0xE9 41 + 0x81..0xA9 41 = 93 EBCDIC characters.
    // Twice: 188 EBCDIC (93 * 2 + 2 blanks) against 136 + 2 + 32 = 170: EBCDIC wins.
    // With the Windows default File Code Page of 437 no guess is made (the other test).
    Expect e = expectations().at("binary-all-bytes.bin");
    e.fileCcsidSetting = 0;
    ReadBack r;
    ASSERT_TRUE(readGolden(e, r));
    EXPECT_EQ(CHAR_EBCDIC, r.result.charFormat);
}

TEST(Golden, PlainLfWithUnixTextIsThePlainCrlfText)
{
    // Read > Unix Text turns each bare LF into CR LF, so the LF file reads as the CR LF file's body
    // (53 bytes plus the 3 added CRs = 56).
    const Expect & e = expectations().at("plain-lf.txt");
    fileio::ReadOptions o = readOptions(e);
    o.unixLineEnds = true;
    MqmdFields fields;
    fileio::ReadResult r;
    ASSERT_TRUE(fileio::read(kGoldenDir + "/plain-lf.txt", o, fields, r).ok());
    EXPECT_EQ(expectations().at("plain-crlf.txt").body, r.message.body);
}

// --- the MQMD tab and MQMD read from the MQMD files, by hand from the layouts ------------

namespace
{
    Bytes counting(unsigned char start, size_t n)
    {
        Bytes b;
        for (size_t i = 0; i < n; i++)
        {
            b.push_back((unsigned char)(start + i));
        }
        return b;
    }

    Bytes bytesOf(const unsigned char * p, size_t n)
    {
        return Bytes(p, p + n);
    }
}

TEST(Golden, MqmdV2TextFillsTheMqmdTab)
{
    ReadBack r;
    ASSERT_TRUE(readGolden(expectations().at("mqmd-v2-text.bin"), r));
    const MqmdFields & f = r.fields;
    EXPECT_EQ("1 Request", f.msgType);
    EXPECT_EQ("6000", f.expiry);
    EXPECT_EQ("5", f.priority);
    EXPECT_EQ(1, f.persistence);
    EXPECT_EQ("MQSTR", f.format);
    EXPECT_EQ("1208", f.ccsid);
    EXPECT_EQ(NUMERIC_PC, f.encoding);
    EXPECT_EQ("tyron", f.userId);
    EXPECT_EQ("REPLY.QUEUE", f.replyToQ);
    EXPECT_EQ("QM1", f.replyToQMgr);
    EXPECT_EQ("golden", f.applIdentity);
    EXPECT_EQ("rfhgolden", f.putApplName);
    EXPECT_EQ("2026/10/17 09:30:15.00", f.dateTime);
    EXPECT_EQ(counting(0x01, 24), bytesOf(f.msgId, 24)) << "MsgId bytes 01..18, not reversed";
    EXPECT_EQ(counting(0x21, 24), bytesOf(f.correlId, 24));

    const MQMD2 & m = r.result.message.mqmd;
    EXPECT_EQ(MQMT_REQUEST, m.MsgType);
    EXPECT_EQ(6000, m.Expiry);
    EXPECT_EQ(5, m.Priority);
    EXPECT_EQ(MQPER_PERSISTENT, m.Persistence);
    EXPECT_EQ(1208, m.CodedCharSetId);
    EXPECT_EQ(546, m.Encoding);
    EXPECT_EQ(0, memcmp("MQSTR   ", m.Format, 8));
    EXPECT_EQ(counting(0x01, 24), bytesOf(m.MsgId, 24));
    EXPECT_EQ(counting(0x21, 24), bytesOf(m.CorrelId, 24));
}

TEST(Golden, MqmdBigendianFillsTheMqmdTabWithTheIdsKept)
{
    ReadBack r;
    ASSERT_TRUE(readGolden(expectations().at("mqmd-bigendian.bin"), r));
    const MqmdFields & f = r.fields;
    EXPECT_EQ("8 Datagram", f.msgType);
    EXPECT_EQ("3000", f.expiry);
    EXPECT_EQ("4", f.priority);
    EXPECT_EQ(1, f.persistence);
    EXPECT_EQ("MQSTR", f.format);
    EXPECT_EQ("819", f.ccsid);
    EXPECT_EQ(NUMERIC_HOST, f.encoding);
    // the ids as in the file; Windows would show each 4-byte word swapped (04 03 02 01 ...)
    EXPECT_EQ(counting(0x01, 24), bytesOf(f.msgId, 24));
    EXPECT_EQ(counting(0x21, 24), bytesOf(f.correlId, 24));
    EXPECT_EQ(counting(0x41, 24), bytesOf(f.groupId, 24));
    std::string token;
    for (unsigned char c : counting(0x61, 32))
    {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", c);
        token += hex;
    }
    EXPECT_EQ(token, f.accountingToken);

    const MQMD2 & m = r.result.message.mqmd;
    EXPECT_EQ(MQMT_DATAGRAM, m.MsgType);
    EXPECT_EQ(3000, m.Expiry);
    EXPECT_EQ(4, m.Priority);
    EXPECT_EQ(MQPER_PERSISTENT, m.Persistence);
    EXPECT_EQ(819, m.CodedCharSetId);
    EXPECT_EQ(0x211, m.Encoding) << "rebuilt from the tab: integers and decimal normal, floats IEEE reversed";
    EXPECT_EQ(0, memcmp("MQSTR   ", m.Format, 8));
    EXPECT_EQ(counting(0x01, 24), bytesOf(m.MsgId, 24));
    EXPECT_EQ(counting(0x21, 24), bytesOf(m.CorrelId, 24));
    EXPECT_EQ(counting(0x41, 24), bytesOf(m.GroupId, 24));
    EXPECT_EQ(counting(0x61, 32), bytesOf(m.AccountingToken, 32));
}

// --- the Data tab views of the plain text files, derived by hand --------------------
//
// Character: 32 characters a line, CR LF shown as two periods; Hex: 16 bytes a line in
// groups of 4; Both: the hex area is 36 wide (each group of 4 bytes and a blank), then
// the characters with a blank after every 8. Offsets are 8 decimal digits, lines end
// with CR LF, and the last line has no line end.

TEST(Golden, PlainCrlfCharacterView)
{
    EXPECT_EQ("00000000 Golden message file...The quick \r\n"
              "00000032 brown fox jumps...End...",
              view("plain-crlf.txt", format::View::Character));
}

TEST(Golden, PlainCrlfHexView)
{
    EXPECT_EQ("00000000 476F6C64 656E206D 65737361 67652066\r\n"
              "00000016 696C652E 0D0A5468 65207175 69636B20\r\n"
              "00000032 62726F77 6E20666F 78206A75 6D70732E\r\n"
              "00000048 0D0A456E 642E0D0A",
              view("plain-crlf.txt", format::View::Hex));
}

TEST(Golden, PlainCrlfBothView)
{
    EXPECT_EQ("00000000 476F6C64 656E206D 65737361 67652066 Golden m essage f \r\n"
              "00000016 696C652E 0D0A5468 65207175 69636B20 ile...Th e quick  \r\n"
              "00000032 62726F77 6E20666F 78206A75 6D70732E brown fo x jumps. \r\n"
              "00000048 0D0A456E 642E0D0A                   ..End... ",
              view("plain-crlf.txt", format::View::Both));
}

TEST(Golden, PlainLfCharacterView)
{
    EXPECT_EQ("00000000 Golden message file..The quick b\r\n"
              "00000032 rown fox jumps..End..",
              view("plain-lf.txt", format::View::Character));
}

TEST(Golden, PlainLfHexView)
{
    EXPECT_EQ("00000000 476F6C64 656E206D 65737361 67652066\r\n"
              "00000016 696C652E 0A546865 20717569 636B2062\r\n"
              "00000032 726F776E 20666F78 206A756D 70732E0A\r\n"
              "00000048 456E642E 0A",
              view("plain-lf.txt", format::View::Hex));
}

TEST(Golden, PlainLfBothView)
{
    EXPECT_EQ("00000000 476F6C64 656E206D 65737361 67652066 Golden m essage f \r\n"
              "00000016 696C652E 0A546865 20717569 636B2062 ile..The  quick b \r\n"
              "00000032 726F776E 20666F78 206A756D 70732E0A rown fox  jumps.. \r\n"
              "00000048 456E642E 0A                         End..",
              view("plain-lf.txt", format::View::Both));
}

// --- the two files that differ from Windows ------------------------------------------

// What the core writes for mqmd-bigendian.bin, written by hand from the layout: the same
// MQMD in little-endian form with the ids as in the golden. The numeric fields are the
// golden's, byte-reversed to little endian. The Encoding is the one the MQMD tab rebuilds:
// integers and decimal normal (0x01 | 0x10) and, because reading an MQMD does not set the
// float format, IEEE reversed floats (0x200).
static Bytes coreBigendianOutput()
{
    Bytes original = golden("mqmd-bigendian.bin");
    EXPECT_GE(original.size(), 364u);
    // the golden MQMD is big-endian: read each number by hand
    auto be = [&](size_t offset) {
        return (int32_t)(((uint32_t)original[offset] << 24) | ((uint32_t)original[offset + 1] << 16) |
                         ((uint32_t)original[offset + 2] << 8) | original[offset + 3]);
    };
    EXPECT_EQ(2, be(4));
    EXPECT_EQ(273, be(O::Encoding));

    L::Bytes expected(0);
    expected.data = original;
    for (size_t offset : { (size_t)4, O::Report, O::MsgType, O::Expiry, O::Feedback, O::CodedCharSetId,
                           O::Priority, O::Persistence, O::BackoutCount, O::PutApplType, O::MsgSeqNumber,
                           O::Offset, O::MsgFlags, O::OriginalLength })
    {
        expected.putLong(offset, be(offset), L::Order::Little);
    }
    expected.putLong(O::Encoding, 0x01 | 0x10 | 0x200, L::Order::Little);
    return expected.data;
}

// What Windows would write for mqmd-bigendian.bin (UNVERIFIED on Windows, from the source):
// processMQMD calls reverseBytes24 on MsgId, CorrelId and GroupId and reverseBytes32 on
// AccountingToken (RFHUtil/comsubs.cpp), which reverse the bytes inside each 4-byte word
// (01 02 03 04 becomes 04 03 02 01), not the whole field; buildMQMD copies the ids back as
// they are. Everything else is what the core writes.
static Bytes windowsBigendianOutput()
{
    Bytes out = coreBigendianOutput();
    for (auto field : { std::make_pair(O::MsgId, (size_t)24), std::make_pair(O::CorrelId, (size_t)24),
                        std::make_pair(O::AccountingToken, (size_t)32), std::make_pair(O::GroupId, (size_t)24) })
    {
        for (size_t word = 0; word < field.second; word += 4)
        {
            std::reverse(out.begin() + field.first + word, out.begin() + field.first + word + 4);
        }
    }
    return out;
}

// What Windows would write for mqmd-ebcdic-host.bin: the golden with Encoding 529.
static Bytes windowsEbcdicHostOutput()
{
    L::Bytes b(0);
    b.data = golden("mqmd-ebcdic-host.bin");
    b.putLong(O::Encoding, 0x211);
    return b.data;
}

// What Windows would write for mqmd-mqmde-text.bin (UNVERIFIED, from the source): no MQMDE
// (nothing builds one) and an MQMD whose group fields come from the MQMDE read with its
// integers swapped: the group id as it is, sequence number 2 -> 0x02000000, offset 10 ->
// 0x0A000000, original length 42 -> 0x2A000000, and flags 8 -> 0x08000000, which has none of
// the five flag bits, so MsgFlags is 0.
static Bytes windowsMdeOutput()
{
    Bytes original = golden("mqmd-mqmde-text.bin");
    L::Bytes b(0);
    b.data.assign(original.begin(), original.begin() + 364);
    b.putBytes(O::GroupId, &original[364 + 32], 24);
    b.putLong(O::MsgSeqNumber, 0x02000000);
    b.putLong(O::Offset, 0x0A000000);
    b.putLong(O::MsgFlags, 0);
    b.putLong(O::OriginalLength, 0x2A000000);
    b.data.insert(b.data.end(), original.begin() + 364 + 72, original.end());
    return b.data;
}

TEST(Golden, MqmdBigendianRoundTripFollowsTheCoreNotWindows)
{
    // Windows swaps the bytes inside each 4-byte word of MsgId, CorrelId, GroupId and
    // AccountingToken of a big-endian MQMD (processMQMD); the core keeps them (CODEMAP
    // section 7, owner's decision), so what it writes has the ids exactly as in the golden.
    Bytes original = golden("mqmd-bigendian.bin");
    Bytes expected = coreBigendianOutput();
    Bytes written = readAndWriteBack("mqmd-bigendian.bin");
    EXPECT_EQ(expected, written);
    ASSERT_EQ(expected.size(), written.size());
    for (auto field : { std::make_pair(O::MsgId, (size_t)24), std::make_pair(O::CorrelId, (size_t)24),
                        std::make_pair(O::GroupId, (size_t)24), std::make_pair(O::AccountingToken, (size_t)32) })
    {
        EXPECT_EQ(Bytes(original.begin() + field.first, original.begin() + field.first + field.second),
                  Bytes(written.begin() + field.first, written.begin() + field.first + field.second));
    }
    // the first MsgId word of the golden is 01 02 03 04; Windows would write 04 03 02 01
    EXPECT_EQ((Bytes{ 1, 2, 3, 4 }), Bytes(original.begin() + O::MsgId, original.begin() + O::MsgId + 4));
    Bytes windows = windowsBigendianOutput();
    EXPECT_EQ((Bytes{ 4, 3, 2, 1, 8, 7, 6, 5 }), Bytes(windows.begin() + O::MsgId, windows.begin() + O::MsgId + 8));
}

TEST(Golden, MqmdEbcdicHostRoundTripDropsTheFloatFormat)
{
    // The golden says Encoding 785 (0x311: integers normal 0x01, decimal normal 0x10, S/390
    // floats 0x300). MQMDPAGE::processMQMD takes only the integer and decimal formats from the
    // MQMD, not the float format, and ReadFileData has set that to PC; writing rebuilds the
    // Encoding from the tab with getEncodingValue: 0x01 | 0x10 | IEEE reversed 0x200 = 529
    // (0x211). Windows does the same by the source; the core ports it. Everything else, the
    // CCSID 500 and the EBCDIC body included, is written back unchanged.
    L::Bytes expected(0);
    expected.data = golden("mqmd-ebcdic-host.bin");
    ASSERT_GE(expected.data.size(), 364u);
    ASSERT_EQ(0x311, (int32_t)(expected.data[O::Encoding] | (expected.data[O::Encoding + 1] << 8)));
    expected.putLong(O::Encoding, 0x211);
    EXPECT_EQ(expected.data, readAndWriteBack("mqmd-ebcdic-host.bin"));
}

TEST(Golden, MqmdMqmdeTextRoundTripFollowsTheCoreNotWindows)
{
    // The golden MQMD holds the default group fields (no group id, sequence 1, offset 0,
    // no flags, original length -1) and the MQMDE holds the real ones. The core reads the
    // MQMDE at its real offset, swaps it only for host data, and carries its group id,
    // sequence number, offset, flags and original length onto the MQMD tab, so the MQMD it
    // writes has them; the MQMDE and the body are written back unchanged. Windows swaps
    // the MQMDE integers whatever the encoding (rfh_header_chain.h), so it would show
    // byte-reversed values and write a different MQMD.
    Bytes original = golden("mqmd-mqmde-text.bin");
    ASSERT_EQ(364u + 72u + strlen("Body after an MQMDE."), original.size());
    // the MQMDE follows the 364-byte MQMD; in it the group id is at 32, the sequence
    // number at 56, the offset at 60, the flags at 64 and the original length at 68
    const size_t mde = 364;
    ASSERT_EQ(0, memcmp(&original[mde], "MDE ", 4));

    L::Bytes expected(0);
    expected.data = original;
    expected.putBytes(O::GroupId, &original[mde + 32], 24);
    expected.putLong(O::MsgSeqNumber, 2);
    expected.putLong(O::Offset, 10);
    expected.putLong(O::MsgFlags, MQMF_MSG_IN_GROUP);
    expected.putLong(O::OriginalLength, 42);

    // by hand: the MQMDE of the golden carries exactly these values
    auto le = [&](size_t offset) {
        return (int32_t)((uint32_t)original[offset] | ((uint32_t)original[offset + 1] << 8) |
                         ((uint32_t)original[offset + 2] << 16) | ((uint32_t)original[offset + 3] << 24));
    };
    ASSERT_EQ(2, le(mde + 56));
    ASSERT_EQ(10, le(mde + 60));
    ASSERT_EQ(MQMF_MSG_IN_GROUP, le(mde + 64));
    ASSERT_EQ(42, le(mde + 68));

    EXPECT_EQ(expected.data, readAndWriteBack("mqmd-mqmde-text.bin"));
}

// --- the generator and the comparison script -------------------------------------------

TEST(GoldenGenerator, RegeneratesIdenticalFiles)
{
    std::string dir = scratchDir();
    int status = -1;
    std::string out = run("\"" + kGenerator + "\" \"" + dir + "\"", status);
    EXPECT_EQ(0, status) << out;

    std::vector<std::string> expected(std::begin(kFiles), std::end(kFiles));
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(expected, listFiles(dir));
    for (const char * name : kFiles)
    {
        EXPECT_EQ(golden(name), load(dir + "/" + name)) << name;
    }

    // the checked-in directory holds those files and the manifest, nothing else
    // (files only: windows/ holds files supplied by the owner, see MANIFEST.md)
    std::vector<std::string> checkedIn = expected;
    checkedIn.push_back("MANIFEST.md");
    std::sort(checkedIn.begin(), checkedIn.end());
    EXPECT_EQ(checkedIn, listFiles(kGoldenDir));
    removeTree(dir);
}

TEST(GoldenGenerator, FailsOnAnIoErrorAndWithoutADirectory)
{
    int status = 0;
    run("\"" + kGenerator + "\" /nonexistent-directory-for-rfh-goldens/x", status);
    EXPECT_NE(0, status);
    run("\"" + kGenerator + "\"", status);
    EXPECT_NE(0, status);
}

namespace
{
    std::string compare(const std::string & dir, int & status, const std::string & options = std::string())
    {
        return run("bash \"" + kCompareScript + "\" " + options + " \"" + dir + "\"", status);
    }

    bool has(const std::string & out, const std::string & text)
    {
        return out.find(text) != std::string::npos;
    }

    const std::string kBigendianReason =
        "Windows writes the MQMD little endian with Encoding 529 and swaps the bytes inside each 4-byte word of MsgId, CorrelId, AccountingToken and GroupId; the core keeps the ids as they are";
}

TEST(Golden, CompareScriptReportsDifferences)
{
    std::string dir = scratchDir();
    save(dir + "/plain-crlf.txt", golden("plain-crlf.txt"));            // identical
    Bytes modified = golden("plain-lf.txt");
    modified[0] ^= 0x20;
    save(dir + "/plain-lf.txt", modified);                              // modified
    save(dir + "/mqmd-ebcdic-host.bin", windowsEbcdicHostOutput());     // known difference, as expected
    save(dir + "/notes.txt", Bytes{ 'x' });                             // not a golden name

    int status = -1;
    std::string out = compare(dir, status);
    EXPECT_EQ(1, status) << out;
    EXPECT_TRUE(has(out, "plain-crlf.txt: same\n")) << out;
    EXPECT_TRUE(has(out, "plain-lf.txt: DIFFERENT; 1 byte differs, length 53 (golden 53), first offsets: 0\n")) << out;
    EXPECT_TRUE(has(out, "mqmd-ebcdic-host.bin: DIFFERENT (expected: Windows rebuilds the MQMD Encoding without the S/390 float format (785 becomes 529), because reading a file does not set the float format; the core does the same); 1 byte differs, length 399 (golden 399), first offsets: 25\n")) << out;
    EXPECT_TRUE(has(out, "notes.txt: not a golden file (ignored)\n")) << out;
    EXPECT_TRUE(has(out, "summary: 1 same, 1 different as expected, 1 DIFFERENT unexpectedly, 8 golden files not in the directory\n")) << out;

    // without the unexpected difference the exit status is 0
    save(dir + "/plain-lf.txt", golden("plain-lf.txt"));
    out = compare(dir, status);
    EXPECT_EQ(0, status) << out;
    EXPECT_TRUE(has(out, "plain-lf.txt: same\n")) << out;

    // --all: a partial run is a failure
    compare(dir, status, "--all");
    EXPECT_EQ(1, status);

    // a directory that is not there, and no directory, are usage errors
    run("bash \"" + kCompareScript + "\" \"" + dir + "/missing\"", status);
    EXPECT_EQ(2, status);
    run("bash \"" + kCompareScript + "\"", status);
    EXPECT_EQ(2, status);
    removeTree(dir);
}

TEST(Golden, CompareScriptChecksTheShapeOfEachKnownDifference)
{
    int status = -1;
    std::string out;

    // mqmd-ebcdic-host.bin: only offset 25 (0x03 -> 0x02) is expected
    {
        std::string dir = scratchDir();
        Bytes b = windowsEbcdicHostOutput();
        save(dir + "/mqmd-ebcdic-host.bin", b);
        out = compare(dir, status);
        EXPECT_EQ(0, status) << out;
        b[200] ^= 1;
        save(dir + "/mqmd-ebcdic-host.bin", b);
        out = compare(dir, status);
        EXPECT_EQ(1, status) << out;
        EXPECT_TRUE(has(out, "mqmd-ebcdic-host.bin: DIFFERENT; 2 bytes differ")) << out;
        EXPECT_TRUE(has(out, "first offsets: 25 200")) << out;
        b = windowsEbcdicHostOutput();
        b[25] = 0x04;      // the right place, the wrong value
        save(dir + "/mqmd-ebcdic-host.bin", b);
        compare(dir, status);
        EXPECT_EQ(1, status);
        removeTree(dir);
    }

    // mqmd-bigendian.bin: the little-endian MQMD with the ids swapped inside each word
    {
        std::string dir = scratchDir();
        Bytes b = windowsBigendianOutput();
        save(dir + "/mqmd-bigendian.bin", b);
        out = compare(dir, status);
        EXPECT_EQ(0, status) << out;
        EXPECT_TRUE(has(out, "mqmd-bigendian.bin: DIFFERENT (expected: " + kBigendianReason + "); ")) << out;

        Bytes outside = b;
        outside[O::ApplIdentityData] ^= 1;          // not an id or a number
        save(dir + "/mqmd-bigendian.bin", outside);
        compare(dir, status);
        EXPECT_EQ(1, status);

        Bytes kept = coreBigendianOutput();         // the ids not swapped: the core's, not Windows'
        save(dir + "/mqmd-bigendian.bin", kept);
        compare(dir, status);
        EXPECT_EQ(1, status);

        Bytes reversedWhole = b;                    // whole-field reversal is not what Windows does
        std::reverse(reversedWhole.begin() + O::MsgId, reversedWhole.begin() + O::MsgId + 24);
        save(dir + "/mqmd-bigendian.bin", reversedWhole);
        compare(dir, status);
        EXPECT_EQ(1, status);

        Bytes shorter = b;
        shorter.pop_back();
        save(dir + "/mqmd-bigendian.bin", shorter);
        compare(dir, status);
        EXPECT_EQ(1, status);
        removeTree(dir);
    }

    // mqmd-mqmde-text.bin: 384 bytes, no MQMDE, group fields from the swapped MQMDE
    {
        std::string dir = scratchDir();
        Bytes b = windowsMdeOutput();
        ASSERT_EQ(384u, b.size());
        save(dir + "/mqmd-mqmde-text.bin", b);
        out = compare(dir, status);
        EXPECT_EQ(0, status) << out;
        EXPECT_TRUE(has(out, "mqmd-mqmde-text.bin: DIFFERENT (expected: Windows swaps the MQMDE integers")) << out;
        b[O::Priority] ^= 1;
        save(dir + "/mqmd-mqmde-text.bin", b);
        compare(dir, status);
        EXPECT_EQ(1, status);
        save(dir + "/mqmd-mqmde-text.bin", golden("mqmd-mqmde-text.bin"));    // same as the golden: fine
        out = compare(dir, status);
        EXPECT_EQ(0, status) << out;
        removeTree(dir);
    }
}
