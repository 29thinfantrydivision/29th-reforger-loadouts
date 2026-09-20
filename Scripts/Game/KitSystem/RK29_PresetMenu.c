//------------------------------------------------------------------------------------------------
//! The context menu a saved kit row opens: overwrite that preset with the picks on screen, or
//! delete it. Reached two ways, both landing here - the row's glyph, and a right-click anywhere on
//! the row. A right-click must never load a preset, which is what it did before this existed:
//! SCR_ModularButtonComponent.OnClick ignores which button was pressed.
//!
//! Lives on the menu rather than on the info band because its two buttons are authored in
//! RK29_LoadoutMenu.layout, once per opening, while the band empties its handler store on every
//! rebuild. The band still owns what the items DO - see RK29_MenuInfoBand.OnPresetMenuOverwrite.
//------------------------------------------------------------------------------------------------
class RK29_PresetMenu
{
	protected static const string MENU_LAYER = "PresetMenuLayer";

	//! The menu layout's own root overlay, which the layer is a direct child of and fills exactly
	//! (both axes aligned 3). NOT RK29_LoadoutMenu.Root(), which is the vanilla DIALOG's root and a
	//! different, larger rectangle - measuring that offset every panel by the menu's inset.
	protected static const string MENU_FRAME = "RK29_LoadoutMenu";
	protected static const string MENU_PANEL = "PresetMenu";
	protected static const string MENU_OVERWRITE = "PresetMenuOverwrite";
	protected static const string MENU_DELETE = "PresetMenuDelete";

	//! What the panel measures, authored on PresetMenu in RK29_LoadoutMenu.layout. Stated rather
	//! than measured on purpose: GetScreenSize answers zero until the engine has laid the panel out,
	//! and the panel is placed in the very frame it is first shown in - the hover tip skips its
	//! clamp in that case and hangs slightly wrong, which a menu opening off the edge of the screen
	//! may not. Both must be kept in step with the two overrides the layout authors.
	protected static const float MENU_WIDTH = 160.0;
	protected static const float MENU_HEIGHT = 68.0;

	//! Below the row that opened it, and this far clear of the layer's edges when it would not fit.
	protected static const float MENU_OFFSET_Y = 2.0;
	protected static const float MENU_MARGIN = 8.0;

	//! How long a back press this menu already spent stays spent. One press, not a duration: the
	//! dialog's own OnCancel arrives in the same frame or not at all, and a stamp rather than a
	//! flag means a press that never reaches it expires instead of swallowing the NEXT one.
	protected static const float BACK_GRACE_MS = 100.0;

	//! Not a ref: the menu owns this panel, and a reference back would be an unfreeable cycle.
	protected RK29_LoadoutMenu m_Menu;

	//! The full-screen frame the panel is positioned inside, and the panel itself; both null outside
	//! the menu's open window, which is what makes every call here safe.
	protected Widget m_wLayer;
	protected Widget m_wPanel;

	//! What the placement is measured against - see MENU_FRAME. The layer would be the natural
	//! choice, being the parent FrameSlot positions inside, but the layout authors it hidden and a
	//! widget hidden since load has never been laid out: on the FIRST open of a session its screen
	//! rect answers zeros, the clamp is skipped for a zero size, and the panel lands outside the
	//! dialog. The overlay holds the same rectangle and is laid out from the moment it appears, so
	//! reading it is right whether or not the layer has been shown yet.
	protected Widget m_wFrame;

	//! The preset the open menu acts on, captured when it opened. A name, not the row index a
	//! handler carries: the index is only good until the band is restamped, the name outlives it.
	protected string m_sPreset;

	protected bool m_bOpen;

	//! World time (ms) at which a MenuBack press closed this menu - see TookBack.
	protected float m_fBackClosedMs;

	//! The item handlers, kept here because the info band's store is emptied on every rebuild while
	//! these two buttons are stamped once, by the layout.
	protected ref array<ref RK29_LoadoutRowHandler> m_aHandlers = {};

	//! Closes the menu when a click lands outside the panel; only armed while the panel is modal.
	protected ref RK29_PresetMenuModalHandler m_Modal;

