//------------------------------------------------------------------------------------------------
//! The right-hand band: weight, traits and saved kits. The only column that is about the player as
//! well as the kit, and the only one holding a store of its own - the preset staleness cache.
//------------------------------------------------------------------------------------------------
class RK29_MenuInfoBand
{
	//! The info band's trait row. It carries no authored tooltip: the vanilla one is unusable here,
	//! the menu's own hover tip says what a trait does - see RK29_HoverTipHandler.
	protected static const ResourceName TRAIT_ROW_LAYOUT = "{AB29C0FFEEB22300}UI/KitSystem/RK29_TraitRow.layout";

	//! The info band's saved kits row. One layout, two modes: StampPresetRow shows the name and
	//! hides the edit box, StampSavePresetRow the reverse.
	protected static const ResourceName PRESET_ROW_LAYOUT = "{AB29C0FFEEB22500}UI/KitSystem/RK29_PresetRow.layout";
	protected static const string TRAITS_HEADER = "TRAITS";
	protected static const string WEIGHT_HEADER = "WEIGHT";
	protected static const string PRESETS_HEADER = "SAVED KITS";
	protected static const string PRESET_STANDARD_LABEL = "Standard";

	//! A preset written in a picks dialect this build does not speak. Said on the row and not only
	//! in the tip: such a row does nothing when clicked, and that reads as broken.
	protected static const string PRESET_OLD_FORMAT_SUFFIX = " (old format)";
	protected static const string PRESET_STALE_SUFFIX = " (outdated)";

	//! Amber ink, and deliberately neither a dim nor a plate: a filled amber row is this menu's
	//! "current selection", and an outdated preset is still perfectly loadable - it wants a warning.
	protected static const ref Color PRESET_STALE_INK = new Color(0.761, 0.386, 0.08, 1.0);

	//! The kit-info band. Rebuilt on a pick as well as on a class change, because weight reads the
	//! picks - see BuildInfoPanel.
	protected Widget m_wColInfo;

	//! The preset name each stamped preset row stands for, in stamp order; the Standard row and the
	//! save row deliberately take no place in it - see PushPresetRow.
	protected ref array<string> m_aPresetRows = {};
	protected ref array<ref RK29_LoadoutRowHandler> m_aInfoHandlers = {};

	//! Preset wire under a class -> its stale count. Cached: it was rebuilding one full offer per
	//! preset on every stepper click.
	protected ref map<string, int> m_mPresetStaleCache = new map<string, int>();
	protected ref array<ref RK29_HoverTipHandler> m_aInfoTipHandlers = {};

	//! Not a ref: the menu owns this panel, and a reference back would be an unfreeable cycle.
	protected RK29_LoadoutMenu m_Menu;

	//------------------------------------------------------------------------------------------------
	void Init(RK29_LoadoutMenu menu, Widget colInfo)
	{
		m_Menu = menu;
		m_wColInfo = colInfo;
	}

	//------------------------------------------------------------------------------------------------
	//! The deferred rebuild is armed in this class and taken back in this class - see
	//! OnPresetNameCommitted; the menu's teardown calls this and nothing else removes it.
	void Release()
	{
		GetGame().GetCallqueue().Remove(BuildInfoPanelDeferred);

		m_aInfoHandlers.Clear();
		m_aInfoTipHandlers.Clear();
		m_aPresetRows.Clear();
		m_mPresetStaleCache.Clear();

		m_wColInfo = null;
	}

	//============================================================================================
	// Kit info
	//============================================================================================
	//------------------------------------------------------------------------------------------------
	//! The band on the right of the columns: weight, then traits, then saved kits. Weight is the sum
	//! of the current picks, so the whole band is rebuilt from AfterPickChanged as well as on a class
	//! change, and it leads so it does not ride below a trait list of varying length. Each section
	//! stamps nothing when it has nothing to say, header included.
	void BuildInfoPanel()
	{
		if (!m_wColInfo)
			return;

		m_Menu.HoverTip().HideHoverTip();
		RK29_WidgetUtil.ClearChildren(m_wColInfo);
		m_aInfoTipHandlers.Clear();
		m_aInfoHandlers.Clear();
		m_aPresetRows.Clear();

		RK29_ClassSetup cls = m_Menu.CurrentClass();
		if (!cls)
			return;

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return;

		// Weight first: it pins the one number that moves on every pick to the top of the band, where it
		// stands in the same place on every class
		BuildWeightSection();

		RK29_KitStruct kit = mgr.KitByName(cls.m_sKitName);
		if (kit && kit.m_aTraits)
			BuildTraitSection(kit);

		// Last, and below traits: the only section about the player rather than the kit, and the only one
		// that grows - a band whose top half moved would put weight somewhere new on every save
		BuildPresetSection(cls);
	}

