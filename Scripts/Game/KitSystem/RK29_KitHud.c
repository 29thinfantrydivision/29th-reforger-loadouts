//------------------------------------------------------------------------------------------------
//! Preround team HUD: own-team kit counts + magnified tally. Client-side view only.
//------------------------------------------------------------------------------------------------
class RK29_KitHud
{
	protected static const ResourceName HUD_LAYOUT = "{AB29C0FFEE292000}UI/KitSystem/RK29_KitHud.layout";
	protected static const ResourceName ROW_LAYOUT = "{AB29C0FFEE291000}UI/KitSystem/RK29_Row.layout";
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
		UpdateTimer(mgr);
		Rebuild();

		if (!s_bShownReported && m_wRows)
		{
			s_bShownReported = true;
			float x, y;
			m_wRows.GetScreenPos(x, y);
			RK29_Log.Trace(string.Format("[RK29] HUD visible at screen %1,%2 | rows parent ok", x, y));
		}
	}

	//--------------------------------------------------------------------------------------------
	//! Briefing countdown in the title band, ticking with the HUD. Rendered only while the
	//! Round Timer addon is loaded - in config-fallback mode there is no clock to show.
	//! Split at the colon into two fixed half-cells so the colon rides the centerline of the
	//! count column below, whatever the digits measure - minutes grow leftward, seconds stay put.
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
		m_wTimerCell = m_wRoot.FindAnyWidget("HudTimerCell");
		m_wTimerMin  = TextWidget.Cast(m_wRoot.FindAnyWidget("HudTimerMin"));
		m_wTimerSec  = TextWidget.Cast(m_wRoot.FindAnyWidget("HudTimerSec"));
		m_wFooterRow = m_wRoot.FindAnyWidget("HudFooterRow");
		m_wHeaderRow = m_wRoot.FindAnyWidget("HudHeaderRow");
		BuildHeaderRow();
	}

	//--------------------------------------------------------------------------------------------
	//! Column headings, built once: a glyph per numeric column, sitting in that column's own
	//! fixed-width cell so it lands dead over the numbers below it. Built from the row layout
	//! precisely so the columns cannot drift apart.
	protected void BuildHeaderRow()
	{
		if (!m_wHeaderRow)
			return;

		WorkspaceWidget ws = GetGame().GetWorkspace();
		Widget header = ws.CreateWidgets(ROW_LAYOUT, m_wHeaderRow);
		if (!header)
			return;

		// a header is a label strip, not a row - shrink it so it sits on top of the table
		// instead of eating a row's worth of height and crowding the first entry
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

		// the count column is headed by the same players glyph the server browser uses,
		// which reads faster than a word and keeps both headings the same shape
		Widget aliveIcon = header.FindAnyWidget("RowAliveIcon");
		if (aliveIcon)
			aliveIcon.SetVisible(true);

		Widget aliveText = header.FindAnyWidget("RowValue");
		if (aliveText)
			aliveText.SetVisible(false);
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
				RK29_Log.Trace("[RK29] HUD hidden - local player has no faction yet");
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

		// with every number in the column hidden, the heading would be labelling nothing
		if (m_wHeaderRow)
		{
			Widget headMagIcon = m_wHeaderRow.FindAnyWidget("RowMagIcon");
			if (headMagIcon)
				headMagIcon.SetVisible(totalMag > 0);
		}

		// row order follows the side config's class order, leftovers append in loadout order;
		// kits sharing a display label sum into one row
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
				value.SetText(alive.ToString());

			// a zero in the optics column is noise - the absence of a number says it
			TextWidget magValue = TextWidget.Cast(row.FindAnyWidget("RowMagValue"));
			if (magValue)
			{
				magValue.SetText(mag.ToString());
				magValue.SetVisible(mag > 0);
			}

			ImageWidget icon = ImageWidget.Cast(row.FindAnyWidget("RowIcon"));
			if (icon)
				RK29_KitHud.SetKitIcon(labelKits.Get(label), icon);
		}

		// the totals are the table's last row, not a caption - same layout, same columns,
		// so the eye keeps scanning straight down instead of re-parsing a sentence
		if (m_wFooterRow)
		{
			Widget old = m_wFooterRow.GetChildren();
			while (old)
			{
				Widget next = old.GetSibling();
				m_wFooterRow.RemoveChild(old);
				old = next;
			}

			Widget totals = ws.CreateWidgets(ROW_LAYOUT, m_wFooterRow);
			if (totals)
			{
				TextWidget totalName = TextWidget.Cast(totals.FindAnyWidget("RowName"));
				if (totalName)
					totalName.SetText("Total");

				TextWidget totalValue = TextWidget.Cast(totals.FindAnyWidget("RowValue"));
				if (totalValue)
					totalValue.SetText(totalAlive.ToString());

				TextWidget totalMagValue = TextWidget.Cast(totals.FindAnyWidget("RowMagValue"));
				if (totalMagValue)
				{
					totalMagValue.SetText(totalMag.ToString());
					totalMagValue.SetVisible(totalMag > 0);
				}

				// the totals line carries no class icon, but it keeps the icon's SPACE so "Total"
				// starts on the same x as the class names above it. Opacity, not visibility: a
				// hidden widget leaves the layout entirely, and an untextured ImageWidget left
				// visible draws as a white square.
				Widget totalIcon = totals.FindAnyWidget("RowIcon");
				if (totalIcon)
					totalIcon.SetOpacity(0);

				Widget totalBase = totals.FindAnyWidget("RowBase");
				if (totalBase)
					totalBase.SetVisible(false); // the footer band already provides the backing
			}
		}
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
		icon.SetVisible(kit.m_UIInfo.SetIconTo(icon));
	}
}