	//------------------------------------------------------------------------------------------------
	//! Wired once per opening of the dialog, against a fresh widget tree. A layout missing the panel
	//! leaves every call below a no-op rather than warning once per row.
	void Init(notnull RK29_LoadoutMenu menu, Widget root)
	{
		m_Menu = menu;
		if (!root)
			return;

		m_wLayer = root.FindAnyWidget(MENU_LAYER);
		m_wFrame = root.FindAnyWidget(MENU_FRAME);
		if (!m_wFrame)
			m_wFrame = m_wLayer;
		m_wPanel = root.FindAnyWidget(MENU_PANEL);
		if (!m_wLayer || !m_wPanel)
		{
			Print("[RK29] loadout menu: the saved-kit context menu is missing from the layout",
				LogLevel.WARNING);
			return;
		}

		m_Modal = new RK29_PresetMenuModalHandler();
		m_Modal.m_Menu = this;
		m_wPanel.AddHandler(m_Modal);

		menu.AttachHandler(m_wPanel, MENU_OVERWRITE, RK29_EMenuRowKind.PRESET_MENU_OVERWRITE, 0,
			m_aHandlers);
		menu.AttachHandler(m_wPanel, MENU_DELETE, RK29_EMenuRowKind.PRESET_MENU_DELETE, 0,
			m_aHandlers);
	}

	//------------------------------------------------------------------------------------------------
	//! Dropped before the root the widgets were found through; the modal is given back first, or the
	//! workspace holds a widget the closing dialog is about to free.
	void Teardown()
	{
		Close();

		m_aHandlers.Clear();
		m_Modal = null;
		m_wPanel = null;
		m_wLayer = null;
		m_wFrame = null;
		m_Menu = null;
	}

