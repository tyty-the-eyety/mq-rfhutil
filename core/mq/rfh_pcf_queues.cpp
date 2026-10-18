// rfh_pcf_queues.cpp - see rfh_pcf_queues.h

#include "rfh_pcf_queues.h"

#include <cmqc.h>
#include <cmqcfc.h>

#include <string.h>

#include <string>
#include <vector>

#include "rfhconst.h"
#include "comsubs.h"
#include "mqsubs.h"

namespace rfh
{
	namespace pcf
	{
		namespace
		{
			void append(std::vector<unsigned char> & out, MQLONG value)
			{
				unsigned char bytes[sizeof(MQLONG)];
				memcpy(bytes, &value, sizeof(bytes));
				out.insert(out.end(), bytes, bytes + sizeof(bytes));
			}

			// The integers of a reply are read in the order the message encoding names.
			struct Reader
			{
				const unsigned char * data;
				size_t length;
				bool swap;

				MQLONG at(size_t offset) const
				{
					unsigned char b[sizeof(MQLONG)];
					memcpy(b, data + offset, sizeof(b));
					if (swap)
					{
						std::swap(b[0], b[3]);
						std::swap(b[1], b[2]);
					}
					MQLONG value;
					memcpy(&value, b, sizeof(value));
					return value;
				}
			};

			const size_t kIntegerFixed = MQCFIN_STRUC_LENGTH;		// 16: type, length, parameter, value
			const size_t kStringFixed = MQCFST_STRUC_LENGTH_FIXED;	// 20: type, length, parameter, ccsid, string length
			const size_t kMinimumParameter = 8;						// type and length

			void addWarning(QueueReply & reply, const std::string & text)
			{
				reply.warnings.push_back(text);
			}
		}

		std::vector<unsigned char> buildInquireQueueRequest(MQLONG queueType, bool clusterInfo, bool extended)
		{
			std::vector<unsigned char> out;

			// MQCFH
			append(out, extended ? MQCFT_COMMAND_XR : MQCFT_COMMAND);
			append(out, MQCFH_STRUC_LENGTH);
			append(out, extended ? MQCFH_VERSION_3 : MQCFH_VERSION_1);
			append(out, MQCMD_INQUIRE_Q);
			append(out, 1);							// MsgSeqNumber
			append(out, MQCFC_LAST);
			append(out, 0);							// CompCode
			append(out, 0);							// Reason
			append(out, clusterInfo ? 4 : 3);		// ParameterCount

			// MQCFST: Q_NAME "*"
			append(out, MQCFT_STRING);
			append(out, (MQLONG)(kStringFixed + 4));
			append(out, MQCA_Q_NAME);
			append(out, MQCCSI_DEFAULT);
			append(out, 1);							// StringLength
			out.push_back('*');						// the string, padded with zeros to a multiple of 4
			out.insert(out.end(), 3, 0);

			// MQCFIN: Q_TYPE
			append(out, MQCFT_INTEGER);
			append(out, MQCFIN_STRUC_LENGTH);
			append(out, MQIA_Q_TYPE);
			append(out, queueType);

			if (clusterInfo)
			{
				append(out, MQCFT_INTEGER);
				append(out, MQCFIN_STRUC_LENGTH);
				append(out, MQIACF_CLUSTER_INFO);
				append(out, 0);
			}

			// MQCFIL: only the queue name and type are wanted back
			append(out, MQCFT_INTEGER_LIST);
			append(out, MQCFIL_STRUC_LENGTH_FIXED + 8);
			append(out, MQIACF_Q_ATTRS);
			append(out, 2);							// Count
			append(out, MQCA_Q_NAME);
			append(out, MQIA_Q_TYPE);

			return out;
		}

