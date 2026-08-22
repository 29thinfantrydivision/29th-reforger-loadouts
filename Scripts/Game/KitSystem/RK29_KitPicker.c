//------------------------------------------------------------------------------------------------
//! Kit picker overlay, toggled by F4. Workspace overlay, not a menu.
//------------------------------------------------------------------------------------------------

class RK29_RowHandler : ScriptedWidgetEventHandler
{
	RK29_KitPicker m_Picker;
	int m_iKind;   // 0 = class, 1 = weapon, 2 = optic, 3 = apply
	int m_iIndex;

	override bool OnClick(Widget w, int x, int y, int button)
	{
		if (button != 0)
			return false;
		if (m_Picker)
			m_Picker.OnRowClicked(m_iKind, m_iIndex);
		return true;
	}
}

//------------------------------------------------------------------------------------------------
class RK29_KitPicker
{
	protected static const ResourceName ROW_LAYOUT = "{AB29C0FFEE291000}UI/KitSystem/RK29_Row.layout";
	//! weapon/optic rows: stacked big item preview above the name
	protected static const ResourceName ROW_BIG_LAYOUT = "{AB29C0FFEE291100}UI/KitSystem/RK29_RowBig.layout";
	protected static const ResourceName DIALOGS_CONF = "{AB29C0FFEE296000}Configs/UI/RK29_Dialogs.conf";

	protected static ref RK29_KitPicker s_Instance;
	protected static bool s_bLocalStash;
	protected static string s_sLocalStashKit;
	protected static ResourceName s_sLocalStashOptic;

	protected SCR_ConfigurableDialogUi m_Dialog;

	protected Widget m_wRoot;
	protected Widget m_wColClasses;
	protected Widget m_wColWeapons;
	protected Widget m_wColOptics;

	// column contents, row order (weapons index 0 = stock, optics index 0 = None)
	protected ref array<string> m_aClassNames = {};
	protected ref array<ResourceName> m_aWeaponChoices = {};
	protected ref array<ResourceName> m_aOpticChoices = {};

	protected string m_sSelectedKit;
	// per class - never one global optic across classes
	protected ref map<string, ref RK29_PlayerSelection> m_mSelections = new map<string, ref RK29_PlayerSelection>();

	// AddHandler does not own handlers
	protected ref array<ref RK29_RowHandler> m_aHandlers = {};

	//--------------------------------------------------------------------------------------------
	//! Rebuilds every world - statics survive scenario changes, the callqueue does not.
	static void Boot()
	{
		if (System.IsConsoleApp())
			return;

		s_Instance = new RK29_KitPicker();
		s_bLocalStash = false;
		s_sLocalStashKit = "";
		s_sLocalStashOptic = ResourceName.Empty;
	}

	//--------------------------------------------------------------------------------------------
	//! Gates the "Current Kit" deploy entry client-side; set via owner RPC on a saved kit.
	static bool HasLocalStash()
	{
		return s_bLocalStash;
	}

	//--------------------------------------------------------------------------------------------
	//! Stashed kit name for the deploy-menu preview body.
	static string LocalStashKit()
	{
		return s_sLocalStashKit;
	}

	//--------------------------------------------------------------------------------------------
	static ResourceName LocalStashOptic()
	{
		return s_sLocalStashOptic;
	}

	//--------------------------------------------------------------------------------------------
	static void MarkLocalStash(string kitName, ResourceName optic)
	{
		s_bLocalStash = true;
		s_sLocalStashKit = kitName;
		s_sLocalStashOptic = optic;
	}

	//--------------------------------------------------------------------------------------------
	static void ToggleFromInput()
	{
		if (s_Instance)
			s_Instance.Toggle();
	}

	//--------------------------------------------------------------------------------------------
	//! Called from the player controller and the world system. Remove-then-Insert so repeat
	//! calls and scenario changes never stack duplicates.
	static void RegisterListeners()
	{
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		im.RemoveActionListener("RK29_ToggleKitMenu", EActionTrigger.DOWN, OnToggleStatic);
		im.AddActionListener("RK29_ToggleKitMenu", EActionTrigger.DOWN, OnToggleStatic);
	}

