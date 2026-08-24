//------------------------------------------------------------------------------------------------
//! Deploy-menu row identity for "Current Kit".
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

//! The row icon read by the loadout list and the expand button.
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
