//------------------------------------------------------------------------------------------------
//! "Current Kit" deploy-menu entry - respawns the player's stashed picker selection.
//!
//! The body it spawns is the side's BARE base: no dress, no weapons, no stock inventory. The kit
//! is applied in OnLoadoutSpawned, the same hook vanilla's arsenal uses. That is what makes this
//! entry honest - it spawns the saved loadout rather than spawning somebody else's kit and
//! stripping it a fraction of a second later, which is what forced the old settle timer and the
//! duplicate-item race it was guessing at.
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
	//! Faction-scoped, like the server's own gate: a stash is for the faction it was applied
	//! on, so the OTHER faction's Current Kit entry must not offer it. Without this the entry
	//! appears for a side the player has never kitted on, previewing a placeholder body.
	override bool IsLoadoutAvailableClient()
	{
		string kitName = RK29_KitPicker.LocalStashKit();
		if (kitName == "")
			return false;

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return false;

		RK29_KitStruct kit = mgr.m_mKits.Get(kitName);
		return kit && kit.m_sFactionKey == GetFactionKey();
	}

	//--------------------------------------------------------------------------------------------
	//! The side's bare body, both for the spawn and for the deploy mannequin. Neither inherits
	//! anything from it: the spawn is dressed in OnLoadoutSpawned and the mannequin from the
	//! loadout the server sent. Borrowing another kit's dressed prefab here is exactly how the
	//! preview ended up showing the wrong gun.
	override ResourceName GetLoadoutResource()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (mgr)
		{
			ResourceName body = mgr.SideBody(GetFactionKey());
			if (body != ResourceName.Empty)
				return body;
		}
		return super.GetLoadoutResource();
	}

	//--------------------------------------------------------------------------------------------
	//! Dress the body the instant it exists, before the player is ever shown it. Same hook
	//! SCR_PlayerArsenalLoadout applies its saved loadout in.
	override void OnLoadoutSpawned(GenericEntity pOwner, int playerId)
	{
		super.OnLoadoutSpawned(pOwner, playerId);

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return;

		if (!mgr.ApplyStashOnSpawn_S(playerId, pOwner))
			Print("[RK29] Current Kit spawned with no usable stash - body stays bare (player "
				+ playerId.ToString() + ")", LogLevel.WARNING);
	}
}
