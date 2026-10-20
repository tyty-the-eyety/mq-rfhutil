// ImsTab.h - the IMS tab (Windows IDD_IMS, class IMSPAGE, IMSPAGE.cpp).
// Threading: GUI thread only.
#pragma once

#include <QWidget>
#include <memory>
#include "rfh_parsed_headers.h"

namespace Ui { class ImsTab; }

namespace rfhqt
{
	class AppDocument;

	class ImsTab : public QWidget
	{
		Q_OBJECT
	public:
		explicit ImsTab(AppDocument & document, QWidget * parent = nullptr);
		~ImsTab() override;

		void setFields(const rfh::ImsFields & f);
		rfh::ImsFields fields() const;

	signals:
		void fieldsEdited();

	private:
		void wire();
		void populateCombos();
		void editedByUser();

		AppDocument & doc;
		std::unique_ptr<Ui::ImsTab> ui;
	};
}
