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
	//! The confirm button plays its own click sound, and on the mouse path it plays it AFTER the
	//! activation it fires - so a refused Apply sounded like an accepted one over the top of its own
	//! refusal. Muting the button leaves OnConfirm to say which sound the press earned.
	override void OnMenuOpen(SCR_ConfigurableDialogUiPreset preset)
	{
		super.OnMenuOpen(preset);

		SCR_InputButtonComponent confirm = FindButton(BUTTON_CONFIRM);
		if (confirm)
			confirm.SetClickedSound(string.Empty);
	}

	//------------------------------------------------------------------------------------------------
	//! The Apply Kit button and the RK29_DialogApply keybind both land here, and vanilla's OnConfirm
	//! closes the dialog after invoking m_OnConfirm - so a kit the menu will not issue has to be
	//! turned away before the base call, not inside the confirm handler. The menu says why on screen.
	override protected void OnConfirm()
	{
		if (m_Menu && m_Menu.RefuseApply())
			return;

		SCR_UISoundEntity.SoundEvent(SCR_SoundEvent.CLICK);
		super.OnConfirm();
	}

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
