// fake_mq.h - an in-memory MQI shared by the core and Qt tests.
//
// fakeApi() returns an MqApi whose entry points act on one FakeMq (fakeMq()).
// Tests call fakeMq().reset() first, then configure what the fake should answer
// and read back what it saw. Header-only.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "rfh_mq_session.h"

namespace rfh_test
{
    struct FakeMessage
    {
        MQMD md;
        std::vector<unsigned char> data;

        FakeMessage()
        {
            MQMD init = {MQMD_DEFAULT};
            md = init;
        }
    };

    // A queue the fake knows. A queue the test never declares answers as before
    // Task 2: a get succeeds with no data, an inquiry reports a depth of 7.
    struct FakeQueue
    {
        MQLONG type = MQQT_LOCAL;
        MQLONG maxMsgLength = 4194304;
        std::string baseQName;
        std::string resolvedQName;   // returned in MQOD.ResolvedQName by an open with MQOD version 3; empty echoes the name opened
        std::deque<FakeMessage> messages;
        bool hasDepthOverride = false;
        MQLONG depthOverride = 0;
    };

    struct FakeMq
    {
        // What the fake should answer.
        MQLONG openRc = MQRC_NONE;
        MQLONG closeRc = MQRC_NONE;
        MQLONG discRc = MQRC_NONE;
        MQLONG putRc = MQRC_NONE;
        MQLONG platform = MQPL_UNIX;
        MQLONG commandLevel = 940;
        std::map<MQLONG, MQLONG> inqRcBySelector;   // an inquiry that names one of these selectors fails with the given reason
        int unlimitedWaitMs = 1000;   // how long a get with MQWI_UNLIMITED waits in the fake
        MQLONG getRc = MQRC_NONE;     // every get fails with this reason, before it looks at the queue
        bool browseCursors = false;   // model one browse cursor per queue handle (off: a browse always returns the first message)

        // What it saw.
        int connxCalls = 0;
        int openCalls = 0;
        int closeCalls = 0;
        int discCalls = 0;
        int putCalls = 0;
        int getCalls = 0;
        int commitCalls = 0;
        int inqCalls = 0;
        MQLONG lastPmoOptions = 0;
        MQLONG lastGmoOptions = 0;
        MQLONG lastGmoWaitInterval = 0;
        MQLONG lastGmoMatchOptions = 0;
        MQLONG lastOpenOptions = 0;
        int lastWaitedMs = 0;
        std::string lastOpenedName;

        std::map<std::string, FakeQueue> queues;
        std::map<MQHOBJ, std::string> handleNames;   // object handle -> queue name
        std::map<MQHOBJ, bool> handleIsQmgr;
        std::set<MQHOBJ> liveHandles;                // opened and not yet closed or dropped by a disconnect; empty when nothing leaks
        std::map<MQHOBJ, int> cursors;               // browseCursors: index of the message under the cursor, -1 before the first

        void reset() { *this = FakeMq(); }

        FakeQueue & queue(const std::string & name) { return queues[name]; }

        FakeQueue * find(const std::string & name)
        {
            std::map<std::string, FakeQueue>::iterator it = queues.find(name);
            return (it == queues.end()) ? nullptr : &it->second;
        }
    };

    inline FakeMq & fakeMq()
    {
        static FakeMq instance;
        return instance;
    }

    namespace fake_detail
    {
        inline void report(MQLONG rc, PMQLONG cc, PMQLONG reason)
        {
            *reason = rc;
            *cc = (rc == MQRC_NONE) ? MQCC_OK : MQCC_FAILED;
        }

        inline std::string trimmed(const char * text, size_t size)
        {
            std::string s(text, strnlen(text, size));   // MQ pads with blanks; a test may pad with NULs
            size_t end = s.find_last_not_of(' ');
            return (end == std::string::npos) ? std::string() : s.substr(0, end + 1);
        }

        inline FakeQueue * queueOf(MQHOBJ h)
        {
            FakeMq & fake = fakeMq();
            std::map<MQHOBJ, std::string>::iterator it = fake.handleNames.find(h);
            return (it == fake.handleNames.end()) ? nullptr : fake.find(it->second);
        }

        inline void fakeConnx(PMQCHAR, PMQCNO, PMQHCONN hConn, PMQLONG cc, PMQLONG rc)
        {
            fakeMq().connxCalls++;
            *hConn = 1;
            report(MQRC_NONE, cc, rc);
        }

        inline void fakeDisc(PMQHCONN, PMQLONG cc, PMQLONG rc)
        {
            FakeMq & fake = fakeMq();
            fake.discCalls++;
            fake.liveHandles.clear();   // MQDISC releases every handle of the connection
            fake.cursors.clear();
            report(fake.discRc, cc, rc);
        }

