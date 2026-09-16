//------------------------------------------------------------------------------------------------
//! Assigned loadout is the only honest record of a player's class - never read it off the body.
//------------------------------------------------------------------------------------------------
modded class SCR_PlayerLoadoutComponent
{
	//------------------------------------------------------------------------------------------------
	//! Server only. Skips CanAssignLoadout_S on purpose: it may refuse a living player, and the kit
	//! system re-assigns identity mid-life.
	bool RK29_AssignLoadout_S(notnull SCR_BasePlayerLoadout loadout)
	{
		if (!Replication.IsServer())
			return false;

		SCR_LoadoutManager lm = GetGame().GetLoadoutManager();
		if (!lm)
			return false;
		int loadoutIndex = lm.GetLoadoutIndex(loadout);
		if (loadoutIndex < 0)
		{
			Print(string.Format("[RK29] loadout '%1' not registered - not assigned",
				loadout.GetLoadoutName()), LogLevel.WARNING);
			return false;
		}

		AssignLoadout_S(loadoutIndex);

		// AssignLoadout_S writes only the server's m_Loadout, which is not replicated - the owner learns
		// its assigned loadout solely from this response RPC, and without it the deploy menu reads a
		// stale GetAssignedLoadout() and never re-requests. Only when nothing is in flight, though: the
		// spawn lock is keyed by source and every vanilla lock/unlock uses this same component, so
		// responding would release a pending request's lock and answer the owner for a request it never
		// made.
		SCR_SpawnLockComponent lock = GetLock();
		if (!lock || (!lock.IsLocked(true) && !lock.IsLocked(false)))
			SendRequestLoadoutResponse_S(loadoutIndex, true);

		return true;
	}
}
