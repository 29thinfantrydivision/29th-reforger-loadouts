//------------------------------------------------------------------------------------------------
//! Hover text on one widget, drawn by the menu's own tip panel: vanilla's
//! SCR_ScriptedWidgetTooltip opens on an engine-side delay no attribute reaches, and these rows
//! want their text instantly. Both handlers answer false - the widget underneath still needs its
//! click, stepper press and marquee. Report against m_wTarget, never the event's widget: a
//! child's bubbled enter would re-key the tip onto the child's box, which is the panel jumping
//! about under the cursor.
class RK29_HoverTipHandler : ScriptedWidgetEventHandler
{
	RK29_HoverTip m_Tip;
	string m_sText;

	//! The words are a quantity of supplies and want the glyph after them; only the count cell asks.
	bool m_bSupplyIcon;

	//! The words are already broken into the lines they want; the tip must not re-wrap them.
	bool m_bKeepLines;

	//! The widget AttachHoverTip put this handler on - not whatever child raised the event.
	protected Widget m_wTarget;

	//------------------------------------------------------------------------------------------------
	override void HandlerAttached(Widget w)
	{
		m_wTarget = w;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnMouseEnter(Widget w, int x, int y)
	{
		if (m_Tip && m_wTarget)
			m_Tip.ShowHoverTip(m_wTarget, m_sText, m_bSupplyIcon, m_bKeepLines);
		return false;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
	{
		if (m_Tip && m_wTarget)
			m_Tip.OnHoverLeft(m_wTarget, enterW);
		return false;
	}
}
