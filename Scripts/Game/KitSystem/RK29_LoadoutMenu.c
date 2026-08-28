//------------------------------------------------------------------------------------------------
//! The loadout menu: the faction's classes, the chosen class's resolved groups as tiles, and the
//! open tile's entries. Every pick is local until Apply; the server re-validates its own offer.
//! ESC arrives through the shared dialog but this menu answers first - see RetreatFromDetail. Row
//! text that outgrows its cell side-scrolls, authored in the row layouts (clip +
//! SCR_AutomaticScrollComponent over SizeToContent), which have no comment syntax of their own.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
class RK29_LoadoutMenu
{
	protected static const ResourceName DIALOGS_CONF = "{AB29C0FFEEB21070}Configs/UI/RK29_Dialogs.conf";

	//! Opacity multiplies the vanilla dialog widgets' authored alphas, so this thins the panel and
	//! the dim without touching the shared layout. The earlier 0.85/0.6 let the world fight the text.
	protected static const float DIALOG_PANEL_OPACITY = 0.95;
	protected static const float DIALOG_DIM_OPACITY = 0.9;

	//! How much wider than vanilla's Big dialog this menu opens. Applied to what the layout actually
	//! authors - ConfigurableDialog_Big.layout's "SizeBase" SizeLayout, read back through
	//! SizeLayoutWidget.GetWidthOverride() - so a vanilla resize of the Big dialog is only scaled.
	protected static const float DIALOG_WIDTH_SCALE = 1.15;

	//! Fallback for a read that answers nothing - a zero width would scale to zero.
	protected static const float DIALOG_BIG_WIDTH = 1284.0;

	//! The vanilla SizeLayout carrying the dialog's width; unique in this hierarchy.
	protected static const string DIALOG_SIZE_WIDGET = "SizeBase";

	//! Layout units kept free of the screen edges when the widened dialog would not fit - a 4:3
	//! screen is 1440 units wide against the 1476 the scale asks for. The detail column is the
	//! only fill-sized band, so it is what gives.
	protected static const float DIALOG_SCREEN_MARGIN = 40.0;
	protected static const int NOTIFY_HOLD_SECONDS = 3;
	protected static const int WATCH_MS = 500;

	//! World time (ms) gate: PopupMsg queues, so a hammered open key would bank a popup per press.
	protected static float s_fNextNotifyTime;

	//! The body (or no body) the menu was opened over; a change of body closes the menu.
	protected IEntity m_BodyAtOpen;

	//! Set by the first teardown: the engine-close path and the dialog's own close callback can both
	//! arrive for one closing, and the second must find nothing left to do.
	protected bool m_bTornDown;
	protected static ref RK29_LoadoutMenu s_Instance;

	//! Kit name -> that class's last picks. Client-side, session-lifetime, so unapplied tweaks
	//! survive a class switch; the server never reads it and re-validates whatever reaches the wire.
	protected static ref map<string, ref array<ref RK29_ChoicePick>> s_mSavedPicks
		= new map<string, ref array<ref RK29_ChoicePick>>();
	protected static string s_sLastKitName;
	protected SCR_ConfigurableDialogUi m_Dialog;
	protected Widget m_wRoot;

	//! Built here rather than on first open, so every call into them is safe before the dialog exists
	//! and after it has gone; each is handed the root on open and torn down on close.
	protected ref RK29_HoverTip m_HoverTip = new RK29_HoverTip();
	protected ref RK29_MannequinView m_Mannequin = new RK29_MannequinView();

	//! One class per column: each owns that column's widgets, the handlers and books indexing its
	//! stamped rows, and the clicks those rows raise. Built here for the same reason the two above
	//! are - every call into them is safe before the dialog exists and after it has gone.
	protected ref RK29_MenuTileColumn m_Tiles = new RK29_MenuTileColumn();
	protected ref RK29_MenuDetailPanel m_Detail = new RK29_MenuDetailPanel();
	protected ref RK29_MenuInfoBand m_Info = new RK29_MenuInfoBand();
	protected string m_sFactionKey;

	//! The local faction's classes, in setup order; m_iClassIndex addresses this list.
	protected ref array<RK29_ClassSetup> m_aClasses = {};
	protected int m_iClassIndex;

	//! Picks name groups of this offer: the array is swapped on a class change, the old one parked
	//! in s_mSavedPicks.
	protected ref array<ref RK29_ChoicePick> m_aPicks = {};
	protected ref array<ref RK29_ResolvedGroup> m_aOffer = {};

	//! The Appearance tab reads this kit's clothing map rather than the offer: six of the seven slots
	//! have no choice group at all.
	protected ref RK29_KitStruct m_PreviewKit;

	//! Whether Open() got all the way through. A half-built menu is still Closed by Toggle, so every
	//! close-side step is gated on this.
	protected bool m_bOpened;

	//------------------------------------------------------------------------------------------------
	//! Remove-then-Insert so repeat calls and scenario changes never stack duplicate listeners.
	static void RegisterListeners()
	{
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		im.RemoveActionListener("RK29_ToggleKitMenu", EActionTrigger.DOWN, OnToggleStatic);
		im.AddActionListener("RK29_ToggleKitMenu", EActionTrigger.DOWN, OnToggleStatic);
	}

