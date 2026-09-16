//------------------------------------------------------------------------------------------------
//! Identity for one stamped row - which entry, tile or preset it stands for - since nothing on
//! the widget itself says. Clicks arrive through the row's SCR_ModularButtonComponent, which
//! carries this object as user data and calls the menu back; that component is what buys the
//! hover tint, focus ring and UI sounds. What this class handles directly is the two edit boxes,
//! which carry no component and are attached the plain way, for OnChange.
class RK29_LoadoutRowHandler : ScriptedWidgetEventHandler
{
	RK29_LoadoutMenu m_Menu;

	RK29_EMenuRowKind m_eKind;
	int m_iIndex;

	//------------------------------------------------------------------------------------------------
	//! finished is false per keystroke and true on commit (enter, or the box losing focus); only the
	//! commit is acted on, so a half-typed count is not clamped and a preset is not saved per
	//! keystroke. Answers false so the edit box keeps its own handling. The character cap runs on
	//! every keystroke because vanilla's EditBoxFilterComponent cannot work on any widget - see
	//! RK29_MenuDetailPanel.CapEditLength. Menu-driven SetText raises this with finished true: hence
	//! the IsEditEcho drop, without which normalising a box would save a preset mid-typing.
	override bool OnChange(Widget w, bool finished)
	{
		if (!m_Menu || m_Menu.Detail().IsEditEcho())
			return false;

		if (m_eKind == RK29_EMenuRowKind.COUNT_EDIT)
			m_Menu.Detail().CapEditLength(w, RK29_MenuDetailPanel.COUNT_EDIT_MAX_CHARS);
		else if (m_eKind == RK29_EMenuRowKind.PRESET_SAVE_EDIT)
			m_Menu.Detail().CapEditLength(w, RK29_KitPresetStorage.MAX_NAME_LENGTH);

		if (!finished)
			return false;

		if (m_eKind == RK29_EMenuRowKind.COUNT_EDIT)
			m_Menu.Detail().OnCountEdited(m_iIndex, w);
		else if (m_eKind == RK29_EMenuRowKind.PRESET_SAVE_EDIT)
			m_Menu.Info().OnPresetNameCommitted(w);

		return false;
	}
}
