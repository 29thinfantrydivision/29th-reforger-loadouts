//------------------------------------------------------------------------------------------------
//! A name too long for its row is first shrunk a step or two, and only scrolls sideways on hover for
//! whatever still hangs off the end. The moving is still vanilla's scroller; only the trigger and
//! the speed change - see RK29_ScrollAnimation. Its own m_bAnimateOnFocus is not usable: our rows
//! are bare ButtonWidgets with no SCR_ButtonBaseComponent, so nothing calls SetFocusedWidget and the
//! gate never opens. It must sit on a widget that actually receives the cursor - a layout widget is
//! not hit-tested - so RK29_CountRow's root clears "Ignore Cursor". Authored in the .layout beside
//! the widget it measures; the menu stamps no handlers for it.
[BaseContainerProps()]
class RK29_MarqueeOnHover : SCR_ScriptedWidgetComponent
{
	//! The name comes down two whole points at a time and never to a size fitted to this one row: a
	//! per-row fit puts visibly different type on two rows of near-equal length, which reads as a
	//! rendering fault. Few enough steps that a step reads as a deliberate size.
	protected const int FONT_STEP = 2;

	//! Every row scrolls at the same READING rate rather than the same pixel rate, and a long name
	//! therefore takes longer than a short one. Crossing whatever hangs off the row in a fixed time
	//! instead is what streams the longest names - the ones actually worth reading - past too fast to
	//! read, and a narrow clip holding a long name is exactly where that bites hardest. Point size is
	//! part of it because the fit above may have put this row in 14 where its neighbour is 18: the
	//! same px/sec is a third more characters a second at the smaller size. RobotoCondensed advances
	//! roughly CHAR_ADVANCE of its point size per character, so the two together give the px/sec the
	//! animation moves at - an authored size, DPI-scaled to the screen where it is spent.
	protected const float READ_CHARS_PER_SEC = 9;
	protected const float CHAR_ADVANCE = 0.45;

	//! A row is stamped and laid out over the frames after it is created, so the first measurements
	//! can still answer zero. Retry that many times rather than fit a name against a zero width.
	protected const int FIT_TRIES = 5;

	[Attribute("NameFrame", desc: "The clipping window the text scrolls inside - carries RK29_ScrollAnimation", category: "29th")]
	protected string m_sFrameWidget;

	[Attribute("NameContent", desc: "The text that moves. Wider than the frame = there is something to scroll", category: "29th")]
	protected string m_sContentWidget;

	[Attribute("RowName", desc: "The one text inside the content that may shrink a step so more of it fits before anything has to scroll. Leave EMPTY where the content is not a single text (a chip strip): that content only gets the speed", category: "29th")]
	protected string m_sTextWidget;

	[Attribute("18", desc: "The Font Size the text widget is authored at - the ceiling the fit starts from, and the size every measurement is scaled against. Must match the layout or the fit reads the wrong width", category: "29th")]
	protected int m_iBaseFontSize;

	[Attribute("14", desc: "Smallest step the fit will take. Past this a name scrolls rather than shrinks - the row is a row, not a paragraph", category: "29th")]
	protected int m_iMinFontSize;

	protected Widget m_wFrame;
	protected Widget m_wContent;
	protected TextWidget m_wText;
	protected RK29_ScrollAnimation m_Anim;
	protected int m_iFontSize;
	protected int m_iFitTries;

	//------------------------------------------------------------------------------------------------
	//! Both widths in screen pixels; false where either has not been laid out yet.
	protected bool Measure(out float frameX, out float contentX)
	{
		if (!m_wFrame || !m_wContent)
			return false;

		float frameY, contentY;
		m_wFrame.GetScreenSize(frameX, frameY);
		m_wContent.GetScreenSize(contentX, contentY);
		return frameX > 0 && contentX > 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Steps the name down until it fits or the ladder bottoms out. One measurement decides the step:
	//! text width tracks point size closely enough to divide, and re-measuring after each
	//! SetExactFontSize would cost a frame per step and show the reader every one of them.
	//! Public because a row re-worded in place has to ask for this again.
	void Fit()
	{
		if (!m_wText || m_iBaseFontSize <= 0)
			return;

		float frameX, contentX;
		if (!Measure(frameX, contentX))
		{
			m_iFitTries++;
			if (m_iFitTries < FIT_TRIES && GetGame() && GetGame().GetCallqueue())
				GetGame().GetCallqueue().CallLater(Fit, 0);

			return;
		}

		// spent on getting THIS text measured; a later re-word gets its own retries
		m_iFitTries = 0;

		float atBase = contentX * m_iBaseFontSize / m_iFontSize;

		int size = m_iBaseFontSize;
		while (size - FONT_STEP >= m_iMinFontSize && atBase * size / m_iBaseFontSize > frameX)
			size -= FONT_STEP;

		if (size == m_iFontSize)
			return;

		m_iFontSize = size;
		m_wText.SetExactFontSize(size);
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
	//! Measured fresh on every hover: these rows are re-stamped with a different name on each pick, so
	//! a width cached at attach would answer about some earlier kit's text - and the fit has moved it
	//! since in any case.
	override bool OnMouseEnter(Widget w, int x, int y)
	{
		float frameX, contentX;
		if (m_Anim && Measure(frameX, contentX) && contentX > frameX)
		{
			float pxPerSec = READ_CHARS_PER_SEC * CHAR_ADVANCE * m_iFontSize;
			m_Anim.SetSpeedPxPerSec(GetGame().GetWorkspace().DPIScale(pxPerSec));
			m_Anim.AnimationStart();
		}

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
			m_Anim = RK29_ScrollAnimation.Cast(m_wFrame.FindHandler(RK29_ScrollAnimation));

		m_iFontSize = m_iBaseFontSize;
		if (m_sTextWidget != "" && m_wContent)
			m_wText = TextWidget.Cast(m_wContent.FindAnyWidget(m_sTextWidget));

		Rest();

		// the row is stamped with its name after this returns, so the fit cannot run here: it is queued
		// for the frame that text has been laid out in, and retries from there
		if (m_wText && GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(Fit, 0);
	}

	//------------------------------------------------------------------------------------------------
	override void HandlerDeattached(Widget w)
	{
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(Fit);

		m_wFrame = null;
		m_wContent = null;
		m_wText = null;
		m_Anim = null;
		super.HandlerDeattached(w);
	}
}
