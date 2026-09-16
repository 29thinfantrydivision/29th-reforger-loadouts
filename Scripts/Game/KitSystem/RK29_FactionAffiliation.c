//------------------------------------------------------------------------------------------------
//! Re-walks a character's outfit for the perceived-faction system after the kit system re-dressed
//! it. Vanilla's InitPlayerOutfitFaction_S runs once per life, from OnPlayerSpawnFinalize_S, and
//! lands before the loadout's OnLoadoutSpawned - it scores the bare body. Do not simply call it
//! again: every call Inserts the same two handlers (one server-side, one in a Broadcast RPC on
//! every machine) and only the destructor and DisableUpdatingPerceivedFaction ever Remove them.
//! Take the subscriptions back first.
//------------------------------------------------------------------------------------------------
modded class SCR_CharacterFactionAffiliationComponent
{
	//------------------------------------------------------------------------------------------------
	//! Server only, like the vanilla init it wraps.
	//! Unconfirmed fix: the leak was found by reading vanilla, not seen in play.
	void RK29_RebuildOutfitFaction_S()
	{
		if (!HasPerceivedFaction())
		{
			InitPlayerOutfitFaction_S();
			return;
		}

		if (m_PerceivedManager)
			m_PerceivedManager.GetOnPerceivedFactionChangesAffectsAIChanged().Remove(OnPerceivedFactionChangesAffectsAIChanged);

		DisableUpdatingPerceivedFaction_S();
		InitPlayerOutfitFaction_S();
	}
}