	//------------------------------------------------------------------------------------------------
	//! Whether the menu may open right now, and the player told why not. Alive: a faction and nothing
	//! more - a live re-kit is legal at any point in the round. Dead: the deploy menu must be open,
	//! and that refusal is silent because it also covers the gap between dying and the menu
	//! appearing. Preloading is refused too: the body is moments away.
	protected static bool OpenRefused()
	{
		if (IsLocalAlive())
		{
			PlayerController pc = GetGame().GetPlayerController();
			if (!pc || !SCR_FactionManager.SGetPlayerFaction(pc.GetPlayerId()))
			{
				RK29_Log.Trace("[RK29] kit menu refused - no faction yet");
				NotifyDisabled("you have not joined a faction yet");
				return true;
			}
			return false;
		}

		if (!IsDeployMenuOpen())
		{
			RK29_Log.Trace("[RK29] kit menu refused - dead and not in the deploy menu");
			return true;
		}
		if (IsLocalPreloading())
		{
			RK29_Log.Trace("[RK29] kit menu refused - spawn preloading");
			NotifyDisabled("you are spawning in");
			return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! The popup and not the notification log: that feed is keyed on ENotification values whose text
	//! lives in the notifications conf and only arrives by RPC, while this refusal is client-side.
	protected static void NotifyDisabled(string reason)
	{
		float now = GetGame().GetWorld().GetWorldTime();
		if (now < s_fNextNotifyTime)
			return;
		s_fNextNotifyTime = now + NOTIFY_HOLD_SECONDS * 1000;

		SCR_PopUpNotification popup = SCR_PopUpNotification.GetInstance();
		if (popup)
			popup.PopupMsg("Kit Menu currently disabled", NOTIFY_HOLD_SECONDS, "Reason: " + reason);
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsLocalAlive()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return false;
		SCR_ChimeraCharacter body = SCR_ChimeraCharacter.Cast(pc.GetControlledEntity());
		if (!body)
			return false;
		CharacterControllerComponent ctrl = body.GetCharacterController();
		return ctrl && !ctrl.IsDead();
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsDeployMenuOpen()
	{
		MenuManager mm = GetGame().GetMenuManager();
		return mm && mm.FindMenuByPreset(ChimeraMenuPreset.RespawnSuperMenu) != null;
	}

	//------------------------------------------------------------------------------------------------
	//! True between pressing Deploy and the world finishing streaming in. Best-effort: a game mode
	//! registers one request component per spawn type and we read the first - the watcher covers it.
	protected static bool IsLocalPreloading()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return false;
		SCR_SpawnRequestComponent req = SCR_SpawnRequestComponent.Cast(pc.FindComponent(SCR_SpawnRequestComponent));
		return req && req.IsPreloading();
	}

	//------------------------------------------------------------------------------------------------
	//! Re-checked while open: a changed body, a changed side, or dead with no deploy menu, closes the
	//! menu. The round phase never does - a live re-kit is legal at any point, and a dead pick stashes
	//! for the next body. The side check matters because a GM reassignment neither despawns the body
	//! nor closes the deploy menu, and the classes here were resolved for the side at open: a Confirm
	//! after the change is refused server-side without any feedback to the player.
	protected void WatchRound()
	{
		if (!m_Dialog)
			return;

		PlayerController pc = GetGame().GetPlayerController();
		if (pc && pc.GetControlledEntity() != m_BodyAtOpen)
		{
			RK29_Log.Trace("[RK29] kit menu closed - body changed");
			Close();
			return;
		}
		Faction faction;
		if (pc)
			faction = SCR_FactionManager.SGetPlayerFaction(pc.GetPlayerId());
		if (!faction || faction.GetFactionKey() != m_sFactionKey)
		{
			RK29_Log.Trace("[RK29] kit menu closed - side changed");
			Close();
			return;
		}
		if (!IsLocalAlive() && !IsDeployMenuOpen())
		{
			RK29_Log.Trace("[RK29] kit menu closed - no live body and no deploy menu");
			Close();
			return;
		}

		GetGame().GetCallqueue().CallLater(WatchRound, WATCH_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	protected static void OnToggleStatic()
	{
		Toggle();
	}

	//------------------------------------------------------------------------------------------------
	//! Forget which class the menu last opened on - a static outlives the world.
	static void ForgetSession()
	{
		// A menu open when the world went away: its widgets and body died with the world, but the
		// instance, its round watch and the mannequin pump are queued on the game's call queue, which
		// outlives it. Abandoned, never closed - there is nothing left to close.
		if (s_Instance)
		{
			s_Instance.AbandonWithWorld();
			s_Instance = null;
		}

		s_sLastKitName = "";
		// the popup rate limit is world time, which restarts with the world
		s_fNextNotifyTime = 0;
		if (s_mSavedPicks)
			s_mSavedPicks.Clear();
		RK29_MenuRowKit.ForgetCaches();
	}

	//------------------------------------------------------------------------------------------------
	static void Toggle()
	{
		if (s_Instance)
		{
			s_Instance.Close();
			return;
		}

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr || !mgr.Setup())
		{
			Print("[RK29] loadout menu: not booted", LogLevel.WARNING);
			return;
		}

		if (OpenRefused())
			return;

		// A failed open is still Closed: a dialog may already be standing when the failure is found. What
		// that close must not do is the work of a real close - see m_bOpened and OnDialogClosed.
		RK29_LoadoutMenu menu = new RK29_LoadoutMenu();
		if (menu.Open(mgr))
			s_Instance = menu;
		else
			menu.Close();
	}

	//------------------------------------------------------------------------------------------------
	//! Answers false for any step that could not be completed, and Toggle then Closes the half-built
	//! menu. Nothing here undoes its own work: the close is the one teardown path, and m_bOpened is
	//! what tells it how far this got.
	protected bool Open(notnull RK29_KitManager mgr)
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return false;

		if (!ResolveClasses(mgr, pc))
			return false;

		LoadPicks();

		if (!CreateDialog())
			return false;

		StyleDialog();
		BindWidgets();

		RefreshAll(true);

		RK29_Log.Trace(string.Format("[RK29] loadout menu: %1 class(es) for faction %2",
			m_aClasses.Count(), m_sFactionKey));

		m_BodyAtOpen = pc.GetControlledEntity();

		// removed before it is armed: a second Open would otherwise leave two watches running, each
		// closing the menu the other is still watching
		GetGame().GetCallqueue().Remove(WatchRound);
		GetGame().GetCallqueue().CallLater(WatchRound, WATCH_MS, false);

		m_bOpened = true;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! The local faction's classes, narrowed to the ones the manager offers this player. Reopening
	//! lands on the class last edited rather than index 0 - the menu is a quick-tweak loop - and
	//! falls back to the first, because a config change can take the remembered one away between
	//! sessions.
	protected bool ResolveClasses(notnull RK29_KitManager mgr, notnull PlayerController pc)
	{
		Faction faction = SCR_FactionManager.SGetPlayerFaction(pc.GetPlayerId());
		if (!faction)
		{
			Print("[RK29] loadout menu: no faction yet - join a side first", LogLevel.WARNING);
			return false;
		}
		m_sFactionKey = faction.GetFactionKey();

		foreach (RK29_ClassSetup cls : mgr.Setup().m_aClasses)
		{
			if (!cls || cls.m_sKitName == "" || cls.m_sSideFactionKey != m_sFactionKey)
				continue;
			m_aClasses.Insert(cls);
		}
		if (m_aClasses.IsEmpty())
		{
			Print(string.Format("[RK29] loadout menu: no migrated class found for faction %1",
				m_sFactionKey), LogLevel.WARNING);
			return false;
		}
		if (!FilterClassesToOffer(mgr))
			return false;

		m_iClassIndex = 0;
		if (s_sLastKitName == "")
			return true;

		foreach (int i, RK29_ClassSetup remembered : m_aClasses)
		{
			if (remembered && remembered.m_sKitName == s_sLastKitName)
			{
				m_iClassIndex = i;
				break;
			}
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Hosts the menu in the shared dialog preset, which provides the action context, the cursor and
	//! ESC. The dialog object is ours so ESC can be caught before it closes - see RK29_LoadoutDialog.
	//! CreateByPreset adopts a provided object only where the base layout authors no dialog handler,
	//! which ConfigurableDialog.layout does not. Both inserts come back in ReleaseBookkeeping.
	protected bool CreateDialog()
	{
		RK29_LoadoutDialog loadoutDialog = new RK29_LoadoutDialog();
		loadoutDialog.m_Menu = this;

		m_Dialog = SCR_ConfigurableDialogUi.CreateFromPreset(DIALOGS_CONF, "loadout", loadoutDialog);
		if (!m_Dialog)
		{
			Print("[RK29] loadout menu: dialog preset failed to open", LogLevel.WARNING);
			return false;
		}

		m_Dialog.m_OnClose.Insert(OnDialogClosed);
		m_Dialog.m_OnConfirm.Insert(OnConfirmDialog);

		m_wRoot = m_Dialog.GetRootWidget();
		return m_wRoot != null;
	}

	//------------------------------------------------------------------------------------------------
	//! What this menu asks of the shared dialog preset, from script only - nothing here may be
	//! written into the layout, which every other dialog in the game also uses.
	//!
	//! The four bands are sized in RK29_LoadoutMenu.layout and not here, so read them there before
	//! changing anything below:
	//!  - ColClasses  WidthOverride 240 - a class name beside its icon, and nothing longer.
	//!  - ColTiles    WidthOverride 311 - a 64-tall TileRow: render, name, spare-magazine count.
	//!  - ColInfo     WidthOverride 230 - weight, traits and a preset name.
	//!  - ColDetail   no width at all - the one fill band, which is therefore what gives when
	//!    WidenDialog runs out of screen (see DIALOG_SCREEN_MARGIN).
	//!  - all four   HeightOverride 720 - what fits between the title row and the button row of the
	//!    Big dialog's fixed 951 (ConfigurableDialog_Big.layout Min/MaxDesiredHeight).
	protected void StyleDialog()
	{
		// The dialog drops our content into a centred slot, leaving dead bands above and below the
		// columns. ContentLayoutContainer is a Fill slot running from under the title to the button row,
		// so stretching it hands the columns that whole run instead of a centred band inside it
		// (CreateByPreset does the horizontal setter of the same slot class on this very widget).
		Widget content = RK29_WidgetUtil.Require(m_wRoot, "RK29_LoadoutMenu");
		if (content)
			AlignableSlot.SetVerticalAlign(content, LayoutVerticalAlign.Stretch);

		// thin the dialog's own paint so the world stays present behind the menu. "Background" is the
		// panel behind the columns and "OuterBackgroundOverlay" the screen dim - vanilla names, unique
		// here
		Widget dialogPanel = m_wRoot.FindAnyWidget("Background");
		if (dialogPanel)
			dialogPanel.SetOpacity(DIALOG_PANEL_OPACITY);

		Widget outerDim = m_wRoot.FindAnyWidget("OuterBackgroundOverlay");
		if (outerDim)
			outerDim.SetOpacity(DIALOG_DIM_OPACITY);

		WidenDialog();
	}

	//------------------------------------------------------------------------------------------------
	//! Each column is required rather than merely looked up: a column that is not there is a band
	//! that silently never fills, and the warning is what names which one. The panels are handed
	//! theirs and own them from here.
	protected void BindWidgets()
	{
		Widget colClasses = RK29_WidgetUtil.Require(m_wRoot, "ColClasses");
		Widget colTiles = RK29_WidgetUtil.Require(m_wRoot, "ColTiles");
		Widget colDetail = RK29_WidgetUtil.Require(m_wRoot, "ColDetail");
		Widget colInfo = RK29_WidgetUtil.Require(m_wRoot, "ColInfo");
		TextWidget detailTitle = TextWidget.Cast(RK29_WidgetUtil.Require(m_wRoot, "DetailTitle"));
		Widget detailTitleBand = RK29_WidgetUtil.Require(m_wRoot, "DetailTitleSize");

		m_Tiles.Init(this, colClasses, colTiles);
		m_Detail.Init(this, colDetail, detailTitle, detailTitleBand);
		m_Info.Init(this, colInfo);

		// both subsystems find their own widgets off the root handed to them here, and both are given
		// back
		m_HoverTip.Bind(m_wRoot);
		m_Mannequin.Init(m_wRoot);
	}

	//------------------------------------------------------------------------------------------------
	//! Opens the shared Big dialog wider than vanilla authors it, from script only. The authored
	//! width is read and multiplied: GetWidthOverride() is a plain accessor for what the layout set
	//! at creation, not a laid-out result, so it is honest in the frame the dialog is built in. The
	//! widget is created fresh by CreateByPreset on every opening, so this cannot compound.
	protected void WidenDialog()
	{
		Widget sizeBase = m_wRoot.FindAnyWidget(DIALOG_SIZE_WIDGET);
		SizeLayoutWidget dialogSize = SizeLayoutWidget.Cast(sizeBase);
		if (!dialogSize)
			return;

		float authored = dialogSize.GetWidthOverride();
		if (authored <= 0)
			authored = DIALOG_BIG_WIDTH;

		float width = authored * DIALOG_WIDTH_SCALE;
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (ws)
		{
			float available = ws.DPIUnscale(ws.GetWidth()) - DIALOG_SCREEN_MARGIN;
			if (available > 0 && width > available)
				width = available;
		}
		dialogSize.SetWidthOverride(width);
	}

	//------------------------------------------------------------------------------------------------
	//! Narrows the faction's classes to what the manager offers the side - the same answer
	//! RK29_KitManager.HandleKitRequest_S validates a request against, so the menu cannot list a
	//! class the server would reject; kept as the menu's end of that one chokepoint. An empty offer
	//! means "this side has no kits at all" (a dev world), so the unfiltered list stands and says so.
	protected bool FilterClassesToOffer(notnull RK29_KitManager mgr)
	{
		array<string> offered = {};
		mgr.GetOfferedKits(m_sFactionKey, offered);

		if (offered.IsEmpty())
		{
			Print("[RK29] loadout menu: offer list is empty - listing every class of "
				+ m_sFactionKey, LogLevel.NORMAL);
			return true;
		}

		// RemoveOrdered, not Remove: Remove is a swap-remove that drops the last element into the hole,
		// and this list's order is the class column
		for (int i = m_aClasses.Count() - 1; i >= 0; i--)
		{
			if (!offered.Contains(m_aClasses[i].m_sKitName))
				m_aClasses.RemoveOrdered(i);
		}

		if (m_aClasses.IsEmpty())
		{
			Print("[RK29] loadout menu: no offered kit has a migrated class on faction "
				+ m_sFactionKey, LogLevel.WARNING);
			return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	void Close()
	{
		if (m_Dialog)
		{
			// the close callback runs the cleanup
			m_Dialog.Close();
			return;
		}
		OnDialogClosed();
	}

	//------------------------------------------------------------------------------------------------
	//! The dialog went away without telling us - see RK29_LoadoutDialog.OnMenuClose. Same teardown,
	//! once: the ordinary close arrives through m_OnClose a moment later and must find nothing to do.
	void OnDialogClosedByEngine()
	{
		if (m_Dialog)
			OnDialogClosed();
	}

	//------------------------------------------------------------------------------------------------
	//! What both teardown paths let go of, in one place: the queued calls this object owns, which
	//! outlive the world; every stamped row's handler and the books indexing those rows, which the
	//! three panels give back themselves; and every handle into the dialog's tree, the two invoker
	//! listeners included - the dialog is held by the menu manager, so a listener left on it keeps
	//! this whole object alive. not here, because the paths disagree: parking the picks, and tearing
	//! the mannequin's body out of the world.
	protected void ReleaseBookkeeping()
	{
		GetGame().GetCallqueue().Remove(WatchRound);
		GetGame().GetCallqueue().Remove(AfterPickChangedDeferred);

		// dropped before the root it was found through - a stale owner would outlive the row it names
		m_HoverTip.Teardown();

		// each column's own handlers, books and widget handles, the queued call one of them owns
		// included
		m_Tiles.Release();
		m_Detail.Release();
		m_Info.Release();

		m_aOffer.Clear();
		m_PreviewKit = null;

		if (m_Dialog)
		{
			m_Dialog.m_OnClose.Remove(OnDialogClosed);
			m_Dialog.m_OnConfirm.Remove(OnConfirmDialog);
		}

		m_Dialog = null;
		m_wRoot = null;
	}

	//------------------------------------------------------------------------------------------------
	//! ForgetSession's path for a menu whose world is gone: marked torn down so a late engine close
	//! is a no-op, nothing saved, and the mannequin abandoned - the body it would delete died with
	//! the world.
	void AbandonWithWorld()
	{
		if (m_bTornDown)
			return;
		m_bTornDown = true;

		m_Mannequin.AbandonWithWorld();
		ReleaseBookkeeping();
	}

	//------------------------------------------------------------------------------------------------
	//! The ordinary close, inside a living world. Both halves that are not ReleaseBookkeeping's are
	//! gated on the menu having actually opened: a failed Open is Closed too (see Toggle) and must
	//! not park an empty pick set over the player's real edits, nor tear down subsystems never handed
	//! a root.
	//! The dialog's m_OnClose invokes with the dialog; the two direct callers pass nothing.
	protected void OnDialogClosed(SCR_ConfigurableDialogUi dialog = null)
	{
		if (m_bTornDown)
			return;
		m_bTornDown = true;

		if (m_bOpened)
		{
			SavePicks();

			// the pump off the call queue, the camera handler off the workspace, the body out of the world,
			// in that order and for the reasons stated there
			m_Mannequin.Teardown();
		}

		ReleaseBookkeeping();

		if (s_Instance == this)
			s_Instance = null;
	}

	//------------------------------------------------------------------------------------------------
	//! Parks the open class's picks under its kit name - the array goes in as it stands, so a reopen
	//! resumes the same edits. Runs on a class switch and on close, which is why s_sLastKitName is
	//! kept current here; Open() reads it back.
	void SavePicks()
	{
		RK29_ClassSetup cls = CurrentClass();
		if (!cls || cls.m_sKitName == "")
			return;

		s_mSavedPicks.Set(cls.m_sKitName, m_aPicks);
		s_sLastKitName = cls.m_sKitName;
	}

	//------------------------------------------------------------------------------------------------
	//! A class this session has never edited starts on a fresh array, which is what shows its
	//! defaults.
	void LoadPicks()
	{
		m_aPicks = new array<ref RK29_ChoicePick>();

		RK29_ClassSetup cls = CurrentClass();
		if (!cls || cls.m_sKitName == "")
			return;

		array<ref RK29_ChoicePick> saved;
		if (s_mSavedPicks.Find(cls.m_sKitName, saved) && saved)
			m_aPicks = saved;
	}

	//============================================================================================
	// State
	//============================================================================================
	//------------------------------------------------------------------------------------------------
	//! Null for an index this column no longer has - a click routed from a handler whose row the
	//! rebuild has already freed. Same shape as DetailRow, TileGroupAt and PresetName.
	RK29_ClassSetup ClassAt(int index)
	{
		if (index < 0 || index >= m_aClasses.Count())
			return null;

		return m_aClasses[index];
	}

	//------------------------------------------------------------------------------------------------
	RK29_ClassSetup CurrentClass()
	{
		return ClassAt(m_iClassIndex);
	}

	//------------------------------------------------------------------------------------------------
	//! What the three column panels reach the shell through, and the whole of it: the offer, the
	//! picks and the open class live here, so a panel that wants one asks rather than keeping a
	//! second copy that can drift.
	array<ref RK29_ResolvedGroup> Offer()
	{
		return m_aOffer;
	}

	//------------------------------------------------------------------------------------------------
	array<ref RK29_ChoicePick> Picks()
	{
		return m_aPicks;
	}

	//------------------------------------------------------------------------------------------------
	void SetPicks(array<ref RK29_ChoicePick> picks)
	{
		m_aPicks = picks;
	}

	//------------------------------------------------------------------------------------------------
	array<RK29_ClassSetup> Classes()
	{
		return m_aClasses;
	}

	//------------------------------------------------------------------------------------------------
	int ClassIndex()
	{
		return m_iClassIndex;
	}

	//------------------------------------------------------------------------------------------------
	void SetClassIndex(int index)
	{
		m_iClassIndex = index;
	}

	//------------------------------------------------------------------------------------------------
	string FactionKey()
	{
		return m_sFactionKey;
	}

	//------------------------------------------------------------------------------------------------
	RK29_KitStruct PreviewKit()
	{
		return m_PreviewKit;
	}

	//------------------------------------------------------------------------------------------------
	RK29_HoverTip HoverTip()
	{
		return m_HoverTip;
	}

	//------------------------------------------------------------------------------------------------
	RK29_MannequinView Mannequin()
	{
		return m_Mannequin;
	}

	//------------------------------------------------------------------------------------------------
	Widget Root()
	{
		return m_wRoot;
	}

	//------------------------------------------------------------------------------------------------
	bool IsTornDown()
	{
		return m_bTornDown;
	}

	//------------------------------------------------------------------------------------------------
	RK29_MenuDetailPanel Detail()
	{
		return m_Detail;
	}

	//------------------------------------------------------------------------------------------------
	RK29_MenuInfoBand Info()
	{
		return m_Info;
	}

	//------------------------------------------------------------------------------------------------
	//! The offer is rebuilt against the current picks, not the defaults: a weapon change swaps
	//! the groups that weapon owns, so the tiles have to be re-resolved after every pick.
	protected void RebuildOffer()
	{
		m_aOffer.Clear();

		RK29_ClassSetup cls = CurrentClass();
		if (!cls)
			return;

		RK29_KitSetup setup = RK29_MenuRowKit.Setup();
		if (!setup)
			return;

		RK29_KitResolve.BuildOffer(cls, setup, m_aPicks, m_aOffer);
		// a pick the offer ruled out is gone or bounded now, and the offer was built with it as it was
		if (DropBlockedPicks())
		{
			m_aOffer.Clear();
			RK29_KitResolve.BuildOffer(cls, setup, m_aPicks, m_aOffer);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! A pick the offer has since ruled out is thrown away rather than kept. Keeping it works, but a
	//! vest quietly returning several picks later reads as the menu acting by itself, and losing it
	//! is one click to recover. This is also what makes a loaded preset honest: everything
	//! CountPresetStale counts against a saved wire is dropped or bounded here, so the live picks
	//! after a load are what the kit issues, and re-saving clears the "(outdated)" mark. Mirror
	//! that counter when adding a category. A group the offer lacks is kept untouched: the other
	//! rifle's ammo counts come back on a switch and nothing reads them meanwhile. RemoveOrdered,
	//! not Remove: a swap-remove would scramble the pick order.
	protected bool DropBlockedPicks()
	{
		bool changed = false;
		for (int i = m_aPicks.Count() - 1; i >= 0; i--)
		{
			RK29_ChoicePick pick = m_aPicks[i];
			if (!pick)
				continue;

			RK29_ResolvedGroup g = RK29_KitResolve.FindGroup(m_aOffer, pick.m_sGroup);
			if (!g)
				continue;

			// a bare pick is an answer only where the group offers None
			if (pick.m_sEntry == "")
			{
				if (!g.m_bAllowEmpty)
				{
					m_aPicks.RemoveOrdered(i);
					changed = true;
				}
				continue;
			}

			// an entry the group no longer has (a catalog revision) goes the same way as a blocked one
			RK29_ResolvedEntry e = g.FindEntry(pick.m_sEntry);
			if (!e || e.m_bBlocked)
			{
				m_aPicks.RemoveOrdered(i);
				changed = true;
				continue;
			}

			if (g.m_eKind == RK29_EChoiceKind.EXCLUSIVE)
				continue;

			// a count outside the entry's bounds is read clamped everywhere; write the clamp back
			array<ref RK29_ChoicePick> single = {};
			single.Insert(pick);
			int bounded = RK29_KitResolve.PickedCount(g, e, single);
			if (bounded != pick.m_iCount)
			{
				pick.m_iCount = bounded;
				changed = true;
			}
		}

		// a BUDGETED group whose picks overspend is issued as its authored defaults wholesale
		// (ResolveCounts); its picks go the same way so the wire says what the kit issues
		foreach (RK29_ResolvedGroup budgeted : m_aOffer)
		{
			if (!budgeted || budgeted.m_eKind != RK29_EChoiceKind.BUDGETED || budgeted.m_iBudget <= 0)
				continue;
			if (RK29_MenuRowKit.GroupSpendOf(budgeted, m_aPicks) <= budgeted.m_iBudget)
				continue;
			for (int i = m_aPicks.Count() - 1; i >= 0; i--)
			{
				if (m_aPicks[i] && m_aPicks[i].m_sGroup == budgeted.m_sId)
				{
					m_aPicks.RemoveOrdered(i);
					changed = true;
				}
			}
		}
		return changed;
	}

	//------------------------------------------------------------------------------------------------
	//! The one preview resolve of a rebuild, at the head of the three flows that move the offer
	//! (Open, AfterPickChanged, a class switch): the mannequin and the weight row otherwise ask for
	//! byte-identical answers in the same frame. False is handed on as null - the mannequin takes the
	//! body away, the weight row stamps nothing. The preview optic is a second view of `orders`.
	protected bool ResolvePreview(out RK29_KitStruct edited,
		out array<ref RK29_AttachmentOrder> orders,
		out map<int, ref array<ref RK29_LoadedPick>> loadedMags)
	{
		edited = null;
		orders = null;
		loadedMags = null;

		RK29_ClassSetup cls = CurrentClass();
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!cls || !mgr)
			return false;

		ResourceName unusedOptic;
		return mgr.RK29_ResolvePreviewKit(cls.m_sKitName, m_aPicks, edited, unusedOptic, orders,
			loadedMags);
	}

	//------------------------------------------------------------------------------------------------
	//! The whole rebuild, in the one order all three flows that move the offer need. The order is
	//! load-bearing: the tiles picture the body's guns and the weight row weighs the body itself, so
	//! the body must carry the new kit before either is stamped. one resolve feeds all three, and the
	//! dress is the real placement pass, cargo and all (~14 ms), because a dressed-only body cannot
	//! be weighed. A pick never moves the class column, but always the weight half.
	//! Not every rebuild comes through here: a path that moves no pick may call BuildTiles and
	//! BuildDetail alone - RetreatFromDetail, OnTileClicked and OnModeClicked all do.
	void RefreshAll(bool rebuildClassColumn)
	{
		RebuildOffer();

		// the panel itself can have gone away with what moved the offer - a weapon swap takes that
		// weapon's own groups with it - and then both of its folds go too
		if (m_Detail.OpenGroup() != "" && !RK29_KitResolve.FindGroup(m_aOffer, m_Detail.OpenGroup()))
			m_Detail.ClearFold();

		// a weapon pick keeps its own group selected - only the owned set behind it swaps - but a
		// override can still take the group itself away
		if (m_Detail.SelectedWeaponGroup() != ""
			&& !RK29_KitResolve.FindGroup(m_aOffer, m_Detail.SelectedWeaponGroup()))
			m_Detail.SetSelectedWeaponGroup("");

		RK29_KitStruct preview;
		array<ref RK29_AttachmentOrder> orders;
		map<int, ref array<ref RK29_LoadedPick>> loadedMags;
		ResolvePreview(preview, orders, loadedMags);

		DressMannequin(preview, orders, loadedMags);

		if (rebuildClassColumn)
			m_Tiles.BuildClassColumn();

		m_Tiles.BuildTiles();
		m_Detail.BuildDetail();
		m_Info.BuildInfoPanel();
	}

	//------------------------------------------------------------------------------------------------
	void AfterPickChanged()
	{
		RefreshAll(false);
	}

	//------------------------------------------------------------------------------------------------
	//! EXCLUSIVE groups: one pick per group, so the group's old pick goes first.
	void SetPick(string groupId, string entryId)
	{
		for (int i = m_aPicks.Count() - 1; i >= 0; i--)
		{
			if (m_aPicks[i] && m_aPicks[i].m_sGroup == groupId)
				m_aPicks.Remove(i);
		}

		RK29_ChoicePick pick = new RK29_ChoicePick();
		pick.m_sGroup = groupId;
		pick.m_sEntry = entryId;
		m_aPicks.Insert(pick);
	}

	//------------------------------------------------------------------------------------------------
	//! COUNTED/BUDGETED groups: one pick per (group, entry) pair, carrying the count.
	void SetCount(string groupId, string entryId, int newCount)
	{
		for (int i = m_aPicks.Count() - 1; i >= 0; i--)
		{
			if (m_aPicks[i] && m_aPicks[i].m_sGroup == groupId && m_aPicks[i].m_sEntry == entryId)
				m_aPicks.Remove(i);
		}

		RK29_ChoicePick pick = new RK29_ChoicePick();
		pick.m_sGroup = groupId;
		pick.m_sEntry = entryId;
		pick.m_iCount = newCount;
		m_aPicks.Insert(pick);
	}

	//------------------------------------------------------------------------------------------------
	//! Gives one widget of a stamped row the identity a click carries back: which kind of row and
	//! which row of that kind. A layout with no such button is named in the log and left unwired -
	//! the row root carries no button component, so a fallback would make the kind un-clickable.
	//! The click arrives through the layout's SCR_ModularButtonComponent: the handler is parked as
	//! its user data and m_OnClicked calls back, which buys the hover tint, focus ring and sounds.
	//! That branch returns without AddHandler, so the handler's own OnChange never fires on such a
	//! widget: the two edit boxes depend on carrying no modular button component.
	void AttachHandler(notnull Widget row, string buttonName,
		RK29_EMenuRowKind kind, int index, notnull array<ref RK29_LoadoutRowHandler> store)
	{
		Widget button = row.FindAnyWidget(buttonName);
		if (!button)
		{
			Print(string.Format("[RK29] loadout menu: row layout has no '%1' widget - row kind %2"
				+ " is not clickable", buttonName, kind), LogLevel.WARNING);
			return;
		}

		RK29_LoadoutRowHandler handler = new RK29_LoadoutRowHandler();
		handler.m_Menu = this;
		handler.m_eKind = kind;
		handler.m_iIndex = index;
		store.Insert(handler);

		SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.FindComponent(button);
		if (comp)
		{
			comp.SetData(handler);
			comp.m_OnClicked.Insert(OnModularButtonClicked);
			return;
		}

		button.AddHandler(handler);
	}

	//------------------------------------------------------------------------------------------------
	//! The row identity is read straight back off the component's user data - the handler
	//! AttachHandler parked there - so one callback serves every button in the menu.
	protected void OnModularButtonClicked(SCR_ModularButtonComponent comp)
	{
		if (!comp)
			return;

		RK29_LoadoutRowHandler handler = RK29_LoadoutRowHandler.Cast(comp.GetData());
		if (!handler)
			return;

		OnRowClicked(handler.m_eKind, handler.m_iIndex);
	}

	//------------------------------------------------------------------------------------------------
	//! Lights or unlights the amber plate that says a row is the picked one. The toggle is set on the
	//! row's modular button rather than on the plate: the button's SCR_ButtonEffectVisibility shows
	//! it, and the button then knows it is picked, which its colour effect needs. plateName is still
	//! load-bearing: a button effect only finds widgets under its own button, and a preset row's
	//! RowBg sits outside RowButton so the highlight spans the delete glyph. The plate names in use:
	//! RowBg on Row, TileRow and PresetRow rows, LoadedBg on a CountRow, and the two mode-tab plates
	//! on the menu layout's own root.
	void SetPlateToggled(notnull Widget row, string buttonName, string plateName,
		bool toggled)
	{
		Widget button = row.FindAnyWidget(buttonName);
		if (!button)
			button = row;

		SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.FindComponent(button);
		if (comp && RK29_MenuRowKit.ButtonOwnsPlate(comp))
		{
			comp.SetToggled(toggled, false, true);
			// a button authored non-toggleable ignores SetToggled outright; the plate is then painted
			// by hand below rather than left dark for good
			if (comp.GetToggled() == toggled)
				return;
		}

		Widget plate = row.FindAnyWidget(plateName);
		if (plate)
			plate.SetVisible(toggled);
	}

	//============================================================================================
	// Mannequin
	//============================================================================================
	//------------------------------------------------------------------------------------------------
	//! Re-dress the mannequin from the open class and the picks as they stand. Run on every offer
	//! change, before BuildTiles and BuildInfoPanel, and three readers depend on that order: the
	//! weapon tiles read their guns off this body, the detail band draws him, the info band weighs
	//! him. The body carries the whole kit, cargo included - DressLoaded runs RK29_KitApply.Place,
	//! the same pass a live soldier gets - about 14 ms a pick, the price of a right weight row. On
	//! failure the body is taken away, not left dressed as the last kit.
	protected void DressMannequin(RK29_KitStruct resolved,
		array<ref RK29_AttachmentOrder> resolvedOrders,
		map<int, ref array<ref RK29_LoadedPick>> resolvedMags)
	{
		// kept for the Appearance tab, which needs the resolved clothing rather than the offer
		m_PreviewKit = resolved;

		RK29_ClassSetup cls = CurrentClass();
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!cls || !mgr)
		{
			m_Mannequin.ClearMannequin();
			return;
		}

		RK29_KitStruct edited = resolved;
		array<ref RK29_AttachmentOrder> orders = resolvedOrders;
		map<int, ref array<ref RK29_LoadedPick>> loadedMags = resolvedMags;
		if (!edited)
		{
			ResourceName unusedOptic;
			if (!mgr.RK29_ResolvePreviewKit(cls.m_sKitName, m_aPicks, edited, unusedOptic, orders,
				loadedMags))
			{
				Print(string.Format("[RK29] loadout menu: mannequin skipped - '%1' did not"
					+ " resolve", cls.m_sKitName), LogLevel.WARNING);
				m_Mannequin.ClearMannequin();
				return;
			}
		}

		// a dress that could not stand a body up takes the old one away rather than leaving the previous
		// class's soldier for the tiles to picture and the weight row to state
		m_Mannequin.DressLoaded(cls.BodyPrefab(), edited, loadedMags, orders,
			"mannequin '" + cls.m_sKitName + "'");
	}

	//============================================================================================
	// Labels
	//============================================================================================
	//------------------------------------------------------------------------------------------------
	ResourceName ResolvedWeaponPrefabOf(notnull RK29_ResolvedGroup g)
	{
		return WeaponPrefabOf(RK29_KitResolve.PickedEntry(g, m_aPicks));
	}

	//------------------------------------------------------------------------------------------------
	//! Names the gun, not the group: "AKM", never "Rifle: AKM" - the caption above says which slot.
	string WeaponLabelOf(notnull RK29_ResolvedGroup g)
	{
		RK29_ResolvedEntry chosen = RK29_KitResolve.PickedEntry(g, m_aPicks);

		ResourceName prefab = WeaponPrefabOf(chosen);
		if (prefab != ResourceName.Empty)
		{
			string itemName = RK29_ItemNames.Get(prefab);
			if (itemName != "")
				return itemName;
		}

		return EntryLabel(chosen);
	}

	//------------------------------------------------------------------------------------------------
	string ResolvedWeaponIdOf(notnull RK29_ResolvedGroup g)
	{
		RK29_ResolvedEntry e = RK29_KitResolve.PickedEntry(g, m_aPicks);
		if (!e)
			return "";

		RK29_EntryWeapon w = RK29_EntryWeapon.Cast(e.m_Def);
		if (!w)
			return "";

		return w.m_sWeapon;
	}

	//------------------------------------------------------------------------------------------------
	//! What one entry of an EXCLUSIVE list is pictured by: a weapon's prefab, an attachment catalog
	//! definition's, or - for an item - whatever ItemEntryPrefab answers. The item case is easy to
	//! miss: a counted item group draws chips that fetch their own renders, but a vest list is
	//! exclusive and items, so its rows came up blank. EntryLabel reads this too.
	ResourceName EntryPreviewPrefab(RK29_ResolvedEntry e, RK29_ResolvedGroup g = null)
	{
		if (!e)
			return ResourceName.Empty;

		ResourceName weapon = WeaponPrefabOf(e);
		if (weapon != ResourceName.Empty)
			return weapon;

		RK29_AttachmentDef adef = AttachmentDefOf(e);
		if (adef)
			return adef.m_sPrefab;

		return ItemEntryPrefab(g, e);
	}

	//------------------------------------------------------------------------------------------------
	protected ResourceName WeaponPrefabOf(RK29_ResolvedEntry e)
	{
		if (!e)
			return ResourceName.Empty;

		RK29_EntryWeapon w = RK29_EntryWeapon.Cast(e.m_Def);
		if (!w)
			return ResourceName.Empty;

		RK29_KitSetup setup = RK29_MenuRowKit.Setup();
		if (!setup)
			return ResourceName.Empty;

		return RK29_KitResolve.WeaponPrefabOfId(setup, w.m_sWeapon, m_sFactionKey);
	}

	//------------------------------------------------------------------------------------------------
	//! The rule is the resolver's - by the payload id, and by the entry's own id for one whose
	//! payload names nothing; see RK29_KitResolve.AttachmentDefOf for why reading the entry id alone
	//! misses one. All this adds is the setup to ask it against.
	protected RK29_AttachmentDef AttachmentDefOf(RK29_ResolvedEntry e)
	{
		if (!e)
			return null;

		RK29_KitSetup setup = RK29_MenuRowKit.Setup();
		if (!setup)
			return null;

		return RK29_KitResolve.AttachmentDefOf(setup, e);
	}

	//------------------------------------------------------------------------------------------------
	//! Authored label, else the resolved item's own in-game name, else the entry id.
	protected string EntryLabel(RK29_ResolvedEntry e)
	{
		if (!e)
			return "None";

		if (e.m_Def && e.m_Def.m_sDisplayName != "")
			return e.m_Def.m_sDisplayName;

		ResourceName prefab = EntryPreviewPrefab(e);
		if (prefab != ResourceName.Empty)
		{
			string itemName = RK29_ItemNames.Get(prefab);
			if (itemName != "")
				return itemName;
		}

		return e.m_sId;
	}

	//------------------------------------------------------------------------------------------------
	string EntryLabelIn(notnull RK29_ResolvedGroup g, RK29_ResolvedEntry e)
	{
		if (e && RK29_EntryItem.Cast(e.m_Def))
			return ItemEntryLabel(g, e);

		return EntryLabel(e);
	}

	//------------------------------------------------------------------------------------------------
	//! An item row names the item, not the entry: "7.62x39mm 30rnd Magazine", never "ball" - the
	//! entry id is a word in the group's own vocabulary. The authored label, then the id, are the
	//! fallbacks.
	protected string ItemEntryLabel(notnull RK29_ResolvedGroup g, notnull RK29_ResolvedEntry e)
	{
		ResourceName prefab = ItemEntryPrefab(g, e);
		if (prefab != ResourceName.Empty)
		{
			string itemName = RK29_ItemNames.Get(prefab);
			if (itemName != "")
				return itemName;
		}

		if (e.m_Def && e.m_Def.m_sDisplayName != "")
			return e.m_Def.m_sDisplayName;

		return e.m_sId;
	}

	//------------------------------------------------------------------------------------------------
	//! What one item entry will become, resolved the way the apply pass resolves it: through the
	//! owning weapon's own ammo table where the group has an owner, the faction item catalog
	//! otherwise. Empty for an entry this faction does not field, and for one that is not an item.
	//! The group is optional: it matters only for a variant or a weapon-scoped alias, where the
	//! round is a different prefab per gun.
	ResourceName ItemEntryPrefab(RK29_ResolvedGroup g, notnull RK29_ResolvedEntry e)
	{
		RK29_EntryItem item = RK29_EntryItem.Cast(e.m_Def);
		if (!item)
			return ResourceName.Empty;

		RK29_KitSetup setup = RK29_MenuRowKit.Setup();
		if (!setup)
			return ResourceName.Empty;

		ResourceName ownerPrefab;
		RK29_WeaponDef ownerDef;
		if (g && g.m_sOwnerWeapon != "")
		{
			ownerPrefab = RK29_KitResolve.WeaponPrefabOfId(setup, g.m_sOwnerWeapon, m_sFactionKey);
			ownerDef = setup.FindWeaponDef(g.m_sOwnerWeapon);
		}

		return RK29_KitResolve.ResolveItemPrefabFor(item, ownerPrefab, ownerDef, m_sFactionKey,
			setup);
	}

	//------------------------------------------------------------------------------------------------
	//! What an EXCLUSIVE tile's row is named: the one thing this group currently gives the kit, by
	//! name - the caption above has already said which group is being answered. Counted groups never
	//! come here: their row carries a chip per item, because one name cannot say how many of each.
	string ExclusiveTileSummary(notnull RK29_ResolvedGroup g)
	{
		RK29_ResolvedEntry chosen = RK29_KitResolve.PickedEntry(g, m_aPicks);
		if (!chosen)
			return "None";

		return EntryLabelIn(g, chosen);
	}

	//------------------------------------------------------------------------------------------------
	//! The picture for the answer ExclusiveTileSummary names, so the tile can show the thing as well
	//! as call it. Empty for a group holding no pick, which is the same empty a bare garment slot
	//! hands FillPreview.
	ResourceName ExclusiveTilePreview(notnull RK29_ResolvedGroup g)
	{
		RK29_ResolvedEntry chosen = RK29_KitResolve.PickedEntry(g, m_aPicks);
		if (!chosen)
			return ResourceName.Empty;

		return EntryPreviewPrefab(chosen, g);
	}

	//============================================================================================
	// Input
	//============================================================================================
	//------------------------------------------------------------------------------------------------
	//! Every row kind but the two edit boxes: COUNT_EDIT and PRESET_SAVE_EDIT are dispatched from
	//! RK29_LoadoutRowHandler.OnChange, on commit only, and never arrive here.
	protected void OnRowClicked(RK29_EMenuRowKind kind, int index)
	{
		if (kind == RK29_EMenuRowKind.MODE_TAB)
		{
			m_Tiles.OnModeClicked(index);
			return;
		}
		if (kind == RK29_EMenuRowKind.CLASS_ROW)
		{
			m_Tiles.OnClassClicked(index);
			return;
		}
		if (kind == RK29_EMenuRowKind.TILE)
		{
			m_Tiles.OnTileClicked(index);
			return;
		}
		if (kind == RK29_EMenuRowKind.DETAIL_ENTRY)
		{
			m_Detail.OnDetailClicked(index);
			return;
		}
		if (kind == RK29_EMenuRowKind.COUNT_MINUS)
		{
			m_Detail.OnCountStep(index, -1);
			return;
		}
		if (kind == RK29_EMenuRowKind.COUNT_PLUS)
		{
			m_Detail.OnCountStep(index, 1);
			return;
		}
		if (kind == RK29_EMenuRowKind.WEAPON_FOLD)
		{
			m_Detail.ToggleWeaponList();
			return;
		}
		if (kind == RK29_EMenuRowKind.LOADED_TOGGLE)
		{
			m_Detail.OnLoadedToggle(index);
			return;
		}
		if (kind == RK29_EMenuRowKind.ATTACHMENT_FOLD)
		{
			m_Detail.OnAttachmentGroupClicked(index);
			return;
		}
		if (kind == RK29_EMenuRowKind.PRESET_STANDARD)
		{
			m_Info.OnPresetStandardClicked();
			return;
		}
		if (kind == RK29_EMenuRowKind.PRESET_ROW)
		{
			m_Info.OnPresetClicked(index);
			return;
		}
		if (kind == RK29_EMenuRowKind.PRESET_DELETE)
			m_Info.OnPresetDeleteClicked(index);
	}

	//------------------------------------------------------------------------------------------------
	//! ESC while something is open goes back one step instead of closing: the detail band returns to
	//! the mannequin, and the folds that belonged to the panel go with it. Only an ESC pressed from
	//! the mannequin state closes the dialog, which is what the second press does. Answers whether it
	//! actually retreated - true swallows the press, false lets vanilla close; called by
	//! RK29_LoadoutDialog. No pick moves and no offer is re-resolved.
	bool RetreatFromDetail()
	{
		if (m_Detail.OpenGroup() == "")
			return false;

		m_Detail.ClearFold();

		m_Tiles.BuildTiles();
		m_Detail.BuildDetail();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! AfterPickChanged from the call queue, for the two edit boxes whose commit event must not
	//! rebuild the column it lives in from inside itself. A menu torn down meanwhile rebuilds
	//! nothing.
	void AfterPickChangedDeferred()
	{
		if (m_bTornDown)
			return;
		AfterPickChanged();
	}

	//============================================================================================
	// Apply
	//============================================================================================
	//------------------------------------------------------------------------------------------------
	protected void OnConfirmDialog(SCR_ConfigurableDialogUi dlg)
	{
		Confirm();
	}

	//------------------------------------------------------------------------------------------------
	//! Sends the picks and changes nothing locally - the menu stays open for another edit and apply.
	protected void Confirm()
	{
		RK29_ClassSetup cls = CurrentClass();
		if (!cls)
			return;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;

		string wire = RK29_KitResolve.EncodePicks(m_aPicks);
		pc.RK29_RequestKit(cls.m_sKitName, wire);
		RK29_Log.Trace(string.Format("[RK29] loadout menu: applying '%1' picks='%2'",
			cls.m_sKitName, wire));
	}

}
