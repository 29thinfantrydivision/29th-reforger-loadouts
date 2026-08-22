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
		return true;
	}
}
