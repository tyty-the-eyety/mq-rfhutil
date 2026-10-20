// UsrPropTab.h - the Usr Prop tab for user-defined message properties.
// Threading: GUI thread only.
#pragma once

#include <QWidget>
#include <memory>
#include <string>
#include <vector>

namespace Ui { class UsrPropTab; }

namespace rfhqt
{
	class AppDocument;

	struct UserProperty
	{
		std::string name;
		std::string type;
		std::string value;
	};

	class UsrPropTab : public QWidget
	{
		Q_OBJECT
	public:
		explicit UsrPropTab(AppDocument & document, QWidget * parent = nullptr);
		~UsrPropTab() override;

		void setProperties(const std::vector<UserProperty> & props);
		std::vector<UserProperty> properties() const;

	signals:
		void fieldsEdited();

	private:
		void wire();
		void addRow();
		void removeRow();
		void tableEdited();

		AppDocument & doc;
		std::unique_ptr<Ui::UsrPropTab> ui;
	};
}
