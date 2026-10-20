// UsrTab.h - the usr folder tab of an RFH2 header.
// Threading: GUI thread only.
#pragma once

#include <QWidget>
#include <memory>
#include <string>

namespace Ui { class UsrTab; }

namespace rfhqt
{
	class AppDocument;

	class UsrTab : public QWidget
	{
		Q_OBJECT
	public:
		explicit UsrTab(AppDocument & document, QWidget * parent = nullptr);
		~UsrTab() override;

		void setFields(const std::string & xml, bool included);
		std::string xml() const;
		bool isIncluded() const;

	signals:
		void fieldsEdited();

	private:
		void wire();

		AppDocument & doc;
		std::unique_ptr<Ui::UsrTab> ui;
	};
}
