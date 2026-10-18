// rfh_format.h - the Data tab's views of a message body.
//
// render produces the text the Windows Data tab shows, with DataFormatter
// (RFHUtil/DataFormatter.cpp), the code the Windows Data tab uses. The text is in
// the display code page options.display.ansi, as Windows puts it in the edit
// control; a UI decodes it from that code page. The bytes that code page does not
// display are shown as '.', as on Windows for its ANSI code page, whatever
// rfh_get_acp says (DataFormatter::setDisplay).
//
// Differences from Windows RFHUtil:
// - the COBOL view reads the copy book named by options.copybookPath instead of
//   asking for one, and fails when it cannot be opened (Windows shows an empty
//   view);
// - the COBOL view also fails when IBM's parser reports an error in the copy book
//   (a level outside its record's hierarchy, or a REDEFINES of a field it does not
//   define), where Windows ignores the error and shows the partly parsed copy book,
//   and when the copy book needs a buffer larger than kMaxCobolBuffer (Windows
//   tries to allocate any size, up to the 2 GB at which getMaxLines saturates);
// - the COBOL view does not list a group occurrence that starts past the end of
//   the data (when the data is shorter than the copy book, or a REDEFINES is
//   larger than what it redefines), and an OCCURS DEPENDING ON without a maximum
//   lists only the occurrences that fit in the data; Windows lists them with
//   empty values (tools/port/harden-copybook.pl);
// - every view fails for a body larger than kMaxRenderBody, where Windows' buffer
//   sizes overflow (the character view's above about 178 MB);
// - see DataFormatter.h for the differences in the views themselves.
//
// The COBOL view finds whole multibyte characters with rfh_mbclen, which uses the
// process ANSI code page (rfh_get_acp), as _mbclen does on Windows, and not
// options.display.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "rfh_codepage.h"
#include "rfh_message.h"
#include "rfh_message_file.h"
#include "rfh_result.h"
#include "rfhconst.h"

namespace rfh
{
	namespace format
	{
		// The Data tab's display choices, in the order of DATA_CHARACTER ... DATA_FIX.
		enum class View { Character, Hex, Both, Xml, Parsed, Cobol, Json, Fixed };

		static_assert((int)View::Fixed == DATA_FIX && (int)View::Cobol == DATA_COBOL && (int)View::Character == DATA_CHARACTER,
			"View follows DATA_*");

		// The formats of a message body, which come from the message and not from the
		// user: defaultsFor gives the values Windows starts with.
		struct DataFormats
		{
			int ccsid = 0;						// code page of the body (DataArea::fileCcsid)
			int intFormat = NUMERIC_PC;			// integer encoding of the body (DataArea::fileIntFormat)
			int charFormat = CHAR_ASCII;		// CHAR_* the body is shown as
			int numericFormat = NUMERIC_PC;		// COBOL view: binary integers
			int pdNumericFormat = NUMERIC_PC;	// COBOL view: packed decimal
		};

		// The Data tab's settings: the message's formats and the user's choices. A UI
		// calls apply after each get or read, which replaces the formats and keeps the
		// user's other choices.
		struct RenderOptions : DataFormats
		{
			bool crlf = false;					// character view: start a line after each CR LF
			bool edi = false;					// character view: a line per EDI segment
			bool bom = false;					// character and both views: honour a byte order mark
			bool indent = false;				// character view: indent XML; COBOL view: indent levels
			bool checkData = false;				// COBOL view: check the field contents
			DisplayCodepages display = { rfh_get_acp(), 437 };	// Windows GetACP() and GetOEMCP()
			std::string conversionTableDir;		// MQ's conversion tables; empty for none
			std::string copybookPath;			// COBOL view: the copy book file

			// Copies the formats and nothing else.
			void apply(const DataFormats & f)
			{
				static_cast<DataFormats &>(*this) = f;
			}
		};

