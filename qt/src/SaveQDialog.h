// SaveQDialog.h - "Save messages from Queue … to file(s)" dialog.
//
// Port of CSavemsgs (Savemsgs.cpp/h) from MFC to Qt.
// Collects the parameters for rfh::saveQueue() and packages them
// as SaveQueueOptions when the user accepts.
#pragma once

#include <QDialog>

#include "rfh_queue_file.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace rfhqt
{
    class SaveQDialog : public QDialog
    {
        Q_OBJECT

    public:
        // `qName` is shown in the dialog title and stored in the returned options.
        explicit SaveQDialog(const QString & qName, QWidget * parent = nullptr);
        ~SaveQDialog() override;

        // Returns the filled options. Call only after the dialog was accepted.
        rfh::SaveQueueOptions values() const;

    private slots:
        void browse();
        void updateDelimiterEnabled();

    private:
        void validate();

        QString         m_qName;
        QLineEdit     * m_fileName;
        QPushButton   * m_browseButton;
        QRadioButton  * m_oneFile;
        QRadioButton  * m_filePerMsg;
        QRadioButton  * m_delimChar;
        QRadioButton  * m_delimHex;
        QLineEdit     * m_delimiter;
        QLineEdit     * m_propDelim;
        QCheckBox     * m_inclMqmd;
        QCheckBox     * m_inclHeaders;
        QRadioButton  * m_browseOnly;
        QRadioButton  * m_removeYes;
        QCheckBox     * m_appendFile;
        QLineEdit     * m_startCount;
        QLineEdit     * m_endCount;
        QComboBox     * m_maxFileSize;
        QLabel        * m_errorLabel;
        QPushButton   * m_okButton;
        QPushButton   * m_cancelButton;
    };
}
