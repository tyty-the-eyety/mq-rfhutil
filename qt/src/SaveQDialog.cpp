#include "SaveQDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <cstring>
#include <string>

#include "comsubs.h"    // HexToAscii, checkIfHex

namespace rfhqt
{
    static constexpr char kDefaultDelim[]     = "#@#@#";
    static constexpr char kDefaultPropDelim[] = "$%$%$";

    SaveQDialog::SaveQDialog(const QString & qName, QWidget * parent)
        : QDialog(parent), m_qName(qName)
    {
        setWindowTitle(QStringLiteral("Save messages from Queue %1 to file(s)").arg(qName));
        setMinimumWidth(520);

        auto * mainLayout = new QVBoxLayout(this);

        // ---- file name row --------------------------------------------------
        auto * fileRow = new QHBoxLayout;
        m_fileName = new QLineEdit;
        m_fileName->setPlaceholderText(QStringLiteral("Output file path"));
        m_fileName->setMaxLength(512);
        m_browseButton = new QPushButton(QStringLiteral("Browse…"));
        connect(m_browseButton, &QPushButton::clicked, this, &SaveQDialog::browse);
        fileRow->addWidget(new QLabel(QStringLiteral("File:")));
        fileRow->addWidget(m_fileName, 1);
        fileRow->addWidget(m_browseButton);
        mainLayout->addLayout(fileRow);

        // ---- file mode ------------------------------------------------------
        auto * modeBox  = new QGroupBox(QStringLiteral("File mode"));
        auto * modeVBox = new QVBoxLayout(modeBox);
        m_oneFile    = new QRadioButton(QStringLiteral("One file (messages separated by delimiter)"));
        m_filePerMsg = new QRadioButton(QStringLiteral("One file per message"));
        m_oneFile->setChecked(true);
        modeVBox->addWidget(m_oneFile);
        modeVBox->addWidget(m_filePerMsg);
        connect(m_oneFile,    &QRadioButton::toggled, this, &SaveQDialog::updateDelimiterEnabled);
        connect(m_filePerMsg, &QRadioButton::toggled, this, &SaveQDialog::updateDelimiterEnabled);
        mainLayout->addWidget(modeBox);

        // ---- delimiter ------------------------------------------------------
        auto * delimBox  = new QGroupBox(QStringLiteral("Delimiter (single-file mode)"));
        auto * delimForm = new QFormLayout(delimBox);
        m_delimChar  = new QRadioButton(QStringLiteral("Character string"));
        m_delimHex   = new QRadioButton(QStringLiteral("Hex string"));
        m_delimChar->setChecked(true);
        auto * delimTypeRow = new QHBoxLayout;
        delimTypeRow->addWidget(m_delimChar);
        delimTypeRow->addWidget(m_delimHex);
        delimTypeRow->addStretch();
        m_delimiter  = new QLineEdit(QString::fromLatin1(kDefaultDelim));
        m_delimiter->setMaxLength(16);
        m_propDelim  = new QLineEdit(QString::fromLatin1(kDefaultPropDelim));
        m_propDelim->setMaxLength(16);
        delimForm->addRow(QStringLiteral("Type:"),                 delimTypeRow);
        delimForm->addRow(QStringLiteral("Delimiter:"),            m_delimiter);
        delimForm->addRow(QStringLiteral("Properties delimiter:"), m_propDelim);
        mainLayout->addWidget(delimBox);

        // ---- options --------------------------------------------------------
        auto * optBox  = new QGroupBox(QStringLiteral("Options"));
        auto * optForm = new QFormLayout(optBox);

        m_inclMqmd   = new QCheckBox;
        m_inclHeaders= new QCheckBox;
        m_appendFile = new QCheckBox;
        m_inclMqmd->setChecked(true);
        m_inclHeaders->setChecked(true);

        auto * removeRow = new QHBoxLayout;
        m_browseOnly = new QRadioButton(QStringLiteral("Browse (keep)"));
        m_removeYes  = new QRadioButton(QStringLiteral("Get (remove)"));
        m_browseOnly->setChecked(true);
        removeRow->addWidget(m_browseOnly);
        removeRow->addWidget(m_removeYes);
        removeRow->addStretch();

        m_startCount = new QLineEdit;
        m_startCount->setPlaceholderText(QStringLiteral("1"));
        m_startCount->setMaxLength(6);
        m_startCount->setFixedWidth(80);
        m_endCount   = new QLineEdit;
        m_endCount->setPlaceholderText(QStringLiteral("0 = all"));
        m_endCount->setMaxLength(6);
        m_endCount->setFixedWidth(80);

        m_maxFileSize = new QComboBox;
        for (const char * s : { "128M", "256M", "512M", "1G", "1.5G", "2G" })
            m_maxFileSize->addItem(QString::fromLatin1(s));
        m_maxFileSize->setCurrentText(QStringLiteral("2G"));

        optForm->addRow(QStringLiteral("Include MQMD:"),    m_inclMqmd);
        optForm->addRow(QStringLiteral("Include headers:"), m_inclHeaders);
        optForm->addRow(QStringLiteral("Messages:"),        removeRow);
        optForm->addRow(QStringLiteral("Append to file:"),  m_appendFile);
        optForm->addRow(QStringLiteral("Start message:"),   m_startCount);
        optForm->addRow(QStringLiteral("End message:"),     m_endCount);
        optForm->addRow(QStringLiteral("Max file size:"),   m_maxFileSize);
        mainLayout->addWidget(optBox);

        // ---- error label + buttons ------------------------------------------
        m_errorLabel = new QLabel;
        m_errorLabel->setStyleSheet(QStringLiteral("color: red;"));
        mainLayout->addWidget(m_errorLabel);

        auto * buttons = new QDialogButtonBox;
        m_okButton     = buttons->addButton(QDialogButtonBox::Ok);
        m_cancelButton = buttons->addButton(QDialogButtonBox::Cancel);
        connect(m_okButton,     &QPushButton::clicked, this, &SaveQDialog::validate);
        connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
        mainLayout->addWidget(buttons);

        updateDelimiterEnabled();
    }

