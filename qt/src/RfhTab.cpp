#include "RfhTab.h"
#include "ui_RfhTab.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRadioButton>

#include "rfhconst.h"

namespace rfhqt
{
	// Encoding combo helpers shared by all header tabs.
	// Numeric encoding: index 0 → NUMERIC_PC (0), index 1 → NUMERIC_HOST (1).
	// Float encoding: index 0 → FLOAT_PC (0), 1 → FLOAT_UNIX, 2 → FLOAT_390, 3 → FLOAT_TNS.
	static void populateNumericCombo(QComboBox * combo)
	{
		combo->addItem(QStringLiteral("PC (reversed)"),  NUMERIC_PC);
		combo->addItem(QStringLiteral("Host (normal)"),  NUMERIC_HOST);
	}

	static void populateFloatCombo(QComboBox * combo)
	{
		combo->addItem(QStringLiteral("PC (IEEE normal)"),    FLOAT_PC);
		combo->addItem(QStringLiteral("Unix (IEEE reversed)"), FLOAT_UNIX);
		combo->addItem(QStringLiteral("390 (S390)"),          FLOAT_390);
		combo->addItem(QStringLiteral("TNS"),                 FLOAT_TNS);
	}

	static void setNumericCombo(QComboBox * combo, int value)
	{
		const int idx = combo->findData(value);
		if (idx >= 0)
			combo->setCurrentIndex(idx);
	}

	static int numericComboValue(const QComboBox * combo)
	{
		return combo->currentData().toInt();
	}

	RfhTab::RfhTab(AppDocument & document, QWidget * parent)
		: QWidget(parent), doc(document), ui(new Ui::RfhTab),
		  versionGroup(new QButtonGroup(this))
	{
		ui->setupUi(this);
		populateCombos();
		versionGroup->addButton(ui->v1Radio, 1);
		versionGroup->addButton(ui->v2Radio, 2);
		ui->v2Radio->setChecked(true);
		updateV1V2Enables();
		wire();
	}

	RfhTab::~RfhTab() = default;

	void RfhTab::populateCombos()
	{
		populateNumericCombo(ui->encodingCombo);
		populateNumericCombo(ui->pdEncodingCombo);
		populateFloatCombo(ui->floatEncodingCombo);
	}

	void RfhTab::wire()
	{
		connect(versionGroup, &QButtonGroup::idToggled, this, [this](int, bool checked) {
			if (checked) { updateV1V2Enables(); editedByUser(); }
		});
		connect(ui->includedCheck, &QCheckBox::toggled, this, &RfhTab::editedByUser);
		const auto editSlot = [this]() { editedByUser(); };
		connect(ui->ccsidEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->encodingCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->pdEncodingCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->floatEncodingCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->formatEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->flagsEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->nvCcsidEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->appGroupEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->msgTypeEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->inclMcdCheck, &QCheckBox::toggled, this, &RfhTab::editedByUser);
		connect(ui->inclJmsCheck, &QCheckBox::toggled, this, &RfhTab::editedByUser);
		connect(ui->inclUsrCheck, &QCheckBox::toggled, this, &RfhTab::editedByUser);
		connect(ui->inclPsCheck, &QCheckBox::toggled, this, &RfhTab::editedByUser);
		connect(ui->inclPscrCheck, &QCheckBox::toggled, this, &RfhTab::editedByUser);
		connect(ui->inclOtherCheck, &QCheckBox::toggled, this, &RfhTab::editedByUser);
		connect(ui->v1InclPubsubCheck, &QCheckBox::toggled, this, &RfhTab::editedByUser);
		connect(ui->v1InclPscrCheck, &QCheckBox::toggled, this, &RfhTab::editedByUser);
	}

	void RfhTab::updateV1V2Enables()
	{
		const bool v2 = ui->v2Radio->isChecked();
		ui->nvCcsidEdit->setEnabled(v2);
		ui->inclMcdCheck->setVisible(v2);
		ui->inclJmsCheck->setVisible(v2);
		ui->inclUsrCheck->setVisible(v2);
		ui->inclPsCheck->setVisible(v2);
		ui->inclPscrCheck->setVisible(v2);
		ui->inclOtherCheck->setVisible(v2);
		ui->v1InclPubsubCheck->setVisible(!v2);
		ui->v1InclPscrCheck->setVisible(!v2);
		ui->appGroupEdit->setEnabled(!v2);
		ui->msgTypeEdit->setEnabled(!v2);
	}

