// rfh_format.cpp - see rfh_format.h

#include "rfh_format.h"

#include <stdio.h>

#include <stdint.h>

#include <memory>
#include <new>
#include <string>

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
			// parseCopyBook's result codes
			std::string copybookError(const std::string & path, int rc)
			{
				std::string text = "copy book file " + path;
				switch (rc)
				{
				case 100:
					return text + " could not be opened";
				case 97:
					return text + " has a level that is outside its record's hierarchy (97)";
				case 99:
					return text + " redefines a field that it does not define (99)";
				default:
					return text + " could not be parsed (" + std::to_string(rc) + ")";
				}
			}

			// DataArea::getCobolData, with the copy book named by the options
			// instead of chosen in a file dialog. The copy book is parsed again for
			// every render.
			Result renderCobol(const DataFormatter & formatter, const RenderOptions & options, std::string & out)
			{
				if (options.copybookPath.empty())
				{
					return Result::failure("the COBOL view needs a copy book file");
				}

				try
				{
					CCopybook copybook;
					copybook.resetCopybook();
					int rc = copybook.parseCopyBook(options.copybookPath.c_str());
					if (rc != 0)
					{
						return Result::failure(copybookError(options.copybookPath, rc));
					}

					if (formatter.fileSize > 0)
					{
						// Windows' buffer: 128 bytes a line and 65536 more, and 8192
						// beyond the size formatData is given, which it may write into
						int64_t maxSize = ((int64_t)copybook.getMaxLines() * 128) + 65536;
						if (maxSize > kMaxCobolBuffer)
						{
							return Result::failure("copy book file " + options.copybookPath
								+ " needs a buffer of " + std::to_string(maxSize)
								+ " bytes, more than the COBOL view's limit of " + std::to_string(kMaxCobolBuffer));
						}

						// not cleared: formatData writes a string from the start
						std::unique_ptr<char[]> textArea(new char[(size_t)maxSize + 8192]);
						textArea[0] = 0;
						copybook.formatData(textArea.get(), (int)maxSize, formatter.fileData, formatter.fileSize,
							options.charFormat, options.numericFormat, options.pdNumericFormat,
							formatter.fileCcsid, options.indent, options.checkData);
						out = "Level  Ofs  Len Type  Occ Variable Name                  Value\r\n";
						out += textArea.get();
					}
				}
				catch (const std::bad_alloc &)
				{
					out.clear();
					return Result::failure("there is not enough memory for the COBOL view of copy book file " + options.copybookPath);
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

			// before anything is copied: larger sizes overflow the formatters' arithmetic
			if (message.body.size() > kMaxRenderBody)
			{
				return Result::failure("the message body is " + std::to_string(message.body.size())
					+ " bytes, more than the Data tab's limit of " + std::to_string(kMaxRenderBody)
					+ " bytes (MQ's maximum message length)");
			}

			// on the heap: its XML work areas are about 125 KB
			std::unique_ptr<DataFormatter> owned(new DataFormatter());
			DataFormatter & formatter = *owned;
			formatter.setData(message.body.data(), (unsigned int)message.body.size());
			formatter.fileCcsid = options.ccsid;
			formatter.fileIntFormat = options.intFormat;
			formatter.m_char_format = options.charFormat;
			formatter.setDisplay(options.display);
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

		namespace
		{
			// the offset label of a rendered line: digits and a blank ("%8.8d ")
			struct Label
			{
				int64_t value = 0;
				size_t prefix = 0;	// characters before the data: the digits and the blank
			};

			bool readLabel(std::string_view line, Label & label)
			{
				size_t i = 0;
				int64_t value = 0;
				while (i < line.size() && line[i] >= '0' && line[i] <= '9')
				{
					if (i >= 18)
					{
						return false;
					}
					value = value * 10 + (line[i] - '0');
					i++;
				}
				if (i == 0 || i >= line.size() || line[i] != ' ')
				{
					return false;
				}
				label.value = value;
				label.prefix = i + 1;
				return true;
			}
		}

		bool locate(View view, const RenderOptions & options, const Message & message, std::string_view text, size_t offset, TextPos & pos)
		{
			if ((view != View::Character && view != View::Hex && view != View::Both) || offset >= message.body.size())
			{
				return false;
			}

			// DataFormatter drops a leading FE byte from the Character and Both views when asked to
			int64_t shown = (int64_t)offset;
			if (view != View::Hex && options.bom && message.body[0] == 0xFE)
			{
				shown--;
				if (shown < 0)
				{
					pos.line = 0;
					pos.column = 0;
					pos.length = 0;
					return true;
				}
			}

			// the last line whose first byte is at or before the shown byte
			bool found = false;
			int line = 0;
			Label label;
			std::string_view lineText;
			size_t start = 0;
			for (int number = 0; start <= text.size() && !text.empty(); number++)
			{
				size_t end = text.find('\n', start);
				size_t next = (end == std::string_view::npos) ? text.size() : end + 1;
				if (end == std::string_view::npos)
				{
					end = text.size();
				}
				else if (end > start && text[end - 1] == '\r')
				{
					end--;
				}
				std::string_view current = text.substr(start, end - start);
				Label candidate;
				if (!readLabel(current, candidate))
				{
					return false;
				}
				if (candidate.value > shown)
				{
					break;
				}
				found = true;
				line = number;
				label = candidate;
				lineText = current;
				if (next >= text.size())
				{
					break;
				}
				start = next;
			}

			int column = 0;
			int length = 1;
			if (view == View::Character && (options.crlf || options.edi))
			{
				// Lines start at the offset of their label, which can be past the offset when the view dropped
				// the CR and LF bytes before it. The bytes of a line follow its label one for one; the CR and
				// LF that end it are not in the text, so they select nothing at the end of the line.
				if (!found)
				{
					line = 0;
					length = 0;
				}
				else
				{
					const size_t col = label.prefix + (size_t)(shown - label.value);
					if (col < lineText.size())
					{
						column = (int)col;
					}
					else
					{
						column = (int)lineText.size();
						length = 0;
					}
				}
			}
			else
			{
				if (!found)
				{
					return false;
				}
				int64_t k = shown - label.value;
				size_t col;
				switch (view)
				{
				case View::Hex:
					if (k > 15)
					{
						return false;
					}
					col = label.prefix + (size_t)(2 * k + k / 4);
					length = 2;
					break;
				case View::Both:
					if (k > 15)
					{
						return false;
					}
					col = label.prefix + 4 * 9 + (size_t)(k + k / 8);
					break;
				default:
					col = label.prefix + (size_t)k;
					break;
				}
				if (col + (size_t)length > lineText.size())
				{
					return false;
				}
				column = (int)col;
			}

			pos.line = line;
			pos.column = column;
			pos.length = length;
			return true;
		}

		bool locate(View view, const RenderOptions & options, const Message & message, size_t offset, TextPos & pos)
		{
			if ((view != View::Character && view != View::Hex && view != View::Both) || offset >= message.body.size())
			{
				return false;
			}
			std::string text;
			if (!render(message, view, options, text).ok())
			{
				return false;
			}
			return locate(view, options, message, std::string_view(text), offset, pos);
		}
	}
}
