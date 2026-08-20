//------------------------------------------------------------------------------------------------
//! Preround team HUD: own-team kit counts + magnified tally. Client-side view only.
//------------------------------------------------------------------------------------------------
class RK29_KitHud
{
	protected static const ResourceName HUD_LAYOUT = "{AB29C0FFEE292000}UI/KitSystem/RK29_KitHud.layout";
	protected static const ResourceName ROW_LAYOUT = "{AB29C0FFEE291000}UI/KitSystem/RK29_Row.layout";
	protected static const int TICK_MS = 1000;

	protected static ref RK29_KitHud s_Instance;

	protected Widget m_wRoot;
	protected Widget m_wRows;
	protected TextWidget m_wTitle;
	protected TextWidget m_wFooter;
	protected bool m_bSubscribed;
	protected static bool s_bHiddenReported;
	protected static bool s_bShownReported;

	//--------------------------------------------------------------------------------------------
	//! Rebuilds every world - statics survive scenario changes, the callqueue does not.
	static void Boot()
	{
		if (System.IsConsoleApp())
			return;

		s_Instance = new RK29_KitHud();
		GetGame().GetCallqueue().CallLater(s_Instance.Tick, TICK_MS, true);
	}

	//--------------------------------------------------------------------------------------------
	protected void Tick()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return;

		bool show = mgr.IsBriefing();
		if (!show)
		{
			if (m_wRoot)
				m_wRoot.SetVisible(false);
			return;
		}

		EnsureWidgets();
		if (!m_wRoot)
			return;

		SubscribeCounts();
		m_wRoot.SetVisible(true);
		Rebuild();

