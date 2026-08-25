//------------------------------------------------------------------------------------------------
//! Kit system core. Runs on every machine; server-only parts no-op elsewhere.
//! TODO(RK29): replace the 2s recompute tick with game-mode invokers once 1.8 signatures checked.
//------------------------------------------------------------------------------------------------

class RK29_PlayerSelection
{
	string m_sKitName;
	ResourceName m_sWeapon; // empty = authored weapon
	ResourceName m_sOptic;  // empty = None/irons
	string m_sIdentityUid;  // persistent player identity, for reconnect parking
}

//------------------------------------------------------------------------------------------------
class RK29_KitManager
{
	protected static const int RECOMPUTE_MS = 2000;
	protected static const int STAMP_MS     = 1000;

	protected static ref RK29_KitManager s_Instance;

	static const ResourceName SETUP_CONF = "{AB29C0FFEE290001}Configs/KitSystem/RK29_KitSetup.conf";

	ref RK29_KitSetup m_Setup;

	ref map<string, ref RK29_KitStruct> m_mKits = new map<string, ref RK29_KitStruct>();

	//! Each kit's weapon options, taken from its composition at boot. Kit-scoped rather than
	//! class-scoped so everything a kit carries is declared in the kit's own file.
	ref map<string, ref array<ref RK29_WeaponSlot>> m_mKitOptions = new map<string, ref array<ref RK29_WeaponSlot>>();

	//! The composition WITHOUT any weapon option applied. A weapon choice is always laid over
	//! this, never over an already-optioned kit, so re-picking a weapon cannot stack a second
	//! copy of that weapon's blocks (its grenade set would double).
	ref map<string, ref RK29_KitStruct> m_mKitsBase = new map<string, ref RK29_KitStruct>();

	//! The raw prefab capture, kept beside the composed kit so /kitdump can export prefab
	//! truth. Re-deriving it later by loadout name resolves to the wrong loadout object.
	ref map<string, ref RK29_KitStruct> m_mCaptured = new map<string, ref RK29_KitStruct>();

	//! Last group identity that fell through to the default squad list - so the warning is
	//! printed once per group rather than on every picker rebuild. Keyed on name+role, since an
	//! unnamed group is the common case and every one of those would otherwise share a key.
	protected string m_sLastUnmatchedGroup;

	//! Same one-shot-per-subject throttle for the "default kit not offered here" note.
	protected string m_sLastDefaultlessFaction;

	//! Same index space as SCR_LoadoutManager's list; the count arrays use it too.
	ref array<string> m_aIndexToKit = {};

	// server only
	protected ref map<int, ref RK29_PlayerSelection> m_mSelections = new map<int, ref RK29_PlayerSelection>();

	//! stashes of disconnected players, keyed by identity uid - survives rejoin-on-body
	protected ref map<string, ref RK29_PlayerSelection> m_mParkedSelections = new map<string, ref RK29_PlayerSelection>();

	ref RK29_RoundTimerProbe m_Probe = new RK29_RoundTimerProbe();

	//--------------------------------------------------------------------------------------------
	static RK29_KitManager GetInstance()
	{
		return s_Instance;
	}

	//--------------------------------------------------------------------------------------------
	//! Called from GM29_KitLoadouts after injection, on every machine. Rebuilds every world -
	//! statics survive scenario changes but the callqueue does not.
	static void Boot(notnull array<ref SCR_BasePlayerLoadout> loadouts)
	{
		s_Instance = new RK29_KitManager();
		s_Instance.Init(loadouts);
	}

	//--------------------------------------------------------------------------------------------
	protected void Init(array<ref SCR_BasePlayerLoadout> loadouts)
	{
		LoadSetup();

		// placement plans are solved against kit definitions and rigs - both rebuild here
		RK29_KitApply.ClearPlans();

		foreach (int i, SCR_BasePlayerLoadout loadout : loadouts)
		{
			m_aIndexToKit.Insert("");
			if (!loadout)
				continue;

			SCR_FactionPlayerLoadout fl = SCR_FactionPlayerLoadout.Cast(loadout);
			if (!fl)
				continue;

			string kitName = loadout.GetLoadoutName();
			if (!kitName.StartsWith("29th"))
				continue;

			RK29_KitStruct kit = BuildKit(kitName, fl.GetFactionKey(), loadout.GetLoadoutResource(), true);
			if (!kit)
				continue;

			m_mKits.Set(kitName, kit);
			m_aIndexToKit[i] = kitName;
		}

		// Pass 2: classes with no loadout entry of their own. A deploy-menu row is how a kit
		// gets SPAWNED, not what makes it exist - a class that states its own body (or takes
		// its side's) is a complete kit, reachable through the picker and counted like any
		// other. The index space just grows past the loadout list; every reader resolves
		// through m_aIndexToKit, which is built identically on server and client.
		foreach (RK29_ClassSetup cls2 : m_Setup.m_aClasses)
		{
			if (!cls2 || cls2.m_sKitName == "" || m_mKits.Contains(cls2.m_sKitName))
				continue;

			ResourceName body = cls2.BodyPrefab();
			if (body == ResourceName.Empty)
			{
				Print("[RK29] config ERROR - class '" + cls2.m_sKitName + "' has no loadout entry and"
					+ " no body prefab (set m_sBodyPrefab, or m_sBodyPrefab on its side config)", LogLevel.ERROR);
				continue;
			}

			RK29_KitStruct standalone = BuildKit(cls2.m_sKitName, cls2.m_sSideFactionKey, body, false);
			if (!standalone)
				continue;

			m_mKits.Set(cls2.m_sKitName, standalone);
			m_aIndexToKit.Insert(cls2.m_sKitName);
			Print("[RK29] kit '" + cls2.m_sKitName + "' is picker-only (no deploy entry, faction "
				+ standalone.m_sFactionKey + ")", LogLevel.NORMAL);
		}

		Print("[RK29] manager up - " + m_mKits.Count().ToString() + " kit(s) walked | server=" + Replication.IsServer().ToString(), LogLevel.NORMAL);

		GetGame().GetCallqueue().CallLater(m_Probe.Probe, 2000, false);

		if (Replication.IsServer())
			GetGame().GetCallqueue().CallLater(ServerTick, RECOMPUTE_MS, true);

		// SetInfoInstance is local, so the stamp must run on every machine.
		GetGame().GetCallqueue().CallLater(StampTick, STAMP_MS, true);

		RK29_KitHud.Boot();
		RK29_KitPicker.Boot();
	}

