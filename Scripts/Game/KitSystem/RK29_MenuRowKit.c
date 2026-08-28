//------------------------------------------------------------------------------------------------
//! What one stamped row's click means; AttachHandler names one and RK29_LoadoutMenu.OnRowClicked
//! dispatches on it - except COUNT_EDIT and PRESET_SAVE_EDIT, the two edit boxes, which are
//! dispatched from RK29_LoadoutRowHandler.OnChange on commit and never reach OnRowClicked.
enum RK29_EMenuRowKind
{
	CLASS_ROW,
	TILE,
	DETAIL_ENTRY,
	COUNT_MINUS,
	COUNT_PLUS,
	//! the weapon panel's own weapon tile - unfolds that weapon group's entries
	WEAPON_FOLD,
	//! a count row's in-gun toggle - seats that row's magazine in the weapon
	LOADED_TOGGLE,
	//! the typed count, routed on commit only
	COUNT_EDIT,
	ATTACHMENT_FOLD,
	//! the SAVED KITS Standard row - the authored defaults, this menu's only reset, and no preset
	PRESET_STANDARD,
	PRESET_ROW,
	//! shares its preset row's index - see RK29_MenuInfoBand.PushPresetRow
	PRESET_DELETE,
	//! the typed preset name, routed on commit only
	PRESET_SAVE_EDIT,
	MODE_TAB
}

//------------------------------------------------------------------------------------------------
//! What a stamped caption carries besides its words. Both marks are authored hidden on
//! HEADER_LAYOUT - the glyph and the SizeLayout reserving its width - so a header that asks for
//! neither pays for neither.
enum RK29_EHeaderFlags
{
	NONE = 0,

	//! the supplies glyph after the words - turns a budget caption's "4/5" into four of five supplies
	SUPPLY = 1,

	//! padlock: the whole group or gun offers nothing adjustable. The per-row padlocks inside a
	//! detail pane are a different statement about a different thing - see ShowPinnedLock.
	LOCK = 2
}

//------------------------------------------------------------------------------------------------
//! Which group, and which entry of it, one stamped detail row stands for - one per row in stamp
//! order, so a handler's index addresses the pair.
class RK29_MenuRowRef
{
	string m_sGroup;

	//! "" for a row that names no entry of its own: the None row, and the two folded tiles.
	string m_sEntry;
}

//------------------------------------------------------------------------------------------------
//! What one count row is bound by, worked out once by the caller and carried into the stamp: a
//! greying and the tip explaining it must not disagree, and the group-wide half is per-group
//! work.
class RK29_CountRowBounds
{
	int m_iCurrent;

	//! what the whole group has spent - what a BUDGETED plus is measured against
	int m_iSpend;

	//! override has closed this entry onto a single legal count - the row is stated, not asked
	bool m_bPinned;

	//! another of the kit's own picks has ruled this entry out
	bool m_bBlocked;

	bool m_bLoaded;

	//! the synthesized loaded-magazine selector standing over the group, or null for plain spares
	RK29_ResolvedGroup m_LoadedGroup;
}

//------------------------------------------------------------------------------------------------
//! Every row-stamping answer this menu's three columns share, and nothing that holds state: the
//! layouts, the marks a row can wear, and the pure questions asked of a group or an entry. The row
//! identity types live here too, because all three columns book rows with them.
//------------------------------------------------------------------------------------------------
class RK29_MenuRowKit
{
	static const ResourceName ROW_LAYOUT = "{AB29C0FFEEB20035}UI/KitSystem/RK29_Row.layout";
	protected static const ResourceName TILE_ROW_LAYOUT = "{AB29C0FFEEB21600}UI/KitSystem/RK29_TileRow.layout";
	protected static const ResourceName HEADER_LAYOUT = "{AB29C0FFEEB21700}UI/KitSystem/RK29_Header.layout";
	protected static const ResourceName TRAIT_ICON_LAYOUT = "{AB29C0FFEEB21C00}UI/KitSystem/RK29_TraitIcon.layout";

	//! "claimed no body slot at all" - above every real slot index, so such a group sorts last.
	protected static const int NO_SLOT = 9999;

	//! A stepper on its bound is greyed rather than removed: it stays clickable and OnCountStep's
	//! clamp no-ops, so this is affordance only.
	static const float STEP_DIM = 0.25;
	static const float STEP_LIT = 1.0;

