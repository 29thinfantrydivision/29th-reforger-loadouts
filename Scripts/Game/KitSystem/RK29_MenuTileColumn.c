//------------------------------------------------------------------------------------------------
//! The left two columns: the faction's classes, and the chosen class's weapons and gear as tiles -
//! or, on the Appearance tab, what the soldier is wearing. Owns the books indexing its own stamped
//! rows; every pick it raises goes back through the menu.
//------------------------------------------------------------------------------------------------
class RK29_MenuTileColumn
{
	protected static const ResourceName SUMMARY_CHIP_LAYOUT = "{AB29C0FFEEB22410}UI/KitSystem/RK29_SummaryChip.layout";

	//! Must match TileRow.layout's authored HeightOverride - the weapon tiles take their height from
	//! it and StampSmallTile overrides the shorter Row.layout to it; disagreeing rows read as two
	//! lists.
	protected static const float TILE_ROW_HEIGHT = 64.0;
	protected Widget m_wColClasses;
	protected Widget m_wColTiles;

	// stamped-row bookkeeping, index-parallel with the widgets in each column. A tile stands for a
	// whole group, a detail row for one entry of one - see RK29_MenuDetailPanel.PushDetailRow for
	// the index invariant.
	protected ref array<string> m_aTileGroup = {};

	// handlers are not owned by the widgets they are added to - one array per column, so a rebuild of
	// one column cannot free the handlers still live on another
	protected ref array<ref RK29_LoadoutRowHandler> m_aClassHandlers = {};
	protected ref array<ref RK29_LoadoutRowHandler> m_aTileHandlers = {};
	protected ref array<ref RK29_LoadoutRowHandler> m_aModeHandlers = {};
	protected bool m_bAppearance;

	//! Worn order, not resolve order. Index-parallel: slot i is captioned by caption i.
	protected static ref array<string> s_aWornSlots = {
		"Hat", "Jacket", "ArmoredVest", "Vest", "Back", "HandWear", "Pants", "Boots"};
	protected static ref array<string> s_aWornCaptions = {
		"HELMET", "JACKET", "BODY ARMOUR", "VEST", "BACKPACK", "GLOVES", "TROUSERS", "BOOTS"};

	//! Not a ref: the menu owns this panel, and a reference back would be an unfreeable cycle.
	protected RK29_LoadoutMenu m_Menu;

	//------------------------------------------------------------------------------------------------
	void Init(RK29_LoadoutMenu menu, Widget colClasses, Widget colTiles)
	{
		m_Menu = menu;
		m_wColClasses = colClasses;
		m_wColTiles = colTiles;

		// bound once, unlike every other handler in this menu: the tabs are authored in the layout rather
		// than stamped per rebuild, so they outlive the columns they switch
		m_Menu.AttachHandler(m_Menu.Root(), "BtnModeLoadout", RK29_EMenuRowKind.MODE_TAB, 0,
			m_aModeHandlers);
		m_Menu.AttachHandler(m_Menu.Root(), "BtnModeAppearance", RK29_EMenuRowKind.MODE_TAB, 1,
			m_aModeHandlers);
	}

	//------------------------------------------------------------------------------------------------
	void Release()
	{
		m_aClassHandlers.Clear();
		m_aTileHandlers.Clear();
		m_aModeHandlers.Clear();
		m_aTileGroup.Clear();

		m_wColClasses = null;
		m_wColTiles = null;
	}