	//--------------------------------------------------------------------------------------------
	protected void LoadSetup()
	{
		Resource res = Resource.Load(SETUP_CONF);
		if (res.IsValid())
			m_Setup = RK29_KitSetup.Cast(BaseContainerTools.CreateInstanceFromContainer(res.GetResource().ToBaseContainer()));

		if (!m_Setup)
		{
			Print("[RK29] RK29_KitSetup.conf missing/unreadable - customization disabled, kits still counted", LogLevel.WARNING);
			m_Setup = new RK29_KitSetup();
		}

		if (!m_Setup.m_aClasses)
			m_Setup.m_aClasses = {};
		if (!m_Setup.m_aOpticCategories)
			m_Setup.m_aOpticCategories = {};
		if (!m_Setup.m_aAliases)
			m_Setup.m_aAliases = {};
		if (!m_Setup.m_aMagazineSets)
			m_Setup.m_aMagazineSets = {};
		if (!m_Setup.m_aSquads)
			m_Setup.m_aSquads = {};

		// merge Sides + Helpers configs into the runtime view
		if (m_Setup.m_aSideConfigs)
		{
			foreach (ResourceName sideRes : m_Setup.m_aSideConfigs)
			{
				Resource sres = Resource.Load(sideRes);
				RK29_SideSetup side;
				if (sres.IsValid())
					side = RK29_SideSetup.Cast(BaseContainerTools.CreateInstanceFromContainer(sres.GetResource().ToBaseContainer()));
				if (!side || !side.m_aClasses)
				{
					Print("[RK29] side config missing/unreadable: " + sideRes, LogLevel.WARNING);
					continue;
				}
				foreach (RK29_ClassSetup c : side.m_aClasses)
				{
					if (!c)
						continue;
					// the flattened list loses which file a class came from, and the second
					// boot pass needs both to build a kit that has no loadout entry
					c.m_sSideFactionKey = side.m_sFactionKey;
					c.m_sSideBodyPrefab = side.m_sBodyPrefab;
					c.m_sSideDefaultKit = side.m_sDefaultKitName;
					m_Setup.m_aClasses.Insert(c);
				}
			}
		}
		if (m_Setup.m_aOpticConfigs)
		{
			foreach (ResourceName opticRes : m_Setup.m_aOpticConfigs)
			{
				Resource ores = Resource.Load(opticRes);
				RK29_OpticLibrary lib;
				if (ores.IsValid())
					lib = RK29_OpticLibrary.Cast(BaseContainerTools.CreateInstanceFromContainer(ores.GetResource().ToBaseContainer()));
				if (!lib || !lib.m_aOpticCategories)
				{
					Print("[RK29] optic config missing/unreadable: " + opticRes, LogLevel.WARNING);
					continue;
				}
				foreach (RK29_OpticCategory cat : lib.m_aOpticCategories)
				{
					if (cat)
						m_Setup.m_aOpticCategories.Insert(cat);
				}
			}
		}

		if (m_Setup.m_aAliasConfigs)
		{
			foreach (ResourceName aliasRes : m_Setup.m_aAliasConfigs)
			{
				Resource ares = Resource.Load(aliasRes);
				RK29_ItemAliasCatalog cat;
				if (ares.IsValid())
					cat = RK29_ItemAliasCatalog.Cast(BaseContainerTools.CreateInstanceFromContainer(ares.GetResource().ToBaseContainer()));
				if (!cat || !cat.m_aAliases)
				{
					Print("[RK29] alias config missing/unreadable: " + aliasRes, LogLevel.WARNING);
					continue;
				}
				foreach (RK29_ItemAlias a : cat.m_aAliases)
				{
					if (a)
						m_Setup.m_aAliases.Insert(a);
				}
			}
		}
		if (m_Setup.m_aMagSetConfigs)
		{
			foreach (ResourceName magRes : m_Setup.m_aMagSetConfigs)
			{
				Resource mres = Resource.Load(magRes);
				RK29_MagazineSetCatalog mcat;
				if (mres.IsValid())
					mcat = RK29_MagazineSetCatalog.Cast(BaseContainerTools.CreateInstanceFromContainer(mres.GetResource().ToBaseContainer()));
				if (!mcat || !mcat.m_aMagazineSets)
				{
					Print("[RK29] magazine set config missing/unreadable: " + magRes, LogLevel.WARNING);
					continue;
				}
				foreach (RK29_MagazineSet magSet : mcat.m_aMagazineSets)
				{
					if (magSet)
						m_Setup.m_aMagazineSets.Insert(magSet);
				}
			}
		}
		if (m_Setup.m_aWeaponConfigs)
		{
			if (!m_Setup.m_aWeaponDefs)
				m_Setup.m_aWeaponDefs = {};
			foreach (ResourceName weaponRes : m_Setup.m_aWeaponConfigs)
			{
				Resource wres = Resource.Load(weaponRes);
				RK29_WeaponCatalog wcat;
				if (wres.IsValid())
					wcat = RK29_WeaponCatalog.Cast(BaseContainerTools.CreateInstanceFromContainer(wres.GetResource().ToBaseContainer()));
				if (!wcat || !wcat.m_aWeapons)
				{
					Print("[RK29] weapon config missing/unreadable: " + weaponRes, LogLevel.WARNING);
					continue;
				}
				foreach (RK29_WeaponDef def : wcat.m_aWeapons)
				{
					if (def)
						m_Setup.m_aWeaponDefs.Insert(def);
				}
			}
		}
		if (m_Setup.m_aSquadConfigs)
		{
			foreach (ResourceName squadRes : m_Setup.m_aSquadConfigs)
			{
				Resource sqres = Resource.Load(squadRes);
				RK29_SquadKitCatalog scat;
				if (sqres.IsValid())
					scat = RK29_SquadKitCatalog.Cast(BaseContainerTools.CreateInstanceFromContainer(sqres.GetResource().ToBaseContainer()));
				if (!scat || !scat.m_aSquads)
				{
					Print("[RK29] squad kit config missing/unreadable: " + squadRes, LogLevel.WARNING);
					continue;
				}
				foreach (RK29_SquadKits sq : scat.m_aSquads)
				{
					if (sq)
						m_Setup.m_aSquads.Insert(sq);
				}
			}
		}

		RK29_Log.s_bVerbose = m_Setup.m_bVerboseLogging;
		if (RK29_Log.s_bVerbose)
			Print("[RK29] verbose apply trace ENABLED - turn m_bVerboseLogging off before a live session", LogLevel.WARNING);

		int weaponCount = 0;
		if (m_Setup.m_aWeaponDefs)
			weaponCount = m_Setup.m_aWeaponDefs.Count();
		Print("[RK29] weapon catalog - " + weaponCount.ToString() + " definition(s)", LogLevel.NORMAL);

		foreach (RK29_ClassSetup stale : m_Setup.m_aClasses)
		{
			if (stale && stale.m_aWeapons && !stale.m_aWeapons.IsEmpty())
				Print("[RK29] config WARNING - class '" + stale.m_sKitName + "' still declares m_aWeapons in the roster;"
					+ " weapon options moved to the kit composition and these are ignored", LogLevel.WARNING);
		}

		Print("[RK29] setup loaded - " + m_Setup.m_aClasses.Count().ToString() + " class(es), " + m_Setup.m_aOpticCategories.Count().ToString() + " optic categorie(s), " + m_Setup.m_aAliases.Count().ToString() + " alias(es)", LogLevel.NORMAL);
	}

