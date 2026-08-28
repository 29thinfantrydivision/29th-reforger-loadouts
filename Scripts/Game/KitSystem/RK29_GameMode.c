//------------------------------------------------------------------------------------------------
//! Replicated kit counts on the GM game mode. Arrays are indexed by RK29_KitManager's kit index:
//! the loadout-manager list first, then the picker-only kits appended past its end.
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

	//------------------------------------------------------------------------------------------------
	RK29_OnCountsChangedInvoker RK29_GetOnCountsChanged()
	{
		if (!m_RK29_OnCountsChanged)
			m_RK29_OnCountsChanged = new RK29_OnCountsChangedInvoker();
		return m_RK29_OnCountsChanged;
	}

	//------------------------------------------------------------------------------------------------
	//! Server only. Edits the arrays in place then BumpMe's, vanilla's own array-replication idiom
	//! (SCR_CampaignSuppliesComponent, SCR_FactionCommanderHandlerComponent); do not swap in a
	//! freshly built array.
	void RK29_SetCounts_S(notnull array<int> alive, notnull array<int> magnified)
	{
		if (RK29_ArraysEqual(m_aRK29AliveCounts, alive) && RK29_ArraysEqual(m_aRK29MagnifiedCounts, magnified))
			return;

		m_aRK29AliveCounts.Copy(alive);
		m_aRK29MagnifiedCounts.Copy(magnified);
		Replication.BumpMe();

		// no Rpl callback for the authority's own write
		OnRK29CountsChanged();
	}

	//------------------------------------------------------------------------------------------------
	int RK29_GetAliveCount(int kitIndex)
	{
		if (!m_aRK29AliveCounts || kitIndex < 0 || kitIndex >= m_aRK29AliveCounts.Count())
			return 0;
		return m_aRK29AliveCounts[kitIndex];
	}

	//------------------------------------------------------------------------------------------------
	int RK29_GetMagnifiedCount(int kitIndex)
	{
		if (!m_aRK29MagnifiedCounts || kitIndex < 0 || kitIndex >= m_aRK29MagnifiedCounts.Count())
			return 0;
		return m_aRK29MagnifiedCounts[kitIndex];
	}

	//------------------------------------------------------------------------------------------------
	protected void OnRK29CountsChanged()
	{
		if (m_RK29_OnCountsChanged)
			m_RK29_OnCountsChanged.Invoke();
	}

	//------------------------------------------------------------------------------------------------
	protected static bool RK29_ArraysEqual(array<int> a, array<int> b)
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

	//------------------------------------------------------------------------------------------------
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

	//------------------------------------------------------------------------------------------------
	//! Hand the chat command back: SCR_ChatPanelManager is a game core and outlives this world,
	//! so anything left inserted on its invokers keeps this game mode alive for the process.
	override void OnGameEnd()
	{
		super.OnGameEnd();

		SCR_ChatPanelManager chatMgr = SCR_ChatPanelManager.GetInstance();
		if (!chatMgr)
			return;

		ChatCommandInvoker inv = chatMgr.GetCommandInvoker("kitmenu");
		if (inv)
			inv.Remove(RK29_OnChatKitMenu);
	}

	//------------------------------------------------------------------------------------------------
	//! Chat commands dispatch entirely on the typing client, so this one is purely local.
	protected void RK29_OnChatKitMenu(SCR_ChatPanel panel, string data)
	{
		RK29_LoadoutMenu.Toggle();
	}
}
