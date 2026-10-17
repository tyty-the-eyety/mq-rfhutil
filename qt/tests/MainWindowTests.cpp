// MainWindowTests.cpp - the application window and the process setup.

#include <QtTest>

#include <locale.h>

#include "MainWindow.h"
#include "ProcessSetup.h"

using rfhqt::MainWindow;

class MainWindowTests : public QObject
{
	Q_OBJECT

private slots:
	void tabsFollowTheWindowsOrderAndCaptions()
	{
		MainWindow window;
		const QStringList expected = {
			"Main", "Data", "MQMD", "PS", "Usr Prop", "RFH", "PubSub", "pscr",
			"jms", "usr", "other", "CICS", "IMS", "DLQ", "Conn" };
		QCOMPARE(window.tabs()->count(), expected.size());
		for (int i = 0; i < expected.size(); i++)
		{
			QCOMPARE(window.tabs()->tabText(i), expected[i]);
		}
	}

	void onlyMainDataAndMqmdAreEnabled()
	{
		MainWindow window;
		for (int i = 0; i < window.tabs()->count(); i++)
		{
			const bool phase1 = (i == MainWindow::TabMain) || (i == MainWindow::TabData) || (i == MainWindow::TabMqmd);
			QCOMPARE(window.tabs()->isTabEnabled(i), phase1);
		}
	}

	void disabledTabsSayTheyAreNotPortedYet()
	{
		MainWindow window;
		QCOMPARE(window.tabs()->tabToolTip(MainWindow::TabRfh), QString("Not yet ported"));
		QCOMPARE(window.tabs()->tabToolTip(MainWindow::TabConn), QString("Not yet ported"));
		QVERIFY(window.tabs()->tabToolTip(MainWindow::TabMain).isEmpty());
	}

	void numbersAndCharactersUseTheCLocaleAfterSetup()
	{
		setlocale(LC_NUMERIC, "");
		rfhqt::useCLocaleForNumbersAndCharacters();
		QCOMPARE(QString(setlocale(LC_NUMERIC, nullptr)), QString("C"));
		QCOMPARE(QString(setlocale(LC_CTYPE, nullptr)), QString("C"));
	}
};

QTEST_MAIN(MainWindowTests)
#include "MainWindowTests.moc"
