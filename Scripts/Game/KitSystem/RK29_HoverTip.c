//------------------------------------------------------------------------------------------------
//! The loadout menu's instant hover tip: the panel, the words in it, the widget it answers for,
//! and the wrapping. The per-column handler stores stay on the panel that stamped the row - which
//! store a handler goes in is what decides which column rebuild frees it, so AttachHoverTip is
//! handed one.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
class RK29_HoverTip
{
	//! Moving from a tip's owner onto one of its own children - a stepper's "+" label, a row's icon
	//! - is not leaving the owner; the walk tells the two apart without promising a layout depth.
	//! Public because RK29_MarqueeOnHover walks the same rows and the two depths must not drift.
	static const int HOVER_PARENT_WALK = 10;

	//! In from the owner's left, down from its bottom, and off the layer's right edge.
	protected static const float TIP_OFFSET_X = 8.0;
	protected static const float TIP_OFFSET_Y = 2.0;
	protected static const float TIP_RIGHT_MARGIN = 8.0;

	//! The tip's width budget in characters, not pixels - what WrapTipText breaks its lines against.
	//! The panel sizes to its content on both axes, so nothing clips; keepLines bypasses the budget.
	protected static const int TIP_CHARS_PER_LINE = 33;

	//! The dialog root the widgets below are found under; null outside the menu's open window,
	//! which is what makes every call here safe.
	protected Widget m_wRoot;

	//! Found from the root on first use. Never sized by script: the layout
	//! sizes the panel to its content on both axes - see ShowHoverTip for why
	//! nothing here may go back to computing a width.
	protected Widget m_wHoverTip;
	protected TextWidget m_wTipText;
	//! found once like the panel - TipLayer is authored last, so a lookup walks every stamped row
	protected Widget m_wTipLayer;
	protected Widget m_wTipSupplyBox;
	protected Widget m_wTipSupplyGlyph;
	protected Widget m_wTipOwner;

	//! What the shown tip reads, so a re-show of the same words on the same owner can do nothing at
	//! all: a child's bubbled enter arrives constantly, and redoing the work is the flicker.
	protected string m_sTipText;

	//------------------------------------------------------------------------------------------------
	void Bind(Widget root)
	{
		m_wRoot = root;
	}

	//------------------------------------------------------------------------------------------------
	//! Dropped before the root it was found through: a stale owner would outlive the row it names.
	void Teardown()
	{
		HideHoverTip();

		m_wHoverTip = null;
		m_wTipText = null;
		m_wTipLayer = null;
		m_wTipSupplyBox = null;
		m_wTipSupplyGlyph = null;
		m_wRoot = null;
	}

	//------------------------------------------------------------------------------------------------
	//! Puts the tip panel under the widget the cursor arrived on, positioned in TipLayer's
	//! own space (DPIUnscaled, because FrameSlot works in layout units). Nothing here sizes
	//! the panel: it is SizeToContent and the words arrive pre-broken - the old estimated
	//! width is what cut the number off "Max: 12". A re-show of the same words on the same
	//! owner does nothing, or the panel flickers. Accepted: the tip does not follow a
	//! scrolling column, and a column rebuild hides it.
	void ShowHoverTip(Widget owner, string text, bool supplyIcon, bool keepLines = false)
	{
		if (!owner || text == "" || !m_wRoot)
			return;

		if (m_wTipOwner == owner && m_sTipText == text)
			return;

		if (!m_wHoverTip)
			m_wHoverTip = m_wRoot.FindAnyWidget("HoverTip");
		if (!m_wTipText)
			m_wTipText = TextWidget.Cast(m_wRoot.FindAnyWidget("TipText"));

		if (!m_wTipLayer)
			m_wTipLayer = m_wRoot.FindAnyWidget("TipLayer");
		if (!m_wHoverTip || !m_wTipText || !m_wTipLayer)
			return;

		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		if (keepLines)
			m_wTipText.SetText(text);
		else
			m_wTipText.SetText(WrapTipText(text));

		// authored hidden, so a tip that carries no glyph is exactly as wide as its words
		if (!m_wTipSupplyBox)
			m_wTipSupplyBox = m_wRoot.FindAnyWidget("TipSupplySize");
		if (m_wTipSupplyBox)
			m_wTipSupplyBox.SetVisible(supplyIcon);

		if (!m_wTipSupplyGlyph)
			m_wTipSupplyGlyph = m_wRoot.FindAnyWidget("TipSupplyIcon");
		if (m_wTipSupplyGlyph)
			m_wTipSupplyGlyph.SetVisible(supplyIcon);

		float ownerX, ownerY, ownerW, ownerH, layerX, layerY;
		owner.GetScreenPos(ownerX, ownerY);
		owner.GetScreenSize(ownerW, ownerH);
		m_wTipLayer.GetScreenPos(layerX, layerY);

		float x = ws.DPIUnscale(ownerX - layerX) + TIP_OFFSET_X;
		float y = ws.DPIUnscale(ownerY + ownerH - layerY) + TIP_OFFSET_Y;

		// shown before it is placed, because placing it needs a width the panel only has once laid out;
		// both happen this frame, ahead of the draw
		m_wHoverTip.SetVisible(true);

		// a tip hangs off its owner's left edge and grows rightward, which only shows
		// when the owner sits against the right edge of the screen - the trash glyph's
		// tip read "Dele". Measured, never estimated: a width of zero means the engine
		// has not laid it out, so the clamp is skipped.
		float tipW, tipH, layerW, layerH;
		m_wHoverTip.GetScreenSize(tipW, tipH);
		m_wTipLayer.GetScreenSize(layerW, layerH);
		if (tipW > 0 && layerW > 0)
		{
			float rightLimit = ws.DPIUnscale(layerW) - ws.DPIUnscale(tipW) - TIP_RIGHT_MARGIN;
			if (x > rightLimit)
				x = rightLimit;
			if (x < 0)
				x = 0;

			// a tip that would cross the bottom of the screen opens above its row instead
			if (y + ws.DPIUnscale(tipH) > ws.DPIUnscale(layerH))
				y = ws.DPIUnscale(ownerY - layerY) - ws.DPIUnscale(tipH) - TIP_OFFSET_Y;
		}

		FrameSlot.SetPos(m_wHoverTip, x, y);
		m_wTipOwner = owner;
		m_sTipText = text;
	}

