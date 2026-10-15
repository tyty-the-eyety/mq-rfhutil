// uisubs.cpp - MFC-only helpers moved out of comsubs.cpp so comsubs stays portable.

#include "stdafx.h"
#include "uisubs.h"

void processBackspace(CWnd *wnd)

{
 	int		nStart=0;
	int		nEnd=0;
	DWORD	style;
	CString value;

	// check if the window is enabled and not read only
	if (!wnd->IsWindowEnabled())
	{
		return;
	}

	// check if the edit box is read only
	style = wnd->GetStyle();
	if ((style & ES_READONLY) > 0)
	{
		return;
	}

	// process backspace key
	// start by getting the current value
	wnd->GetWindowText(value);

	// get the current location of the caret
	DWORD wSel = ((CEdit *)wnd)->GetSel();
	nStart = HIWORD(wSel);
	nEnd = LOWORD(wSel);

	// make sure there is something in the edit control to start with
	// and that the cursor is not at offset 0
	if ((value.GetLength() > 0) && (nEnd > 0))
	{
		// check if any characters are selected
		if (nStart == nEnd)
		{
			// nothing selected so select the character to the left of the cursor
			nStart = nEnd - 1;

			// check if it is a CR or LF character
			if ((value[nStart] != '\n') && (value[nStart] != '\r'))
			{
				// delete the character
				value.Delete(nStart, 1);
			}
			else
			{
				// delete the CR or LF character
				value.Delete(nStart, 1);

				// check for a second character in front of this one
				if ((nStart > 0) && (((value[nStart-1] == '\n') || (value[nStart-1] == '\r'))))
				{
					// delete the CR or LF character
					value.Delete(nStart-1, 1);
				}
			}
		}
		else
		{
			// just delete the selected characters
			value.Delete(nStart, nEnd - nStart);
		}

		// update the text in the edit control
		wnd->SetWindowText((LPCTSTR)value);

		// point the caret at the correct location
		((CEdit *)wnd)->SetSel(nStart, nStart, TRUE);
	}
}
