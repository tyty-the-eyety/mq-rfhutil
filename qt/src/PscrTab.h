// PscrTab.h - the pscr (pub/sub create-response) folder tab of an RFH2 header.
// Threading: GUI thread only.
#pragma once

#include <QWidget>
#include <memory>
#include <string>

namespace Ui { class PscrTab; }

namespace rfhqt
{
	class AppDocument;

	class PscrTab : public QWidget
	{
		Q_OBJECT
	public:
		explicit PscrTab(AppDocument & document, QWidget * parent = nullptr);
		~PscrTab() override;

		void setFields(const std::string & xml, bool included);
		std::string xml() const;
		bool isIncluded() const;

	signals:
		void fieldsEdited();

	private:
		void wire();

		AppDocument & doc;
		std::unique_ptr<Ui::PscrTab> ui;
	};
}
