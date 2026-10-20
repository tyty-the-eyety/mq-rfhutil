// PubSubTab.h - the psc (pub/sub command) folder tab of an RFH2 header.
// Threading: GUI thread only.
#pragma once

#include <QWidget>
#include <memory>
#include <string>

namespace Ui { class PubSubTab; }

namespace rfhqt
{
	class AppDocument;

	class PubSubTab : public QWidget
	{
		Q_OBJECT
	public:
		explicit PubSubTab(AppDocument & document, QWidget * parent = nullptr);
		~PubSubTab() override;

		void setFields(const std::string & xml, bool included);
		std::string xml() const;
		bool isIncluded() const;

	signals:
		void fieldsEdited();

	private:
		void wire();

		AppDocument & doc;
		std::unique_ptr<Ui::PubSubTab> ui;
	};
}
