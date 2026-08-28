//------------------------------------------------------------------------------------------------
//! The loadout menu's own dialog, so ESC can mean "back" before it means "close". The preset's
//! "cancel" button binds to SCR_ConfigurableDialogUi.OnCancel and there is no earlier hook - the
//! proxy menu handles no back action of its own. CreateFromPreset's third argument adopts this
//! object as the dialog, because ConfigurableDialog.layout authors no dialog handler on its root.
//------------------------------------------------------------------------------------------------
class RK29_LoadoutDialog : SCR_ConfigurableDialogUi
{
	//! Not a ref: the menu owns the dialog, and a reference back would be an unfreeable cycle.
	RK29_LoadoutMenu m_Menu;

	//------------------------------------------------------------------------------------------------
	//! ESC and the Close button both land here. With a group open the menu takes the press and goes
	//! back to the mannequin; otherwise the vanilla path runs unchanged.
	override protected void OnCancel()
	{
		if (m_Menu && m_Menu.RetreatFromDetail())
			return;

		super.OnCancel();
	}

	//------------------------------------------------------------------------------------------------
	//! The engine closed the menu (a death opening the deploy screen, round end). That path never
	//! reaches Internal_Close so m_OnClose never fires, leaving the mannequin in the world and the
	//! camera handler on the workspace. The proxy forwards its close here, the one hook there is.
	override void OnMenuClose()
	{
		super.OnMenuClose();
		if (m_Menu)
			m_Menu.OnDialogClosedByEngine();
	}
}
