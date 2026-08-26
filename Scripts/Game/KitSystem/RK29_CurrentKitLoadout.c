//------------------------------------------------------------------------------------------------
//! "Current Kit" deploy-menu entry - respawns the player's stashed picker selection, and the
//! side's default kit (Rifleman) for a player who has not picked one yet.
//!
//! With the per-role deploy rows dropped this is how almost everybody spawns, so it is always
//! offered rather than gated on having used the picker: a squad with kits always has a row, and
//! that row always resolves to a real kit. The row's icon and label follow the kit it will
//! actually spawn, not the literal loadout name - see RK29_LoadoutRowIcon.c.
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
	//! Available whenever this side's squad has any kit to give - which, with the per-role
	//! deploy rows gone, is the whole point: Current Kit is how a player deploys at all, so it
	//! cannot be gated on having already used the picker. EffectiveKitFor() resolves the side
	//! default (Rifleman) for a player with no stash, and "" only for a squad offered nothing.
	//!
	//! Still faction-scoped. The row is per faction and the caller already filters by faction,
	//! but a stash belongs to the side it was applied on, so asking with our own key keeps the
	//! OTHER side's row from answering with a kit it cannot spawn.
	override bool IsLoadoutAvailable(int playerId)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		return mgr && mgr.EffectiveKitFor(playerId, GetFactionKey()) != "";
	}

	//--------------------------------------------------------------------------------------------
	//! Client mirror of the gate above, resolved the same way so the row the player sees and the
	//! row the server honours cannot disagree.
	override bool IsLoadoutAvailableClient()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return false;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return false;

		return mgr.EffectiveKitFor(pc.GetPlayerId(), GetFactionKey()) != "";
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

		// Hand our own faction over: a first-deploy player has no stash to read a side off, and
		// the row that was clicked is the only thing that knows which side they picked.
		if (!mgr.ApplyStashOnSpawn_S(playerId, pOwner, GetFactionKey()))
			Print("[RK29] Current Kit spawned with no usable stash - body stays bare (player "
				+ playerId.ToString() + ")", LogLevel.WARNING);
	}
}