	//--------------------------------------------------------------------------------------------
	bool IsPreround()
	{
		m_Probe.EnsureProbed();

		bool noTimerOpen = false;
		if (m_Setup)
			noTimerOpen = m_Setup.m_bNoTimerOpen;
		return m_Probe.IsPreround(noTimerOpen);
	}

	//--------------------------------------------------------------------------------------------
	bool IsBriefing()
	{
		m_Probe.EnsureProbed();

		bool noTimerOpen = false;
		if (m_Setup)
			noTimerOpen = m_Setup.m_bNoTimerOpen;
		return m_Probe.IsBriefing(noTimerOpen);
	}

	// ======================================================================== server chokepoint

	//--------------------------------------------------------------------------------------------
	//! Single server entry for every kit request. Client input is never trusted.
	void HandleKitRequest_S(int playerId, string kitName, ResourceName weapon, ResourceName optic)
	{
		if (!Replication.IsServer())
			return;

		RK29_KitStruct kit = m_mKits.Get(kitName);
		if (!kit)
		{
			Print("[RK29] request rejected - unknown kit '" + kitName + "' (player " + playerId.ToString() + ")", LogLevel.WARNING);
			return;
		}

		Faction playerFaction = SCR_FactionManager.SGetPlayerFaction(playerId);
		if (!playerFaction || playerFaction.GetFactionKey() != kit.m_sFactionKey)
		{
			Print("[RK29] request rejected - kit faction mismatch (player " + playerId.ToString() + ")", LogLevel.WARNING);
			return;
		}
		array<string> offered = {};
		GetOfferedKits(playerId, kit.m_sFactionKey, offered);
		if (!offered.Contains(kitName))
		{
			Print("[RK29] request rejected - kit not offered to player's squad (player " + playerId.ToString() + ")", LogLevel.WARNING);
			return;
		}

		RK29_ClassSetup cls = m_Setup.FindClass(kitName);

		if (weapon != ResourceName.Empty && weapon != kit.m_sPrimaryWeapon)
		{
			if (!m_Setup.FindWeapon(m_mKitOptions.Get(kitName), weapon, kit.m_sFactionKey))
			{
				Print("[RK29] request rejected - weapon not in class list (player " + playerId.ToString() + ")", LogLevel.WARNING);
				return;
			}
		}

		if (optic != ResourceName.Empty && !m_Setup.IsOpticAllowed(cls, optic))
		{
			Print("[RK29] request rejected - optic not allowed for '" + kitName + "' (player " + playerId.ToString() + ")", LogLevel.WARNING);
			return;
		}

		// a weapon no longer routes to a different kit - the option brings its own gear,
		// ammo and item deltas, so one class covers every weapon it offers

		// the picker filters its own column, but the request is what the server acts on: a
		// stale menu or a crafted call must not mount a scope the weapon has no rail for.
		// Dropped rather than rejected - a bad optic should not cost the player their kit.
		if (optic != ResourceName.Empty)
		{
			RK29_OpticOption opticOpt = m_Setup.FindOpticOption(cls, optic);
			bool mountsDirectly = !opticOpt
				|| (opticOpt.m_sWeaponVariantPrefab == ResourceName.Empty
					&& (!opticOpt.m_aRequiredAttachments || opticOpt.m_aRequiredAttachments.IsEmpty()));

			ResourceName effectiveWeapon = weapon;
			if (effectiveWeapon == ResourceName.Empty)
				effectiveWeapon = kit.m_sPrimaryWeapon;

			if (mountsDirectly && RK29_KitCompose.WeaponRejectsAttachment(effectiveWeapon, optic))
			{
				Print("[RK29] optic dropped - " + RK29_ItemNames.Get(optic) + " does not mount on "
					+ RK29_ItemNames.Get(effectiveWeapon) + " (player " + playerId.ToString() + ")", LogLevel.WARNING);
				optic = ResourceName.Empty;
			}
		}

		RK29_PlayerSelection sel = new RK29_PlayerSelection();
		sel.m_sKitName = kitName;
		sel.m_sWeapon  = weapon;
		sel.m_sOptic   = optic;
		sel.m_sIdentityUid = SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId);
		m_mSelections.Set(playerId, sel);

		IEntity body = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(body);
		bool alive = character && character.GetCharacterController() && !character.GetCharacterController().IsDead();

		// Resolve once. The same resolved kit is what gets applied to a living body AND what the
		// client is handed for its preview, so the two can never disagree about which weapon,
		// optic or garment the player ended up with.
		RK29_KitStruct edited;
		ResourceName applyOptic;
		array<ResourceName> mounts;
		ResolveSelection(kit, cls, sel, edited, applyOptic, mounts);

		if (alive)
		{
			if (!IsPreround())
			{
				Print("[RK29] live re-kit refused - not preround (player " + playerId.ToString() + ")", LogLevel.NORMAL);
				return;
			}
			if (character.IsInVehicle())
			{
				Print("[RK29] live re-kit refused - player in vehicle (player " + playerId.ToString() + ")", LogLevel.NORMAL);
				return;
			}

			// remember drawn-weapon slot + stance so the client can restore both after
			int heldSlot = -1;
			int stance = 0;
			float dynStance = 1.0;
			bool wasDown = false;
			CharacterControllerComponent charCtrl = character.GetCharacterController();
			if (charCtrl)
			{
				stance = charCtrl.GetStance();
				dynStance = charCtrl.GetDynamicStance();
				wasDown = charCtrl.GetLifeState() == ECharacterLifeState.INCAPACITATED;
				BaseWeaponManagerComponent wm = charCtrl.GetWeaponManagerComponent();
				if (wm && wm.GetCurrentWeapon())
				{
					WeaponSlotComponent heldWs = wm.GetCurrentSlot();
					if (heldWs)
						heldSlot = heldWs.GetWeaponSlotIndex();
				}
			}

			// A re-kit hands out a clean loadout; hand out the clean body to go with it.
			// Preround only - the guards above see to that - so this is scratch damage from
			// the staging area, never a round the player is meant to live with. Immediate
			// even when the apply below waits: healing does not touch the hands.
			RK29_KitHeal.Heal(character);

			// an incapacitated player was lying down; the heal just stood them back up, so
			// the stance we captured a moment ago is a stance they no longer have. Restoring
			// it would drop the freshly revived player straight back onto their face.
			if (wasDown)
			{
				stance = ECharacterStance.STAND;
				dynStance = 1.0;
			}

			// The generation bump no longer guards anything - every apply is synchronous again -
			// but it keeps the counter moving in step with the applies, ready if deferred work
			// ever comes back.
			BumpApplyGen(playerId);

			array<ResourceName> droppedItems;
			RK29_KitApply.Apply(character, edited, applyOptic, mounts, droppedItems);
			NotifyDropped_S(playerId, droppedItems);
			StampBody(character, kit);

			SCR_PlayerController heldPc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
			if (heldPc)
				heldPc.RK29_NotifyRestoreState_S(stance, dynStance, WeaponRplIdInSlot(character, heldSlot));
		}

