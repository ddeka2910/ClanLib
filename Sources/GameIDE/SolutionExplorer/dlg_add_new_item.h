/*
**  ClanLib SDK
**  Copyright (c) 1997-2012 The ClanLib Team
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
**  Note: Some of the libraries ClanLib may link to may have additional
**  requirements or restrictions.
**
**  File Author(s):
**
**    Magnus Norddahl
*/
#pragma once

#include "../FileItemType/file_item_type_factory.h"
namespace clan
{
class DlgAddNewItem : public clan::GUIComponent
{
public:
	DlgAddNewItem(clan::GUIComponent *owner, FileItemTypeFactory &factory, const std::string &default_location);

	std::string get_name() const;
	std::string get_location() const;
	FileItemType *get_fileitemtype() const;

private:
	void on_resized();
	bool on_close();
	void on_button_ok_clicked();
	void on_button_cancel_clicked();
	void on_button_browse_location_clicked();
	void on_list_items_selection_changed(clan::ListViewSelection item);

	void populate(FileItemTypeFactory &factory);

	static clan::GUITopLevelDescription get_toplevel_description();

	clan::Label *label_name;
	clan::LineEdit *lineedit_name;

	clan::Label *label_location;
	clan::LineEdit *lineedit_location;
	clan::PushButton *button_browse_location;
	
	clan::ListView *list_items;
	clan::Label *label_description_header;
	clan::Label *label_description;

	clan::PushButton *button_ok;
	clan::PushButton *button_cancel;

	class ListViewItemData : public clan::ListViewItemUserData
	{
	public:
		ListViewItemData(FileItemType *type) : type(type) { }
		FileItemType *type;
	};
	typedef std::shared_ptr<ListViewItemData> ListViewItemDataPtr;
};

}
