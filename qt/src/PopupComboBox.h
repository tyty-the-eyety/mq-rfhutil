// PopupComboBox.h - a combo box that says when its list is about to open, so the owner can refill it first
// (Windows CBN_DROPDOWN: loadQMComboBox, OnDropdownQName).
#pragma once

#include <QComboBox>

namespace rfhqt
{
	class PopupComboBox : public QComboBox
	{
		Q_OBJECT

	public:
		using QComboBox::QComboBox;

		void showPopup() override
		{
			emit aboutToShowPopup();
			QComboBox::showPopup();
		}

	signals:
		void aboutToShowPopup();
	};
}
