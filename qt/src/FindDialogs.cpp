#include "FindDialogs.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QValidator>
#include <QVBoxLayout>

#include <string.h>

namespace rfhqt
{
	qint64 searchBytes(std::string_view data, std::string_view needle, bool down, qint64 previous)
	{
		const qint64 size = (qint64)data.size();
		const qint64 len = (qint64)needle.size();
		if (len == 0 || len > size)
		{
			return -1;
		}
		const qint64 last = size - len;		// the last position a match can start at
		const unsigned char first = (unsigned char)needle[0];
		if (down)
		{
			for (qint64 at = previous + 1; at <= last; at++)
			{
				const void * hit = memchr(data.data() + at, first, (size_t)(last - at + 1));
				if (hit == nullptr)
				{
					return -1;
				}
				at = (const char *)hit - data.data();
				if (memcmp(data.data() + at, needle.data(), (size_t)len) == 0)
				{
					return at;
				}
			}
			return -1;
		}
		qint64 at = (previous < 0) ? last : previous - 1;
		if (at > last)
		{
			at = last;
		}
		for (; at >= 0; at--)
		{
			if ((unsigned char)data[(size_t)at] == first && memcmp(data.data() + at, needle.data(), (size_t)len) == 0)
			{
				return at;
			}
		}
		return -1;
	}

	namespace
	{
		// ES_UPPERCASE for every way text gets in: typing, pasting and setText all pass through validate
		class UpperCaseValidator : public QValidator
		{
		public:
			using QValidator::QValidator;
			State validate(QString & input, int &) const override
			{
				input = input.toUpper();
				return Acceptable;
			}
		};

		QGroupBox * directionBox(QWidget * parent, QRadioButton *& down)
		{
			QGroupBox * box = new QGroupBox(QObject::tr("Direction"), parent);
			box->setObjectName(QStringLiteral("directionBox"));
			QHBoxLayout * layout = new QHBoxLayout(box);
			QRadioButton * up = new QRadioButton(QObject::tr("&Up"), box);
			up->setObjectName(QStringLiteral("upRadio"));
			down = new QRadioButton(QObject::tr("&Down"), box);
			down->setObjectName(QStringLiteral("downRadio"));
			down->setChecked(true);
			layout->addWidget(up);
			layout->addWidget(down);
			return box;
		}
	}

	// ---- Find -----------------------------------------------------------------------------------------------------

	FindDialog::FindDialog(QWidget * parent)
		: QDialog(parent)
	{
		setObjectName(QStringLiteral("findDialog"));
		setWindowTitle(tr("Find"));
		setModal(false);
		QVBoxLayout * root = new QVBoxLayout(this);
		QHBoxLayout * row = new QHBoxLayout;
		QLabel * label = new QLabel(tr("Fi&nd what:"), this);
		edit = new QLineEdit(this);
		edit->setObjectName(QStringLiteral("findEdit"));
		edit->setAccessibleName(tr("Find what"));
		label->setBuddy(edit);
		row->addWidget(label);
		row->addWidget(edit, 1);
		root->addLayout(row);
		root->addWidget(directionBox(this, down));
		QDialogButtonBox * buttons = new QDialogButtonBox(Qt::Vertical, this);
		findButton = new QPushButton(tr("&Find Next"), this);
		findButton->setObjectName(QStringLiteral("findNextButton"));
		findButton->setDefault(true);
		findButton->setEnabled(false);
		QPushButton * cancel = new QPushButton(tr("Cancel"), this);
		cancel->setObjectName(QStringLiteral("cancelButton"));
		buttons->addButton(findButton, QDialogButtonBox::ActionRole);
		buttons->addButton(cancel, QDialogButtonBox::RejectRole);
		root->addWidget(buttons);
		connect(edit, &QLineEdit::textChanged, this, [this](const QString & t) { findButton->setEnabled(!t.isEmpty()); });
		connect(findButton, &QPushButton::clicked, this, [this]() { emit findRequested(); });
		connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
	}

	QString FindDialog::text() const
	{
		return edit->text();
	}

	bool FindDialog::searchDown() const
	{
		return down->isChecked();
	}

	// ---- Find Hex -------------------------------------------------------------------------------------------------

