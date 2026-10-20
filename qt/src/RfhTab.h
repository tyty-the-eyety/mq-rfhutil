// RfhTab.h - the RFH tab (Windows RFH/RFHPAGE, rfhutil.rc IDD_RFH).
// Shows and edits the main MQRFH (v1) or MQRFH2 (v2) header fields plus
// the include/exclude flags for each sub-folder.
// Threading: GUI thread only.
#pragma once

#include <QWidget>
#include <memory>
#include "rfh_parsed_headers.h"

class QButtonGroup;

namespace Ui { class RfhTab; }

namespace rfhqt
{
	class AppDocument;

	class RfhTab : public QWidget
	{
		Q_OBJECT
	public:
		explicit RfhTab(AppDocument & document, QWidget * parent = nullptr);
		~RfhTab() override;

		// Show parsed header fields.  Not an edit: fieldsEdited() is not emitted.
		void setRfh1(const rfh::Rfh1Fields & f);
		void setRfh2(const rfh::Rfh2Fields & f);

		// Read the tab's current state back as fields.
		rfh::Rfh1Fields rfh1Fields() const;
		rfh::Rfh2Fields rfh2Fields() const;

		// True when the v2 radio is selected.
		bool isV2() const;

	signals:
		void fieldsEdited();

	private:
		void wire();
		void populateCombos();
		void updateV1V2Enables();
		void editedByUser();

		AppDocument & doc;
		std::unique_ptr<Ui::RfhTab> ui;
		QButtonGroup * versionGroup = nullptr;
	};
}
