// fake_mq.h - an in-memory MQI shared by the core and Qt tests.
//
// fakeApi() returns an MqApi whose entry points act on one FakeMq (fakeMq()).
// Tests call fakeMq().reset() first, then configure what the fake should answer
// and read back what it saw. Header-only.
//
// UNIT OF WORK (added in the cleanup of Tasks 13/14/19). A put with MQPMO_SYNCPOINT (or neither syncpoint
// option, which is a syncpoint put in real MQ) waits in pendingPuts, invisible to gets and browses; a
// destructive get under syncpoint (MQGMO_SYNCPOINT, or MQGMO_SYNCPOINT_IF_PERSISTENT of a persistent message)
// takes its message off the queue into pendingGets. Then, as in MQ:
//   MQCMIT   applies the puts and forgets the gets. A failing MQCMIT leaves the unit pending, except 2003
//            (backed out) and 2009 (see lostUnitCommits); a warning (2123, 2124) has committed.
//   MQBACK   discards the puts and returns the gets to the front of their queues, in their old order.
//   MQDISC   COMMITS what is pending, like real MQ. After a call has reported 2009 (connectionBroken) the
//            unit is resolved as lostUnitCommits says instead.
//   2009     a connection that breaks leaves the unit's fate unknown to the caller. The fake settles it as
//            lostUnitCommits (default false: backed out) so a test can check that a message is NOT claimed
//            to be in the one state or the other whichever way the queue manager went.
// The depth a queue reports includes pending puts and pending gets, as MQ's CURDEPTH does.
//
// WHAT THE FAKE DOES NOT MODEL (a passing fake test says nothing about these; the live tests in
// MqSessionTests.cpp do):
//   - message selectors are recorded in openLog but never applied;
//   - option rules: structure versions and option combinations are not validated (for example MQGMO_LOGICAL_ORDER
//     with MQMO_MATCH_MSG_ID, or MQGMO_BROWSE_MSG_UNDER_CURSOR with other options, are accepted);
//   - report() makes every non-zero reason MQCC_FAILED; a warning needs warnGetRc, cmitCc or connxCc;
//   - a handle used after its close is not detected (the fake answers as for an open one);
//   - the unit of work is one per fake, not per connection, and has no locking against other connections;
//   - the browse cursor exists only with browseCursors, and then ignores logical order and groups.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <functional>
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
        bool dynamic = false;        // created by an open of the model queue; MQCO_DELETE_PURGE removes it
    };

    // What the fake saw, one record per call (Task 12). Since Task 13, connect() opens the queue manager
    // object to inquire on it. Those calls are counted and logged apart (qmgrOpenCalls, qmgrOpenLog,
    // qmgrInqCalls, qmgrCloseCalls) so the counters and logs below keep describing queue calls only;
    // the handle stays in liveHandles, so a leak is still seen. Tests read them to check the open
    // options, the object descriptor fields, the request bytes and the close options.
    struct OpenRecord
    {
        std::string name;            // MQOD.ObjectName as sent, trimmed
        std::string dynamicName;     // MQOD.DynamicQName as sent, trimmed
        std::string openedAs;        // the queue the handle refers to (the generated name for a model open)
        std::string alternateUser;   // MQOD.AlternateUserId as sent, trimmed
        MQLONG objectType = 0;
        MQLONG options = 0;
        MQHOBJ handle = 0;
        MQLONG odVersion = 0;        // MQOD.Version as sent
        std::string selector;        // MQOD.SelectionString as sent (version 4 and above); empty when none
        MQLONG resObjectStringBufSize = 0;   // MQOD.ResObjectString.VSBufSize as sent (version 4 and above)
    };

    // One MQGET as the fake saw it (Task 13): the options and the identifiers the caller sent.
    struct GetRecord
    {
        MQLONG gmoVersion = 0;
        MQLONG options = 0;
        MQLONG matchOptions = 0;
        MQBYTE24 msgId = {0};        // MQMD.MsgId as sent
        MQBYTE24 correlId = {0};     // MQMD.CorrelId as sent
        MQBYTE24 groupId = {0};      // MQMD.GroupId as sent (a version 2 descriptor)
        MQLONG mdCcsid = 0;          // MQMD.CodedCharSetId as sent (the target of MQGMO_CONVERT)
        MQLONG mdEncoding = 0;       // MQMD.Encoding as sent
    };

    // One MQCONNX as the fake saw it (Task 16). The structures are copied byte for byte at the time of the call,
    // with the strings their pointers address, because the caller's buffers do not outlive the call (and a
    // session may wipe them). passwordPtr is the address MQCSP.CSPPasswordPtr held: a test may read it after
    // the call only when it knows the session still owns that memory. Nothing here is a real credential.
    struct ConnxRecord
    {
        std::string qmName;          // the queue manager name argument, trimmed
        MQCNO cno = {MQCNO_DEFAULT};
        bool hasClientConn = false;  // MQCNO.ClientConnPtr was set; cd is then a copy of the MQCD
        MQCD cd = {MQCD_CLIENT_CONN_DEFAULT};
        bool hasSslConfig = false;   // MQCNO.SSLConfigPtr was set; sco is then a copy of the MQSCO
        MQSCO sco = {MQSCO_DEFAULT};
        bool hasSecurityParms = false;   // MQCNO.SecurityParmsPtr was set; csp is then a copy of the MQCSP
        MQCSP csp = {MQCSP_DEFAULT};
        std::string userId;          // CSPUserIdLength bytes at CSPUserIdPtr
        std::vector<char> password;  // CSPPasswordLength bytes at CSPPasswordPtr
        const void * passwordPtr = nullptr;
        std::string sslPeerName;     // SSLPeerNameLength bytes at MQCD.SSLPeerNamePtr
        std::string ccdtUrl;         // CCDTUrlLength bytes at MQCNO.CCDTUrlPtr
    };

    struct CloseRecord
    {
        std::string queue;           // the queue the handle referred to
        MQLONG options = 0;
    };

    struct PutRecord
    {
        std::string queue;
        FakeMessage message;
        MQLONG pmoOptions = 0;
        MQLONG pmoVersion = 0;       // MQPMO.Version as sent (Task 14)
    };

    struct FakeMq
    {
        // What the fake should answer.
        MQLONG openRc = MQRC_NONE;
        MQLONG closeRc = MQRC_NONE;
        MQLONG discRc = MQRC_NONE;
        MQLONG putRc = MQRC_NONE;
        MQLONG connxRc = MQRC_NONE;   // MQCONNX answers with this reason, completion code connxCc (Task 16)
        MQLONG connxCc = MQCC_FAILED;
        MQLONG cmitRc = MQRC_NONE;    // MQCMIT answers with this reason (Task 14)
        MQLONG cmitCc = MQCC_FAILED;  // ... and this completion code when cmitRc is not MQRC_NONE (a test may ask for MQCC_WARNING)
        MQLONG backRc = MQRC_NONE;    // MQBACK answers with this reason, completion code MQCC_FAILED
        bool lostUnitCommits = false; // a unit of work whose connection broke: false backs it out, true commits it
        int failPutsAfter = -1;       // from the put after this many puts (counting all of them), every put fails with failPutsRc; -1 never
        MQLONG failPutsRc = MQRC_NONE;
        MQLONG platform = MQPL_UNIX;
        MQLONG commandLevel = 940;
        MQLONG qmgrMaxMsgLength = 4194304;   // answered for MQIA_MAX_MSG_LENGTH on the queue manager object (Task 13)
        std::map<MQLONG, MQLONG> inqRcBySelector;   // an inquiry that names one of these selectors fails with the given reason
        int unlimitedWaitMs = 1000;   // how long a get with MQWI_UNLIMITED waits in the fake
        MQLONG getRc = MQRC_NONE;     // every get fails with this reason, before it looks at the queue
        bool browseCursors = false;   // model one browse cursor per queue handle (off: a browse always returns the first message)
        std::map<std::string, MQLONG> openRcByName;   // an open of the object with this name fails with the given reason (before openRc is looked at)
        int failGetsAfter = -1;       // from the get after this many gets (counting all of them), every get fails with failGetsRc; -1 never
        MQLONG failGetsRc = MQRC_NONE;
        // Task 19 (purge), all off by default:
        std::function<void(int)> onGet;   // called at the start of every get with its number (1-based, getCalls); a test sets a cancel flag here
        std::map<int, MQLONG> warnGetRc;  // the get with this number delivers its message and reports MQCC_WARNING with this reason
        MQLONG maxUncommitted = 0;        // above 0: a destructive get under syncpoint (MQGMO_SYNCPOINT, or MQGMO_SYNCPOINT_IF_PERSISTENT with a persistent message) fails with 2024 when this many are uncommitted
        int uncommittedGets = 0;          // destructive gets under syncpoint since the last commit that worked
        std::vector<int> commitAtGet;     // getCalls at each MQCMIT, in order
        std::vector<std::string> callOrder;   // "cmit", "back" and "disc", in the order the session called them
        struct PendingChange
        {
            std::string queue;
            FakeMessage message;
        };
        std::vector<PendingChange> pendingPuts;   // put under syncpoint, not yet committed
        std::vector<PendingChange> pendingGets;   // got under syncpoint, not yet committed
        bool connectionBroken = false;            // a call has reported 2009 since the last MQCONNX
        std::string dynamicQueueName; // the name an open of SYSTEM.DEFAULT.MODEL.QUEUE gives its dynamic queue; empty generates <prefix>FAKE<n>
        int dynamicQueueCount = 0;
        // Called after a put to the named queue, with the message just put. A test uses it to play the
        // command server: it reads the request and enqueues replies on the queue the request names.
        std::map<std::string, std::function<void(const FakeMessage &)>> onPut;

        // What it saw.
        int connxCalls = 0;
        int openCalls = 0;
        int closeCalls = 0;
        int discCalls = 0;
        int putCalls = 0;
        int getCalls = 0;
        int commitCalls = 0;
        int backCalls = 0;
        int inqCalls = 0;
        int qmgrOpenCalls = 0;       // opens of the queue manager object (connect()'s inquiry)
        int qmgrInqCalls = 0;
        int qmgrCloseCalls = 0;
        MQLONG lastPmoOptions = 0;
        MQLONG lastPmoVersion = 0;
        MQLONG lastGmoOptions = 0;
        MQLONG lastGmoWaitInterval = 0;
        MQLONG lastGmoMatchOptions = 0;
        MQLONG lastOpenOptions = 0;
        int lastWaitedMs = 0;
        std::string lastOpenedName;
        std::vector<OpenRecord> openLog;       // opens of queues
        std::vector<OpenRecord> qmgrOpenLog;   // opens of the queue manager object
        std::vector<GetRecord> getLog;
        std::vector<ConnxRecord> connxLog;     // one per MQCONNX (Task 16)
        std::vector<CloseRecord> closeLog;
        std::vector<PutRecord> putLog;
        std::vector<std::string> deletedQueues;      // dynamic queues removed by a close with MQCO_DELETE_PURGE

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
            if (rc == MQRC_CONNECTION_BROKEN)
            {
                fakeMq().connectionBroken = true;
            }
        }

        inline FakeQueue * queueNamed(const std::string & name)
        {
            return fakeMq().find(name);
        }

        // Makes the pending puts visible, forgets the pending gets (their messages are gone for good).
        inline void commitUnit()
        {
            FakeMq & fake = fakeMq();
            std::vector<FakeMq::PendingChange> puts;
            puts.swap(fake.pendingPuts);
            fake.pendingGets.clear();
            fake.uncommittedGets = 0;
            for (size_t i = 0; i < puts.size(); i++)
            {
                FakeQueue * q = queueNamed(puts[i].queue);
                if (q != nullptr)
                {
                    q->messages.push_back(puts[i].message);
                }
                std::map<std::string, std::function<void(const FakeMessage &)>>::iterator hook = fake.onPut.find(puts[i].queue);
                if (hook != fake.onPut.end())
                {
                    std::function<void(const FakeMessage &)> run = hook->second;
                    run(puts[i].message);
                }
            }
        }

        // Discards the pending puts and returns the pending gets to the front of their queues, in their old order.
        inline void backUnit()
        {
            FakeMq & fake = fakeMq();
            fake.pendingPuts.clear();
            for (size_t i = fake.pendingGets.size(); i > 0; i--)
            {
                fake.queue(fake.pendingGets[i - 1].queue).messages.push_front(fake.pendingGets[i - 1].message);
            }
            fake.pendingGets.clear();
            fake.uncommittedGets = 0;
        }

        // The unit of work of a connection that broke: the fake settles it as lostUnitCommits says.
        inline void settleLostUnit()
        {
            if (fakeMq().lostUnitCommits)
            {
                commitUnit();
            }
            else
            {
                backUnit();
            }
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

        inline void fakeConnx(PMQCHAR qmName, PMQCNO cno, PMQHCONN hConn, PMQLONG cc, PMQLONG rc)
        {
            FakeMq & fake = fakeMq();
            fake.connxCalls++;

            fake.connectionBroken = false;

            ConnxRecord record;
            if (qmName != nullptr)
            {
                record.qmName = trimmed(qmName, 48);
            }
            if (cno != nullptr)
            {
                record.cno = *cno;
                if (cno->ClientConnPtr != nullptr)
                {
                    record.hasClientConn = true;
                    memcpy(&record.cd, cno->ClientConnPtr, sizeof(MQCD));
                    const MQCD * cd = static_cast<const MQCD *>(cno->ClientConnPtr);
                    if ((cd->SSLPeerNamePtr != nullptr) && (cd->SSLPeerNameLength > 0))
                    {
                        record.sslPeerName.assign(static_cast<const char *>(cd->SSLPeerNamePtr), (size_t)cd->SSLPeerNameLength);
                    }
                }
                if (cno->SSLConfigPtr != nullptr)
                {
                    record.hasSslConfig = true;
                    memcpy(&record.sco, cno->SSLConfigPtr, sizeof(MQSCO));
                }
                if (cno->SecurityParmsPtr != nullptr)
                {
                    record.hasSecurityParms = true;
                    memcpy(&record.csp, cno->SecurityParmsPtr, sizeof(MQCSP));
                    if ((record.csp.CSPUserIdPtr != nullptr) && (record.csp.CSPUserIdLength > 0))
                    {
                        record.userId.assign(static_cast<const char *>(record.csp.CSPUserIdPtr), (size_t)record.csp.CSPUserIdLength);
                    }
                    if ((record.csp.CSPPasswordPtr != nullptr) && (record.csp.CSPPasswordLength > 0))
                    {
                        const char * password = static_cast<const char *>(record.csp.CSPPasswordPtr);
                        record.password.assign(password, password + record.csp.CSPPasswordLength);
                        record.passwordPtr = record.csp.CSPPasswordPtr;
                    }
                }
                if ((cno->CCDTUrlPtr != nullptr) && (cno->CCDTUrlLength > 0))
                {
                    record.ccdtUrl.assign(static_cast<const char *>(cno->CCDTUrlPtr), (size_t)cno->CCDTUrlLength);
                }
            }
            fake.connxLog.push_back(record);

            *hConn = 1;
            report(fake.connxRc, cc, rc);
            if (fake.connxRc != MQRC_NONE)
            {
                *cc = fake.connxCc;
            }
        }

        inline void fakeDisc(PMQHCONN, PMQLONG cc, PMQLONG rc)
        {
            FakeMq & fake = fakeMq();
            fake.discCalls++;
            fake.callOrder.push_back("disc");
            fake.liveHandles.clear();   // MQDISC releases every handle of the connection
            fake.cursors.clear();
            if (fake.discRc == MQRC_NONE)
            {
                if (fake.connectionBroken)
                {
                    settleLostUnit();
                }
                else
                {
                    commitUnit();   // MQDISC commits what the connection left pending
                }
            }
            report(fake.discRc, cc, rc);
        }

        inline void fakeOpen(MQHCONN, PMQVOID objDesc, MQLONG options, PMQHOBJ hObj, PMQLONG cc, PMQLONG rc)
        {
            FakeMq & fake = fakeMq();
            const bool qmgrObject = (objDesc != nullptr) && (static_cast<const MQOD *>(objDesc)->ObjectType == MQOT_Q_MGR);
            (qmgrObject ? fake.qmgrOpenCalls : fake.openCalls)++;
            MQLONG openRc = fake.openRc;
            if (objDesc != nullptr)
            {
                const MQOD * named = static_cast<const MQOD *>(objDesc);
                std::map<std::string, MQLONG>::const_iterator byName = fake.openRcByName.find(trimmed(named->ObjectName, sizeof(named->ObjectName)));
                if (byName != fake.openRcByName.end())
                {
                    openRc = byName->second;
                }
            }
            if (!qmgrObject)
            {
                fake.lastOpenOptions = options;
            }
            *hObj = fake.openCalls + fake.qmgrOpenCalls;

            MQOD * od = static_cast<MQOD *>(objDesc);
            OpenRecord record;
            record.options = options;
            record.handle = *hObj;
            if (od != nullptr)
            {
                const std::string opened = trimmed(od->ObjectName, sizeof(od->ObjectName));
                if (!qmgrObject)
                {
                    fake.lastOpenedName = opened;
                }
                record.name = opened;
                record.objectType = od->ObjectType;
                record.dynamicName = trimmed(od->DynamicQName, sizeof(od->DynamicQName));
                record.alternateUser = trimmed(od->AlternateUserId, sizeof(od->AlternateUserId));
                record.openedAs = opened;
                record.odVersion = od->Version;
                if (od->Version >= MQOD_VERSION_4)
                {
                    if ((od->SelectionString.VSPtr != nullptr) && (od->SelectionString.VSLength > 0))
                    {
                        record.selector.assign(static_cast<const char *>(od->SelectionString.VSPtr), (size_t)od->SelectionString.VSLength);
                    }
                    record.resObjectStringBufSize = od->ResObjectString.VSBufSize;
                }
                if ((openRc == MQRC_NONE) && (od->ObjectType == MQOT_Q) &&
                    (fake.lastOpenedName == "SYSTEM.DEFAULT.MODEL.QUEUE") && !record.dynamicName.empty())
                {
                    // a model queue open creates a dynamic queue and reports its name in the descriptor
                    fake.dynamicQueueCount++;
                    std::string prefix = record.dynamicName.substr(0, record.dynamicName.find('*'));
                    std::string generated = fake.dynamicQueueName.empty() ? prefix + "FAKE" + std::to_string(fake.dynamicQueueCount) : fake.dynamicQueueName;
                    fake.queues[generated].dynamic = true;
                    memset(od->ObjectName, ' ', sizeof(od->ObjectName));
                    memcpy(od->ObjectName, generated.data(), std::min(sizeof(od->ObjectName), generated.size()));
                    fake.lastOpenedName = generated;
                    record.openedAs = generated;
                }
                fake.handleNames[*hObj] = qmgrObject ? std::string() : fake.lastOpenedName;
                fake.handleIsQmgr[*hObj] = (od->ObjectType == MQOT_Q_MGR);
            }
            (qmgrObject ? fake.qmgrOpenLog : fake.openLog).push_back(record);
            if ((od != nullptr) && (od->Version >= MQOD_VERSION_3) && (openRc == MQRC_NONE))
            {
                // an open with a version 3 descriptor reports the name the object resolved to
                MQOD * out = od;
                FakeQueue * resolved = fake.find(fake.lastOpenedName);
                std::string name = ((resolved != nullptr) && !resolved->resolvedQName.empty()) ? resolved->resolvedQName : fake.lastOpenedName;
                memset(out->ResolvedQName, ' ', sizeof(out->ResolvedQName));
                memcpy(out->ResolvedQName, name.data(), std::min(sizeof(out->ResolvedQName), name.size()));
            }
            if (openRc == MQRC_NONE)
            {
                fake.liveHandles.insert(*hObj);
            }
            report(openRc, cc, rc);
        }

        inline void fakeClose(MQHCONN, PMQHOBJ hObj, MQLONG options, PMQLONG cc, PMQLONG rc)
        {
            FakeMq & fake = fakeMq();
            const bool qmgrObject = fake.handleIsQmgr.count(*hObj) && fake.handleIsQmgr[*hObj];
            (qmgrObject ? fake.qmgrCloseCalls : fake.closeCalls)++;
            CloseRecord record;
            record.options = options;
            std::map<MQHOBJ, std::string>::iterator named = fake.handleNames.find(*hObj);
            if (named != fake.handleNames.end())
            {
                record.queue = named->second;
            }
            if (!qmgrObject)
            {
                fake.closeLog.push_back(record);
            }
            if (fake.closeRc == MQRC_NONE)
            {
                FakeQueue * closing = fake.find(record.queue);
                if ((closing != nullptr) && closing->dynamic && ((options & MQCO_DELETE_PURGE) == MQCO_DELETE_PURGE))
                {
                    fake.deletedQueues.push_back(record.queue);
                    fake.queues.erase(record.queue);
                }
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
                fake.lastPmoVersion = static_cast<const MQPMO *>(pmo)->Version;
            }

            FakeQueue * q = queueOf(hObj);
            MQLONG putRc = fake.putRc;
            if ((putRc == MQRC_NONE) && (fake.failPutsAfter >= 0) && (fake.putCalls > fake.failPutsAfter))
            {
                putRc = fake.failPutsRc;
            }
            if (putRc == MQRC_NONE)
            {
                // MQPMO_NEW_MSG_ID: MQ returns the new id in the caller's descriptor ("FAKEID" and the put number)
                if ((md != nullptr) && (pmo != nullptr) && ((static_cast<const MQPMO *>(pmo)->Options & MQPMO_NEW_MSG_ID) != 0))
                {
                    MQMD * back = static_cast<MQMD *>(md);
                    memset(back->MsgId, 0, sizeof(back->MsgId));
                    memcpy(back->MsgId, "FAKEID", 6);
                    back->MsgId[6] = static_cast<MQBYTE>(fake.putCalls);
                }
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

                PutRecord record;
                std::map<MQHOBJ, std::string>::iterator named = fake.handleNames.find(hObj);
                record.queue = (named == fake.handleNames.end()) ? std::string() : named->second;
                record.message = m;
                record.pmoOptions = (pmo != nullptr) ? static_cast<const MQPMO *>(pmo)->Options : 0;
                record.pmoVersion = (pmo != nullptr) ? static_cast<const MQPMO *>(pmo)->Version : 0;
                fake.putLog.push_back(record);

                // neither syncpoint option means under syncpoint, as in MQ
                const bool syncpoint = (pmo == nullptr) || ((record.pmoOptions & MQPMO_SYNCPOINT) != 0) ||
                                       ((record.pmoOptions & MQPMO_NO_SYNCPOINT) == 0);
                if (syncpoint)
                {
                    FakeMq::PendingChange change;
                    change.queue = record.queue;
                    change.message = m;
                    fake.pendingPuts.push_back(change);
                }
                else
                {
                    if (q != nullptr)
                    {
                        q->messages.push_back(m);
                    }
                    std::map<std::string, std::function<void(const FakeMessage &)>>::iterator hook = fake.onPut.find(record.queue);
                    if (hook != fake.onPut.end())
                    {
                        std::function<void(const FakeMessage &)> run = hook->second;
                        run(m);
                    }
                }
            }
            report(putRc, cc, rc);
        }

        inline void fakeGet(MQHCONN, MQHOBJ hObj, PMQVOID md, PMQVOID gmoIn, MQLONG bufferLength, PMQVOID buffer, PMQLONG length, PMQLONG cc, PMQLONG rc)
        {
            FakeMq & fake = fakeMq();
            fake.getCalls++;
            *length = 0;
            if (fake.onGet)
            {
                fake.onGet(fake.getCalls);
            }

            MQGMO * gmo = static_cast<MQGMO *>(gmoIn);
            if (gmo != nullptr)
            {
                fake.lastGmoOptions = gmo->Options;
                fake.lastGmoWaitInterval = gmo->WaitInterval;
                fake.lastGmoMatchOptions = gmo->MatchOptions;

                GetRecord record;
                record.gmoVersion = gmo->Version;
                record.options = gmo->Options;
                record.matchOptions = gmo->MatchOptions;
                if (md != nullptr)
                {
                    const MQMD2 * sent = static_cast<const MQMD2 *>(md);   // MQMD is the version 1 layout: GroupId needs MQMD2
                    memcpy(record.msgId, sent->MsgId, sizeof(record.msgId));
                    memcpy(record.correlId, sent->CorrelId, sizeof(record.correlId));
                    record.mdCcsid = sent->CodedCharSetId;
                    record.mdEncoding = sent->Encoding;
                    if (sent->Version >= MQMD_VERSION_2)
                    {
                        memcpy(record.groupId, sent->GroupId, sizeof(record.groupId));
                    }
                }
                fake.getLog.push_back(record);
            }

            if (fake.getRc != MQRC_NONE)
            {
                report(fake.getRc, cc, rc);
                if (fake.getRc == MQRC_BACKED_OUT)
                {
                    backUnit();		// MQ has backed the unit of work out when it says so
                }
                return;
            }
            if ((fake.failGetsAfter >= 0) && (fake.getCalls > fake.failGetsAfter))
            {
                report(fake.failGetsRc, cc, rc);
                if (fake.failGetsRc == MQRC_BACKED_OUT)
                {
                    backUnit();
                }
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
            const bool browseUnderCursor = (gmo != nullptr) && ((gmo->Options & MQGMO_BROWSE_MSG_UNDER_CURSOR) != 0);
            size_t index = 0;
            if (fake.browseCursors && (browseFirst || browseNext || browseUnderCursor))
            {
                int & cursor = fake.cursors.emplace(hObj, -1).first->second;
                if (browseFirst)
                {
                    cursor = -1;   // a browse first starts over, even when it then finds nothing
                }
                // under the cursor is the message the last browse returned; before the first browse there is none
                index = browseUnderCursor ? (size_t)std::max(cursor, 0) : (size_t)(cursor + 1);
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

            const bool browsing = browseFirst || browseNext || browseUnderCursor;
            const bool underSyncpoint = (gmo != nullptr) && !browsing &&
                (((gmo->Options & MQGMO_SYNCPOINT) != 0) ||
                 (((gmo->Options & MQGMO_SYNCPOINT_IF_PERSISTENT) != 0) && (q->messages[index].md.Persistence == MQPER_PERSISTENT)));
            if (underSyncpoint && (fake.maxUncommitted > 0) && (fake.uncommittedGets >= fake.maxUncommitted))
            {
                report(MQRC_SYNCPOINT_LIMIT_REACHED, cc, rc);
                return;
            }
            if (underSyncpoint)
            {
                fake.uncommittedGets++;
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
            const bool browse = browsing;
            if (browse)
            {
                if (fake.browseCursors)
                {
                    fake.cursors[hObj] = (int)index;
                }
            }
            else
            {
                if (underSyncpoint)
                {
                    FakeMq::PendingChange change;
                    change.queue = fake.handleNames[hObj];
                    change.message = q->messages.front();
                    fake.pendingGets.push_back(change);
                }
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
            std::map<int, MQLONG>::const_iterator warning = fake.warnGetRc.find(fake.getCalls);
            if (warning != fake.warnGetRc.end())
            {
                *rc = warning->second;
                *cc = MQCC_WARNING;
                return;
            }
            report(MQRC_NONE, cc, rc);
        }

        // Uncommitted puts and gets of a queue; MQ's CURDEPTH counts both.
        inline size_t pendingFor(const std::string & queue)
        {
            FakeMq & fake = fakeMq();
            size_t n = 0;
            for (size_t i = 0; i < fake.pendingPuts.size(); i++)
            {
                n += (fake.pendingPuts[i].queue == queue) ? 1 : 0;
            }
            for (size_t i = 0; i < fake.pendingGets.size(); i++)
            {
                n += (fake.pendingGets[i].queue == queue) ? 1 : 0;
            }
            return n;
        }

        // Integer selectors come first in the selector list, character selectors after.
        inline void fakeInq(MQHCONN, MQHOBJ hObj, MQLONG selectorCount, PMQLONG selectors, MQLONG intCount, PMQLONG intAttrs, MQLONG charLength, PMQCHAR charAttrs, PMQLONG cc, PMQLONG rc)
        {
            FakeMq & fake = fakeMq();
            const bool qmgrObject = fake.handleIsQmgr.count(hObj) && fake.handleIsQmgr[hObj];
            (qmgrObject ? fake.qmgrInqCalls : fake.inqCalls)++;
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
                    value = (s == MQIA_PLATFORM) ? fake.platform : (s == MQIA_COMMAND_LEVEL) ? fake.commandLevel :
                            (s == MQIA_MAX_MSG_LENGTH) ? fake.qmgrMaxMsgLength : 0;
                }
                else if (s == MQIA_CURRENT_Q_DEPTH)
                {
                    value = (q == nullptr) ? 7 : q->hasDepthOverride ? q->depthOverride : (MQLONG)(q->messages.size() + pendingFor(fake.handleNames[hObj]));
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

    // MQCMIT (Task 14): counted in commitCalls; answers with cmitRc (cmitCc when that is not MQRC_NONE).
    // What happens to the unit of work is described at the top of this file.
    inline void fakeCmit(MQHCONN, PMQLONG cc, PMQLONG rc)
    {
        FakeMq & fake = fakeMq();
        fake.commitCalls++;
        fake.commitAtGet.push_back(fake.getCalls);
        fake.callOrder.push_back("cmit");
        fake_detail::report(fake.cmitRc, cc, rc);
        if (fake.cmitRc != MQRC_NONE)
        {
            *cc = fake.cmitCc;
        }
        if (*cc != MQCC_FAILED)
        {
            fake_detail::commitUnit();		// success, or a warning such as 2123 and 2124: the queue manager has committed
        }
        else if (fake.cmitRc == MQRC_BACKED_OUT)
        {
            fake_detail::backUnit();
        }
        else if (fake.cmitRc == MQRC_CONNECTION_BROKEN)
        {
            fake_detail::settleLostUnit();
        }
        // any other failure leaves the unit of work pending
    }

    // MQBACK: counted in backCalls; answers with backRc (MQCC_FAILED when that is not MQRC_NONE).
    inline void fakeBack(MQHCONN, PMQLONG cc, PMQLONG rc)
    {
        FakeMq & fake = fakeMq();
        fake.backCalls++;
        fake.callOrder.push_back("back");
        fake_detail::report(fake.backRc, cc, rc);
        if (fake.backRc == MQRC_NONE)
        {
            fake_detail::backUnit();
        }
        else if (fake.backRc == MQRC_CONNECTION_BROKEN)
        {
            fake_detail::settleLostUnit();
        }
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
        api.cmit = fakeCmit;
        api.back = fakeBack;
        api.serverLibrary = true;
        api.bindingsAvailable = true;
        api.libraryPath = "fake";
        return api;
    }
}
