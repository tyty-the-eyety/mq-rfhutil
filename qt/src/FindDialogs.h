// FindDialogs.h - the Data tab's Find, Find Hex and Go To dialogs, and the byte search they drive.
//
// Windows: CFindReplaceDialog (rfhutilView.cpp:1717, no match-case and no whole-word box), CHexFind (HexFind.cpp,
// IDD_HEXFIND, caption "Hex Finder") and CGoto (Goto.cpp, IDD_GOTO). The dialogs only collect and validate input; the
// search state and the highlight are DataTab's (as findStartOffset is DataArea's and moveDisplay the page's).
//
// searchBytes is DataArea::findNext (DataArea.cpp:12649) without the state: the caller holds the previous hit. Three
// Windows defects are fixed on purpose (owner decision of 2026-10-17): a match that ends exactly at the end of the data
// is found (Windows' bound is fileSize - len, exclusive), searching up starts at the last possible match (Windows:
// fileSize - 1 - len), and a hex search is not translated to EBCDIC (that is the caller's rule, not this function's).
//
// Threading: GUI thread only.
#pragma once

#include <QByteArray>
#include <QDialog>
#include <QString>

#include <string>
#include <string_view>
#include <utility>

class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace rfhqt
{
	// The offset of the next match of needle in data after (down) or before (up) `previous`, the offset of the previous
	// hit (-1 = none: down starts at 0, up at the last position a match can start), or -1 for no match. Case sensitive,
	// byte for byte; overlapping matches are found; there is no wrap-around. An empty needle matches nothing.
	qint64 searchBytes(std::string_view data, std::string_view needle, bool down, qint64 previous);

	// The longest needle DataArea::findNext uses (its buffer is 256 bytes with a terminator).
	constexpr int kMaxNeedleBytes = 255;

	// Find (Ctrl+F): modeless, "Find what", Direction Up/Down (Down), Find Next and Cancel.
	class FindDialog : public QDialog
	{
		Q_OBJECT

	public:
		explicit FindDialog(QWidget * parent = nullptr);

		QString text() const;
		bool searchDown() const;

	signals:
		void findRequested();		// Find Next (the button is off while the text is empty, as in the Windows dialog)

	private:
		QLineEdit * edit;
		QRadioButton * down;
		QPushButton * findButton;
	};

	// Find Hex (Ctrl+Z): "Hex Finder", an upper-casing edit of up to 16 characters, Direction Up/Down, Find and Cancel,
	// and the error label of HexFind.cpp hexDatavalid.
	class HexFindDialog : public QDialog
	{
		Q_OBJECT

	public:
		static constexpr int MaxDigits = 16;		// 8 bytes

		explicit HexFindDialog(QWidget * parent = nullptr);

		QByteArray bytes() const;		// the bytes of the digits (after a successful Find)
		bool searchDown() const;

		// "" for valid digits, else the Windows message.
		static QString check(const QString & digits);

	signals:
		void findRequested();		// valid digits: look at bytes()

	private:
		void find();

		QLineEdit * edit;
		QLabel * error;
		QRadioButton * down;
	};

	// Go To (Ctrl+G): modal, "Go to offset", a digits-only edit, OK and Cancel.
	class GotoDialog : public QDialog
	{
		Q_OBJECT

	public:
		explicit GotoDialog(qint64 dataSize, QWidget * parent = nullptr);

		qint64 offset() const { return value; }		// the zero-based byte offset the user asked for (after OK)

		// Goto.cpp OnOK: (offset, "") or (0, message). An empty box is 0, as Windows' atoi("") makes it.
		static std::pair<qint64, QString> check(const QString & text, qint64 dataSize);

	private:
		void ok();

		qint64 size;
		qint64 value = 0;
		QLineEdit * edit;
		QLabel * error;
	};
}
