//------------------------------------------------------------------------------------------------
//! Kit system core. Runs on every machine; server-only parts no-op elsewhere.
//! TODO(RK29): replace the 2s recompute tick with game-mode invokers once 1.8 signatures checked.
//------------------------------------------------------------------------------------------------

class RK29_PlayerSelection
{
	string m_sKitName;
	ResourceName m_sWeapon; // empty = authored weapon
	ResourceName m_sOptic;  // empty = None/irons
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

	//! Same index space as SCR_LoadoutManager's list; the count arrays use it too.
	ref array<string> m_aIndexToKit = {};

	// server only
	protected ref map<int, ref RK29_PlayerSelection> m_mSelections = new map<int, ref RK29_PlayerSelection>();

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

			RK29_KitStruct kit = RK29_KitCapture.Capture(kitName, fl.GetFactionKey(), loadout.GetLoadoutResource());
			if (!kit)
				continue;

			m_mKits.Set(kitName, kit);
			m_aIndexToKit[i] = kitName;
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
					if (c)
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

		Print("[RK29] setup loaded - " + m_Setup.m_aClasses.Count().ToString() + " class(es), " + m_Setup.m_aOpticCategories.Count().ToString() + " optic categorie(s)", LogLevel.NORMAL);
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
			if (!m_Setup.FindWeapon(cls, weapon))
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

		// weapon choice may route to a different kit entirely (e.g. M60 -> Machine Gunner body)
		RK29_WeaponOption chosenWo = m_Setup.FindWeapon(cls, weapon);
		if (chosenWo && chosenWo.m_sSourceKitName != string.Empty)
		{
			RK29_KitStruct sourceKit = m_mKits.Get(chosenWo.m_sSourceKitName);
			if (sourceKit && sourceKit.m_sFactionKey == kit.m_sFactionKey)
			{
				kitName = chosenWo.m_sSourceKitName;
				kit = sourceKit;
			}
			else
				Print("[RK29] source kit '" + chosenWo.m_sSourceKitName + "' unknown - using class kit", LogLevel.WARNING);
		}
		// stock for the effective kit = no weapon delta, authored item layout survives intact
		if (weapon == kit.m_sPrimaryWeapon)
			weapon = ResourceName.Empty;

		RK29_PlayerSelection sel = new RK29_PlayerSelection();
		sel.m_sKitName = kitName;
		sel.m_sWeapon  = weapon;
		sel.m_sOptic   = optic;
		m_mSelections.Set(playerId, sel);

		IEntity body = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(body);
		bool alive = character && character.GetCharacterController() && !character.GetCharacterController().IsDead();

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

