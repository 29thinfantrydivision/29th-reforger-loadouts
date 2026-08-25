//------------------------------------------------------------------------------------------------
//! Assigned loadout is the only honest record of a player's class - never read it off the body.
//------------------------------------------------------------------------------------------------
modded class SCR_PlayerLoadoutComponent
{
	//--------------------------------------------------------------------------------------------
	//! Server only. force skips CanAssignLoadout_S (it may refuse living players).
	bool RK29_AssignLoadout_S(notnull SCR_BasePlayerLoadout loadout, bool force = false)
	{
		if (!Replication.IsServer())
			return false;

		// vanilla chain works in loadout-manager index space
		SCR_LoadoutManager lm = GetGame().GetLoadoutManager();
		if (!lm)
			return false;
		int loadoutIndex = lm.RK29_IndexOfLoadout(loadout);
		if (loadoutIndex < 0)
		{
			Print("[RK29] loadout '" + loadout.GetLoadoutName() + "' not registered - not assigned", LogLevel.WARNING);
			return false;
		}

		if (!force && !CanAssignLoadout_S(loadoutIndex))
		{
			Print("[RK29] vanilla CanAssignLoadout_S refused '" + loadout.GetLoadoutName() + "' - not assigned", LogLevel.WARNING);
			return false;
		}

		AssignLoadout_S(loadoutIndex);

		// AssignLoadout_S only writes the SERVER's m_Loadout, and that field is not replicated -
		// the owner learns its assigned loadout solely from this response RPC. Skipping it leaves
		// the deploy menu reading a stale GetAssignedLoadout(): it believes the player already
		// holds some other kit, so it never re-requests one. The player picks a kit, the server
		// never hears about it, and spawns whatever WE last assigned.
		//
		// But ONLY when nothing is in flight. The spawn lock is a set keyed by source, and every
		// vanilla lock/unlock uses this same component as that source - so the responder would
		// release a pending request's lock along with ours and answer the owner for a request it
		// never made. A player reconnecting into the deploy menu is exactly that case. When a
		// request IS pending, its own response carries the truth to the client moments later,
		// and whichever assignment lands last is the one the player asked for.
		SCR_SpawnLockComponent lock = GetLock();
		if (!lock || (!lock.IsLocked(true) && !lock.IsLocked(false)))
			SendRequestLoadoutResponse_S(loadoutIndex, true);

		return true;
	}
}
