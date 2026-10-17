#include <QApplication>

#include "MainWindow.h"
#include "ProcessSetup.h"

int main(int argc, char ** argv)
{
	QApplication app(argc, argv);
	rfhqt::useCLocaleForNumbersAndCharacters();

	rfhqt::MainWindow window;
	window.show();
	return app.exec();
}
