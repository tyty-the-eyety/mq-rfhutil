// rfh_format.cpp - see rfh_format.h

#include "rfh_format.h"

#include <stdio.h>

#include <memory>

#include "comsubs.h"
#include "mqsubs.h"
#include "rfh_header_chain.h"

namespace rfh
{
	namespace format
	{
		RenderOptions defaultsFor(const Message & message, std::vector<std::string> & warnings)
		{
			RenderOptions options;

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
			HeaderChain chain = walkHeaders(message.headers.data(), message.headers.size(), message.mqmd);
			options.charFormat = chain.charFormat;
			options.numericFormat = chain.encoding;
			options.pdNumericFormat = (NUMERIC_PC == chain.encoding) ? NUMERIC_PC : NUMERIC_HOST;
			return options;
		}

		RenderOptions defaultsFor(const fileio::ReadResult & read)
		{
			RenderOptions options;
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
			std::unique_ptr<DataFormatter> formatter(new DataFormatter());
			formatter->setData(message.body.data(), (unsigned int)message.body.size());
			formatter->fileCcsid = options.ccsid;
			formatter->fileIntFormat = options.intFormat;
			formatter->m_char_format = options.charFormat;
			formatter->display = options.display;
			formatter->conversionTableDir = options.conversionTableDir;

			const unsigned char * text = NULL;
			switch (view)
			{
			case View::Character:
				formatter->getCharacterData(options.charFormat, options.crlf, options.edi, options.bom, options.indent);
				text = formatter->m_data_ascii;
				break;
			case View::Hex:
				formatter->getHexData();
				text = formatter->m_data_hex;
				break;
			case View::Both:
				formatter->getBothData(options.charFormat, options.bom);
				text = formatter->m_data_both;
				break;
			case View::Xml:
				formatter->getXmlData(options.charFormat);
				text = formatter->m_data_xml;
				break;
			case View::Parsed:
				formatter->getParsedData(options.charFormat);
				text = formatter->m_data_parsed;
				break;
			case View::Json:
				formatter->getJsonData(options.charFormat);
				text = formatter->m_data_json;
				break;
			case View::Fixed:
				formatter->getFixData(options.charFormat);
				text = formatter->m_data_fix;
				break;
			case View::Cobol:
				return Result::failure("the COBOL view is not supported yet");
			}

			Result result = Result::success();
			if (!formatter->m_error_msg.empty())
			{
				result.warnings.push_back(formatter->m_error_msg);
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