	//============================================================================================
	// Columns
	//============================================================================================
	//------------------------------------------------------------------------------------------------
	void BuildClassColumn()
	{
		if (!m_wColClasses)
			return;

		RK29_WidgetUtil.ClearChildren(m_wColClasses);
		m_aClassHandlers.Clear();

		foreach (int i, RK29_ClassSetup cls : m_Menu.Classes())
		{
			if (!cls)
				continue;

			Widget row = GetGame().GetWorkspace().CreateWidgets(RK29_MenuRowKit.ROW_LAYOUT, m_wColClasses);
			if (!row)
				continue;
			RK29_MenuRowKit.TrimRowColumns(row);

			string label = cls.m_sDisplayName;
			if (label == "")
				label = cls.m_sKitName;

			RK29_WidgetUtil.SetText(row, "RowName", label);
			m_Menu.SetPlateToggled(row, "RowButton", "RowBg", i == m_Menu.ClassIndex());

			StampClassIcon(row, cls);
			m_Menu.AttachHandler(row, "RowButton", RK29_EMenuRowKind.CLASS_ROW, i, m_aClassHandlers);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void StampClassIcon(notnull Widget row, notnull RK29_ClassSetup cls)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return;

		RK29_KitStruct kit = mgr.KitByName(cls.m_sKitName);
		if (!kit || !kit.m_UIInfo)
			return;

		ImageWidget icon = ImageWidget.Cast(row.FindAnyWidget("RowIcon"));
		if (!icon)
			return;

		RK29_KitHud.SetKitIcon(kit, icon);
	}

	//------------------------------------------------------------------------------------------------
	//! The tile column reads as the kit's weapon slots in body-slot order: a caption naming the slot
	//! the kit will actually dress, then a tile carrying that weapon's picture and name - both off
	//! the resolver's own claiming. What a weapon owns is not a tile: it lives in the panel the tile
	//! opens.
	void BuildTiles()
	{
		if (!m_wColTiles)
			return;

		m_Menu.HoverTip().HideHoverTip();
		RK29_WidgetUtil.ClearChildren(m_wColTiles);
		m_aTileGroup.Clear();
		m_aTileHandlers.Clear();

		UpdateModeTabs();
		if (m_bAppearance)
		{
			BuildAppearanceTiles();
			return;
		}

		array<RK29_ResolvedGroup> weapons = {};
		array<RK29_ResolvedGroup> others = {};
		PartitionOffer(weapons, others);

		map<string, int> slots = new map<string, int>();
		BuildWeaponSlotMap(slots);
		OrderWeaponGroups(weapons, slots);

		foreach (RK29_ResolvedGroup wg : weapons)
		{
			int slot = RK29_MenuRowKit.ClaimedSlotOf(wg, slots);

			// the padlock rides the caption - "PRIMARY [lock]" - and not the tile under it: what is locked
			// is the whole slot, which the caption names, while the tile is busy naming the gun
			RK29_MenuRowKit.StampHeader(m_wColTiles, RK29_MenuRowKit.SlotCaptionOf(slot),
				RK29_MenuRowKit.LockFlag(WeaponTileLocked(wg)));
			StampWeaponTile(wg, slot);
		}

		foreach (RK29_ResolvedGroup other : others)
		{
			// a garment group belongs to the other tab, never to both
			if (other && other.IsClothingGroup())
				continue;
			StampSmallTile(other);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! What the character is wearing, one row per garment slot. Driven by the resolved kit rather
	//! than by the offer, because most slots have no choice group at all - helmet, jacket, trousers
	//! and boots are block clothing. A slot that does have a group opens in the detail pane like any
	//! tile.
	protected void BuildAppearanceTiles()
	{
		for (int i = 0, n = s_aWornSlots.Count(); i < n; i++)
		{
			string slot = s_aWornSlots[i];

			RK29_ResolvedGroup g = null;
			// the whole offer, not the kit-level rest: a vest a gun brings with it owns the slot too
			foreach (RK29_ResolvedGroup candidate : m_Menu.Offer())
			{
				if (candidate && candidate.IsClothingGroup() && candidate.m_sWornSlot == slot)
				{
					g = candidate;
					break;
				}
			}

			StampWornTile(slot, s_aWornCaptions[i], g);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One garment slot, whether or not it is a choice. TILE_ROW_LAYOUT and not the small gear row
	//! because it is the one carrying an ItemPreviewWidget. Picture and name come from the resolved
	//! kit: for six of the seven slots there is no group to ask.
	protected void StampWornTile(string slot, string caption, RK29_ResolvedGroup g)
	{
		// no group means nothing to open, one answer means nothing to change - to a player both are the
		// same fact, so both wear the padlock
		RK29_MenuRowKit.StampHeader(m_wColTiles, caption,
			RK29_MenuRowKit.LockFlag(!g || RK29_MenuRowKit.GroupFullyPinned(g)));

		ResourceName garment;
		if (m_Menu.PreviewKit())
			m_Menu.PreviewKit().m_mClothing.Find(slot, garment);

		string label = "None";
		if (garment != ResourceName.Empty)
		{
			label = RK29_ItemNames.Get(garment);
			if (label == "")
				label = FilePath.StripPath(garment);
		}

		Widget row = RK29_MenuRowKit.StampTile(m_wColTiles, label);
		if (!row)
			return;

		RK29_MenuRowKit.FillPreview(row, garment);

		if (!g)
			return;

		int index = m_aTileGroup.Count();
		m_aTileGroup.Insert(g.m_sId);

		m_Menu.SetPlateToggled(row, "RowButton", "RowBg", m_Menu.Detail().OpenGroup() == g.m_sId);

		// "+2 more", the same door the folded tiles carry. Without it a backpack slot offering four bags
		// looks exactly like the boots, which offer none.
		RK29_MenuRowKit.ShowMoreHint(row, RK29_MenuRowKit.SelectableCount(g) - 1);

		// and a padlocked slot does not open at all: six of these slots have no group and have never been
		// clickable, so a seventh wearing the same lock and still opening makes the lock mean nothing
		if (RK29_MenuRowKit.GroupFullyPinned(g))
			return;

		m_Menu.AttachHandler(row, "RowButton", RK29_EMenuRowKind.TILE, index, m_aTileHandlers);
	}

	//------------------------------------------------------------------------------------------------
	//! Flipping the tab changes what the column shows and nothing about the kit. The open detail
	//! panel belongs to the tab being left, so it closes on the way through.
	void OnModeClicked(int index)
	{
		bool want = index == 1;
		if (want == m_bAppearance)
			return;

		m_bAppearance = want;
		m_Menu.Detail().ClearFold();

		// BuildTiles stamps the lit tab itself
		BuildTiles();
		m_Menu.Detail().BuildDetail();
	}

	//------------------------------------------------------------------------------------------------
	//! The lit tab. Authored visible so the bar reads correctly before the first click.
	protected void UpdateModeTabs()
	{
		if (!m_Menu.Root())
			return;

		m_Menu.SetPlateToggled(m_Menu.Root(), "BtnModeLoadout", "ModeLoadoutBg", !m_bAppearance);
		m_Menu.SetPlateToggled(m_Menu.Root(), "BtnModeAppearance", "ModeAppearanceBg", m_bAppearance);
	}

	//------------------------------------------------------------------------------------------------
	//! Which body slot each weapon group's current pick claims; the captions and the column order are
	//! drawn off this, not off display order. One call to RK29_KitResolve.BuildWeaponSlotMap - the
	//! very claiming Apply does - because a copy of those rules can drift while both still compile.
	//! An absent group (unresolvable, or no setup yet) is read as the NO_SLOT sink by ClaimedSlotOf.
	protected void BuildWeaponSlotMap(notnull map<string, int> outSlots)
	{
		RK29_KitSetup setup = RK29_MenuRowKit.Setup();
		if (!setup)
			return;

		RK29_KitResolve.BuildWeaponSlotMap(m_Menu.Offer(), m_Menu.Picks(), setup, m_Menu.FactionKey(),
			outSlots);
	}

	//------------------------------------------------------------------------------------------------
	//! The offer split two ways: kit-level weapon groups and the kit-level rest, offer order kept
	//! inside each. A group a weapon owns falls into neither and is skipped - the weapon panel reads
	//! those off the offer itself, keyed by the weapon its open group resolves to.
	protected void PartitionOffer(notnull array<RK29_ResolvedGroup> outWeapons,
		notnull array<RK29_ResolvedGroup> outOther)
	{
		foreach (RK29_ResolvedGroup g : m_Menu.Offer())
		{
			if (!g || g.m_sOwnerWeapon != "")
				continue;

			if (g.IsWeaponGroup())
				outWeapons.Insert(g);
			else
				outOther.Insert(g);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Body-slot order: primary, secondary, sidearm, then whatever claimed no slot. Insertion sort
	//! because it is stable - two slotless groups keep offer order instead of swapping per rebuild.
	protected void OrderWeaponGroups(notnull array<RK29_ResolvedGroup> groups,
		notnull map<string, int> slots)
	{
		for (int i = 1, n = groups.Count(); i < n; i++)
		{
			RK29_ResolvedGroup moving = groups[i];
			int key = RK29_MenuRowKit.ClaimedSlotOf(moving, slots);

			int j = i - 1;
			while (j >= 0 && RK29_MenuRowKit.ClaimedSlotOf(groups[j], slots) > key)
			{
				groups.Set(j + 1, groups[j]);
				j--;
			}
			groups.Set(j + 1, moving);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One weapon tile, under the caption naming the slot it fills; clicking it opens that weapon's
	//! panel. It pictures the gun as configured by pointing at the mannequin's own weapon, dressed by
	//! DressMannequin before this column is stamped. No second gun is spawned and no tile claims the
	//! one it draws: one entity in several preview widgets is vanilla's own pattern
	//! (SCR_InventoryMenuUI:751-753, SCR_InventorySlotUI:358). No mannequin falls back to the prefab.
	protected void StampWeaponTile(notnull RK29_ResolvedGroup g, int slot)
	{
		Widget row = RK29_MenuRowKit.StampTile(m_wColTiles, m_Menu.WeaponLabelOf(g));
		if (!row)
			return;

		int index = m_aTileGroup.Count();
		m_aTileGroup.Insert(g.m_sId);

		m_Menu.SetPlateToggled(row, "RowButton", "RowBg",
			m_Menu.Detail().SelectedWeaponGroup() == g.m_sId || m_Menu.Detail().OpenGroup() == g.m_sId);

		IEntity onBody = m_Menu.Mannequin().WeaponAt(slot);
		if (onBody)
			RK29_MenuRowKit.FillPreviewEntity(row, onBody);
		else
			RK29_MenuRowKit.FillPreview(row, m_Menu.ResolvedWeaponPrefabOf(g));

		// the glass lives two panels deeper; on the tile it is the one thing worth saying about a gun
		// without opening it. Right end of the row line, opposite the spare count over the render.
		if (WeaponWearsMagnified(g))
			RK29_MenuRowKit.ShowMagnifiedBadge(row, "RowMagIconRight");

		StampSpareMagCount(row, slot);

		m_Menu.AttachHandler(row, "RowButton", RK29_EMenuRowKind.TILE, index, m_aTileHandlers);
	}

	//------------------------------------------------------------------------------------------------
	//! Spare magazines for this gun, read the way vanilla's inventory reads them: off the live weapon
	//! rather than the config - the mannequin is the entity carrying the real kit, and
	//! SCR_InventorySlotUI asks the same two questions of it. No spare arithmetic: the engine counts
	//! magazines that fit this weapon, so the chambered round and the underbarrel's 40mm are out.
	protected void StampSpareMagCount(notnull Widget row, int slot)
	{
		TextWidget countText = TextWidget.Cast(row.FindAnyWidget("RowSpareCount"));
		Widget ammoBox = row.FindAnyWidget("RowAmmoBox");
		if (!countText || !ammoBox)
			return;

		IEntity body = m_Menu.Mannequin().Body();
		IEntity weapon = m_Menu.Mannequin().WeaponAt(slot);
		if (!body || !weapon)
			return;

		BaseWeaponComponent weaponComp = BaseWeaponComponent.Cast(
			weapon.FindComponent(BaseWeaponComponent));
		if (!weaponComp)
			return;

		SCR_InventoryStorageManagerComponent manager = SCR_InventoryStorageManagerComponent.Cast(
			body.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!manager)
			return;

		// By muzzle, not by weapon: a rifle with a launcher under it is one weapon with two muzzles, and
		// the count the badge means is the rifle's
		int spares = 0;
		BaseMuzzleComponent countMuzzle = weaponComp.GetCurrentMuzzle();
		if (countMuzzle)
			spares = manager.GetMagazineCountByMuzzle(body, countMuzzle);
		else
			spares = manager.GetMagazineCountByWeapon(weaponComp);

		// vanilla hides the whole indicator at zero rather than writing "+0"
		if (spares <= 0)
			return;

		countText.SetText("+" + spares.ToString());
		StampMagGlyph(row, weaponComp);
		ammoBox.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! Straight off MuzzleUIInfo.m_MagIndicator - the same object and images SCR_InventorySlotUI
	//! loads. A muzzle that authors no indicator keeps the layout's default.
	protected void StampMagGlyph(notnull Widget row, notnull BaseWeaponComponent weaponComp)
	{
		BaseMuzzleComponent muzzle = weaponComp.GetCurrentMuzzle();
		if (!muzzle)
			return;

		MuzzleUIInfo info = MuzzleUIInfo.Cast(muzzle.GetUIInfo());
		if (!info || !info.m_MagIndicator)
			return;

		ResourceName imageset = info.m_MagIndicator.m_sImagesetIcons;
		if (imageset == ResourceName.Empty)
			return;

		ImageWidget outlineImg = ImageWidget.Cast(row.FindAnyWidget("RowMagOutline"));
		if (outlineImg && info.m_MagIndicator.m_sOutline != "")
			outlineImg.LoadImageFromSet(0, imageset, info.m_MagIndicator.m_sOutline);

		ImageWidget fillImg = ImageWidget.Cast(row.FindAnyWidget("RowMagFill"));
		if (!fillImg)
			return;

		// Not every indicator has a fill, and m_sProgress cannot be trusted to say so: it carries the
		// class default ("magazine-default-fill") whenever the config leaves it out. The RPG is exactly
		// that - m_sOutline "rocket-pg7vm", m_bProgressBar 0 - and drew a magazine box over its rocket.
		if (!info.m_MagIndicator.m_bProgressBar || info.m_MagIndicator.m_sProgress == "")
		{
			fillImg.SetVisible(false);
			return;
		}

		fillImg.SetVisible(true);
		fillImg.LoadImageFromSet(0, imageset, info.m_MagIndicator.m_sProgress);
		// the mask is per magazine too: the pistol's fill through the AK's alpha is a curved silhouette
		if (info.m_MagIndicator.m_sProgressAlphaMask != "")
			fillImg.LoadMaskFromSet(imageset, info.m_MagIndicator.m_sProgressAlphaMask);

		// Full, always: every magazine this menu issues is fresh. Vanilla drives this from rounds left.
		fillImg.SetMaskProgress(1.0);
	}

	//------------------------------------------------------------------------------------------------
	//! The group's own name, shouted the way the weapon slot captions are. A group that authored no
	//! display name is captioned by its id - a blank caption leaves whatever is under it unexplained.
	protected string SmallTileCaptionOf(notnull RK29_ResolvedGroup g)
	{
		string caption = g.m_sDisplayName;
		if (caption == "")
			caption = g.m_sId;

		return RK29_MenuRowKit.Upper(caption);
	}

	//------------------------------------------------------------------------------------------------
	//! Everything that is not a weapon: a caption naming the group (a label; only the row is a
	//! button), and under it one row carrying what the group gives the kit. An exclusive group reads
	//! as the one item it holds, pictured and named; a counted group reads as one chip per item.
	protected void StampSmallTile(notnull RK29_ResolvedGroup g)
	{
		// a group that issues nothing adjustable wears its padlock on the caption - "GRENADES [lock]" -
		// which answers "why can't I change any of this" before the tile is opened. It still opens,
		// unlike a padlocked garment slot: behind the click are rows naming items this tile only
		// summarises.
		RK29_MenuRowKit.StampHeader(m_wColTiles, SmallTileCaptionOf(g),
			RK29_MenuRowKit.LockFlag(RK29_MenuRowKit.GroupFullyPinned(g)));

		// One answer gets the row that can picture it. An exclusive group holds a single item and so
		// reads exactly like a garment slot; StampWornTile takes TILE_ROW_LAYOUT for the same reason,
		// it being the row carrying an ItemPreviewWidget. A COUNTED group cannot: its summary is a
		// strip of chips, and Row.layout is the one authored to hold them.
		Widget row;
		if (g.m_eKind == RK29_EChoiceKind.EXCLUSIVE)
		{
			row = RK29_MenuRowKit.StampTile(m_wColTiles, m_Menu.ExclusiveTileSummary(g));
			if (!row)
				return;

			// an unpicked group hands the same empty a bare garment slot does
			RK29_MenuRowKit.FillPreview(row, m_Menu.ExclusiveTilePreview(g));
		}
		else
		{
			row = GetGame().GetWorkspace().CreateWidgets(RK29_MenuRowKit.ROW_LAYOUT, m_wColTiles);
			if (!row)
				return;

			// Row.layout is authored 40 tall against the weapon tiles' TileRow 64, which stepped the
			// column down at Medical. Its root SizeLayout authors AllowHeightOverride, so the row is
			// told the tile height instead. TILE_ROW_LAYOUT is already that tall and needs none of it.
			SizeLayoutWidget rowSize = SizeLayoutWidget.Cast(row);
			if (rowSize)
				rowSize.SetHeightOverride(TILE_ROW_HEIGHT);

			// a chip row uses none of the number columns, and hides its name clip rather than the text
			// inside it - both clips are Fill, so an emptied clip would still claim half the row
			RK29_MenuRowKit.TrimRowColumns(row);

			Widget nameClip = row.FindAnyWidget("NameClip");
			if (nameClip)
			{
				nameClip.SetVisible(false);
			}
			else
			{
				TextWidget name = TextWidget.Cast(row.FindAnyWidget("RowName"));
				if (name)
					name.SetVisible(false);
			}

			StampSummaryChips(row, g);
		}

		int index = m_aTileGroup.Count();
		m_aTileGroup.Insert(g.m_sId);

		m_Menu.SetPlateToggled(row, "RowButton", "RowBg", m_Menu.Detail().OpenGroup() == g.m_sId);

		m_Menu.AttachHandler(row, "RowButton", RK29_EMenuRowKind.TILE, index, m_aTileHandlers);
	}

	//------------------------------------------------------------------------------------------------
	//! GroupFullyPinned's question asked of a weapon slot: is anything behind this tile changeable?
	//! Locked means the group offers fewer than two answers and every group the gun owns is
	//! uneditable - an EXCLUSIVE one (optic seat, bayonet, and the synthesized loaded-magazine
	//! selector, deliberately not skipped) below two answers, a counted one with every entry pinned.
	//! A group resolving to no weapon is locked outright: parade's M21 would open an empty panel.
	protected bool WeaponTileLocked(notnull RK29_ResolvedGroup g)
	{
		if (RK29_MenuRowKit.SelectableCount(g) > 1)
			return false;

		string weaponId = m_Menu.ResolvedWeaponIdOf(g);
		if (weaponId == "")
			return true;

		foreach (RK29_ResolvedGroup held : m_Menu.Offer())
		{
			if (!held || held.m_sOwnerWeapon != weaponId)
				continue;

			if (held.m_eKind == RK29_EChoiceKind.EXCLUSIVE)
			{
				if (RK29_MenuRowKit.SelectableCount(held) > 1)
					return false;

				continue;
			}

			// item entries only, the same rows the detail pane stamps and GroupFullyPinned reads
			foreach (RK29_ResolvedEntry e : held.m_aEntries)
			{
				if (e && RK29_EntryItem.Cast(e.m_Def) && !RK29_MenuRowKit.IsPinnedEntry(e))
					return false;
			}
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! One "4x [render]" chip per item a counted group is issuing, in offer order; an entry the picks
	//! hold at zero is not shown. The chip's preview is layout-authored like every ItemPreviewWidget
	//! here: a procedural one carries none of the authored texture state and draws a black field.
	//! RowChipsClip is the marquee clip carrying the row's Fill width, so it is revealed too.
	protected void StampSummaryChips(notnull Widget row, notnull RK29_ResolvedGroup g)
	{
		Widget chipsClip = row.FindAnyWidget("RowChipsClip");
		if (chipsClip)
			chipsClip.SetVisible(true);

		Widget chips = row.FindAnyWidget("RowChips");
		if (!chips)
			return;

		chips.SetVisible(true);

		foreach (RK29_ResolvedEntry e : g.m_aEntries)
		{
			if (!e)
				continue;

			int count = RK29_KitResolve.PickedCount(g, e, m_Menu.Picks());
			if (count <= 0)
				continue;

			Widget chip = GetGame().GetWorkspace().CreateWidgets(SUMMARY_CHIP_LAYOUT, chips);
			if (!chip)
				continue;

			TextWidget chipCount = TextWidget.Cast(chip.FindAnyWidget("ChipCount"));
			if (chipCount)
				chipCount.SetText(count.ToString() + "x");

			RK29_MenuRowKit.StampChipPreview(chip, m_Menu.ItemEntryPrefab(g, e));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Whether the gun this weapon tile stands for is carrying magnified glass. Asked through
	//! RK29_KitResolve.IsMagnifiedEntry exactly as the optic rows are, so an exempt point reads
	//! unmagnified here too and the tile cannot say one thing while the list says another. Per
	//! weapon, because a kit can carry two scoped guns.
	protected bool WeaponWearsMagnified(notnull RK29_ResolvedGroup g)
	{
		RK29_KitSetup setup = RK29_MenuRowKit.Setup();
		if (!setup)
			return false;

		RK29_ResolvedEntry chosen = RK29_KitResolve.PickedEntry(g, m_Menu.Picks());
		if (!chosen)
			return false;

		RK29_EntryWeapon weaponEntry = RK29_EntryWeapon.Cast(chosen.m_Def);
		if (!weaponEntry)
			return false;

		foreach (RK29_ResolvedGroup point : m_Menu.Offer())
		{
			if (!point || !point.m_bIsOpticsPoint || point.m_sOwnerWeapon != weaponEntry.m_sWeapon)
				continue;

			RK29_ResolvedEntry seated = RK29_KitResolve.PickedEntry(point, m_Menu.Picks());
			if (seated && RK29_KitResolve.IsMagnifiedEntry(setup, point, seated))
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! "" for an index the middle column no longer has; same shape as DetailRow and ClassAt.
	protected string TileGroupAt(int index)
	{
		if (index < 0 || index >= m_aTileGroup.Count())
			return "";

		return m_aTileGroup[index];
	}

	//------------------------------------------------------------------------------------------------
	//! A class change swaps the whole pick set: a pick names a group of the old class's offer. The
	//! old set is parked rather than thrown away, so stepping through the classes and back costs
	//! nothing.
	void OnClassClicked(int index)
	{
		if (index == m_Menu.ClassIndex() || !m_Menu.ClassAt(index))
			return;

		m_Menu.SavePicks();

		m_Menu.SetClassIndex(index);
		m_Menu.LoadPicks();
		m_Menu.Detail().ClearFold();
		m_Menu.Detail().SetSelectedWeaponGroup("");

		// the class column moves too, which is the one thing a pick change never does
		m_Menu.RefreshAll(true);
	}

	//------------------------------------------------------------------------------------------------
	void OnTileClicked(int index)
	{
		string groupId = TileGroupAt(index);
		if (groupId == "")
			return;

		// a new panel always opens folded in both senses: both folds belonged to the panel that was
		// showing
		m_Menu.Detail().ClearInnerFold();

		// clicking a weapon moves the selection to it, which is what the panel speaks for
		RK29_ResolvedGroup g = RK29_KitResolve.FindGroup(m_Menu.Offer(), groupId);
		if (g && g.m_sOwnerWeapon == "" && g.IsWeaponGroup())
			m_Menu.Detail().SetSelectedWeaponGroup(groupId);

		if (m_Menu.Detail().OpenGroup() == groupId)
			m_Menu.Detail().SetOpenGroup("");
		else
			m_Menu.Detail().SetOpenGroup(groupId);

		BuildTiles();
		m_Menu.Detail().BuildDetail();
	}
}
