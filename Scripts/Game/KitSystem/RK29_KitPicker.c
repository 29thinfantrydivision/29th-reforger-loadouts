//------------------------------------------------------------------------------------------------
//! Kit picker overlay, toggled by F4 (or /kitmenu). Workspace overlay, not a menu.
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
	protected static const ResourceName DIALOGS_CONF = "{AB29C0FFEE296000}Configs/UI/RK29_Dialogs.conf";

	protected static ref RK29_KitPicker s_Instance;
	protected static bool s_bLocalStash;

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
	}

	//--------------------------------------------------------------------------------------------
	//! Gates the "Current Kit" deploy entry client-side; set via owner RPC on a saved kit.
	static bool HasLocalStash()
	{
		return s_bLocalStash;
	}

	//--------------------------------------------------------------------------------------------
	static void MarkLocalStash()
	{
		s_bLocalStash = true;
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
	//! Deferred so the closing chat box does not eat the first click.
	static void ToggleFromChat()
	{
		if (!s_Instance)
			return;
		Print("[RK29] toggle via CHAT", LogLevel.NORMAL);
		GetGame().GetCallqueue().CallLater(s_Instance.Toggle, 100, false);
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
			return;
		}

		if (RK29_KitHud.LocalFactionKey() == "")
		{
			Print("[RK29] kit menu refused - no faction yet", LogLevel.NORMAL);
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

		// row 0 = stock (empty ResourceName on the wire)
		m_aWeaponChoices.Insert(ResourceName.Empty);
		MakeRow(m_wColWeapons, RK29_ItemNames.Get(kit.m_sPrimaryWeapon) + " (stock)", "",
			sel.m_sWeapon == ResourceName.Empty, 1, 0);

		if (cls && cls.m_aWeapons)
		{
			foreach (RK29_WeaponOption w : cls.m_aWeapons)
			{
				if (!w || w.m_sWeaponPrefab == ResourceName.Empty || w.m_sWeaponPrefab == kit.m_sPrimaryWeapon)
					continue;
				int rowIdx = m_aWeaponChoices.Count();
				m_aWeaponChoices.Insert(w.m_sWeaponPrefab);

				string label = w.m_sDisplayName;
				if (label == string.Empty)
					label = RK29_ItemNames.Get(w.m_sWeaponPrefab);

				MakeRow(m_wColWeapons, label, "", sel.m_sWeapon == w.m_sWeaponPrefab, 1, rowIdx);
			}
		}
	}

	//--------------------------------------------------------------------------------------------
	protected void RebuildOptics()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		ClearChildren(m_wColOptics);
		m_aOpticChoices.Clear();

		RK29_ClassSetup cls = mgr.m_Setup.FindClass(m_sSelectedKit);
		RK29_PlayerSelection sel = SelectionFor(m_sSelectedKit, cls);

		m_aOpticChoices.Insert(ResourceName.Empty);
		MakeRow(m_wColOptics, "None", "", sel.m_sOptic == ResourceName.Empty, 2, 0);

		if (!cls || !cls.m_aOpticCategories)
			return;

		foreach (string catName : cls.m_aOpticCategories)
		{
			RK29_OpticCategory cat = mgr.m_Setup.FindCategory(catName);
			if (!cat || !cat.m_aOptics)
				continue;

			string badge = "1X";
			if (cat.m_bMagnified)
				badge = "MAG";

			foreach (RK29_OpticOption opt : cat.m_aOptics)
			{
				if (!opt || opt.m_sOpticPrefab == ResourceName.Empty || m_aOpticChoices.Contains(opt.m_sOpticPrefab))
					continue;
				int rowIdx = m_aOpticChoices.Count();
				m_aOpticChoices.Insert(opt.m_sOpticPrefab);

				string label = opt.m_sDisplayName;
				if (label == string.Empty)
					label = RK29_ItemNames.Get(opt.m_sOpticPrefab);

				MakeRow(m_wColOptics, label, badge, sel.m_sOptic == opt.m_sOpticPrefab, 2, rowIdx);
			}
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
					if (sel.m_sOptic != ResourceName.Empty && !mgr.m_Setup.IsOpticAllowed(cls, sel.m_sOptic))
						sel.m_sOptic = SeedOptic(cls);
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
		sel.m_sWeapon  = ResourceName.Empty;
		sel.m_sOptic   = SeedOptic(cls);
		m_mSelections.Set(kitName, sel);
		return sel;
	}

	//--------------------------------------------------------------------------------------------
	protected ResourceName SeedOptic(RK29_ClassSetup cls)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (cls && cls.m_sDefaultOptic != ResourceName.Empty && mgr.m_Setup.IsOpticAllowed(cls, cls.m_sDefaultOptic))
			return cls.m_sDefaultOptic;
		return ResourceName.Empty;
	}

	// ============================================================================== widgets

	//--------------------------------------------------------------------------------------------
	protected Widget MakeRow(Widget parent, string label, string value, bool selected, int kind, int index, bool clickable = true)
	{
		if (!parent)
			return null;
		Widget row = GetGame().GetWorkspace().CreateWidgets(ROW_LAYOUT, parent);
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