	//--------------------------------------------------------------------------------------------
	protected static void OnToggleStatic()
	{
		ToggleFromInput();
	}

	//--------------------------------------------------------------------------------------------
	protected void Toggle()
	{
		if (m_Dialog)
		{
			CloseMenu();
			return;
		}
		Open();
	}

	//--------------------------------------------------------------------------------------------
	static void CloseMenu()
	{
		if (s_Instance && s_Instance.m_Dialog)
			s_Instance.m_Dialog.Close();
	}

	//--------------------------------------------------------------------------------------------
	protected void OnApplyConfirm(SCR_ConfigurableDialogUi dlg)
	{
		OnRowClicked(3, 0);
	}

	//--------------------------------------------------------------------------------------------
	protected void OnDialogClosed()
	{
		m_Dialog = null;
		m_wRoot = null;
		m_aHandlers.Clear();
	}

	//--------------------------------------------------------------------------------------------
	protected void BindWidgets(Widget root)
	{
		if (!root)
			return;
		m_wRoot = root;
		m_wColClasses = root.FindAnyWidget("ColClasses");
		m_wColWeapons = root.FindAnyWidget("ColWeapons");
		m_wColOptics  = root.FindAnyWidget("ColOptics");

		if (m_Dialog)
			m_Dialog.SetTitle("SELECT KIT - " + RK29_KitHud.LocalFactionKey());

		// the shell focuses its confirm button; unfocus so Space cannot trigger Apply
		// (Enter still applies via the DialogConfirm action, which is focus-independent)
		GetGame().GetWorkspace().SetFocusedWidget(null);

		RK29_KitManager mgr = RK29_KitManager.GetInstance();

		// default to the player's current kit
		if (mgr && m_sSelectedKit == "")
		{
			PlayerController localPc = GetGame().GetPlayerController();
			if (localPc)
			{
				string current = mgr.CurrentLoadoutName(localPc.GetPlayerId());
				if (mgr.m_mKits.Contains(current))
					m_sSelectedKit = current;
			}
		}

		RebuildAll();
	}

	//--------------------------------------------------------------------------------------------
	//! Local feedback for a refused open. Deliberately the popup and not the notification
	//! log: that feed is keyed on ENotification values whose text lives in
	//! Configs/Notifications/Notifications.conf and only arrives by RPC from the server,
	//! while this refusal is decided entirely on the client and never leaves the machine.
	protected void NotifyDisabled(string reason)
	{
		SCR_PopUpNotification popup = SCR_PopUpNotification.GetInstance();
		if (popup)
			popup.PopupMsg("Kit Menu currently disabled", 3, "Reason: " + reason);
	}

	//--------------------------------------------------------------------------------------------
	void Open()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return;

		if (!mgr.IsPreround())
		{
			Print("[RK29] kit menu refused - not preround", LogLevel.NORMAL);
			NotifyDisabled("kit selection is briefing only");
			return;
		}

		if (RK29_KitHud.LocalFactionKey() == "")
		{
			Print("[RK29] kit menu refused - no faction yet", LogLevel.NORMAL);
			NotifyDisabled("you have not joined a faction yet");
			return;
		}

		m_Dialog = SCR_ConfigurableDialogUi.CreateFromPreset(DIALOGS_CONF, "kitpicker");
		if (!m_Dialog)
		{
			Print("[RK29] dialog preset failed to open", LogLevel.WARNING);
			return;
		}

