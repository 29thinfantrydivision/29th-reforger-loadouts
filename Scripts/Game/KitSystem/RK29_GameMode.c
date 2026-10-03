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
	//! freshly built array. Answers whether anything actually moved - the count sweep reports a
	//! true as an event the tally missed.
	bool RK29_SetCounts_S(notnull array<int> alive, notnull array<int> magnified)
	{
		if (RK29_ArraysEqual(m_aRK29AliveCounts, alive) && RK29_ArraysEqual(m_aRK29MagnifiedCounts, magnified))
			return false;

		m_aRK29AliveCounts.Copy(alive);
		m_aRK29MagnifiedCounts.Copy(magnified);
		Replication.BumpMe();

		// no Rpl callback for the authority's own write
		OnRK29CountsChanged();
		return true;
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
	//! Client side: watch the LOCAL player's side, which is when a remembered kit can be offered.
	//! Vanilla's own idiom for "this player has a side now" (SCR_GameModeCampaign.OnPlayerRegistered),
	//! including the immediate call for a faction already assigned by the time this runs - the
	//! invoker will not fire again for it. The invoker fires on both machines, so the guard to the
	//! local controller is what makes this the client's.
	override void OnPlayerRegistered(int playerId)
	{
		super.OnPlayerRegistered(playerId);

		SCR_PlayerController local = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!local || playerId != local.GetPlayerId())
			return;

		SCR_PlayerFactionAffiliationComponent affiliation = SCR_PlayerFactionAffiliationComponent.Cast(
			local.FindComponent(SCR_PlayerFactionAffiliationComponent));
		if (!affiliation || !affiliation.GetOnPlayerFactionChangedInvoker())
			return;

		// Remove then Insert: registration can run more than once for one controller, and a stacked
		// subscription would seed twice
		affiliation.GetOnPlayerFactionChangedInvoker().Remove(RK29_OnLocalFactionAssigned);
		affiliation.GetOnPlayerFactionChangedInvoker().Insert(RK29_OnLocalFactionAssigned);

		Faction already = affiliation.GetAffiliatedFaction();
		if (already)
			RK29_OnLocalFactionAssigned(affiliation, null, already);
	}

	//------------------------------------------------------------------------------------------------
	//! Offer the server this side's default kit, built from the saved kit this client last wore it
	//! as and as that saved kit reads NOW - a name, not a copy, is what the store holds, so one
	//! edited since is seeded edited, and one deleted since, or one today's config would answer
	//! differently (RK29_KitLastUsedStore.WireFor), is not seeded at all. Fires on every side change, not only
	//! the first, so switching sides seeds that side's kit rather than landing on bare defaults. The
	//! SIDE DEFAULT and not the last class played: a session starts where the roster says it starts,
	//! and only the personalisation of that kit is remembered.
	//! Nothing is enforced here - the server serves this only if it holds nothing for the player.
	protected void RK29_OnLocalFactionAssigned(SCR_PlayerFactionAffiliationComponent component,
		Faction previous, Faction current)
	{
		if (!current)
			return;

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr || !mgr.Setup())
			return;

		string kitName = mgr.Setup().DefaultKitName(current.GetFactionKey());
		if (kitName == "")
			return;

		RK29_KitLastUsedStore store = RK29_KitLastUsedStore.GetInstance();
		if (!store)
			return;

		RK29_ELastUsedStatus status;
		string presetName;
		string wire = store.WireFor(kitName, status, presetName);
		if (wire == "")
		{
			RK29_HintUnseededKit(status, presetName);
			return;
		}

		SCR_PlayerController local = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (local)
			local.RK29_SeedKit(kitName, wire);
	}

	//------------------------------------------------------------------------------------------------
	//! A remembered kit the player did not get, said once per side assignment until they re-save it:
	//! starting them on the defaults unannounced is gear changing without a word. A MISSING kit
	//! is not said - it was deleted or renamed by the player, who knows.
	protected void RK29_HintUnseededKit(RK29_ELastUsedStatus status, string presetName)
	{
		string why;
		switch (status)
		{
			case RK29_ELastUsedStatus.OUTDATED:
				why = "has changed since you saved it";
				break;
			case RK29_ELastUsedStatus.UNREADABLE:
				why = "was saved in a format this version cannot read";
				break;
			case RK29_ELastUsedStatus.SHORT_POOL:
				why = "leaves a supply pool under its minimum";
				break;
			default:
				return;
		}

		SCR_HintManagerComponent.ShowCustomHint(string.Format("Your saved kit '%1' %2, so you start"
			+ " with the standard kit. Open the loadout menu to load it, then save it again.",
			presetName, why), "SAVED KIT NOT USED", 12);
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
	//! The faction subscription goes back for the same reason, though its component shares this
	//! world's life - an Insert without its Remove is how the next one stacks.
	override void OnGameEnd()
	{
		super.OnGameEnd();

		SCR_PlayerController local = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (local)
		{
			SCR_PlayerFactionAffiliationComponent affiliation = SCR_PlayerFactionAffiliationComponent.Cast(
				local.FindComponent(SCR_PlayerFactionAffiliationComponent));
			if (affiliation && affiliation.GetOnPlayerFactionChangedInvoker())
				affiliation.GetOnPlayerFactionChangedInvoker().Remove(RK29_OnLocalFactionAssigned);
		}

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