	//------------------------------------------------------------------------------------------------
	//! What the kit weighs, as one plain row; stated, never asked. It is a measurement, not a sum:
	//! pricing each prefab and adding the buckets up got flares, the M72 family and the radios wrong.
	//! RK29_KitWeight.LiveTotal asks the dressed preview body vanilla's own
	//! GetTotalWeightOfAllStorages. Order is load-bearing: the body must already wear the current
	//! picks. No body means no row, header included; reading low by one item's mass is a dress fault.
	protected void BuildWeightSection()
	{
		float total = RK29_KitWeight.LiveTotal(m_Menu.Mannequin().Body());
		if (total < 0)
			return;

		RK29_MenuRowKit.StampHeader(m_wColInfo, WEIGHT_HEADER);

		Widget row = GetGame().GetWorkspace().CreateWidgets(RK29_MenuRowKit.ROW_LAYOUT, m_wColInfo);
		if (!row)
			return;

		RK29_MenuRowKit.TrimRowColumns(row);
		RK29_WidgetUtil.SetText(row, "RowName", RK29_KitWeight.WeightLabel(total));

		// the number is what fits; anything the dress could not place is not in it, so the row goes
		// red and names the overflow until the picks fit again
		array<ResourceName> dropped = m_Menu.Mannequin().Dropped();
		if (!dropped || dropped.IsEmpty())
			return;

		TextWidget name = TextWidget.Cast(row.FindAnyWidget("RowName"));
		if (name)
			name.SetColor(UIColors.WARNING);
		// one item per line as authored: a long round name must not be cut by the tip's budget
		m_Menu.HoverTip().AttachHoverTip(row, OverflowTipOf(dropped), m_aInfoTipHandlers, false, true);
	}

	//------------------------------------------------------------------------------------------------
	//! "Does not fit:" then one "2x Bandage" line per item name, in drop order.
	protected static string OverflowTipOf(notnull array<ResourceName> dropped)
	{
		array<string> order = {};
		map<string, int> counts = new map<string, int>();
		foreach (ResourceName prefab : dropped)
		{
			string label = RK29_ItemNames.Get(prefab);
			if (!counts.Contains(label))
				order.Insert(label);
			counts.Set(label, counts.Get(label) + 1);
		}

		string text = "Does not fit:";
		foreach (string label : order)
			text += string.Format("\n%1x %2", counts.Get(label), label);
		return text;
	}

	//------------------------------------------------------------------------------------------------
	//! NONE is the enum's "unset" rather than a qualification, so it is counted out before the header
	//! is stamped: a kit whose only entry is NONE has no traits to caption.
	protected void BuildTraitSection(notnull RK29_KitStruct kit)
	{
		array<RK29_ETrait> shown = {};
		foreach (RK29_ETrait trait : kit.m_aTraits)
		{
			if (trait != RK29_ETrait.NONE && !shown.Contains(trait))
				shown.Insert(trait);
		}

		if (shown.IsEmpty())
			return;

		RK29_MenuRowKit.StampHeader(m_wColInfo, TRAITS_HEADER);

		foreach (RK29_ETrait entry : shown)
			StampTraitRow(entry);
	}

	//------------------------------------------------------------------------------------------------
	//! One trait, stated and never asked - no click handler, no bookkeeping slot. What it carries is
	//! hover text saying what the qualification does, the one thing the name alone cannot say. The
	//! tip hangs on the row's own button, the deepest widget the cursor answers with here.
	protected void StampTraitRow(RK29_ETrait trait)
	{
		Widget row = GetGame().GetWorkspace().CreateWidgets(TRAIT_ROW_LAYOUT, m_wColInfo);
		if (!row)
			return;

		RK29_WidgetUtil.SetText(row, "RowName", RK29_MenuRowKit.TraitNameOf(trait));

		// a trait this menu has no words for yet still gets a tip naming itself - a hover answered with
		// nothing reads as text that failed to load
		string description = RK29_MenuRowKit.TraitDescOf(trait);
		if (description == "")
			description = RK29_MenuRowKit.TraitNameOf(trait);

		Widget button = row.FindAnyWidget("RowButton");
		if (!button)
			button = row;
		m_Menu.HoverTip().AttachHoverTip(button, description, m_aInfoTipHandlers);

		ResourceName texture = RK29_MenuRowKit.TraitIconOf(trait);
		if (texture == ResourceName.Empty)
			return;

		ImageWidget icon = ImageWidget.Cast(row.FindAnyWidget("RowIcon"));
		if (!icon)
			return;

		// a texture that failed to load must leave RowIcon hidden rather than show a blank square -
		// SCR_FactionPlayerList makes the same pair of moves on its own icons
		icon.SetVisible(icon.LoadImageTexture(0, texture));
	}

