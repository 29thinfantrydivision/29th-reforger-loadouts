//------------------------------------------------------------------------------------------------
//! Widget moves the kit UI makes over and over, in one place - clear a parent's children, reveal
//! a glyph with the wrapper holding its column, write a named text child, require a widget. No
//! kits.
//------------------------------------------------------------------------------------------------
class RK29_WidgetUtil
{
	//------------------------------------------------------------------------------------------------
	//! RemoveFromHierarchy rather than parent.RemoveChild (works whatever the parent is), and the
	//! sibling is taken before the removal because a removed widget no longer answers for the next.
	static void ClearChildren(Widget parent)
	{
		if (!parent)
			return;

		Widget child = parent.GetChildren();
		while (child)
		{
			Widget next = child.GetSibling();
			child.RemoveFromHierarchy();
			child = next;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Toggles the glyph and the SizeLayout wrapper reserving its width: touching only one leaves the
	//! row a column short, or an empty column standing.
	static void RevealPair(Widget row, string boxName, string iconName, bool show)
	{
		if (!row)
			return;

		Widget box = row.FindAnyWidget(boxName);
		if (box)
			box.SetVisible(show);

		Widget icon = row.FindAnyWidget(iconName);
		if (icon)
			icon.SetVisible(show);
	}

	//------------------------------------------------------------------------------------------------
	//! A row with no such child is left alone: it is a label, so the row reads short, not broken.
	static void SetText(Widget row, string widgetName, string text)
	{
		if (!row)
			return;

		TextWidget label = TextWidget.Cast(row.FindAnyWidget(widgetName));
		if (label)
			label.SetText(text);
	}

	//------------------------------------------------------------------------------------------------
	//! Warns and names the missing widget - for the few a screen cannot work without, where a silent
	//! null means a blank panel and no way to tell which layout lost which name.
	static Widget Require(Widget parent, string name)
	{
		if (!parent)
			return null;

		Widget found = parent.FindAnyWidget(name);
		if (!found)
			Print(string.Format("[RK29] layout is missing the '%1' widget", name), LogLevel.WARNING);

		return found;
	}
}
