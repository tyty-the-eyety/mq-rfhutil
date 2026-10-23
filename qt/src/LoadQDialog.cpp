#include "LoadQDialog.h"

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

#include "comsubs.h"    // HexToAscii, checkIfHex

namespace rfhqt
{
    static constexpr char kDefaultDelim[]     = "#@#@#";
    static constexpr char kDefaultPropDelim[] = "$%$%$";

    LoadQDialog::LoadQDialog(const QString & qName, QWidget * parent)
        : QDialog(parent), m_qName(qName)
    {
        setWindowTitle(QStringLiteral("Load messages from file(s) to Queue %1").arg(qName));
        setMinimumWidth(520);

        auto * mainLayout = new QVBoxLayout(this);

        // ---- file name row --------------------------------------------------
        auto * fileRow = new QHBoxLayout;
        m_fileName = new QLineEdit;
        m_fileName->setPlaceholderText(QStringLiteral("Input file path"));
        m_fileName->setMaxLength(512);
        m_browseButton = new QPushButton(QStringLiteral("Browse…"));
        connect(m_browseButton, &QPushButton::clicked, this, &LoadQDialog::browse);
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
        connect(m_oneFile,    &QRadioButton::toggled, this, &LoadQDialog::updateDelimiterEnabled);
        connect(m_filePerMsg, &QRadioButton::toggled, this, &LoadQDialog::updateDelimiterEnabled);
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

        m_removeMqmd  = new QCheckBox;
        m_removeHdrs  = new QCheckBox;
        m_useSetAll   = new QCheckBox;
        m_newMsgId    = new QCheckBox;
        m_persistent  = new QCheckBox;
        m_writeOnce   = new QCheckBox;
        m_singleFile  = new QCheckBox;
        m_ignoreProps = new QCheckBox;

        // defaults matching CLoadQ
        m_useSetAll->setChecked(true);
        m_newMsgId->setChecked(true);
        m_writeOnce->setChecked(true);

        m_maxCount  = new QLineEdit;
        m_maxCount->setPlaceholderText(QStringLiteral("0 = all"));
        m_maxCount->setMaxLength(8);
        m_maxCount->setFixedWidth(80);

        m_startMsg  = new QLineEdit;
        m_startMsg->setPlaceholderText(QStringLiteral("0"));
        m_startMsg->setMaxLength(6);
        m_startMsg->setFixedWidth(80);

        m_batchSize = new QLineEdit(QStringLiteral("1"));
        m_batchSize->setMaxLength(6);
        m_batchSize->setFixedWidth(80);

        m_waitTime = new QLineEdit;
        m_waitTime->setPlaceholderText(QStringLiteral("0 ms"));
        m_waitTime->setMaxLength(8);
        m_waitTime->setFixedWidth(80);

        m_formatCombo = new QComboBox;
        for (const char * f : { "(default)", "MQFMT_NONE", "MQFMT_STRING", "MQFMT_XMIT_Q_HEADER",
                                 "MQFMT_DEAD_LETTER_HEADER", "MQFMT_RF_HEADER_2",
                                 "MQFMT_IMS", "MQFMT_CICS" })
            m_formatCombo->addItem(QString::fromLatin1(f));

        m_ccsid = new QLineEdit;
        m_ccsid->setPlaceholderText(QStringLiteral("0 = from file"));
        m_ccsid->setMaxLength(6);
        m_ccsid->setFixedWidth(80);

        m_encoding = new QLineEdit;
        m_encoding->setPlaceholderText(QStringLiteral("0 = from file"));
        m_encoding->setMaxLength(6);
        m_encoding->setFixedWidth(80);

        optForm->addRow(QStringLiteral("Remove MQMD (use defaults):"),   m_removeMqmd);
        optForm->addRow(QStringLiteral("Remove MQ headers:"),            m_removeHdrs);
        optForm->addRow(QStringLiteral("Set All Context (MQPMO):"),      m_useSetAll);
        optForm->addRow(QStringLiteral("Generate new MsgId:"),           m_newMsgId);
        optForm->addRow(QStringLiteral("Force persistent:"),             m_persistent);
        optForm->addRow(QStringLiteral("Stop after first file:"),        m_writeOnce);
        optForm->addRow(QStringLiteral("Single message (no delimiter):"),m_singleFile);
        optForm->addRow(QStringLiteral("Ignore properties:"),            m_ignoreProps);
        optForm->addRow(QStringLiteral("Max messages:"),                 m_maxCount);
        optForm->addRow(QStringLiteral("Start file #:"),                 m_startMsg);
        optForm->addRow(QStringLiteral("Batch size (0 = no syncpoint):"),m_batchSize);
        optForm->addRow(QStringLiteral("Wait between batches (ms):"),    m_waitTime);
        optForm->addRow(QStringLiteral("Default format:"),               m_formatCombo);
        optForm->addRow(QStringLiteral("Default CCSID:"),                m_ccsid);
        optForm->addRow(QStringLiteral("Default encoding:"),             m_encoding);
        mainLayout->addWidget(optBox);

        // ---- error label + buttons ------------------------------------------
        m_errorLabel = new QLabel;
        m_errorLabel->setStyleSheet(QStringLiteral("color: red;"));
        mainLayout->addWidget(m_errorLabel);

        auto * buttons = new QDialogButtonBox;
        m_okButton     = buttons->addButton(QDialogButtonBox::Ok);
        m_cancelButton = buttons->addButton(QDialogButtonBox::Cancel);
        connect(m_okButton,     &QPushButton::clicked, this, &LoadQDialog::validate);
        connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
        mainLayout->addWidget(buttons);

        updateDelimiterEnabled();
    }

