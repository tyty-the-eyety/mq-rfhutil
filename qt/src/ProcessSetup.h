// ProcessSetup.h - process-wide settings that the Qt application makes after creating
// its QApplication.
#pragma once

namespace rfhqt
{
	// Qt's QApplication sets the C library locale from the environment. The core and IBM's
	// shared sources format numbers with printf and parse text with the C library, and they
	// expect the "C" locale for numbers and character classes: a decimal comma would change
	// every number they print. Call this once, after the QApplication exists.
	void useCLocaleForNumbersAndCharacters();
}