			RK29_KitStruct edited;
			ResourceName applyOptic;
			array<ResourceName> mounts;
			ResolveSelection(kit, cls, sel, edited, applyOptic, mounts);
			int droppedItems;
			RK29_KitApply.Apply(character, edited, applyOptic, mounts, droppedItems);
			NotifyDropped_S(playerId, droppedItems);
			StampBody(character, kit);
		}

		// deploy menu shows "Current Kit" selected; spawn re-dresses from the stash
		string identityKit = CurrentKitLoadoutName(kit.m_sFactionKey);
		if (identityKit == "")
			identityKit = kitName;
		AssignIdentity_S(playerId, identityKit);

		SCR_PlayerController spc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (spc)
			spc.RK29_NotifyKitSaved_S();

		Recompute_S();
	}

	//--------------------------------------------------------------------------------------------
	//! Applies the stashed customization onto a freshly spawned body.
	void OnPlayerSpawned_S(int playerId, IEntity entity)
	{
		if (!Replication.IsServer())
			return;

		RK29_PlayerSelection sel = m_mSelections.Get(playerId);
		if (sel)
		{
			RK29_KitStruct kit = m_mKits.Get(sel.m_sKitName);
			SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
			string current = CurrentLoadoutName(playerId);
			// "Current Kit" spawns a placeholder body - always re-dress from the stash;
			// a stock kit picked in the deploy menu only needs its stashed deltas
			bool wantsApply = IsCurrentKitLoadoutName(current)
				|| (current == sel.m_sKitName
					&& (sel.m_sWeapon != ResourceName.Empty || sel.m_sOptic != ResourceName.Empty));
			if (kit && character && wantsApply)
			{
				RK29_ClassSetup cls = m_Setup.FindClass(sel.m_sKitName);
				RK29_KitStruct edited;
				ResourceName applyOptic;
				array<ResourceName> mounts;
				ResolveSelection(kit, cls, sel, edited, applyOptic, mounts);
				// deferred: InitialInventoryItems land async on the spawn frame
				GetGame().GetCallqueue().CallLater(ApplySpawnMutation, 500, false, playerId, edited, applyOptic, mounts);
			}
		}

		Recompute_S();
	}

	//--------------------------------------------------------------------------------------------
	protected void ApplySpawnMutation(int playerId, RK29_KitStruct edited, ResourceName optic, array<ResourceName> mounts)
	{
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId));
		if (!character || !character.GetCharacterController() || character.GetCharacterController().IsDead())
			return;
		int droppedItems;
		RK29_KitApply.Apply(character, edited, optic, mounts, droppedItems);
		NotifyDropped_S(playerId, droppedItems);
	}

	//--------------------------------------------------------------------------------------------
	protected void NotifyDropped_S(int playerId, int droppedItems)
	{
		if (droppedItems <= 0)
			return;
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (pc)
			pc.RK29_NotifyItemsDropped_S(droppedItems);
	}

	//--------------------------------------------------------------------------------------------
	//! Selection -> edited struct + what the optic pass should do. A weapon-variant optic swaps
	//! the primary to the pre-authored variant; passing its optic prefab through makes the
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

		ResourceName chosenMag;
		int magCount = 0;
		RK29_WeaponOption wo = m_Setup.FindWeapon(cls, sel.m_sWeapon);
		if (wo)
		{
			chosenMag = wo.m_sMagazinePrefab;
			magCount  = wo.m_iMagazineCount;
		}

		array<ResourceName> classMags = {};
		m_Setup.GetClassMagazines(cls, classMags);

		edited = kit.CloneWithChoices(chosenWeapon, chosenMag, classMags, magCount);
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
	//! Assigned loadout, with "Current Kit" resolved to the stashed kit.
	string EffectiveKitName(int playerId)
	{
		string name = CurrentLoadoutName(playerId);
		if (!IsCurrentKitLoadoutName(name))
			return name;

		RK29_PlayerSelection sel = m_mSelections.Get(playerId);
		if (sel)
			return sel.m_sKitName;
		return "";
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
	//! Stashed kit name if it exists and is still offered to the player's squad, else "".
	string StashOfferedKit(int playerId)
	{
		RK29_PlayerSelection sel = m_mSelections.Get(playerId);
		if (!sel)
			return "";
		RK29_KitStruct kit = m_mKits.Get(sel.m_sKitName);
		if (!kit)
			return "";
		array<string> offered = {};
		GetOfferedKits(playerId, kit.m_sFactionKey, offered);
		if (!offered.Contains(sel.m_sKitName))
			return "";
		return sel.m_sKitName;
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
	//! Kits this player may take: squad-restricted when their group has a loadout list,
	//! else all faction kits. Empty group result falls back to the faction list.
	int GetOfferedKits(int playerId, string factionKey, notnull array<string> outKitNames)
	{
		outKitNames.Clear();

		SCR_LoadoutManager lm = GetGame().GetLoadoutManager();
		Faction faction;
		FactionManager fm = GetGame().GetFactionManager();
		if (fm)
			faction = fm.GetFactionByKey(factionKey);

		SCR_AIGroup group;
		SCR_GroupsManagerComponent groups = SCR_GroupsManagerComponent.GetInstance();
		if (groups)
			group = groups.GetPlayerGroup(playerId);

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
