#include "ProcessSetup.h"

#include <locale.h>

namespace rfhqt
{
	void useCLocaleForNumbersAndCharacters()
	{
		setlocale(LC_NUMERIC, "C");
		setlocale(LC_CTYPE, "C");
	}
}
