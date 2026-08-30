//------------------------------------------------------------------------------------------------
//! Deploy-menu row identity for "Current Kit" - icon AND label - and the list rebuild that
//! makes the row exist.
//!
//! Vanilla builds a loadout row's icon straight from its resource prefab: load the resource,
//! find SCR_EditableCharacterComponent, read m_UIInfo. That bypasses the kit system entirely -
//! and Current Kit's resource is the stashed kit's BODY, which for a picker-only class is the
//! side's shared body. A stashed medic therefore drew the rifleman's icon. The label has the
//! same problem from the other direction: it is the loadout's name, so every kit in the game
//! reads "Current Kit".
//!
//! The kit manager already knows which kit this row would spawn - the stash, or the side
//! default for a player who has not picked yet - and the composed kit carries the config
//! identity, so hand that over for both. Every other loadout row is left to vanilla: its
//! resource IS its kit, so reading the prefab is correct there.
//------------------------------------------------------------------------------------------------

//! The row icon read by the loadout list and the expand button, and the list rebuild that puts
//! a newly-available row in front of the player without waiting for the menu to be reopened.
modded class SCR_LoadoutRequestUIComponent
{
	//--------------------------------------------------------------------------------------------
	override SCR_EditableEntityUIInfo GetUIInfo(SCR_BasePlayerLoadout loadout)
	{
		SCR_EditableEntityUIInfo stashed = RK29_StashedLoadoutUIInfo.Resolve(loadout);
		if (stashed)
			return stashed;
		return super.GetUIInfo(loadout);
	}

	//--------------------------------------------------------------------------------------------
	//! Row label, stamped over vanilla's after the fact.
	//!
	//! Vanilla writes loadout.GetLoadoutName() into three widgets from four different methods,
	//! and GetLoadoutName() cannot be the hook: it is the loadout's IDENTITY - the key the kit
	//! manager, the injector and the ownership cache all match on - and it is shared by every
	//! player on the machine, while the kit behind a Current Kit row is per player and per
	//! side. So the name stays honest and the DISPLAY is corrected here, after super has run.
	//! Overriding the setters rather than reimplementing them keeps this from rotting when
	//! vanilla changes what else those methods do.
	protected void RK29_StampRowName(SCR_BasePlayerLoadout loadout)
	{
		string label = RK29_StashedLoadoutUIInfo.ResolveName(loadout);
		if (label == "")
			return;

		if (m_wLoadoutName)
			m_wLoadoutName.SetText(label);
		if (m_wLoadoutNameText)
			m_wLoadoutNameText.SetText(label);
		if (m_wExpandButtonName)
			m_wExpandButtonName.SetText(label);
	}

	//--------------------------------------------------------------------------------------------
	override void HandlerAttached(Widget w)
	{
		super.HandlerAttached(w);

		// vanilla's HandlerAttached bails out (with a log) before assigning m_PlyLoadoutComp
		// when the world has no loadout manager or no player controller yet, and
		// GetPlayerLoadout() derefs it unguarded - match vanilla's degradation, don't crash
		if (!m_PlyLoadoutComp)
			return;

		RK29_StampRowName(GetPlayerLoadout());
	}

	//--------------------------------------------------------------------------------------------
	override protected void RequestPlayerLoadout(SCR_BasePlayerLoadout loadout)
	{
		super.RequestPlayerLoadout(loadout);
		RK29_StampRowName(loadout);
	}

	//--------------------------------------------------------------------------------------------
	//! Mirrors vanilla's own two gates. Without them a hover would relabel the row on mouse and
	//! keyboard, where vanilla deliberately leaves the preview - and its name - alone.
	override protected void SetLoadoutPreview(SCR_BasePlayerLoadout loadout)
	{
		super.SetLoadoutPreview(loadout);

		InputManager inputManager = GetGame().GetInputManager();
		if (inputManager && inputManager.GetLastUsedInputDevice() == EInputDeviceType.KEYBOARD)
			return;

		if (!m_PreviewComp || !loadout)
			return;

		RK29_StampRowName(loadout);
	}

	//--------------------------------------------------------------------------------------------
	override void RefreshLoadoutPreview()
	{
		super.RefreshLoadoutPreview();

		if (!m_PlyLoadoutComp)
			return;

		SCR_BasePlayerLoadout loadout = m_PlyLoadoutComp.GetLoadout();
		RK29_StampRowName(loadout);

		// SetLoadoutPreview() is ALSO the gallery's hover handler, so it early-returns while the
		// last input was the keyboard - that guard is there to stop keyboard navigation from
		// re-previewing on every hover. RefreshLoadoutPreview() reuses the same method for an
		// EXPLICIT refresh and inherits the guard with it, so confirming a kit from the keyboard
		// updated the row icon and the label but left the mannequin wearing the previous kit.
		// Redo the preview super skipped; on mouse or gamepad it already ran and we do nothing.
		InputManager inputManager = GetGame().GetInputManager();
		if (!inputManager || inputManager.GetLastUsedInputDevice() != EInputDeviceType.KEYBOARD)
			return;

		if (!m_PreviewComp || !loadout)
			return;

		m_PreviewedEntity = m_PreviewComp.SetPreviewedLoadout(loadout);
		if (m_wLoadoutPreview)
			m_wLoadoutPreview.SetVisible(true);
	}

	//--------------------------------------------------------------------------------------------
	//! Rebuild the loadout gallery against IsLoadoutAvailableClient() as it reads NOW.
	//!
	//! Vanilla runs ShowAvailableLoadouts() on menu open, faction change and group change, and
	//! nowhere else - so a loadout that BECOMES available while the menu is up never gets a row.
	//! That is Current Kit's entire life cycle: a player who dies with no stash opens the deploy
	//! menu, applies a kit in the picker, and the row it just earned is never created. The server
	//! has the stash by then, so deploying without touching anything still spawns the kit - the
	//! entry simply is not there to click. A row that already exists is just as stale: its icon
	//! is read once, in SCR_LoadoutButton.SetLoadout(), so re-applying a different kit leaves the
	//! old class icon on the row.
	//!
	//! Deliberately NOT a call to ShowAvailableLoadouts(). Its tail re-requests a loadout, and
	//! prefers the arsenal entry over whatever is currently assigned - running that here would
	//! take the kit the player just applied straight back off them. Only the list is rebuilt;
	//! the selection stays where the server put it.
	void RK29_RefreshLoadoutList()
	{
		if (!m_LoadoutManager || !m_LoadoutSelector || !m_PlyFactionAffilComp || !m_PlyLoadoutComp)
			return;

		Faction faction = m_PlyFactionAffilComp.GetAffiliatedFaction();
		if (!faction)
			return;

		// same source split vanilla uses: group-scoped roles when the faction configures them
		SCR_AIGroup group;
		if (m_PlayerControllerGroupComponent)
			group = m_PlayerControllerGroupComponent.GetPlayersGroup();

		SCR_Faction scrFaction = SCR_Faction.Cast(faction);
		array<ref SCR_BasePlayerLoadout> loadouts = {};
		if (group && scrFaction && scrFaction.IsGroupRolesConfigured())
			m_LoadoutManager.GetPlayerLoadoutsByGroup(group, faction, loadouts);
		else
			m_LoadoutManager.GetPlayerLoadoutsByFaction(faction, loadouts);

		m_LoadoutSelector.ClearAll();
		foreach (SCR_BasePlayerLoadout loadout : loadouts)
		{
			if (loadout && loadout.IsLoadoutAvailableClient())
				m_LoadoutSelector.AddItem(loadout, true);
		}

		// ClearAll took the highlight with the button that carried it
		SCR_BasePlayerLoadout assigned = m_PlyLoadoutComp.GetLoadout();
		if (assigned)
			m_LoadoutSelector.SetSelected(assigned);
	}
}