	//============================================================================================
	// Saved kits
	//============================================================================================
	//------------------------------------------------------------------------------------------------
	//! The player's own pick sets for the open class: the Standard row that puts the authored
	//! defaults back, one row per saved preset, and the name box that saves what is on screen.
	//! Stamped on every BuildInfoPanel, not only on a class change, and that is not waste: which row
	//! is highlighted is computed, never remembered - a row lights when the string it stands for is
	//! the picks' own wire ("" for Standard), so nudging a count after a load lights nothing.
	//! Stamped even when the store holds nothing - the Standard row and the name box are what an
	//! empty section is for.
	protected void BuildPresetSection(notnull RK29_ClassSetup cls)
	{
		if (cls.m_sKitName == "")
			return;

		RK29_KitPresetStorage store = RK29_KitPresetStorage.GetInstance();
		if (!store)
			return;

		string currentWire = RK29_KitResolve.EncodePicks(m_Menu.Picks());

		array<RK29_KitPreset> presets = {};
		store.PresetsFor(cls.m_sKitName, presets);

		// Exactly one row lights. The comparison is stateless, but two rows can hold the same wire - a
		// kit saved while the class stood at its defaults holds the empty wire, Standard's own condition
		// - and pointing at both says nothing. So: a saved kit outranks Standard, the first of two
		// identical saves outranks the rest, and Standard lights only when nothing saved claims it.
		int lit = -1;
		foreach (int i, RK29_KitPreset candidate : presets)
		{
			if (candidate && candidate.m_sPicks == currentWire)
			{
				lit = i;
				break;
			}
		}

		RK29_MenuRowKit.StampHeader(m_wColInfo, PRESETS_HEADER);
		StampStandardRow(currentWire, lit >= 0);

		foreach (int i, RK29_KitPreset preset : presets)
		{
			if (preset)
				StampPresetRow(preset, i == lit);
		}

		StampSavePresetRow();
	}

	//------------------------------------------------------------------------------------------------
	//! The kit as the config authors it, always first and never deletable. Clicking it throws the
	//! session's picks for this class away, which is this menu's only reset to default. Highlighted
	//! exactly when the class is standard, which on the wire is the empty string: an empty pick array
	//! is what makes the authored defaults show, and EncodePicks answers "" for exactly that array.
	//! It takes no place in the preset book, so its handler carries the index 0 and nothing reads it.
	protected void StampStandardRow(string currentWire, bool savedKitLit)
	{
		Widget row = GetGame().GetWorkspace().CreateWidgets(PRESET_ROW_LAYOUT, m_wColInfo);
		if (!row)
			return;

		RK29_WidgetUtil.SetText(row, "RowName", PRESET_STANDARD_LABEL);
		m_Menu.SetPlateToggled(row, "RowButton", "RowBg", currentWire == "" && !savedKitLit);

		Widget button = row.FindAnyWidget("RowButton");
		if (button)
			m_Menu.HoverTip().AttachHoverTip(button, "Reset to the authored defaults.", m_aInfoTipHandlers);

		m_Menu.AttachHandler(row, "RowButton", RK29_EMenuRowKind.PRESET_STANDARD, 0, m_aInfoHandlers);
	}

