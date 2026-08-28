//------------------------------------------------------------------------------------------------
//! The detail band: the open tile's entries, as a weapon panel, an exclusive list or counted rows.
//! Holds three fold fields and the book indexing its stamped rows. The two writers are split by
//! question: the tile column decides which group is open, through SetOpenGroup,
//! SetSelectedWeaponGroup, ClearFold and ClearInnerFold; this panel decides how far it unfolds.
//------------------------------------------------------------------------------------------------
class RK29_MenuDetailPanel
{
	protected static const ResourceName COUNT_ROW_LAYOUT = "{AB29C0FFEEB21500}UI/KitSystem/RK29_CountRow.layout";

	//! Typing convenience only - OnCountEdited clamps whatever is typed to the row's real bounds.
	static const int COUNT_EDIT_MAX_CHARS = 3;
	protected static const float BLOCKED_DIM = 0.35;

	//! The layout's own grey: the seated row overwrites it with black and nothing else puts it back.
	protected static const ref Color LOADED_IDLE = UIColors.NEUTRAL_ACTIVE_STANDBY;

	//! The seated plate when the row it sits on issues NONE: the gun starts on a round the kit does
	//! not carry, which is a legal empty chamber and reads as one - see StampLoadedToggle.
	protected static const ref Color LOADED_EMPTY = UIColors.WARNING;
	protected static const string WEAPON_HEADER = "CHANGE WEAPON";

	//! No number in it on purpose: the glyph the tip panel draws after these words names the
	//! currency that ran out, matching the section caption and the cost tip.
	protected static const string TIP_EXCEEDS_ALLOWED = "Exceeds allowed";
	protected Widget m_wColDetail;
	protected TextWidget m_wDetailTitle;
	protected Widget m_wDetailTitleBand;

	//! Group id the detail pane is showing; "" = closed.
	protected string m_sOpenGroup;

	//! Weapon group the tile column is highlighting; "" = none. Its ammo and attachments are what
	//! the detail pane is then showing.
	protected string m_sSelectedWeaponGroup;

	//! While set, the weapon panel lists the weapon group's own entries instead of what the gun owns.
	protected bool m_bWeaponListOpen;

	//! Which of the gun's choice groups is unfolded; "" = all folded. One id and not a set: the panel
	//! asks one question at a time, so unfolding folds whatever was open, and ToggleWeaponList clears
	//! this for the same reason.
	protected string m_sOpenAttachmentGroup;
	protected ref array<ref RK29_MenuRowRef> m_aDetailRows = {};
	protected ref array<ref RK29_LoadoutRowHandler> m_aDetailHandlers = {};
	protected ref array<ref RK29_HoverTipHandler> m_aDetailTipHandlers = {};

	//! Up while the menu is writing into an edit box, read back through IsEditEcho as "this change is
	//! mine, ignore it". Raised by GuardedSetText and nowhere else - see there for the crash it
	//! answers. Two boxes are behind it: this panel's count boxes, and the info band's preset-name
	//! box, which RK29_LoadoutRowHandler routes through Detail().IsEditEcho and CapEditLength.
	protected bool m_bCountEditEcho;

	//! Not a ref: the menu owns this panel, and a reference back would be an unfreeable cycle.
	protected RK29_LoadoutMenu m_Menu;

	//------------------------------------------------------------------------------------------------
	void Init(RK29_LoadoutMenu menu, Widget colDetail, TextWidget detailTitle, Widget titleBand)
	{
		m_Menu = menu;
		m_wColDetail = colDetail;
		m_wDetailTitle = detailTitle;
		m_wDetailTitleBand = titleBand;
	}

	//------------------------------------------------------------------------------------------------
	void Release()
	{
		m_aDetailHandlers.Clear();
		m_aDetailTipHandlers.Clear();
		m_aDetailRows.Clear();

		m_wColDetail = null;
		m_wDetailTitle = null;
		m_wDetailTitleBand = null;
	}

	//------------------------------------------------------------------------------------------------
	//! Which group the band is showing, and which weapon the tile column is highlighting. Both are
	//! the tile column's answer and reach here through the setters below; what this panel decides
	//! for itself is the two inner folds.
	string OpenGroup()
	{
		return m_sOpenGroup;
	}

	//------------------------------------------------------------------------------------------------
	void SetOpenGroup(string groupId)
	{
		m_sOpenGroup = groupId;
	}

	//------------------------------------------------------------------------------------------------
	string SelectedWeaponGroup()
	{
		return m_sSelectedWeaponGroup;
	}

	//------------------------------------------------------------------------------------------------
	void SetSelectedWeaponGroup(string groupId)
	{
		m_sSelectedWeaponGroup = groupId;
	}

	//------------------------------------------------------------------------------------------------
	//! Fold everything the detail band had open. Three fields: every path that changes which panel is
	//! showing has to put all three back, or a fold stays marked open on a panel that is gone.
	void ClearFold()
	{
		m_sOpenGroup = "";
		ClearInnerFold();
	}

	//------------------------------------------------------------------------------------------------
	//! The two inner folds only, for a path changing what the open panel shows rather than closing
	//! it.
	void ClearInnerFold()
	{
		m_bWeaponListOpen = false;
		m_sOpenAttachmentGroup = "";
	}

	//------------------------------------------------------------------------------------------------
	void BuildDetail()
	{
		if (!m_wColDetail)
			return;

		m_Menu.HoverTip().HideHoverTip();
		RK29_WidgetUtil.ClearChildren(m_wColDetail);
		m_aDetailRows.Clear();
		m_aDetailHandlers.Clear();
		m_aDetailTipHandlers.Clear();

		// nothing open: the band shows the soldier instead of 410 pixels of nothing. He is already
		// dressed - DressMannequin runs on every offer change - so all that happens here is the reveal.
		if (m_sOpenGroup == "")
		{
			SetDetailTitle("");
			m_Menu.Mannequin().RevealMannequin();
			return;
		}

		m_Menu.Mannequin().ShowMannequin(false);

		RK29_ResolvedGroup g = RK29_KitResolve.FindGroup(m_Menu.Offer(), m_sOpenGroup);
		if (!g)
		{
			SetDetailTitle("");
			return;
		}

		// a kit-level weapon group opens the weapon panel instead. The panel names itself, so no title.
		if (g.m_sOwnerWeapon == "" && g.IsWeaponGroup())
		{
			SetDetailTitle("");
			BuildWeaponPanel(g);
			return;
		}

		// a kit-level EXCLUSIVE group has nothing else naming it - its rows are bare answers under no
		// caption of their own, so the band's title is what says which question they answer
		if (g.m_eKind == RK29_EChoiceKind.EXCLUSIVE)
		{
			SetDetailTitle(g.m_sDisplayName);
			BuildExclusiveDetail(g);
			return;
		}

		// No title over a COUNTED group: the header stamped right under it opens with the same display
		// name, so a title band read "Medical" over "Medical".
		SetDetailTitle("");
		StampCountedHeader(g);
		BuildCountedDetail(g);
	}