	//------------------------------------------------------------------------------------------------
	//! Breaks the words into lines at TIP_CHARS_PER_LINE and hands back one string with real "\n"
	//! breaks, which RichText renders. Capping the widget's width does not wrap - TipText runs
	//! straight off the side of the panel. The budget is in characters because there is no honest
	//! text measurement: vanilla's only two GetTextSize callers (SCR_EditableCommentComponent:34,
	//! SCR_GearShiftInfo.Scale:133) both discard the width. Greedy; an over-long word is left uncut.
	//! Text that carries its own "\n" breaks goes through keepLines instead - this counts across them.
	protected string WrapTipText(string text)
	{
		array<string> words = {};
		text.Split(" ", words, true);

		array<string> built = {};
		string line;

		foreach (string word : words)
		{
			if (line == "")
			{
				line = word;
				continue;
			}

			if (line.Length() + 1 + word.Length() <= TIP_CHARS_PER_LINE)
			{
				line = line + " " + word;
				continue;
			}

			built.Insert(line);
			line = word;
		}

		if (line != "")
			built.Insert(line);

		// a string of nothing but separators leaves no words at all - it is handed back whole
		if (built.IsEmpty())
			return text;

		string wrapped;
		foreach (int i, string one : built)
		{
			if (i > 0)
				wrapped = wrapped + "\n";

			wrapped = wrapped + one;
		}

		return wrapped;
	}

	//------------------------------------------------------------------------------------------------
	//! Moving onto one of the widget's own children is not leaving it, so the entered
	//! widget is walked up first. The owner check is what stops a stale leave closing a tip
	//! a later row has since opened - enters and leaves arrive in no guaranteed order. A
	//! leave with no entered widget at all (cursor left the window, or crossed onto
	//! TipLayer, which ignores it) falls through and hides.
	void OnHoverLeft(Widget w, Widget enterW)
	{
		Widget entered = enterW;
		for (int depth = 0; entered && depth < HOVER_PARENT_WALK; depth++)
		{
			if (entered == w)
				return;

			entered = entered.GetParent();
		}

		if (m_wTipOwner == w)
			HideHoverTip();
	}

	//------------------------------------------------------------------------------------------------
	void HideHoverTip()
	{
		if (m_wHoverTip)
			m_wHoverTip.SetVisible(false);

		m_wTipOwner = null;
		m_sTipText = "";
	}

	//------------------------------------------------------------------------------------------------
	//! The handler is kept in the caller's store because a widget does not own
	//! the handlers added to it - the store keeps it alive, and which store it is
	//! in decides which column rebuild frees it.
	//! `keepLines` shows the text exactly as authored, no character budget - for a list that has
	//! already put one item per line and must not have a name cut in two.
	void AttachHoverTip(notnull Widget target, string text,
		notnull array<ref RK29_HoverTipHandler> store, bool supplyIcon = false, bool keepLines = false)
	{
		if (text == "")
			return;

		RK29_HoverTipHandler handler = new RK29_HoverTipHandler();
		handler.m_Tip = this;
		handler.m_sText = text;
		handler.m_bSupplyIcon = supplyIcon;
		handler.m_bKeepLines = keepLines;
		store.Insert(handler);

		target.AddHandler(handler);
	}
}
