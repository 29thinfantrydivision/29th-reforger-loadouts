//------------------------------------------------------------------------------------------------
//! Reordering the saved kits by dragging a row up or down. Enfusion has no drag events at all -
//! ScriptedWidgetEventHandler ends at OnMouseButtonDown/Up - so a drag is press, a pump, and
//! release, built the same way RK29_MannequinView builds the turn-the-soldier drag: the press
//! arms it, a repeating call reads WidgetManager.GetMousePos each tick, and the release commits.
//!
//! The handle is the row itself, so every press on a saved kit carries two possible meanings:
//! press-and-release loads it, press-and-hold moves it. Nothing begins until the cursor has
//! travelled DRAG_THRESHOLD.
//!
//! What decides between them at the release is not whether a drag BEGAN but whether it MOVED the
//! row: only a drop that reordered the list eats the click, so a press that drifted past the
//! threshold and came back still counts as the click it looks like. Eating it on any begun drag
//! made presses nobody meant as a drag do nothing. See TookClick and Drop's answer, and the
//! PRESET_ROW branch of RK29_LoadoutMenu.OnRowClicked that asks.
//!
//! The threshold guards the other direction - "I tried to load it and it moved" - and is in PHYSICAL
//! pixels, which a high-DPI screen spends faster than the number suggests.
//------------------------------------------------------------------------------------------------
class RK29_PresetDrag
{
	protected static const string DRAG_LAYER = "PresetDragLayer";

	//! The menu layout's root overlay - the layer's parent, same rectangle, and always laid out.
	//! See RK29_PresetMenu.MENU_FRAME for why the layer itself cannot be measured and why
	//! RK29_LoadoutMenu.Root() is the wrong widget.
	protected static const string DRAG_FRAME = "RK29_LoadoutMenu";
	protected static const string DRAG_GHOST = "PresetDragGhost";
	protected static const string DRAG_NAME = "PresetDragName";
	protected static const string DROP_LINE = "PresetDropLine";

	//! Physical pixels of travel before a press becomes a drag. Mouse coordinates and GetScreenPos
	//! share that space, so nothing here is DPI-unscaled - see RK29_MannequinView.CursorOverMannequin.
	//! Physical, so a high-DPI screen spends them faster than it looks: at 6 an ordinary click
	//! tripped it, which flashed the ghost on presses nobody meant as a drag.
	protected static const float DRAG_THRESHOLD = 14.0;

	//! One tick. The ghost is under the cursor, so anything slower reads as lag on the hand.
	protected static const int DRAG_TICK_MS = 16;

	//! How far the row left behind is faded while its copy is under the cursor - the same "not right
	//! now" statement the steppers make, so the list says which row is in the air.
	protected static const float DRAG_SOURCE_DIM = 0.35;

	//! Not a ref: the menu owns this, and a reference back would be an unfreeable cycle.
	protected RK29_LoadoutMenu m_Menu;

	protected Widget m_wLayer;
	protected Widget m_wFrame;
	protected Widget m_wGhost;
	protected Widget m_wLine;
	protected TextWidget m_wGhostName;

	//! The row being dragged: its name, and where it sits in its class's list. The name is what the
	//! store is told; the position is what the drop is measured against.
	protected string m_sPreset;
	protected int m_iFrom;

	//! The row left behind, faded for as long as the drag lasts and put back whatever ends it.
	protected Widget m_wSource;

	//! Where the cursor was when the button went down, and where the line says the row would land -
	//! a position BETWEEN rows, so it runs 0 (above the first) to count (below the last).
	protected float m_fPressX;
	protected float m_fPressY;
	protected int m_iInsert;

	//! The ghost's height in screen pixels, taken off the row it was copied from. Held rather
	//! than asked of the ghost every tick: a widget shown this frame answers zero for its size
	//! until the engine lays it out, and a zero here puts the first frame half a row out.
	protected float m_fGhostH;

	//! Pressed but still inside the threshold. Dragging is past it. Neither is the other, and the
	//! pump runs for both: the armed state is what the pump is measuring.
	protected bool m_bArmed;
	protected bool m_bDragging;
	protected bool m_bPumpRunning;

	//! A drag finished, so the release that ended it also raises a click on the glyph, which would
	//! open the menu over the row just moved. Taken by TookClick, once.
	protected bool m_bTookClick;

