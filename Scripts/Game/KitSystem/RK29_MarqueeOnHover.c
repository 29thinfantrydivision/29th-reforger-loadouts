//------------------------------------------------------------------------------------------------
//! A name too long for its row scrolls sideways on hover only. The moving is still vanilla's
//! SCR_HorizontalScrollAnimationComponent; only the trigger changes. Its own m_bAnimateOnFocus is
//! not usable: our rows are bare ButtonWidgets with no SCR_ButtonBaseComponent, so nothing calls
//! SetFocusedWidget and the gate never opens. It must sit on a widget that actually receives the
//! cursor - a layout widget is not hit-tested - so RK29_CountRow's root clears "Ignore Cursor".
//! Authored in the .layout beside the widget it measures; the menu stamps no handlers for it.
[BaseContainerProps()]
class RK29_MarqueeOnHover : SCR_ScriptedWidgetComponent
{
	[Attribute("NameFrame", desc: "The clipping window the text scrolls inside - carries SCR_HorizontalScrollAnimationComponent", category: "29th")]
	protected string m_sFrameWidget;

	[Attribute("NameContent", desc: "The text that moves. Wider than the frame = there is something to scroll", category: "29th")]
	protected string m_sContentWidget;

	protected Widget m_wFrame;
	protected Widget m_wContent;
	protected SCR_HorizontalScrollAnimationComponent m_Anim;

	//------------------------------------------------------------------------------------------------
	//! Measured fresh on every hover: these rows are re-stamped with a different name on each pick,
	//! so a width cached at attach would answer about some earlier kit's text.
	protected bool Overflows()
	{
		if (!m_wFrame || !m_wContent)
			return false;

		float frameX, frameY, textX, textY;
		m_wFrame.GetScreenSize(frameX, frameY);
		m_wContent.GetScreenSize(textX, textY);
		return textX > frameX;
	}

	//------------------------------------------------------------------------------------------------
	protected void Rest()
	{
		if (!m_Anim)
			return;

		m_Anim.AnimationStop();
		m_Anim.ResetPosition();
	}

	//------------------------------------------------------------------------------------------------
	override bool OnMouseEnter(Widget w, int x, int y)
	{
		if (m_Anim && Overflows())
			m_Anim.AnimationStart();

		// never consumed: the row under this one carries the hover tip
		return false;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
	{
		// leaving the row for one of its own children - the clipped name itself - is not leaving the
		// row; the same walk the hover tip makes, to the same depth
		Widget probe = enterW;
		for (int depth = 0; probe && depth < RK29_HoverTip.HOVER_PARENT_WALK; depth++)
		{
			if (probe == w)
				return false;
			probe = probe.GetParent();
		}
		Rest();
		return false;
	}

	//------------------------------------------------------------------------------------------------
	override void HandlerAttached(Widget w)
	{
		super.HandlerAttached(w);

		m_wFrame = w.FindAnyWidget(m_sFrameWidget);
		m_wContent = w.FindAnyWidget(m_sContentWidget);
		if (m_wFrame)
			m_Anim = SCR_HorizontalScrollAnimationComponent.Cast(
				m_wFrame.FindHandler(SCR_HorizontalScrollAnimationComponent));

		Rest();
	}

	//------------------------------------------------------------------------------------------------
	override void HandlerDeattached(Widget w)
	{
		m_wFrame = null;
		m_wContent = null;
		m_Anim = null;
		super.HandlerDeattached(w);
	}
}