    LoadQDialog::~LoadQDialog() = default;

    void LoadQDialog::browse()
    {
        QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Load messages from file"), m_fileName->text());
        if (!path.isEmpty())
            m_fileName->setText(path);
    }

    void LoadQDialog::updateDelimiterEnabled()
    {
        bool oneFile = m_oneFile->isChecked();
        m_delimChar->setEnabled(oneFile);
        m_delimHex->setEnabled(oneFile);
        m_delimiter->setEnabled(oneFile);
        m_propDelim->setEnabled(oneFile);
    }

    void LoadQDialog::validate()
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

    rfh::LoadQueueOptions LoadQDialog::values() const
    {
        rfh::LoadQueueOptions opts;
        opts.fileName = m_fileName->text().toStdString();
        opts.qName    = m_qName.toStdString();

        opts.filesType    = m_filePerMsg->isChecked() ? rfh::SAVE_FILE_DIFF_FILES : rfh::SAVE_FILE_SAME_FILE;
        opts.removeMQMD   = m_removeMqmd->isChecked();
        opts.removeHdrs   = m_removeHdrs->isChecked();
        opts.useSetAll    = m_useSetAll->isChecked();
        opts.newMsgId     = m_newMsgId->isChecked();
        opts.persistent   = m_persistent->isChecked();
        opts.writeOnce    = m_writeOnce->isChecked();
        opts.singleFile   = m_singleFile->isChecked();
        opts.ignoreProps  = m_ignoreProps->isChecked();

        opts.maxCount        = m_maxCount->text().isEmpty() ? 0 : m_maxCount->text().toInt();
        opts.startFileNumber = m_startMsg->text().isEmpty() ? 0 : m_startMsg->text().toInt();
        opts.batchSize       = m_batchSize->text().isEmpty() ? 0 : m_batchSize->text().toInt();
        opts.waitTime        = m_waitTime->text().isEmpty()  ? 0 : m_waitTime->text().toInt();
        opts.defaultCcsid    = m_ccsid->text().isEmpty()     ? 0 : m_ccsid->text().toInt();
        opts.defaultEncoding = m_encoding->text().isEmpty()  ? 0 : m_encoding->text().toInt();

        // default format — map combo item text back to MQ format string
        QString fmtText = m_formatCombo->currentText();
        if (fmtText != QLatin1String("(default)"))
        {
            QByteArray fmtLatin = fmtText.toLatin1();
            // strip "MQFMT_" prefix and map to the 8-byte space-padded MQ constant
            static const struct { const char * label; const char * fmt; } kFmtMap[] = {
                { "MQFMT_NONE",             MQFMT_NONE              },
                { "MQFMT_STRING",           MQFMT_STRING            },
                { "MQFMT_XMIT_Q_HEADER",    MQFMT_XMIT_Q_HEADER    },
                { "MQFMT_DEAD_LETTER_HEADER",MQFMT_DEAD_LETTER_HEADER },
                { "MQFMT_RF_HEADER_2",      MQFMT_RF_HEADER_2       },
                { "MQFMT_IMS",              MQFMT_IMS               },
                { "MQFMT_CICS",             MQFMT_CICS              },
            };
            const char * raw = nullptr;
            for (const auto & m : kFmtMap)
                if (fmtText.toLatin1() == m.label) { raw = m.fmt; break; }
            if (raw)
                memcpy(opts.format, raw, MQ_FORMAT_LENGTH);
        }

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
            size_t copyLen = (size_t)delimLen < sizeof(opts.delimiter)
                           ? (size_t)delimLen : sizeof(opts.delimiter) - 1;
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
            size_t copyLen = (size_t)propLen < sizeof(opts.propDelim)
                           ? (size_t)propLen : sizeof(opts.propDelim) - 1;
            memcpy(opts.propDelim, propRaw.constData(), copyLen);
        }
        opts.propDelimLen = propLen;

        return opts;
    }
}
