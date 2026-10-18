// DataTab.h - the Data tab (Windows IDD_DATA, class MSGDATA, MSGDATA.cpp; layout in rfhutil.rc) with Find, Find Hex
// and Go To (rfhutilView.cpp findHelper 1905, hexFindHelper 2069; DataArea.cpp findNext 12649).
//
// The tab shows format::render of the document's message for the chosen view. It owns no display state:
//
//   * Document. The view is AppDocument::shell().view and the formats and options are AppDocument::render(). A radio or
//     check box click writes the document at once (setShell, setRender), and the widgets show whatever the document
//     holds after any change, so a get, a file read or Operations that sets the formats is shown without the tab being
//     told. The Find dialogs' presence is shell().findDialogOpen. Nothing reads the tab's widgets back.
//   * Render. A change of the message, the formats, the options or the view renders once (renderCount()); changes that do
//     not touch the display do not. A tab that is not visible only marks itself stale and renders once when it is shown
//     (or when a search needs the text); the document's acceptMessage() delivers a message with its formats as one
//     change, so a get or a file read renders once. An empty body shows an empty box. A failed render shows its text. The text is in
//     options.display.ansi (the settings' display code page when AppSettings is given) and is decoded with
//     rfh_MultiByteToWideChar, never with a Qt codec; CR LF is shown as the edit's line break.
//   * Search. The Windows search state (findStartOffset) is here: -1 or the offset of the previous hit. A new text, a new
//     view or a new message starts again from the end the direction begins at. Character, Hex and Both search the raw
//     bytes of the body (the typed text in the display code page, translated ASCII to EBCDIC when the character format
//     is EBCDIC; Find Hex bytes are not translated). XML, Parsed, JSON and Fixed search the rendered bytes. A hit is
//     shown with format::locate (byte views) or its place in the rendered text, selecting its first byte (two digits in
//     the Hex view), with the hit line the first visible line. A miss resets the search, shows no message and moves to
//     the top.
//   * Not ported in Phase 1: Allow Edit and the tab's Write Q (disabled, "Not yet ported"); the BOM box is hidden and
//     disabled as on Windows.
//
// The Ctrl+ and Alt+ keys of inventory 7.1 are window shortcuts that act only while this tab is visible. Where a key
// has a mnemonic on a control (Alt+A Ascii, Alt+E Ebcdic, Alt+S, Alt+K, Alt+T, Alt+J, Alt+H, Alt+O, Alt+F, Alt+Y, Alt+R),
// the mnemonic is the key; Character, Hex and Browse Next carry none because Windows' table gives their letters to other
// commands (Alt+T Trad Chinese, Alt+H Thai, Alt+N next tab).
//
// Threading: GUI thread only.
#pragma once

#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <string>

#include "rfh_format.h"

class QAbstractButton;
class QButtonGroup;
class QPlainTextEdit;

namespace Ui
{
	class DataTab;
}

namespace rfhqt
{
	class AppDocument;
	class AppSettings;
	class FindDialog;
	class HexFindDialog;
	class Operations;

	class DataTab : public QWidget
	{
		Q_OBJECT

	public:
		// Windows OnCopybook: the "COBOL copy book file" dialog. Returns the chosen path, empty = cancelled.
		using CopybookChooser = std::function<QString(QWidget * parent, const QString & directory)>;

		DataTab(AppDocument & document, Operations & operations, AppSettings * settings = nullptr, QWidget * parent = nullptr);
		~DataTab() override;

		void setCopybookChooser(CopybookChooser chooser);

		// How many times the text was produced (a render, or the empty text of an empty body).
		int renderCount() const { return renders; }

		void focusFirst();

		// The size from which a body is announced before it is formatted: a 10 MB body takes seconds in the larger views
		// (the render itself is quick; the decode and the text box are not), and the tab cannot show progress.
		static constexpr size_t LargeBodyBytes = 4 * 1024 * 1024;

		// The search and Go To by program, as the dialogs do them: the typed text in the display code page of the
		// character set (translated to EBCDIC for the byte views when that is the character format), and a byte offset.
		void searchFor(const QString & text, bool down);
		void goToOffset(qint64 offset);

	public slots:
		void showFind();			// Ctrl+F
		void showFindHex();			// Ctrl+Z
		void showGoto();			// Ctrl+G
		void chooseCopybook();		// Copybook button, Ctrl+Y, Alt+Y
		void scrollTop();			// Edit > Top
		void scrollBottom();		// Edit > Bottom

	protected:
		void showEvent(QShowEvent * event) override;

	signals:
		void statusMessage(const QString &);		// a line for the status box (copy book read, render warnings)

	private:
		void wire();
		void addShortcuts();
		void refresh();
		void syncWidgets();
		void render();
		void markStale() { stale = true; }

		void selectView(rfh::format::View view);
		bool askForCopybook(std::string & path);
		void updateRender(const std::function<void(rfh::format::RenderOptions &)> & change);
		void toggle(QAbstractButton * a, QAbstractButton * b);

		void updateDialogFlag();
		void dialogClosed(QDialog * dialog);
		void findRequested(const std::string & needle, bool down);
		void findTyped();
		void ensureRendered();
		bool viewUsable(const char * what);
		int textCodePage() const;
		void findHex();
		void resetSearch() { searchStart = -1; }
		std::string typedBytes(const QString & text) const;
		void select(size_t byteOffset, size_t byteLength);
		void showHit(size_t offset);
		void moveToTop(bool selectFirst);
		QString decode(const char * bytes, size_t length) const;
		size_t lineStart(size_t line) const;

		AppDocument & doc;
		Operations & ops;
		AppSettings * settings;
		std::unique_ptr<Ui::DataTab> ui;
		QButtonGroup * viewGroup;
		QButtonGroup * charGroup;
		QButtonGroup * intGroup;
		QButtonGroup * pdGroup;
		CopybookChooser chooser;
		QPointer<FindDialog> findDialog;
		QPointer<HexFindDialog> hexDialog;

		std::string shownBytes;			// the render output, in the display code page
		int shownCodePage = 0;			// the code page shownBytes is in
		bool stale = true;				// the display data changed since the last render
		rfh::format::View renderedView = rfh::format::View::Character;
		int batch = 0;					// > 0: refresh is held back
		int renders = 0;
		std::string renderFailure;		// why the view could not be produced; empty when it could
		QString copybookToReport;		// a copy book chosen and not yet read by a render

		qint64 searchStart = -1;		// DataArea::findStartOffset
		std::string searchNeedle;		// what the position belongs to
		rfh::format::View searchView = rfh::format::View::Character;
	};
}
