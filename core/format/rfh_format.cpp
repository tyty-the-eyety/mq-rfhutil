// rfh_format.cpp - see rfh_format.h

#include "rfh_format.h"

#include <stdio.h>

#include <memory>

#include "DataFormatter.h"
#include "Copybook.h"
#include "comsubs.h"
#include "mqsubs.h"
#include "rfh_header_chain.h"

namespace rfh
{
	namespace format
	{
		namespace
		{
			// DataArea::getCobolData, with the copy book named by the options
			// instead of chosen in a file dialog
			Result renderCobol(const DataFormatter & formatter, const RenderOptions & options, std::string & out)
			{
				if (options.copybookPath.empty())
				{
					return Result::failure("the COBOL view needs a copy book file");
				}

				CCopybook copybook;
				copybook.resetCopybook();
				int rc = copybook.parseCopyBook(options.copybookPath.c_str());
				if (rc != 0)
				{
					char text[32];
					snprintf(text, sizeof(text), " could not be read (%d)", rc);
					return Result::failure("copy book file " + options.copybookPath + text);
				}

				if (formatter.fileSize > 0)
				{
					int maxSize = (copybook.getMaxLines() * 128) + 65536;
					std::vector<char> textArea(maxSize + 8192, 0);
					copybook.formatData(textArea.data(), maxSize, formatter.fileData, formatter.fileSize,
						options.charFormat, options.numericFormat, options.pdNumericFormat,
						formatter.fileCcsid, options.indent, options.checkData);
					out = "Level  Ofs  Len Type  Occ Variable Name                  Value\r\n";
					out += textArea.data();
				}
				return Result::success();
			}
		}

		DataFormats defaultsFor(const Message & message, std::vector<std::string> & warnings)
		{
			DataFormats options;

			// DataArea::processMessage
			options.intFormat = ((message.mqmd.Encoding & MQENC_INTEGER_REVERSED) > 0) ? NUMERIC_PC : NUMERIC_HOST;
			MQLONG ccsid = message.mqmd.CodedCharSetId;
			options.ccsid = ccsid;
			if ((ccsid < 0) || (ccsid > 99999))
			{
				MQLONG reversed = reverseBytes4(ccsid);
				if ((reversed > 0) && (reversed < 99999))
				{
					options.ccsid = reversed;
				}
				else
				{
					char text[64];
					snprintf(text, sizeof(text), "Invalid code page in MQMD - %d\r\n", (int)ccsid);
					warnings.push_back(text);
					options.ccsid = 0;
				}
			}

			// DataArea::parseMsgHeaders: the data after the last header
			// The character type comes from the repaired code page while the walk uses
			// the raw one, as on Windows.
			MQLONG typeCcsid = ((ccsid < 0) || (ccsid > 99999)) ? reverseBytes4(ccsid) : ccsid;
			HeaderChain chain = walkHeaders(message.headers.data(), message.headers.size(),
				std::string(message.mqmd.Format, MQ_FORMAT_LENGTH), message.mqmd.CodedCharSetId,
				getCcsidType(typeCcsid), getIntEncode(message.mqmd.Encoding));
			options.charFormat = chain.charFormat;
			options.numericFormat = chain.encoding;
			options.pdNumericFormat = (NUMERIC_PC == chain.encoding) ? NUMERIC_PC : NUMERIC_HOST;
			return options;
		}

		DataFormats defaultsFor(const fileio::ReadResult & read)
		{
			DataFormats options;
			options.ccsid = read.fileCcsid;
			options.intFormat = read.fileIntFormat;
			options.charFormat = read.charFormat;
			options.numericFormat = read.numericFormat;
			options.pdNumericFormat = read.pdFormat;
			return options;
		}

		Result render(const Message & message, View view, const RenderOptions & options, std::string & out)
		{
			out.clear();

			// on the heap: its XML work areas are about 125 KB
			std::unique_ptr<DataFormatter> owned(new DataFormatter());
			DataFormatter & formatter = *owned;
			formatter.setData(message.body.data(), (unsigned int)message.body.size());
			formatter.fileCcsid = options.ccsid;
			formatter.fileIntFormat = options.intFormat;
			formatter.m_char_format = options.charFormat;
			formatter.display = options.display;
			formatter.conversionTableDir = options.conversionTableDir;

			const unsigned char * text = NULL;
			switch (view)
			{
			case View::Character:
				formatter.getCharacterData(options.charFormat, options.crlf, options.edi, options.bom, options.indent);
				text = formatter.m_data_ascii;
				break;
			case View::Hex:
				formatter.getHexData();
				text = formatter.m_data_hex;
				break;
			case View::Both:
				formatter.getBothData(options.charFormat, options.bom);
				text = formatter.m_data_both;
				break;
			case View::Xml:
				formatter.getXmlData(options.charFormat);
				text = formatter.m_data_xml;
				break;
			case View::Parsed:
				formatter.getParsedData(options.charFormat);
				text = formatter.m_data_parsed;
				break;
			case View::Json:
				formatter.getJsonData(options.charFormat);
				text = formatter.m_data_json;
				break;
			case View::Fixed:
				formatter.getFixData(options.charFormat);
				text = formatter.m_data_fix;
				break;
			case View::Cobol:
				return renderCobol(formatter, options, out);
			}

			Result result = Result::success();
			if (!formatter.m_error_msg.empty())
			{
				result.warnings.push_back(formatter.m_error_msg);
			}
			if (text != NULL)
			{
				// the edit control shows the text up to its terminator
				out.assign((const char *)text);
			}
			return result;
		}
	}
}
