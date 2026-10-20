// DlqTab.h - the DLQ tab (Windows IDD_DLQ, class DLQPAGE, DLQPAGE.cpp).
// Threading: GUI thread only.
#pragma once

#include <QWidget>
#include <memory>
#include "rfh_parsed_headers.h"

namespace Ui { class DlqTab; }

namespace rfhqt
{
	class AppDocument;

	class DlqTab : public QWidget
	{
		Q_OBJECT
	public:
		explicit DlqTab(AppDocument & document, QWidget * parent = nullptr);
		~DlqTab() override;

		void setFields(const rfh::DlqFields & f);
		rfh::DlqFields fields() const;

	signals:
		void fieldsEdited();

	private:
		void wire();
		void populateCombos();
		void editedByUser();

		AppDocument & doc;
		std::unique_ptr<Ui::DlqTab> ui;
	};
}
