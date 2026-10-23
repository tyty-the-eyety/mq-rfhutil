// rfh_queue_file.cpp - save/load queue messages to/from files.
// Portable port of DataArea::saveMsgs and DataArea::loadMsgs.
#include "rfh_queue_file.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <memory>
#include <string>
#include <vector>

#include <cmqc.h>

#include "rfh_header_chain.h"
#include "rfh_message.h"
#include "rfh_mq_session.h"
#include "rfh_result.h"

namespace rfh
{
    // ---- helpers ------------------------------------------------------------

    std::string createNumberedFileName(const std::string & base, int seq)
    {
        char suffix[16];
        snprintf(suffix, sizeof(suffix), ".%03d", seq);
        return base + suffix;
    }

    static bool hasMqmdStrucId(const unsigned char * data, size_t len)
    {
        if (len < 4) return false;
        return memcmp(data, MQMD_STRUC_ID, 4) == 0;
    }

    static size_t headerBytesToStrip(const unsigned char * data, size_t len, const MQMD2 & mqmd)
    {
        if (len == 0) return 0;
        HeaderChain chain = walkHeaders(data, len, mqmd);
        return chain.bodyOffset;
    }

    static Result failWith(MQLONG rc, const char * fmt, const char * arg = nullptr)
    {
        char buf[512];
        if (arg)
            snprintf(buf, sizeof(buf), fmt, arg);
        else
            snprintf(buf, sizeof(buf), "%s", fmt);
        return Result::failure(buf);
    }

    // ---- saveQueue ----------------------------------------------------------

    Result saveQueue(MqSession & s, const SaveQueueOptions & opts, long & count)
    {
        count = 0;
        if (opts.fileName.empty())
            return Result::failure("File name required");

        const bool multiFile = (opts.filesType == SAVE_FILE_DIFF_FILES);
        const bool browse    = (opts.browseMsgs == SAVE_REMOVE_MSGS_NO);
        const bool limited   = (opts.endCount > 0);

        FILE * outFile = nullptr;
        int    fileSeq  = 0;

        auto openOutputFile = [&](bool append) -> bool
        {
            std::string name;
            if (multiFile)
            {
                fileSeq++;
                name = createNumberedFileName(opts.fileName, fileSeq);
            }
            else
            {
                name = opts.fileName;
            }
            outFile = fopen(name.c_str(), append ? "ab" : "wb");
            return outFile != nullptr;
        };

        if (!multiFile)
        {
            if (!openOutputFile(opts.appendFile))
                return failWith(MQRC_UNKNOWN_OBJECT_NAME, "Unable to open output file: %s",
                                opts.fileName.c_str());
        }

        int    skipRemaining = (opts.startCount > 1) ? (opts.startCount - 1) : 0;
        long   msgIndex      = 0;

        GetOptions get;
        get.mode   = browse ? GetMode::BrowseFirst : GetMode::Destructive;
        get.waitMs = 0;

        for (;;)
        {
            if (opts.cancel && opts.cancel->load())
                break;

            if (limited && (msgIndex >= opts.endCount))
                break;

            Message message;
            Result r = s.get(opts.qName, message, get);

            if (!r.ok())
            {
                if (r.rc == MQRC_NO_MSG_AVAILABLE)
                    break;
                if (outFile) { fclose(outFile); outFile = nullptr; }
                char buf[256];
                snprintf(buf, sizeof(buf), "%ld messages saved from %s before error rc=%ld",
                         count, opts.qName.c_str(), (long)r.rc);
                return { r.cc, r.rc, buf, {} };
            }

            if (browse && get.mode == GetMode::BrowseFirst)
                get.mode = GetMode::BrowseNext;

            if (skipRemaining > 0)
            {
                skipRemaining--;
                continue;
            }

            msgIndex++;
            count++;

            // flatten headers+body into one buffer
            std::vector<unsigned char> flat;
            flat.reserve(message.headers.size() + message.body.size());
            flat.insert(flat.end(), message.headers.begin(), message.headers.end());
            flat.insert(flat.end(), message.body.begin(),   message.body.end());

            const unsigned char * data    = flat.data();
            size_t                dataLen = flat.size();

            size_t dataOffset = 0;
            if (opts.removeHdrs && dataLen > 0)
                dataOffset = headerBytesToStrip(data, dataLen, message.mqmd);

            const unsigned char * writeData = data + dataOffset;
            size_t                writeLen  = (dataLen > dataOffset) ? (dataLen - dataOffset) : 0;

            if (multiFile)
            {
                if (!openOutputFile(false))
                {
                    char buf[512];
                    snprintf(buf, sizeof(buf), "Unable to open output file for message %ld", msgIndex);
                    return Result::failure(buf);
                }
            }
            else
            {
                // check max-file-size rollover
                if (opts.maxFileSize > 0)
                {
                    long curPos = ftell(outFile);
                    int64_t needed = (int64_t)(opts.delimLen > 0 && curPos > 0 ? opts.delimLen : 0)
                                   + (opts.includeMQMD ? (int64_t)sizeof(MQMD2) : 0)
                                   + (int64_t)writeLen;
                    if ((int64_t)curPos + needed > opts.maxFileSize)
                    {
                        fclose(outFile);
                        fileSeq++;
                        std::string name = createNumberedFileName(opts.fileName, fileSeq);
                        outFile = fopen(name.c_str(), "wb");
                        if (!outFile)
                            return Result::failure("Unable to open overflow output file");
                    }
                }

                // delimiter between messages (not before the first one in the file)
                long curPos = ftell(outFile);
                if (curPos > 0 && opts.delimLen > 0)
                    fwrite(opts.delimiter, 1, (size_t)opts.delimLen, outFile);
            }

            if (opts.includeMQMD)
                fwrite(&message.mqmd, 1, sizeof(MQMD2), outFile);

            if (writeLen > 0)
                fwrite(writeData, 1, writeLen, outFile);

            if (multiFile)
            {
                fclose(outFile);
                outFile = nullptr;
            }
        }

        if (outFile) { fclose(outFile); outFile = nullptr; }

        char buf[128];
        snprintf(buf, sizeof(buf), "%ld messages saved from %s", count, opts.qName.c_str());
        return { MQCC_OK, MQRC_NONE, buf, {} };
    }

