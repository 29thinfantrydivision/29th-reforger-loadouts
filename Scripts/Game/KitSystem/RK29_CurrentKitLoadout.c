//------------------------------------------------------------------------------------------------
//! "Current Kit" deploy-menu entry - respawns the player's stashed picker selection.
//! Resource is a placeholder body; the real kit is applied on spawn by the manager.
//------------------------------------------------------------------------------------------------
[BaseContainerProps(configRoot: true), BaseContainerCustomTitleField("m_sLoadoutName")]
class RK29_CurrentKitLoadout : SCR_FactionPlayerLoadout
{
	//--------------------------------------------------------------------------------------------
	override bool IsLoadoutAvailable(int playerId)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		return mgr && mgr.StashOfferedKit(playerId, GetFactionKey()) != "";
	}

	//--------------------------------------------------------------------------------------------
	override bool IsLoadoutAvailableClient()
	{
		return RK29_KitPicker.HasLocalStash();
	}

	//--------------------------------------------------------------------------------------------
	//! Any machine with a local player renders the stashed kit's body (deploy preview)
	//! instead of the placeholder - listen hosts included. Spawn correctness never
	//! depends on this resource: Current Kit spawns are fully re-dressed from the stash.
	override ResourceName GetLoadoutResource()
	{
		if (!System.IsConsoleApp())
		{
			string kitName = RK29_KitPicker.LocalStashKit();
			if (kitName != "")
			{
				RK29_KitManager mgr = RK29_KitManager.GetInstance();
				if (mgr)
				{
					RK29_KitStruct kit = mgr.m_mKits.Get(kitName);
					if (kit && kit.m_sSourcePrefab != ResourceName.Empty && kit.m_sFactionKey == GetFactionKey())
						return kit.m_sSourcePrefab;
				}
			}
		}
		return super.GetLoadoutResource();
	}
}
