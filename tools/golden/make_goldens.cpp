// make_goldens.cpp - writes the golden message files of tests/core/golden/.
//
//     rfh_make_goldens OUTPUT_DIRECTORY
//
// Every byte is built from the layouts IBM documents for the MQ structures, with
// tests/core/mq_layout.h only. This program deliberately does not include or link
// the core (fileio, mqmd_fields, walkHeaders): the goldens must not depend on the
// code they test. The output is deterministic (no clock, no environment) and the
// exit status is non-zero on any I/O error. See tests/core/golden/MANIFEST.md.

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "mq_layout.h"

namespace L = mq_layout;
namespace O = mq_layout::mqmd;
using L::Order;
using Bytes = std::vector<unsigned char>;

namespace
{
    // MQ constants named by the documentation
    const int32_t kEncodingIntelPc = 546;       // MQENC_NATIVE on x86: integers reversed (0x02), decimal reversed (0x20), IEEE floats reversed (0x200)
    const int32_t kEncodingS390 = 785;          // z/OS: integers normal (0x01), decimal normal (0x10), S/390 floats (0x300)
    const int32_t kEncodingBigEndianUnix = 273; // AIX, Solaris: integers, decimal and IEEE floats normal (0x01 | 0x10 | 0x100)
    const int32_t kMsgInGroup = 8;              // MQMF_MSG_IN_GROUP
    const int32_t kMsgTypeRequest = 1;          // MQMT_REQUEST

    // 24 (or n) bytes counting up from start: distinct, non-palindromic ids make a byte reversal visible
    std::string counting(unsigned char start, size_t n)
    {
        std::string s;
        for (size_t i = 0; i < n; i++)
        {
            s.push_back((char)(start + i));
        }
        return s;
    }

    // MQMD version 2: StrucId "MD  ", Version 2, the Format, CCSID and Encoding given, and the
    // other numeric fields as the MQMD tab of RFHUtil writes them for a non-persistent
    // datagram (Persistence 0, Priority 0, Expiry unlimited), with a fixed put date and time.
    L::Bytes mqmd(Order order, int32_t encoding, int32_t ccsid, const std::string & format)
    {
        L::Bytes b = L::defaultMqmd2(order);
        b.putLong(O::Encoding, encoding, order);
        b.putLong(O::CodedCharSetId, ccsid, order);
        b.putChars(O::Format, 8, format);
        b.putLong(O::Persistence, 0, order);
        b.putLong(O::Priority, 0, order);
        b.putLong(O::Expiry, -1, order);
        b.putChars(O::PutDate, 8, "20261017");      // YYYYMMDD
        b.putChars(O::PutTime, 8, "09301500");      // HHMMSSTH
        return b;
    }

    // One RFH2 name/value folder: a 4-byte length (little-endian) and the XML text padded
    // with blanks to a multiple of four bytes.
    std::string folder(std::string xml)
    {
        while (xml.size() % 4 != 0)
        {
            xml.push_back(' ');
        }
        L::Bytes length(4);
        length.putLong(0, (int32_t)xml.size());
        return std::string((const char *)length.data.data(), 4) + xml;
    }

    std::string nameValues()
    {
        return folder("<mcd><Msd>xml</Msd></mcd>") + folder("<usr><k>v</k></usr>");
    }

    std::pair<std::string, Bytes> file(const char * name, const Bytes & bytes)
    {
        return std::make_pair(std::string(name), bytes);
    }

