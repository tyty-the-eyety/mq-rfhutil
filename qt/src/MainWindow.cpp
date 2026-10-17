#include "MainWindow.h"

#include <QLabel>
#include <QTabWidget>

namespace rfhqt
{
	const QStringList & MainWindow::tabCaptions()
	{
		static const QStringList captions = {
			"Main", "Data", "MQMD", "PS", "Usr Prop", "RFH", "PubSub", "pscr",
			"jms", "usr", "other", "CICS", "IMS", "DLQ", "Conn"
		};
		return captions;
	}

	bool MainWindow::isPhase1Tab(int index)
	{
		return (index == TabMain) || (index == TabData) || (index == TabMqmd);
	}

	MainWindow::MainWindow(QWidget * parent)
		: QMainWindow(parent), tabWidget(new QTabWidget(this))
	{
		setWindowTitle("RfhUtil");
		setCentralWidget(tabWidget);

		const QStringList & captions = tabCaptions();
		for (int i = 0; i < TabCount; i++)
		{
			// the real pages replace these placeholders in later tasks
			tabWidget->addTab(new QLabel(captions[i], tabWidget), captions[i]);
			if (!isPhase1Tab(i))
			{
				tabWidget->setTabEnabled(i, false);
				tabWidget->setTabToolTip(i, "Not yet ported");
			}
		}
	}
}
