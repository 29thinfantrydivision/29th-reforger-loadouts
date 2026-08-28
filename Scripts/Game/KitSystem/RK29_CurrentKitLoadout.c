//------------------------------------------------------------------------------------------------
//! "Current Kit" deploy-menu entry - respawns the player's stashed picker selection, or the
//! side's default kit for a player who has not picked one yet. Always offered rather than gated
//! on having used the picker, since it is how almost everybody spawns. The body it spawns is the
//! side's BaseLoadout and the kit is applied over it in OnLoadoutSpawned, the same hook vanilla's
//! arsenal uses. Dressed rather than bare on purpose: on a dedicated server the apply lands about
//! a second after the body. It brings no weapon, so the owner-side draw at possession is still
//! what puts a rifle in the player's hands.
//------------------------------------------------------------------------------------------------
[BaseContainerProps(configRoot: true), BaseContainerCustomTitleField("m_sLoadoutName")]
class RK29_CurrentKitLoadout : SCR_FactionPlayerLoadout
{
	//------------------------------------------------------------------------------------------------
	//! Available whenever this side has any kit to give - EffectiveKitFor() answers "" only for a
	//! side with no kits. Asked with our own faction key: a stash belongs to the side it was applied
	//! on, which is what keeps the other side's row from answering with a kit it cannot spawn.
	override bool IsLoadoutAvailable(int playerId)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		return mgr && mgr.EffectiveKitFor(playerId, GetFactionKey()) != "";
	}

	//------------------------------------------------------------------------------------------------
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

	//------------------------------------------------------------------------------------------------
	//! The side's base body, for the spawn and for the deploy mannequin; both strip first. The
	//! authored m_sLoadoutResource is not a fallback for this - it is the row's group membership key:
	//! IsLoadoutInGroup tests m_aLoadoutResources.Contains(GetDefaultLoadoutResource()), the raw
	//! field. Change it and the Current Kit row vanishes from the respawn menu once the player is in
	//! a squad.
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

	//------------------------------------------------------------------------------------------------
	//! Dress the body the instant it exists, before the player is shown it. Same hook
	//! SCR_PlayerArsenalLoadout applies its saved loadout in.
	override void OnLoadoutSpawned(GenericEntity pOwner, int playerId)
	{
		super.OnLoadoutSpawned(pOwner, playerId);

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return;

		// our own faction key: a first-deploy player has no stash to read a side off, and the row
		// that was clicked is the only thing that knows which side they picked

		// the warning below covers three causes - not the server, no body, or no kit under the
		// stashed name. The fourth, a hard apply failure, names itself inside ApplyStashOnSpawn_S.
		bool logged;
		if (!mgr.ApplyStashOnSpawn_S(playerId, pOwner, GetFactionKey(), logged) && !logged)
			Print(string.Format(
				"[RK29] Current Kit spawned with no usable stash - body stays bare (player %1,"
				+ " faction '%2')", playerId, GetFactionKey()), LogLevel.WARNING);
	}
}
