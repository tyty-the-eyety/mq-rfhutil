// IdEdit.h - the edit box of the Message Id, Correl Id and Group Id (Windows CIdEdit, IdEdit.cpp:150-290).
//
// Overtype is the default (typing replaces the character right of the caret; Insert toggles it). In hex mode only
// 0-9 and A-F are accepted, lower case is upper-cased, and anything else is refused with keyRejected() where Windows
// beeps. A paste replaces from the caret (or the selection) in place, up to the maximum length, and in hex mode turns
// every non-hex character into 0 as MQMDPAGE::editIDField does (MQMDPAGE.cpp:3709).
//
// Every change the user makes ends in textEdited(), also the ones made here instead of by QLineEdit.
//
// Threading: GUI thread only.
#pragma once

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>

namespace rfhqt
{
	class IdEdit : public QLineEdit
	{
		Q_OBJECT

	public:
		using QLineEdit::QLineEdit;

		bool hexOnly() const { return hex; }
		void setHexOnly(bool value) { hex = value; }
		bool overtype() const { return over; }
		void setOvertype(bool value) { over = value; }

	signals:
		void keyRejected();		// a key that hex mode does not accept

	protected:
		void keyPressEvent(QKeyEvent * event) override
		{
			if (isReadOnly())
			{
				QLineEdit::keyPressEvent(event);
				return;
			}
			if (event->key() == Qt::Key_Insert && (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier)
			{
				over = !over;
				event->accept();
				return;
			}
			if (event->matches(QKeySequence::Paste))
			{
				paste();
				event->accept();
				return;
			}
			const QString typed = event->text();
			const bool command = (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != 0;
			if (!command && typed.size() == 1 && typed[0].unicode() >= 0x20 && typed[0].unicode() != 0x7f)
			{
				QChar c = typed[0];
				if (hex)
				{
					c = c.toUpper();
					if (!isHex(c))
					{
						emit keyRejected();
						event->accept();
						return;
					}
				}
				typeCharacter(c);
				event->accept();
				return;
			}
			QLineEdit::keyPressEvent(event);
		}

	private:
		static bool isHex(QChar c) { return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('A') && c <= QLatin1Char('F')); }

		void replaceText(const QString & text, int caret)
		{
			setText(text);
			setCursorPosition(caret);
			emit textEdited(this->text());
		}

		void typeCharacter(QChar c)
		{
			QString current = text();
			if (hasSelectedText())
			{
				const int start = selectionStart();
				current.remove(start, selectedText().size());
				current.insert(start, c);
				replaceText(current, start + 1);
				return;
			}
			const int pos = cursorPosition();
			if (over && pos < current.size())
			{
				current[pos] = c;
			}
			else if (maxLength() > 0 && current.size() >= maxLength())
			{
				return;
			}
			else
			{
				current.insert(pos, c);
			}
			replaceText(current, pos + 1);
		}

		void paste()
		{
			QString clip = QGuiApplication::clipboard()->text();
			if (clip.isEmpty())
			{
				return;
			}
			if (hex)
			{
				for (QChar & c : clip)
				{
					c = c.toUpper();
					if (!isHex(c))
					{
						c = QLatin1Char('0');
					}
				}
			}
			QString current = text();
			int begin = hasSelectedText() ? selectionStart() : cursorPosition();
			const int end = hasSelectedText() ? begin + selectedText().size() : maxLength();
			for (int i = 0; i < clip.size() && begin < end; i++, begin++)
			{
				if (begin < current.size())
				{
					current[begin] = clip[i];
				}
				else
				{
					current.append(clip[i]);
				}
			}
			replaceText(current, begin);
		}

		bool hex = false;
		bool over = true;
	};
}