		QueueReply parseQueueReply(const unsigned char * data, size_t length, MQLONG encoding, MQLONG ccsid)
		{
			QueueReply reply;
			if ((data == nullptr) || (length < kHeaderLength))
			{
				addWarning(reply, "PCF reply of " + std::to_string(length) + " bytes is shorter than the " +
				                  std::to_string(kHeaderLength) + " byte header");
				return reply;
			}

			Reader in = { data, length, false };
			const MQLONG integerEncoding = encoding & MQENC_INTEGER_MASK;
			in.swap = (integerEncoding != MQENC_INTEGER_UNDEFINED) && (integerEncoding != (MQENC_NATIVE & MQENC_INTEGER_MASK));

			if (in.at(4) != (MQLONG)kHeaderLength)
			{
				addWarning(reply, "PCF header length " + std::to_string(in.at(4)) + " is not " + std::to_string(kHeaderLength));
				return reply;
			}

			reply.headerValid = true;
			reply.type = in.at(0);
			reply.last = in.at(20) != MQCFC_NOT_LAST;
			reply.compCode = in.at(24);
			reply.reason = in.at(28);
			const MQLONG parameterCount = in.at(32);

			const bool failed = reply.compCode == MQCC_FAILED;
			if (!failed && (reply.type != MQCFT_RESPONSE) && (reply.type != MQCFT_XR_ITEM))
			{
				return reply;	// a summary or message type: not a queue
			}

			const bool ebcdic = getCcsidType(ccsid) == CHAR_EBCDIC;
			bool haveName = false;
			bool haveType = false;
			bool nameDropped = false;
			size_t offset = kHeaderLength;

			for (MQLONG index = 0; (index < parameterCount) && !(haveName && haveType) && !failed; index++)
			{
				if (length - offset < kMinimumParameter)
				{
					addWarning(reply, "PCF reply ends inside parameter " + std::to_string(index + 1) + " of " + std::to_string(parameterCount));
					break;
				}

				const MQLONG type = in.at(offset);
				const MQLONG size = in.at(offset + 4);
				if (size < (MQLONG)kMinimumParameter)
				{
					addWarning(reply, "PCF parameter " + std::to_string(index + 1) + " has length " + std::to_string(size) + "; the rest of the reply is not read");
					break;
				}
				if ((size_t)size > length - offset)
				{
					addWarning(reply, "PCF parameter " + std::to_string(index + 1) + " of length " + std::to_string(size) + " runs past the end of the reply; the rest is not read");
					break;
				}

				if ((type == MQCFT_INTEGER) && ((size_t)size >= kIntegerFixed))
				{
					if (in.at(offset + 8) == MQIA_Q_TYPE)
					{
						reply.queueType = in.at(offset + 12);
						haveType = true;
					}
				}
				else if ((type == MQCFT_STRING) && ((size_t)size >= kStringFixed))
				{
					if (in.at(offset + 8) == MQCA_Q_NAME)
					{
						const MQLONG stringLength = in.at(offset + 16);
						if ((stringLength < 0) || ((size_t)stringLength > (size_t)size - kStringFixed))
						{
							addWarning(reply, "PCF queue name length " + std::to_string(stringLength) + " does not fit its parameter; the name is ignored");
							nameDropped = true;
						}
						else
						{
							std::string text((const char *)data + offset + kStringFixed, (size_t)stringLength);
							if (ebcdic && !text.empty())
							{
								std::vector<unsigned char> converted(text.size());
								EbcdicToAscii((const unsigned char *)text.data(), (unsigned int)text.size(), converted.data());
								text.assign((const char *)converted.data(), converted.size());
							}
							// MQParmCpy: the name ends at the first blank (MQ pads names with blanks)
							const size_t end = text.find_first_of(std::string(" \0", 2));
							if (end != std::string::npos)
							{
								text.erase(end);
							}
							if (text.empty())
							{
								addWarning(reply, "PCF queue name is empty; the entry is ignored");
								nameDropped = true;
							}
							else if (text.size() > kMaxQueueNameLength)
							{
								addWarning(reply, "PCF queue name of " + std::to_string(text.size()) + " characters is longer than " +
								                  std::to_string(kMaxQueueNameLength) + "; the entry is ignored");
								nameDropped = true;
							}
							else
							{
								reply.name = text;
								haveName = true;
							}
						}
						if (nameDropped)
						{
							break;
						}
					}
				}
				// byte strings, integer lists, string lists, and any type this code does not know: step over

				offset += (size_t)size;
			}

			if (failed)
			{
				// the first integer parameters of an error reply say which parameter was wrong
				size_t at = kHeaderLength;
				for (MQLONG index = 0; index < parameterCount; index++)
				{
					if (length - at < kMinimumParameter)
					{
						break;
					}
					const MQLONG size = in.at(at + 4);
					if ((size < (MQLONG)kMinimumParameter) || ((size_t)size > length - at))
					{
						break;
					}
					if ((in.at(at) == MQCFT_INTEGER) && ((size_t)size >= kIntegerFixed) && (in.at(at + 8) == MQIACF_PARAMETER_ID))
					{
						reply.parameterId = in.at(at + 12);
						break;
					}
					at += (size_t)size;
				}
				return reply;
			}

			reply.hasQueue = haveName && haveType && !nameDropped;
			return reply;
		}
	}
}
