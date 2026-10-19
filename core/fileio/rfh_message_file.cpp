// rfh_message_file.cpp - see rfh_message_file.h

#include "rfh_message_file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

#include <algorithm>
#include <memory>
#include <new>

#include "rfh_codepage.h"
#include "rfh_platform.h"
#include "rfhconst.h"
#include "comsubs.h"
#include "mqsubs.h"
#include "xmlsubs.h"

namespace rfh
{
	namespace fileio
	{
		namespace
		{
			std::string number(int value)
			{
				char temp[16];
				snprintf(temp, sizeof(temp), "%d", value);
				return temp;
			}

			// FileHandler::changeUnixFile: a bare LF becomes CR LF, CR LF pairs stay
			void unixToCrLf(std::vector<unsigned char> & data)
			{
				std::vector<unsigned char> out;
				out.reserve(data.size() * 2);
				for (size_t i = 0; i < data.size(); i++)
				{
					if ((data[i] == '\r') && (i + 1 < data.size()) && (data[i + 1] == '\n'))
					{
						out.push_back(data[i]);
						out.push_back(data[++i]);
					}
					else
					{
						if (data[i] == '\n')
						{
							out.push_back('\r');
						}
						out.push_back(data[i]);
					}
				}
				data.swap(out);
			}

			// FileHandler::openOutputFile's messages, mapped from errno
			std::string openOutputError(int err)
			{
				switch (err)
				{
				case EBUSY: return "Unable to open output file - sharing violation";
				case ENOLCK: return "Unable to open output file - file locked";
				case ENOSPC: return "Unable to open output file - disk full";
				case ENOENT: case ENOTDIR: return "Unable to open output file - path not found";
				case EACCES: case EPERM: case EROFS: case EISDIR: return "Unable to open output file - access denied";
				default: return "Unable to open output file - Error " + number(err);
				}
			}

			struct HeaderLengths
			{
				size_t rfh1 = 0, rfh2 = 0, dlq = 0, cics = 0, ims = 0;
			};

			HeaderLengths lengthsOf(const HeaderChain & chain)
			{
				HeaderLengths l;
				for (const HeaderInfo & h : chain.headers)
				{
					switch (h.type)
					{
					case HeaderType::Rfh1: l.rfh1 += h.length; break;
					case HeaderType::Rfh2: l.rfh2 += h.length; break;
					case HeaderType::DeadLetter: l.dlq += h.length; break;
					case HeaderType::Cics: l.cics += h.length; break;
					case HeaderType::Ims: l.ims += h.length; break;
					case HeaderType::MdExtension: break;	// not counted, as on Windows
					}
				}
				return l;
			}
		}

