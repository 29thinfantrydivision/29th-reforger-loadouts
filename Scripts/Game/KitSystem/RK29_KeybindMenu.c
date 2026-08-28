//------------------------------------------------------------------------------------------------
//! Exposes the RK29 binds in the vanilla controls settings (SPEC29_KeybindMenu pattern).
//! Additive - stacks with the spectator mod's own modding of this class.
//------------------------------------------------------------------------------------------------
modded class SCR_KeybindSetting
{
	//------------------------------------------------------------------------------------------------
	override protected void InsertCategoriesToComboBox()
	{
		// settings can be opened from the main menu before any world init runs the restore
		RK29_KeybindPrefs.RestoreOnce();
		RK29_AppendKitCategory();
		super.InsertCategoriesToComboBox();
	}

	//------------------------------------------------------------------------------------------------
	//! The one moment every edit of our rows funnels through - mirror engine state into the backup.
	override void OnTabHide()
	{
		super.OnTabHide();
		RK29_KeybindPrefs.SyncFromEngine();
	}

	//------------------------------------------------------------------------------------------------
	protected void RK29_AppendKitCategory()
	{
		if (!m_KeybindConfig || !m_KeybindConfig.m_KeyBindingCategories)
			return;

		foreach (SCR_KeyBindingCategory existing : m_KeybindConfig.m_KeyBindingCategories)
		{
			if (existing && existing.m_sName == "rk29")
				return;
		}

		SCR_KeyBindingCategory cat = new SCR_KeyBindingCategory();
		cat.m_sName = "rk29";
		cat.m_sDisplayName = "Kits - 29th ID";
		cat.m_KeyBindingEntries = {};

		// first separator carries the column headers
		RK29_AddEntry(cat, "separator",          "Kits - 29th ID");
		RK29_AddEntry(cat, "RK29_ToggleKitMenu", "Kit menu");
		RK29_AddEntry(cat, "RK29_DialogApply",   "Apply kit");
		RK29_AddEntry(cat, "RK29_StepFive",      "Count step x5 (hold)");
		RK29_AddEntry(cat, "RK29_StepTen",       "Count step x10 (hold)");

		m_KeybindConfig.m_KeyBindingCategories.Insert(cat);
	}

	//------------------------------------------------------------------------------------------------
	protected void RK29_AddEntry(SCR_KeyBindingCategory cat, string actionName, string displayName)
	{
		SCR_KeyBindingEntry entry = new SCR_KeyBindingEntry();
		entry.m_sActionName  = actionName;
		entry.m_sDisplayName = displayName;
		entry.m_aPlatforms = {};
		cat.m_KeyBindingEntries.Insert(entry);
	}
}
