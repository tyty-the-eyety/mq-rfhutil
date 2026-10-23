// DisplayQDialog.h - list of messages in a queue; port of CDispQ (DispQ.cpp/h).
//
// Populated from a pre-scanned vector of DisplayQEntry structs (MQMD + length).
// The user selects a row and clicks Read Q, Browse Q or Start Browse; the dialog
// returns the chosen action and the 0-based index so the caller can look up the
// message id from the same vector.
#pragma once

#include <QDialog>
#include <vector>
#include <cmqc.h>

class QTableWidget;
class QPushButton;
class QLabel;

namespace rfhqt
{
    // What the user chose to do with the selected message.
    enum class DispQAction { None, ReadQ, BrowseQ, StartBrowse };

    struct DisplayQEntry
    {
        MQMD2 mqmd  = { MQMD2_DEFAULT };
        int   msgLen = 0;
    };

    class DisplayQDialog : public QDialog
    {
        Q_OBJECT

    public:
        explicit DisplayQDialog(const QString & qName,
                                const std::vector<DisplayQEntry> & entries,
                                QWidget * parent = nullptr);
        ~DisplayQDialog() override;

        // Valid after accept(); -1 if nothing was selected.
        int         selectedRow()    const { return m_selectedRow; }
        DispQAction selectedAction() const { return m_action; }

    private slots:
        void onSelectionChanged();
        void onReadQ();
        void onBrowseQ();
        void onStartBrowse();
        void onRowDoubleClicked(int row, int col);

    private:
        void choose(DispQAction action);
        void updateButtons();

        const std::vector<DisplayQEntry> & m_entries;
        int         m_selectedRow = -1;
        DispQAction m_action      = DispQAction::None;

        QTableWidget * m_table;
        QPushButton  * m_readQButton;
        QPushButton  * m_browseQButton;
        QPushButton  * m_startBrowseButton;
        QPushButton  * m_closeButton;
        QLabel       * m_countLabel;
    };
}
