#include <QApplication>

#include "MainWindow.h"
#include "ProcessSetup.h"

int main(int argc, char ** argv)
{
	QApplication app(argc, argv);
	app.setApplicationVersion(QStringLiteral(RFHUTIL_VERSION));
	rfhqt::useCLocaleForNumbersAndCharacters();

	rfhqt::MainWindow window;
	window.show();
	return app.exec();
}
