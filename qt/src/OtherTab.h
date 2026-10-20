// OtherTab.h - the "other" tab showing unrecognized RFH2 folders (read-only).
// Threading: GUI thread only.
#pragma once

#include <QWidget>
#include <memory>
#include <string>

namespace Ui { class OtherTab; }

namespace rfhqt
{
	class AppDocument;

	class OtherTab : public QWidget
	{
		Q_OBJECT
	public:
		explicit OtherTab(AppDocument & document, QWidget * parent = nullptr);
		~OtherTab() override;

		void setXml(const std::string & xml);

	private:
		AppDocument & doc;
		std::unique_ptr<Ui::OtherTab> ui;
	};
}