	//! Three dims, three meanings: STEP_DIM "not right now", LOADED_DIM "not seated" on a LOADED
	//! mark that is not the chambered row, BLOCKED_DIM "not with that" on a ruled-out attachment.
	static const float LOADED_DIM = 0.5;

	//! Vanilla's own ammo-type glyphs, off the imageset its inventory slots read
	//! (UI/layouts/Menus/Inventory/InventoryAmmoType.layout stamps these very quads).
	protected static const ResourceName AMMO_IMAGESET
		= "{A37CF52DBA874559}UI/Imagesets/WeaponInfo/WeaponInfo_Ammo.imageset";
	protected static const float TRAIT_ICON_SIZE = 20.0;

	//! Exactly the number of branches AmmoTypeQuad answers for - the two have to move together.
	protected static const int AMMO_TYPE_BITS = 13;

	//! MEDIC and SAPPER are the icons EditableEntityCore.conf puts on the labels those traits grant.
	protected static const ResourceName TRAIT_MEDIC_ICON
		= "{00AEC968FDD1DBD0}UI/Textures/Editor/ContentBrowser/ContentBrowser_Character_Medic.edds";
	protected static const ResourceName TRAIT_SAPPER_ICON
		= "{A707BB4A14979472}UI/Textures/Editor/ContentBrowser/ContentBrowser_Trait_Explosive.edds";

	//! Vanilla authors no label icon for TRAIT_VEHICLE_CREW; nearest honest picture off the same
	//! sheet.
	protected static const ResourceName TRAIT_VEHICLE_CREW_ICON
		= "{48C179C2D6F9A236}UI/Textures/Editor/ContentBrowser/ContentBrowser_Trait_Management_Vehicle.edds";

	//! Cached: otherwise a prefab load per count row per detail rebuild. Cleared by ForgetCaches.
	static ref map<ResourceName, int> s_mAmmoFlagsCache = new map<ResourceName, int>();

	//------------------------------------------------------------------------------------------------
	//! Every cache this class holds, at the one boundary that can have invalidated them: a config
	//! edit between sessions could have moved a prefab's authored flags. Called by
	//! RK29_LoadoutMenu.ForgetSession and nothing else.
	static void ForgetCaches()
	{
		s_mAmmoFlagsCache.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! Row.layout reserves fixed-width columns for the magnified tally and the value even while their
	//! contents are hidden, which starves the name and clips long class names. No stamped row uses
	//! them.
	static void TrimRowColumns(notnull Widget row)
	{
		Widget magCol = row.FindAnyWidget("RowMagCol");
		if (magCol)
			magCol.SetVisible(false);

		Widget valueCol = row.FindAnyWidget("RowValueCol");
		if (valueCol)
			valueCol.SetVisible(false);
	}

	//------------------------------------------------------------------------------------------------
	//! The setup every RK29_KitResolve static takes as its first argument, or null while the manager
	//! is not booted far enough to have one.
	static RK29_KitSetup Setup()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return null;

		return mgr.Setup();
	}

	//------------------------------------------------------------------------------------------------
	//! The opening every tile in this menu shares. The five stampers diverge immediately afterwards
	//! and are not folded past this point. Null where the row could not be created, and every caller
	//! must stop there: a booked index with no row behind it is a click routed into a hole.
	static Widget StampTile(Widget parent, string label)
	{
		Widget row = GetGame().GetWorkspace().CreateWidgets(TILE_ROW_LAYOUT, parent);
		if (!row)
			return null;

		RK29_WidgetUtil.SetText(row, "RowName", label);
		return row;
	}

	//------------------------------------------------------------------------------------------------
	static int ClaimedSlotOf(RK29_ResolvedGroup g, notnull map<string, int> slots)
	{
		int slot;
		if (!g || !slots.Find(g.m_sId, slot) || slot < 0)
			return NO_SLOT;

		return slot;
	}

