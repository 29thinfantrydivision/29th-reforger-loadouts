//------------------------------------------------------------------------------------------------
//! Identity for one stamped row - which entry, tile or preset it stands for - since nothing on
//! the widget itself says. Clicks arrive through the row's SCR_ModularButtonComponent, which
//! carries this object as user data and calls the menu back; that component is what buys the
//! hover tint, focus ring and UI sounds. What this class handles directly is what that path cannot
//! carry: the two edit boxes, which have no component and are attached the plain way for OnChange,
//! and the press and release under a click, for the rows that need the mouse button or a drag
//! handle - see RK29_LoadoutMenu.AttachClickHandler.
class RK29_LoadoutRowHandler : ScriptedWidgetEventHandler
{
	RK29_LoadoutMenu m_Menu;

	RK29_EMenuRowKind m_eKind;
	int m_iIndex;

	//! Whether this handler is also ADDED to its widget, and so hears the mouse directly. False for
	//! the ordinary wiring, where this object is only the identity parked as the modular button's
	//! user data and raises no events of its own. It also keeps the two edit boxes out of the mouse
	//! events: they are attached the plain way, for OnChange.
	bool m_bMouseAware;

	//------------------------------------------------------------------------------------------------
	//! The press under the click. It carries the mouse button, which the click cannot: the component
	//! takes the button and drops it, and m_OnClicked has no trace of it - so a right-click used to
	//! arrive as an ordinary one and load the kit. The press is also where a drag is armed, Enfusion
	//! raising no drag events of its own.
	//!
	//! DO NOT move either job to an OnClick override on this handler. SCR_ModularButtonComponent
	//! calls SetFocusedWidget inside ITS OnClick whenever the widget is not already focused, and that
	//! focus change costs every handler after it their OnClick for that press: the first click on
	//! each glyph was lost and only a second, already-focused click opened its menu. The press runs
	//! before any of it.
	override bool OnMouseButtonDown(Widget w, int x, int y, int button)
	{
		if (!m_bMouseAware || !m_Menu)
			return false;

		m_Menu.NoteMouseButton(button);
		m_Menu.OnRowPressed(m_eKind, m_iIndex, button);
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Raised on the widget the press began on as well as the one under the cursor, which is what
	//! lets a drag be released anywhere and still be finished by the handle it started from.
	override bool OnMouseButtonUp(Widget w, int x, int y, int button)
	{
		if (m_bMouseAware && m_Menu)
			m_Menu.OnRowReleased(m_eKind, m_iIndex, button);

		return false;
	}

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
