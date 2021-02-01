#pragma once

#include <vector>
#include "win32/Window.h"
#include "win32/ListView.h"
#include "ELF.h"

class CELFSymbolView : public Framework::Win32::CWindow
{
public:
	CELFSymbolView(HWND, CELF*);
	virtual ~CELFSymbolView();

private:
	struct ITEM
	{
		std::tstring name;
		std::tstring address;
		std::tstring size;
		std::tstring type;
		std::tstring binding;
		std::tstring section;
	};

	typedef std::vector<ITEM> ItemArray;

	enum SORT_STATE
	{
		SORT_STATE_NONE,
		SORT_STATE_NAME_ASC,
		SORT_STATE_NAME_DESC,
		SORT_STATE_ADDRESS_ASC,
		SORT_STATE_ADDRESS_DESC,
	};

	long OnSize(unsigned int, unsigned int, unsigned int) override;
	LRESULT OnNotify(WPARAM, NMHDR*) override;

	static int ItemNameComparer(const ITEM&, const ITEM&);
	static int ItemAddressComparer(const ITEM&, const ITEM&);

	void RefreshLayout();
	void PopulateList();
	void GetItemInfo(LVITEM*) const;

	CELF* m_pELF;
	Framework::Win32::CListView* m_listView;
	ItemArray m_items;

	SORT_STATE m_sortState;
};