    // ---- loadQueue ----------------------------------------------------------

    static const unsigned char * findDelim(
        const unsigned char * data, size_t len,
        const unsigned char * delim, int delimLen)
    {
        if (delimLen <= 0) return nullptr;
        if ((size_t)delimLen > len) return nullptr;
        size_t limit = len - (size_t)delimLen;
        for (size_t i = 0; i <= limit; i++)
        {
            if (memcmp(data + i, delim, (size_t)delimLen) == 0)
                return data + i;
        }
        return nullptr;
    }

    static bool readFileContents(const std::string & path, std::vector<unsigned char> & out)
    {
        FILE * f = fopen(path.c_str(), "rb");
        if (!f) return false;

        fseek(f, 0L, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0L, SEEK_SET);

        if (sz <= 0) { fclose(f); out.clear(); return true; }

        const long kMaxRead = 256L * 1024 * 1024;
        if (sz > kMaxRead) sz = kMaxRead;

        out.resize((size_t)sz);
        size_t got = fread(out.data(), 1, (size_t)sz, f);
        fclose(f);
        out.resize(got);
        return true;
    }

    Result loadQueue(MqSession & s, const LoadQueueOptions & opts, long & count)
    {
        count = 0;
        if (opts.fileName.empty())
            return Result::failure("File name required");

        const bool multiFile = (opts.filesType == SAVE_FILE_DIFF_FILES);

        PutOptions put;
        put.newMsgId     = opts.newMsgId;
        put.newCorrelId  = false;
        put.setAllContext = opts.useSetAll;
        put.remoteQm     = opts.remoteQm;

        int  fileSeq     = opts.startFileNumber > 0 ? opts.startFileNumber - 1 : 0;
        bool doneLoading = false;

        while (!doneLoading)
        {
            if (opts.cancel && opts.cancel->load())
                break;

            std::string filePath;
            if (multiFile)
            {
                fileSeq++;
                filePath = createNumberedFileName(opts.fileName, fileSeq);
            }
            else
            {
                filePath  = opts.fileName;
                doneLoading = true;
            }

            std::vector<unsigned char> fileData;
            if (!readFileContents(filePath, fileData))
            {
                if (multiFile && fileSeq > (opts.startFileNumber > 0 ? opts.startFileNumber : 1))
                    break;
                if (count == 0)
                    return failWith(MQRC_UNKNOWN_OBJECT_NAME, "Unable to open input file: %s",
                                    filePath.c_str());
                break;
            }

            const unsigned char * cursor    = fileData.data();
            size_t                remaining = fileData.size();

            bool fileDone = false;
            while (!fileDone && remaining > 0)
            {
                if (opts.cancel && opts.cancel->load())
                { doneLoading = true; break; }

                if (opts.maxCount > 0 && count >= (long)opts.maxCount)
                { doneLoading = true; break; }

                size_t msgLen  = remaining;
                size_t skipLen = 0;

                if (!opts.singleFile && opts.delimLen > 0)
                {
                    const unsigned char * delimAt =
                        findDelim(cursor, remaining, opts.delimiter, opts.delimLen);
                    if (delimAt)
                    {
                        msgLen  = (size_t)(delimAt - cursor);
                        skipLen = (size_t)opts.delimLen;
                    }
                    else
                    {
                        msgLen  = remaining;
                        skipLen = 0;
                        fileDone = true;
                    }
                }
                else
                {
                    fileDone = true;
                }

                const unsigned char * msgData    = cursor;
                size_t               msgDataLen  = msgLen;

                // skip optional properties prefix
                if (!opts.ignoreProps && opts.propDelimLen > 0 && msgDataLen > 0)
                {
                    const unsigned char * propEnd =
                        findDelim(msgData, msgDataLen, opts.propDelim, opts.propDelimLen);
                    if (propEnd)
                    {
                        size_t skip = (size_t)(propEnd - msgData) + (size_t)opts.propDelimLen;
                        if (skip <= msgDataLen)
                        {
                            msgData    += skip;
                            msgDataLen -= skip;
                        }
                    }
                }

                // extract embedded MQMD if present
                MQMD2 mqmd = { MQMD2_DEFAULT };
                if (!opts.removeMQMD && msgDataLen >= sizeof(MQMD2)
                    && hasMqmdStrucId(msgData, msgDataLen))
                {
                    memcpy(&mqmd, msgData, sizeof(MQMD2));
                    msgData    += sizeof(MQMD2);
                    msgDataLen -= sizeof(MQMD2);
                }
                else
                {
                    if (opts.defaultCcsid != 0)    mqmd.CodedCharSetId = opts.defaultCcsid;
                    if (opts.defaultEncoding != 0)  mqmd.Encoding       = opts.defaultEncoding;
                    if (opts.format[0] != '\0' && opts.format[0] != ' ')
                        memcpy(mqmd.Format, opts.format, MQ_FORMAT_LENGTH);
                    if (opts.persistent)
                        mqmd.Persistence = MQPER_PERSISTENT;
                }

                // strip leading MQ headers if requested
                size_t hdrOfs = 0;
                if (opts.removeHdrs && msgDataLen > 0)
                    hdrOfs = headerBytesToStrip(msgData, msgDataLen, mqmd);

                Message msg;
                msg.mqmd = mqmd;
                if (hdrOfs < msgDataLen)
                    msg.body.assign(msgData + hdrOfs, msgData + msgDataLen);

                Result putResult = s.put(opts.qName, msg, put);
                if (!putResult.ok())
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf),
                             "%ld messages loaded to %s before error rc=%ld",
                             count, opts.qName.c_str(), (long)putResult.rc);
                    return { putResult.cc, putResult.rc, buf, {} };
                }
                count++;

                size_t advance = msgLen + skipLen;
                if (advance >= remaining)
                    remaining = 0;
                else
                {
                    cursor    += advance;
                    remaining -= advance;
                }

                if (opts.batchSize > 0 && (count % opts.batchSize) == 0)
                {
                    Result cr = s.commit();
                    if (!cr.ok())
                    {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "Commit failed after %ld messages: rc=%ld",
                                 count, (long)cr.rc);
                        return { cr.cc, cr.rc, buf, {} };
                    }
                    if (opts.waitTime > 0)
                    {
                        struct timespec ts;
                        ts.tv_sec  = opts.waitTime / 1000;
                        ts.tv_nsec = (opts.waitTime % 1000) * 1000000L;
                        nanosleep(&ts, nullptr);
                    }
                }

                if (opts.writeOnce && multiFile)
                    doneLoading = true;
            }
        }

        if (opts.batchSize > 0 && count > 0)
            s.commit();

        char buf[128];
        snprintf(buf, sizeof(buf), "%ld messages loaded to %s", count, opts.qName.c_str());
        return { MQCC_OK, MQRC_NONE, buf, {} };
    }

} // namespace rfh