//! The per-row button, which resolves its own icon off the loadout it was given.
modded class SCR_LoadoutButton
{
	//--------------------------------------------------------------------------------------------
	override SCR_EditableEntityUIInfo GetUIInfo()
	{
		SCR_EditableEntityUIInfo stashed = RK29_StashedLoadoutUIInfo.Resolve(GetLoadout());
		if (stashed)
			return stashed;
		return super.GetUIInfo();
	}
}

//------------------------------------------------------------------------------------------------
class RK29_StashedLoadoutUIInfo
{
	//--------------------------------------------------------------------------------------------
	//! Config identity of the kit this row would actually spawn, or null for anything that is
	//! not Current Kit - which leaves vanilla's prefab read in charge of every ordinary row.
	static SCR_EditableEntityUIInfo Resolve(SCR_BasePlayerLoadout loadout)
	{
		RK29_KitStruct kit = ResolveKit(loadout);
		if (!kit)
			return null;

		SCR_EditableEntityUIInfo info = SCR_EditableEntityUIInfo.Cast(kit.m_UIInfo);
		if (!info)
			return null;

		// SCR_LoadoutButton.SetLoadout() does entityUIInfo.GetFaction().GetFactionColor() with
		// no null check, so a UIInfo whose m_sFaction is unset or unknown crashes the deploy
		// menu outright - "NULL pointer to instance" the moment a row is built. Refusing to
		// hand ours over sends vanilla back to reading the prefab instead of taking the menu
		// down, so one missing key in a kit conf costs an icon, not the round.
		if (!info.GetFaction())
		{
			Print("[RK29] kit '" + kit.m_sKitName + "' has a UIInfo with no usable m_sFaction -"
				+ " falling back to the prefab icon. Set m_sFaction in its composition",
				LogLevel.WARNING);
			return null;
		}

		return info;
	}

