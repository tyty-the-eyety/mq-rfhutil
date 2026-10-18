// pcf_server.h - a command server for the fake MQI, for the Qt tests of Load Names.
//
// The messages are built byte by byte from the IBM layouts (MQCFH 36 bytes, MQCFIN 16, MQCFST 20 plus the string padded to 4),
// as tests/core/ListQueuesTests.cpp does, not with the core's builders. installCommandServer() plays SYSTEM.ADMIN.COMMAND.QUEUE:
// it reads the queue type of an Inquire Queue request (MQIA_Q_TYPE, the Value of the first MQCFIN after the name) and puts the
// scripted replies on the request's ReplyToQ. A type without a script answers "none found" (reason 3200), as a queue manager does.
#pragma once

#include <cmqc.h>
#include <cmqcfc.h>

#include <map>
#include <string>
#include <vector>

#include "fake_mq.h"

namespace pcf_test
{
    using Bytes = std::vector<unsigned char>;

    inline void i32(Bytes & b, long value)
    {
        for (int k = 0; k < 4; k++)
        {
            b.push_back((unsigned char)(((unsigned long)value >> (8 * k)) & 0xFF));
        }
    }

    inline Bytes header(long type, long control, long compCode, long reason, long count)
    {
        Bytes b;
        for (long v : { type, 36L, 3L, 13L, 1L, control, compCode, reason, count })
        {
            i32(b, v);
        }
        return b;
    }

    // One reply to Inquire Queue: the 48 character name, then the type.
    inline Bytes queueReply(const std::string & name, long queueType, long control)
    {
        Bytes b = header(MQCFT_XR_ITEM, control, 0, 0, 2);
        std::string padded = name + std::string(48 - name.size(), ' ');
        i32(b, 4); i32(b, 20 + 48); i32(b, 2016); i32(b, 819); i32(b, 48);
        b.insert(b.end(), padded.begin(), padded.end());
        i32(b, 3); i32(b, 16); i32(b, 20); i32(b, queueType);
        return b;
    }

    inline Bytes noneFound()
    {
        return header(MQCFT_XR_ITEM, 1, MQCC_FAILED, 3200, 0);
    }

    // names by queue type; the last name of a type carries the "last" control.
    inline void installCommandServer(rfh_test::FakeMq & fake, const std::map<long, std::vector<std::string>> & script)
    {
        fake.onPut["SYSTEM.ADMIN.COMMAND.QUEUE"] = [&fake, script](const rfh_test::FakeMessage & request)
        {
            const unsigned char * v = &request.data[36 + 24 + 12];
            const long type = (long)(v[0] | (v[1] << 8) | (v[2] << 16) | ((unsigned long)v[3] << 24));
            std::string replyTo(request.md.ReplyToQ, strnlen(request.md.ReplyToQ, sizeof(request.md.ReplyToQ)));
            while (!replyTo.empty() && replyTo.back() == ' ')
            {
                replyTo.pop_back();
            }
            std::vector<Bytes> replies;
            const auto found = script.find(type);
            if (found == script.end() || found->second.empty())
            {
                replies.push_back(noneFound());
            }
            else
            {
                for (size_t i = 0; i < found->second.size(); i++)
                {
                    replies.push_back(queueReply(found->second[i], type, (i + 1 == found->second.size()) ? 1 : 0));
                }
            }
            for (const Bytes & reply : replies)
            {
                rfh_test::FakeMessage m;
                m.md.Encoding = MQENC_NATIVE;
                m.md.CodedCharSetId = 819;
                m.data = reply;
                fake.queue(replyTo).messages.push_back(m);
            }
        };
    }
}
