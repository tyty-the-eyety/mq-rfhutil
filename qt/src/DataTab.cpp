#include "DataTab.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QKeySequence>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QTextBlock>
#include <QTextCursor>

#include <string.h>

#include <vector>

#include "AppDocument.h"
#include "AppSettings.h"
#include "comsubs.h"
#include "FindDialogs.h"
#include "Operations.h"
#include "rfh_codepage.h"
#include "ui_DataTab.h"


namespace rfhqt
{
	namespace
	{
		using rfh::format::View;

		bool isByteView(View v)
		{
			return v == View::Character || v == View::Hex || v == View::Both;
		}

		bool isSearchableView(View v)
		{
			return v != View::Cobol;
		}

		// the radio is off when the host has no such code page (MSGDATA.cpp:502-532)
		struct CharSetCodePage
		{
			const char * radio;
			int codePage;
		};
	}

	DataTab::DataTab(AppDocument & document, Operations & operations, AppSettings * appSettings, QWidget * parent)
		: QWidget(parent), doc(document), ops(operations), settings(appSettings), ui(new Ui::DataTab),
		  viewGroup(new QButtonGroup(this)), charGroup(new QButtonGroup(this)), intGroup(new QButtonGroup(this)),
		  pdGroup(new QButtonGroup(this))
	{
		ui->setupUi(this);
		setObjectName(QStringLiteral("dataTab"));
		viewGroup->setObjectName(QStringLiteral("viewGroup"));
		charGroup->setObjectName(QStringLiteral("charGroup"));
		intGroup->setObjectName(QStringLiteral("intGroup"));
		pdGroup->setObjectName(QStringLiteral("pdGroup"));

		const QList<QAbstractButton *> viewButtons = { ui->viewCharacterRadio, ui->viewHexRadio, ui->viewBothRadio, ui->viewXmlRadio,
			ui->viewParsedRadio, ui->viewCobolRadio, ui->viewJsonRadio, ui->viewFixRadio };
		const View views[] = { View::Character, View::Hex, View::Both, View::Xml, View::Parsed, View::Cobol, View::Json, View::Fixed };
		for (int i = 0; i < viewButtons.size(); i++)
		{
			viewGroup->addButton(viewButtons[i], (int)views[i]);
		}
		charGroup->addButton(ui->charAsciiRadio, CHAR_ASCII);
		charGroup->addButton(ui->charEbcdicRadio, CHAR_EBCDIC);
		charGroup->addButton(ui->charChineseRadio, CHAR_CHINESE);
		charGroup->addButton(ui->charKoreanRadio, CHAR_KOREAN);
		charGroup->addButton(ui->charTradChineseRadio, CHAR_TRAD_CHIN);
		charGroup->addButton(ui->charJapaneseRadio, CHAR_JAPANESE);
		charGroup->addButton(ui->charThaiRadio, CHAR_THAI);
		charGroup->addButton(ui->charRussianRadio, CHAR_RUSSIAN);
		intGroup->addButton(ui->intPcRadio, NUMERIC_PC);
		intGroup->addButton(ui->intHostRadio, NUMERIC_HOST);
		pdGroup->addButton(ui->pdPcRadio, NUMERIC_PC);
		pdGroup->addButton(ui->pdHostRadio, NUMERIC_HOST);

		// OnSetActive: the character sets the host cannot show are off
		const CharSetCodePage sets[] = { { "charChineseRadio", 936 }, { "charKoreanRadio", 949 }, { "charTradChineseRadio", 950 },
			{ "charJapaneseRadio", 932 }, { "charThaiRadio", 874 }, { "charRussianRadio", 1251 } };
		for (const CharSetCodePage & s : sets)
		{
			if (QAbstractButton * b = findChild<QAbstractButton *>(QString::fromLatin1(s.radio)))
			{
				b->setEnabled(rfh_is_codepage_installed(s.codePage) != 0);
			}
		}

		// out of Phase 1 (inventory 5.6); the BOM box is never shown or enabled (MSGDATA.cpp:870)
		ui->allowEditCheck->setEnabled(false);
		ui->allowEditCheck->setToolTip(tr("Not yet ported"));
		ui->writeQButton->setEnabled(false);
		ui->writeQButton->setToolTip(tr("Not yet ported"));
		ui->bomCheck->setVisible(false);
		ui->bomCheck->setEnabled(false);

		ui->dataEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
		ui->dataEdit->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);

