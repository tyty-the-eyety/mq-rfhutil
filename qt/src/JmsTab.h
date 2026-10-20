// JmsTab.h - the jms sub-folder tab of an RFH2 header.
// Threading: GUI thread only.
#pragma once

#include <QWidget>
#include <memory>
#include "rfh_parsed_headers.h"

namespace Ui { class JmsTab; }

namespace rfhqt
{
	class AppDocument;

	class JmsTab : public QWidget
	{
		Q_OBJECT
	public:
		explicit JmsTab(AppDocument & document, QWidget * parent = nullptr);
		~JmsTab() override;

		void setFields(const rfh::Rfh2JmsFields & f, bool included);
		rfh::Rfh2JmsFields fields() const;
		bool isIncluded() const;

	signals:
		void fieldsEdited();

	private:
		void wire();
		void editedByUser();

		AppDocument & doc;
		std::unique_ptr<Ui::JmsTab> ui;
	};
}
