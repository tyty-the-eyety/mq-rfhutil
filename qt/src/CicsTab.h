// CicsTab.h - the CICS tab (Windows IDD_CICS, class CICSPAGE, CICSPAGE.cpp).
// Threading: GUI thread only.
#pragma once

#include <QWidget>
#include <memory>
#include "rfh_parsed_headers.h"

namespace Ui { class CicsTab; }

namespace rfhqt
{
	class AppDocument;

	class CicsTab : public QWidget
	{
		Q_OBJECT
	public:
		explicit CicsTab(AppDocument & document, QWidget * parent = nullptr);
		~CicsTab() override;

		void setFields(const rfh::CicsFields & f);
		rfh::CicsFields fields() const;

	signals:
		void fieldsEdited();

	private:
		void wire();
		void populateCombos();
		void editedByUser();

		AppDocument & doc;
		std::unique_ptr<Ui::CicsTab> ui;
	};
}