	//------------------------------------------------------------------------------------------------
	//! The header over a counted group's rows: its name on the left, and - when it spends a budget -
	//! that spend on the right beside the glyph naming the unit. One call, so the two sides cannot be
	//! asked for separately and disagree.
	protected void StampCountedHeader(notnull RK29_ResolvedGroup g)
	{
		RK29_EHeaderFlags flags;
		string total;
		string caption = CountedHeaderCaption(g, flags, total);
		RK29_MenuRowKit.StampHeader(m_wColDetail, caption, flags, total);
	}

	//------------------------------------------------------------------------------------------------
	//! An empty title takes the band away rather than leaving its 32 pixels standing empty - the
	//! weapon panel names itself, so a blank title is the common case and not an accident.
	protected void SetDetailTitle(string title)
	{
		if (m_wDetailTitle)
			m_wDetailTitle.SetText(title);

		if (m_wDetailTitleBand)
			m_wDetailTitleBand.SetVisible(title != "");
	}

	//------------------------------------------------------------------------------------------------
	//! One weapon, whole: the gun as a tile that opens the weapon group's own entries and - while
	//! that list is closed - every group this gun owns. The open list replaces the tile, and picking
	//! any row folds it away again (OnDetailClicked). The owned groups fold the same way in
	//! StampOwnedChoice and share the one-open rule: unfolding either folds the other.
	protected void BuildWeaponPanel(notnull RK29_ResolvedGroup g)
	{
		if (m_bWeaponListOpen && RK29_MenuRowKit.SelectableCount(g) > 1)
		{
			RK29_MenuRowKit.StampHeader(m_wColDetail, WEAPON_HEADER);
			BuildExclusiveDetail(g);
			return;
		}

		// the same caption folded: the tile under it is the button that opens the list. A single-option
		// group gets neither - the header would promise a choice that does not exist, and the tile was a
		// button that did nothing. The panel then opens straight onto what the gun owns.
		if (RK29_MenuRowKit.SelectableCount(g) > 1)
		{
			RK29_MenuRowKit.StampHeader(m_wColDetail, WEAPON_HEADER);
			StampFoldTile(g, m_Menu.WeaponLabelOf(g), m_Menu.ResolvedWeaponPrefabOf(g),
				RK29_EMenuRowKind.WEAPON_FOLD, false, false);
		}

		// keyed by the weapon ID this group currently resolves to, so a pick swap re-parents the panel
		// without touching the selection
		string weaponId = m_Menu.ResolvedWeaponIdOf(g);
		if (weaponId == "")
			return;

		StampOwnedGroups(weaponId);
	}

	//------------------------------------------------------------------------------------------------
	//! One folded tile, of two kinds: the weapon group's, which opens the list of guns, and an
	//! attachment group's, which unfolds that seat's answers in place. Both carry "+N more" - a tile
	//! picturing one answer says nothing about being a button. Only stamped while the fold is closed,
	//! so neither carries a highlight. A bare seat reads "None" with no picture, and the magnified
	//! badge and zoom text stand in for the list this tile replaces.
	protected void StampFoldTile(notnull RK29_ResolvedGroup g, string label, ResourceName prefab,
		RK29_EMenuRowKind kind, bool magnifiedBadge, bool showZoom)
	{
		Widget row = RK29_MenuRowKit.StampTile(m_wColDetail, label);
		if (!row)
			return;

		int index = PushDetailRow(g.m_sId, "");

		RK29_MenuRowKit.FillPreview(row, prefab);
		RK29_MenuRowKit.ShowMoreHint(row, RK29_MenuRowKit.SelectableCount(g) - 1);

		if (magnifiedBadge)
			RK29_MenuRowKit.ShowMagnifiedBadge(row, "RowMagIcon");

		if (showZoom)
			RK29_MenuRowKit.ShowZoomText(row, prefab);

		m_Menu.AttachHandler(row, "RowButton", kind, index, m_aDetailHandlers);
	}

