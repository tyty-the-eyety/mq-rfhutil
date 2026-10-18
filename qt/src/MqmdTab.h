// MqmdTab.h - the MQMD tab (Windows IDD_MQMD, class MQMDPAGE, MQMDPAGE.cpp; layout in rfhutil.rc).
//
// The tab only translates between its widgets and rfh::MqmdFields. It owns no MQ logic: the put and get rules are
// mqmd_fields::toPutMqmd / fromGetMqmd, which Operations applies, and the Message Id, Correl Id and Group Id texts are
// core/message/rfh_id_display. What the tab adds is the widget behaviour the core cannot hold: the key filter of the
// id boxes (IdEdit), the edit-time application of an id box to its bytes, the display mode, and the enable rules.
//
//   * Document. AppDocument::mqmd() is the tab's state. A change the user makes is put into the document at once
//     (setMqmd, then fieldsEdited()); a change the document announces (a get, Open File, Clear) is shown without being
//     reported as an edit. The tab does not touch message().mqmd.
//   * Ids. The bytes live in a copy of the fields. A box is applied to its bytes with id_display::applyEdit: the Correl
//     Id and Group Id boxes on each change (OnUpdateCorrelId, OnUpdateGroupId), the Message Id box too (Windows does it
//     when the box loses focus, MQMDPAGE.cpp:3920, but Operations reads the document and never asks the tab, so the
//     document must never be behind the box), and every box when the display mode changes, when Copy Msg Id is used and when fields() is
//     asked, so pending edits are never lost. A box whose text equals what the bytes show is not applied
//     (updateIdFields, :629): untouched non-printable bytes survive. The mode a box is applied in is the mode it was
//     displayed in.
//   * Read-only rules. MQMDPAGE::OnSetActive applies them when the tab is activated; here they follow the document's
//     enables, which are current whenever the Main tab's Set Iden / Set All / Alternate User check boxes change, so the
//     tab never shows a stale state. Backout Count is always read-only; the Message Id is always editable.
//   * Text. Fields longer than their edit limit are cut by the edit when shown (as the Windows DDX does), so fields()
//     returns the cut text; setFields itself never reports an edit. A radio value no button stands for (a report
//     option outside 0-3, an unknown persistence) is kept and returned unchanged.
//   * Report and other radio groups are QButtonGroups whose ids are the core's option values (REPORT_NONE..REPORT_FULL
//     0-3, MQPERSIST_*, NUMERIC_*, FLOAT_*), so checkedId() == -1 ("none selected") round-trips as -1 and no default is
//     forced.
//
// Threading: GUI thread only.
#pragma once

#include <QString>
#include <QWidget>

#include <memory>
#include <string>

#include "rfh_id_display.h"
#include "rfh_mqmd_fields.h"

class QAbstractButton;
class QButtonGroup;
class QLineEdit;

namespace Ui
{
	class MqmdTab;
}

namespace rfhqt
{
	class AppDocument;
	class IdEdit;

	class MqmdTab : public QWidget
	{
		Q_OBJECT

	public:
		explicit MqmdTab(AppDocument & document, QWidget * parent = nullptr);
		~MqmdTab() override;

		// The widgets as MqmdFields (what Write Q needs). Stores pending id edits first.
		rfh::MqmdFields fields() const;

		// Shows the fields (after a get). Not an edit: fieldsEdited() is not emitted.
		void setFields(const rfh::MqmdFields & fields);

		// WM_SETPAGEFOCUS: the focus goes to the Format combo box.
		void focusFirst();

	signals:
		void fieldsEdited();					// the user changed a field
		void statusMessage(const QString &);	// a refused key or id edit (Windows beeps)

	private:
		void wire();
		void applyEnables();
		void editedByUser();
		void showIds();
		void showMode(rfh::id_display::Mode newMode);
		bool storeIds();									// applies every id box; true when a byte changed
		bool boxToBytes(rfh::id_display::Field field, unsigned char * id, std::string & error) const;
		bool storeId(rfh::id_display::Field field);
		unsigned char * idBytes(rfh::id_display::Field field);
		IdEdit * idEdit(rfh::id_display::Field field) const;
		void copyMsgId();
		void resetIds();

		AppDocument & doc;
		std::unique_ptr<Ui::MqmdTab> ui;
		QButtonGroup * persistGroup;
		QButtonGroup * encodingGroup;
		QButtonGroup * pdEncodingGroup;
		QButtonGroup * floatGroup;
		QButtonGroup * idDisplayGroup;
		QButtonGroup * reportGroups[4];
		mutable rfh::MqmdFields cur;		// the ids, and the values of radio groups that have no button for them
		rfh::id_display::Mode mode = rfh::id_display::Mode::Hex;
		bool loading = false;				// setFields in progress: widget signals are not edits
		bool publishing = false;			// our own setMqmd: the document's signal is not news
	};
}
