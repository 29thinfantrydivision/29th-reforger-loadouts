//------------------------------------------------------------------------------------------------
//! Replicated kit counts on the GM game mode. Arrays use the loadout-manager index space.
//------------------------------------------------------------------------------------------------
void RK29_OnCountsChangedMethod();
typedef func RK29_OnCountsChangedMethod;
typedef ScriptInvokerBase<RK29_OnCountsChangedMethod> RK29_OnCountsChangedInvoker;

modded class SCR_GameModeEditor
{
	[RplProp(onRplName: "OnRK29CountsChanged")]
	protected ref array<int> m_aRK29AliveCounts = {};

	[RplProp(onRplName: "OnRK29CountsChanged")]
	protected ref array<int> m_aRK29MagnifiedCounts = {};

	protected ref RK29_OnCountsChangedInvoker m_RK29_OnCountsChanged;

	//--------------------------------------------------------------------------------------------
	RK29_OnCountsChangedInvoker RK29_GetOnCountsChanged()
	{
		if (!m_RK29_OnCountsChanged)
			m_RK29_OnCountsChanged = new RK29_OnCountsChangedInvoker();
		return m_RK29_OnCountsChanged;
	}

	//--------------------------------------------------------------------------------------------
	//! Server only. Assign rebuilt arrays, never mutate in place, or replication misses it.
	void RK29_SetCounts(notnull array<int> alive, notnull array<int> magnified)
	{
		if (RK29_ArraysEqual(m_aRK29AliveCounts, alive) && RK29_ArraysEqual(m_aRK29MagnifiedCounts, magnified))
			return;

		m_aRK29AliveCounts = alive;
		m_aRK29MagnifiedCounts = magnified;
		Replication.BumpMe();

		// no Rpl callback for the authority's own write
		OnRK29CountsChanged();
	}

	//--------------------------------------------------------------------------------------------
	int RK29_GetAliveCount(int loadoutIndex)
	{
		if (!m_aRK29AliveCounts || loadoutIndex < 0 || loadoutIndex >= m_aRK29AliveCounts.Count())
			return 0;
		return m_aRK29AliveCounts[loadoutIndex];
	}

	//--------------------------------------------------------------------------------------------
	int RK29_GetMagnifiedCount(int loadoutIndex)
	{
		if (!m_aRK29MagnifiedCounts || loadoutIndex < 0 || loadoutIndex >= m_aRK29MagnifiedCounts.Count())
			return 0;
		return m_aRK29MagnifiedCounts[loadoutIndex];
	}

	//--------------------------------------------------------------------------------------------
	protected void OnRK29CountsChanged()
	{
		if (m_RK29_OnCountsChanged)
			m_RK29_OnCountsChanged.Invoke();
	}

	//--------------------------------------------------------------------------------------------
	protected bool RK29_ArraysEqual(array<int> a, array<int> b)
	{
		if (!a || !b || a.Count() != b.Count())
			return false;
		for (int i = 0, n = a.Count(); i < n; i++)
		{
			if (a[i] != b[i])
				return false;
		}
		return true;
	}

	//--------------------------------------------------------------------------------------------
	override void OnPlayerSpawned(int playerId, IEntity controlledEntity)
	{
		super.OnPlayerSpawned(playerId, controlledEntity);

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (mgr)
			mgr.OnPlayerSpawned_S(playerId, controlledEntity);
	}

	//--------------------------------------------------------------------------------------------
	//! "/kitmenu" chat fallback for the picker.
	override void OnGameStart()
	{
		super.OnGameStart();

		SCR_ChatPanelManager chatMgr = SCR_ChatPanelManager.GetInstance();
		if (!chatMgr)
			return;

		ChatCommandInvoker inv = chatMgr.GetCommandInvoker("kitmenu");
		if (inv)
			inv.Insert(RK29_OnChatKitMenu);
	}

	//--------------------------------------------------------------------------------------------
	protected void RK29_OnChatKitMenu(SCR_ChatPanel panel, string data)
	{
		RK29_KitPicker.ToggleFromChat();
	}
}
