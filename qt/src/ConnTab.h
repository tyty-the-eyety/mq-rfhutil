// ConnTab.h - the Conn tab: connection settings stored in AppSettings.
// Threading: GUI thread only.
#pragma once

#include <QWidget>
#include <memory>
#include "rfh_mq_session.h"

namespace Ui { class ConnTab; }

namespace rfhqt
{
	class AppSettings;

	class ConnTab : public QWidget
	{
		Q_OBJECT
	public:
		explicit ConnTab(AppSettings & settings, QWidget * parent = nullptr);
		~ConnTab() override;

		// Show the connection options from settings.
		void load();

	signals:
		void settingsApplied();

	private:
		void wire();
		void apply();
		void reset();

		AppSettings & appSettings;
		std::unique_ptr<Ui::ConnTab> ui;
	};
}
