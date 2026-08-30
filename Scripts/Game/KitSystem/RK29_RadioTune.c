//------------------------------------------------------------------------------------------------
//! Give a re-kitted body the radio tune a spawn would have given it.
//!
//! Vanilla tunes a player's radio only at spawn, by two paths in SCR_GroupsManagerComponent that
//! do the same thing:
//!
//!   - TunePlayersFrequency(), hooked onto SCR_BaseGameMode.GetOnPlayerSpawned() in EOnInit and
//!     gated on the authored "tune players radio to group frequency" switch.
//!   - TuneAgentsRadio(), off group.GetOnAgentAdded() and gated on the "tune agents radio"
//!     switch. It reads as an AI path but it fires for players too - a player's agent joins the
//!     group through SCR_AIGroup.QueueAddAgent(), which waits for GetOnPlayerSpawned() and then
//!     for gadget init, so it is spawn-gated as well. It additionally calls
//!     SetActiveChannel(group.GetDefaultActiveRadioChannel()) on the owner's VON controller.
//!
//! Both pull the FIRST RADIO gadget off the body (GetGadgetByType(EGadgetType.RADIO)), take
//! GetTransceiver(0), and write the player's actual group frequency onto it, server-side, where
//! SetFrequency replicates to every machine. Note what neither does: no second transceiver, and
//! no RADIO_BACKPACK - a manpack has always spawned on its authored channels, so a re-kit leaves
//! one alone too. Verified against every SetFrequency/SetEncryptionKey call site in the 1.8.0.10
//! corpus: the only code that tunes a manpack or a second transceiver is SCR_VONAutoTune, and
//! the only code that writes a radio encryption key is Campaign's base/antenna/mobile-assembly
//! systems, none of which touch a carried radio.
//!
//! Nothing re-runs either path for a radio that arrives later. SCR_VONAutoTune is authored on
//! DefaultPlayerControllerMP_Campaign alone, and its one hook for "the radios you carry changed"
//! does not work anyway: Init() inserts OnEntriesChanged into SCR_VONController's entries-changed
//! invoker, but SCR_VONAutoTune has no method of that name - it resolves to the global prototype
//! the invoker's typedef is declared from, so the insert is a no-op and the auto-tune only ever
//! fires on a controlled-entity change (a respawn or a possession).
//!
//! So a live re-kit - which deletes the player's radio and spawns a fresh one sitting on its
//! authored channel - used to drop them off their squad's net until they respawned or dialled it
//! back in by hand. Asking for the spawn tune at the end of the re-kit is the whole fix.
//!
//! Spawns need nothing added: OnLoadoutSpawned, where we dress, is called from
//! OnPlayerSpawnFinalize_S a few lines BEFORE m_OnPlayerSpawned.Invoke(), so vanilla's hook
//! already tunes the kit we just put on the body.
//------------------------------------------------------------------------------------------------
modded class SCR_GroupsManagerComponent
{
	//--------------------------------------------------------------------------------------------
	//! Vanilla's spawn-time tune, on demand. Vanilla's own method rather than a copy of it, so a
	//! re-kit and a spawn cannot drift apart, and authority-only like the hook it borrows.
	//!
	//! Gated on whether a spawn would have tuned this player at all - a scenario that turned group
	//! tuning off must not get it back through the kit picker. EITHER switch is enough: both spawn
	//! paths write the same transceiver the same way, so a player whose radio is tuned only by the
	//! agent path must still get it after a re-kit. Both default to on.
	void RK29_TuneToGroupFrequency_S(int playerId, IEntity body)
	{
		if (IsProxy() || !body)
			return;

		if (!m_bTunePlayersRadioToGroupFrequency && !m_bTuneAgentsRadioToGroupFrequency)
			return;

		TunePlayersFrequency(playerId, body);
	}
}
