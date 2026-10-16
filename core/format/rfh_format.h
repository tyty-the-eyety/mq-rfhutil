// rfh_format.h - the Data tab's views of a message body.
//
// render produces the text the Windows Data tab shows, with DataFormatter
// (RFHUtil/DataFormatter.cpp), the code the Windows Data tab uses. The text is in
// the display code page options.display.ansi, as Windows puts it in the edit
// control; a UI decodes it from that code page.
//
// Differences from Windows RFHUtil:
// - the COBOL view (Task 7; until then View::Cobol fails) reads the copy book
//   named by options.copybookPath instead of asking for one, and fails when it
//   cannot be read (Windows shows an empty view);
// - see DataFormatter.h for the differences in the views themselves.
#pragma once

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

		// Renders message.body (the headers are not shown, as on Windows) into out.
		// Each call makes a new formatter and caches nothing, so a UI that renders
		// large messages often should keep the result.
		// Returns a failure only when the view cannot be produced; problems Windows
		// reports in the status line are warnings.
		Result render(const Message & message, View view, const RenderOptions & options, std::string & out);
	}
}