		Result read(const std::string & path, const ReadOptions & options, MqmdFields & fields, ReadResult & out)
		{
			out = ReadResult();

			// Windows' test for an ANSI i-circumflex (0xEE) in the name; in UTF-8 it
			// rejects only the lead bytes of the private-use range.
			if (path.find('\xEE') != std::string::npos)
			{
				return Result::failure("Invalid file name (" + path + ")");
			}

			typedef std::unique_ptr<FILE, int (*)(FILE *)> FilePtr;
			FilePtr input(fopen(path.c_str(), "rb"), fclose);
			struct stat info;
			if (!input || (fstat(fileno(input.get()), &info) != 0) || !S_ISREG(info.st_mode))
			{
				return Result::failure("Could not open file " + path);
			}

			// read everything before the tab is touched, so a failure leaves it unchanged
			std::vector<unsigned char> data;
			fseek(input.get(), 0L, SEEK_END);
			long size = ftell(input.get());
			if (size < 0)
			{
				return Result::failure("Could not read file " + path);
			}
			fseek(input.get(), 0L, SEEK_SET);
			if (size > 0)
			{
				try
				{
					data.resize((size_t)size);
				}
				catch (const std::bad_alloc &)
				{
					return Result::failure("Not enough memory to read " + path);
				}
				size_t got = fread(data.data(), 1, data.size(), input.get());
				if (ferror(input.get()))
				{
					return Result::failure("Could not read file " + path);
				}
				data.resize(got);
			}
			input.reset();

			// default code page and encoding: the setting, then the MQMD tab, then the system
			int ccsid = options.fileCcsid;
			if (0 == ccsid)
			{
				ccsid = atoi(fields.ccsid.c_str());
				if (0 == ccsid)
				{
					ccsid = (options.ansiCcsid != 0) ? options.ansiCcsid : rfh_get_acp();
					if (0 == ccsid)
					{
						// kept for Windows fidelity; rfh_get_acp() never returns 0 on Linux
						ccsid = 1252;
					}
				}
			}
			int encoding = (fields.encoding < 0) ? NUMERIC_PC : fields.encoding;

			// ClearData + setEncoding(PC, PC, PC) + clearMQMD
			fields.floatEncoding = FLOAT_PC;
			mqmd_fields::clear(fields);

			// a UTF-16 byte order mark decides the encoding
			if ((data.size() >= 2) && (0xFF == data[0]) && (0xFE == data[1]))
			{
				encoding = NUMERIC_PC;
			}
			else if ((data.size() >= 2) && (0xFE == data[0]) && (0xFF == data[1]))
			{
				encoding = NUMERIC_HOST;
			}

			out.fileCcsid = ccsid;
			out.fileIntFormat = encoding;
			out.numericFormat = NUMERIC_PC;
			out.charFormat = getCcsidType(ccsid);

			// an MQMD at the front (MQMDPAGE::processMQMD; at least sizeof(MQMD2) bytes)
			if (data.size() >= sizeof(MQMD2))
			{
				MQMD2 md;
				memcpy(&md, data.data(), sizeof(md));
				if (translateMQMD(&md) && mqmd_fields::fromMqmd(md, fields))
				{
					size_t mqmdLength = (MQMD_VERSION_1 == md.Version) ? sizeof(MQMD1) : sizeof(MQMD2);
					out.hadMqmd = true;
					data.erase(data.begin(), data.begin() + mqmdLength);
					out.fileCcsid = atoi(fields.ccsid.c_str());
					out.fileIntFormat = fields.encoding;
					out.numericFormat = fields.encoding;
					out.charFormat = getCcsidType(md.CodedCharSetId);
				}
			}

			if (!out.hadMqmd && !options.ignoreHeaders)
			{
				LeadingHeader lead = detectLeadingHeader(data.data(), data.size());
				if (lead.found)
				{
					char format[MQ_FORMAT_LENGTH + 1] = { 0 };
					memcpy(format, lead.format.data(), (std::min)(lead.format.size(), (size_t)MQ_FORMAT_LENGTH));
					Rtrim(format);
					// Windows stores the format blank-padded; trimmed here, which is
					// display-only because the same bytes are written back
					fields.format = format;
					fields.encoding = lead.encoding;
					fields.pdEncoding = lead.pdEncoding;
					fields.floatEncoding = lead.floatEncoding;
					fields.ccsid = number(lead.ccsid);
					out.charFormat = lead.charFormat;
				}
				else if ((0 == options.fileCcsid) && looksLikeEbcdic(data.data(), data.size()))
				{
					out.charFormat = CHAR_EBCDIC;
					out.numericFormat = NUMERIC_HOST;
					fields.ccsid = number(DEF_EBCDIC_CCSID);
					fields.encoding = NUMERIC_HOST;
					fields.pdEncoding = NUMERIC_HOST;
					fields.floatEncoding = FLOAT_390;
				}
			}

			if (!options.ignoreHeaders)
			{
				// read starts from the MQMD tab's fields, which match the MQMD apart from
				// fromMqmd's format cut-off at the first byte <= 0x20 or >= 0x80; deliberate,
				// because Windows walks from the tab
				out.chain = walkHeaders(data.data(), data.size(), fields.format,
				                        atoi(fields.ccsid.c_str()), out.charFormat, fields.encoding);
				out.message.headers.assign(data.begin(), data.begin() + out.chain.bodyOffset);
				data.erase(data.begin(), data.begin() + out.chain.bodyOffset);

				out.charFormat = out.chain.charFormat;
				out.numericFormat = out.chain.encoding;

				// an MQMDE carries version 2 MQMD fields onto the MQMD tab
				if (out.chain.hasMdExtension)
				{
					mqmd_fields::applyMdExtension(out.chain.mdExtension, fields);
				}
			}

			// Windows leaves the float format at the previous file's value when the
			// headers are ignored; it is derived here
			if (NUMERIC_PC == out.numericFormat)
			{
				out.pdFormat = NUMERIC_PC;
				out.floatFormat = FLOAT_PC;
			}
			else
			{
				out.pdFormat = NUMERIC_HOST;
				out.floatFormat = FLOAT_390;
			}
			if (options.ignoreHeaders && out.hadMqmd)
			{
				out.pdFormat = fields.pdEncoding;
			}

			// the MQMD the tab now holds, whether or not the file had one
			out.message.mqmd = MQMD2{ MQMD2_DEFAULT };
			out.message.mqmd.Version = MQMD_VERSION_2;
			mqmd_fields::toMqmd(fields, out.message.mqmd);

			Result r;
			HeaderLengths l = lengthsOf(out.chain);
			size_t headerBytes = l.rfh1 + l.rfh2 + l.dlq + l.cics + l.ims;
			char text[1024];
			if (headerBytes > 0)
			{
				snprintf(text, sizeof(text), "%d bytes read (data %d dlq %d rfh1 %d, rfh2 %d cics %d ims %d) from file %s",
				         (int)(data.size() + headerBytes), (int)data.size(), (int)l.dlq, (int)l.rfh1, (int)l.rfh2,
				         (int)l.cics, (int)l.ims, path.c_str());
			}
			else
			{
				snprintf(text, sizeof(text), "%d bytes read from file %s", (int)data.size(), path.c_str());
			}
			r.text = text;
			r.warnings = out.chain.warnings;

			if (options.removeCrLf)
			{
				data.push_back(0);
				int len = removeCrLf(data.data(), (unsigned int)(data.size() - 1));
				data.resize((size_t)len);
			}
			else if (options.unixLineEnds)
			{
				unixToCrLf(data);
			}

			out.message.body.swap(data);

			std::string shown = path;
			if (shown.size() > 75)
			{
				shown = "..." + shown.substr(shown.size() - 75);
			}
			out.dataSource = "Message Data (" + number((int)out.message.body.size()) + ") from " + shown;
			return r;
		}