	//------------------------------------------------------------------------------------------------
	//! One saved preset: its name, what is wrong with it, and the glyph that deletes it. The whole
	//! row loads, only the glyph deletes, and whether it is lit is the caller's answer.
	//! - Healthy - plain white, no suffix, no tip.
	//! - Outdated - amber name and "(outdated)", the tip says how many. It still loads: survivors
	//!   apply and the rest degrade as a stale wire does (PickedEntry/PickedCount tolerate it).
	//! - Old format - a dialect this build does not speak, so the click does nothing. Dimmed and
	//!   named on the row, because a row that ignores a click has to say why; delete still works.
	protected void StampPresetRow(notnull RK29_KitPreset preset, bool lit)
	{
		Widget row = GetGame().GetWorkspace().CreateWidgets(PRESET_ROW_LAYOUT, m_wColInfo);
		if (!row)
			return;

		int index = PushPresetRow(preset.m_sName);

		m_Menu.SetPlateToggled(row, "RowButton", "RowBg", lit);

		bool loadable = RK29_KitPresetStorage.CanLoad(preset);
		int stale = 0;
		if (loadable)
			stale = PresetStaleCount(preset);

		string label = preset.m_sName;
		if (!loadable)
			label += PRESET_OLD_FORMAT_SUFFIX;
		else if (stale > 0)
			label += PRESET_STALE_SUFFIX;

		TextWidget name = TextWidget.Cast(row.FindAnyWidget("RowName"));
		if (name)
		{
			name.SetText(label);

			if (!loadable)
				name.SetOpacity(RK29_MenuRowKit.LOADED_DIM);
			else if (stale > 0)
				name.SetColor(PRESET_STALE_INK);
		}

		Widget button = row.FindAnyWidget("RowButton");
		if (button)
			m_Menu.HoverTip().AttachHoverTip(button, RK29_MenuRowKit.PresetTipOf(loadable, stale),
				m_aInfoTipHandlers);

		// the loading click is attached whatever the state: an old-format row answers it with a log line
		// in OnPresetClicked rather than being a row the cursor silently passes over
		m_Menu.AttachHandler(row, "RowButton", RK29_EMenuRowKind.PRESET_ROW, index, m_aInfoHandlers);

		ShowPresetDelete(row, index);
	}

	//------------------------------------------------------------------------------------------------
	//! The delete glyph on one preset row - the layout authors its whole column hidden, so this is
	//! both the reveal and the wiring. The early return is for the tip: AttachHoverTip takes a
	//! notnull target, while AttachHandler answers a widget it cannot find by naming it in the log
	//! and wiring nothing. SetPlateToggled is the one that falls back to the row.
	protected void ShowPresetDelete(notnull Widget row, int index)
	{
		Widget box = row.FindAnyWidget("RowDeleteSize");
		if (box)
			box.SetVisible(true);

		Widget button = row.FindAnyWidget("RowDelete");
		if (!button)
			return;

		m_Menu.HoverTip().AttachHoverTip(button, "Delete this preset.", m_aInfoTipHandlers);
		m_Menu.AttachHandler(row, "RowDelete", RK29_EMenuRowKind.PRESET_DELETE, index, m_aInfoHandlers);
	}

	//------------------------------------------------------------------------------------------------
	//! The name box that saves what is on screen: the same layout as the rows above, in its other
	//! mode - the name's button hidden whole (an empty Fill button would still take the width) and
	//! the edit box revealed. What the row says is the box's own placeholder, authored in the layout
	//! as vanilla does it (UI/layouts/Editor/Saving/Save_New.layout:59), and the only way that
	//! works: text set in is text the commit would save as a name. Its one write is CapEditLength's
	//! guarded truncate.
	protected void StampSavePresetRow()
	{
		Widget row = GetGame().GetWorkspace().CreateWidgets(PRESET_ROW_LAYOUT, m_wColInfo);
		if (!row)
			return;

		Widget button = row.FindAnyWidget("RowButton");
		if (button)
			button.SetVisible(false);

		EditBoxWidget edit = EditBoxWidget.Cast(row.FindAnyWidget("RowNameEdit"));
		if (!edit)
			return;

		edit.SetVisible(true);

		// PRESET_SAVE_EDIT is routed on commit only - enter, or the box losing focus - the same
		// discipline the count box keeps. The index is unread: the box names no preset, it makes one.
		m_Menu.AttachHandler(row, "RowNameEdit", RK29_EMenuRowKind.PRESET_SAVE_EDIT, 0, m_aInfoHandlers);
	}

	//------------------------------------------------------------------------------------------------
	//! Books one stamped preset row and answers the index its handlers must carry - the same
	//! invariant as PushDetailRow. Both of a preset row's handlers, the load click and the delete
	//! glyph, carry this one index. The Standard row and the save row book nothing: neither stands
	//! for a preset.
	protected int PushPresetRow(string name)
	{
		m_aPresetRows.Insert(name);
		return m_aPresetRows.Count() - 1;
	}

