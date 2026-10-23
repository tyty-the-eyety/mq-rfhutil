#include "DisplayQDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cstring>

#include "comsubs.h"    // AsciiToHex, Rtrim

namespace rfhqt
{
    namespace
    {
        // Format MQMD PutDate (YYYYMMDD) + PutTime (HHMMSSTH) as "YYYY-MM-DD HH:MM:SS".
        QString fmtDateTime(const MQCHAR8 & d, const MQCHAR8 & t)
        {
            char date[9] = {};
            char time_[9] = {};
            memcpy(date,  d, 8);
            memcpy(time_, t, 8);
            if (date[0] == ' ' || date[0] == '\0') return QString();
            return QStringLiteral("%1-%2-%3 %4:%5:%6")
                .arg(QLatin1String(date,  4))
                .arg(QLatin1String(date  + 4, 2))
                .arg(QLatin1String(date  + 6, 2))
                .arg(QLatin1String(time_, 2))
                .arg(QLatin1String(time_ + 2, 2))
                .arg(QLatin1String(time_ + 4, 2));
        }

        QString fmtFixed(const char * src, int maxLen)
        {
            char buf[64] = {};
            int  copyLen = (maxLen < 63) ? maxLen : 63;
            memcpy(buf, src, copyLen);
            Rtrim(buf);
            return QString::fromLatin1(buf);
        }

        QString fmtId(const MQBYTE * id, int len)
        {
            // len bytes → 2*len hex chars
            std::vector<char> hex(len * 2 + 1, 0);
            AsciiToHex(reinterpret_cast<const unsigned char *>(id), len,
                       reinterpret_cast<unsigned char *>(hex.data()));
            return QString::fromLatin1(hex.data());
        }

        QString fmtGS(MQLONG flags)
        {
            char gs[3] = "  ";
            if (flags & MQMF_LAST_MSG_IN_GROUP)  gs[0] = 'L';
            else if (flags & MQMF_MSG_IN_GROUP)   gs[0] = 'G';
            if (flags & MQMF_LAST_SEGMENT)        gs[1] = 'L';
            else if (flags & MQMF_SEGMENT)         gs[1] = 'S';
            return QString::fromLatin1(gs, 2).trimmed();
        }

        QTableWidgetItem * cell(const QString & text)
        {
            auto * item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            return item;
        }
    }

