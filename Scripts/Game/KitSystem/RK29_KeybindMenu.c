//------------------------------------------------------------------------------------------------
//! Exposes the kit menu bind in the vanilla controls settings (SPEC29_KeybindMenu pattern).
//! Additive - stacks with the spectator's own modding of this class.
//------------------------------------------------------------------------------------------------
modded class SCR_KeybindSetting
{
	//--------------------------------------------------------------------------------------------
	override protected void InsertCategoriesToComboBox()
	{
		RK29_AppendKitCategory();
		super.InsertCategoriesToComboBox();
	}

	//--------------------------------------------------------------------------------------------
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

		m_KeybindConfig.m_KeyBindingCategories.Insert(cat);
	}

	//--------------------------------------------------------------------------------------------
	protected void RK29_AddEntry(SCR_KeyBindingCategory cat, string actionName, string displayName)
	{
		SCR_KeyBindingEntry entry = new SCR_KeyBindingEntry();
		entry.m_sActionName  = actionName;
		entry.m_sDisplayName = displayName;
		entry.m_aPlatforms = {};
		cat.m_KeyBindingEntries.Insert(entry);
	}
}