    std::vector<std::pair<std::string, Bytes>> build()
    {
        std::vector<std::pair<std::string, Bytes>> files;

        // plain text, with and without CR LF
        files.push_back(file("plain-crlf.txt", L::text("Golden message file.\r\nThe quick brown fox jumps.\r\nEnd.\r\n")));
        files.push_back(file("plain-lf.txt", L::text("Golden message file.\nThe quick brown fox jumps.\nEnd.\n")));

        // every byte value, twice
        Bytes all;
        for (int pass = 0; pass < 2; pass++)
        {
            for (int i = 0; i < 256; i++)
            {
                all.push_back((unsigned char)i);
            }
        }
        files.push_back(file("binary-all-bytes.bin", all));

        // EBCDIC text, CCSID 500: long enough (at least 64 bytes, mostly letters) for
        // DataArea::checkForEBCDIC to recognise it
        files.push_back(file("ebcdic-500.bin", L::ebcdic(
            "The quick brown fox jumps over the lazy dog, 0123456789. "
            "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG: it is EBCDIC text in code page 500.")));

        // MQMD version 2 (little endian, CCSID 1208, Format MQSTR), with most fields set
        {
            L::Bytes md = mqmd(Order::Little, kEncodingIntelPc, 1208, "MQSTR");
            md.putLong(O::MsgType, kMsgTypeRequest);
            md.putLong(O::Priority, 5);
            md.putLong(O::Persistence, 1);
            md.putLong(O::Expiry, 6000);
            md.putBytes(O::MsgId, counting(0x01, 24).data(), 24);
            md.putBytes(O::CorrelId, counting(0x21, 24).data(), 24);
            md.putChars(O::ReplyToQ, 48, "REPLY.QUEUE");
            md.putChars(O::ReplyToQMgr, 48, "QM1");
            md.putChars(O::UserIdentifier, 12, "tyron");
            md.putChars(O::ApplIdentityData, 32, "golden");
            md.putChars(O::PutApplName, 28, "rfhgolden");
            files.push_back(file("mqmd-v2-text.bin", L::join({ md.data, L::text("Text after a version 2 MQMD, caf\xc3\xa9.") })));
        }

        // host data: the MQMD is written by a PC (little endian) and says the data is S/390 EBCDIC
        {
            L::Bytes md = mqmd(Order::Little, kEncodingS390, 500, "MQSTR");
            files.push_back(file("mqmd-ebcdic-host.bin", L::join({ md.data, L::ebcdic("HELLO FROM A HOST MQMD, 0123456789.") })));
        }

        // MQMD, RFH2 with two folders, text
        {
            L::Bytes md = mqmd(Order::Little, kEncodingIntelPc, 1208, "MQHRF2");
            L::Bytes rfh = L::rfh2(Order::Little, kEncodingIntelPc, 1208, "MQSTR", nameValues());
            files.push_back(file("mqmd-rfh2-text.bin", L::join({ md.data, rfh.data, L::text("<a>hi</a>") })));
        }

        // a leading RFH2 and no MQMD
        {
            L::Bytes rfh = L::rfh2(Order::Little, kEncodingIntelPc, 1208, "MQSTR", nameValues());
            files.push_back(file("rfh2-only.bin", L::join({ rfh.data, L::text("Body after a leading RFH2, no MQMD.") })));
        }

        // MQMD naming a dead letter header, which names an RFH2
        {
            L::Bytes md = mqmd(Order::Little, kEncodingIntelPc, 1208, "MQDEAD");
            L::Bytes dead = L::dlh(Order::Little, kEncodingIntelPc, 1208, "MQHRF2");
            L::Bytes rfh = L::rfh2(Order::Little, kEncodingIntelPc, 819, "MQSTR", nameValues());
            files.push_back(file("mqmd-dlh-rfh2-text.bin", L::join({ md.data, dead.data, rfh.data, L::text("Dead letter body.") })));
        }

        // a big-endian MQMD (every number most significant byte first) with all four ids set
        {
            L::Bytes md = mqmd(Order::Big, kEncodingBigEndianUnix, 819, "MQSTR");
            md.putLong(O::Priority, 4, Order::Big);
            md.putLong(O::Persistence, 1, Order::Big);
            md.putLong(O::Expiry, 3000, Order::Big);
            md.putBytes(O::MsgId, counting(0x01, 24).data(), 24);
            md.putBytes(O::CorrelId, counting(0x21, 24).data(), 24);
            md.putBytes(O::GroupId, counting(0x41, 24).data(), 24);
            md.putBytes(O::AccountingToken, counting(0x61, 32).data(), 32);
            files.push_back(file("mqmd-bigendian.bin", L::join({ md.data, L::text("Big-endian MQMD body.") })));
        }

        // MQMD (default group fields) naming an MQMDE that holds the real ones
        {
            L::Bytes md = mqmd(Order::Little, kEncodingIntelPc, 1208, "MQHMDE");
            L::Bytes extension = L::mde(Order::Little, kEncodingIntelPc, 1208, "MQSTR", 2, 10, kMsgInGroup, 42, counting(0x81, 24));
            files.push_back(file("mqmd-mqmde-text.bin", L::join({ md.data, extension.data, L::text("Body after an MQMDE.") })));
        }

        return files;
    }

    bool writeFile(const std::string & path, const Bytes & bytes)
    {
        FILE * f = fopen(path.c_str(), "wb");
        if (f == nullptr)
        {
            fprintf(stderr, "rfh_make_goldens: cannot create %s: %s\n", path.c_str(), strerror(errno));
            return false;
        }
        bool ok = (fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size());
        if (fclose(f) != 0)
        {
            ok = false;
        }
        if (!ok)
        {
            fprintf(stderr, "rfh_make_goldens: cannot write %s\n", path.c_str());
        }
        return ok;
    }
}

int main(int argc, char ** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: rfh_make_goldens OUTPUT_DIRECTORY\n");
        return 2;
    }

    try
    {
        for (const auto & f : build())
        {
            if (!writeFile(std::string(argv[1]) + "/" + f.first, f.second))
            {
                return 1;
            }
        }
    }
    catch (const std::exception & e)
    {
        fprintf(stderr, "rfh_make_goldens: %s\n", e.what());
        return 1;
    }
    return 0;
}