    SaveQDialog::~SaveQDialog() = default;

    void SaveQDialog::browse()
    {
        QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save messages to file"), m_fileName->text());
        if (!path.isEmpty())
            m_fileName->setText(path);
    }

    void SaveQDialog::updateDelimiterEnabled()
    {
        bool oneFile = m_oneFile->isChecked();
        m_delimChar->setEnabled(oneFile);
        m_delimHex->setEnabled(oneFile);
        m_delimiter->setEnabled(oneFile);
        m_propDelim->setEnabled(oneFile);
    }

    void SaveQDialog::validate()
    {
        m_errorLabel->clear();

        if (m_fileName->text().trimmed().isEmpty())
        {
            m_errorLabel->setText(QStringLiteral("File name is required"));
            return;
        }

        if (m_oneFile->isChecked() && m_delimHex->isChecked())
        {
            QString delim = m_delimiter->text();
            if (delim.isEmpty() || (delim.length() & 1) ||
                !checkIfHex(delim.toLatin1().constData(), delim.length()))
            {
                m_errorLabel->setText(QStringLiteral("Invalid hex delimiter"));
                return;
            }
            QString pdel = m_propDelim->text();
            if (pdel.isEmpty() || (pdel.length() & 1) ||
                !checkIfHex(pdel.toLatin1().constData(), pdel.length()))
            {
                m_errorLabel->setText(QStringLiteral("Invalid hex properties delimiter"));
                return;
            }
        }

        if (m_oneFile->isChecked() &&
            !m_delimiter->text().isEmpty() &&
            m_delimiter->text() == m_propDelim->text())
        {
            m_errorLabel->setText(QStringLiteral(
                "Delimiter and properties delimiter cannot be the same"));
            return;
        }

        accept();
    }

    rfh::SaveQueueOptions SaveQDialog::values() const
    {
        rfh::SaveQueueOptions opts;
        opts.fileName = m_fileName->text().toStdString();
        opts.qName    = m_qName.toStdString();

        opts.filesType   = m_filePerMsg->isChecked() ? rfh::SAVE_FILE_DIFF_FILES : rfh::SAVE_FILE_SAME_FILE;
        opts.browseMsgs  = m_browseOnly->isChecked() ? rfh::SAVE_REMOVE_MSGS_NO : rfh::SAVE_REMOVE_MSGS_YES;
        opts.includeMQMD = m_inclMqmd->isChecked();
        opts.removeHdrs  = !m_inclHeaders->isChecked();
        opts.appendFile  = m_appendFile->isChecked();

        int start = m_startCount->text().isEmpty() ? 1 : m_startCount->text().toInt();
        int end   = m_endCount->text().isEmpty()   ? 0 : m_endCount->text().toInt();
        opts.startCount = (start < 1) ? 1 : start;
        opts.endCount   = (end   < 0) ? 0 : end;

        // max file size
        QString szStr = m_maxFileSize->currentText();
        int64_t maxSz = (int64_t)2 * 1024 * 1024 * 1024 - 1;
        if (szStr == QLatin1String("128M"))      maxSz = 128LL * 1024 * 1024;
        else if (szStr == QLatin1String("256M")) maxSz = 256LL * 1024 * 1024;
        else if (szStr == QLatin1String("512M")) maxSz = 512LL * 1024 * 1024;
        else if (szStr == QLatin1String("1G"))   maxSz = 1024LL * 1024 * 1024;
        else if (szStr == QLatin1String("1.5G")) maxSz = 1536LL * 1024 * 1024;
        opts.maxFileSize = maxSz;

        // delimiter
        bool hexDelim = m_delimHex->isChecked();
        QByteArray delimRaw = m_delimiter->text().toLatin1();
        int delimLen = delimRaw.size();

        if (hexDelim && delimLen > 0)
        {
            delimLen >>= 1;
            HexToAscii(const_cast<unsigned char *>(reinterpret_cast<const unsigned char *>(delimRaw.constData())),
                       delimRaw.size(),
                       opts.delimiter);
        }
        else if (delimLen > 0)
        {
            size_t copyLen = (size_t)delimLen < sizeof(opts.delimiter) ? (size_t)delimLen : sizeof(opts.delimiter) - 1;
            memcpy(opts.delimiter, delimRaw.constData(), copyLen);
        }
        opts.delimLen = delimLen;

        QByteArray propRaw = m_propDelim->text().toLatin1();
        int propLen = propRaw.size();
        if (hexDelim && propLen > 0)
        {
            propLen >>= 1;
            HexToAscii(const_cast<unsigned char *>(reinterpret_cast<const unsigned char *>(propRaw.constData())),
                       propRaw.size(),
                       opts.propDelim);
        }
        else if (propLen > 0)
        {
            size_t copyLen = (size_t)propLen < sizeof(opts.propDelim) ? (size_t)propLen : sizeof(opts.propDelim) - 1;
            memcpy(opts.propDelim, propRaw.constData(), copyLen);
        }
        opts.propDelimLen = propLen;

        return opts;
    }
}
