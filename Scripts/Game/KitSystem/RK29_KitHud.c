//------------------------------------------------------------------------------------------------
//! Preround team HUD: own-team kit counts + magnified tally. Client-side view only.
//------------------------------------------------------------------------------------------------
class RK29_KitHud
{
	protected static const ResourceName HUD_LAYOUT = "{AB29C0FFEEB20033}UI/KitSystem/RK29_KitHud.layout";
	protected static const int TICK_MS = 1000;
	protected static const int HEADER_ROW_HEIGHT = 24;

	protected static ref RK29_KitHud s_Instance;

	protected Widget m_wRoot;
	protected Widget m_wRows;
	protected TextWidget m_wTitle;
	protected Widget m_wTimerCell;
	protected TextWidget m_wTimerMin;
	protected TextWidget m_wTimerSec;
	protected Widget m_wFooterRow;
	protected Widget m_wHeaderRow;
	protected bool m_bSubscribed;

	//! False means the table on screen is stale or absent: the counts invoker rebuilds on every
	//! change but cannot give the first paint after create, re-show, or a faction-less bail.
	protected bool m_bRowsPainted;
	protected static bool s_bHiddenReported;
	protected static bool s_bShownReported;
	protected static bool s_bLayoutWarned;

	//------------------------------------------------------------------------------------------------
	//! Rebuilds every world - statics survive scenario changes, the callqueue does not.
	static void Boot()
	{
		if (System.IsConsoleApp())
			return;

		// take the outgoing tick and its counts subscription back first: that invoker belongs to the
		// game mode and outlives this object, so one left standing is a dead HUD still rebuilding.
		if (s_Instance)
		{
			GetGame().GetCallqueue().Remove(s_Instance.Tick);
			s_Instance.Unsubscribe();
		}
		s_bLayoutWarned = false;
		s_bHiddenReported = false;
		s_bShownReported = false;

		s_Instance = new RK29_KitHud();
		GetGame().GetCallqueue().CallLater(s_Instance.Tick, TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	//! The clock and the show/hide. The table is not rebuilt here - the counts invoker does that;
	//! the one rebuild it cannot give is the first paint after the HUD appears (m_bRowsPainted).
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
			m_bRowsPainted = false;
			return;
		}

		EnsureWidgets();
		if (!m_wRoot)
			return;

		SubscribeCounts();

		// visible before the rebuild, because Rebuild refuses to stamp into a hidden root
		m_wRoot.SetVisible(true);
		UpdateTimer(mgr);
		if (!m_bRowsPainted)
			Rebuild();

		if (!s_bShownReported && m_wRows)
		{
			s_bShownReported = true;
			float x, y;
			m_wRows.GetScreenPos(x, y);
			RK29_Log.Trace(string.Format("[RK29] HUD visible at screen %1,%2 | rows parent ok", x, y));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Briefing countdown, hidden when the Round Timer addon is absent (remaining < 0). Split at the
	//! colon into two fixed half-cells so the colon rides the centerline of the count column below.
	protected void UpdateTimer(RK29_KitManager mgr)
	{
		if (!m_wTimerCell || !m_wTimerMin || !m_wTimerSec)
			return;

		int remaining = mgr.GetPhaseRemainingSeconds();
		if (remaining < 0)
		{
			m_wTimerCell.SetVisible(false);
			return;
		}

		int m = remaining / 60;
		int s = remaining % 60;
		m_wTimerMin.SetText(string.Format("%1:", m));
		if (s < 10)
			m_wTimerSec.SetText(string.Format("0%1", s));
		else
			m_wTimerSec.SetText(s.ToString());

		Color c = Color.White;
		if (remaining < 10)
			c = UIColors.WARNING;
		else if (remaining < 30)
			c = UIColors.SLIGHT_WARNING;
		m_wTimerMin.SetColor(c);
		m_wTimerSec.SetColor(c);

		m_wTimerCell.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	protected void EnsureWidgets()
	{
		if (m_wRoot)
			return;

		// the HUD manager hands the layout the cursor-ignore flag and full-screen anchors; LOW is the
		// layer read-only elements draw in. Absent for a world's first ticks, so a bare return retries.
		SCR_HUDManagerComponent hud = GetGame().GetHUDManager();
		if (!hud)
			return;

		m_wRoot = hud.CreateLayout(HUD_LAYOUT, EHudLayers.LOW);
		if (!m_wRoot)
		{
			if (!s_bLayoutWarned)
				Print("[RK29] HUD layout failed to load - retrying each tick", LogLevel.WARNING);
			s_bLayoutWarned = true;
			return;
		}
		m_wRows   = m_wRoot.FindAnyWidget("HudRows");
		m_wTitle  = TextWidget.Cast(m_wRoot.FindAnyWidget("HudTitle"));
		m_wTimerCell = m_wRoot.FindAnyWidget("HudTimerCell");
		m_wTimerMin  = TextWidget.Cast(m_wRoot.FindAnyWidget("HudTimerMin"));
		m_wTimerSec  = TextWidget.Cast(m_wRoot.FindAnyWidget("HudTimerSec"));
		m_wFooterRow = m_wRoot.FindAnyWidget("HudFooterRow");
		m_wHeaderRow = m_wRoot.FindAnyWidget("HudHeaderRow");
		BuildHeaderRow();
	}

	//------------------------------------------------------------------------------------------------
	//! Built from the row layout so the header glyphs cannot drift out of the columns below them.
	protected void BuildHeaderRow()
	{
		if (!m_wHeaderRow)
			return;

		WorkspaceWidget ws = GetGame().GetWorkspace();
		Widget header = ws.CreateWidgets(RK29_MenuRowKit.ROW_LAYOUT, m_wHeaderRow);
		if (!header)
			return;

		// a header is a label strip, not a row - shrink it or it eats a row's worth of height
		SizeLayoutWidget headerSize = SizeLayoutWidget.Cast(header);
		if (headerSize)
			headerSize.SetHeightOverride(HEADER_ROW_HEIGHT);

		Widget headerIcon = header.FindAnyWidget("RowIcon");
		if (headerIcon)
			headerIcon.SetVisible(false);
		Widget headerBase = header.FindAnyWidget("RowBase");
		if (headerBase)
			headerBase.SetVisible(false);

		TextWidget headerName = TextWidget.Cast(header.FindAnyWidget("RowName"));
		if (headerName)
			headerName.SetText("");

		Widget magIcon = header.FindAnyWidget("RowMagIcon");
		if (magIcon)
			magIcon.SetVisible(true);

		Widget aliveIcon = header.FindAnyWidget("RowAliveIcon");
		if (aliveIcon)
			aliveIcon.SetVisible(true);

		Widget aliveText = header.FindAnyWidget("RowValue");
		if (aliveText)
			aliveText.SetVisible(false);
	}

	//------------------------------------------------------------------------------------------------
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

	//------------------------------------------------------------------------------------------------
	//! Called on the outgoing instance when a world reboots - see Boot.
	protected void Unsubscribe()
	{
		if (!m_bSubscribed)
			return;

		SCR_GameModeEditor gm = SCR_GameModeEditor.Cast(GetGame().GetGameMode());
		if (gm)
			gm.RK29_GetOnCountsChanged().Remove(Rebuild);

		m_bSubscribed = false;
	}

	//------------------------------------------------------------------------------------------------
	//! Row order follows the side config's class order, with anything unlisted appended in loadout
	//! order; kits sharing a label sum into one row, whose icon is the first kit claiming that label.
	protected void Rebuild()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		SCR_GameModeEditor gm = SCR_GameModeEditor.Cast(GetGame().GetGameMode());
		if (!mgr || !gm || !m_wRoot || !m_wRows || !m_wRoot.IsVisible())
			return;

		string factionKey = RK29_KitHud.LocalFactionKey();
		if (factionKey == "")
		{
			if (!s_bHiddenReported)
			{
				s_bHiddenReported = true;
				RK29_Log.Trace("[RK29] HUD hidden - local player has no faction yet");
			}
			m_wRoot.SetVisible(false);
			// nothing was stamped, so make the next tick repaint instead of trusting the standing table
			m_bRowsPainted = false;
			return;
		}

		RK29_WidgetUtil.ClearChildren(m_wRows);

		if (m_wTitle)
			m_wTitle.SetText(factionKey + " - KITS FIELDED");

		WorkspaceWidget ws = GetGame().GetWorkspace();
		int totalAlive = 0;
		int totalMag = 0;

		map<string, int> aliveByLabel = new map<string, int>();
		map<string, int> magByLabel = new map<string, int>();
		map<string, RK29_KitStruct> labelKits = new map<string, RK29_KitStruct>();
		array<string> indexOrder = {};

		int kitCount = mgr.KitCount();
		for (int idx = 0; idx < kitCount; idx++)
		{
			string kitName = mgr.KitNameAt(idx);
			if (kitName == "")
				continue;
			RK29_KitStruct kit = mgr.KitByName(kitName);
			if (!kit || kit.m_sFactionKey != factionKey)
				continue;

			int alive = gm.RK29_GetAliveCount(idx);
			int mag = gm.RK29_GetMagnifiedCount(idx);
			totalAlive += alive;
			totalMag += mag;

			string label = RK29_KitHud.ShortKitName(kitName);
			if (!indexOrder.Contains(label))
			{
				indexOrder.Insert(label);
				labelKits.Set(label, kit);
			}

			aliveByLabel.Set(label, aliveByLabel.Get(label) + alive);
			magByLabel.Set(label, magByLabel.Get(label) + mag);
		}

		if (m_wHeaderRow)
		{
			Widget headMagIcon = m_wHeaderRow.FindAnyWidget("RowMagIcon");
			if (headMagIcon)
				headMagIcon.SetVisible(totalMag > 0);
		}

		array<string> labels = {};
		if (mgr.Setup() && mgr.Setup().m_aClasses)
		{
			foreach (RK29_ClassSetup cls : mgr.Setup().m_aClasses)
			{
				if (!cls)
					continue;
				RK29_KitStruct kit = mgr.KitByName(cls.m_sKitName);
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
		foreach (string leftover : indexOrder)
		{
			if (!labels.Contains(leftover))
				labels.Insert(leftover);
		}

		foreach (string label : labels)
		{
			int alive = aliveByLabel.Get(label);
			if (alive == 0)
				continue;

			Widget row = StampRow(ws, m_wRows, label, alive, magByLabel.Get(label));
			if (!row)
				continue;

			ImageWidget icon = ImageWidget.Cast(row.FindAnyWidget("RowIcon"));
			if (icon)
				RK29_KitHud.SetKitIcon(labelKits.Get(label), icon);
		}

		StampFooterRow(ws, totalAlive, totalMag);
		m_bRowsPainted = true;
	}

	//------------------------------------------------------------------------------------------------
	//! A zero in the optics column is hidden - same judgement on the totals line as on a class one.
	protected Widget StampRow(WorkspaceWidget ws, Widget parent, string label, int alive,
		int magnified)
	{
		Widget row = ws.CreateWidgets(RK29_MenuRowKit.ROW_LAYOUT, parent);
		if (!row)
			return null;

		RK29_WidgetUtil.SetText(row, "RowName", label);
		RK29_WidgetUtil.SetText(row, "RowValue", alive.ToString());

		TextWidget magValue = TextWidget.Cast(row.FindAnyWidget("RowMagValue"));
		if (magValue)
		{
			magValue.SetText(magnified.ToString());
			magValue.SetVisible(magnified > 0);
		}

		return row;
	}

	//------------------------------------------------------------------------------------------------
	protected void StampFooterRow(WorkspaceWidget ws, int totalAlive, int totalMag)
	{
		if (!m_wFooterRow)
			return;

		RK29_WidgetUtil.ClearChildren(m_wFooterRow);

		Widget totals = StampRow(ws, m_wFooterRow, "Total", totalAlive, totalMag);
		if (!totals)
			return;

		// keeps the icon's space so "Total" starts on the same x as the class names. Opacity, not
		// visibility: a hidden widget leaves the layout, and an untextured ImageWidget draws white.
		Widget totalIcon = totals.FindAnyWidget("RowIcon");
		if (totalIcon)
			totalIcon.SetOpacity(0);

		Widget totalBase = totals.FindAnyWidget("RowBase");
		if (totalBase)
			totalBase.SetVisible(false);
	}

	//------------------------------------------------------------------------------------------------
	protected static string LocalFactionKey()
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

	//------------------------------------------------------------------------------------------------
	//! Config display name, else kit name with the "29th <FACTION> - " prefix cut.
	static string ShortKitName(string kitName)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (mgr && mgr.Setup())
		{
			RK29_ClassSetup cls = mgr.Setup().FindClass(kitName);
			if (cls && cls.m_sDisplayName != string.Empty)
				return cls.m_sDisplayName;
		}

		int dash = kitName.IndexOf("- ");
		if (dash >= 0)
			return kitName.Substring(dash + 2, kitName.Length() - dash - 2);
		return kitName;
	}

	//------------------------------------------------------------------------------------------------
	static void SetKitIcon(RK29_KitStruct kit, ImageWidget icon)
	{
		if (!kit || !kit.m_UIInfo)
		{
			icon.SetVisible(false);
			return;
		}
		icon.SetVisible(kit.m_UIInfo.SetIconTo(icon));
	}
}
