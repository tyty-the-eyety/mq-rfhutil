// rfh_id_display.cpp - see rfh_id_display.h

#include "rfh_id_display.h"

#include <string.h>

#include "comsubs.h"

namespace rfh
{
	namespace id_display
	{
		namespace
		{
			const size_t kIdLength = 24;

			// A character of a text, or the NUL terminator when past the end.
			unsigned char charAt(const std::string & text, size_t i)
			{
				return i < text.size() ? static_cast<unsigned char>(text[i]) : 0;
			}

			// Hex edit: HexToAscii of the first length/2 bytes (setMsgId 4012).
			void applyHex(MQBYTE24 id, const std::string & text)
			{
				std::string work = text;
				work.push_back('\0');
				HexToAscii(reinterpret_cast<unsigned char *>(&work[0]), static_cast<unsigned int>(text.size() / 2), reinterpret_cast<unsigned char *>(id));
			}

			// Ascii and Ebcdic edit: only the characters that differ from the previous text replace a byte.
			void applyChangedCharacters(MQBYTE24 id, const std::string & text, const std::string & previous, bool ebcdic)
			{
				unsigned char padded[kIdLength] = { 0 };
				unsigned char converted[kIdLength] = { 0 };
				memcpy(padded, text.data(), text.size() < kIdLength ? text.size() : kIdLength);
				if (ebcdic)
				{
					AsciiToEbcdic(padded, static_cast<unsigned int>(kIdLength), converted);
				}

				for (size_t i = 0; i < kIdLength; i++)
				{
					if (i > text.size())
					{
						id[i] = 0;
					}
					else if (charAt(text, i) != charAt(previous, i))
					{
						id[i] = ebcdic ? converted[i] : charAt(text, i);
					}
				}
			}
		}

		size_t maxLength(Mode mode)
		{
			return Mode::Hex == mode ? kIdLength * 2 : kIdLength;
		}

		std::string toText(const MQBYTE24 id, Mode mode)
		{
			unsigned char out[2 * kIdLength + 1] = { 0 };

			if (Mode::Hex == mode)
			{
				AsciiToHex(reinterpret_cast<const unsigned char *>(id), static_cast<unsigned int>(kIdLength), out);
				return std::string(reinterpret_cast<char *>(out), 2 * kIdLength);
			}

			if (Mode::Ebcdic == mode)
			{
				EbcdicToAscii(reinterpret_cast<const unsigned char *>(id), static_cast<unsigned int>(kIdLength), out);
			}
			else
			{
				memcpy(out, id, kIdLength);
			}

			for (size_t i = 0; i < kIdLength; i++)
			{
				if (out[i] < ' ')
				{
					out[i] = ' ';
				}
			}

			return std::string(reinterpret_cast<char *>(out), kIdLength);
		}

		bool applyEdit(Field field, MQBYTE24 id, const std::string & typed, const std::string & previous, Mode mode, std::string & error)
		{
			error.clear();
			std::string text = typed;

			if (Mode::Hex == mode)
			{
				if (text.size() > 2 * kIdLength)
				{
					error = "The identifier cannot be longer than 48 hex digits";
					return false;
				}

				if (Field::MsgId != field)
				{
					text.resize(2 * kIdLength, '0');
				}

				applyHex(id, text);
				return true;
			}

			if (Field::CorrelId == field)
			{
				text.resize(kIdLength, ' ');
			}
			else if (Field::GroupId == field && text.size() > kIdLength)
			{
				error = "The identifier cannot be longer than 24 characters";
				return false;
			}

			applyChangedCharacters(id, text, previous, Mode::Ebcdic == mode);
			return true;
		}

		bool isHexDigit(char c)
		{
			return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
		}

		bool isZero(const MQBYTE24 id)
		{
			for (size_t i = 0; i < kIdLength; i++)
			{
				if (0 != id[i])
				{
					return false;
				}
			}

			return true;
		}

		void reset(MQBYTE24 id)
		{
			memset(id, 0, kIdLength);
		}
	}
}
