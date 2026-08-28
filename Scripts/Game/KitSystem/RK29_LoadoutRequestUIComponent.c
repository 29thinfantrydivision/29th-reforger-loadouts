//------------------------------------------------------------------------------------------------
//! Deploy-menu row identity for "Current Kit" - icon and label - and the list rebuild that makes
//! the row exist. Vanilla builds a row's icon from its resource prefab
//! (SCR_EditableCharacterComponent m_UIInfo), which for a picker-only class is the side's shared
//! body, so a stashed medic drew the rifleman's icon; the label is the loadout's name, so every
//! kit read "Current Kit". Every other row is left to vanilla: its resource is its kit.
//------------------------------------------------------------------------------------------------

modded class SCR_LoadoutRequestUIComponent
{
	//------------------------------------------------------------------------------------------------
	override SCR_EditableEntityUIInfo GetUIInfo(SCR_BasePlayerLoadout loadout)
	{
		SCR_EditableEntityUIInfo stashed = RK29_StashedLoadoutUIInfo.Resolve(loadout);
		if (stashed)
			return stashed;
		return super.GetUIInfo(loadout);
	}

	//------------------------------------------------------------------------------------------------
	//! Row label, stamped over vanilla's after the fact. GetLoadoutName() cannot be the hook: it is
	//! the loadout's identity, matched on by the kit manager and shared by every player on the
	//! machine, while a Current Kit row is per player and side. All four callers below are
	//! load-bearing; none duplicates another (HandlerAttached writes m_wExpandButtonName directly;
	//! RequestPlayerLoadout reaches SetLoadoutPreview, which self-skips on keyboard;
	//! SetLoadoutPreview writes all three and is the hover handler; RefreshLoadoutPreview writes
	//! m_wLoadoutName again after it).
	protected void RK29_StampRowName(SCR_BasePlayerLoadout loadout)
	{
		string label = RK29_StashedLoadoutUIInfo.ResolveName(loadout);
		if (label == "")
			return;

		if (m_wLoadoutName)
			m_wLoadoutName.SetText(label);
		if (m_wLoadoutNameText)
			m_wLoadoutNameText.SetText(label);
		if (m_wExpandButtonName)
			m_wExpandButtonName.SetText(label);
	}

	//------------------------------------------------------------------------------------------------
	override void HandlerAttached(Widget w)
	{
		super.HandlerAttached(w);

		// vanilla's HandlerAttached bails out before assigning m_PlyLoadoutComp when the world has no
		// loadout manager or player controller yet, and GetPlayerLoadout() derefs it unguarded
		if (!m_PlyLoadoutComp)
			return;

		RK29_StampRowName(GetPlayerLoadout());
	}

	//------------------------------------------------------------------------------------------------
	override protected void RequestPlayerLoadout(SCR_BasePlayerLoadout loadout)
	{
		super.RequestPlayerLoadout(loadout);
		RK29_StampRowName(loadout);
	}

	//------------------------------------------------------------------------------------------------
	//! Mirrors vanilla's own two gates. Without them a hover would relabel the row on mouse and
	//! keyboard, where vanilla deliberately leaves the preview - and its name - alone.
	override protected void SetLoadoutPreview(SCR_BasePlayerLoadout loadout)
	{
		super.SetLoadoutPreview(loadout);

		InputManager inputManager = GetGame().GetInputManager();
		if (inputManager && inputManager.GetLastUsedInputDevice() == EInputDeviceType.KEYBOARD)
			return;

		if (!m_PreviewComp || !loadout)
			return;

		RK29_StampRowName(loadout);
	}

	//------------------------------------------------------------------------------------------------
	override void RefreshLoadoutPreview()
	{
		// vanilla's first line reads m_PlyLoadoutComp unguarded, and it is null whenever the
		// handler attached before the player controller existed
		if (!m_PlyLoadoutComp)
			return;

		super.RefreshLoadoutPreview();

		SCR_BasePlayerLoadout loadout = m_PlyLoadoutComp.GetLoadout();
		RK29_StampRowName(loadout);

		// SetLoadoutPreview() is also the gallery's hover handler, so it early-returns while the last
		// input was the keyboard; RefreshLoadoutPreview() reuses it for an explicit refresh and inherits
		// the guard, leaving the mannequin on the previous kit
		InputManager inputManager = GetGame().GetInputManager();
		if (!inputManager || inputManager.GetLastUsedInputDevice() != EInputDeviceType.KEYBOARD)
			return;

		if (!m_PreviewComp || !loadout)
			return;

		m_PreviewedEntity = m_PreviewComp.SetPreviewedLoadout(loadout);
		if (m_wLoadoutPreview)
			m_wLoadoutPreview.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! Rebuild the loadout gallery against IsLoadoutAvailableClient() as it reads now. Vanilla runs
	//! ShowAvailableLoadouts() on menu open, faction change and group change only, so a loadout that
	//! becomes available while the menu is up never gets a row, and an existing row's icon is read
	//! once in SCR_LoadoutButton.SetLoadout(). Deliberately not a call to ShowAvailableLoadouts():
	//! its tail re-requests a loadout and prefers the arsenal entry, taking the kit just applied
	//! straight back off the player.
	void RK29_RefreshLoadoutList()
	{
		if (!m_LoadoutManager || !m_LoadoutSelector || !m_PlyFactionAffilComp || !m_PlyLoadoutComp)
			return;

		Faction faction = m_PlyFactionAffilComp.GetAffiliatedFaction();
		if (!faction)
			return;

		// same source split vanilla uses: group-scoped roles when the faction configures them
		SCR_AIGroup group;
		if (m_PlayerControllerGroupComponent)
			group = m_PlayerControllerGroupComponent.GetPlayersGroup();

		SCR_Faction scrFaction = SCR_Faction.Cast(faction);
		array<ref SCR_BasePlayerLoadout> loadouts = {};
		if (group && scrFaction && scrFaction.IsGroupRolesConfigured())
			m_LoadoutManager.GetPlayerLoadoutsByGroup(group, faction, loadouts);
		else
			m_LoadoutManager.GetPlayerLoadoutsByFaction(faction, loadouts);

		m_LoadoutSelector.ClearAll();
		foreach (SCR_BasePlayerLoadout loadout : loadouts)
		{
			if (loadout && loadout.IsLoadoutAvailableClient())
				m_LoadoutSelector.AddItem(loadout, true);
		}

		// ClearAll took the highlight with the button that carried it
		SCR_BasePlayerLoadout assigned = m_PlyLoadoutComp.GetLoadout();
		if (assigned)
			m_LoadoutSelector.SetSelected(assigned);
	}
}
