// PsTab.h - the PS (pub/sub settings) tab.
// Threading: GUI thread only.
#pragma once

#include <QWidget>
#include <memory>
#include <string>

namespace Ui { class PsTab; }

namespace rfhqt
{
	class AppDocument;

	struct PsSettings
	{
		std::string topic;
		std::string subName;
		std::string subQm;
		std::string pubQueue;
	};

	class PsTab : public QWidget
	{
		Q_OBJECT
	public:
		explicit PsTab(AppDocument & document, QWidget * parent = nullptr);
		~PsTab() override;

		void setFields(const PsSettings & s);
		PsSettings fields() const;

	signals:
		void fieldsEdited();

	private:
		void wire();

		AppDocument & doc;
		std::unique_ptr<Ui::PsTab> ui;
	};
}