	void RfhTab::editedByUser()
	{
		emit fieldsEdited();
	}

	bool RfhTab::isV2() const
	{
		return ui->v2Radio->isChecked();
	}

	void RfhTab::setRfh1(const rfh::Rfh1Fields & f)
	{
		ui->v1Radio->setChecked(true);
		ui->includedCheck->setChecked(f.included);
		ui->ccsidEdit->setText(QString::number(f.ccsid));
		setNumericCombo(ui->encodingCombo, f.encoding);
		setNumericCombo(ui->pdEncodingCombo, f.pdEncoding);
		setNumericCombo(ui->floatEncodingCombo, f.floatEncoding);
		ui->formatEdit->setText(QString::fromStdString(f.format));
		ui->flagsEdit->setText(QString::number(f.flags));
		ui->nvCcsidEdit->clear();
		ui->appGroupEdit->setText(QString::fromStdString(f.appGroup));
		ui->msgTypeEdit->setText(QString::fromStdString(f.msgType));
		ui->v1InclPubsubCheck->setChecked(f.hasPubsub);
		ui->v1InclPscrCheck->setChecked(f.hasPscr);
		updateV1V2Enables();
	}

	void RfhTab::setRfh2(const rfh::Rfh2Fields & f)
	{
		ui->v2Radio->setChecked(true);
		ui->includedCheck->setChecked(f.included);
		ui->ccsidEdit->setText(QString::number(f.ccsid));
		setNumericCombo(ui->encodingCombo, f.encoding);
		setNumericCombo(ui->pdEncodingCombo, f.pdEncoding);
		setNumericCombo(ui->floatEncodingCombo, f.floatEncoding);
		ui->formatEdit->setText(QString::fromStdString(f.format));
		ui->flagsEdit->setText(QString::number(f.flags));
		ui->nvCcsidEdit->setText(QString::number(f.nameValueCcsid));
		ui->appGroupEdit->clear();
		ui->msgTypeEdit->clear();
		ui->inclMcdCheck->setChecked(f.hasMcd);
		ui->inclJmsCheck->setChecked(f.hasJms);
		ui->inclUsrCheck->setChecked(f.hasUsr);
		ui->inclPsCheck->setChecked(f.hasPs);
		ui->inclPscrCheck->setChecked(f.hasPscr);
		ui->inclOtherCheck->setChecked(f.hasOther);
		updateV1V2Enables();
	}

	rfh::Rfh1Fields RfhTab::rfh1Fields() const
	{
		rfh::Rfh1Fields f;
		f.included = ui->includedCheck->isChecked();
		f.ccsid = ui->ccsidEdit->text().toInt();
		f.encoding = numericComboValue(ui->encodingCombo);
		f.pdEncoding = numericComboValue(ui->pdEncodingCombo);
		f.floatEncoding = numericComboValue(ui->floatEncodingCombo);
		f.format = ui->formatEdit->text().toStdString();
		f.flags = ui->flagsEdit->text().toInt();
		f.appGroup = ui->appGroupEdit->text().toStdString();
		f.msgType = ui->msgTypeEdit->text().toStdString();
		f.hasPubsub = ui->v1InclPubsubCheck->isChecked();
		f.hasPscr = ui->v1InclPscrCheck->isChecked();
		return f;
	}

	rfh::Rfh2Fields RfhTab::rfh2Fields() const
	{
		rfh::Rfh2Fields f;
		f.included = ui->includedCheck->isChecked();
		f.ccsid = ui->ccsidEdit->text().toInt();
		f.encoding = numericComboValue(ui->encodingCombo);
		f.pdEncoding = numericComboValue(ui->pdEncodingCombo);
		f.floatEncoding = numericComboValue(ui->floatEncodingCombo);
		f.format = ui->formatEdit->text().toStdString();
		f.flags = ui->flagsEdit->text().toInt();
		f.nameValueCcsid = ui->nvCcsidEdit->text().isEmpty() ? 1208 : ui->nvCcsidEdit->text().toInt();
		f.hasMcd = ui->inclMcdCheck->isChecked();
		f.hasJms = ui->inclJmsCheck->isChecked();
		f.hasUsr = ui->inclUsrCheck->isChecked();
		f.hasPs = ui->inclPsCheck->isChecked();
		f.hasPscr = ui->inclPscrCheck->isChecked();
		f.hasOther = ui->inclOtherCheck->isChecked();
		return f;
	}
}