		Result write(const std::string & path, const Message & message, const WriteOptions & options)
		{
			std::unique_ptr<FILE, int (*)(FILE *)> file(fopen(path.c_str(), "wb"), fclose);
			if (!file)
			{
				return Result::failure(openOutputError(errno));
			}

			FILE * output = file.get();
			size_t mqmdLength = 0;
			bool ok = true;
			if (options.includeMqmd)
			{
				MQMD2 md = message.mqmd;
				md.Version = MQMD_VERSION_2;
				ok = ok && (fwrite(&md, 1, sizeof(md), output) == sizeof(md));
				mqmdLength = sizeof(md);
			}

			const std::vector<unsigned char> noHeaders;
			const std::vector<unsigned char> & headers = options.dataOnly ? noHeaders : message.headers;
			if (!headers.empty())
			{
				ok = ok && (fwrite(headers.data(), 1, headers.size(), output) == headers.size());
			}
			if (!message.body.empty())
			{
				ok = ok && (fwrite(message.body.data(), 1, message.body.size(), output) == message.body.size());
			}
			if (fclose(file.release()) != 0)
			{
				ok = false;
			}
			if (!ok)
			{
				return Result::failure("Unable to write output file " + path + " - " + strerror(errno));
			}

			// the breakdown names each header kind, as Windows does
			HeaderChain chain = walkHeaders(headers.data(), headers.size(), message.mqmd);
			HeaderLengths l = lengthsOf(chain);

			std::string breakdown;
			if (headers.size() + mqmdLength > 0)
			{
				breakdown = " (data " + number((int)message.body.size());
				if (mqmdLength > 0) breakdown += " mqmd " + number((int)mqmdLength);
				if (l.rfh1 > 0) breakdown += " rfh1 " + number((int)l.rfh1);
				if (l.rfh2 > 0) breakdown += " rfh2 " + number((int)l.rfh2);
				if (l.dlq > 0) breakdown += " dlq " + number((int)l.dlq);
				if (l.cics > 0) breakdown += " cics " + number((int)l.cics);
				if (l.ims > 0) breakdown += " ims " + number((int)l.ims);
				breakdown += ")";
			}

			Result r;
			r.text = number((int)(message.body.size() + headers.size() + mqmdLength)) + " bytes" + breakdown + " written to file " + path;
			return r;
		}
	}
}