	//------------------------------------------------------------------------------------------------
	//! The body slot the kit will actually dress with this weapon; the grenade slot and the
	//! unplaceable fall through to WEAPON. Numbering off the display order instead was the whole bug,
	//! and no group may override this with an authored caption of its own. 0/1/2 mean what
	//! RK29_KitResolve.ClaimSlot made them mean - RK29_KitResolve.BuildWeaponSlotMap fills the map
	//! this index is read out of.
	static string SlotCaptionOf(int slot)
	{
		if (slot == 0)
			return "PRIMARY";
		if (slot == 1)
			return "SECONDARY";
		if (slot == 2)
			return "SIDEARM";

		return "WEAPON";
	}

	//------------------------------------------------------------------------------------------------
	//! A caption above the rows it introduces: no button, and no bookkeeping slot to route a click
	//! to. What marks it wears besides its words is said by the flags - see RK29_EHeaderFlags.
	//!
	//! The SUPPLY flag opens the header's right-hand zone: `total`, the group's spend over its
	//! budget, with the supplies glyph after it as its unit; the section's name keeps the left.
	//! KitHeader's HeightOverride keeps the larger type from growing the header, so it has to fit
	//! 26 pixels. `total` is ignored without the flag.
	static void StampHeader(Widget parent, string caption,
		RK29_EHeaderFlags flags = RK29_EHeaderFlags.NONE, string total = "")
	{
		if (!parent)
			return;

		Widget row = GetGame().GetWorkspace().CreateWidgets(HEADER_LAYOUT, parent);
		if (!row)
			return;

		RK29_WidgetUtil.SetText(row, "HeaderText", caption);

		// the zone is the one thing authored hidden - what is inside it rides its visibility, so the
		// glyph and the total cannot come up half-drawn on a header that asked for neither
		if (flags & RK29_EHeaderFlags.SUPPLY)
		{
			Widget zone = row.FindAnyWidget("HeaderSupplyZone");
			if (zone)
				zone.SetVisible(true);

			RK29_WidgetUtil.SetText(row, "HeaderTotal", total);
		}

		if (flags & RK29_EHeaderFlags.LOCK)
			RK29_WidgetUtil.RevealPair(row, "HeaderLockSize", "HeaderLockIcon", true);
	}

	//------------------------------------------------------------------------------------------------
	static RK29_EHeaderFlags LockFlag(bool locked)
	{
		if (locked)
			return RK29_EHeaderFlags.LOCK;

		return RK29_EHeaderFlags.NONE;
	}