	HexFindDialog::HexFindDialog(QWidget * parent)
		: QDialog(parent)
	{
		setObjectName(QStringLiteral("hexFindDialog"));
		setWindowTitle(tr("Hex Finder"));
		setModal(false);
		QVBoxLayout * root = new QVBoxLayout(this);
		QHBoxLayout * row = new QHBoxLayout;
		QLabel * label = new QLabel(tr("&Hex value:"), this);
		edit = new QLineEdit(this);
		edit->setObjectName(QStringLiteral("hexEdit"));
		edit->setMaxLength(MaxDigits);
		edit->setValidator(new UpperCaseValidator(edit));
		edit->setToolTip(tr("Value to search for (even number of hex digits such as A1B2C3)"));
		edit->setAccessibleName(tr("Hex value"));
		label->setBuddy(edit);
		row->addWidget(label);
		row->addWidget(edit, 1);
		root->addLayout(row);
		root->addWidget(directionBox(this, down));
		error = new QLabel(this);
		error->setObjectName(QStringLiteral("errorLabel"));
		error->setAccessibleName(tr("Error"));
		root->addWidget(error);
		QDialogButtonBox * buttons = new QDialogButtonBox(Qt::Horizontal, this);
		QPushButton * find = new QPushButton(tr("&Find"), this);
		find->setObjectName(QStringLiteral("findButton"));
		find->setDefault(true);
		QPushButton * cancel = new QPushButton(tr("Cancel"), this);
		cancel->setObjectName(QStringLiteral("cancelButton"));
		buttons->addButton(find, QDialogButtonBox::ActionRole);
		buttons->addButton(cancel, QDialogButtonBox::RejectRole);
		root->addWidget(buttons);
		connect(find, &QPushButton::clicked, this, &HexFindDialog::find);
		connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
	}

	QString HexFindDialog::check(const QString & digits)
	{
		if (digits.isEmpty())
		{
			return QStringLiteral("No data entered");
		}
		if ((digits.size() & 1) != 0)
		{
			return QStringLiteral("Odd number of characters");		// before the characters, as in hexDatavalid
		}
		for (const QChar c : digits)
		{
			if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('A') && c <= QLatin1Char('F'))))
			{
				return QStringLiteral("Invalid hex character(s)");
			}
		}
		return QString();
	}

	void HexFindDialog::find()
	{
		const QString message = check(edit->text());
		error->setText(message);
		if (!message.isEmpty())
		{
			QApplication::beep();		// Beep(500, 500)
			return;
		}
		emit findRequested();
	}

	QByteArray HexFindDialog::bytes() const
	{
		return QByteArray::fromHex(edit->text().toLatin1());
	}

	bool HexFindDialog::searchDown() const
	{
		return down->isChecked();
	}

	// ---- Go To ----------------------------------------------------------------------------------------------------

	GotoDialog::GotoDialog(qint64 dataSize, QWidget * parent)
		: QDialog(parent), size(dataSize)
	{
		setObjectName(QStringLiteral("gotoDialog"));
		setWindowTitle(tr("Go to offset"));
		setModal(true);
		QVBoxLayout * root = new QVBoxLayout(this);
		QLabel * prompt = new QLabel(tr("Enter offset (0 - %1)").arg(dataSize - 1), this);
		prompt->setObjectName(QStringLiteral("promptLabel"));
		edit = new QLineEdit(this);
		edit->setObjectName(QStringLiteral("offsetEdit"));
		edit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]*")), edit));
		edit->setAccessibleName(tr("Offset"));
		prompt->setBuddy(edit);
		error = new QLabel(this);
		error->setObjectName(QStringLiteral("errorLabel"));
		error->setAccessibleName(tr("Error"));
		root->addWidget(prompt);
		root->addWidget(edit);
		root->addWidget(error);
		QDialogButtonBox * buttons = new QDialogButtonBox(Qt::Horizontal, this);
		QPushButton * okButton = new QPushButton(tr("OK"), this);
		okButton->setObjectName(QStringLiteral("okButton"));
		okButton->setDefault(true);
		QPushButton * cancel = new QPushButton(tr("Cancel"), this);
		cancel->setObjectName(QStringLiteral("cancelButton"));
		buttons->addButton(okButton, QDialogButtonBox::AcceptRole);
		buttons->addButton(cancel, QDialogButtonBox::RejectRole);
		root->addWidget(buttons);
		connect(okButton, &QPushButton::clicked, this, &GotoDialog::ok);
		connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
	}

	std::pair<qint64, QString> GotoDialog::check(const QString & text, qint64 dataSize)
	{
		for (const QChar c : text)
		{
			if (c < QLatin1Char('0') || c > QLatin1Char('9'))
			{
				return { 0, QStringLiteral("Offset must be a number") };
			}
		}
		bool fits = true;
		const qint64 offset = text.isEmpty() ? 0 : text.toLongLong(&fits);
		if (!fits || offset < 0 || offset > dataSize - 1)
		{
			return { 0, QStringLiteral("Offset too large or < 0") };
		}
		return { offset, QString() };
	}

	void GotoDialog::ok()
	{
		const std::pair<qint64, QString> checked = check(edit->text(), size);
		error->setText(checked.second);
		if (checked.second.isEmpty())
		{
			value = checked.first;
			accept();
		}
	}
}