		m_Dialog.m_OnClose.Insert(OnDialogClosed);
		m_Dialog.m_OnConfirm.Insert(OnApplyConfirm);
		BindWidgets(m_Dialog.GetRootWidget());
	}

	// ============================================================================== building

	//--------------------------------------------------------------------------------------------
	protected void RebuildAll()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return;

		string factionKey = RK29_KitHud.LocalFactionKey();

		m_aClassNames.Clear();
		m_aHandlers.Clear();
		ClearChildren(m_wColClasses);

		SCR_GameModeEditor gm = SCR_GameModeEditor.Cast(GetGame().GetGameMode());

		// squad restriction - same filter the server enforces
		array<string> offered = {};
		PlayerController localPc = GetGame().GetPlayerController();
		if (localPc)
			mgr.GetOfferedKits(localPc.GetPlayerId(), factionKey, offered);

		if (m_sSelectedKit != "" && !offered.Contains(m_sSelectedKit))
			m_sSelectedKit = "";

		// class order follows the side config, leftovers append in loadout order
		array<string> ordered = {};
		if (mgr.m_Setup && mgr.m_Setup.m_aClasses)
		{
			foreach (RK29_ClassSetup orderCls : mgr.m_Setup.m_aClasses)
			{
				if (orderCls && !ordered.Contains(orderCls.m_sKitName))
					ordered.Insert(orderCls.m_sKitName);
			}
		}
		foreach (string loadoutKit : mgr.m_aIndexToKit)
		{
			if (loadoutKit != "" && !ordered.Contains(loadoutKit))
				ordered.Insert(loadoutKit);
		}

		foreach (string kitName : ordered)
		{
			RK29_KitStruct kit = mgr.m_mKits.Get(kitName);
			if (!kit || kit.m_sFactionKey != factionKey)
				continue;
			if (!offered.Contains(kitName))
				continue;

			// legacy kits (deploy-menu only) never show here
			RK29_ClassSetup kitCls = mgr.m_Setup.FindClass(kitName);
			if (kitCls && kitCls.m_bLegacyHidden)
				continue;

			if (m_sSelectedKit == "")
				m_sSelectedKit = kitName;

			int rowIdx = m_aClassNames.Count();
			m_aClassNames.Insert(kitName);

			string label = RK29_KitHud.ShortKitName(kitName);
			string value = "";
			if (gm)
			{
				int mag;
				value = RK29_KitHud.LabelAlive(gm, mgr, factionKey, label, mag).ToString();
			}

			Widget row = MakeRow(m_wColClasses, label, value,
				kitName == m_sSelectedKit, 0, rowIdx);
			ImageWidget icon;
			if (row)
				icon = ImageWidget.Cast(row.FindAnyWidget("RowIcon"));
			if (icon)
				RK29_KitHud.SetKitIcon(kit, icon);
		}

		RebuildWeapons();
		RebuildOptics();
	}

	//--------------------------------------------------------------------------------------------
	protected void RebuildWeapons()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		ClearChildren(m_wColWeapons);
		m_aWeaponChoices.Clear();

		RK29_KitStruct kit = mgr.m_mKits.Get(m_sSelectedKit);
		if (!kit)
			return;
		RK29_ClassSetup cls = mgr.m_Setup.FindClass(m_sSelectedKit);
		RK29_PlayerSelection sel = SelectionFor(m_sSelectedKit, cls);

		// a class with options owns the whole list: config order, first entry = default;
		// the prefab's authored primary only shows for optionless classes
		if (cls && cls.m_aWeapons && !cls.m_aWeapons.IsEmpty())
		{
			foreach (RK29_WeaponOption w : cls.m_aWeapons)
			{
				if (!w || w.m_sWeaponPrefab == ResourceName.Empty)
					continue;
				int rowIdx = m_aWeaponChoices.Count();
				m_aWeaponChoices.Insert(w.m_sWeaponPrefab);

				string label = w.m_sDisplayName;
				if (label == string.Empty)
					label = RK29_ItemNames.Get(w.m_sWeaponPrefab);

				// server normalizes "option == authored primary" to an empty delta
				bool selected = sel.m_sWeapon == w.m_sWeaponPrefab
					|| (sel.m_sWeapon == ResourceName.Empty && w.m_sWeaponPrefab == kit.m_sPrimaryWeapon);
				Widget wrow = MakeRow(m_wColWeapons, label, "", selected, 1, rowIdx);
				SetRowPreview(wrow, w.m_sWeaponPrefab);
			}
			return;
		}

		m_aWeaponChoices.Insert(ResourceName.Empty);
		Widget prow = MakeRow(m_wColWeapons, RK29_ItemNames.Get(kit.m_sPrimaryWeapon), "",
			sel.m_sWeapon == ResourceName.Empty, 1, 0);
		SetRowPreview(prow, kit.m_sPrimaryWeapon);
	}

	//--------------------------------------------------------------------------------------------
	//! Mount compatibility only decides an option that mounts. An option that swaps in a
	//! scoped weapon variant, or that brings its own adapter, answers a different question -
	//! leave those to the config that authored them.
	protected bool OpticNeedsFiltering(RK29_OpticOption opt)
	{
		if (!opt)
			return false;
		if (opt.m_sWeaponVariantPrefab != ResourceName.Empty)
			return false;
		if (opt.m_aRequiredAttachments && !opt.m_aRequiredAttachments.IsEmpty())
			return false;
		return true;
	}

	//--------------------------------------------------------------------------------------------
	//! An empty weapon delta means "the kit's authored primary" - resolve it, because mount
	//! compatibility is a question about a concrete weapon.
	protected ResourceName EffectiveWeapon(RK29_PlayerSelection sel, RK29_KitStruct kit)
	{
		if (sel && sel.m_sWeapon != ResourceName.Empty)
			return sel.m_sWeapon;
		if (kit)
			return kit.m_sPrimaryWeapon;
		return ResourceName.Empty;
	}

	//--------------------------------------------------------------------------------------------
	protected void RebuildOptics()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		ClearChildren(m_wColOptics);
		m_aOpticChoices.Clear();

		RK29_ClassSetup cls = mgr.m_Setup.FindClass(m_sSelectedKit);
		RK29_PlayerSelection sel = SelectionFor(m_sSelectedKit, cls);

		// the categories are per-faction, not per-weapon: the same Soviet magnified list
		// carries the AK dovetail scopes and the SVD's PSO. Offering a mount the chosen
		// weapon has no rail for is a dead row, so the weapon filters its own column.
		ResourceName weapon = EffectiveWeapon(sel, mgr.m_mKits.Get(m_sSelectedKit));

		m_aOpticChoices.Insert(ResourceName.Empty);
		Widget noneRow = MakeRow(m_wColOptics, "None", "", sel.m_sOptic == ResourceName.Empty, 2, 0);
		if (noneRow)
		{
			// irons: a square box the size of an optic tile, holding the empty-slot glyph
			SizeLayoutWidget noneBox = SizeLayoutWidget.Cast(noneRow.FindAnyWidget("RowPreviewSize"));
			if (noneBox)
			{
				noneBox.SetWidthOverride(96);
				noneBox.SetHeightOverride(96);
			}
			Widget noneIcon = noneRow.FindAnyWidget("RowNoneIcon");
			if (noneIcon)
				noneIcon.SetVisible(true);
		}

		if (!cls)
			return;

		if (cls.m_aOpticCategories)
		{
			foreach (string catName : cls.m_aOpticCategories)
			{
				RK29_OpticCategory cat = mgr.m_Setup.FindCategory(catName);
				if (!cat || !cat.m_aOptics)
					continue;

				foreach (RK29_OpticOption opt : cat.m_aOptics)
				{
					if (!opt || opt.m_sOpticPrefab == ResourceName.Empty || m_aOpticChoices.Contains(opt.m_sOpticPrefab))
						continue;
					if (cls.m_aOpticExclude && cls.m_aOpticExclude.Contains(opt.m_sOpticPrefab))
						continue;
					if (OpticNeedsFiltering(opt) && RK29_KitCompose.WeaponRejectsAttachment(weapon, opt.m_sOpticPrefab))
						continue;
					AddOpticRow(opt, cat.m_bMagnified, sel);
				}
			}
		}

		if (cls.m_aOpticInclude)
		{
			foreach (ResourceName inc : cls.m_aOpticInclude)
			{
				if (inc == ResourceName.Empty || m_aOpticChoices.Contains(inc))
					continue;
				RK29_OpticOption opt = mgr.m_Setup.FindOpticOptionAnywhere(inc);
				if (!opt)
					continue;
				if (OpticNeedsFiltering(opt) && RK29_KitCompose.WeaponRejectsAttachment(weapon, inc))
					continue;
				RK29_OpticCategory home = mgr.m_Setup.CategoryOf(inc);
				AddOpticRow(opt, home && home.m_bMagnified, sel);
			}
		}
	}

	//--------------------------------------------------------------------------------------------
	//! Same live 3D render the inventory uses for item icons. Manager is world-level and
	//! spawned locally on demand (vanilla SCR_InventoryMenuUI idiom).
	protected static ItemPreviewManagerEntity GetItemPreviewManager()
	{
		ChimeraWorld world = GetGame().GetWorld();
		if (!world)
			return null;

		ItemPreviewManagerEntity manager = world.GetItemPreviewManager();
		if (!manager)
		{
			Resource rsc = Resource.Load("{9F18C476AB860F3B}Prefabs/World/Game/ItemPreviewManager.et");
			if (rsc.IsValid())
				GetGame().SpawnEntityPrefabLocal(rsc, world);
			manager = world.GetItemPreviewManager();
		}
		return manager;
	}

	protected static ref map<ResourceName, int> s_mSlotSizeCache = new map<ResourceName, int>();

	//--------------------------------------------------------------------------------------------
	//! The item's authored inventory footprint (SCR_ItemAttributeCollection m_Size) - the
	//! same data the vanilla inventory sizes its slot widgets from, so previews keep the
	//! aspect the item's PreviewRenderAttributes camera was tuned for.
	protected static int SlotSizeOf(ResourceName prefab)
	{
		int size;
		if (s_mSlotSizeCache.Find(prefab, size))
			return size;

		size = ESlotSize.SLOT_2x1; // the attribute's default
		Resource res = Resource.Load(prefab);
		if (res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			if (src)
			{
				for (int i = 0, n = src.GetComponentCount(); i < n; i++)
				{
					IEntityComponentSource comp = src.GetComponent(i);
					if (!comp)
						continue;
					BaseContainer attributes = comp.GetObject("Attributes");
					if (!attributes)
						continue;
					int authored;
					if (attributes.Get("m_Size", authored) && authored > 0)
						size = authored;
					break;
				}
			}
		}
		s_mSlotSizeCache.Set(prefab, size);
		return size;
	}

	//--------------------------------------------------------------------------------------------
	protected static void SetRowPreview(Widget row, ResourceName prefab)
	{
		if (!row || prefab == ResourceName.Empty)
			return;
		ItemPreviewWidget preview = ItemPreviewWidget.Cast(row.FindAnyWidget("RowPreview"));
		if (!preview)
			return;
		ItemPreviewManagerEntity manager = GetItemPreviewManager();
		if (!manager)
			return;

		// aspect from the item's inventory footprint: square for 1x1/2x2/3x3, 2:1 for 2x1
		SizeLayoutWidget sizeBox = SizeLayoutWidget.Cast(row.FindAnyWidget("RowPreviewSize"));
		if (sizeBox)
		{
			int cellsX = 1;
			int cellsY = 1;
			switch (SlotSizeOf(prefab))
			{
				case ESlotSize.SLOT_2x1: { cellsX = 2; cellsY = 1; } break;
				case ESlotSize.SLOT_2x2: { cellsX = 2; cellsY = 2; } break;
				case ESlotSize.SLOT_3x3: { cellsX = 3; cellsY = 3; } break;
			}
			const float previewHeight = 96;
			sizeBox.SetHeightOverride(previewHeight);
			sizeBox.SetWidthOverride(previewHeight * cellsX / cellsY);
		}

		manager.SetPreviewItemFromPrefab(preview, prefab);
		preview.SetVisible(true);
	}

	//--------------------------------------------------------------------------------------------
	//! Magnified optics carry the optics glyph; 1x rows stay unmarked.
	protected void AddOpticRow(notnull RK29_OpticOption opt, bool magnified, RK29_PlayerSelection sel)
	{
		int rowIdx = m_aOpticChoices.Count();
		m_aOpticChoices.Insert(opt.m_sOpticPrefab);

		string label = opt.m_sDisplayName;
		if (label == string.Empty)
			label = RK29_ItemNames.Get(opt.m_sOpticPrefab);

		Widget row = MakeRow(m_wColOptics, label, "", sel.m_sOptic == opt.m_sOpticPrefab, 2, rowIdx);
		SetRowPreview(row, opt.m_sOpticPrefab);
		if (row && magnified)
		{
			Widget magIcon = row.FindAnyWidget("RowMagIcon");
			if (magIcon)
				magIcon.SetVisible(true);
		}
	}

	// ============================================================================== interaction

	//--------------------------------------------------------------------------------------------
	void OnRowClicked(int kind, int index)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return;
		RK29_ClassSetup cls = mgr.m_Setup.FindClass(m_sSelectedKit);
		RK29_PlayerSelection sel = SelectionFor(m_sSelectedKit, cls);

		switch (kind)
		{
			case 0: // class
			{
				if (index >= 0 && index < m_aClassNames.Count())
				{
					m_sSelectedKit = m_aClassNames[index];
					RebuildAll();
				}
				break;
			}
			case 1: // weapon - optic reseeds: keep -> default -> None
			{
				if (index >= 0 && index < m_aWeaponChoices.Count())
				{
					sel.m_sWeapon = m_aWeaponChoices[index];
					ResourceName newWeapon = EffectiveWeapon(sel, mgr.m_mKits.Get(m_sSelectedKit));
					if (sel.m_sOptic != ResourceName.Empty
						&& (!mgr.m_Setup.IsOpticAllowed(cls, sel.m_sOptic)
							|| RK29_KitCompose.WeaponRejectsAttachment(newWeapon, sel.m_sOptic)))
					{
						sel.m_sOptic = SeedOptic(cls, m_sSelectedKit);
						if (RK29_KitCompose.WeaponRejectsAttachment(newWeapon, sel.m_sOptic))
							sel.m_sOptic = ResourceName.Empty;
					}
					RebuildWeapons();
					RebuildOptics();
				}
				break;
			}
			case 2: // optic
			{
				if (index >= 0 && index < m_aOpticChoices.Count())
				{
					sel.m_sOptic = m_aOpticChoices[index];
					RebuildOptics();
				}
				break;
			}
			case 3: // apply
			{
				SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
				if (pc)
					pc.RK29_RequestKit(m_sSelectedKit, sel.m_sWeapon, sel.m_sOptic);
				CloseMenu();
				break;
			}
		}
	}

	//--------------------------------------------------------------------------------------------
	//! First use of a class: weapon = stock, optic = class default.
	protected RK29_PlayerSelection SelectionFor(string kitName, RK29_ClassSetup cls)
	{
		RK29_PlayerSelection sel = m_mSelections.Get(kitName);
		if (sel)
			return sel;

		sel = new RK29_PlayerSelection();
		sel.m_sKitName = kitName;
		sel.m_sWeapon  = SeedWeapon(cls);
		sel.m_sOptic   = SeedOptic(cls, kitName);
		m_mSelections.Set(kitName, sel);
		return sel;
	}

	//--------------------------------------------------------------------------------------------
	//! First config option is the default; optionless classes ride the authored primary.
	protected ResourceName SeedWeapon(RK29_ClassSetup cls)
	{
		if (cls && cls.m_aWeapons)
		{
			foreach (RK29_WeaponOption w : cls.m_aWeapons)
			{
				if (w && w.m_sWeaponPrefab != ResourceName.Empty)
					return w.m_sWeaponPrefab;
			}
		}
		return ResourceName.Empty;
	}

	//--------------------------------------------------------------------------------------------
	//! The class the live body is wearing seeds from the body (what you have stays
	//! selected until you change it); everything else seeds the config default.
	protected ResourceName SeedOptic(RK29_ClassSetup cls, string kitName)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();

		if (kitName != "" && kitName == LocalBodyKit())
		{
			ResourceName mounted;
			if (LocalBodyOptic(mounted))
			{
				if (mounted == ResourceName.Empty || mgr.m_Setup.IsOpticAllowed(cls, mounted))
					return mounted;
			}
		}

		if (cls && cls.m_sDefaultOptic != ResourceName.Empty && mgr.m_Setup.IsOpticAllowed(cls, cls.m_sDefaultOptic))
			return cls.m_sDefaultOptic;
		return ResourceName.Empty;
	}

	//--------------------------------------------------------------------------------------------
	protected string LocalBodyKit()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		PlayerController pc = GetGame().GetPlayerController();
		if (!mgr || !pc)
			return "";
		string name = mgr.CurrentLoadoutName(pc.GetPlayerId());
		if (mgr.IsCurrentKitLoadoutName(name))
			return s_sLocalStashKit;
		return name;
	}

	//--------------------------------------------------------------------------------------------
	//! False = no live body to read; true with empty optic = irons on the body.
	protected bool LocalBodyOptic(out ResourceName optic)
	{
		optic = ResourceName.Empty;
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return false;
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(pc.GetControlledEntity());
		if (!character || !character.GetCharacterController() || character.GetCharacterController().IsDead())
			return false;
		BaseWeaponManagerComponent wm = character.GetCharacterController().GetWeaponManagerComponent();
		if (!wm)
			return false;

		array<WeaponSlotComponent> slots = {};
		wm.GetWeaponsSlots(slots);
		foreach (WeaponSlotComponent slot : slots)
		{
			if (!slot || slot.GetWeaponSlotIndex() != 0)
				continue;
			IEntity weapon = slot.GetWeaponEntity();
			if (!weapon)
				return false;
			IEntity mounted = RK29_KitApply.FindMountedOpticRecursive(weapon);
			if (mounted)
			{
				EntityPrefabData epd = mounted.GetPrefabData();
				if (epd)
					optic = epd.GetPrefabName();
			}
			return true;
		}
		return false;
	}

	// ============================================================================== widgets

	//--------------------------------------------------------------------------------------------
	protected Widget MakeRow(Widget parent, string label, string value, bool selected, int kind, int index, bool clickable = true)
	{
		if (!parent)
			return null;
		ResourceName rowLayout = ROW_LAYOUT;
		if (kind == 1 || kind == 2)
			rowLayout = ROW_BIG_LAYOUT;
		Widget row = GetGame().GetWorkspace().CreateWidgets(rowLayout, parent);
		if (!row)
			return null;

		TextWidget name = TextWidget.Cast(row.FindAnyWidget("RowName"));
		if (name)
			name.SetText(label);

		TextWidget val = TextWidget.Cast(row.FindAnyWidget("RowValue"));
		if (val)
			val.SetText(value);

		Widget bg = row.FindAnyWidget("RowBg");
		if (bg)
			bg.SetVisible(selected);

		ImageWidget icon = ImageWidget.Cast(row.FindAnyWidget("RowIcon"));
		if (icon)
			icon.SetVisible(false); // class rows re-enable it

		if (clickable)
		{
			Widget button = row.FindAnyWidget("RowButton");
			if (button)
				HookRow(button, kind, index);
		}
		return row;
	}

	//--------------------------------------------------------------------------------------------
	protected void HookRow(Widget button, int kind, int index)
	{
		RK29_RowHandler handler = new RK29_RowHandler();
		handler.m_Picker = this;
		handler.m_iKind  = kind;
		handler.m_iIndex = index;
		button.AddHandler(handler);
		m_aHandlers.Insert(handler);
	}

	//--------------------------------------------------------------------------------------------
	protected void ClearChildren(Widget parent)
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

}
