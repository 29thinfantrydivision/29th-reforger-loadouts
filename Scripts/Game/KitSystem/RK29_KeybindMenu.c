//------------------------------------------------------------------------------------------------
//! Keeps the keybind backup in step with the vanilla controls settings. The rows themselves come
//! from Configs/System/keyBindingMenu.conf - DO NOT build them in script: a row is only
//! rebindable when its m_sPreset names a FilterPreset on the action's source.
//------------------------------------------------------------------------------------------------
modded class SCR_KeybindSetting
{
	//------------------------------------------------------------------------------------------------
	override protected void InsertCategoriesToComboBox()
	{
		// settings can be opened from the main menu before any world init runs the restore
		RK29_KeybindPrefs.RestoreOnce();
		super.InsertCategoriesToComboBox();
	}

	//------------------------------------------------------------------------------------------------
	//! The one moment every edit of our rows funnels through - mirror engine state into the backup.
	override void OnTabHide()
	{
		super.OnTabHide();
		RK29_KeybindPrefs.SyncFromEngine();
	}
}