	//------------------------------------------------------------------------------------------------
	//! "" for an index this section no longer has - a click routed from a handler whose row the
	//! rebuild has already freed. Every consumer goes through this, so the test is written once.
	protected string PresetName(int index)
	{
		if (index < 0 || index >= m_aPresetRows.Count())
			return "";

		return m_aPresetRows[index];
	}

	//------------------------------------------------------------------------------------------------
	//! How many of a preset's picks the current offer would refuse, without loading it. Nothing here
	//! clamps anything - every test is put to the code that does the real resolution: FindGroup,
	//! m_bAllowEmpty for a bare pick, FindEntry, and PickedCount handed this pick alone, where a
	//! changed number is one the resolver clamped. The budget is asked last and per group, and an
	//! over-budget group counts once. Cached: each uncached answer builds a whole offer of its own.
	protected int PresetStaleCount(notnull RK29_KitPreset preset)
	{
		RK29_ClassSetup cls = m_Menu.CurrentClass();
		string cacheKey;
		if (cls)
			cacheKey = cls.m_sKitName + "|" + preset.m_sPicks;
		int known;
		if (cacheKey != "" && m_mPresetStaleCache.Find(cacheKey, known))
			return known;
		int counted = CountPresetStale(preset, cls);
		if (cacheKey != "")
			m_mPresetStaleCache.Set(cacheKey, counted);
		return counted;
	}