		if (!s_bShownReported && m_wRows)
		{
			s_bShownReported = true;
			float x, y;
			m_wRows.GetScreenPos(x, y);
			Print(string.Format("[RK29] HUD visible at screen %1,%2 | rows parent ok", x, y), LogLevel.NORMAL);
		}
	}

	//--------------------------------------------------------------------------------------------
	protected void EnsureWidgets()
	{
		if (m_wRoot)
			return;

		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		m_wRoot = ws.CreateWidgets(HUD_LAYOUT);
		if (!m_wRoot)
		{
			Print("[RK29] HUD layout failed to load", LogLevel.WARNING);
			return;
		}
		m_wRows   = m_wRoot.FindAnyWidget("HudRows");
		m_wTitle  = TextWidget.Cast(m_wRoot.FindAnyWidget("HudTitle"));
		m_wFooter = TextWidget.Cast(m_wRoot.FindAnyWidget("HudFooter"));
	}

	//--------------------------------------------------------------------------------------------
	protected void SubscribeCounts()
	{
		if (m_bSubscribed)
			return;
		SCR_GameModeEditor gm = SCR_GameModeEditor.Cast(GetGame().GetGameMode());
		if (!gm)
			return;
		gm.RK29_GetOnCountsChanged().Insert(Rebuild);
		m_bSubscribed = true;
	}

	//--------------------------------------------------------------------------------------------
	protected void Rebuild()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		SCR_GameModeEditor gm = SCR_GameModeEditor.Cast(GetGame().GetGameMode());
		if (!mgr || !gm || !m_wRows || !m_wRoot.IsVisible())
			return;

		string factionKey = RK29_KitHud.LocalFactionKey();
		if (factionKey == "")
		{
			if (!s_bHiddenReported)
			{
				s_bHiddenReported = true;
				Print("[RK29] HUD hidden - local player has no faction yet", LogLevel.NORMAL);
			}
			m_wRoot.SetVisible(false);
			return;
		}

		Widget child = m_wRows.GetChildren();
		while (child)
		{
			Widget next = child.GetSibling();
			child.RemoveFromHierarchy();
			child = next;
		}

		if (m_wTitle)
			m_wTitle.SetText(factionKey + " - KITS FIELDED");

		WorkspaceWidget ws = GetGame().GetWorkspace();
		int totalAlive = 0;
		int totalMag = 0;

		foreach (int idx, string kitName : mgr.m_aIndexToKit)
		{
			if (kitName == "")
				continue;
			RK29_KitStruct kit = mgr.m_mKits.Get(kitName);
			if (!kit || kit.m_sFactionKey != factionKey)
				continue;
			totalAlive += gm.RK29_GetAliveCount(idx);
			totalMag   += gm.RK29_GetMagnifiedCount(idx);
		}

		// row order follows the side config's class order, leftovers append in loadout order;
		// kits sharing a display label (AR + legacy MG under "Machine Gunner") sum into one row
		array<string> labels = {};
		map<string, RK29_KitStruct> labelKits = new map<string, RK29_KitStruct>();
		if (mgr.m_Setup && mgr.m_Setup.m_aClasses)
		{
			foreach (RK29_ClassSetup cls : mgr.m_Setup.m_aClasses)
			{
				if (!cls)
					continue;
				RK29_KitStruct kit = mgr.m_mKits.Get(cls.m_sKitName);
				if (!kit || kit.m_sFactionKey != factionKey)
					continue;
				string label = RK29_KitHud.ShortKitName(cls.m_sKitName);
				if (!labels.Contains(label))
				{
					labels.Insert(label);
					labelKits.Set(label, kit);
				}
			}
		}
		foreach (string kitName : mgr.m_aIndexToKit)
		{
			if (kitName == "")
				continue;
			RK29_KitStruct kit = mgr.m_mKits.Get(kitName);
			if (!kit || kit.m_sFactionKey != factionKey)
				continue;
			string label = RK29_KitHud.ShortKitName(kitName);
			if (!labels.Contains(label))
			{
				labels.Insert(label);
				labelKits.Set(label, kit);
			}
		}

		foreach (string label : labels)
		{
			int mag;
			int alive = RK29_KitHud.LabelAlive(gm, mgr, factionKey, label, mag);
			if (alive == 0)
				continue;

			Widget row = ws.CreateWidgets(ROW_LAYOUT, m_wRows);
			if (!row)
				continue;

			TextWidget name = TextWidget.Cast(row.FindAnyWidget("RowName"));
			if (name)
				name.SetText(label);

			TextWidget value = TextWidget.Cast(row.FindAnyWidget("RowValue"));
			if (value)
			{
				string txt = alive.ToString();
				if (mag > 0)
					txt = txt + " (" + mag.ToString() + " mag)";
				value.SetText(txt);
			}

			ImageWidget icon = ImageWidget.Cast(row.FindAnyWidget("RowIcon"));
			if (icon)
				RK29_KitHud.SetKitIcon(labelKits.Get(label), icon);
		}

		if (m_wFooter)
			m_wFooter.SetText(totalAlive.ToString() + " alive | " + totalMag.ToString() + " mag");
	}

	// ============================================================================== helpers

	//--------------------------------------------------------------------------------------------
	//! Alive/magnified summed over every kit of this faction sharing the display label.
	static int LabelAlive(SCR_GameModeEditor gm, RK29_KitManager mgr, string factionKey, string label, out int magnified)
	{
		int alive = 0;
		magnified = 0;
		foreach (int idx, string kitName : mgr.m_aIndexToKit)
		{
			if (kitName == "")
				continue;
			RK29_KitStruct kit = mgr.m_mKits.Get(kitName);
			if (!kit || kit.m_sFactionKey != factionKey)
				continue;
			if (ShortKitName(kitName) != label)
				continue;
			alive     += gm.RK29_GetAliveCount(idx);
			magnified += gm.RK29_GetMagnifiedCount(idx);
		}
		return alive;
	}

	//--------------------------------------------------------------------------------------------
	static string LocalFactionKey()
	{
		Faction f = SCR_FactionManager.SGetLocalPlayerFaction();

		// unreliable on clients - fall back to the controlled entity's affiliation
		if (!f)
		{
			PlayerController pc = GetGame().GetPlayerController();
			if (pc)
			{
				IEntity body = pc.GetControlledEntity();
				if (body)
				{
					FactionAffiliationComponent fac = FactionAffiliationComponent.Cast(body.FindComponent(FactionAffiliationComponent));
					if (fac)
						f = fac.GetAffiliatedFaction();
				}
			}
		}

		if (!f)
			return "";
		return f.GetFactionKey();
	}

	//--------------------------------------------------------------------------------------------
	//! Config display name, else kit name with the "29th <FACTION> - " prefix cut.
	static string ShortKitName(string kitName)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (mgr && mgr.m_Setup)
		{
			RK29_ClassSetup cls = mgr.m_Setup.FindClass(kitName);
			if (cls && cls.m_sDisplayName != string.Empty)
				return cls.m_sDisplayName;
		}

		int dash = kitName.IndexOf("- ");
		if (dash >= 0)
			return kitName.Substring(dash + 2, kitName.Length() - dash - 2);
		return kitName;
	}

	//--------------------------------------------------------------------------------------------
	static void SetKitIcon(RK29_KitStruct kit, ImageWidget icon)
	{
		if (!kit || !kit.m_UIInfo)
		{
			icon.SetVisible(false);
			return;
		}
		if (!kit.m_UIInfo.SetIconTo(icon))
			icon.SetVisible(false);
	}
}