		// deploy menu shows "Current Kit" selected; spawn re-dresses from the stash
		AssignIdentity_S(playerId, IdentityKitFor(kit.m_sFactionKey, kitName));

		// Hand the client the loadout it will actually spawn wearing, rather than a few fields
		// for it to reassemble. Every "preview shows the wrong thing" bug came from the client
		// re-deriving what the server had already worked out.
		SCR_PlayerController spc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (spc)
			spc.RK29_NotifyKitSaved_S(kitName, sel.m_sOptic, RK29_KitWire.Pack(edited));

		Recompute_S();
	}

	//! Bumped on every spawn and every apply. A deferred mutation carries the value it was
	//! scheduled under and abandons itself if it is no longer current - so a stale snapshot
	//! cannot dress a body that has since respawned, and two applies inside the settle window
	//! do not both run.
	protected ref map<int, int> m_mApplyGen_S = new map<int, int>();

	//--------------------------------------------------------------------------------------------
	//! Two options in one slot both claiming m_bDefault is an authoring mistake with a silent
	//! outcome - the earlier one just wins and the other looks ignored. Say so once, at boot.
	protected void WarnOnDuplicateDefaults(string kitName, array<ref RK29_WeaponSlot> slots)
	{
		if (!slots)
			return;

		foreach (RK29_WeaponSlot slotGroup : slots)
		{
			if (!slotGroup || !slotGroup.m_aOptions)
				continue;

			string flagged;
			int count = 0;
			foreach (RK29_WeaponOption opt : slotGroup.m_aOptions)
			{
				if (!opt || !opt.m_bDefault)
					continue;
				count = count + 1;
				if (flagged != "")
					flagged = flagged + ", ";
				flagged = flagged + opt.m_sWeapon;
			}

			if (count > 1)
				Print("[RK29] '" + kitName + "' slot " + slotGroup.m_iSlot.ToString() + " marks "
					+ count.ToString() + " defaults (" + flagged + ") - the first wins, the rest are"
					+ " ignored. Leave m_bDefault set on only one", LogLevel.WARNING);
		}
	}

	//--------------------------------------------------------------------------------------------
	//! Capture a body, then compose over it when the class says how. Shared by both boot
	//! passes so a kit built from a roster class is identical to one built from a loadout -
	//! only where the body came from differs. Null = the body would not capture.
	protected RK29_KitStruct BuildKit(string kitName, string factionKey, ResourceName body, bool ownBody)
	{
		RK29_KitStruct kit = RK29_KitCapture.Capture(kitName, factionKey, body);
		if (!kit)
			return null;

		m_mCaptured.Set(kitName, kit);

		// hybrid: a class with a composition composes from config, else the capture stands
		RK29_ClassSetup cls = m_Setup.FindClass(kitName);
		if (!cls || cls.m_sComposition == ResourceName.Empty)
			return kit;

		array<ref RK29_WeaponSlot> options;
		RK29_KitStruct composed = RK29_KitCompose.Compose(cls, kit, m_Setup, options);
		if (!composed)
			return kit;

		m_mKitOptions.Set(kitName, options);
		WarnOnDuplicateDefaults(kitName, options);
		// the base is what weapon choices lay over; m_mKits keeps the
		// default-weapon kit so every reader still sees a fieldable one
		m_mKitsBase.Set(kitName, composed);
		composed = RK29_KitCompose.ApplyWeaponChoices(composed, options, ResourceName.Empty, m_Setup);

		// drift guardrail: only meaningful for a kit whose body is the prefab that used to
		// DEFINE it - i.e. one built from its own deploy entry. A kit built from a shared side
		// body is dressed entirely from config, so the body's item count says nothing and the
		// comparison would fire on every one of them.
		if (ownBody)
		{
			// intended config-vs-prefab deltas are small (grenade fold, bayonet
			// normalization). A big gap means the prefab was reworked after the dump the
			// configs were generated from - LAT taught us this.
			int drift = composed.CountItems() - kit.CountItems();
			if (drift < -2 || drift > 4)
				Print(string.Format("[RK29] DRIFT WARNING '%1': composed %2 vs prefab %3 items - regenerate configs from a fresh /kitdump",
					kitName, composed.CountItems(), kit.CountItems()), LogLevel.WARNING);
		}

		Print(string.Format("[RK29] kit '%1' from CONFIG (%2 items)",
			kitName, composed.CountItems()), LogLevel.NORMAL);
		return composed;
	}

	//--------------------------------------------------------------------------------------------
	//! The bare body this side spawns. Config dresses it; nothing is inherited from it.
	ResourceName SideBody(string factionKey)
	{
		if (!m_Setup || !m_Setup.m_aClasses)
			return ResourceName.Empty;
		foreach (RK29_ClassSetup c : m_Setup.m_aClasses)
		{
			if (c && c.m_sSideFactionKey == factionKey && c.m_sSideBodyPrefab != ResourceName.Empty)
				return c.m_sSideBodyPrefab;
		}
		return ResourceName.Empty;
	}

	//--------------------------------------------------------------------------------------------
	//! Create and store this player's first selection from the side default. Returns null when
	//! the squad is offered nothing on that side, or when the resolved kit has no definition.
	protected RK29_PlayerSelection SeedDefaultSelection_S(int playerId, string factionKey)
	{
		if (factionKey == "")
			return null;

		string kitName = DefaultKit(playerId, factionKey);
		if (kitName == "" || !m_mKits.Contains(kitName))
			return null;

		RK29_PlayerSelection sel = new RK29_PlayerSelection();
		sel.m_sKitName = kitName;
		// empty weapon = the composition's authored default, same as an untouched picker row
		sel.m_sWeapon = ResourceName.Empty;

		RK29_ClassSetup cls = m_Setup.FindClass(kitName);
		if (cls)
			sel.m_sOptic = cls.m_sDefaultOptic;

		sel.m_sIdentityUid = SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId);

		m_mSelections.Set(playerId, sel);

		// Tell the client, exactly like an apply or a rejoin does. Without this the seed is the
		// one selection change the client never hears about: its stash stays empty, the deploy
		// row keeps whatever icon and label it was built with, and the mannequin falls back to
		// composing the kit locally instead of showing the loadout the server actually resolved.
		// On a listen host those two happen to agree; on a dedicated server the resolved wire is
		// the only thing that carries the server's own fit decisions.
		SCR_PlayerController spc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (spc)
		{
			RK29_KitStruct seedKit = m_mKits.Get(kitName);
			RK29_KitStruct seedEdited;
			ResourceName seedOptic;
			array<ResourceName> seedMounts;
			if (seedKit)
				ResolveSelection(seedKit, cls, sel, seedEdited, seedOptic, seedMounts);
			spc.RK29_NotifyKitSaved_S(kitName, sel.m_sOptic, RK29_KitWire.Pack(seedEdited));
		}

		Print("[RK29] player " + playerId.ToString() + " had no kit - started on '" + kitName + "'", LogLevel.NORMAL);
		return sel;
	}

	//--------------------------------------------------------------------------------------------
	//! Dress a body straight from the stash, called by RK29_CurrentKitLoadout.OnLoadoutSpawned.
	//! This is the whole spawn path now: the body arrives bare, so there is nothing to strip,
	//! no async stock-item race to wait out, and no window where the player sees another kit.
	//! Returns true if the kit was applied - the spawn handler then leaves the body alone.
	bool ApplyStashOnSpawn_S(int playerId, IEntity body, string factionKey = "")
	{
		if (!Replication.IsServer() || !body)
			return false;

		// First deploy of the session lands here with nothing stashed. Seed the side's starting
		// kit rather than refusing - Current Kit is the only row most squads have, so refusing
		// spawns a bare body. Seeding a real selection (not just dressing one) keeps the HUD
		// count, the picker's pre-selection and the respawn identity agreeing from the first life.
		RK29_PlayerSelection sel = m_mSelections.Get(playerId);
		if (!sel)
			sel = SeedDefaultSelection_S(playerId, factionKey);
		if (!sel)
			return false;

		RK29_KitStruct kit = m_mKits.Get(sel.m_sKitName);
		if (!kit)
			return false;

		RK29_KitStruct edited;
		ResourceName applyOptic;
		array<ResourceName> mounts;
		ResolveSelection(kit, m_Setup.FindClass(sel.m_sKitName), sel, edited, applyOptic, mounts);

		array<ResourceName> droppedItems;
		RK29_KitApply.Apply(body, edited, applyOptic, mounts, droppedItems);
		NotifyDropped_S(playerId, droppedItems);
		StampBody(body, kit);

		// Draw the primary. Holding a weapon is character-controller STATE, not inventory, so
		// it cannot be baked into the saved loadout - somebody has to ask for it. Same request
		// the live re-kit makes, so a spawned player and a re-kitted one end up identical:
		// primary in hand, low ready.
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		SCR_ChimeraCharacter spawnChar = SCR_ChimeraCharacter.Cast(body);
		CharacterControllerComponent ctrl;
		if (spawnChar)
			ctrl = spawnChar.GetCharacterController();
		if (pc && ctrl)
			pc.RK29_NotifyRestoreState_S(ctrl.GetStance(), ctrl.GetDynamicStance(), WeaponRplIdInSlot(body, 0));

		m_mSpawnApplied_S.Set(playerId, BumpApplyGen(playerId));
		return true;
	}

	//! Apply generation the loadout hook dressed this player under, so the spawn handler can
	//! tell "already done" from "needs the old deferred path".
	protected ref map<int, int> m_mSpawnApplied_S = new map<int, int>();

	//--------------------------------------------------------------------------------------------
	protected int BumpApplyGen(int playerId)
	{
		int gen;
		m_mApplyGen_S.Find(playerId, gen);
		gen++;
		m_mApplyGen_S.Set(playerId, gen);
		return gen;
	}

	void OnPlayerSpawned_S(int playerId, IEntity entity)
	{
		if (!Replication.IsServer())
			return;

		// EVERY spawn, not just a kitted one: a mutation queued against the previous body must
		// not find itself still current and dress the new one.
		BumpApplyGen(playerId);

		// "Current Kit" is the ONLY entry that dresses from the stash, and it does so in
		// OnLoadoutSpawned, on a bare body - no strip, no settle window, nothing to redo here.
		// Picking any other entry is a request for that loadout as authored, including the stock
		// entry for the very class the stash came from; stock spawns are NEVER mutated.
		//
		// There used to be a deferred re-dress here for a Current Kit spawn that somehow missed
		// the loadout hook. Removed 2026-08-25: it was the original spawn path left behind after
		// OnLoadoutSpawned superseded it, never once observed firing, and it fired silently so a
		// miss would have looked like nothing at all. Its gate read the ASSIGNED loadout rather
		// than the body's provenance, so the only thing that could reach it was a body the game
		// mode counts as a spawn but that never came out of the loadout - GM possession, or a
		// respawn route bypassing SCR_PlayerLoadoutComponent. If that turns up, the body keeps
		// the gear it spawned with and the player re-kits (preround only) or respawns.
		int appliedGen;
		bool willApply = m_mSpawnApplied_S.Find(playerId, appliedGen);
		m_mSpawnApplied_S.Remove(playerId);

		// A stock spawn keeps its GEAR as authored - that is the rule above and it stands - but a
		// role's QUALIFICATIONS are config-owned, and selections live in server memory only, so
		// the first spawn of every session is a stock one. Without this a medic would bandage at
		// rifleman speed until they opened the picker. Labels only, nothing touched in the
		// inventory, and no settle defer: labels do not race the async item-init that the apply
		// pass waits out. Foreign or vanilla loadouts resolve to no kit and are left alone.
		if (!willApply)
		{
			RK29_KitStruct spawnedKit = m_mKits.Get(CurrentLoadoutName(playerId));
			if (spawnedKit && entity)
				RK29_KitApply.ApplyTraits(entity, spawnedKit);
		}

		Recompute_S();
	}

	//--------------------------------------------------------------------------------------------
	//! Network id of the weapon now sitting in `slotIndex`, so the client can wait for THAT
	//! entity rather than for a slot to look occupied. The slot is a poor readiness test on its
	//! own: the body's previous weapon may not have been reaped client-side yet, and if the
	//! re-kit hands back the same prefab the two are indistinguishable by name - the client
	//! would draw the doomed one. An id cannot be confused that way.
	//! Invalid when the slot is empty or the index is negative (nothing was in hands).
	protected static RplId WeaponRplIdInSlot(IEntity character, int slotIndex)
	{
		if (!character || slotIndex < 0)
			return RplId.Invalid();

		ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
		if (!chimera)
			return RplId.Invalid();
		CharacterControllerComponent ctrl = chimera.GetCharacterController();
		if (!ctrl)
			return RplId.Invalid();
		BaseWeaponManagerComponent wm = ctrl.GetWeaponManagerComponent();
		if (!wm)
			return RplId.Invalid();

		array<WeaponSlotComponent> slots = {};
		wm.GetWeaponsSlots(slots);
		foreach (WeaponSlotComponent slot : slots)
		{
			if (!slot || slot.GetWeaponSlotIndex() != slotIndex)
				continue;
			IEntity weapon = slot.GetWeaponEntity();
			if (!weapon)
				break;
			RplComponent rpl = RplComponent.Cast(weapon.FindComponent(RplComponent));
			if (rpl)
				return rpl.Id();
			break;
		}
		return RplId.Invalid();
	}

	//--------------------------------------------------------------------------------------------
	//! Same prefab dropped N times folds to "Nx Name" so the hint stays one line per item.
	protected void NotifyDropped_S(int playerId, array<ResourceName> droppedItems)
	{
		if (!droppedItems || droppedItems.IsEmpty())
			return;
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (!pc)
			return;

		map<string, int> counts = new map<string, int>();
		array<string> order = {};
		foreach (ResourceName prefab : droppedItems)
		{
			string name = RK29_ItemNames.Get(prefab);
			int seen;
			if (!counts.Find(name, seen))
				order.Insert(name);
			counts.Set(name, seen + 1);
		}

		string itemList;
		foreach (string name : order)
		{
			if (itemList != string.Empty)
				itemList += ", ";
			int n = counts.Get(name);
			if (n > 1)
				itemList += n.ToString() + "x " + name;
			else
				itemList += name;
		}
		pc.RK29_NotifyItemsDropped_S(droppedItems.Count(), itemList);
	}

	//--------------------------------------------------------------------------------------------
	//! Selection -> edited struct + what the optic pass should do. A weapon-variant optic swaps
	//! the primary to the pre-authored variant; passing its optic prefab through makes the
	//--------------------------------------------------------------------------------------------
	//! dedup in ApplyOptic a no-op instead of stripping the variant's integral scope.
	protected void ResolveSelection(RK29_KitStruct kit, RK29_ClassSetup cls, RK29_PlayerSelection sel,
		out RK29_KitStruct edited, out ResourceName applyOptic, out array<ResourceName> mounts)
	{
		ResourceName chosenWeapon = sel.m_sWeapon;
		applyOptic = sel.m_sOptic;
		mounts = null;

		RK29_OpticOption opt = m_Setup.FindOpticOption(cls, sel.m_sOptic);
		if (opt)
		{
			if (opt.m_sWeaponVariantPrefab != ResourceName.Empty)
				chosenWeapon = opt.m_sWeaponVariantPrefab;
			else
				mounts = opt.m_aRequiredAttachments;
		}

		// a stash written before a config edit can name a weapon nobody offers any more -
		// fall back to the class default rather than composing a weaponless kit
		ResourceName pickedWeapon = sel.m_sWeapon;
		array<ref RK29_WeaponSlot> options = m_mKitOptions.Get(kit.m_sKitName);
		if (pickedWeapon != ResourceName.Empty && !m_Setup.FindWeapon(options, pickedWeapon, kit.m_sFactionKey))
		{
			Print("[RK29] stashed weapon no longer offered by '" + kit.m_sKitName
				+ "' - using the class default", LogLevel.WARNING);
			pickedWeapon = ResourceName.Empty;
		}

		RK29_KitStruct base = m_mKitsBase.Get(kit.m_sKitName);
		if (!base)
			base = kit;

		edited = RK29_KitCompose.ApplyWeaponChoices(base, options, pickedWeapon, m_Setup);

		// an optic that swaps in a scoped weapon variant still wins the primary slot
		if (opt && opt.m_sWeaponVariantPrefab != ResourceName.Empty && chosenWeapon != ResourceName.Empty)
		{
			edited.m_mWeapons.Set(0, chosenWeapon);
			edited.m_sPrimaryWeapon = chosenWeapon;
		}
	}

	//--------------------------------------------------------------------------------------------
	bool IsCurrentKitLoadoutName(string loadoutName)
	{
		if (loadoutName == "")
			return false;
		return RK29_CurrentKitLoadout.Cast(FindLoadoutByName(loadoutName)) != null;
	}

	//--------------------------------------------------------------------------------------------
	protected string CurrentKitLoadoutName(string factionKey)
	{
		foreach (string name : m_aIndexToKit)
		{
			if (name == "" || !IsCurrentKitLoadoutName(name))
				continue;
			RK29_KitStruct kit = m_mKits.Get(name);
			if (kit && kit.m_sFactionKey == factionKey)
				return name;
		}
		return "";
	}

	//--------------------------------------------------------------------------------------------
	//! Assigned loadout, with "Current Kit" resolved to the stashed kit. A stash from
	//! another faction (player switched sides) never resolves.
	string EffectiveKitName(int playerId)
	{
		string name = CurrentLoadoutName(playerId);
		if (!IsCurrentKitLoadoutName(name))
			return name;

		RK29_PlayerSelection sel = m_mSelections.Get(playerId);
		if (!sel)
			return "";

		RK29_KitStruct pseudo = m_mKits.Get(name);
		RK29_KitStruct stash = m_mKits.Get(sel.m_sKitName);
		if (pseudo && stash && stash.m_sFactionKey != pseudo.m_sFactionKey)
			return "";
		return sel.m_sKitName;
	}

	//--------------------------------------------------------------------------------------------
	protected int KitIndexByName(string kitName)
	{
		if (kitName == "")
			return -1;
		foreach (int idx, string name : m_aIndexToKit)
		{
			if (name == kitName)
				return idx;
		}
		return -1;
	}

	//--------------------------------------------------------------------------------------------
	//! The kit Current Kit would spawn for this player on this side, stash or not.
	//!
	//! A player who has never opened the picker still deploys through Current Kit - it is the
	//! only general-purpose row left in the deploy menu - so "no stash" has to resolve to a real
	//! kit rather than to nothing. Order:
	//!   1. the stash, when it is this faction's and still offered to the player's squad
	//!   2. the side's configured default (Rifleman), when the squad is offered it
	//!   3. the squad's first offered kit - a crew has no rifleman, and an empty row is worse
	//! Returns "" only when the squad is offered no kits at all on this side.
	//!
	//! Readable from either side of the wire. The stash lives in two different places depending
	//! on who is asking: the authority keeps every player's selection in m_mSelections, a client
	//! only ever knows its own and learns it from the server's confirmation RPC. Reading
	//! whichever one this machine has lets the deploy row, its label, its mannequin and the
	//! server's spawn all resolve the same kit.
	string EffectiveKitFor(int playerId, string factionKey = "")
	{
		string stash;
		RK29_PlayerSelection sel = m_mSelections.Get(playerId);
		if (sel)
			stash = sel.m_sKitName;
		else
			stash = RK29_KitPicker.LocalStashKit();

		if (stash != "")
		{
			RK29_KitStruct kit = m_mKits.Get(stash);
			if (kit && (factionKey == "" || kit.m_sFactionKey == factionKey))
			{
				array<string> stashOffered = {};
				GetOfferedKits(playerId, kit.m_sFactionKey, stashOffered);
				if (stashOffered.Contains(stash))
					return stash;
			}
		}

		// No usable stash. Without a faction there is no side to default ON, so a caller that
		// wants the fallback has to name one.
		if (factionKey == "")
			return "";

		return DefaultKit(playerId, factionKey);
	}

	//--------------------------------------------------------------------------------------------
	//! The side's starting kit for this player, filtered through their squad's offer list.
	//! Falls back to the first offered kit so every squad gets a spawnable answer.
	string DefaultKit(int playerId, string factionKey)
	{
		array<string> offered = {};
		GetOfferedKits(playerId, factionKey, offered);
		if (offered.IsEmpty())
			return "";

		string configured = m_Setup.DefaultKitName(factionKey);
		if (configured != "" && offered.Contains(configured))
			return configured;

		if (configured != "" && factionKey != m_sLastDefaultlessFaction)
		{
			m_sLastDefaultlessFaction = factionKey;
			Print("[RK29] side '" + factionKey + "' default kit '" + configured + "' is not offered to this"
				+ " squad - Current Kit starts on '" + offered[0] + "' instead", LogLevel.NORMAL);
		}

		return offered[0];
	}

	//--------------------------------------------------------------------------------------------
	//! Player ids can be recycled - a newcomer must never inherit a stranger's stash.
	//! The leaver's stash parks under their identity uid for reconnect (bodies persist).
	void OnPlayerDisconnected_S(int playerId)
	{
		if (!Replication.IsServer())
			return;

		RK29_PlayerSelection sel = m_mSelections.Get(playerId);
		m_mSelections.Remove(playerId);
		m_mApplyGen_S.Remove(playerId);
		if (sel && sel.m_sIdentityUid != "")
			m_mParkedSelections.Set(sel.m_sIdentityUid, sel);

		Recompute_S();
	}

	//--------------------------------------------------------------------------------------------
	//! Reconnect: restore a parked stash to the (possibly new) player id. Deferred
	//! re-assignment covers rejoin-on-body, where the loadout assignment died with the
	//! old controller while the dressed body lives on.
	void OnPlayerAuditSuccess_S(int playerId)
	{
		if (!Replication.IsServer())
			return;

		string uid = SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId);
		if (uid == "")
			return;

		RK29_PlayerSelection sel = m_mParkedSelections.Get(uid);
		if (!sel)
			return;
		m_mParkedSelections.Remove(uid);
		m_mSelections.Set(playerId, sel);

		SCR_PlayerController spc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (spc)
		{
			// same resolved-loadout contract on a rejoin: the client gets the real thing
			RK29_KitStruct rejoinKit = m_mKits.Get(sel.m_sKitName);
			RK29_KitStruct rejoinEdited;
			ResourceName rejoinOptic;
			array<ResourceName> rejoinMounts;
			if (rejoinKit)
				ResolveSelection(rejoinKit, m_Setup.FindClass(sel.m_sKitName), sel,
					rejoinEdited, rejoinOptic, rejoinMounts);
			spc.RK29_NotifyKitSaved_S(sel.m_sKitName, sel.m_sOptic, RK29_KitWire.Pack(rejoinEdited));
		}

		GetGame().GetCallqueue().CallLater(RestoreRejoinIdentity, 2000, false, playerId);
	}

	//--------------------------------------------------------------------------------------------
	protected void RestoreRejoinIdentity(int playerId)
	{
		RK29_PlayerSelection sel = m_mSelections.Get(playerId);
		if (!sel)
			return;

		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId));
		if (!character || !character.GetCharacterController() || character.GetCharacterController().IsDead())
			return;

		RK29_KitStruct kit = m_mKits.Get(sel.m_sKitName);
		if (!kit)
			return;

		AssignIdentity_S(playerId, IdentityKitFor(kit.m_sFactionKey, sel.m_sKitName));
		Recompute_S();
	}

	//--------------------------------------------------------------------------------------------
	//! Loadout to record the player under after a kit apply. Normally the faction's
	//! "Current Kit" entry, which is what makes the stash survive a respawn. Without one we
	//! fall back to the stock entry for the kit - the player keeps the right class, but only
	//! "Current Kit" re-dresses on spawn, so the stash is lost on death. Loud, because the
	//! cause is a missing roster entry and the symptom shows up a death later.
	protected string IdentityKitFor(string factionKey, string kitName)
	{
		string identityKit = CurrentKitLoadoutName(factionKey);
		if (identityKit != "")
			return identityKit;

		Print("[RK29] config WARNING - faction '" + factionKey + "' has no Current Kit loadout;"
			+ " customization will not survive respawn", LogLevel.WARNING);
		return kitName;
	}

	//--------------------------------------------------------------------------------------------
	protected void AssignIdentity_S(int playerId, string kitName)
	{
		PlayerController pc = GetGame().GetPlayerManager().GetPlayerController(playerId);
		if (!pc)
			return;
		SCR_PlayerLoadoutComponent plc = SCR_PlayerLoadoutComponent.Cast(pc.FindComponent(SCR_PlayerLoadoutComponent));
		if (!plc)
			return;

		SCR_BasePlayerLoadout loadout = FindLoadoutByName(kitName);
		if (!loadout)
			return;

		if (plc.GetAssignedLoadout() == loadout)
			return;

		// force: vanilla's check may refuse living players
		plc.RK29_AssignLoadout_S(loadout, true);
	}

	// ============================================================================= counting

	//--------------------------------------------------------------------------------------------
	protected void ServerTick()
	{
		Recompute_S();
	}

	//--------------------------------------------------------------------------------------------
	//! Always rebuilds from scratch - never increment counters.
	void Recompute_S()
	{
		if (!Replication.IsServer())
			return;

		SCR_GameModeEditor gm = SCR_GameModeEditor.Cast(GetGame().GetGameMode());
		if (!gm)
			return;

		int n = m_aIndexToKit.Count();
		array<int> alive = {};
		array<int> magnified = {};
		for (int i = 0; i < n; i++)
		{
			alive.Insert(0);
			magnified.Insert(0);
		}

		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);
		foreach (int pid : players)
		{
			SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(GetGame().GetPlayerManager().GetPlayerControlledEntity(pid));
			if (!character || !character.GetCharacterController())
				continue;
			if (character.GetCharacterController().IsDead())
				continue; // uncon counts as alive

			int idx = KitIndexByName(EffectiveKitName(pid));
			if (idx < 0 || idx >= n || m_aIndexToKit[idx] == "")
				continue;

			alive[idx] = alive[idx] + 1;

			if (m_Setup.IsOpticMagnified(EffectiveOptic(pid, m_aIndexToKit[idx])))
				magnified[idx] = magnified[idx] + 1;
		}

		gm.RK29_SetCounts(alive, magnified);
	}

	//--------------------------------------------------------------------------------------------
	//! Selection if set, else class default - so a stock Marksman still tallies as magnified.
	protected ResourceName EffectiveOptic(int playerId, string kitName)
	{
		RK29_PlayerSelection sel = m_mSelections.Get(playerId);
		if (sel && sel.m_sKitName == kitName)
			return sel.m_sOptic;

		RK29_ClassSetup cls = m_Setup.FindClass(kitName);
		if (cls)
			return cls.m_sDefaultOptic;
		return ResourceName.Empty;
	}

	// ========================================================================== UIInfo stamp

	//--------------------------------------------------------------------------------------------
	protected void StampTick()
	{
		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);
		foreach (int pid : players)
		{
			IEntity body = GetGame().GetPlayerManager().GetPlayerControlledEntity(pid);
			if (!body)
				continue;

			string kitName = EffectiveKitName(pid);
			if (kitName == "")
				continue;
			RK29_KitStruct kit = m_mKits.Get(kitName);
			if (!kit)
				continue;

			StampBody(body, kit);
		}
	}

	//--------------------------------------------------------------------------------------------
	protected void StampBody(IEntity body, RK29_KitStruct kit)
	{
		SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(body);
		if (!editable)
			return;

		RK29_KitUIInfo ours = RK29_KitUIInfo.Cast(editable.GetInfo());
		if (ours)
		{
			// mutate, never re-instance - consumers cache the returned reference
			if (ours.m_sRK29_KitName != kit.m_sKitName)
				ours.RK29_SetKit(kit);
			return;
		}

		editable.SetInfoInstance(RK29_KitUIInfo.RK29_Create(kit));
	}

	//--------------------------------------------------------------------------------------------
	//! The name the group's own preset declares - "29th HQ" - or "" when the group has no
	//! preset behind it. Independent of anything a player typed or renamed.
	//!
	//! Role NONE resolves too. The no-role creation path
	//! (SCR_PlayerControllerGroupComponent.RPC_AskCreateGroup, reached via CreateAndJoinGroup)
	//! never applies a preset, so such a group has no role AND no name - and vanilla's own
	//! deploy filter, SCR_AIGroup.IsLoadoutInGroup(), matches presets by role as well, so it
	//! would hand that player an EMPTY deploy menu, not just the wrong kits. GM29_Groups.conf
	//! therefore carries a hidden NONE preset mirroring "29th Squad" (m_bCanBeCreatedByPlayer 0
	//! keeps it out of the create-group dialog, which filters on that flag). Both lookups then
	//! land on the rifle squad, which is the sane default for an unassigned group.
	protected string PresetGroupName(notnull SCR_AIGroup group)
	{
		SCR_GroupsManagerComponent groups = SCR_GroupsManagerComponent.GetInstance();
		if (!groups)
			return "";

		Faction faction = group.GetFaction();
		if (!faction)
			return "";

		SCR_GroupRolePresetConfig preset = groups.FindGroupRolePresetConfig(faction, group.GetGroupRole());
		if (!preset)
			return "";

		return preset.GetGroupName();
	}

	//--------------------------------------------------------------------------------------------
	//! Kits this player may take. Authority order: our squad kit catalog (by group
	//! name), the legacy vanilla group loadout lists, all faction kits.
	int GetOfferedKits(int playerId, string factionKey, notnull array<string> outKitNames)
	{
		outKitNames.Clear();

		SCR_AIGroup group;
		SCR_GroupsManagerComponent groups = SCR_GroupsManagerComponent.GetInstance();
		if (groups)
			group = groups.GetPlayerGroup(playerId);

		if (group)
		{
			// Squads are matched on the name their PRESET declares, with the group's runtime
			// custom name as an override.
			//
			// The runtime name is not identity. Vanilla's create-group dialog opens with an
			// empty name box and then overwrites whatever the preset set with the typed text
			// (SCR_PlayerControllerGroupComponent.SetCustomNameAndDescription), so a group
			// created as "29th HQ" reports an EMPTY name and every squad collapses onto the "*"
			// default. It also crosses an async profanity filter, so even a named group reads
			// back empty on clients for a while.
			//
			// The preset behind the group is stable: role is a plain replicated int set at
			// creation and carried in RplSave/RplLoad, and FindGroupRolePresetConfig() maps it
			// back to the authored config - the same call vanilla makes in
			// SCR_PlayerControllerGroupComponent to rank-check a group. So GM29_Groups.conf
			// stays the single source of the role-to-name mapping and RK29_Squads.conf keeps
			// keying on readable names.
			string groupName = PresetGroupName(group);
			if (groupName == "")
				groupName = group.GetCustomName();

			RK29_SquadKits sq = m_Setup.FindSquadKits(groupName);

			// Only the "*" fallback is worth a warning - a preset hit is the normal path.
			if (sq && sq.m_sGroupName == "*")
			{
				string groupKey = groupName + "/" + SCR_Enum.GetEnumName(SCR_EGroupRole, group.GetGroupRole());
				if (groupKey != m_sLastUnmatchedGroup)
				{
					m_sLastUnmatchedGroup = groupKey;
					Print("[RK29] group '" + groupName + "' (role "
						+ SCR_Enum.GetEnumName(SCR_EGroupRole, group.GetGroupRole())
						+ ") has no squad entry - using the '*' default list. Add an entry under that"
						+ " name in RK29_Squads.conf if it should get its own kits", LogLevel.WARNING);
				}
			}
			if (sq && sq.m_aKitNames && !sq.m_aKitNames.IsEmpty())
			{
				foreach (string sqKit : sq.m_aKitNames)
				{
					RK29_KitStruct kit = m_mKits.Get(sqKit);
					if (kit && kit.m_sFactionKey == factionKey)
						outKitNames.Insert(sqKit);
				}
				if (!outKitNames.IsEmpty())
					return outKitNames.Count();
			}
		}

		SCR_LoadoutManager lm = GetGame().GetLoadoutManager();
		Faction faction;
		FactionManager fm = GetGame().GetFactionManager();
		if (fm)
			faction = fm.GetFactionByKey(factionKey);

		if (lm && faction && group)
		{
			array<ref SCR_BasePlayerLoadout> groupLoadouts = {};
			lm.GetPlayerLoadoutsByGroup(group, faction, groupLoadouts);
			foreach (SCR_BasePlayerLoadout loadout : groupLoadouts)
			{
				if (!loadout)
					continue;
				string name = loadout.GetLoadoutName();
				if (m_mKits.Contains(name))
					outKitNames.Insert(name);
			}

			// NOT the place to add picker-only kits back. Vanilla's group filter answers with
			// loadouts, so it can never name one - but injecting them here offers every squad
			// every picker-only kit, which is how a rifle squad ended up being offered Pilot.
			// Squad membership is config's business: give every squad an answer in
			// RK29_Squads.conf, including the "*" default for squads with no entry of their own.

			if (!outKitNames.IsEmpty())
				return outKitNames.Count();
		}

		foreach (string kitName, RK29_KitStruct kit : m_mKits)
		{
			if (kit && kit.m_sFactionKey == factionKey)
				outKitNames.Insert(kitName);
		}
		return outKitNames.Count();
	}

	// ============================================================================== lookups

	//--------------------------------------------------------------------------------------------
	protected SCR_BasePlayerLoadout FindLoadoutByName(string kitName)
	{
		SCR_LoadoutManager lm = GetGame().GetLoadoutManager();
		if (!lm)
			return null;
		return lm.RK29_FindLoadoutByName(kitName);
	}

	//--------------------------------------------------------------------------------------------
	string CurrentLoadoutName(int playerId)
	{
		SCR_LoadoutManager lm = GetGame().GetLoadoutManager();
		if (!lm)
			return "";
		SCR_BasePlayerLoadout loadout = lm.GetPlayerLoadout(playerId);
		if (!loadout)
			return "";
		return loadout.GetLoadoutName();
	}

	//--------------------------------------------------------------------------------------------
	int CurrentLoadoutIndex(int playerId)
	{
		SCR_LoadoutManager lm = GetGame().GetLoadoutManager();
		if (!lm)
			return -1;
		return lm.RK29_IndexOfLoadout(lm.GetPlayerLoadout(playerId));
	}
}
