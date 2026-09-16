//------------------------------------------------------------------------------------------------
//! SCR_GroupsManagerComponent changes: the group-creation gates the 29th lifts, and the radio
//! tune a live re-kit asks for. The three gate overrides always pass, and their signatures are
//! verbatim from the 1.x API - if a patch changes one, the override silently becomes a new method
//! and the vanilla gate returns. The admin master switch (SetNewGroupsAllowed) is deliberately
//! left alone.
//!
//! Radio: vanilla tunes only at spawn, via TunePlayersFrequency (GetOnPlayerSpawned) and
//! TuneAgentsRadio (group.GetOnAgentAdded - reads as an AI path but fires for players too). Both
//! write the first RADIO gadget's GetTransceiver(0) server-side; neither touches a second
//! transceiver or RADIO_BACKPACK, so a manpack keeps its authored channels. Nothing re-tunes a
//! radio that arrives later - SCR_VONAutoTune's hook is a no-op, it inserts a method name the
//! class does not define - so a re-kit must ask explicitly. Spawns need nothing: OnLoadoutSpawned
//! runs before m_OnPlayerSpawned.Invoke().
//------------------------------------------------------------------------------------------------
modded class SCR_GroupsManagerComponent
{
	//------------------------------------------------------------------------------------------------
	override bool HasPlayerRequiredRank(SCR_GroupRolePresetConfig preset, int playerId, bool ignoreGroupRequiredRank)
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanCreateGroupWithLocalPlayerRank(SCR_EGroupRole groupRole, notnull Faction faction)
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Reporting "all full" drops the "previous group of this type must be over half full" gate.
	override bool AreAllGroupsMajorityFull(SCR_EGroupRole groupRole, notnull Faction faction)
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla's own method rather than a copy of it, so a re-kit and a spawn cannot drift apart.
	//! Authority only. Either tune switch being on is enough - both spawn paths write the same
	//! transceiver the same way - but a scenario that turned group tuning off keeps it off.
	void RK29_TuneToGroupFrequency_S(int playerId, IEntity body)
	{
		if (IsProxy() || !body)
			return;

		if (!m_bTunePlayersRadioToGroupFrequency && !m_bTuneAgentsRadioToGroupFrequency)
			return;

		TunePlayersFrequency(playerId, body);
	}
}
