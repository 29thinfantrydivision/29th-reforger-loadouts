//------------------------------------------------------------------------------------------------
//! 29th Infantry Division - lift rank and squad-count restrictions on group creation.
//!
//! For 29th events, roles are assigned by the unit rather than by in-game rank, and players
//! may create any number of squads. Both of those are enforced inside
//! SCR_GroupsManagerComponent, so we override the three gate methods to always pass. This is a
//! game-mode component (one instance per mode), so this single modded class covers every
//! scenario that uses it - no per-faction or per-scenario editing.
//!
//! Signatures below are taken verbatim from the 1.x SCR_GroupsManagerComponent API. If a future
//! game update changes a signature, the override silently stops applying (it becomes a new
//! method instead of an override) and the vanilla gate returns - so re-verify after major
//! patches.
//!
//! Gates lifted:
//!   1. HasPlayerRequiredRank            - authority-side rank check for a group role preset and
//!                                         its loadouts. true => SL/Crew and any future role are
//!                                         valid regardless of the player's rank.
//!   2. CanCreateGroupWithLocalPlayerRank- client-side UI check that greys a role out in the
//!                                         create-group menu. true => nothing greys out.
//!   3. AreAllGroupsMajorityFull         - the "previous group of the same type must be over
//!                                         half full" gate. true tells the game the existing
//!                                         groups are already full, so a new one is always
//!                                         permitted => create as many squads as needed.
//!
//! Deliberately NOT touched: the admin "new groups allowed" master switch
//! (SetNewGroupsAllowed / m_bNewGroupsAllowed). Leaving it alone means you can still hard-lock
//! group creation server-wide for an event if you ever want to.
//------------------------------------------------------------------------------------------------
modded class SCR_GroupsManagerComponent
{
	//--------------------------------------------------------------------------------------------
	//! Rank gate (authority). Always eligible - the 29th does not gate roles by in-game rank.
	override bool HasPlayerRequiredRank(SCR_GroupRolePresetConfig preset, int playerId, bool ignoreGroupRequiredRank)
	{
		return true;
	}

	//--------------------------------------------------------------------------------------------
	//! Rank gate (client UI). Always creatable, so nothing greys out in the create-group menu.
	override bool CanCreateGroupWithLocalPlayerRank(SCR_EGroupRole groupRole, notnull Faction faction)
	{
		return true;
	}

	//--------------------------------------------------------------------------------------------
	//! Squad-count gate. Reporting "all full" removes the half-full requirement for creating an
	//! additional group of the same type, so any number of squads can be created.
	override bool AreAllGroupsMajorityFull(SCR_EGroupRole groupRole, notnull Faction faction)
	{
		return true;
	}
}
