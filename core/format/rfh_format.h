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
		struct RenderOptions
		{
			int ccsid = 0;						// code page of the body (DataArea::fileCcsid)
			int intFormat = NUMERIC_PC;			// integer encoding of the body (DataArea::fileIntFormat)
			int charFormat = CHAR_ASCII;		// CHAR_* the body is shown as
			int numericFormat = NUMERIC_PC;		// COBOL view: binary integers
			int pdNumericFormat = NUMERIC_PC;	// COBOL view: packed decimal
			bool crlf = false;					// character view: start a line after each CR LF
			bool edi = false;					// character view: a line per EDI segment
			bool bom = false;					// character and both views: honour a byte order mark
			bool indent = false;				// character view: indent XML; COBOL view: indent levels
			bool checkData = false;				// COBOL view: check the field contents
			DisplayCodepages display = { rfh_get_acp(), 437 };	// Windows GetACP() and GetOEMCP()
			std::string conversionTableDir;		// MQ's conversion tables; empty for none
			std::string copybookPath;			// COBOL view: the copy book file

			// Copies the five formats and nothing else.
			void apply(const DataFormats & f)
			{
				ccsid = f.ccsid;
				intFormat = f.intFormat;
				charFormat = f.charFormat;
				numericFormat = f.numericFormat;
				pdNumericFormat = f.pdNumericFormat;
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
	}
}