	//--------------------------------------------------------------------------------------------
	//! The kit behind a Current Kit row: the stash, or the side default a player who has never
	//! opened the picker will be spawned with. Resolved through the manager rather than off the
	//! local stash directly, so an untouched row shows Rifleman instead of an empty identity.
	static RK29_KitStruct ResolveKit(SCR_BasePlayerLoadout loadout)
	{
		RK29_CurrentKitLoadout currentKit = RK29_CurrentKitLoadout.Cast(loadout);
		if (!currentKit)
			return null;

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		PlayerController pc = GetGame().GetPlayerController();
		if (!mgr || !pc)
			return null;

		string kitName = mgr.EffectiveKitFor(pc.GetPlayerId(), currentKit.GetFactionKey());
		if (kitName == "")
			return null;

		return mgr.m_mKits.Get(kitName);
	}

	//--------------------------------------------------------------------------------------------
	//! Sight the server would seed this kit with - the class default, screened by the same
	//! allow-list the picker uses so config can never mount a sight the kit forbids.
	protected static ResourceName DefaultOpticFor(string kitName)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr || !mgr.m_Setup)
			return ResourceName.Empty;

		RK29_ClassSetup cls = mgr.m_Setup.FindClass(kitName);
		if (!cls || cls.m_sDefaultOptic == ResourceName.Empty)
			return ResourceName.Empty;

		if (!mgr.m_Setup.IsOpticAllowed(cls, cls.m_sDefaultOptic))
			return ResourceName.Empty;

		return cls.m_sDefaultOptic;
	}

	//--------------------------------------------------------------------------------------------
	//! Dress and weapons for a Current Kit mannequin.
	//!
	//! The server's resolved wire wins whenever it matches the kit the row resolved to: it
	//! carries the weapon OPTION the player actually picked, which the catalog's default-weapon
	//! copy does not. Otherwise - before a first pick, or when the row has fallen back off a
	//! stash the player can no longer spawn - the composed kit is read straight out of the local
	//! catalog, the same struct the server will dress the body from. outOptic follows the same
	//! source, so the sight on the mannequin's rifle can never come from a kit the rest of the
	//! preview is no longer showing.
	static bool ResolvePreviewLoadout(SCR_BasePlayerLoadout loadout,
		notnull map<string, ResourceName> outDress, notnull map<int, ResourceName> outWeapons,
		out ResourceName outOptic)
	{
		outDress.Clear();
		outWeapons.Clear();
		outOptic = ResourceName.Empty;

		if (!RK29_CurrentKitLoadout.Cast(loadout))
			return false;

		RK29_KitStruct kit = ResolveKit(loadout);
		if (!kit)
			return false;

		// ...but only while the wire still DESCRIBES the kit this row resolved to. The stash is
		// not cleared when the player changes side or squad, so a stale one out-votes a row that
		// has correctly fallen back to the new side's default: the label reads Rifleman and the
		// mannequin stands there in the faction the player just left.
		if (RK29_KitPicker.HasLocalStash() && RK29_KitPicker.LocalStashKit() == kit.m_sKitName)
		{
			foreach (string stashSlot, ResourceName stashGarment : RK29_KitPicker.LocalStashDress())
				outDress.Set(stashSlot, stashGarment);
			foreach (int stashIdx, ResourceName stashWeapon : RK29_KitPicker.LocalStashWeapons())
				outWeapons.Set(stashIdx, stashWeapon);
			outOptic = RK29_KitPicker.LocalStashOptic();
			return true;
		}

		// The optic travels with the stash, so falling off one has to drop it too. The mannequin
		// does not merely paint the wrong sight otherwise: the swap DELETES whatever the weapon
		// was authored with before mounting, and a sight from the side the player just left will
		// not mount on this side's rifle - so the stale value leaves the preview on irons.
		// The seed the server is about to run picks the class default, so preview that instead.
		outOptic = DefaultOpticFor(kit.m_sKitName);

		foreach (string slot, ResourceName garment : kit.m_mClothing)
		{
			if (garment != ResourceName.Empty)
				outDress.Set(slot, garment);
		}

		// grenades live in the weapon map under a sentinel index and never reach a mannequin,
		// same exclusion RK29_KitWire.Pack() makes
		foreach (int idx, ResourceName weapon : kit.m_mWeapons)
		{
			if (weapon != ResourceName.Empty && idx != RK29_KitStruct.GRENADE_SLOT)
				outWeapons.Set(idx, weapon);
		}

		return true;
	}

	//--------------------------------------------------------------------------------------------
	//! Row label for a Current Kit row - the kit's own short name ("Automatic Rifleman"), not the
	//! literal loadout name. Empty for every other row, which keeps its own name.
	static string ResolveName(SCR_BasePlayerLoadout loadout)
	{
		RK29_KitStruct kit = ResolveKit(loadout);
		if (!kit)
			return "";

		return RK29_KitHud.ShortKitName(kit.m_sKitName);
	}
}
