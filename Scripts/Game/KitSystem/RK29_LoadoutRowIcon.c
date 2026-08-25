//------------------------------------------------------------------------------------------------
//! Deploy-menu row identity for "Current Kit", and the list rebuild that makes the row exist.
//!
//! Vanilla builds a loadout row's icon straight from its resource prefab: load the resource,
//! find SCR_EditableCharacterComponent, read m_UIInfo. That bypasses the kit system entirely -
//! and Current Kit's resource is the stashed kit's BODY, which for a picker-only class is the
//! side's shared body. A stashed medic therefore drew the rifleman's icon.
//!
//! The stash already knows which kit it is, and the composed kit already carries the config
//! identity, so hand that over instead. Every other loadout row is left to vanilla: its
//! resource IS its kit, so reading the prefab is correct there.
//------------------------------------------------------------------------------------------------

//! The row icon read by the loadout list and the expand button, and the list rebuild that puts
//! a newly-available row in front of the player without waiting for the menu to be reopened.
modded class SCR_LoadoutRequestUIComponent
{
	//--------------------------------------------------------------------------------------------
	override SCR_EditableEntityUIInfo GetUIInfo(SCR_BasePlayerLoadout loadout)
	{
		SCR_EditableEntityUIInfo stashed = RK29_StashedLoadoutUIInfo.Resolve(loadout);
		if (stashed)
			return stashed;
		return super.GetUIInfo(loadout);
	}

	//--------------------------------------------------------------------------------------------
	//! Rebuild the loadout gallery against IsLoadoutAvailableClient() as it reads NOW.
	//!
	//! Vanilla runs ShowAvailableLoadouts() on menu open, faction change and group change, and
	//! nowhere else - so a loadout that BECOMES available while the menu is up never gets a row.
	//! That is Current Kit's entire life cycle: a player who dies with no stash opens the deploy
	//! menu, applies a kit in the picker, and the row it just earned is never created. The server
	//! has the stash by then, so deploying without touching anything still spawns the kit - the
	//! entry simply is not there to click. A row that already exists is just as stale: its icon
	//! is read once, in SCR_LoadoutButton.SetLoadout(), so re-applying a different kit leaves the
	//! old class icon on the row.
	//!
	//! Deliberately NOT a call to ShowAvailableLoadouts(). Its tail re-requests a loadout, and
	//! prefers the arsenal entry over whatever is currently assigned - running that here would
	//! take the kit the player just applied straight back off them. Only the list is rebuilt;
	//! the selection stays where the server put it.
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

//! The per-row button, which resolves its own icon off the loadout it was given.
modded class SCR_LoadoutButton
{
	//--------------------------------------------------------------------------------------------
	override SCR_EditableEntityUIInfo GetUIInfo()
	{
		SCR_EditableEntityUIInfo stashed = RK29_StashedLoadoutUIInfo.Resolve(GetLoadout());
		if (stashed)
			return stashed;
		return super.GetUIInfo();
	}
}

//------------------------------------------------------------------------------------------------
class RK29_StashedLoadoutUIInfo
{
	//--------------------------------------------------------------------------------------------
	//! Config identity of the stashed kit, or null for anything that is not Current Kit - which
	//! leaves vanilla's prefab read in charge of every ordinary loadout row.
	static SCR_EditableEntityUIInfo Resolve(SCR_BasePlayerLoadout loadout)
	{
		if (!loadout || !RK29_CurrentKitLoadout.Cast(loadout) || !RK29_KitPicker.HasLocalStash())
			return null;

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return null;

		RK29_KitStruct kit = mgr.m_mKits.Get(RK29_KitPicker.LocalStashKit());
		if (!kit)
			return null;

		return SCR_EditableEntityUIInfo.Cast(kit.m_UIInfo);
	}
}
