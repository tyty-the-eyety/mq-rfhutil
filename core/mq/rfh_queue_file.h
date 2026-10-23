// rfh_queue_file.h - save messages from a queue to file(s) and load them back.
//
// These are the portable cores of DataArea::saveMsgs and DataArea::loadMsgs,
// re-expressed in terms of rfh::MqSession + C file I/O and without any MFC
// or Qt dependency. The structs map directly to the dialog fields of
// CSavemsgs / CLoadQ. The caller connects, opens, and disconnects; these
// functions own nothing except the file handles they open.
//
// File format (single-file mode):
//   [optional props delimiter][optional msg props][optional MQMD][data]
//   [delimiter]
//   [optional msg props delimiter][optional msg props][optional MQMD][data]
//   ...
// Multi-file mode: each message is one sequenced file (base.001, base.002, …).
//
// Header stripping: when removeHdrs is set the leading MQ headers whose
// StrucId is recognised (MQRFH, MQRFH2, MQDLH, MQCIH, MQIIH) are removed
// from the saved data. The MQMD.Format is updated accordingly.
//
// Properties: user message properties are not captured in this first
// implementation; the properties delimiter fields are accepted and stored but
// have no effect on the file written.
#pragma once

#include <atomic>
#include <string>

#include <cmqc.h>

#include "rfh_result.h"

namespace rfh
{
    class MqSession;

    // ---- shared constants -----------------------------------------------
    static constexpr int SAVE_DELIM_CHAR = 0;
    static constexpr int SAVE_DELIM_HEX  = 1;

    static constexpr int SAVE_FILE_SAME_FILE   = 0;    // all messages in one file
    static constexpr int SAVE_FILE_DIFF_FILES  = 1;    // one file per message

    static constexpr int SAVE_REMOVE_MSGS_NO   = 0;    // browse (keep on queue)
    static constexpr int SAVE_REMOVE_MSGS_YES  = 1;    // destructive get (remove from queue)

    // ---- SaveQueueOptions -----------------------------------------------

    struct SaveQueueOptions
    {
        std::string fileName;           // base file name; for multi-file it is the template
        std::string qName;
        std::string qmName;

        int startCount    = 1;          // 1-based: skip the first (startCount-1) messages
        int endCount      = 0;          // stop after this 1-based message (0 = no limit)
        int filesType     = SAVE_FILE_SAME_FILE;
        int browseMsgs    = SAVE_REMOVE_MSGS_NO;    // use browse (true) or destructive get

        bool includeMQMD  = true;
        bool removeHdrs   = false;
        bool appendFile   = false;

        // Message delimiter (written between messages in single-file mode).
        // delimLen is the byte length of the binary delimiter (not the hex-string length).
        unsigned char delimiter[64]  = { '#', '@', '#', '@', '#' };
        int           delimLen       = 5;

        // Properties delimiter (written before the message properties, if any).
        unsigned char propDelim[64]  = { '$', '%', '$', '%', '$' };
        int           propDelimLen   = 5;

        // Maximum output file size in bytes (single-file mode only).
        // 0 means no limit; the default matches the Windows UI default of 2 GiB.
        int64_t maxFileSize = (int64_t)2 * 1024 * 1024 * 1024 - 1;

        const std::atomic<bool> * cancel = nullptr;
    };

    // Saves messages from the open queue on `s` to file(s) as described by opts.
    // The queue handle must already be open; `s` must be connected.
    // On return `count` holds the number of messages written.
    // The Result text follows Windows format: "N messages saved from <queue>".
    Result saveQueue(MqSession & s, const SaveQueueOptions & opts, long & count);

    // ---- LoadQueueOptions -----------------------------------------------

    struct LoadQueueOptions
    {
        std::string fileName;           // base file name; for multi-file it is the template
        std::string qName;
        std::string qmName;
        std::string remoteQm;

        int startFileNumber = 0;        // 0 = start at base name; n = start at base.00n
        int maxCount    = 0;            // maximum messages to put; 0 = no limit
        int filesType   = SAVE_FILE_SAME_FILE;
        int defaultCcsid     = 0;       // default MQMD CCSID if not taken from file
        int defaultEncoding  = 0;       // default MQMD encoding if not taken from file
        int batchSize   = 0;            // commit every N messages; 0 = no syncpoint
        int waitTime    = 0;            // ms to sleep between batches (when batchSize > 0)

        bool removeMQMD   = false;      // ignore any embedded MQMD; use defaults
        bool removeHdrs   = false;      // strip recognised MQ headers before putting
        bool useSetAll    = true;       // MQPMO_SET_ALL_CONTEXT; otherwise MQPMO_NO_CONTEXT
        bool newMsgId     = true;       // generate a new MsgId on each put
        bool persistent   = false;      // override MQMD.Persistence to MQPER_PERSISTENT
        bool writeOnce    = true;       // stop after putting all messages from the first file
        bool singleFile   = false;      // treat the file as a single message (no delimiter)
        bool ignoreProps  = false;      // ignore message properties found in the file

        char format[MQ_FORMAT_LENGTH + 1] = {};   // default MQMD.Format (space-padded); empty = keep from file

        unsigned char delimiter[64]  = { '#', '@', '#', '@', '#' };
        int           delimLen       = 5;

        unsigned char propDelim[64]  = { '$', '%', '$', '%', '$' };
        int           propDelimLen   = 5;

        const std::atomic<bool> * cancel = nullptr;
    };

    // Loads messages from file(s) and puts them to the open queue on `s`.
    // The queue handle must already be open for put; `s` must be connected.
    // On return `count` holds the number of messages put.
    // The Result text follows Windows format: "N messages loaded to <queue>".
    Result loadQueue(MqSession & s, const LoadQueueOptions & opts, long & count);

    // Builds the numbered file name that DataArea::createNextFileName produces:
    //   base + "." + zero-padded three-digit sequence number.
    // e.g. createNumberedFileName("/tmp/msgs", 1) → "/tmp/msgs.001"
    std::string createNumberedFileName(const std::string & base, int seq);
}
