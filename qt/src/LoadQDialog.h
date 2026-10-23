// LoadQDialog.h - "Load messages from file(s) to Queue …" dialog.
//
// Port of CLoadQ (LoadQ.cpp/h) from MFC to Qt.
// Collects the parameters for rfh::loadQueue() and packages them
// as LoadQueueOptions when the user accepts.
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
    class LoadQDialog : public QDialog
    {
        Q_OBJECT

    public:
        // `qName` is shown in the dialog title and stored in the returned options.
        explicit LoadQDialog(const QString & qName, QWidget * parent = nullptr);
        ~LoadQDialog() override;

        // Returns the filled options. Call only after the dialog was accepted.
        rfh::LoadQueueOptions values() const;

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
        QCheckBox     * m_removeMqmd;
        QCheckBox     * m_removeHdrs;
        QCheckBox     * m_useSetAll;
        QCheckBox     * m_newMsgId;
        QCheckBox     * m_persistent;
        QCheckBox     * m_writeOnce;
        QCheckBox     * m_singleFile;
        QCheckBox     * m_ignoreProps;
        QLineEdit     * m_maxCount;
        QLineEdit     * m_startMsg;
        QLineEdit     * m_batchSize;
        QLineEdit     * m_waitTime;
        QComboBox     * m_formatCombo;
        QLineEdit     * m_ccsid;
        QLineEdit     * m_encoding;
        QLabel        * m_errorLabel;
        QPushButton   * m_okButton;
        QPushButton   * m_cancelButton;
    };
}