	//------------------------------------------------------------------------------------------------
	void Init(notnull RK29_LoadoutMenu menu, Widget root)
	{
		m_Menu = menu;
		if (!root)
			return;

		m_wLayer = root.FindAnyWidget(DRAG_LAYER);
		m_wFrame = root.FindAnyWidget(DRAG_FRAME);
		if (!m_wFrame)
			m_wFrame = m_wLayer;
		m_wGhost = root.FindAnyWidget(DRAG_GHOST);
		m_wLine = root.FindAnyWidget(DROP_LINE);
		m_wGhostName = TextWidget.Cast(root.FindAnyWidget(DRAG_NAME));

		if (!m_wLayer || !m_wGhost || !m_wLine)
		{
			Print("[RK29] loadout menu: the saved-kit drag layer is missing from the layout",
				LogLevel.WARNING);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! A repeating call outliving the object it is queued against is the one way this could crash -
	//! RK29_MannequinView says the same about its own pump.
	void Teardown()
	{
		Cancel();
		StopPump();

		m_wGhostName = null;
		m_wLine = null;
		m_wGhost = null;
		m_wLayer = null;
		m_wFrame = null;
		m_Menu = null;
	}

	//------------------------------------------------------------------------------------------------
	//! The glyph went down. Nothing is shown yet and nothing is decided: this only banks where the
	//! press was, and the pump decides from here whether it was a click or a pull.
	void Press(int index, string preset)
	{
		if (!m_wLayer || preset == "")
			return;

		Cancel();

		// a drag released off the glyph raises no click on it at all, so the flag set by the last
		// one can still be standing; a new press is the point at which it certainly means nothing
		m_bTookClick = false;

		int cursorX, cursorY;
		WidgetManager.GetMousePos(cursorX, cursorY);

		m_sPreset = preset;
		m_iFrom = index;
		m_iInsert = index;
		m_fPressX = cursorX;
		m_fPressY = cursorY;
		m_bArmed = true;

		StartPump();
	}

	//------------------------------------------------------------------------------------------------
	//! The glyph came back up - and it is raised on the widget the press began on as well as the one
	//! under the cursor, so a release out over another row still lands here. A drag that never
	//! passed the threshold is just a click, and is left entirely to the click.
	//!
	//! Order is load-bearing: the drag is taken down BEFORE the drop is committed. Committing
	//! restamps the band, which frees every preset row - m_wSource among them - and the fade put on
	//! that row has to come off while the row still exists.
	void Release()
	{
		bool dropped = m_bDragging;
		string preset = m_sPreset;
		int from = m_iFrom;
		int insert = m_iInsert;

		Cancel();
		StopPump();

		if (!dropped)
			return;

		// only a drag that actually MOVED the row eats the click that ended it. Beginning one is
		// not enough: a press whose cursor wandered past the threshold and came back is a click by
		// every measure the player has, and swallowing those is what made the glyph fail to open
		// its menu every few presses.
		m_bTookClick = Drop(preset, from, insert);
	}

	//------------------------------------------------------------------------------------------------
	//! Whether the click now arriving is the tail of a drag rather than a press on the glyph, and
	//! forgets it either way: an un-taken flag would swallow the NEXT click on the glyph, which is
	//! the one that should open the menu.
	bool TookClick()
	{
		bool took = m_bTookClick;
		m_bTookClick = false;
		return took;
	}

	//------------------------------------------------------------------------------------------------
	//! Everything the drag put on screen, put back. Called by the release, by the teardown and by
	//! any new press: the faded source row especially must not survive whatever ended the drag.
	void Cancel()
	{
		if (m_wSource)
			m_wSource.SetOpacity(1.0);

		m_wSource = null;
		m_sPreset = "";
		m_bArmed = false;
		m_bDragging = false;

		if (m_wGhost)
			m_wGhost.SetVisible(false);
		if (m_wLine)
			m_wLine.SetVisible(false);
		if (m_wLayer)
			m_wLayer.SetVisible(false);
	}

	//------------------------------------------------------------------------------------------------
	protected void StartPump()
	{
		if (m_bPumpRunning)
			return;

		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (!queue)
			return;

		m_bPumpRunning = true;
		queue.CallLater(DragFrame, DRAG_TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void StopPump()
	{
		if (!m_bPumpRunning)
			return;

		m_bPumpRunning = false;

		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
			queue.Remove(DragFrame);
	}

	//------------------------------------------------------------------------------------------------
	//! One tick: promote an armed press that has travelled far enough, then carry the ghost and the
	//! line to the cursor. The menu closing under a held button is what the first test is for - the
	//! pump is the only thing still running at that point.
	protected void DragFrame()
	{
		if (!m_Menu || m_Menu.IsTornDown() || (!m_bArmed && !m_bDragging))
		{
			Cancel();
			StopPump();
			return;
		}

		int cursorX, cursorY;
		WidgetManager.GetMousePos(cursorX, cursorY);

		if (m_bArmed)
		{
			float dx = cursorX - m_fPressX;
			float dy = cursorY - m_fPressY;
			if (dx * dx + dy * dy < DRAG_THRESHOLD * DRAG_THRESHOLD)
				return;

			Begin();
		}

		// the rows under the ghost still answer the cursor, and a tip opening beneath a row in the
		// air is the one thing on screen that is not about the drag
		m_Menu.HoverTip().HideHoverTip();

		PlaceGhost(cursorX, cursorY);
		m_iInsert = InsertPointAt(cursorY);
		PlaceLine(m_iInsert);
	}

	//------------------------------------------------------------------------------------------------
	//! The press becomes a drag: the ghost takes the row's name and its width, and the row it was
	//! copied from fades. The width is measured rather than authored - the band is one of the two
	//! columns that give when the dialog will not fit, so a ghost of a stated width would be the
	//! wrong width on a 4:3 screen.
	protected void Begin()
	{
		m_bArmed = false;
		m_bDragging = true;

		if (m_wGhostName)
			m_wGhostName.SetText(m_sPreset);

		m_wSource = m_Menu.Info().PresetAnchor(m_iFrom);
		if (m_wSource)
		{
			float rowW, rowH;
			m_wSource.GetScreenSize(rowW, rowH);
			SizeRow(m_wGhost, rowW, rowH);
			SizeRow(m_wLine, rowW, -1);
			m_fGhostH = rowH;

			m_wSource.SetOpacity(DRAG_SOURCE_DIM);
		}

		m_wLayer.SetVisible(true);
		m_wGhost.SetVisible(true);
		m_wLine.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! Screen pixels in, layout units out - SizeLayoutWidget's overrides are in the same units
	//! FrameSlot works in. A height of -1 leaves the authored one standing, which the drop line
	//! wants: it takes the rows' width and keeps its own 2.
	protected void SizeRow(Widget w, float screenW, float screenH)
	{
		SizeLayoutWidget size = SizeLayoutWidget.Cast(w);
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!size || !ws)
			return;

		size.SetWidthOverride(ws.DPIUnscale(screenW));
		if (screenH > 0)
			size.SetHeightOverride(ws.DPIUnscale(screenH));
	}

	//------------------------------------------------------------------------------------------------
	//! Where FrameSlot positions are measured from - see DRAG_FRAME. The same rectangle as the
	//! layer the ghost and the line sit in, but laid out before the first drag shows it.
	protected void LayerOrigin(out float originX, out float originY)
	{
		m_wFrame.GetScreenPos(originX, originY);
	}

	//------------------------------------------------------------------------------------------------
	//! The ghost hangs off the cursor rather than centred on it, so the hand never covers the name.
	protected void PlaceGhost(float cursorX, float cursorY)
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		float layerX, layerY;
		LayerOrigin(layerX, layerY);

		FrameSlot.SetPos(m_wGhost, ws.DPIUnscale(cursorX - layerX),
			ws.DPIUnscale(cursorY - layerY - m_fGhostH * 0.5));
	}

	//------------------------------------------------------------------------------------------------
	//! Which gap the row would drop into, from the cursor alone: the first row whose top half the
	//! cursor is above claims the gap before it, and a cursor past every row claims the one after
	//! the last. Reading the rows' own screen boxes is what makes this survive the band scrolling,
	//! which moves the rows but not their order.
	protected int InsertPointAt(float cursorY)
	{
		array<Widget> anchors = m_Menu.Info().PresetAnchors();
		if (!anchors)
			return m_iFrom;

		foreach (int i, Widget anchor : anchors)
		{
			if (!anchor)
				continue;

			float rowX, rowY, rowW, rowH;
			anchor.GetScreenPos(rowX, rowY);
			anchor.GetScreenSize(rowW, rowH);

			if (cursorY < rowY + rowH * 0.5)
				return i;
		}

		return anchors.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! The line sits on a boundary, so it is drawn at the top edge of the row it would push down -
	//! or at the bottom edge of the last row, for the gap that has no row after it.
	protected void PlaceLine(int insert)
	{
		array<Widget> anchors = m_Menu.Info().PresetAnchors();
		if (!anchors || anchors.IsEmpty())
			return;

		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		bool below = insert >= anchors.Count();
		Widget anchor;
		if (below)
			anchor = anchors[anchors.Count() - 1];
		else
			anchor = anchors[insert];

		if (!anchor)
			return;

		float rowX, rowY, rowW, rowH, layerX, layerY;
		anchor.GetScreenPos(rowX, rowY);
		anchor.GetScreenSize(rowW, rowH);
		LayerOrigin(layerX, layerY);

		float edge = rowY;
		if (below)
			edge = rowY + rowH;

		FrameSlot.SetPos(m_wLine, ws.DPIUnscale(rowX - layerX), ws.DPIUnscale(edge - layerY));
	}

	//------------------------------------------------------------------------------------------------
	//! Commits the move, handed everything it needs because the drag it belongs to has already been
	//! taken down - see Release. The insertion point counts gaps with the dragged row still in the
	//! list, and the store counts positions with it taken out, so every gap below the row it came
	//! from is one position lower than it looks. A drop back where it started is not a move, and the
	//! store is not asked to make one. Answers whether the list actually moved, which is what
	//! decides if the release's click belonged to the drag or to the player.
	protected bool Drop(string preset, int from, int insert)
	{
		int to = insert;
		if (to > from)
			to--;

		if (to == from || !m_Menu)
			return false;

		m_Menu.Info().OnPresetDropped(preset, to);
		return true;
	}
}
