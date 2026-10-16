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

    // MQMD version 2 with the MQMD2_DEFAULT numeric values and blank-filled text fields,
    // as mqmd_fields::toMqmd writes them (the C MQMD2_DEFAULT has zero bytes in the text
    // fields).
    inline Bytes defaultMqmd2(Order order = Order::Little)
    {
        Bytes b(364);
        b.putChars(0, 4, "MD  ");
        b.putLong(4, 2, order);             // Version
        b.putLong(8, 0, order);             // Report MQRO_NONE
        b.putLong(12, 8, order);            // MsgType MQMT_DATAGRAM
        b.putLong(16, -1, order);           // Expiry MQEI_UNLIMITED
        b.putLong(20, 0, order);            // Feedback MQFB_NONE
        b.putLong(24, 546, order);          // Encoding MQENC_NATIVE (x86)
        b.putLong(28, 0, order);            // CodedCharSetId MQCCSI_Q_MGR
        b.putChars(32, 8, "");              // Format MQFMT_NONE
        b.putLong(40, -1, order);           // Priority MQPRI_PRIORITY_AS_Q_DEF
        b.putLong(44, 2, order);            // Persistence MQPER_PERSISTENCE_AS_Q_DEF
        // MsgId 48, CorrelId 72: zeros
        b.putLong(96, 0, order);            // BackoutCount
        b.putChars(100, 48, "");            // ReplyToQ
        b.putChars(148, 48, "");            // ReplyToQMgr
        b.putChars(196, 12, "");            // UserIdentifier
        // AccountingToken 208: zeros
        b.putChars(240, 32, "");            // ApplIdentityData
        b.putLong(272, 0, order);           // PutApplType MQAT_NO_CONTEXT
        b.putChars(276, 28, "");            // PutApplName
        // PutDate 304 and PutTime 312: zeros in the C MQMD2_DEFAULT
        b.putChars(320, 4, "");             // ApplOriginData
        // GroupId 324: zeros
        b.putLong(348, 1, order);           // MsgSeqNumber
        b.putLong(352, 0, order);           // Offset
        b.putLong(356, 0, order);           // MsgFlags MQMF_NONE
        b.putLong(360, -1, order);          // OriginalLength MQOL_UNDEFINED
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
        b.putLong(28, 0, order);            // Flags
        b.putLong(32, 1208, order);         // NameValueCCSID
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
        b.putLong(8, 2085, order);          // Reason
        b.putChars(12, 48, "ORIG.Q");
        b.putChars(60, 48, "QM1");
        b.putLong(108, encoding, order);
        b.putLong(112, ccsid, order);
        b.putChars(116, 8, format);
        b.putLong(124, 6, order);           // PutApplType
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