	//------------------------------------------------------------------------------------------------
	//! Opens under the anchor, right-aligned with it, holding the named preset. Re-opening on
	//! another row closes first: AddModal on an already-modal widget is refused with a log line.
	//! The panel is made modal so a click anywhere else closes it instead of reaching the row
	//! underneath - vanilla's combo box opens its list exactly this way.
	void Open(Widget anchor, string preset)
	{
		if (!m_wPanel || !m_wLayer || !anchor || preset == "")
			return;

		Close();

		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		// the tip belongs to the row the menu is about to cover
		if (m_Menu)
			m_Menu.HoverTip().HideHoverTip();

		m_sPreset = preset;
		m_bOpen = true;

		m_wLayer.SetVisible(true);
		m_wPanel.SetVisible(true);
		PlaceUnder(ws, anchor);

		ws.AddModal(m_wPanel, m_wPanel.FindAnyWidget(MENU_OVERWRITE));

		// The modal takes the input the dialog would otherwise answer ESC with, so the press has to
		// be listened for directly - vanilla's combo box arms the same two actions for the same
		// reason when its list goes modal (SCR_ComboBoxComponent.c:346-350). MenuBackWB is the
		// Workbench binding of the same press and is harmless where it is not bound.
		InputManager input = GetGame().GetInputManager();
		if (input)
		{
			input.AddActionListener(UIConstants.MENU_ACTION_BACK, EActionTrigger.DOWN, OnMenuBack);
			input.AddActionListener(UIConstants.MENU_ACTION_BACK_WB, EActionTrigger.DOWN, OnMenuBack);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Answers whether it actually closed something - true is what lets ESC take the press instead
	//! of the dialog closing. Safe on a menu that was never opened, and called that way: the info
	//! band closes on every rebuild, because a rebuild frees the row this menu was anchored to.
	bool Close()
	{
		if (!m_bOpen)
			return false;

		m_bOpen = false;
		m_sPreset = "";

		// taken back on every close, whatever closed it: a listener outliving the open menu would
		// answer ESC presses meant for the dialog
		InputManager input = GetGame().GetInputManager();
		if (input)
		{
			input.RemoveActionListener(UIConstants.MENU_ACTION_BACK, EActionTrigger.DOWN, OnMenuBack);
			input.RemoveActionListener(UIConstants.MENU_ACTION_BACK_WB, EActionTrigger.DOWN,
				OnMenuBack);
		}

		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (ws && m_wPanel)
			ws.RemoveModal(m_wPanel);

		if (m_wPanel)
			m_wPanel.SetVisible(false);
		if (m_wLayer)
			m_wLayer.SetVisible(false);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! ESC with the menu open shuts the menu and nothing else. Only the menu: the dialog's own back
	//! handling still runs for the press, so the close is stamped and RK29_LoadoutMenu.RetreatFromDetail
	//! asks TookBack before it goes on to fold the detail panel or let the dialog close.
	protected void OnMenuBack()
	{
		if (!Close())
			return;

		m_fBackClosedMs = GetGame().GetWorld().GetWorldTime();
	}

	//------------------------------------------------------------------------------------------------
	//! Whether the back press now reaching the dialog is the one this menu has already spent, and
	//! spends it: without this, one ESC would shut the menu AND take the step behind it.
	bool TookBack()
	{
		if (m_fBackClosedMs <= 0)
			return false;

		bool fresh = GetGame().GetWorld().GetWorldTime() - m_fBackClosedMs < BACK_GRACE_MS;
		m_fBackClosedMs = 0;
		return fresh;
	}

	//------------------------------------------------------------------------------------------------
	//! The preset the open menu stands for, "" when nothing is open. Both items read this before
	//! they Close, which clears it.
	string Target()
	{
		return m_sPreset;
	}

	//------------------------------------------------------------------------------------------------
	//! Positioned in the layer's own space (DPIUnscaled, because FrameSlot works in layout units),
	//! the same move the hover tip makes. Right-aligned with the anchor and hanging below it, so the
	//! glyph and a right-click on the row put the panel in the same place; it opens above the anchor
	//! instead when the bottom of the screen is in the way.
	protected void PlaceUnder(notnull WorkspaceWidget ws, notnull Widget anchor)
	{
		float anchorX, anchorY, anchorW, anchorH, layerX, layerY, layerW, layerH;
		anchor.GetScreenPos(anchorX, anchorY);
		anchor.GetScreenSize(anchorW, anchorH);
		m_wFrame.GetScreenPos(layerX, layerY);
		m_wFrame.GetScreenSize(layerW, layerH);

		float x = ws.DPIUnscale(anchorX + anchorW - layerX) - MENU_WIDTH;
		float y = ws.DPIUnscale(anchorY + anchorH - layerY) + MENU_OFFSET_Y;

		// belt and braces: a frame that answered zero would make the clamp pin the panel to the top
		// left rather than leave it where it was put. MENU_FRAME is laid out long before any of this
		// runs, which is the whole reason it is measured instead of the layer
		if (layerW > 0 && layerH > 0)
		{
			float rightLimit = ws.DPIUnscale(layerW) - MENU_WIDTH - MENU_MARGIN;
			if (x > rightLimit)
				x = rightLimit;

			if (y + MENU_HEIGHT > ws.DPIUnscale(layerH) - MENU_MARGIN)
				y = ws.DPIUnscale(anchorY - layerY) - MENU_HEIGHT - MENU_OFFSET_Y;
		}

		if (x < MENU_MARGIN)
			x = MENU_MARGIN;
		if (y < MENU_MARGIN)
			y = MENU_MARGIN;

		FrameSlot.SetPos(m_wPanel, x, y);
	}
}

//------------------------------------------------------------------------------------------------
//! A click outside the open menu closes it. OnModalClickOut only reaches a widget the workspace
//! holds as modal, which is what RK29_PresetMenu.Open makes the panel; answering true swallows the
//! click, so the row underneath is not pressed as well - vanilla's SCR_ComboModalHandler answers
//! the same way.
//------------------------------------------------------------------------------------------------
class RK29_PresetMenuModalHandler : ScriptedWidgetEventHandler
{
	RK29_PresetMenu m_Menu;

	//------------------------------------------------------------------------------------------------
	override bool OnModalClickOut(Widget modalRoot, int x, int y, int button)
	{
		if (m_Menu)
			m_Menu.Close();

		return true;
	}
}