	//------------------------------------------------------------------------------------------------
	//! string.ToUpper mutates in place and answers the new length rather than the string, so a copy
	//! is shouted and the copy comes back - vanilla makes the same move in
	//! ActionsManagerComponent:62.
	static string Upper(string text)
	{
		string shouted = text;
		shouted.ToUpper();
		return shouted;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether a counted group offers no adjustable count at all: every entry it actually issues is
	//! pinned, so opening it can only state numbers. Counted over the item entries only - exactly the
	//! rows BuildCountedDetail stamps. A counted group that issues nothing is not pinned either. The
	//! answer becomes a padlock on the group's caption - see RK29_MenuTileColumn.StampSmallTile.
	static bool GroupFullyPinned(notnull RK29_ResolvedGroup g)
	{
		// EXCLUSIVE is a choice by construction - but only where there is something to choose between:
		// the marksman rig has one entry per side, so every sniper who opens it sees a single fixed row
		if (g.m_eKind == RK29_EChoiceKind.EXCLUSIVE)
			return SelectableCount(g) < 2;

		bool issued = false;
		foreach (RK29_ResolvedEntry e : g.m_aEntries)
		{
			if (!e || !RK29_EntryItem.Cast(e.m_Def))
				continue;

			issued = true;
			if (!IsPinnedEntry(e))
				return false;
		}

		return issued;
	}

	//------------------------------------------------------------------------------------------------
	//! An entry naming something this faction does not field loses the picture's whole column rather
	//! than holding 30px open for a render that is never coming.
	static void StampChipPreview(notnull Widget chip, ResourceName prefab)
	{
		FillPreviewSlot(chip, "ChipPreviewSize", "ChipPreview", prefab);
	}

	//------------------------------------------------------------------------------------------------
	//! One layout-authored preview slot filled from a prefab: the wrapper reserving its width and the
	//! ItemPreviewWidget inside it. An empty prefab, or no preview manager, takes the wrapper away
	//! rather than leave it holding a column open. CountRow authors both hidden, SummaryChip visible.
	protected static void FillPreviewSlot(notnull Widget host, string boxName, string previewName,
		ResourceName prefab)
	{
		Widget box = host.FindAnyWidget(boxName);
		ItemPreviewWidget preview = ItemPreviewWidget.Cast(host.FindAnyWidget(previewName));
		if (!box || !preview)
			return;

		ItemPreviewManagerEntity previewMgr = RK29_MannequinView.GetItemPreviewManager();
		if (prefab == ResourceName.Empty || !previewMgr)
		{
			box.SetVisible(false);
			return;
		}

		previewMgr.SetPreviewItemFromPrefab(preview, prefab);
		preview.SetVisible(true);
		box.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! The tile row's own preview, filled from a prefab. RowPreview is authored on TILE_ROW_LAYOUT
	//! alone, and it has no wrapper to take away: an empty prefab leaves the row as it stands rather
	//! than blanking it, which is why this is not FillPreviewSlot.
	static void FillPreview(notnull Widget row, ResourceName prefab)
	{
		if (prefab == ResourceName.Empty)
			return;

		ItemPreviewWidget rowPreview = ItemPreviewWidget.Cast(row.FindAnyWidget("RowPreview"));
		ItemPreviewManagerEntity previewMgr = RK29_MannequinView.GetItemPreviewManager();
		if (!rowPreview || !previewMgr)
			return;

		previewMgr.SetPreviewItemFromPrefab(rowPreview, prefab);
		rowPreview.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! The same TileRow slot pointed at a live local entity: what lets a tile picture a gun with
	//! things hanging off it.
	static void FillPreviewEntity(notnull Widget row, notnull IEntity entity)
	{
		ItemPreviewWidget rowPreview = ItemPreviewWidget.Cast(row.FindAnyWidget("RowPreview"));
		ItemPreviewManagerEntity previewMgr = RK29_MannequinView.GetItemPreviewManager();
		if (!rowPreview || !previewMgr)
			return;

		previewMgr.SetPreviewItem(rowPreview, entity);
		rowPreview.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! Only the two folded tiles ever show the hint: a row of an open list is one of the answers
	//! being counted, not a door onto them. A count below one shows nothing rather than "+0 more".
	//! Tile rows only - RowMoreHint is authored on TILE_ROW_LAYOUT and nowhere else.
	static void ShowMoreHint(notnull Widget row, int count)
	{
		if (count < 1)
			return;

		TextWidget hint = TextWidget.Cast(row.FindAnyWidget("RowMoreHint"));
		if (!hint)
			return;

		hint.SetText("+" + count.ToString() + " more");
		hint.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! How many answers an EXCLUSIVE group actually offers, counting the None row where the group
	//! allows one. Fewer than two means there is nothing to decide.
	static int SelectableCount(notnull RK29_ResolvedGroup g)
	{
		int count = 0;
		foreach (RK29_ResolvedEntry e : g.m_aEntries)
		{
			// a row another pick has ruled out cannot be chosen, so it is not one of the answers this group
			// offers: it must not pad a "+2 more", and a group on its last live answer must padlock
			if (e && !e.m_bBlocked)
				count++;
		}

		if (g.m_bAllowEmpty)
			count++;

		return count;
	}

	//------------------------------------------------------------------------------------------------
	//! TileRow authors the glyph hidden and every tile in this menu is that layout, so this one path
	//! is the only thing that ever turns it on.
	static void ShowMagnifiedBadge(notnull Widget row, string glyph)
	{
		Widget icon = row.FindAnyWidget(glyph);
		if (icon)
			icon.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! What one optic's glass does, in the tile's bottom-left corner. RowCornerValue is an overlay on
	//! the frame, not a cell of the line, which is what keeps it off the marquee'd name and out of
	//! the value column the "+3 more" hint holds. Never suppressed, unlike the badge:
	//! m_bMagnifiedExempt is a doctrine switch and cannot make a 12x scope stop magnifying. Tile rows
	//! only - RowCornerValue is authored on TILE_ROW_LAYOUT and nowhere else.
	static void ShowZoomText(notnull Widget row, ResourceName optic)
	{
		TextWidget corner = TextWidget.Cast(row.FindAnyWidget("RowCornerValue"));
		if (!corner)
			return;

		corner.SetText(ZoomTextOf(optic));
		corner.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! Whether override has closed an entry's bounds onto a single legal count - such a row is stated
	//! rather than asked. A negative or absent maximum is no bound at all and never pins.
	static bool IsPinnedEntry(notnull RK29_ResolvedEntry e)
	{
		return e.m_iMin == e.m_iMax && e.m_iMax > 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Takes a pinned row's two stepper columns away whole - the SizeLayout wrappers, not the buttons
	//! inside them: hiding only the button leaves 60px of reserved nothing with the count in the
	//! hole. Count rows only - BtnMinusSize and BtnPlusSize are RK29_CountRow.layout's alone.
	static void StripStepperColumns(notnull Widget row)
	{
		Widget minusCol = row.FindAnyWidget("BtnMinusSize");
		if (minusCol)
			minusCol.SetVisible(false);

		Widget plusCol = row.FindAnyWidget("BtnPlusSize");
		if (plusCol)
			plusCol.SetVisible(false);
	}

	//------------------------------------------------------------------------------------------------
	//! The padlock on one count row, standing in the width StripStepperColumns just took back.
	//! Per-row on purpose: one entry of a group can be fixed while the one under it is still stepped.
	//! The top-level locks are a different mark in a different place - StampHeader's LOCK flag on the
	//! caption. Only CountRow authors the pair.
	static void ShowPinnedLock(notnull Widget row)
	{
		// the quad is authored on the widget; only the trait icons need LoadImageFromSet, because which
		// quad they draw differs per row
		RK29_WidgetUtil.RevealPair(row, "LockBox", "LockIcon", true);
	}

	//------------------------------------------------------------------------------------------------
	//! What the minus stepper cannot step past: the authored floor, on every row alike, the seated
	//! one included - a LOADED mark on a zeroed row is a deliberate empty chamber. Named on the
	//! button that greys at it rather than on the row, which answered the cursor everywhere but where
	//! it reached.
	static string MinusTipOf(notnull RK29_ResolvedEntry e)
	{
		return "Min: " + RK29_KitResolve.FloorOf(e).ToString();
	}

	//------------------------------------------------------------------------------------------------
	//! What the plus stepper cannot step past: the entry's own cap, and nothing else. The budget is
	//! Not repeated here - the section caption already states it - and a plus the budget rather than
	//! the cap stopped says TIP_EXCEEDS_ALLOWED instead. Every stepper has a bound to name: an entry
	//! that authors no m_iMax still resolves to one through RK29_KitResolve.CeilingOf.
	static string PlusTipOf(notnull RK29_ResolvedEntry e)
	{
		return "Max: " + RK29_KitResolve.CeilingOf(e).ToString();
	}

	//------------------------------------------------------------------------------------------------
	//! What one of this entry costs the budget its group spends against, hung on the count itself:
	//! the caption states the section total and the plus states the cap, so neither says this. The
	//! unit is the supplies glyph the tip panel draws after the words, revealed off the flag only
	//! this tip's attachment sets. Pinned rows carry it too - a fixed row still spends supplies.
	static string CostTipOf(notnull RK29_ResolvedEntry e)
	{
		return "Cost: " + e.m_iCost.ToString();
	}

	//------------------------------------------------------------------------------------------------
	//! The same sum against any pick set, which is what lets a saved preset be weighed against the
	//! budget it would land in without first being loaded - see PresetStaleCount. The counts come
	//! from RK29_KitResolve.PickedCount either way, so this is never a second opinion about clamping.
	//! Delegates to RK29_KitResolve.SpendOf, so it is the same sum the server spends against.
	static int GroupSpendOf(notnull RK29_ResolvedGroup g, array<ref RK29_ChoicePick> picks)
	{
		return RK29_KitResolve.SpendOf(g, picks);
	}

	//------------------------------------------------------------------------------------------------
	//! Under its own cap, and - on a BUDGETED group - still inside the budget with this cost added.
	static bool CanAddOne(notnull RK29_ResolvedGroup g, notnull RK29_ResolvedEntry e,
		int current, int spend)
	{
		if (current >= RK29_KitResolve.CeilingOf(e))
			return false;

		return !BudgetBlocksAdd(g, e, spend);
	}

	//------------------------------------------------------------------------------------------------
	//! Whether the supplies, rather than the row's own cap, are what stops one more. Split out of
	//! CanAddOne so the greying and the tip explaining it read the same test: one test for both
	//! reasons says "Max: 3" where the budget bound, sending the player after a cap that is not in
	//! their way.
	static bool BudgetBlocksAdd(notnull RK29_ResolvedGroup g, notnull RK29_ResolvedEntry e,
		int spend)
	{
		return RK29_KitResolve.OverBudget(g, spend + e.m_iCost);
	}

	//------------------------------------------------------------------------------------------------
	//! Whether a string is a plain run of digits. string.ToInt() cannot be asked this on its own: it
	//! reads "abc", "" and "0" as the same zero, so a mistyped row would silently empty itself. A
	//! leading sign is not accepted either - there is no negative count to express.
	static bool IsWholeNumber(string text)
	{
		int length = text.Length();
		if (length < 1)
			return false;

		for (int i = 0; i < length; i++)
		{
			if (!text.IsDigitAt(i))
				return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Affordance only: the button keeps its handler either way and the clamp in OnCountStep no-ops.
	//! Greyed means "you are standing on a bound"; which bound is named by that button's own hover
	//! tip. Count rows only - BtnMinus and BtnPlus are RK29_CountRow.layout's alone.
	static void DimStepper(notnull Widget row, string buttonName, bool enabled)
	{
		Widget button = row.FindAnyWidget(buttonName);
		if (!button)
			return;

		if (enabled)
			button.SetOpacity(STEP_LIT);
		else
			button.SetOpacity(STEP_DIM);
	}

	//------------------------------------------------------------------------------------------------
	//! A blocked row is on screen to be read, not used: the layout authors hover tint, focus and
	//! click sounds onto every row button, so a greyed row would still light amber, take the focus
	//! ring and click audibly with nothing listening. The loaded chip is hidden too - the stamp that
	//! would have dimmed it never runs for a blocked row.
	//! It spans two layouts on purpose: RowButton is Row/TileRow/PresetRow's, while BtnMinus,
	//! BtnPlus, BtnLoaded and LoadedBox are CountRow's - a row answers for the names it has and the
	//! rest miss harmlessly, so both kinds of blocked row are muted by this one call.
	static void MuteRowButtons(notnull Widget row)
	{
		array<string> buttons = {"RowButton", "BtnMinus", "BtnPlus", "BtnLoaded"};
		foreach (string name : buttons)
		{
			Widget button = row.FindAnyWidget(name);
			if (!button)
				continue;
			SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.FindComponent(button);
			if (comp)
				comp.IgnoreStandardInputs(true);
		}
		Widget loadedBox = row.FindAnyWidget("LoadedBox");
		if (loadedBox)
			loadedBox.SetVisible(false);
	}

	//------------------------------------------------------------------------------------------------
	//! Asked rather than assumed: a button whose plate is out of its reach carries no such effect.
	static bool ButtonOwnsPlate(notnull SCR_ModularButtonComponent comp)
	{
		foreach (SCR_ButtonEffectBase effect : comp.GetAllEffects())
		{
			if (SCR_ButtonEffectVisibility.Cast(effect))
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! The item's own render on a count row, in the narrow column CountRow reserves for it. A
	//! magazine row does not get one - its ammo glyphs already picture it, so StampCountRowPicture
	//! passes the empty prefab, which FillPreviewSlot answers by hiding the whole 40px wrapper.
	static void StampItemPreview(notnull Widget row, ResourceName prefab)
	{
		FillPreviewSlot(row, "RowItemPreviewSize", "RowItemPreview", prefab);
	}

	//------------------------------------------------------------------------------------------------
	//! The enum names are the config's vocabulary and read as shouting. The fallback is for a member
	//! added to RK29_ETrait and not yet given a phrasing here.
	static string TraitNameOf(RK29_ETrait trait)
	{
		switch (trait)
		{
			case RK29_ETrait.MEDIC:			return "Combat Medic";
			case RK29_ETrait.SAPPER:		return "Sapper";
			case RK29_ETrait.VEHICLE_CREW:	return "Vehicle Crew";
		}

		return RK29_Traits.NameOf(trait);
	}

	//------------------------------------------------------------------------------------------------
	//! What a trait does, read off the vanilla user actions that ask for the label
	//! RK29_Traits.LabelOf grants:
	//! - MEDIC (ROLE_MEDIC): SCR_InspectCasualtyUserAction:137,
	//!   SCR_LoadCasualtySupportStationUserAction:86, SCR_HealSupportStationAction:153; the dressing
	//!   and tourniquet multipliers are SCR_ConsumableEffectBase:141.
	//! - SAPPER (ROLE_SAPPER): SCR_CampaignBuildingBuildUserAction:74,
	//!   SCR_DeployMultiPartInventoryItemAction:384, SCR_RepairAtSupportStationAction:248.
	//! - VEHICLE_CREW (TRAIT_VEHICLE_CREW): repair:248, refuel:225, resupply:100,
	//!   SCR_ResourceContainerVehicleUnloadAction:178 - the same four TRAIT_HELI_CREW is read in.
	static string TraitDescOf(RK29_ETrait trait)
	{
		switch (trait)
		{
			case RK29_ETrait.MEDIC:
				return "Field dressing 1.5x faster, tourniquets 1.2x. Inspects, loads and heals"
					+ " casualties at medical stations 2x faster.";

			case RK29_ETrait.SAPPER:
				return "Builds and fortifies 2x faster. Repairs vehicles 2x faster.";

			case RK29_ETrait.VEHICLE_CREW:
				return "Repairs, refuels, rearms and unloads supplies 2x faster at vehicles.";
		}

		return "";
	}

	//------------------------------------------------------------------------------------------------
	//! Empty for a trait with no picture - the row renders text-only. See the TRAIT_*_ICON constants.
	static ResourceName TraitIconOf(RK29_ETrait trait)
	{
		switch (trait)
		{
			case RK29_ETrait.MEDIC:			return TRAIT_MEDIC_ICON;
			case RK29_ETrait.SAPPER:		return TRAIT_SAPPER_ICON;
			case RK29_ETrait.VEHICLE_CREW:	return TRAIT_VEHICLE_CREW_ICON;
		}

		return ResourceName.Empty;
	}

	//------------------------------------------------------------------------------------------------
	//! "" for a row with no problem to report - AttachHoverTip takes an empty string as "attach
	//! nothing", so a healthy preset answers the cursor with silence.
	static string PresetTipOf(bool loadable, int stale)
	{
		if (!loadable)
			return "Saved by an older build in a picks format this one cannot read. It can only be"
				+ " deleted.";

		if (stale == 1)
			return "1 pick no longer valid - loading this preset drops or adjusts it; save again"
				+ " to clear this.";

		if (stale > 1)
			return stale.ToString() + " picks no longer valid - loading this preset drops or"
				+ " adjusts them; save again to clear this.";

		return "";
	}

	//============================================================================================
	// Ammo trait icons
	//============================================================================================
	//------------------------------------------------------------------------------------------------
	//! The ammo-type glyphs vanilla's inventory puts on a magazine - AP, tracer, subsonic - on the
	//! count row for the same magazine. On such a row these are the only picture, so the item render
	//! is hidden rather than a second box put beside them. Takes flags the caller already resolved:
	//! it has to ask AmmoTypeFlagsOf anyway, and asking twice would load the prefab twice. Count rows
	//! only - TraitIcons is RK29_CountRow.layout's alone.
	static void StampTraitIcons(notnull Widget row, int flags)
	{
		Widget box = row.FindAnyWidget("TraitIcons");
		if (!box)
			return;

		RK29_WidgetUtil.ClearChildren(box);

		if (flags == 0)
			return;

		for (int bit = 0; bit < AMMO_TYPE_BITS; bit++)
		{
			int flag = 1 << bit;
			if (!(flags & flag))
				continue;

			string quad = AmmoTypeQuad(flag);
			if (quad == "")
				continue;

			// stamped from a layout rather than run up with CreateWidget: a bare procedural ImageWidget
			// carries none of the authored texture state and draws a corner of the sheet on a black field
			ImageWidget icon = ImageWidget.Cast(
				GetGame().GetWorkspace().CreateWidgets(TRAIT_ICON_LAYOUT, box));
			if (!icon)
				continue;

			// SCR_VisualisedBallisticConfig.ShowAmmoType switches this very imageset's quads with exactly
			// this pair of calls, in this order
			icon.LoadImageFromSet(0, AMMO_IMAGESET, quad);
			icon.SetSize(TRAIT_ICON_SIZE, TRAIT_ICON_SIZE);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! EAmmoType flags off a magazine prefab's own MagazineComponent UIInfo. Container reads are
	//! merged, so a magazine inheriting its flags answers with them; a family that authors none is
	//! not a miss but plain ball, the attribute's default. 0 for anything that is not a magazine at
	//! all. Cached - see s_mAmmoFlagsCache.
	static int AmmoTypeFlagsOf(ResourceName prefab)
	{
		if (prefab == ResourceName.Empty)
			return 0;

		int cached;
		if (s_mAmmoFlagsCache.Find(prefab, cached))
			return cached;

		int flags = ReadAmmoTypeFlags(prefab);
		s_mAmmoFlagsCache.Set(prefab, flags);
		return flags;
	}

	//------------------------------------------------------------------------------------------------
	protected static int ReadAmmoTypeFlags(ResourceName prefab)
	{
		Resource res = Resource.Load(prefab);
		if (!res.IsValid())
			return 0;

		IEntitySource src = res.GetResource().ToEntitySource();
		if (!src)
			return 0;

		for (int i = 0, n = src.GetComponentCount(); i < n; i++)
		{
			IEntityComponentSource comp = src.GetComponent(i);
			if (!comp || comp.GetClassName() != "MagazineComponent")
				continue;

			BaseContainer info = comp.GetObject("UIInfo");
			if (!info)
				return EAmmoType.FMJ;

			int flags;
			if (!info.Get("m_eAmmoTypeFlags", flags))
				return EAmmoType.FMJ;

			return flags;
		}

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! A table and not a string built from the enum name: the imageset's casing is inconsistent
	//! (acronyms upper, words lower) and ILLUMINATION is served by "ammotype-flare_1" rather than the
	//! "ammotype-flare" beside it. AMMO_TYPE_BITS is how many branches this has.
	protected static string AmmoTypeQuad(int flag)
	{
		if (flag == EAmmoType.FMJ)
			return "ammotype-fmj";
		if (flag == EAmmoType.TRACER)
			return "ammotype-tracer";
		if (flag == EAmmoType.AP)
			return "ammotype-AP";
		if (flag == EAmmoType.HE)
			return "ammotype-HE";
		if (flag == EAmmoType.HEAT)
			return "ammotype-HEAT";
		if (flag == EAmmoType.FRAG)
			return "ammotype-frag";
		if (flag == EAmmoType.SMOKE)
			return "ammotype-smoke";
		if (flag == EAmmoType.INCENDIARY)
			return "ammotype-incendiary";
		if (flag == EAmmoType.SNIPER)
			return "ammotype-sniper";
		if (flag == EAmmoType.ILLUMINATION)
			return "ammotype-flare_1";
		if (flag == EAmmoType.TRAINING)
			return "ammotype-training";
		if (flag == EAmmoType.SUBSONIC)
			return "ammotype-subsonic";
		if (flag == EAmmoType.HEDP)
			return "ammotype-HEDP";

		return "";
	}

	//------------------------------------------------------------------------------------------------
	//! What an optic's glass does, as the row reads it: "4x", "2.7x", "3x-9x". One decimal, and a
	//! whole number written as one. A range only where there is one - the tolerance keeps a variable
	//! scope whose ends round to the same tenth from reading "6x-6x". A red dot reads "1x" like
	//! anything else, in the same words the row above used, which is what makes the column scannable.
	protected static string ZoomTextOf(ResourceName optic)
	{
		float minZoom;
		float maxZoom;
		RK29_KitCompose.ReadZoomRange(optic, minZoom, maxZoom);

		string text = ZoomNumber(minZoom) + "x";
		if (maxZoom - minZoom > 0.05)
			text += "-" + ZoomNumber(maxZoom) + "x";

		return text;
	}

	//------------------------------------------------------------------------------------------------
	protected static string ZoomNumber(float zoom)
	{
		string text = zoom.ToString(-1, 1);
		if (text.EndsWith(".0"))
			text = text.Substring(0, text.Length() - 2);

		return text;
	}
}
