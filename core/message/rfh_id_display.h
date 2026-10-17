// rfh_id_display.h - the MQMD tab's Message Id, Correl Id and Group Id display and edit rules.
//
// Ported from MQMDPAGE (RFHUtil/MQMDPAGE.cpp): SetId 1787, setMsgId 3931, setCorrelId 3381, setGroupId 1891,
// OnUpdateCorrelId 3548, OnUpdateGroupId 704, setMaxFieldLengths 670, OnResetIds 506.
//
// Differences from the brief of Plan 3 Task 8, because the Windows source does otherwise:
// - applyEdit takes the field. The three boxes are not handled alike: only the Correl Id box pads
//   (hex with '0', other modes with blanks) and truncates; the Group Id box pads only in hex and rejects
//   text over 24 characters; the Msg Id box never pads and has no length check.
// - A non-hex digit in Hex mode is not rejected: Windows converts it as a zero nibble. Only the key filter
//   of the edit control stops it being typed (use isHexDigit for that).
// - Only text over 48 characters in Hex mode is rejected. Windows prevents it with the edit limit; its
//   conversion would write past the 24-byte id.
// - A character beyond the end of a text is read as NUL, as the NUL terminator of the Windows string class.
// - Not ported: the paste normalisation of editIDField (MQMDPAGE.cpp:3709) and the key filtering of CIdEdit,
//   which are widget behaviour.
#pragma once

#include <cmqc.h>

#include <stddef.h>

#include <string>

namespace rfh
{
	namespace id_display
	{
		// The values are those of ID_DISPLAY_ASCII, ID_DISPLAY_EBCDIC and ID_DISPLAY_HEX (DataArea.h:50-52).
		enum class Mode { Ascii = 0, Ebcdic = 1, Hex = 2 };

		enum class Field { MsgId, CorrelId, GroupId };

		size_t maxLength(Mode mode);	// 48 for Hex, 24 otherwise

		// Hex: 48 upper-case digits. Ascii: 24 bytes, those below 0x20 shown as blanks. Ebcdic: translated
		// with the Windows table, then the same rule.
		std::string toText(const MQBYTE24 id, Mode mode);

		// Applies the new text of a box. `previous` is the text it showed before the edit. Returns false and
		// sets `error`, leaving `id` unchanged, for a rejected edit; otherwise clears `error`.
		bool applyEdit(Field field, MQBYTE24 id, const std::string & typed, const std::string & previous, Mode mode, std::string & error);

		bool isHexDigit(char c);		// 0-9, A-F, a-f
		bool isZero(const MQBYTE24 id);	// all 24 bytes zero
		void reset(MQBYTE24 id);		// Reset Ids: binary zeros
	}
}
