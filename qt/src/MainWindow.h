// MainWindow.h - the application window: a tab widget in the original RFHUtil tab order.
// Only the Main, Data and MQMD tabs work in Phase 1; the others are shown disabled.
#pragma once

#include <QMainWindow>
#include <QStringList>

class QTabWidget;

namespace rfhqt
{
	class MainWindow : public QMainWindow
	{
		Q_OBJECT

	public:
		// The tabs, in the order of the Windows program (rfhutilView.cpp).
		enum Tab
		{
			TabMain, TabData, TabMqmd, TabPs, TabUsrProp, TabRfh, TabPubSub, TabPscr,
			TabJms, TabUsr, TabOther, TabCics, TabIms, TabDlq, TabConn,
			TabCount
		};

		explicit MainWindow(QWidget * parent = nullptr);

		QTabWidget * tabs() const { return tabWidget; }

		// The tab captions, exactly as Windows shows them (some are lower case).
		static const QStringList & tabCaptions();

		// True for the tabs that work in Phase 1.
		static bool isPhase1Tab(int index);

	private:
		QTabWidget * tabWidget;
	};
}