    DisplayQDialog::DisplayQDialog(const QString & qName,
                                   const std::vector<DisplayQEntry> & entries,
                                   QWidget * parent)
        : QDialog(parent), m_entries(entries)
    {
        setWindowTitle(QStringLiteral("Display messages in Queue %1").arg(qName));
        resize(1100, 480);

        auto * mainLayout = new QVBoxLayout(this);

        // ---- table ---------------------------------------------------------
        static const char * kHeaders[] = {
            "Pos", "Length", "Format", "GS", "User Id",
            "Put Date/Time", "Application", "Type", "Pr",
            "CCSID", "Encoding", "Reply To Q", "Reply To QMgr",
            "Message ID", "Correlation ID", "Group ID"
        };
        constexpr int kCols = 16;

        m_table = new QTableWidget(0, kCols, this);
        QStringList headers;
        for (const char * h : kHeaders) headers << QString::fromLatin1(h);
        m_table->setHorizontalHeaderLabels(headers);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setStretchLastSection(false);
        m_table->setAlternatingRowColors(true);
        m_table->verticalHeader()->setVisible(false);
        m_table->setSortingEnabled(false);

        // populate
        int row = 0;
        for (const auto & e : entries)
        {
            m_table->insertRow(row);
            m_table->setItem(row,  0, cell(QString::number(row + 1)));
            m_table->setItem(row,  1, cell(QString::number(e.msgLen)));
            m_table->setItem(row,  2, cell(fmtFixed(e.mqmd.Format, MQ_FORMAT_LENGTH)));
            m_table->setItem(row,  3, cell(fmtGS(e.mqmd.MsgFlags)));
            m_table->setItem(row,  4, cell(fmtFixed(e.mqmd.UserIdentifier, MQ_USER_ID_LENGTH)));
            m_table->setItem(row,  5, cell(fmtDateTime(e.mqmd.PutDate, e.mqmd.PutTime)));
            m_table->setItem(row,  6, cell(fmtFixed(e.mqmd.PutApplName, MQ_PUT_APPL_NAME_LENGTH)));
            m_table->setItem(row,  7, cell(QString::number(e.mqmd.MsgType)));
            m_table->setItem(row,  8, cell(QString::number(e.mqmd.Priority)));
            m_table->setItem(row,  9, cell(QString::number(e.mqmd.CodedCharSetId)));
            m_table->setItem(row, 10, cell(QString::number(e.mqmd.Encoding)));
            m_table->setItem(row, 11, cell(fmtFixed(e.mqmd.ReplyToQ, MQ_Q_NAME_LENGTH)));
            m_table->setItem(row, 12, cell(fmtFixed(e.mqmd.ReplyToQMgr, MQ_Q_MGR_NAME_LENGTH)));
            m_table->setItem(row, 13, cell(fmtId(e.mqmd.MsgId,   MQ_MSG_ID_LENGTH)));
            m_table->setItem(row, 14, cell(fmtId(e.mqmd.CorrelId, MQ_CORREL_ID_LENGTH)));
            m_table->setItem(row, 15, cell(fmtId(e.mqmd.GroupId,  MQ_GROUP_ID_LENGTH)));
            row++;
        }
        mainLayout->addWidget(m_table, 1);

        // ---- status label --------------------------------------------------
        m_countLabel = new QLabel(
            entries.empty()
                ? QStringLiteral("No messages found")
                : QStringLiteral("%1 message(s)").arg((int)entries.size()));
        mainLayout->addWidget(m_countLabel);

        // ---- buttons -------------------------------------------------------
        auto * btnRow = new QHBoxLayout;
        m_readQButton        = new QPushButton(QStringLiteral("Read Q"));
        m_browseQButton      = new QPushButton(QStringLiteral("Browse Q"));
        m_startBrowseButton  = new QPushButton(QStringLiteral("Start Browse"));
        m_closeButton        = new QPushButton(QStringLiteral("Close"));

        m_readQButton->setToolTip(QStringLiteral("Destructively get the selected message"));
        m_browseQButton->setToolTip(QStringLiteral("Browse (peek) the selected message"));
        m_startBrowseButton->setToolTip(QStringLiteral("Start a browse session from the selected message"));
        m_closeButton->setDefault(true);

        btnRow->addWidget(m_readQButton);
        btnRow->addWidget(m_browseQButton);
        btnRow->addWidget(m_startBrowseButton);
        btnRow->addStretch();
        btnRow->addWidget(m_closeButton);
        mainLayout->addLayout(btnRow);

        connect(m_table,            &QTableWidget::itemSelectionChanged, this, &DisplayQDialog::onSelectionChanged);
        connect(m_table,            &QTableWidget::cellDoubleClicked,    this, &DisplayQDialog::onRowDoubleClicked);
        connect(m_readQButton,       &QPushButton::clicked, this, &DisplayQDialog::onReadQ);
        connect(m_browseQButton,     &QPushButton::clicked, this, &DisplayQDialog::onBrowseQ);
        connect(m_startBrowseButton, &QPushButton::clicked, this, &DisplayQDialog::onStartBrowse);
        connect(m_closeButton,       &QPushButton::clicked, this, &QDialog::reject);

        updateButtons();

        if (!entries.empty())
            m_table->selectRow(0);
    }

    DisplayQDialog::~DisplayQDialog() = default;

    void DisplayQDialog::onSelectionChanged()
    {
        auto sel = m_table->selectedItems();
        m_selectedRow = sel.isEmpty() ? -1 : m_table->currentRow();
        updateButtons();
    }

    void DisplayQDialog::onRowDoubleClicked(int row, int /*col*/)
    {
        m_selectedRow = row;
        choose(DispQAction::BrowseQ);
    }

    void DisplayQDialog::onReadQ()       { choose(DispQAction::ReadQ);       }
    void DisplayQDialog::onBrowseQ()     { choose(DispQAction::BrowseQ);     }
    void DisplayQDialog::onStartBrowse() { choose(DispQAction::StartBrowse); }

    void DisplayQDialog::choose(DispQAction action)
    {
        m_action = action;
        accept();
    }

    void DisplayQDialog::updateButtons()
    {
        bool hasSel = (m_selectedRow >= 0);
        m_readQButton->setEnabled(hasSel);
        m_browseQButton->setEnabled(hasSel);
        m_startBrowseButton->setEnabled(hasSel);
    }
}