        inline void fakeOpen(MQHCONN, PMQVOID objDesc, MQLONG options, PMQHOBJ hObj, PMQLONG cc, PMQLONG rc)
        {
            FakeMq & fake = fakeMq();
            fake.openCalls++;
            fake.lastOpenOptions = options;
            *hObj = fake.openCalls;

            const MQOD * od = static_cast<const MQOD *>(objDesc);
            if (od != nullptr)
            {
                fake.lastOpenedName = trimmed(od->ObjectName, sizeof(od->ObjectName));
                fake.handleNames[*hObj] = fake.lastOpenedName;
                fake.handleIsQmgr[*hObj] = (od->ObjectType == MQOT_Q_MGR);
            }
            if ((od != nullptr) && (od->Version >= MQOD_VERSION_3) && (fake.openRc == MQRC_NONE))
            {
                // an open with a version 3 descriptor reports the name the object resolved to
                MQOD * out = static_cast<MQOD *>(objDesc);
                FakeQueue * resolved = fake.find(fake.lastOpenedName);
                std::string name = ((resolved != nullptr) && !resolved->resolvedQName.empty()) ? resolved->resolvedQName : fake.lastOpenedName;
                memset(out->ResolvedQName, ' ', sizeof(out->ResolvedQName));
                memcpy(out->ResolvedQName, name.data(), std::min(sizeof(out->ResolvedQName), name.size()));
            }
            if (fake.openRc == MQRC_NONE)
            {
                fake.liveHandles.insert(*hObj);
            }
            report(fake.openRc, cc, rc);
        }

        inline void fakeClose(MQHCONN, PMQHOBJ hObj, MQLONG, PMQLONG cc, PMQLONG rc)
        {
            FakeMq & fake = fakeMq();
            fake.closeCalls++;
            if (fake.closeRc == MQRC_NONE)
            {
                fake.liveHandles.erase(*hObj);
                fake.cursors.erase(*hObj);
                *hObj = MQHO_UNUSABLE_HOBJ;   // as real MQ does on success
            }
            report(fake.closeRc, cc, rc);
        }

        inline void fakePut(MQHCONN, MQHOBJ hObj, PMQVOID md, PMQVOID pmo, MQLONG length, PMQVOID buffer, PMQLONG cc, PMQLONG rc)
        {
            FakeMq & fake = fakeMq();
            fake.putCalls++;
            if (pmo != nullptr)
            {
                fake.lastPmoOptions = static_cast<const MQPMO *>(pmo)->Options;
            }

            FakeQueue * q = queueOf(hObj);
            if ((fake.putRc == MQRC_NONE) && (q != nullptr))
            {
                FakeMessage m;
                if (md != nullptr)
                {
                    m.md = *static_cast<const MQMD *>(md);
                }
                if ((length > 0) && (buffer != nullptr))
                {
                    const unsigned char * p = static_cast<const unsigned char *>(buffer);
                    m.data.assign(p, p + length);
                }
                q->messages.push_back(m);
            }
            report(fake.putRc, cc, rc);
        }

        inline void fakeGet(MQHCONN, MQHOBJ hObj, PMQVOID md, PMQVOID gmoIn, MQLONG bufferLength, PMQVOID buffer, PMQLONG length, PMQLONG cc, PMQLONG rc)
        {
            FakeMq & fake = fakeMq();
            fake.getCalls++;
            *length = 0;

            MQGMO * gmo = static_cast<MQGMO *>(gmoIn);
            if (gmo != nullptr)
            {
                fake.lastGmoOptions = gmo->Options;
                fake.lastGmoWaitInterval = gmo->WaitInterval;
                fake.lastGmoMatchOptions = gmo->MatchOptions;
            }

            if (fake.getRc != MQRC_NONE)
            {
                report(fake.getRc, cc, rc);
                return;
            }

            FakeQueue * q = queueOf(hObj);
            if (q == nullptr)
            {
                report(MQRC_NONE, cc, rc);   // a queue the test did not declare: success, no data
                return;
            }

            const bool browseFirst = (gmo != nullptr) && ((gmo->Options & MQGMO_BROWSE_FIRST) != 0);
            const bool browseNext = (gmo != nullptr) && ((gmo->Options & MQGMO_BROWSE_NEXT) != 0);
            size_t index = 0;
            if (fake.browseCursors && (browseFirst || browseNext))
            {
                int & cursor = fake.cursors.emplace(hObj, -1).first->second;
                if (browseFirst)
                {
                    cursor = -1;   // a browse first starts over, even when it then finds nothing
                }
                index = (size_t)(cursor + 1);
            }
            const bool available = index < q->messages.size();

            fake.lastWaitedMs = 0;
            if ((!available) && (gmo != nullptr) && ((gmo->Options & MQGMO_WAIT) != 0))
            {
                // sleep in 50 ms slices up to the interval, as a waiting get would
                int total = (gmo->WaitInterval == MQWI_UNLIMITED) ? fake.unlimitedWaitMs : (int)gmo->WaitInterval;
                while (fake.lastWaitedMs < total)
                {
                    int slice = std::min(50, total - fake.lastWaitedMs);
                    std::this_thread::sleep_for(std::chrono::milliseconds(slice));
                    fake.lastWaitedMs += slice;
                }
            }

            if (!available)
            {
                report(MQRC_NO_MSG_AVAILABLE, cc, rc);
                *cc = MQCC_FAILED;
                return;
            }

            const FakeMessage & m = q->messages[index];
            *length = (MQLONG)m.data.size();
            if (md != nullptr)
            {
                *static_cast<MQMD *>(md) = m.md;
            }

            const bool accept = (gmo != nullptr) && ((gmo->Options & MQGMO_ACCEPT_TRUNCATED_MSG) != 0);
            const bool truncated = *length > bufferLength;
            if (truncated && !accept)
            {
                if ((buffer != nullptr) && (bufferLength > 0))
                {
                    memcpy(buffer, m.data.data(), (size_t)bufferLength);
                }
                report(MQRC_TRUNCATED_MSG_FAILED, cc, rc);
                return;
            }

            const MQLONG copied = truncated ? bufferLength : *length;
            if (copied > 0)
            {
                memcpy(buffer, m.data.data(), (size_t)copied);
            }
            const bool browse = browseFirst || browseNext;
            if (browse)
            {
                if (fake.browseCursors)
                {
                    fake.cursors[hObj] = (int)index;
                }
            }
            else
            {
                q->messages.pop_front();
                for (std::map<MQHOBJ, int>::iterator it = fake.cursors.begin(); it != fake.cursors.end(); ++it)
                {
                    if ((it->second >= 0) && (queueOf(it->first) == q))
                    {
                        it->second--;   // the messages after the removed one move up
                    }
                }
            }
            if (truncated)
            {
                *rc = MQRC_TRUNCATED_MSG_ACCEPTED;   // accepted truncation: the message is delivered with a warning
                *cc = MQCC_WARNING;
                return;
            }
            report(MQRC_NONE, cc, rc);
        }