	//------------------------------------------------------------------------------------------------
	protected int CountPresetStale(notnull RK29_KitPreset preset, RK29_ClassSetup cls)
	{
		array<ref RK29_ChoicePick> picks = {};
		RK29_KitResolve.ParsePicks(preset.m_sPicks, picks);

		// measured against the offer the preset's own picks produce, not the one the current session is
		// looking at: a preset for the other rifle owns groups this rifle does not, which would read as
		// gone
		array<ref RK29_ResolvedGroup> offer = {};
		RK29_KitSetup setup = RK29_MenuRowKit.Setup();
		if (cls && setup)
			RK29_KitResolve.BuildOffer(cls, setup, picks, offer);
		else
			offer = m_Menu.Offer();

		int stale = 0;
		array<string> budgeted = {};

		foreach (RK29_ChoicePick pick : picks)
		{
			if (!pick)
				continue;

			// a group the offer no longer has is not stale: the menu keeps such picks on purpose
			// (the other rifle's ammo counts survive a switch and back), loading drops nothing, and
			// the apply never reads them - counting one here marks a freshly saved preset outdated
			RK29_ResolvedGroup g = RK29_KitResolve.FindGroup(offer, pick.m_sGroup);
			if (!g)
				continue;

			if (pick.m_sEntry == "")
			{
				// a pick naming nothing is a deliberately bare seat, an answer only where the group
				// offers one
				if (!g.m_bAllowEmpty)
					stale++;

				continue;
			}

			RK29_ResolvedEntry e = g.FindEntry(pick.m_sEntry);
			if (!e)
			{
				stale++;
				continue;
			}

			// before the EXCLUSIVE early-out: a config revision that blocks a saved exclusive pick
			// (a new exclusion, a new obstruction) is otherwise loaded silently and dropped by
			// DropBlockedPicks without a word
			if (e.m_bBlocked)
			{
				stale++;
				continue;
			}

			if (g.m_eKind == RK29_EChoiceKind.EXCLUSIVE)
				continue;

			array<ref RK29_ChoicePick> single = {};
			single.Insert(pick);
			if (RK29_KitResolve.PickedCount(g, e, single) != pick.m_iCount)
			{
				stale++;
				continue;
			}

			if (g.m_eKind == RK29_EChoiceKind.BUDGETED && g.m_iBudget > 0
				&& !budgeted.Contains(g.m_sId))
				budgeted.Insert(g.m_sId);
		}

		foreach (string groupId : budgeted)
		{
			RK29_ResolvedGroup g = RK29_KitResolve.FindGroup(offer, groupId);
			if (g && RK29_MenuRowKit.GroupSpendOf(g, picks) > g.m_iBudget)
				stale++;
		}

		return stale;
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildInfoPanelDeferred()
	{
		if (m_Menu.IsTornDown())
			return;
		BuildInfoPanel();
	}

	//------------------------------------------------------------------------------------------------
	//! The Standard row: the session's picks for this class are thrown away and the authored defaults
	//! come back. A fresh array and not a cleared one - the array parked in s_mSavedPicks under a
	//! class the player may switch back to must not be emptied out from under it. The empty set is
	//! parked in its turn, deliberately: a reset the menu forgot the moment it closed would not be a
	//! reset.
	void OnPresetStandardClicked()
	{
		m_Menu.SetPicks(new array<ref RK29_ChoicePick>());
		m_Menu.AfterPickChanged();
	}

	//------------------------------------------------------------------------------------------------
	//! A preset row: its picks replace this class's session picks wholesale. Survivors apply and
	//! invalid picks degrade exactly as a stale wire does - see PresetStaleCount. No confirmation
	//! dialog, and the unapplied edits it overwrites are a real cost, spent knowingly: a dialog would
	//! tax every load to save the occasional misclick, which Standard and the other presets undo
	//! anyway. A dialect this build cannot read does nothing but log.
	void OnPresetClicked(int index)
	{
		string name = PresetName(index);
		if (name == "")
			return;

		RK29_ClassSetup cls = m_Menu.CurrentClass();
		if (!cls)
			return;

		RK29_KitPresetStorage store = RK29_KitPresetStorage.GetInstance();
		if (!store)
			return;

		RK29_KitPreset preset = store.Find(cls.m_sKitName, name);
		if (!preset)
			return;

		if (!RK29_KitPresetStorage.CanLoad(preset))
		{
			Print(string.Format("[RK29] loadout menu: preset '%1' is picks format %2 and this"
				+ " build reads %3 - not loaded", name, preset.m_iFormat,
				RK29_KitPresetStorage.PICKS_FORMAT), LogLevel.NORMAL);
			return;
		}

		m_Menu.SetPicks(new array<ref RK29_ChoicePick>());
		RK29_KitResolve.ParsePicks(preset.m_sPicks, m_Menu.Picks());
		m_Menu.AfterPickChanged();
	}

	//------------------------------------------------------------------------------------------------
	//! The store persists the removal itself, so only the band is restamped: nothing about the picks
	//! moved, so the columns, the offer and the mannequin are all still right.
	void OnPresetDeleteClicked(int index)
	{
		string name = PresetName(index);
		if (name == "")
			return;

		RK29_ClassSetup cls = m_Menu.CurrentClass();
		if (!cls)
			return;

		RK29_KitPresetStorage store = RK29_KitPresetStorage.GetInstance();
		if (!store || !store.Delete(cls.m_sKitName, name))
			return;

		RK29_Log.Trace(string.Format("[RK29] loadout menu: deleted preset '%1' of %2", name,
			cls.m_sKitName));
		BuildInfoPanel();
	}

	//------------------------------------------------------------------------------------------------
	//! A name typed into the save row and committed. The picks saved are encoded by the very call
	//! Confirm sends to the server, so what a preset holds and what an apply would send are the same
	//! string by construction. An empty or blank commit is a no-op. A refusal from the store - the
	//! per-class cap is the only one that can reach here - leaves the typed name standing in the box
	//! rather than rebuilding, which does not cost the player their typing.
	void OnPresetNameCommitted(Widget w)
	{
		EditBoxWidget edit = EditBoxWidget.Cast(w);
		if (!edit)
			return;

		string typed = edit.GetText();
		typed = typed.Trim();
		if (typed == "")
			return;

		RK29_ClassSetup cls = m_Menu.CurrentClass();
		if (!cls)
			return;

		RK29_KitPresetStorage store = RK29_KitPresetStorage.GetInstance();
		if (!store)
			return;

		string wire = RK29_KitResolve.EncodePicks(m_Menu.Picks());
		if (!store.Save(cls.m_sKitName, typed, wire))
		{
			Print(string.Format("[RK29] loadout menu: preset '%1' was not saved - %2 already"
				+ " holds %3 presets", typed, cls.m_sKitName,
				RK29_KitPresetStorage.MAX_PER_KIT), LogLevel.NORMAL);
			return;
		}

		RK29_Log.Trace(string.Format("[RK29] loadout menu: saved preset '%1' of %2 picks='%3'",
			typed, cls.m_sKitName, wire));
		// next frame - see RK29_LoadoutMenu.AfterPickChangedDeferred: the rebuild destroys this box
		GetGame().GetCallqueue().CallLater(BuildInfoPanelDeferred, 0, false);
	}
}