		chooser = [](QWidget * owner, const QString & directory)
		{
			return QFileDialog::getOpenFileName(owner, QObject::tr("COBOL copy book file"), directory,
			                                    QObject::tr("All (*.*);;COBOL files (*.cbl *.cpy);;Text (*.TXT)"));
		};

		if (settings != nullptr && doc.render().display.ansi != settings->displayCodePage())
		{
			rfh::format::RenderOptions o = doc.render();
			o.display.ansi = settings->displayCodePage();
			doc.setRender(o);
		}

		wire();
		addShortcuts();
		syncWidgets();		// the text is made when the tab is first shown
	}

	DataTab::~DataTab()
	{
		// the dialogs go with the tab; the document must not keep saying one is open
		if (doc.shell().findDialogOpen)
		{
			ShellState s = doc.shell();
			s.findDialogOpen = false;
			doc.setShell(s);
		}
	}

	void DataTab::setCopybookChooser(CopybookChooser newChooser)
	{
		chooser = std::move(newChooser);
	}

	void DataTab::focusFirst()
	{
		ui->dataEdit->setFocus();
	}

	// ---- wiring --------------------------------------------------------------------------------------------------------

	void DataTab::wire()
	{
		connect(&doc, &AppDocument::messageChanged, this, [this]() { markStale(); resetSearch(); });
		connect(&doc, &AppDocument::renderChanged, this, [this]() { markStale(); });
		connect(&doc, &AppDocument::shellChanged, this, [this]()
		{
			if (doc.shell().view != renderedView)
			{
				markStale();
			}
		});
		connect(&doc, &AppDocument::stateChanged, this, &DataTab::refresh);

		connect(viewGroup, &QButtonGroup::idClicked, this, [this](int id) { selectView((View)id); });
		connect(charGroup, &QButtonGroup::idClicked, this, [this](int id)
		{
			updateRender([id](rfh::format::RenderOptions & o) { o.charFormat = id; });
		});
		connect(intGroup, &QButtonGroup::idClicked, this, [this](int id)
		{
			updateRender([id](rfh::format::RenderOptions & o) { o.numericFormat = id; });
		});
		connect(pdGroup, &QButtonGroup::idClicked, this, [this](int id)
		{
			updateRender([id](rfh::format::RenderOptions & o) { o.pdNumericFormat = id; });
		});
		connect(ui->crlfCheck, &QAbstractButton::clicked, this, [this](bool on)
		{
			updateRender([on](rfh::format::RenderOptions & o) { o.crlf = on; });
		});
		connect(ui->ediCheck, &QAbstractButton::clicked, this, [this](bool on)
		{
			updateRender([on](rfh::format::RenderOptions & o) { o.edi = on; });
		});
		connect(ui->indentCheck, &QAbstractButton::clicked, this, [this](bool on)
		{
			updateRender([on](rfh::format::RenderOptions & o) { o.indent = on; });
		});
		connect(ui->validateCheck, &QAbstractButton::clicked, this, [this](bool on)
		{
			updateRender([on](rfh::format::RenderOptions & o) { o.checkData = on; });
		});
		connect(ui->browseNextButton, &QAbstractButton::clicked, this, [this]() { ops.browseNext(); });
		connect(ui->browsePrevButton, &QAbstractButton::clicked, this, [this]() { ops.browsePrev(); });
		connect(ui->copybookButton, &QAbstractButton::clicked, this, &DataTab::chooseCopybook);
	}

	// Inventory 7.1, the Data tab column. Keys that are mnemonics of a control are not repeated here.
	void DataTab::addShortcuts()
	{
		auto add = [this](const QKeySequence & keys, std::function<void()> action)
		{
			QShortcut * shortcut = new QShortcut(keys, this);
			shortcut->setContext(Qt::WindowShortcut);
			connect(shortcut, &QShortcut::activated, this, std::move(action));
		};
		auto click = [add](const QKeySequence & keys, QAbstractButton * button)
		{
			add(keys, [button]() { button->click(); });
		};
		click(Qt::CTRL | Qt::Key_Q, ui->viewParsedRadio);
		click(Qt::CTRL | Qt::Key_B, ui->viewBothRadio);
		click(Qt::CTRL | Qt::Key_T, ui->viewCharacterRadio);
		click(Qt::CTRL | Qt::Key_K, ui->viewCobolRadio);
		click(Qt::CTRL | Qt::Key_L, ui->viewXmlRadio);
		click(Qt::CTRL | Qt::Key_H, ui->viewHexRadio);
		click(Qt::CTRL | Qt::Key_E, ui->charEbcdicRadio);
		click(Qt::CTRL | Qt::Key_N, ui->browseNextButton);
		click(Qt::CTRL | Qt::Key_R, ui->browsePrevButton);
		click(Qt::CTRL | Qt::Key_Y, ui->copybookButton);
		add(Qt::CTRL | Qt::Key_D, [this]() { toggle(ui->pdPcRadio, ui->pdHostRadio); });
		add(Qt::CTRL | Qt::Key_I, [this]() { toggle(ui->intPcRadio, ui->intHostRadio); });
		add(Qt::CTRL | Qt::Key_M, [this]() { ops.endBrowse(); });
		add(Qt::ALT | Qt::Key_V, [this]() { ops.endBrowse(); });
		add(Qt::ALT | Qt::Key_X, [this]() { ops.clearData(); });
		add(Qt::CTRL | Qt::Key_F, [this]() { showFind(); });
		add(Qt::CTRL | Qt::Key_Z, [this]() { showFindHex(); });
		add(Qt::CTRL | Qt::Key_G, [this]() { showGoto(); });
	}

	void DataTab::toggle(QAbstractButton * a, QAbstractButton * b)
	{
		(a->isChecked() ? b : a)->click();
	}

	// ---- what the document says ------------------------------------------------------------------------------------------

	void DataTab::refresh()
	{
		if (batch > 0)
		{
			return;
		}
		syncWidgets();
		if (stale && isVisible())
		{
			render();
		}
	}

	void DataTab::showEvent(QShowEvent * event)
	{
		QWidget::showEvent(event);
		refresh();
	}

	void DataTab::ensureRendered()
	{
		if (stale)
		{
			render();
		}
	}

	// The character set decides the code page of the text, as the per-script fonts of Windows decided how it was drawn
	// (MSGDATA.cpp setDisplayFont). Nothing in the core maps one to the other: DataFormatter only knows the lead bytes
	// of the double-byte sets. The other character sets use the display setting.
	int DataTab::textCodePage() const
	{
		const rfh::format::RenderOptions & o = doc.render();
		switch (o.charFormat)
		{
		case CHAR_CHINESE: return 936;
		case CHAR_KOREAN: return 949;
		case CHAR_TRAD_CHIN: return 950;
		case CHAR_JAPANESE: return 932;
		case CHAR_THAI: return 874;
		case CHAR_RUSSIAN: return 1251;
		default: return o.display.ansi;
		}
	}

	void DataTab::syncWidgets()
	{
		const rfh::format::RenderOptions & o = doc.render();
		auto check = [](QButtonGroup * group, int id)
		{
			if (QAbstractButton * button = group->button(id))
			{
				button->setChecked(true);
				return;
			}
			group->setExclusive(false);		// a value no button stands for (CHAR_UNICODE): none is shown
			for (QAbstractButton * b : group->buttons())
			{
				b->setChecked(false);
			}
			group->setExclusive(true);
		};
		check(viewGroup, (int)doc.shell().view);
		check(charGroup, o.charFormat);
		check(intGroup, o.numericFormat);
		check(pdGroup, o.pdNumericFormat);
		ui->crlfCheck->setChecked(o.crlf);
		ui->ediCheck->setChecked(o.edi);
		ui->indentCheck->setChecked(o.indent);
		ui->validateCheck->setChecked(o.checkData);
		ui->bomCheck->setChecked(o.bom);
		const QString caption = QString::fromStdString(doc.dataSource());
		if (ui->captionLabel->text() != caption)
		{
			ui->captionLabel->setText(caption);
		}
		const Enables e = doc.enables();
		ui->browseNextButton->setEnabled(e.dataBrowseNext);
		ui->browsePrevButton->setEnabled(e.dataBrowsePrev);
		ui->allowEditCheck->setEnabled(e.dataAllowEdit);
		ui->writeQButton->setEnabled(e.dataWriteQ);
		ui->bomCheck->setEnabled(e.dataBom);
	}

	QString DataTab::decode(const char * bytes, size_t length) const
	{
		if (length == 0)
		{
			return QString();
		}
		std::vector<RFH_UCS2> units(length);		// a byte gives at most one code unit
		const int n = rfh_MultiByteToWideChar(shownCodePage, 0, bytes, (int)length, units.data(), (int)length);
		QString text;
		if (n <= 0)
		{
			text = QString::fromLatin1(bytes, (qsizetype)length);		// the code page is not available: show the bytes as they are
		}
		else
		{
			text = QString(reinterpret_cast<const QChar *>(units.data()), n);
		}
		text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		return text;
	}

	void DataTab::render()
	{
		stale = false;
		renderedView = doc.shell().view;
		renders++;
		const std::string previous = std::move(shownBytes);
		shownBytes.clear();
		renderFailure.clear();
		const rfh::Message & message = doc.message();
		if (message.body.empty())
		{
			ui->dataEdit->clear();
			return;
		}
		if (message.body.size() > LargeBodyBytes)
		{
			emit statusMessage(tr("Formatting %1 bytes for display; this can take several seconds").arg(message.body.size()));
		}
		rfh::format::RenderOptions options = doc.render();
		options.display.ansi = textCodePage();
		std::string out;
		const rfh::Result result = rfh::format::render(message, renderedView, options, out);
		const bool cobol = renderedView == View::Cobol;
		if (!result.ok())
		{
			renderFailure = result.text;
			if (!isByteView(renderedView))
			{
				resetSearch();
			}
			if (cobol && !copybookToReport.isEmpty())
			{
				emit statusMessage(tr("error reading copy book file (%1): %2").arg(copybookToReport, QString::fromStdString(result.text)));
				copybookToReport.clear();
			}
			ui->dataEdit->setPlainText(QString::fromUtf8(result.text.data(), (qsizetype)result.text.size()));
			return;
		}
		if (cobol && !copybookToReport.isEmpty())
		{
			emit statusMessage(tr("copybook file read (%1)").arg(copybookToReport));
			copybookToReport.clear();
		}
		for (const std::string & warning : result.warnings)
		{
			emit statusMessage(QString::fromUtf8(warning.data(), (qsizetype)warning.size()));
		}
		if (!isByteView(renderedView) && out != previous)
		{
			resetSearch();		// the position was an offset in text that is gone
		}
		shownBytes = std::move(out);
		shownCodePage = options.display.ansi;
		ui->dataEdit->setPlainText(decode(shownBytes.data(), shownBytes.size()));
	}

	// ---- the user's choices -----------------------------------------------------------------------------------------------

	void DataTab::updateRender(const std::function<void(rfh::format::RenderOptions &)> & change)
	{
		rfh::format::RenderOptions o = doc.render();
		const rfh::format::RenderOptions before = o;
		change(o);
		const bool same = before.charFormat == o.charFormat && before.numericFormat == o.numericFormat
			&& before.pdNumericFormat == o.pdNumericFormat && before.crlf == o.crlf && before.edi == o.edi && before.bom == o.bom
			&& before.indent == o.indent && before.checkData == o.checkData && before.copybookPath == o.copybookPath;
		if (!same)
		{
			doc.setRender(o);
		}
	}

	bool DataTab::askForCopybook(std::string & path)
	{
		QString directory;
		if (settings != nullptr)
		{
			directory = settings->lastDirectory(QStringLiteral("copybook"));
		}
		const QString chosen = chooser ? chooser(this, directory) : QString();
		if (chosen.isEmpty())
		{
			return false;
		}
		if (settings != nullptr)
		{
			settings->setLastDirectory(QStringLiteral("copybook"), QFileInfo(chosen).absolutePath());
		}
		path = chosen.toStdString();
		copybookToReport = chosen;		// reported when a render has read it
		return true;
	}

	void DataTab::chooseCopybook()
	{
		std::string path;
		if (askForCopybook(path))
		{
			const int before = renders;
			updateRender([&path](rfh::format::RenderOptions & o) { o.copybookPath = path; });
			if (doc.shell().view == View::Cobol && renders == before)
			{
				markStale();		// the same file again: read it again
				refresh();
			}
		}
	}

	// OnCobol (MSGDATA.cpp:188): without a copy book the file dialog comes first, and a cancel keeps the old view
	void DataTab::selectView(View view)
	{
		std::string path;
		if (view == View::Cobol && doc.render().copybookPath.empty() && !askForCopybook(path))
		{
			syncWidgets();
			return;
		}
		batch++;
		if (!path.empty())
		{
			updateRender([&path](rfh::format::RenderOptions & o) { o.copybookPath = path; });
		}
		ShellState s = doc.shell();
		s.view = view;
		doc.setShell(s);
		batch--;
		refresh();
	}

	void DataTab::scrollTop()
	{
		QTextCursor c = ui->dataEdit->textCursor();
		c.movePosition(QTextCursor::Start);
		ui->dataEdit->setTextCursor(c);
		ui->dataEdit->verticalScrollBar()->setValue(0);
	}

	void DataTab::scrollBottom()
	{
		QTextCursor c = ui->dataEdit->textCursor();
		c.movePosition(QTextCursor::End);
		ui->dataEdit->setTextCursor(c);
		ui->dataEdit->ensureCursorVisible();
	}

	// ---- the dialogs --------------------------------------------------------------------------------------------------------

	void DataTab::updateDialogFlag()
	{
		ShellState s = doc.shell();
		s.findDialogOpen = !findDialog.isNull() || !hexDialog.isNull();
		doc.setShell(s);
	}

	void DataTab::dialogClosed(QDialog * dialog)
	{
		if (dialog == findDialog.data())
		{
			findDialog.clear();
		}
		if (dialog == hexDialog.data())
		{
			hexDialog.clear();
		}
		dialog->setParent(nullptr);
		dialog->deleteLater();
		updateDialogFlag();
	}

	void DataTab::showFind()
	{
		if (!findDialog.isNull())
		{
			findDialog->raise();		// a second request brings the open dialog forward
			findDialog->activateWindow();
			return;
		}
		if (!doc.enables().menuFind)
		{
			return;
		}
		FindDialog * dialog = new FindDialog(this);
		connect(dialog, &FindDialog::findRequested, this, &DataTab::findTyped);
		connect(dialog, &QDialog::finished, this, [this, dialog]() { dialogClosed(dialog); });
		findDialog = dialog;
		updateDialogFlag();
		dialog->show();
	}

	void DataTab::showFindHex()
	{
		if (!hexDialog.isNull())
		{
			hexDialog->raise();
			hexDialog->activateWindow();
			return;
		}
		if (!doc.enables().menuFindHex)
		{
			return;
		}
		HexFindDialog * dialog = new HexFindDialog(this);
		connect(dialog, &HexFindDialog::findRequested, this, &DataTab::findHex);
		connect(dialog, &QDialog::finished, this, [this, dialog]() { dialogClosed(dialog); });
		hexDialog = dialog;
		updateDialogFlag();
		dialog->show();
	}

	void DataTab::showGoto()
	{
		if (!doc.enables().menuGoto)
		{
			return;
		}
		GotoDialog dialog((qint64)doc.dataSize(), this);
		if (dialog.exec() == QDialog::Accepted)
		{
			goToOffset(dialog.offset());
		}
	}

	// ---- searching ---------------------------------------------------------------------------------------------------------------

	// The typed text as the bytes of the display code page (CString in the ANSI build), at most 255 of them.
	std::string DataTab::typedBytes(const QString & text) const
	{
		std::string bytes;
		if (!text.isEmpty())
		{
			std::vector<char> out((size_t)text.size() * 4 + 4);
			const int n = rfh_WideCharToMultiByte(textCodePage(), 0, reinterpret_cast<const RFH_UCS2 *>(text.utf16()),
			                                      (int)text.size(), out.data(), (int)out.size(), nullptr, nullptr);
			if (n > 0)
			{
				bytes.assign(out.data(), (size_t)n);
			}
			else
			{
				bytes = text.toLatin1().toStdString();
			}
		}
		if (bytes.size() > (size_t)kMaxNeedleBytes)
		{
			bytes.resize((size_t)kMaxNeedleBytes);
		}
		return bytes;
	}

	void DataTab::findTyped()
	{
		if (!findDialog.isNull())
		{
			searchFor(findDialog->text(), findDialog->searchDown());
		}
	}

	void DataTab::searchFor(const QString & text, bool down)
	{
		std::string needle = typedBytes(text);
		const View view = doc.shell().view;
		if (isByteView(view) && doc.render().charFormat == CHAR_EBCDIC && !needle.empty())
		{
			std::string translated(needle.size(), '\0');
			AsciiToEbcdic((unsigned char *)needle.data(), (unsigned int)needle.size(), (unsigned char *)&translated[0]);
			needle = translated;
		}
		findRequested(needle, down);
	}

	void DataTab::findHex()
	{
		if (hexDialog.isNull() || !isByteView(doc.shell().view))
		{
			return;
		}
		const QByteArray bytes = hexDialog->bytes();		// not translated to EBCDIC (a Windows defect, fixed)
		findRequested(std::string(bytes.constData(), (size_t)bytes.size()), hexDialog->searchDown());
	}

	void DataTab::findRequested(const std::string & needle, bool down)
	{
		const View view = doc.shell().view;
		if (needle.empty() || !isSearchableView(view))
		{
			return;
		}
		ensureRendered();
		if (!viewUsable("Find"))
		{
			return;		// the position does not move on a view that could not be made
		}
		if (needle != searchNeedle || view != searchView)
		{
			searchStart = -1;		// findHelper: the text or the display type changed
			searchNeedle = needle;
			searchView = view;
		}
		const bool bytesView = isByteView(view);
		const std::string_view data = bytesView
			? std::string_view(reinterpret_cast<const char *>(doc.message().body.data()), doc.message().body.size())
			: std::string_view(shownBytes);
		const qint64 hit = searchBytes(data, needle, down, searchStart);
		if (hit < 0)
		{
			resetSearch();
			moveToTop(!bytesView);
			return;
		}
		searchStart = hit;
		if (bytesView)
		{
			showHit((size_t)hit);
		}
		else
		{
			select((size_t)hit, 1);
		}
	}

	void DataTab::showHit(size_t offset)
	{
		rfh::format::TextPos pos;
		if (rfh::format::locate(doc.shell().view, doc.render(), doc.message(), std::string_view(shownBytes), offset, pos))
		{
			select(lineStart((size_t)pos.line) + (size_t)pos.column, (size_t)pos.length);
		}
	}

	void DataTab::goToOffset(qint64 offset)
	{
		if (!isByteView(doc.shell().view) || offset < 0 || (size_t)offset >= doc.dataSize())
		{
			return;
		}
		ensureRendered();
		if (viewUsable("Go to"))
		{
			showHit((size_t)offset);
		}
	}

	// false, with a line for the status box, when the view could not be produced
	bool DataTab::viewUsable(const char * what)
	{
		if (renderFailure.empty())
		{
			return true;
		}
		emit statusMessage(tr("%1: the view cannot be shown (%2)").arg(QString::fromLatin1(what), QString::fromStdString(renderFailure)));
		return false;
	}

	// The byte offset in shownBytes of the first byte of a line (lines end in LF).
	size_t DataTab::lineStart(size_t line) const
	{
		size_t at = 0;
		for (size_t n = 0; n < line; n++)
		{
			const void * lf = memchr(shownBytes.data() + at, '\n', shownBytes.size() - at);
			if (lf == nullptr)
			{
				return shownBytes.size();
			}
			at = (const char *)lf - shownBytes.data() + 1;
		}
		return at;
	}

	// MSGDATA::moveDisplay: the line of the hit becomes the first visible line, `length` bytes from byteOffset are selected
	void DataTab::select(size_t byteOffset, size_t byteLength)
	{
		QPlainTextEdit * edit = ui->dataEdit;
		byteOffset = std::min(byteOffset, shownBytes.size());
		byteLength = std::min(byteLength, shownBytes.size() - byteOffset);
		const int textSize = edit->document()->characterCount() - 1;
		const int start = std::min((int)decode(shownBytes.data(), byteOffset).size(), textSize);
		int end = start;
		if (byteLength > 0)
		{
			end = std::min((int)decode(shownBytes.data(), byteOffset + byteLength).size(), textSize);
			if (end <= start)
			{
				end = std::min(start + 1, textSize);		// the end of the range fell inside a double-byte character
			}
		}
		QTextCursor c(edit->document());
		c.setPosition(start);
		const int line = c.blockNumber();
		c.setPosition(end, QTextCursor::KeepAnchor);
		edit->setTextCursor(c);
		edit->ensureCursorVisible();
		edit->verticalScrollBar()->setValue(line);
		edit->setFocus();
	}

	void DataTab::moveToTop(bool selectFirst)
	{
		QPlainTextEdit * edit = ui->dataEdit;
		QTextCursor c(edit->document());
		c.setPosition(0);
		if (selectFirst && edit->document()->characterCount() > 1)
		{
			c.setPosition(1, QTextCursor::KeepAnchor);
		}
		edit->setTextCursor(c);
		edit->verticalScrollBar()->setValue(0);
		edit->horizontalScrollBar()->setValue(0);
		edit->setFocus();
	}
}