		// The settings after getting this message, as DataArea::processMessage and
		// parseMsgHeaders set them: the MQMD's code page and integer encoding, and
		// the character format and encoding of the data after the last header. A
		// code page that is out of range is byte-reversed if that makes it valid
		// (Windows' workaround for SIBus), and otherwise becomes 0 with a warning.
		DataFormats defaultsFor(const Message & message, std::vector<std::string> & warnings);

		// The settings after reading a message file (the formats ReadResult reports).
		DataFormats defaultsFor(const fileio::ReadResult & read);

		// The largest buffer the COBOL view allocates: 256 MB, about two million lines
		// at Windows' 128 bytes a line. The output stops earlier, as on Windows, with
		// "***** Exceeded memory buffer" when the lines are longer.
		const int64_t kMaxCobolBuffer = 256 * 1024 * 1024;

		// The largest body render shows: 100 MB (104,857,600 bytes), MQ's maximum
		// message length. The formatters size their buffers with int and unsigned int
		// arithmetic of up to 16 bytes a byte of data (the JSON view), which stays
		// below 2^31 at this size but overflows not far above it (the character view's
		// fileLength * 12 + 45 at about 178 MB). render fails for a larger body before
		// copying it.
		const size_t kMaxRenderBody = 100 * 1024 * 1024;

		// Renders message.body (the headers are not shown, as on Windows) into out.
		// Each call makes a new formatter and caches nothing (the COBOL view parses
		// its copy book again each time), so a UI that renders large messages often
		// should keep the result.
		// Returns a failure only when the view cannot be produced (including a body
		// over kMaxRenderBody); problems Windows reports in the status line are
		// warnings.
		Result render(const Message & message, View view, const RenderOptions & options, std::string & out);

		// A position in the text render produces, as the Data tab selects it.
		struct TextPos
		{
			int line = 0;		// zero-based line (lines end in CR LF)
			int column = 0;		// zero-based byte index in that line of the rendered text
			int length = 0;		// bytes to select: 2 for a byte in the Hex view, 0 for a byte the view does not show, otherwise 1
		};

		// Where byte `offset` of message.body is in the text render(message, view, options)
		// produces: Go To and the highlight of a Find hit (Windows getLineNumber,
		// getLineOffset and MSGDATA::moveDisplay). Supported views: Character, Hex, Both.
		// Returns false, leaving pos alone, for another view, an empty body, an offset
		// outside the body, or text that does not have the layout of the view.
		//
		// The formatter owns the layout, so locate reads the line each byte is on from
		// the offset label each rendered line starts with (the offset, in the shown data,
		// of its first byte) and only adds the position within the line.
		//
		// Differences from Windows (all of them places where its formulas select the
		// wrong text):
		// - the Both view selects the byte's character in the text part (column 45 and
		//   on), where Windows' ofs % 16 + 9 lands in the hex digits;
		// - the byte order mark that the BOM option hides (Character and Both) is not
		//   counted, so later bytes move one back; the mark itself is at line 0, column
		//   0, length 0 (Windows ignores the option and selects a byte too far on);
		// - a double-byte character is one of the 32 on its line, so the offsets after
		//   it are on the line the view shows them on (Windows divides by 32 bytes);
		// - with CR/LF on, the line is the line the view shows the byte on (Windows counts LF bytes,
		//   which is wrong after a blank line); with EDI on it is the segment line (Windows always says
		//   line 0). The column is the byte's own (Windows always used 0, the first digit of the label
		//   and not the byte); a CR or LF that the view does not show is at the end of its line with
		//   length 0;
		// - a label wider than 8 digits (a body over 99,999,999 bytes) moves the columns
		//   right, which Windows' formulas do not allow for.
		bool locate(View view, const RenderOptions & options, const Message & message, size_t offset, TextPos & pos);

		// The same, for a UI that already holds the text of render(message, view, options):
		// nothing is rendered again and the work is one pass over the lines.
		bool locate(View view, const RenderOptions & options, const Message & message, std::string_view text, size_t offset, TextPos & pos);
	}
}
