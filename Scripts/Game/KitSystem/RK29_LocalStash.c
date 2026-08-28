//------------------------------------------------------------------------------------------------
//! What the server last told this client it applied, fed by the owner-only RK29_RpcDo_KitSaved.
//! The manager's m_mSelections is server state, so EffectiveKitFor asked on a client always
//! answers the side default. Not authoritative: it only decides the deploy row's icon and name,
//! and both read sites re-check it against the live offer.
//------------------------------------------------------------------------------------------------
class RK29_LocalStash
{
	protected static string s_sKit;

	//! Wire dialect, so the deploy row shows the chosen rifle and sight rather than the class
	//! defaults.
	protected static string s_sPicks;

	//------------------------------------------------------------------------------------------------
	static void Mark(string kitName, string picksWire)
	{
		s_sKit = kitName;
		s_sPicks = picksWire;

		// next frame: this arrives inside an RPC handler, not the frame vanilla builds the list
		// in
		GetGame().GetCallqueue().CallLater(RefreshDeployRow, 0, false);
	}

	//------------------------------------------------------------------------------------------------
	static string Kit()
	{
		return s_sKit;
	}

	//------------------------------------------------------------------------------------------------
	static string Picks()
	{
		return s_sPicks;
	}

	//------------------------------------------------------------------------------------------------
	//! Forget the last session's kit: a static outlives the world, so a restart inside the same
	//! process would open its deploy row on whatever the previous session applied.
	static void Clear()
	{
		s_sKit = "";
		s_sPicks = "";

		// the next-frame refresh Mark arms has no other disarm, and a world rebuild does not carry the
		// callqueue over: one left armed would redraw the new session against the old kit
		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
			queue.Remove(RefreshDeployRow);
	}

	//------------------------------------------------------------------------------------------------
	//! Redraw the deploy row against what was just applied. Vanilla builds the gallery on menu open,
	//! faction change and group change only, and a row's icon is read once when its button is built.
	protected static void RefreshDeployRow()
	{
		MenuManager mm = GetGame().GetMenuManager();
		if (!mm)
			return;

		MenuBase menu = mm.FindMenuByPreset(ChimeraMenuPreset.RespawnSuperMenu);
		if (!menu)
			return;

		Widget root = menu.GetRootWidget();
		if (!root)
			return;

		Widget holder = root.FindAnyWidget("LoadoutSelector");
		if (!holder)
			return;

		SCR_LoadoutRequestUIComponent comp = SCR_LoadoutRequestUIComponent.Cast(
			holder.FindHandler(SCR_LoadoutRequestUIComponent));
		if (!comp)
			return;

		// the row has to exist before anything can restamp it, and a first-ever apply is what
		// makes Current Kit available
		comp.RK29_RefreshLoadoutList();

		// vanilla reads the loadout manager on its first line, unguarded
		if (GetGame().GetLoadoutManager())
			comp.RefreshLoadoutPreview();
	}
}
