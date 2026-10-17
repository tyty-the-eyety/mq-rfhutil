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

			// The five MsgFlags check boxes, shared by processMQMD and processMQMDE
			void setFlagBoxes(MQLONG msgFlags, MqmdFields & f)
			{
				f.groupYes = (msgFlags & MQMF_MSG_IN_GROUP) != 0;
				f.groupLast = (msgFlags & MQMF_LAST_MSG_IN_GROUP) != 0;
				f.segmentYes = (msgFlags & MQMF_SEGMENT) != 0;
				f.segmentLast = (msgFlags & MQMF_LAST_SEGMENT) != 0;
				f.segmentAllowed = (msgFlags & MQMF_SEGMENTATION_ALLOWED) != 0;
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
			cleared.floatEncoding = f.floatEncoding;	// clearMQMD leaves only this alone
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

		void toPutMqmd(const MqmdFields & f, const PutContext & context, MQMD2 & mqmd)
		{
			static const MQBYTE24 zeros = { 0 };

			// pad the format on the right with blanks, then copy exactly 8 bytes
			memset(mqmd.Format, ' ', sizeof(mqmd.Format));
			memcpy(mqmd.Format, f.format.data(), std::min(f.format.size(), sizeof(mqmd.Format)));

			// Windows copies the text over the existing contents, neither filling nor clearing
			memcpy(mqmd.ReplyToQMgr, f.replyToQMgr.data(), std::min(f.replyToQMgr.size(), sizeof(mqmd.ReplyToQMgr)));
			memcpy(mqmd.ReplyToQ, f.replyToQ.data(), std::min(f.replyToQ.size(), sizeof(mqmd.ReplyToQ)));

			mqmd.Encoding = getEncodingValue(f.encoding, f.pdEncoding, f.floatEncoding);

			if (!f.ccsid.empty())
			{
				mqmd.CodedCharSetId = atoi(f.ccsid.c_str());
			}

			mqmd.Persistence = persistenceValue(f.persistence);

			if (!f.priority.empty())
			{
				mqmd.Priority = atoi(f.priority.c_str());
			}

			if (!f.expiry.empty())
			{
				mqmd.Expiry = atoi(f.expiry.c_str());
			}

			if (!f.feedback.empty())
			{
				mqmd.Feedback = atoi(f.feedback.c_str());
			}

			if (!f.originalLength.empty())
			{
				mqmd.OriginalLength = atoi(f.originalLength.c_str());
			}

			if (!f.seqNo.empty())
			{
				mqmd.MsgSeqNumber = atoi(f.seqNo.c_str());
			}

			if (memcmp(f.msgId, zeros, sizeof(f.msgId)) != 0)
			{
				memcpy(mqmd.MsgId, f.msgId, sizeof(mqmd.MsgId));
			}

			if (memcmp(f.correlId, zeros, sizeof(f.correlId)) != 0)
			{
				memcpy(mqmd.CorrelId, f.correlId, sizeof(mqmd.CorrelId));
			}

			MQLONG flags = buildFlags(f);
			if (flags != 0)
			{
				mqmd.MsgFlags = flags;

				if (!f.offset.empty())
				{
					mqmd.Offset = atoi(f.offset.c_str());
				}
			}

			if (memcmp(f.groupId, zeros, sizeof(f.groupId)) != 0)
			{
				memcpy(mqmd.GroupId, f.groupId, sizeof(mqmd.GroupId));

				if (0 == flags)
				{
					mqmd.MsgFlags = MQMF_LAST_MSG_IN_GROUP;
				}
			}

			if (!f.msgType.empty())
			{
				int msgType = atoi(f.msgType.c_str());
				if (acceptedMsgType(msgType))
				{
					mqmd.MsgType = msgType;
				}
			}

			MQLONG report = buildReportOptions(f);
			if (report != 0)
			{
				mqmd.Report = report;
			}

			// a segment sets the offset again; with flags checked it is already set
			if ((f.segmentYes || f.segmentLast) && !f.offset.empty())
			{
				mqmd.Offset = atoi(f.offset.c_str());
			}

			if (!(context.setIdentity || context.setAll))
			{
				return;
			}

			if (!f.userId.empty())
			{
				memset(mqmd.UserIdentifier, 0, sizeof(mqmd.UserIdentifier));
				memcpy(mqmd.UserIdentifier, f.userId.data(), std::min(f.userId.size(), sizeof(mqmd.UserIdentifier)));
			}
			else if (!context.osUser.empty())
			{
				// setUserId reads the (empty) page field, not the OS user it was meant to use
				memset(mqmd.UserIdentifier, 0, sizeof(mqmd.UserIdentifier));
			}

			if (!f.accountingToken.empty())
			{
				unsigned char token[48];
				memset(token, 0, sizeof(token));
				unsigned int len = (unsigned int)std::min(f.accountingToken.size() / 2, sizeof(token));
				std::string hex = f.accountingToken;
				HexToAscii((unsigned char *)&hex[0], len, token);
				memcpy(mqmd.AccountingToken, token, sizeof(mqmd.AccountingToken));
			}

			if (!context.setAll)
			{
				return;
			}

			// each of these goes through a zeroed 48 byte buffer, so a short text is zero padded
			if (!f.applIdentity.empty())
			{
				memset(mqmd.ApplIdentityData, 0, sizeof(mqmd.ApplIdentityData));
				memcpy(mqmd.ApplIdentityData, f.applIdentity.data(), std::min(f.applIdentity.size(), sizeof(mqmd.ApplIdentityData)));
			}

			if (!f.applOrigin.empty() && (f.applOrigin.size() <= MQ_APPL_ORIGIN_DATA_LENGTH))
			{
				memset(mqmd.ApplOriginData, 0, sizeof(mqmd.ApplOriginData));
				memcpy(mqmd.ApplOriginData, f.applOrigin.data(), f.applOrigin.size());
			}

			if (!f.putApplName.empty() && (f.putApplName.size() <= MQ_APPL_NAME_LENGTH))
			{
				memset(mqmd.PutApplName, 0, sizeof(mqmd.PutApplName));
				memcpy(mqmd.PutApplName, f.putApplName.data(), f.putApplName.size());
			}

			mqmd.PutApplType = atoi(f.putApplType.c_str());

			// Put Date and Time: a zeroed 48 byte buffer holds the text (47 characters at
			// most); an empty text leaves it zero
			char temp[48];
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

		void applyAfterPut(const MQMD2 & returned, MQLONG msgLen, MqmdFields & f)
		{
			f.seqNo = number(returned.MsgSeqNumber);

			if (f.segmentYes && !f.segmentLast)
			{
				// 32 bit arithmetic as on Windows, without the signed overflow
				f.offset = number((MQLONG)((unsigned int)returned.Offset + (unsigned int)msgLen));
			}
			else
			{
				f.offset = "0";
			}

			memcpy(f.msgId, returned.MsgId, sizeof(f.msgId));

			if (f.groupYes || f.groupLast || f.segmentYes || f.segmentLast)
			{
				memcpy(f.groupId, returned.GroupId, sizeof(f.groupId));
			}
			else
			{
				memset(f.groupId, 0, sizeof(f.groupId));
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

			// every byte at or below a blank, or above 0x7F, ends the text: MSVC's char is signed
			char format[MQ_FORMAT_LENGTH + 1];
			memcpy(format, mqmd.Format, MQ_FORMAT_LENGTH);
			format[MQ_FORMAT_LENGTH] = 0;
			for (int i = 0; i < MQ_FORMAT_LENGTH; i++)
			{
				if ((signed char)format[i] <= ' ')
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

			// two separate C strings, as processMQMD builds them: a NUL in PutDate cuts only the date
			char date[12];
			memcpy(date, mqmd.PutDate, 4);
			date[4] = '/';
			memcpy(date + 5, mqmd.PutDate + 4, 2);
			date[7] = '/';
			memcpy(date + 8, mqmd.PutDate + 6, 2);
			date[10] = 0;

			char time[12];
			memcpy(time, mqmd.PutTime, 2);
			time[2] = ':';
			memcpy(time + 3, mqmd.PutTime + 2, 2);
			time[5] = ':';
			memcpy(time + 6, mqmd.PutTime + 4, 2);
			time[8] = '.';
			memcpy(time + 9, mqmd.PutTime + 6, 2);
			time[11] = 0;

			f.dateTime = std::string(date) + " " + time;

			f.applOrigin = trimmedField(mqmd.ApplOriginData, sizeof(mqmd.ApplOriginData));

			if (MQMD_VERSION_2 == mqmd.Version)
			{
				f.seqNo = number(mqmd.MsgSeqNumber);
				f.offset = number(mqmd.Offset);
				setFlagBoxes(mqmd.MsgFlags, f);
				f.originalLength = number(mqmd.OriginalLength);
			}

			return true;
		}

		void applyMdExtension(const MdExtensionFields & e, MqmdFields & f)
		{
			memcpy(f.groupId, e.groupId, sizeof(f.groupId));
			f.seqNo = number(e.msgSeqNumber);
			f.offset = number(e.offset);
			f.originalLength = number(e.originalLength);
			setFlagBoxes(e.msgFlags, f);
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