        // Integer selectors come first in the selector list, character selectors after.
        inline void fakeInq(MQHCONN, MQHOBJ hObj, MQLONG selectorCount, PMQLONG selectors, MQLONG intCount, PMQLONG intAttrs, MQLONG charLength, PMQCHAR charAttrs, PMQLONG cc, PMQLONG rc)
        {
            FakeMq & fake = fakeMq();
            fake.inqCalls++;
            for (MQLONG i = 0; i < selectorCount; i++)
            {
                std::map<MQLONG, MQLONG>::const_iterator failure = fake.inqRcBySelector.find(selectors[i]);
                if (failure != fake.inqRcBySelector.end())
                {
                    report(failure->second, cc, rc);
                    return;
                }
            }

            FakeQueue * q = queueOf(hObj);
            bool qmgr = fake.handleIsQmgr.count(hObj) && fake.handleIsQmgr[hObj];

            MQLONG ints = 0;
            MQLONG charUsed = 0;
            for (MQLONG i = 0; i < selectorCount; i++)
            {
                MQLONG s = selectors[i];
                if (s >= MQCA_FIRST)
                {
                    if (s == MQCA_BASE_Q_NAME)
                    {
                        MQLONG width = MQ_Q_NAME_LENGTH;
                        if ((charAttrs != nullptr) && (charUsed + width <= charLength))
                        {
                            std::string name = (q != nullptr) ? q->baseQName : std::string();
                            memset(charAttrs + charUsed, ' ', (size_t)width);
                            memcpy(charAttrs + charUsed, name.data(), std::min((size_t)width, name.size()));
                        }
                        charUsed += width;
                    }
                    continue;
                }

                if (ints >= intCount)
                {
                    continue;
                }
                MQLONG value = 0;
                if (qmgr)
                {
                    value = (s == MQIA_PLATFORM) ? fake.platform : (s == MQIA_COMMAND_LEVEL) ? fake.commandLevel : 0;
                }
                else if (s == MQIA_CURRENT_Q_DEPTH)
                {
                    value = (q == nullptr) ? 7 : q->hasDepthOverride ? q->depthOverride : (MQLONG)q->messages.size();
                }
                else if (s == MQIA_Q_TYPE)
                {
                    value = (q == nullptr) ? MQQT_LOCAL : q->type;
                }
                else if (s == MQIA_MAX_MSG_LENGTH)
                {
                    value = (q == nullptr) ? 4194304 : q->maxMsgLength;
                }
                intAttrs[ints++] = value;
            }
            report(MQRC_NONE, cc, rc);
        }
    }

    // MQCMIT is not in MqApi yet; Task 14 adds it. The entry point exists so that a
    // test can drive the count, and the API member can be wired to it later.
    inline void fakeCmit(MQHCONN, PMQLONG cc, PMQLONG rc)
    {
        fakeMq().commitCalls++;
        fake_detail::report(MQRC_NONE, cc, rc);
    }

    inline rfh::MqApi fakeApi()
    {
        rfh::MqApi api;
        api.connx = fake_detail::fakeConnx;
        api.disc = fake_detail::fakeDisc;
        api.open = fake_detail::fakeOpen;
        api.close = fake_detail::fakeClose;
        api.put = fake_detail::fakePut;
        api.get = fake_detail::fakeGet;
        api.inq = fake_detail::fakeInq;
        api.serverLibrary = true;
        api.bindingsAvailable = true;
        api.libraryPath = "fake";
        return api;
    }
}