	//------------------------------------------------------------------------------------------------
	//! Everything one weapon owns, in panel order: its attachment-flavoured seats first, then any
	//! other single choice the gun makes - so no group in the offer is unreachable - then its counted
	//! groups. one walk, three buckets. An EXCLUSIVE group with one answer is not shown, and the
	//! synthesized loaded-magazine group is never a section: it is each count row's LOADED toggle.
	//! Sections come out in catalog order - the offer is sorted by m_iOrder, hence the seat first.
	protected void StampOwnedGroups(string weaponId)
	{
		array<RK29_ResolvedGroup> seats = {};
		array<RK29_ResolvedGroup> choices = {};
		array<RK29_ResolvedGroup> counted = {};

		foreach (RK29_ResolvedGroup g : m_Menu.Offer())
		{
			if (!g || g.m_sOwnerWeapon != weaponId)
				continue;

			if (g.m_eKind != RK29_EChoiceKind.EXCLUSIVE)
			{
				counted.Insert(g);
				continue;
			}

			if (g.m_bLoaded || RK29_MenuRowKit.SelectableCount(g) < 2)
				continue;

			if (g.IsAttachmentGroup())
				seats.Insert(g);
			else
				choices.Insert(g);
		}

		foreach (RK29_ResolvedGroup seat : seats)
			StampOwnedChoice(seat);

		foreach (RK29_ResolvedGroup choice : choices)
			StampOwnedChoice(choice);

		foreach (RK29_ResolvedGroup count : counted)
		{
			StampCountedHeader(count);
			BuildCountedDetail(count);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One EXCLUSIVE group the gun owns: its caption, then either the folded tile naming what that
	//! seat currently holds or - for the single group m_sOpenAttachmentGroup names - the flat list of
	//! its answers. The optic seat gets a caption like every other, rather than sectioning itself
	//! into 1X and magnified under a caption over a caption.
	protected void StampOwnedChoice(notnull RK29_ResolvedGroup g)
	{
		RK29_MenuRowKit.StampHeader(m_wColDetail, g.m_sDisplayName);

		if (m_sOpenAttachmentGroup == g.m_sId)
		{
			// the optic seat's list carries the magnified badge per row; every other seat is plain
			if (g.m_bIsOpticsPoint)
				BuildOpticDetail(g);
			else
				BuildExclusiveDetail(g);

			return;
		}

		RK29_KitSetup setup = RK29_MenuRowKit.Setup();
		RK29_ResolvedEntry chosen = RK29_KitResolve.PickedEntry(g, m_Menu.Picks());
		StampFoldTile(g, m_Menu.ExclusiveTileSummary(g), m_Menu.EntryPreviewPrefab(chosen, g),
			RK29_EMenuRowKind.ATTACHMENT_FOLD,
			setup && chosen && RK29_KitResolve.IsMagnifiedEntry(setup, g, chosen),
			chosen != null && g.m_bIsOpticsPoint);
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildExclusiveDetail(notnull RK29_ResolvedGroup g)
	{
		RK29_ResolvedEntry current = RK29_KitResolve.PickedEntry(g, m_Menu.Picks());

		StampNoneRow(g, current);

		foreach (RK29_ResolvedEntry e : g.m_aEntries)
		{
			if (!e)
				continue;

			StampEntryRow(g, e, current);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The optic seat's answers as one flat list under StampOwnedChoice's caption, in the order the
	//! catalog authored (m_iOrder = max magnification x10, so irons and collimators lead). The badge
	//! is asked per row through IsMagnifiedEntry, so order and badging are independent facts - the
	//! list is deliberately not split into 1x and magnified halves, which would order by inference.
	protected void BuildOpticDetail(notnull RK29_ResolvedGroup g)
	{
		RK29_KitSetup setup = RK29_MenuRowKit.Setup();
		RK29_ResolvedEntry current = RK29_KitResolve.PickedEntry(g, m_Menu.Picks());
		StampNoneRow(g, current);
		foreach (RK29_ResolvedEntry e : g.m_aEntries)
		{
			if (!e)
				continue;
			Widget row = StampEntryRow(g, e, current);
			if (row && setup && RK29_KitResolve.IsMagnifiedEntry(setup, g, e))
				RK29_MenuRowKit.ShowMagnifiedBadge(row, "RowMagIcon");
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The bare answer, where the group allows one: the same tile as the answers, minus the picture.
	//! A shorter row here read as a heading rather than a choice.
	protected void StampNoneRow(notnull RK29_ResolvedGroup g, RK29_ResolvedEntry current)
	{
		if (!g.m_bAllowEmpty)
			return;

		Widget noneRow = RK29_MenuRowKit.StampTile(m_wColDetail, "None");
		if (!noneRow)
			return;

		int noneIndex = PushDetailRow(g.m_sId, "");

		// an empty-slot glyph: a name over a blank picture box reads as a picture that failed to load
		Widget noneIcon = noneRow.FindAnyWidget("RowNoneIcon");
		if (noneIcon)
			noneIcon.SetVisible(true);

		m_Menu.SetPlateToggled(noneRow, "RowButton", "RowBg", current == null);

		m_Menu.AttachHandler(noneRow, "RowButton", RK29_EMenuRowKind.DETAIL_ENTRY, noneIndex,
			m_aDetailHandlers);
	}

	//------------------------------------------------------------------------------------------------
	protected string GroupLabel(string groupId)
	{
		RK29_ResolvedGroup g = RK29_KitResolve.FindGroup(m_Menu.Offer(), groupId);
		if (!g)
			return "";
		if (g.m_sDisplayName != "")
			return g.m_sDisplayName;
		return g.m_sId;
	}

	//------------------------------------------------------------------------------------------------
	//! The white glyph on a choice that will take something away, and what it says on hover: the fact
	//! the greyed row states after the click, said before it on the row that causes it. Two hosts,
	//! one name: tile rows carry RowWarnIcon as a corner overlay, stepper rows carry it inside
	//! RowWarnCol so it takes a column rather than landing on a plus button.
	protected void ShowExclusionWarning(notnull Widget row, notnull RK29_ResolvedEntry e)
	{
		if (e.m_sExcludes == "")
			return;

		string note = ExclusionNoteOf(e);
		if (note == "")
			return;

		Widget host = row.FindAnyWidget("RowWarnCol");
		if (!host)
			host = row.FindAnyWidget("RowWarnIcon");
		if (!host)
			return;

		host.SetVisible(true);

		// Image and size-layout widgets ignore the cursor unless a layout says otherwise (vanilla authors
		// "Ignore Cursor" 0 onto every such widget it wants hovered), so the glyph never raised an enter
		// and the tip stayed shut. Every other tip owner here is a button or holds a text child.
		host.ClearFlags(WidgetFlags.IGNORE_CURSOR);
		m_Menu.HoverTip().AttachHoverTip(host, note, m_aDetailTipHandlers);
	}

	//------------------------------------------------------------------------------------------------
	//! "Blocks <item>." - the item, or the whole section where the exclusion names one. Nothing more: the
	//! greyed row explains itself.
	protected string ExclusionNoteOf(notnull RK29_ResolvedEntry e)
	{
		string weaponId, groupId, entryId;
		RK29_KitResolve.SplitTarget(e.m_sExcludes, weaponId, groupId, entryId);

		// the resolver's lookup, not FindGroup: a target behind an include or a weapon prefix
		// resolves there and nowhere else, and a note that misses it hides the warning glyph
		RK29_ResolvedGroup target = RK29_KitResolve.FindGroupOwnedBy(m_Menu.Offer(), weaponId, groupId);
		if (!target)
			return "";

		RK29_ResolvedEntry victim = target.FindEntry(entryId);
		if (!victim)
			return "Blocks " + GroupLabel(groupId) + ".";
		return "Blocks " + m_Menu.EntryLabelIn(target, victim) + ".";
	}

	//------------------------------------------------------------------------------------------------
	//! Why a greyed row is greyed, in terms of a row the player can go and change. The log's
	//! "AttachmentSuppressorMark3" names nothing they have seen or can click; what they need is the
	//! other pick and the section holding it, spelled the way the menu spells them.
	protected string BlockedTipOf(notnull RK29_ResolvedGroup g, notnull RK29_ResolvedEntry e)
	{
		string what;
		if (e.m_sBlockedGroup != "")
		{
			RK29_ResolvedGroup src = RK29_KitResolve.FindGroup(m_Menu.Offer(), e.m_sBlockedGroup);
			if (src)
			{
				RK29_ResolvedEntry culprit = src.FindEntry(e.m_sBlockedEntry);
				if (culprit)
					what = m_Menu.EntryLabelIn(src, culprit) + " (" + src.m_sDisplayName + ")";
			}
		}

		// a garment attachment the picked garment cannot seat: what it needs is a host with the slot,
		// and the host's section is the row to change. Before the generic forms, whose "Needs
		// <culprit>" would name the very helmet that refuses it
		if (g.IsGarmentAttachmentGroup() && e.m_bBlockedMissing)
		{
			string hostLabel = GroupLabel(e.m_sBlockedGroup);
			if (hostLabel == "")
				hostLabel = g.m_sGarmentSlot;
			return "Needs a " + hostLabel + " with the " + g.m_sSlotOnGarment + " slot.";
		}

		// an exclusion the kit states rather than something the prefabs refuse. "over 4" is a bound, not an
		// incompatibility, and the count form stays out of prose: item names cannot be pluralised safely
		if (e.m_iBlockedOver >= 0)
		{
			if (what == "")
				what = GroupLabel(e.m_sBlockedGroup);
			if (what == "")
				return "Ruled out by another choice.";
			if (e.m_iBlockedOver > 0)
				return "Blocked - " + what + " over " + e.m_iBlockedOver.ToString() + ".";
			return "Cannot be taken with " + what + ".";
		}

		if (e.m_bBlockedMissing)
		{
			if (what != "")
				return "Needs " + what + ".";
			return "Needs something this weapon is not carrying.";
		}

		if (what != "")
			return "Cannot be fitted alongside " + what + ".";
		return "The weapon itself leaves no room for this.";
	}

	//------------------------------------------------------------------------------------------------
	//! One answer of an EXCLUSIVE group, as the same compact tile the weapon column uses. Returns the
	//! stamped row for a caller with something to say on it - the optics sections' magnified badge.
	protected Widget StampEntryRow(notnull RK29_ResolvedGroup g, notnull RK29_ResolvedEntry e,
		RK29_ResolvedEntry current)
	{
		Widget row = RK29_MenuRowKit.StampTile(m_wColDetail, m_Menu.EntryLabelIn(g, e));
		if (!row)
			return null;

		int index = PushDetailRow(g.m_sId, e.m_sId);

		// a row of the optic seat says what its glass does in its bottom-left corner. The badge only said
		// which family a scope belongs to, and a 6x fixed and a 3-12x variable badge identically. Read
		// off the prefab, so it is right for glass nobody has written a line of config about.
		if (g.m_bIsOpticsPoint)
			RK29_MenuRowKit.ShowZoomText(row, m_Menu.EntryPreviewPrefab(e, g));

		m_Menu.SetPlateToggled(row, "RowButton", "RowBg", current && current.m_sId == e.m_sId);

		RK29_MenuRowKit.FillPreview(row, m_Menu.EntryPreviewPrefab(e, g));

		// An attachment another of this kit's own picks rules out stays on screen - greyed, not
		// clickable, carrying the reason on hover. Taking it out of the list answered a question the
		// player never got to ask: the bayonet was simply gone, with nothing connecting that to the
		// suppressor just fitted.
		if (e.m_bBlocked)
		{
			row.SetOpacity(BLOCKED_DIM);
			RK29_MenuRowKit.MuteRowButtons(row);
			Widget blockedButton = row.FindAnyWidget("RowButton");
			if (blockedButton)
				m_Menu.HoverTip().AttachHoverTip(blockedButton, BlockedTipOf(g, e), m_aDetailTipHandlers);
			return row;
		}

		ShowExclusionWarning(row, e);

		m_Menu.AttachHandler(row, "RowButton", RK29_EMenuRowKind.DETAIL_ENTRY, index,
			m_aDetailHandlers);
		return row;
	}

	//------------------------------------------------------------------------------------------------
	//! One stepper row per item entry. Each stepper greys at the bound it stands on - the floor
	//! below, the entry's cap or the group's remaining budget above - and hovering it names that
	//! bound; the count is typed into as well as stepped, held to the same bounds. A row whose bounds
	//! have closed on one count loses its steppers to a padlock but keeps its LOADED toggle. The
	//! seated row is bound by its authored floor alone: zero is a deliberate empty chamber.
	protected void BuildCountedDetail(notnull RK29_ResolvedGroup g)
	{
		// worked out once for the whole group rather than per row: the spend is the sum over every entry,
		// and the selector over these same entries is what the rows' LOADED toggles pick into
		int spend = GroupSpend(g);
		RK29_ResolvedGroup loadedGroup = LoadedSiblingOf(g);
		string loadedId = SeatedEntryIdOf(loadedGroup);

		foreach (RK29_ResolvedEntry e : g.m_aEntries)
		{
			if (!e || !RK29_EntryItem.Cast(e.m_Def))
				continue;

			RK29_CountRowBounds bounds = new RK29_CountRowBounds();
			bounds.m_iCurrent = RK29_KitResolve.PickedCount(g, e, m_Menu.Picks());
			bounds.m_iSpend = spend;
			bounds.m_bPinned = RK29_MenuRowKit.IsPinnedEntry(e);
			bounds.m_bBlocked = e.m_bBlocked;
			bounds.m_bLoaded = loadedId != "" && e.m_sId == loadedId;
			bounds.m_LoadedGroup = loadedGroup;

			StampCountRow(g, e, bounds);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One count row, against bounds the caller worked out once - see RK29_CountRowBounds.
	protected void StampCountRow(notnull RK29_ResolvedGroup g, notnull RK29_ResolvedEntry e,
		notnull RK29_CountRowBounds bounds)
	{
		Widget row = GetGame().GetWorkspace().CreateWidgets(COUNT_ROW_LAYOUT, m_wColDetail);
		if (!row)
			return;

		int index = PushDetailRow(g.m_sId, e.m_sId);

		RK29_WidgetUtil.SetText(row, "RowName", m_Menu.EntryLabelIn(g, e));
		StampCountRowPicture(row, g, e);

		// A COUNTED row can be ruled out too, and for a long time nothing said so: the flag was only ever
		// read on the exclusive rows, so a blocked magazine or grenade greyed nowhere and was issued
		// anyway. PickedCount already answers zero, so the row states zero and the steppers go.
		if (bounds.m_bBlocked)
		{
			row.SetOpacity(BLOCKED_DIM);
			StampCountCell(row, 0, true, index);
			RK29_MenuRowKit.StripStepperColumns(row);
			RK29_MenuRowKit.MuteRowButtons(row);
			m_Menu.HoverTip().AttachHoverTip(row, BlockedTipOf(g, e), m_aDetailTipHandlers);
			return;
		}

		ShowExclusionWarning(row, e);

		// a pinned row still has to say how many it issues; StampCountCell picks which widget states it
		StampCountCell(row, bounds.m_iCurrent, bounds.m_bPinned, index);

		// what one of this row costs the section budget rides the number it is spent on, pinned or not;
		// an unbudgeted group has no supplies to name and gets no tip at all
		if (g.m_eKind == RK29_EChoiceKind.BUDGETED && g.m_iBudget > 0)
		{
			Widget countBox = row.FindAnyWidget("RowCountSize");
			if (countBox)
				m_Menu.HoverTip().AttachHoverTip(countBox, RK29_MenuRowKit.CostTipOf(e),
					m_aDetailTipHandlers, true);
		}

		if (bounds.m_bPinned)
		{
			// no steppers, and so no stepper handlers and no stepper tips: the padlock is the whole answer.
			// The cost tip attached above is a different question and stays - see CostTipOf.
			RK29_MenuRowKit.StripStepperColumns(row);
			RK29_MenuRowKit.ShowPinnedLock(row);
		}
		else
		{
			StampCountSteppers(row, g, e, bounds, index);
		}

		// the chambering question survives the pin: which magazine the gun starts on is a separate choice
		StampLoadedToggle(row, bounds.m_LoadedGroup, bounds.m_bLoaded, bounds.m_iCurrent, index);
	}

	//------------------------------------------------------------------------------------------------
	//! One resolution, then one of two ways of picturing it: a magazine is marked with the ammo
	//! glyphs it carries, everything else gets the item's own render. Never both - the glyphs already
	//! say it.
	protected void StampCountRowPicture(notnull Widget row, notnull RK29_ResolvedGroup g,
		notnull RK29_ResolvedEntry e)
	{
		ResourceName itemPrefab = m_Menu.ItemEntryPrefab(g, e);
		int ammoFlags = RK29_MenuRowKit.AmmoTypeFlagsOf(itemPrefab);
		if (ammoFlags == 0)
		{
			RK29_MenuRowKit.StampItemPreview(row, itemPrefab);
			return;
		}

		// the empty prefab hides the render's column whole rather than leaving 40px open for nothing
		RK29_MenuRowKit.StampItemPreview(row, ResourceName.Empty);
		RK29_MenuRowKit.StampTraitIcons(row, ammoFlags);
	}

	//------------------------------------------------------------------------------------------------
	//! The two steppers of an adjustable count row: each greyed at the bound it is standing on, each
	//! naming that bound on hover. The greying and the sentence explaining it are read off the same
	//! bounds in the same method, so they cannot disagree about which limit is in the way.
	protected void StampCountSteppers(notnull Widget row, notnull RK29_ResolvedGroup g,
		notnull RK29_ResolvedEntry e, notnull RK29_CountRowBounds bounds, int index)
	{
		RK29_MenuRowKit.DimStepper(row, "BtnMinus", bounds.m_iCurrent > RK29_KitResolve.FloorOf(e));
		RK29_MenuRowKit.DimStepper(row, "BtnPlus",
			RK29_MenuRowKit.CanAddOne(g, e, bounds.m_iCurrent, bounds.m_iSpend));

		m_Menu.AttachHandler(row, "BtnMinus", RK29_EMenuRowKind.COUNT_MINUS, index, m_aDetailHandlers);
		m_Menu.AttachHandler(row, "BtnPlus", RK29_EMenuRowKind.COUNT_PLUS, index, m_aDetailHandlers);

		// the tips go into the same store as the handlers, so a detail rebuild frees both together
		Widget minus = row.FindAnyWidget("BtnMinus");
		if (minus)
			m_Menu.HoverTip().AttachHoverTip(minus, RK29_MenuRowKit.MinusTipOf(e), m_aDetailTipHandlers);

		Widget plus = row.FindAnyWidget("BtnPlus");
		if (!plus)
			return;

		// the row's own cap wins when both bind: raising the budget cannot help a row on its ceiling
		if (bounds.m_iCurrent < RK29_KitResolve.CeilingOf(e)
			&& RK29_MenuRowKit.BudgetBlocksAdd(g, e, bounds.m_iSpend))
			m_Menu.HoverTip().AttachHoverTip(plus, TIP_EXCEEDS_ALLOWED, m_aDetailTipHandlers, true);
		else
			m_Menu.HoverTip().AttachHoverTip(plus, RK29_MenuRowKit.PlusTipOf(e), m_aDetailTipHandlers);
	}

	//------------------------------------------------------------------------------------------------
	//! The number column of one count row, in whichever of its two forms this row can use: a plain
	//! RichText, all a pinned row can be given, or the edit box, which is the count display on a
	//! changeable row. Exactly one is ever visible. What the number means in either form is the
	//! stored total, the seated magazine included - moving the LOADED mark changes nothing the kit
	//! carries, and a LOADED row reading 0 is an authored empty chamber.
	protected void StampCountCell(notnull Widget row, int current, bool pinned, int index)
	{
		TextWidget count = TextWidget.Cast(row.FindAnyWidget("RowCount"));
		EditBoxWidget edit = EditBoxWidget.Cast(row.FindAnyWidget("RowCountEdit"));

		if (pinned)
		{
			if (count)
			{
				count.SetVisible(true);
				count.SetText(current.ToString());
			}

			if (edit)
				edit.SetVisible(false);

			return;
		}

		if (count)
			count.SetVisible(false);

		if (!edit)
			return;

		edit.SetVisible(true);

		// Seeded before the handler is attached: the SetText inside EchoCount raises an OnChange, but
		// nothing is listening to this box yet. It still goes through EchoCount, so "every programmatic
		// SetText on a count box is guarded" holds for the file, and swapping the two calls cannot arm
		// it.
		EchoCount(edit, current);

		// COUNT_EDIT is routed on commit only, in the handler's own OnChange - see OnCountEdited
		m_Menu.AttachHandler(row, "RowCountEdit", RK29_EMenuRowKind.COUNT_EDIT, index,
			m_aDetailHandlers);
	}

	//------------------------------------------------------------------------------------------------
	//! The one way this menu writes into an edit box. EditBoxWidget.SetText fires OnChange in this
	//! engine, with finished true - the one state RK29_LoadoutRowHandler.OnChange routes onward - so
	//! the echo at the end of OnCountEdited arrived straight back in it, forever; the crash reported
	//! from the field is that stack overflow. The flag is raised around the write and read back
	//! through IsEditEcho. On the name box, an unguarded truncate would save a preset mid-typing.
	protected void GuardedSetText(notnull EditBoxWidget edit, string text)
	{
		m_bCountEditEcho = true;
		edit.SetText(text);
		m_bCountEditEcho = false;
	}

	//------------------------------------------------------------------------------------------------
	//! Writes the normalised number into a count row's edit box, unless the box already reads exactly
	//! that. The skip is not only an optimisation: no SetText means no OnChange, so it is the guard
	//! that kills the first re-entry where GuardedSetText's flag kills the loop.
	protected void EchoCount(notnull EditBoxWidget edit, int value)
	{
		string normalised = value.ToString();
		if (edit.GetText() == normalised)
			return;

		GuardedSetText(edit, normalised);
	}

	//------------------------------------------------------------------------------------------------
	//! Read by RK29_LoadoutRowHandler.OnChange, which cannot see the protected flag itself, so a
	//! write of ours is never mistaken for the player typing.
	bool IsEditEcho()
	{
		return m_bCountEditEcho;
	}

	//------------------------------------------------------------------------------------------------
	//! Holds one edit box to a character limit, in script because neither authored route works:
	//! vanilla's EditBoxFilterComponent tests `if (!m_wEditBox || !m_wMultilineEditBox)` where it
	//! means &&, so it refuses every widget in the game, and "Max Length" is multiline-only - a GUI
	//! parse error on the single-line boxes both of ours are. The write back is guarded twice, and
	//! truncation is by bytes, matching RK29_KitPresetStorage.Save, which trims first and governs.
	void CapEditLength(Widget w, int limit)
	{
		if (limit <= 0)
			return;

		EditBoxWidget edit = EditBoxWidget.Cast(w);
		if (!edit)
			return;

		string text = edit.GetText();
		if (text.Length() <= limit)
			return;

		GuardedSetText(edit, text.Substring(0, limit));
	}

	//------------------------------------------------------------------------------------------------
	//! The LOADED toggle on one counted row: lit on the entry the gun is seated with, dim on the
	//! others, clickable while the loaded sibling has more than one answer. No sibling means plain
	//! spares and no toggle; a one-answer sibling lights but takes no handler. The count does not
	//! gate the mark - a mark on a row held at zero is an authored empty chamber, still a real pick -
	//! it only colours it, amber for a seated round the kit carries and red for one it does not.
	protected void StampLoadedToggle(notnull Widget row, RK29_ResolvedGroup loadedGroup,
		bool isLoaded, int count, int index)
	{
		Widget box = row.FindAnyWidget("LoadedBox");

		if (!loadedGroup)
		{
			if (box)
				box.SetVisible(false);
			return;
		}

		if (box)
			box.SetVisible(true);

		m_Menu.SetPlateToggled(row, "BtnLoaded", "LoadedBg", isLoaded);

		// seated on a row held at zero is the deliberate empty chamber. Only the red case is painted:
		// the row is built fresh every stamp, with the layout's amber
		if (isLoaded && count <= 0)
		{
			ImageWidget plate = ImageWidget.Cast(row.FindAnyWidget("LoadedBg"));
			if (plate)
				plate.SetColor(LOADED_EMPTY);
		}

		// the layout's light grey on the seated row's orange plate is barely a word; black is what reads
		TextWidget mark = TextWidget.Cast(row.FindAnyWidget("LoadedMark"));
		if (mark)
		{
			if (isLoaded)
			{
				mark.SetColor(Color.Black);
				mark.SetOpacity(RK29_MenuRowKit.STEP_LIT);
			}
			else
			{
				mark.SetColor(LOADED_IDLE);
				mark.SetOpacity(RK29_MenuRowKit.LOADED_DIM);
			}
		}

		if (RK29_MenuRowKit.SelectableCount(loadedGroup) > 1)
			m_Menu.AttachHandler(row, "BtnLoaded", RK29_EMenuRowKind.LOADED_TOGGLE, index,
				m_aDetailHandlers);
	}

	//------------------------------------------------------------------------------------------------
	//! Every entry's count against its own cost - what the resolver checks a BUDGETED group with.
	protected int GroupSpend(notnull RK29_ResolvedGroup g)
	{
		return RK29_MenuRowKit.GroupSpendOf(g, m_Menu.Picks());
	}

	//------------------------------------------------------------------------------------------------
	//! How far one stepper click travels: ten with Ctrl held, five with Shift, else one. Read off the
	//! two held-key actions the mod's input config puts in DialogContext - the context the dialog
	//! host re-activates every frame, so a plain key-state read is never needed. Ctrl wins when both
	//! are held.
	static int StepMultiplier()
	{
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return 1;
		if (im.GetActionValue("RK29_StepTen") > 0)
			return 10;
		if (im.GetActionValue("RK29_StepFive") > 0)
			return 5;
		return 1;
	}

	//------------------------------------------------------------------------------------------------
	//! The highest count the group's budget leaves this entry; both the plus stepper and a typed
	//! number are bound by this one answer. Integer division on purpose. Negative headroom is floored
	//! at none rather than clamped through - a group already over budget is stepped down out of, and
	//! answering below what the row holds would drag the count with it on a press meant to raise it.
	protected int BudgetCeilingFor(notnull RK29_ResolvedGroup g, notnull RK29_ResolvedEntry e,
		int current)
	{
		if (g.m_eKind != RK29_EChoiceKind.BUDGETED || g.m_iBudget <= 0 || e.m_iCost <= 0)
			return RK29_KitResolve.COUNT_HARD_CEILING;

		int headroom = (g.m_iBudget - GroupSpend(g)) / e.m_iCost;
		if (headroom < 0)
			headroom = 0;

		return current + headroom;
	}

	//------------------------------------------------------------------------------------------------
	//! The caption above a counted group - its name only. The budget spend comes back in outTotal
	//! because the two sit on opposite sides of the header line, the spend inside the supply zone.
	//! Whether a row can be stepped is said on the row by its padlock - a group can hold a pinned
	//! entry and an adjustable one at once - so this answers the flags as well as the words.
	protected string CountedHeaderCaption(notnull RK29_ResolvedGroup g,
		out RK29_EHeaderFlags outFlags, out string outTotal)
	{
		outFlags = RK29_EHeaderFlags.NONE;
		outTotal = "";

		if (g.m_eKind != RK29_EChoiceKind.BUDGETED || g.m_iBudget <= 0)
			return g.m_sDisplayName;

		outFlags = RK29_EHeaderFlags.SUPPLY;
		outTotal = GroupSpend(g).ToString() + "/" + g.m_iBudget.ToString();
		return g.m_sDisplayName;
	}

	//------------------------------------------------------------------------------------------------
	//! The synthesized loaded-magazine selector standing over this counted group, or null when the
	//! offer carries none - then the counts are plain spares and no row shows a LOADED toggle. The
	//! resolver owns the rule and the suffix it is named off; all this adds is the offer to look in.
	protected RK29_ResolvedGroup LoadedSiblingOf(notnull RK29_ResolvedGroup g)
	{
		return RK29_KitResolve.LoadedSiblingOf(m_Menu.Offer(), g);
	}

	//------------------------------------------------------------------------------------------------
	//! The pick, else what the selector defaults to; "" for no selector at all.
	protected string SeatedEntryIdOf(RK29_ResolvedGroup loaded)
	{
		if (!loaded)
			return "";

		RK29_ResolvedEntry seated = RK29_KitResolve.PickedEntry(loaded, m_Menu.Picks());
		if (!seated)
			seated = loaded.DefaultEntry();
		if (!seated)
			return "";

		return seated.m_sId;
	}

	//------------------------------------------------------------------------------------------------
	//! Books one stamped detail row and answers the index its handler must carry. The invariant lives
	//! here: a row's handler index is its position in m_aDetailRows, so the ref is inserted and the
	//! index read off one call - spelled out per stamp site it is two arrays kept parallel by hand.
	protected int PushDetailRow(string groupId, string entryId)
	{
		RK29_MenuRowRef stamped = new RK29_MenuRowRef();
		stamped.m_sGroup = groupId;
		stamped.m_sEntry = entryId;
		m_aDetailRows.Insert(stamped);

		return m_aDetailRows.Count() - 1;
	}

	//------------------------------------------------------------------------------------------------
	//! Null for an index this column no longer has - a click routed from a handler the rebuild that
	//! freed its row has not yet taken away. Every consumer goes through this, so the test is once.
	protected RK29_MenuRowRef DetailRow(int index)
	{
		if (index < 0 || index >= m_aDetailRows.Count())
			return null;

		return m_aDetailRows[index];
	}

	//------------------------------------------------------------------------------------------------
	//! A folded group's tile: that group unfolds in place and everything else on the panel folds, the
	//! weapon list included. Nothing is picked, so only the detail column moves. Set rather than
	//! toggled - the tile exists only while the group is folded, and picking any row of the list it
	//! opened is what folds it again, in OnDetailClicked.
	void OnAttachmentGroupClicked(int index)
	{
		RK29_MenuRowRef stamped = DetailRow(index);
		if (!stamped)
			return;

		m_sOpenAttachmentGroup = stamped.m_sGroup;
		m_bWeaponListOpen = false;
		BuildDetail();
	}

	//------------------------------------------------------------------------------------------------
	//! The pick lands on the ammo group's loaded sibling, naming the entry this row is for; the
	//! ordinary pick path rebuilds from there, which moves the mark onto the row just clicked.
	void OnLoadedToggle(int index)
	{
		RK29_MenuRowRef stamped = DetailRow(index);
		if (!stamped)
			return;

		RK29_ResolvedGroup g = RK29_KitResolve.FindGroup(m_Menu.Offer(), stamped.m_sGroup);
		if (!g)
			return;

		RK29_ResolvedGroup loaded = LoadedSiblingOf(g);
		if (!loaded)
			return;

		string entryId = stamped.m_sEntry;
		if (entryId == "" || !loaded.FindEntry(entryId))
			return;

		m_Menu.SetPick(loaded.m_sId, entryId);
		m_Menu.AfterPickChanged();
	}

	//------------------------------------------------------------------------------------------------
	//! Unfolds this weapon group's entries in place, and folds them away again; nothing is picked, so
	//! only the detail column moves. Whatever group of the gun was unfolded folds with it either way
	//! round: opening cannot leave a seat of the old gun standing open, and closing must not leave
	//! one seat marked open on a panel that is going back to showing every seat folded.
	void ToggleWeaponList()
	{
		m_bWeaponListOpen = !m_bWeaponListOpen;
		m_sOpenAttachmentGroup = "";
		BuildDetail();
	}

	//------------------------------------------------------------------------------------------------
	void OnDetailClicked(int index)
	{
		RK29_MenuRowRef stamped = DetailRow(index);
		if (!stamped)
			return;

		// a pick folds the panel back up whichever list it came out of: the panel then shows what the
		// newly chosen gun owns, and the answer just chosen is read on its own folded tile
		ClearInnerFold();

		string groupId = stamped.m_sGroup;
		string entryId = stamped.m_sEntry;

		m_Menu.SetPick(groupId, entryId);
		ClearSiblingSlotPicks(groupId, entryId);

		// A garment pick is its own confirmation: one answer, and the thing worth looking at next is
		// the soldier wearing it - so the group closes and BuildDetail brings the mannequin back.
		// Anything worn closes this way, a seat on the helmet included. A seat on a gun does not: the
		// panel it would close is the weapon's own. Counted groups stay open: a number is edited.
		RK29_ResolvedGroup picked = RK29_KitResolve.FindGroup(m_Menu.Offer(), groupId);
		if (picked && (picked.IsClothingGroup() || picked.IsGarmentAttachmentGroup()))
			SetOpenGroup("");

		m_Menu.AfterPickChanged();
	}

	//------------------------------------------------------------------------------------------------
	//! One attachment point, one attachment. Several groups may ask about the same seat - the optic
	//! point is authored as a 1x tier and a magnified tier, so an override can take one away - so
	//! seating out of one tier states explicitly that every sibling tier is bare: the resolver reads
	//! a group with no pick as merely defaulting to bare, so only the pick written here empties it.
	//! Choosing None clears nothing, and a group with no seat of its own shares with nobody.
	protected void ClearSiblingSlotPicks(string groupId, string entryId)
	{
		if (entryId == "")
			return;

		RK29_ResolvedGroup picked = RK29_KitResolve.FindGroup(m_Menu.Offer(), groupId);
		if (!picked || !picked.IsAttachmentGroup())
			return;

		foreach (RK29_ResolvedGroup g : m_Menu.Offer())
		{
			if (!g || g.m_sId == groupId)
				continue;
			if (!picked.SharesSeatWith(g))
				continue;

			m_Menu.SetPick(g.m_sId, "");
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Client-side clamp only: the resolver clamps again on every read, and the server re-clamps the
	//! parsed picks against its own offer. `step` is the button's direction, one either way; a held
	//! modifier scales it (Shift five, Ctrl ten) and the same bounds then clamp the whole stride, so
	//! a big step near a limit lands on the limit rather than being refused.
	void OnCountStep(int index, int step)
	{
		RK29_MenuRowRef stamped = DetailRow(index);
		if (!stamped)
			return;

		RK29_ResolvedGroup g = RK29_KitResolve.FindGroup(m_Menu.Offer(), stamped.m_sGroup);
		if (!g)
			return;

		RK29_ResolvedEntry e = g.FindEntry(stamped.m_sEntry);
		if (!e)
			return;

		// the authored floor and nothing else: a row the gun is loaded from may be stepped all the way to
		// zero, which is how an empty chamber is authored - the apply pass clears the muzzle for it
		int low = RK29_KitResolve.FloorOf(e);
		int high = RK29_KitResolve.CeilingOf(e);

		int current = RK29_KitResolve.PickedCount(g, e, m_Menu.Picks());

		// the budget was the one bound this path never checked: DimStepper greys the plus but the button
		// stays clickable, so a budgeted group could be stepped to 6/5 - and the server, finding an
		// over-budget pick set, silently reset the whole kit to defaults. Checked only upwards, so a
		// group already over its budget can still be stepped back down - and a stride that would
		// overrun the budget stops at it
		int budgetHigh = BudgetCeilingFor(g, e, current);
		if (step > 0 && current >= budgetHigh)
			return;
		if (step > 0 && budgetHigh < high)
			high = budgetHigh;

		int next = current + step * StepMultiplier();
		if (next < low)
			next = low;
		if (next > high)
			next = high;
		if (next == current)
			return;

		m_Menu.SetCount(g.m_sId, e.m_sId, next);
		m_Menu.AfterPickChanged();
	}

	//------------------------------------------------------------------------------------------------
	//! A count typed into a row's edit box and committed - enter, or the box losing focus - held to
	//! the same three bounds OnCountStep uses. Anything that is not a plain run of digits is not
	//! guessed at: the box is put back and no pick moves, an empty box included. A value that clamps
	//! onto the count already held triggers no rebuild and is normalised in place.
	//! Normalising is what used to crash the game: every write goes through EchoCount, and the first
	//! thing here is a refusal to answer a change EchoCount is itself making - SetText raises
	//! OnChange, so an unguarded echo re-enters until the VM runs out of stack.
	void OnCountEdited(int index, Widget w)
	{
		if (m_bCountEditEcho)
			return;

		EditBoxWidget edit = EditBoxWidget.Cast(w);
		RK29_MenuRowRef stamped = DetailRow(index);
		if (!edit || !stamped)
			return;

		RK29_ResolvedGroup g = RK29_KitResolve.FindGroup(m_Menu.Offer(), stamped.m_sGroup);
		if (!g)
			return;

		RK29_ResolvedEntry e = g.FindEntry(stamped.m_sEntry);
		if (!e)
			return;

		int current = RK29_KitResolve.PickedCount(g, e, m_Menu.Picks());

		string typed = edit.GetText();
		typed = typed.Trim();
		if (!RK29_MenuRowKit.IsWholeNumber(typed))
		{
			EchoCount(edit, current);
			return;
		}

		int low = RK29_KitResolve.FloorOf(e);
		int high = RK29_KitResolve.CeilingOf(e);

		int budgetHigh = BudgetCeilingFor(g, e, current);
		if (budgetHigh < high)
			high = budgetHigh;

		// an exhausted budget can bind below the floor the config authored for the row, and a kit is not
		// allowed to issue fewer than that floor whatever the supplies say - so the floor wins the tie
		if (high < low)
			high = low;

		int next = typed.ToInt();
		if (next < low)
			next = low;
		if (next > high)
			next = high;

		if (next == current)
		{
			EchoCount(edit, current);
			return;
		}

		m_Menu.SetCount(g.m_sId, e.m_sId, next);
		// Next frame, not now: this runs inside the edit box's own change event, and the rebuild
		// destroys that box and frees the handler still on the stack. The Remove pairing with this
		// arm is RK29_LoadoutMenu.ReleaseBookkeeping's, not this class's.
		GetGame().GetCallqueue().CallLater(m_Menu.AfterPickChangedDeferred, 0, false);
	}
}
