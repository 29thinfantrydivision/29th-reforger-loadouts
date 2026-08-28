//------------------------------------------------------------------------------------------------
//! Kit system core. Runs on every machine; server-only parts no-op elsewhere.
//------------------------------------------------------------------------------------------------

//! What a player has chosen: the kit name plus the picks is the whole selection.
class RK29_PlayerSelection
{
	string m_sKitName;
	string m_sIdentityUid;  // persistent player identity, for reconnect parking

	//! Choice picks (weapon/ammo/attachment groups). Empty = every group at its default.
	ref array<ref RK29_ChoicePick> m_aPicks = {};
}

//------------------------------------------------------------------------------------------------
//! A live re-kit, resolved and waiting for the body's hands to come free.
class RK29_PendingApply
{
	int m_iPlayerId;
	int m_iGen;             // apply generation this was queued under; stale = abandon
	ref RK29_KitStruct m_Kit;     // unedited, for the line that names the kit in the log
	ref RK29_KitStruct m_Edited;  // what actually gets applied
	ref array<ref RK29_AttachmentOrder> m_aOrders; // every attachment decision, bound to its own weapon
	ref map<int, ref array<ref RK29_LoadedPick>> m_mLoadedMags; // weapon slot -> the rounds it starts loaded with
	int m_iStance;
	float m_fDynStance;
	int m_iHeldSlot;        // weapon slot that was drawn, resolved to an RplId after the apply
	bool m_bHeldThrowable;  // that slot was a grenade/throwable slot: index alone cannot say (3 is shared)
	float m_fDeadline;
}

//------------------------------------------------------------------------------------------------
//! The HUD, the picker, the mannequin and the deploy row read the config and the kits through
//! Setup/KitByName/KitCount/KitNameAt. Nothing outside writes them.
//------------------------------------------------------------------------------------------------
class RK29_KitManager
{
	//! Backstop behind the kit-index arrival and the registration hook, so it runs slowly.
	protected static const int STAMP_MS = 5000;

	//! The one server-side poll: the Round Timer keeps its phase in a replicated field with no
	//! invoker, and this mod reads that field by name so it needs no hard dependency. Nothing
	//! else fires when a briefing begins - no player spawns, dies or moves - so the kit counts
	//! the HUD shows would stay at whatever the last player event left them.
	protected static const int PHASE_WATCH_MS = 1000;

	//! Fewest milliseconds between two accepted requests from one player: cheap to send, not to
	//! serve.
	protected static const int REQUEST_MIN_INTERVAL_MS = 500;

	//! Ceiling on the hands-free wait, not the sequencer - the wait ends on the condition. Same
	//! shape as vanilla's ITEM_INSERTION_CALLBACK_CLEANUP_TIME; 3s covers the longest hand
	//! action.
	protected static const float HANDS_SETTLE_TIMEOUT_MS = 3000;

	//! How long after a spawn to check the draw held. Must be longer than RK29_SPAWN_DRAW_CAP_MS,
	//! which is anchored at possession and can trail a first spawn by many seconds.
	protected static const int DRAW_VERIFY_MS = 35000;

	//! First slot index a kit never claims on Character_Base: ClaimSlot hands out 0-2 only; the
	//! grenade slot and the hand-weapon slot both sit at 3, the throwable slot at 4. Vanilla's
	//! restore draws only slotIdx < 3.
	protected static const int FIRST_THROWABLE_SLOT = 3;

	static const ResourceName SETUP_CONF = "{AB29C0FFEEB20030}Configs/KitSystem/RK29_KitSetup.conf";

	protected static ref RK29_KitManager s_Instance;

	protected ref RK29_KitSetup m_Setup;

	protected ref map<string, ref RK29_KitStruct> m_mKits = new map<string, ref RK29_KitStruct>();

	//! The composition without choice-group resolution. A weapon choice is always laid over this,
	//! never over an already-resolved kit, or re-picking stacks that weapon's blocks twice.
	protected ref map<string, ref RK29_KitStruct> m_mKitsBase = new map<string, ref RK29_KitStruct>();

	//! A single slot would let two misconfigured sides re-announce each other on every deploy.
	protected ref array<string> m_aDefaultlessFactionsNoted = {};

	//! Same index space as SCR_LoadoutManager's list for as far as that list runs, then extends past
	//! its end with the picker-only kits; the count arrays use it too.
	protected ref array<string> m_aIndexToKit = {};
	//! read once at boot: asked per kit in every offer build, which was a linear scan each time
	protected ref array<string> m_aCurrentKitNames = {};

	//! The only owner of the role identities stamped onto bodies. SCR_EditableEntityComponent
	//! declares its holder as a plain `SCR_UIInfo` with no `ref`, so SetInfoInstance takes a weak
	//! reference: an instance handed straight over can be collected out from under the component.
	protected ref map<string, ref RK29_KitUIInfo> m_mKitInfos = new map<string, ref RK29_KitUIInfo>();

	protected int m_iStampsLanded;

	//! Whether Event_OnEntityRegistered took our hook. False is normal at boot -
	//! SCR_EditableEntityCore does not exist yet when the game mode inits.
	protected bool m_bStampSubscribed;

	protected ref RK29_RoundTimerProbe m_Probe = new RK29_RoundTimerProbe();

	// ---- server only ----

	protected ref map<int, ref RK29_PlayerSelection> m_mSelections = new map<int, ref RK29_PlayerSelection>();

	//! stashes of disconnected players, keyed by identity uid - survives rejoin-on-body. Never
	//! pruned, by design: it is bounded by the distinct identities one session has seen.
	protected ref map<string, ref RK29_PlayerSelection> m_mParkedSelections = new map<string, ref RK29_PlayerSelection>();

	//! Bumped on every spawn and every apply. A deferred mutation carries the value it was
	//! scheduled under and abandons itself if it is no longer current.
	protected ref map<int, int> m_mApplyGen_S = new map<int, int>();

	protected ref map<int, float> m_mLastRequestMs_S = new map<int, float>();

	//! Players whose body the loadout hook dressed, so the spawn handler can tell that body from a
	//! stock spawn it must leave alone. Presence only - the value is never read.
	protected ref map<int, int> m_mSpawnApplied_S = new map<int, int>();

	//! At most one per player: a newer request bumps the generation, so an older pending
	//! abandons.
	protected ref map<int, ref RK29_PendingApply> m_mPendingApply_S = new map<int, ref RK29_PendingApply>();

	//! One rebuild per frame however many events asked: each resolves every living player's
	//! offer.
	protected bool m_bRecomputeQueued_S;
	protected bool m_bWasBriefing_S;  // WatchPhase_S edge memory

	//------------------------------------------------------------------------------------------------
	static RK29_KitManager GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	//! The merged config every RK29_KitResolve static takes as its first argument; null before Init.
	RK29_KitSetup Setup()
	{
		return m_Setup;
	}

	//------------------------------------------------------------------------------------------------
	//! The kit composed under this name, at its choice-group defaults; null when unknown.
	RK29_KitStruct KitByName(string kitName)
	{
		return m_mKits.Get(kitName);
	}

	//------------------------------------------------------------------------------------------------
	//! Size of the kit index space: the loadout list, then the picker-only kits past its end.
	int KitCount()
	{
		return m_aIndexToKit.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! The kit name at this index of that space; "" out of range, and "" for an unfilled list slot.
	string KitNameAt(int kitIndex)
	{
		if (kitIndex < 0 || kitIndex >= m_aIndexToKit.Count())
			return "";
		return m_aIndexToKit[kitIndex];
	}

	//------------------------------------------------------------------------------------------------
	//! Called from GM29_KitLoadouts after injection, on every machine. Statics survive a scenario
	//! change; the callqueue does not.
	static void Boot(notnull array<ref SCR_BasePlayerLoadout> loadouts)
	{
		// The outgoing manager must let go first: SCR_EditableEntityCore is a game core and the
		// invoker holds the object it was given, so each world reload would leave another dead
		// manager alive.
		if (s_Instance)
			s_Instance.Unsubscribe();

		// Stated, not inherited. Client-side session statics survive a world rebuild inside one
		// process, so the deploy row and menu would open on the previous session's kit.
		RK29_LocalStash.Clear();
		RK29_LoadoutMenu.ForgetSession();

		s_Instance = new RK29_KitManager();
		s_Instance.Init(loadouts);
	}

	//------------------------------------------------------------------------------------------------
	protected void Unsubscribe()
	{
		// a repeating timer that survived a scenario change would double up per world
		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
		{
			queue.Remove(StampAll);
			queue.Remove(WatchPhase_S);
			// the one-shots too: one outliving its world would read old state against a new body
			queue.Remove(ApplyWhenHandsFree_S);
			queue.Remove(Recompute_S);
			queue.Remove(VerifyDrawn_S);
			queue.Remove(RestoreRejoinIdentity);
		}

		SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(
			SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
		if (core && core.Event_OnEntityRegistered)
			core.Event_OnEntityRegistered.Remove(OnEditableRegistered);
		m_bStampSubscribed = false;

		SCR_FactionManager fm = SCR_FactionManager.Cast(GetGame().GetFactionManager());
		if (fm && fm.GetOnPlayerFactionChanged_S())
			fm.GetOnPlayerFactionChanged_S().Remove(OnPlayerFactionChanged_S);

		SCR_BaseGameMode gm = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (!gm)
			return;

		if (gm.GetOnPlayerSpawned())
			gm.GetOnPlayerSpawned().Remove(OnPlayerSpawned_S);
		if (gm.GetOnPlayerKilled())
			gm.GetOnPlayerKilled().Remove(OnPlayerKilled_S);
		if (gm.GetOnPlayerDeleted())
			gm.GetOnPlayerDeleted().Remove(OnPlayerDeleted_S);
		if (gm.GetOnPlayerDisconnected())
			gm.GetOnPlayerDisconnected().Remove(OnPlayerDisconnected_Event_S);
		if (gm.GetOnPlayerAuditSuccess())
			gm.GetOnPlayerAuditSuccess().Remove(OnPlayerAuditSuccess_S);
	}

	//------------------------------------------------------------------------------------------------
	protected void Init(notnull array<ref SCR_BasePlayerLoadout> loadouts)
	{
		// Before LoadSetup: a cache that outlived the world would compose last session's files,
		// and the boot lint LoadSetup runs would then report on them
		RK29_KitCompose.ClearCaches();
		RK29_KitApply.ClearCaches();
		RK29_KitResolve.ClearSessionState();
		RK29_ItemNames.ClearCache();

		LoadSetup();

		foreach (int i, SCR_BasePlayerLoadout loadout : loadouts)
		{
			m_aIndexToKit.Insert("");
			if (!loadout)
				continue;
			if (RK29_CurrentKitLoadout.Cast(loadout))
				m_aCurrentKitNames.Insert(loadout.GetLoadoutName());

			SCR_FactionPlayerLoadout fl = SCR_FactionPlayerLoadout.Cast(loadout);
			if (!fl)
				continue;

			string kitName = loadout.GetLoadoutName();
			if (!kitName.StartsWith("29th"))
				continue;

			RK29_KitStruct kit = BuildKit(kitName, fl.GetFactionKey(), loadout.GetLoadoutResource());
			if (!kit)
				continue;

			m_mKits.Set(kitName, kit);
			m_aIndexToKit[i] = kitName;
		}

		// Pass 2: classes with no loadout entry of their own. A deploy-menu row is how a kit gets
		// spawned, not what makes it exist; every reader resolves through m_aIndexToKit.
		foreach (RK29_ClassSetup cls2 : m_Setup.m_aClasses)
		{
			if (!cls2 || cls2.m_sKitName == "" || m_mKits.Contains(cls2.m_sKitName))
				continue;

			ResourceName body = cls2.BodyPrefab();
			if (body == ResourceName.Empty)
			{
				Print(string.Format("[RK29] config ERROR - class '%1' has no loadout entry and"
					+ " no body prefab (set m_sBodyPrefab on its side config)",
					cls2.m_sKitName), LogLevel.ERROR);
				continue;
			}

			RK29_KitStruct standalone = BuildKit(cls2.m_sKitName, cls2.m_sSideFactionKey, body);
			if (!standalone)
				continue;

			m_mKits.Set(cls2.m_sKitName, standalone);
			m_aIndexToKit.Insert(cls2.m_sKitName);
			Print(string.Format("[RK29] kit '%1' is picker-only (no deploy entry, faction %2)",
				cls2.m_sKitName, standalone.m_sFactionKey), LogLevel.NORMAL);
		}

		Print(string.Format("[RK29] manager up - %1 kit(s) walked | server=%2",
			m_mKits.Count(), Replication.IsServer()), LogLevel.NORMAL);

		if (Replication.IsServer())
		{
			SubscribeLifecycle_S();
			GetGame().GetCallqueue().CallLater(WatchPhase_S, PHASE_WATCH_MS, true);
		}

		// SetInfoInstance is local, so the stamp must run on every machine.
		SubscribeStamp();
		GetGame().GetCallqueue().CallLater(StampAll, STAMP_MS, true);

		RK29_KitHud.Boot();
	}

	//------------------------------------------------------------------------------------------------
	//! One catalog .conf, instantiated, or null when the file is missing or unreadable.
	protected Managed LoadCatalog(ResourceName res)
	{
		Resource loaded = Resource.Load(res);
		if (!loaded.IsValid())
			return null;

		return BaseContainerTools.CreateInstanceFromContainer(loaded.GetResource().ToBaseContainer());
	}

	//------------------------------------------------------------------------------------------------
	//! Every catalog of one kind, loaded and type-checked, with one warning per file that did not
	//! answer - a missing file and a wrong root class both leave the merge nothing to read. The
	//! array owns what it holds (array<ref Managed>); a plain one would let a fresh catalog die
	//! first.
	//! outSources, when asked for, is filled in lockstep with the returned list - an unreadable
	//! source is in neither - so a caller can name the file a bad entry came from.
	protected array<ref Managed> LoadCatalogs(array<ResourceName> sources, typename catalogType,
		string label, array<ResourceName> outSources = null)
	{
		array<ref Managed> loaded = {};
		if (!sources)
			return loaded;

		foreach (ResourceName res : sources)
		{
			Managed cat = LoadCatalog(res);
			if (!cat || !cat.IsInherited(catalogType))
			{
				Print(string.Format("[RK29] %1 config missing/unreadable: %2",
					label, res), LogLevel.WARNING);
				continue;
			}

			loaded.Insert(cat);
			if (outSources)
				outSources.Insert(res);
		}

		return loaded;
	}

	//------------------------------------------------------------------------------------------------
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

		// every list the merge Inserts into must exist first; an unauthored array arrives null
		if (!m_Setup.m_aClasses)
			m_Setup.m_aClasses = {};
		if (!m_Setup.m_aAliases)
			m_Setup.m_aAliases = {};
		if (!m_Setup.m_aMagazineSets)
			m_Setup.m_aMagazineSets = {};
		if (!m_Setup.m_aChoiceGroups)
			m_Setup.m_aChoiceGroups = {};
		if (!m_Setup.m_aAttachments)
			m_Setup.m_aAttachments = {};
		if (!m_Setup.m_aWeaponDefs)
			m_Setup.m_aWeaponDefs = {};
		if (!m_Setup.m_aOverrides)
			m_Setup.m_aOverrides = {};

		MergeCatalogs();

		RK29_Log.s_bVerbose = m_Setup.m_bVerboseLogging;
		if (RK29_Log.s_bVerbose)
			Print("[RK29] verbose apply trace ENABLED - turn m_bVerboseLogging off before a live session", LogLevel.WARNING);

		Print(string.Format("[RK29] setup loaded - %1 class(es), %2 alias(es), %3 choice"
			+ " group(s), %4 attachment(s), %5 weapon definition(s)",
			m_Setup.m_aClasses.Count(), m_Setup.m_aAliases.Count(),
			m_Setup.m_aChoiceGroups.Count(), m_Setup.m_aAttachments.Count(),
			m_Setup.m_aWeaponDefs.Count()), LogLevel.NORMAL);

		// the config sweeps are for whoever can act on them - the host's log, Workbench included
		if (Replication.IsServer())
			RK29_KitLint.Run(m_Setup);
	}

	//------------------------------------------------------------------------------------------------
	//! Seven loops rather than one because Enforce cannot say "that field, of that type"
	//! generically.
	protected void MergeCatalogs()
	{
		array<ref Managed> catalogs = LoadCatalogs(m_Setup.m_aSideConfigs, RK29_SideSetup, "side");
		foreach (Managed sideRaw : catalogs)
		{
			RK29_SideSetup side = RK29_SideSetup.Cast(sideRaw);
			if (!side.m_aClasses)
				continue;
			foreach (RK29_ClassSetup c : side.m_aClasses)
			{
				if (!c)
					continue;
				// the flattened list loses which file a class came from, and the second pass
				// needs both
				c.m_sSideFactionKey = side.m_sFactionKey;
				c.m_sSideBodyPrefab = side.m_sBodyPrefab;
				c.m_sSideDefaultKit = side.m_sDefaultKitName;
				m_Setup.m_aClasses.Insert(c);
			}
		}

		catalogs = LoadCatalogs(m_Setup.m_aAliasConfigs, RK29_ItemAliasCatalog, "alias");
		foreach (Managed aliasRaw : catalogs)
		{
			RK29_ItemAliasCatalog cat = RK29_ItemAliasCatalog.Cast(aliasRaw);
			if (!cat.m_aAliases)
				continue;
			foreach (RK29_ItemAlias a : cat.m_aAliases)
			{
				if (a)
					m_Setup.m_aAliases.Insert(a);
			}
		}

		catalogs = LoadCatalogs(m_Setup.m_aMagSetConfigs, RK29_MagazineSetCatalog, "magazine set");
		foreach (Managed magRaw : catalogs)
		{
			RK29_MagazineSetCatalog mcat = RK29_MagazineSetCatalog.Cast(magRaw);
			if (!mcat.m_aMagazineSets)
				continue;
			foreach (RK29_MagazineSet magSet : mcat.m_aMagazineSets)
			{
				if (magSet)
					m_Setup.m_aMagazineSets.Insert(magSet);
			}
		}

		catalogs = LoadCatalogs(m_Setup.m_aWeaponConfigs, RK29_WeaponCatalog, "weapon");
		foreach (Managed weaponRaw : catalogs)
		{
			RK29_WeaponCatalog wcat = RK29_WeaponCatalog.Cast(weaponRaw);
			if (!wcat.m_aWeapons)
				continue;
			foreach (RK29_WeaponDef def : wcat.m_aWeapons)
			{
				if (def)
					m_Setup.m_aWeaponDefs.Insert(def);
			}
		}

		array<ResourceName> choiceSources = {};
		catalogs = LoadCatalogs(m_Setup.m_aChoiceConfigs, RK29_ChoiceGroupCatalog, "choice group",
			choiceSources);
		foreach (int ci, Managed choiceRaw : catalogs)
		{
			RK29_ChoiceGroupCatalog ccat = RK29_ChoiceGroupCatalog.Cast(choiceRaw);
			if (!ccat)
				continue;
			array<RK29_ChoiceGroup> groups = {};
			ccat.Collect(groups);
			foreach (RK29_ChoiceGroup g : groups)
			{
				// a *Ref is a kit's way of naming a shared group. In a catalog it would insert an
				// empty shell under no id at all, which FindChoiceGroup would then hand out in
				// place of a real definition
				string refId = g.RefId();
				if (refId != "")
				{
					Print(string.Format("[RK29] config ERROR - choice catalog %1 states a"
						+ " reference to '%2'; a catalog holds DEFINITIONS only, so the entry is"
						+ " skipped. Move the reference to the kit that wants the group",
						choiceSources[ci], refId), LogLevel.ERROR);
					continue;
				}
				m_Setup.m_aChoiceGroups.Insert(g);
			}
		}

		catalogs = LoadCatalogs(m_Setup.m_aOverrideConfigs, RK29_OverrideCatalog, "override");
		foreach (Managed overrideRaw : catalogs)
		{
			RK29_OverrideCatalog pcat = RK29_OverrideCatalog.Cast(overrideRaw);
			if (!pcat.m_aOverrides)
				continue;
			foreach (RK29_Override p : pcat.m_aOverrides)
			{
				if (p)
					m_Setup.m_aOverrides.Insert(p);
			}
		}

		catalogs = LoadCatalogs(m_Setup.m_aAttachmentConfigs, RK29_AttachmentCatalog, "attachment");
		foreach (Managed attRaw : catalogs)
		{
			RK29_AttachmentCatalog acat = RK29_AttachmentCatalog.Cast(attRaw);
			if (!acat.m_aAttachments)
				continue;
			foreach (RK29_AttachmentDef adef : acat.m_aAttachments)
			{
				if (adef)
					m_Setup.m_aAttachments.Insert(adef);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! What the phase reads as with no Round Timer present - the config fallback both phase
	//! questions hand the probe. False without a setup, which is the closed reading.
	protected bool NoTimerOpen()
	{
		if (!m_Setup)
			return false;
		return m_Setup.m_bNoTimerOpen;
	}

	//------------------------------------------------------------------------------------------------
	bool IsPreround()
	{
		m_Probe.EnsureProbed();
		return m_Probe.IsPreround(NoTimerOpen());
	}

	//------------------------------------------------------------------------------------------------
	bool IsBriefing()
	{
		m_Probe.EnsureProbed();
		return m_Probe.IsBriefing(NoTimerOpen());
	}

	//------------------------------------------------------------------------------------------------
	//! Seconds left in the current Round Timer phase; -1 without the timer, so the HUD
	//! countdown hides instead of guessing in config-fallback mode.
	int GetPhaseRemainingSeconds()
	{
		m_Probe.EnsureProbed();
		return m_Probe.GetRemainingSeconds();
	}

	// ==================================================================== server chokepoint

	//------------------------------------------------------------------------------------------------
	//! Single server entry for every kit request. Client input is never trusted: the picks are
	//! re-validated against the live offer on every resolve.
	void HandleKitRequest_S(int playerId, string kitName, string choices)
	{
		if (!Replication.IsServer())
			return;

		float nowMs = GetGame().GetWorld().GetWorldTime();
		RK29_KitStruct kit;
		if (RefuseRequest_S(playerId, kitName, nowMs, kit))
		{
			// a refusal holds the window too, or a rejected request can be retried unthrottled
			m_mLastRequestMs_S.Set(playerId, nowMs);
			return;
		}

		RK29_ClassSetup cls = m_Setup.FindClass(kitName);

		IEntity body = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(body);
		bool alive = character && character.GetCharacterController() && !character.GetCharacterController().IsDead();

		// the accepted half of the same stamp; a refusal stamped its own above
		m_mLastRequestMs_S.Set(playerId, nowMs);

		RK29_PlayerSelection sel = new RK29_PlayerSelection();
		sel.m_sKitName = kitName;
		sel.m_sIdentityUid = SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId);
		RK29_KitResolve.ParsePicks(choices, sel.m_aPicks);
		m_mSelections.Set(playerId, sel);

		// resolve once: the same resolved kit dresses the body and feeds the client's preview
		RK29_KitStruct edited;
		map<int, ref array<ref RK29_LoadedPick>> loadedMags;
		array<ref RK29_AttachmentOrder> orders;
		ResolveSelection(kit, cls, sel, edited, loadedMags, orders);

		if (alive)
			QueueLiveApply_S(playerId, character, kit, edited, orders, loadedMags);

		// deploy menu shows "Current Kit" selected; spawn re-dresses from the stash
		AssignIdentity_S(playerId, IdentityKitFor(kit.m_sFactionKey, kitName));

		// every "preview shows the wrong thing" bug came from the client re-deriving this
		NotifyKitSaved_S(playerId, sel);

		QueueRecompute_S();
	}

	//------------------------------------------------------------------------------------------------
	//! True when this request is not to be served, with the reason logged. Hands back the kit it
	//! resolved on the way through; `kit` is null whenever this returns true. Nothing is written
	//! here, the throttle included - the caller stamps that past every refusal. Neither the round
	//! phase nor a vehicle seat is a refusal, nor is the optic: a sight is an attachment entry,
	//! so the offer already answered whether the gun can mount it.
	protected bool RefuseRequest_S(int playerId, string kitName, float nowMs,
		out RK29_KitStruct kit)
	{
		kit = null;

		float lastMs;
		if (m_mLastRequestMs_S.Find(playerId, lastMs) && nowMs - lastMs < REQUEST_MIN_INTERVAL_MS)
		{
			RK29_Log.Trace(string.Format("[RK29] request throttled - player %1 asked again within"
				+ " %2 ms", playerId, REQUEST_MIN_INTERVAL_MS));
			return true;
		}

		kit = m_mKits.Get(kitName);
		if (!kit)
		{
			Print(string.Format("[RK29] request rejected - unknown kit '%1' (player %2)",
				kitName, playerId), LogLevel.WARNING);
			return true;
		}

		// the picker hides the Current Kit rows, but a crafted request must not get to stash one
		if (IsCurrentKitLoadoutName(kitName))
		{
			Print(string.Format("[RK29] request rejected - '%1' is not a requestable kit"
				+ " (player %2)", kitName, playerId), LogLevel.WARNING);
			kit = null;
			return true;
		}

		Faction playerFaction = SCR_FactionManager.SGetPlayerFaction(playerId);
		if (!playerFaction || playerFaction.GetFactionKey() != kit.m_sFactionKey)
		{
			Print(string.Format("[RK29] request rejected - kit faction mismatch"
				+ " (player %1)", playerId), LogLevel.WARNING);
			kit = null;
			return true;
		}

		array<string> offered = {};
		GetOfferedKits(kit.m_sFactionKey, offered);
		if (!offered.Contains(kitName))
		{
			Print(string.Format("[RK29] request rejected - kit not on this side's list"
				+ " (player %1)", playerId), LogLevel.WARNING);
			kit = null;
			return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Queue the re-dress of a living body and snapshot what the client needs to be put back as
	//! found.
	protected void QueueLiveApply_S(int playerId, notnull SCR_ChimeraCharacter character,
		notnull RK29_KitStruct kit, RK29_KitStruct edited,
		array<ref RK29_AttachmentOrder> orders,
		map<int, ref array<ref RK29_LoadedPick>> loadedMags)
	{
		// remember drawn-weapon slot + stance so the client can restore both after
		int heldSlot = -1;
		bool heldThrowable = false;
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
				{
					heldSlot = heldWs.GetWeaponSlotIndex();
					heldThrowable = IsThrowableSlot(heldWs);
				}
			}
		}

		// A re-kit hands out a clean loadout; preround it hands out the clean body too. Once the
		// round is live a kit change is not a free heal. Immediate: healing does not touch the
		// hands.
		bool preround = IsPreround();
		if (preround)
			RK29_KitHeal.Heal_S(character);

		// the heal just stood an incapacitated player up, so the captured stance would put them
		// back down
		if (wasDown && preround)
		{
			stance = ECharacterStance.STAND;
			dynStance = 1.0;
		}

		// hands are usually already free, so the wait normally applies on this same tick
		RK29_PendingApply pending = new RK29_PendingApply();
		pending.m_iPlayerId = playerId;
		pending.m_iGen = BumpApplyGen(playerId);
		pending.m_Kit = kit;
		pending.m_Edited = edited;
		pending.m_aOrders = orders;
		pending.m_mLoadedMags = loadedMags;
		pending.m_iStance = stance;
		pending.m_fDynStance = dynStance;
		pending.m_iHeldSlot = heldSlot;
		pending.m_bHeldThrowable = heldThrowable;
		pending.m_fDeadline = GetGame().GetWorld().GetWorldTime() + HANDS_SETTLE_TIMEOUT_MS;
		m_mPendingApply_S.Set(playerId, pending);
		ApplyWhenHandsFree_S(pending);
	}

	// ======================================================== kit building & the spawn path

	//------------------------------------------------------------------------------------------------
	//! Capture a body, then compose over it when the class says how. Shared by both boot passes,
	//! so a roster-class kit is identical to a loadout one. Null = the body would not capture.
	protected RK29_KitStruct BuildKit(string kitName, string factionKey, ResourceName body)
	{
		RK29_KitStruct kit = RK29_KitCapture.Capture(kitName, factionKey, body);
		if (!kit)
			return null;

		// a class with a composition composes from config; a kit with none (a deploy-only row) is
		// identity alone - nothing dresses from it
		RK29_ClassSetup cls = m_Setup.FindClass(kitName);
		if (!cls || cls.m_sComposition == ResourceName.Empty)
			return kit;

		RK29_KitStruct composed = RK29_KitCompose.Compose(cls, kit);
		if (!composed)
			return kit;

		// the base is what the choice groups lay over; m_mKits keeps a separate copy resolved to
		// its defaults, so no reader can reach in and edit the shared base
		m_mKitsBase.Set(kitName, composed);
		composed = composed.DeepCopy();

		// choice groups at their defaults, so every m_mKits reader sees what a choiceless request
		// resolves. Orders and loaded magazines are dropped: boot has no body to seat them on.
		array<ref RK29_ResolvedGroup> offer = {};
		RK29_KitResolve.BuildOffer(cls, m_Setup, null, offer);
		if (!offer.IsEmpty())
		{
			map<int, ref array<ref RK29_LoadedPick>> defaultLoadedMags = new map<int, ref array<ref RK29_LoadedPick>>();
			array<ref RK29_AttachmentOrder> defaultOrders;
			ResourceName defaultOptic;
			RK29_KitResolve.Apply(composed, offer, null, m_Setup, defaultLoadedMags, defaultOrders,
				defaultOptic);
		}

		Print(string.Format("[RK29] kit '%1' from CONFIG (%2 items)",
			kitName, composed.CountItems()), LogLevel.NORMAL);
		return composed;
	}

	//------------------------------------------------------------------------------------------------
	//! The base body this side spawns - vanilla's BaseLoadout, dressed as a plain soldier so a
	//! dedicated server's apply delay does not show a naked one. Config strips and re-dresses it.
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

	//------------------------------------------------------------------------------------------------
	//! Create and store this player's first selection from the side default; null when the side
	//! has no kits.
	protected RK29_PlayerSelection SeedDefaultSelection_S(int playerId, string factionKey)
	{
		if (factionKey == "")
			return null;

		string kitName = DefaultKit(factionKey);
		if (kitName == "" || !m_mKits.Contains(kitName))
			return null;

		// no picks: every offered group at its default, exactly what an untouched picker row
		// sends
		RK29_PlayerSelection sel = new RK29_PlayerSelection();
		sel.m_sKitName = kitName;
		sel.m_sIdentityUid = SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId);

		m_mSelections.Set(playerId, sel);

		// the seed is a selection change like any other, and the client has to hear about it
		NotifyKitSaved_S(playerId, sel);

		RK29_Log.Trace(string.Format("[RK29] player %1 had no kit - started on '%2'",
			playerId, kitName));
		return sel;
	}

	//------------------------------------------------------------------------------------------------
	//! The stash the spawn may actually dress from, re-seeded when it no longer fits. A selection
	//! belongs to the side it was applied on and nothing clears it when the side changes, so an
	//! unfiltered spawn was the one place the row and the body could disagree. Dropping a stale
	//! stash rather than ignoring it is what re-runs the confirmation RPC and corrects the
	//! client's copy.
	protected RK29_PlayerSelection UsableSelection_S(int playerId, string factionKey)
	{
		RK29_PlayerSelection sel = m_mSelections.Get(playerId);

		// leave a good stash alone rather than lose it to a caller that could not name a side
		if (factionKey == "")
			return sel;

		if (sel)
		{
			// EffectiveKitFor answers with the stash only when it is still offered on this side,
			// so this equality is the offered-on-this-side test
			if (EffectiveKitFor(playerId, factionKey) == sel.m_sKitName)
				return sel;

			RK29_Log.Trace(string.Format("[RK29] player %1 stash '%2' does not belong to side '%3'"
				+ " (or is no longer a kit on this side) - re-seeding",
				playerId, sel.m_sKitName, factionKey));

			m_mSelections.Remove(playerId);
		}

		return SeedDefaultSelection_S(playerId, factionKey);
	}

	//------------------------------------------------------------------------------------------------
	//! Dress a body straight from the stash, from RK29_CurrentKitLoadout.OnLoadoutSpawned: the
	//! body arrives bare, so there is nothing to strip. True means the spawn handler leaves it
	//! alone. `outLogged` comes back true on the one refusal path that already named its reason,
	//! so the caller does not print a second, vaguer line over it.
	bool ApplyStashOnSpawn_S(int playerId, IEntity body, string factionKey, out bool outLogged)
	{
		outLogged = false;
		if (!Replication.IsServer() || !body)
			return false;

		// Seed a real selection, not just a dress, so the HUD count, the picker and the respawn
		// identity agree
		RK29_PlayerSelection sel = UsableSelection_S(playerId, factionKey);
		if (!sel)
			return false;

		RK29_KitStruct kit = m_mKits.Get(sel.m_sKitName);
		if (!kit)
			return false;

		RK29_KitStruct edited;
		map<int, ref array<ref RK29_LoadedPick>> loadedMags;
		array<ref RK29_AttachmentOrder> orders;
		ResolveSelection(kit, m_Setup.FindClass(sel.m_sKitName), sel, edited, loadedMags, orders);

		array<ResourceName> droppedItems;
		if (!RK29_KitApply.Apply_S(body, edited, droppedItems, loadedMags, orders))
		{
			// a body with no inventory to dress: nothing applied, so nothing claimed
			Print(string.Format("[RK29] spawn apply failed hard for player %1 - body left as"
				+ " spawned", playerId), LogLevel.ERROR);
			outLogged = true;
			return false;
		}
		NotifyDropped_S(playerId, droppedItems);
		// nothing stamps the body here - ApplyTraits_S is the one owner of the stamp

		RequestSpawnDraw_S(playerId, body);

		m_mSpawnApplied_S.Set(playerId, BumpApplyGen(playerId));
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Ask the owner to draw the fresh body's primary at possession. not done server-side: in-hands
	//! is controller state the owning client rebuilds when it takes the body, so a server equip in
	//! the spawn pass never held on a dedicated server. Vanilla needs neither because its prefabs
	//! carry a weapon in slot 0 with DefaultWeaponIndex 0, so every machine's engine arms the body
	//! at init; our body is the side's BaseLoadout with slot 0 empty.
	protected void RequestSpawnDraw_S(int playerId, notnull IEntity body)
	{
		SCR_ChimeraCharacter spawnChar = SCR_ChimeraCharacter.Cast(body);
		if (!spawnChar)
			return;

		CharacterControllerComponent ctrl = spawnChar.GetCharacterController();
		if (!ctrl)
			return;

		IEntity primary = PrimaryWeaponOf(ctrl);
		if (!primary)
			return;

		SCR_PlayerController pc = PlayerControllerOf(playerId);
		if (pc)
			pc.RK29_NotifySpawnDraw_S(RplIdOf(primary), RplIdOf(body));
	}

	//------------------------------------------------------------------------------------------------
	//! Slot 0 is the primary by kit-config convention. Same slot fallback vanilla's
	//! ApplyCharacterDataLoadoutString uses when the saved slot is gone. Gun slots only:
	//! GetWeaponsSlots lists the grenade and throwable slots too, and Character_Base declares them
	//! BEFORE the gun slots, so without the index test a kit with no gun would draw a grenade
	//! (vanilla's own restore filters the same way, SCR_PlayerArsenalLoadout: slotIdx < 3).
	protected IEntity PrimaryWeaponOf(CharacterControllerComponent ctrl)
	{
		BaseWeaponManagerComponent wm = ctrl.GetWeaponManagerComponent();
		if (!wm)
			return null;

		array<WeaponSlotComponent> slots = {};
		wm.GetWeaponsSlots(slots);

		IEntity weapon;
		foreach (WeaponSlotComponent slot : slots)
		{
			if (!slot || !slot.GetWeaponEntity())
				continue;
			if (slot.GetWeaponSlotIndex() >= FIRST_THROWABLE_SLOT)
				continue;
			if (slot.GetWeaponSlotIndex() == 0)
				return slot.GetWeaponEntity();
			if (!weapon)
				weapon = slot.GetWeaponEntity();
		}

		return weapon;
	}

	//------------------------------------------------------------------------------------------------
	//! Strip a body only once its hands are free. Every vanilla path that mutates a character's
	//! inventory refuses while an item action is in flight (CanMoveItem and InsertItem gate on
	//! IsAnimationReady()/IsInventoryLocked(), the equip/drop actions on IsChangingItem()), and
	//! an item change cannot be interrupted. Checked here because the owner drives its own
	//! commands and the server learns of them late. Re-scheduled one frame at a time: Remove() is
	//! keyed by function, so a repeating call cancelled for one player would cancel the rest.
	//! Unconfirmed fix: adopted on vanilla's rule, aimed at the still-open launcher-swap bug.
	protected void ApplyWhenHandsFree_S(RK29_PendingApply pending)
	{
		if (!pending)
			return;

		// a newer apply or a respawn has claimed this body, and owns the pending-map entry too
		int current;
		if (!m_mApplyGen_S.Find(pending.m_iPlayerId, current) || current != pending.m_iGen)
		{
			// unless nothing newer took the entry over - a respawn retires an apply without
			// queueing one
			if (m_mPendingApply_S.Get(pending.m_iPlayerId) == pending)
				m_mPendingApply_S.Remove(pending.m_iPlayerId);
			return;
		}

		// past here we are the current pending for this player; the map entry is ours to drop
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(
			GetGame().GetPlayerManager().GetPlayerControlledEntity(pending.m_iPlayerId));
		CharacterControllerComponent ctrl;
		if (character)
			ctrl = character.GetCharacterController();
		if (!ctrl || ctrl.IsDead())
		{
			m_mPendingApply_S.Remove(pending.m_iPlayerId);
			return;
		}

		// a gadget still in the hands counts as busy: it is engine-attached where the strip
		// cannot see it, so applying over it leaves the old compass beside the one the kit brings
		bool busy = ctrl.IsChangingItem() || ctrl.IsPlayingGesture() || ctrl.IsUsingItem()
			|| ctrl.IsGadgetInHands() || ctrl.IsWeaponADS();
		if (busy && GetGame().GetWorld().GetWorldTime() < pending.m_fDeadline)
		{
			GetGame().GetCallqueue().CallLater(ApplyWhenHandsFree_S, 0, false, pending);
			return;
		}

		m_mPendingApply_S.Remove(pending.m_iPlayerId);
		if (busy)
			Print(string.Format("[RK29] hands never came free on the server - applying anyway"
				+ " (player %1)", pending.m_iPlayerId), LogLevel.WARNING);

		ApplyPending_S(pending, character);
	}

	//------------------------------------------------------------------------------------------------
	//! The re-dress itself. Everything here is ordered after the apply: the announcement must
	//! never precede the gear, and the gadget manager only learns of the radio once it is
	//! inserted.
	protected void ApplyPending_S(notnull RK29_PendingApply pending,
		notnull SCR_ChimeraCharacter character)
	{
		// a body Place refuses (no storage manager) was neither stripped nor dressed, so there is
		// nothing to announce or re-tune - but Apply_S emptied the hands before asking, so the
		// restore below still runs: the old weapon is intact and goes back into the hand. Place
		// has already logged the refusal
		array<ResourceName> droppedItems;
		bool applied = RK29_KitApply.Apply_S(character, pending.m_Edited, droppedItems,
			pending.m_mLoadedMags, pending.m_aOrders);
		if (applied)
		{
			NotifyDropped_S(pending.m_iPlayerId, droppedItems);

			// mid-round the re-kit is announced rather than refused, and only for a body that got
			// dressed; the phase is read here because that is the moment the gear changed
			if (!IsPreround())
			{
				Print(string.Format("[RK29] live re-kit with the round live - '%1' (player %2)",
					pending.m_Kit.m_sKitName, pending.m_iPlayerId), LogLevel.NORMAL);
				SCR_NotificationsComponent.SendToEveryone(ENotification.RK29_LIVE_REKIT, pending.m_iPlayerId);
			}

			// the radio the dress just spawned sits on its authored channel and vanilla re-tunes
			// nothing acquired later. The spawn path needs none of this - see RK29_GroupsManager.c
			SCR_GroupsManagerComponent groups = SCR_GroupsManagerComponent.GetInstance();
			if (groups)
				groups.RK29_TuneToGroupFrequency_S(pending.m_iPlayerId, character);
		}

		SCR_PlayerController pc = PlayerControllerOf(pending.m_iPlayerId);
		if (pc)
			pc.RK29_NotifyRestoreState_S(pending.m_iStance, pending.m_fDynStance,
				DrawTargetAfterApply(character, pending.m_iHeldSlot, pending.m_bHeldThrowable),
				RplIdOf(character));
	}

	//------------------------------------------------------------------------------------------------
	//! What the owner draws after a live re-kit: the weapon now in the slot that was drawn, else -
	//! when the new kit seats nothing in a GUN slot that was drawn (launcher drawn, re-kit to a
	//! kit without one) - the primary, the same fallback the spawn path takes. An invalid id is
	//! what the owner answers with a deliberate unarmed state. It is reserved for a hand that held
	//! no weapon slot (-1: empty, a raised gadget) and for a held throwable the new kit does not
	//! carry: a grenade in hand reports its own slot, not -1, and putting a rifle into a hand that
	//! was holding a grenade is not a restore. Throwable-ness travels as a flag because the
	//! grenade slot and the hand-weapon slot share index 3 on Character_Base.
	protected RplId DrawTargetAfterApply(notnull SCR_ChimeraCharacter character, int heldSlot,
		bool heldThrowable)
	{
		if (heldSlot < 0)
			return RplId.Invalid();

		RplId inSlot = WeaponRplIdInSlot(character, heldSlot, heldThrowable);
		if (inSlot.IsValid())
			return inSlot;
		if (heldThrowable)
			return RplId.Invalid();

		CharacterControllerComponent ctrl = character.GetCharacterController();
		if (!ctrl)
			return RplId.Invalid();
		return RplIdOf(PrimaryWeaponOf(ctrl));
	}

	//------------------------------------------------------------------------------------------------
	protected int BumpApplyGen(int playerId)
	{
		int gen;
		m_mApplyGen_S.Find(playerId, gen);
		gen++;
		m_mApplyGen_S.Set(playerId, gen);
		return gen;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnPlayerSpawned_S(int playerId, IEntity entity)
	{
		if (!Replication.IsServer() || s_Instance != this)
			return;

		// Every spawn: a mutation queued against the previous body must not dress the new one
		int spawnGen = BumpApplyGen(playerId);

		// "Current Kit" is the only entry that dresses from the stash, on a bare body in
		// OnLoadoutSpawned. Any other entry is a request for that loadout as authored; stock
		// spawns are never mutated, and neither is a body that never came out of the loadout at
		// all.
		bool dressedBySpawnHook = m_mSpawnApplied_S.Contains(playerId);
		m_mSpawnApplied_S.Remove(playerId);

		// Diagnostic check on the spawn draw, a LOG and not a fix - a corrective draw this
		// long after the spawn is worthless. This path once failed silently and cost a session of
		// guessing.
		if (dressedBySpawnHook)
			GetGame().GetCallqueue().CallLater(VerifyDrawn_S, DRAW_VERIFY_MS, false, playerId, spawnGen);

		// A stock spawn keeps its gear as authored, but a role's qualifications are config-owned
		// and selections live in server memory only - so the first spawn of a session is a stock
		// one, and a medic would bandage at rifleman speed until they opened the picker.
		if (!dressedBySpawnHook)
		{
			// a body that arrived under the Current Kit row without this manager dressing it is
			// not a bare side body, whatever the row's capture says; stamping it would strip its
			// qualifications
			string spawnedName = CurrentLoadoutName(playerId);
			RK29_KitStruct spawnedKit;
			if (!IsCurrentKitLoadoutName(spawnedName))
				spawnedKit = m_mKits.Get(spawnedName);
			if (spawnedKit && entity)
				RK29_KitApply.ApplyTraits_S(entity, spawnedKit);
		}

		QueueRecompute_S();
	}

	//------------------------------------------------------------------------------------------------
	//! Did the spawn draw hold? Log-only: empty hands mean both halves failed, and a draw this
	//! late fixes nothing. Gated on the apply generation; a dead or mounted player is skipped.
	protected void VerifyDrawn_S(int playerId, int gen)
	{
		if (!Replication.IsServer())
			return;

		int currentGen;
		if (!m_mApplyGen_S.Find(playerId, currentGen) || currentGen != gen)
			return;

		IEntity body = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(body);
		if (!character || character.IsInVehicle())
			return;

		CharacterControllerComponent ctrl = character.GetCharacterController();
		if (!ctrl || ctrl.IsDead() || ctrl.IsUnconscious())
			return;

		BaseWeaponManagerComponent wm = ctrl.GetWeaponManagerComponent();
		if (wm && wm.GetCurrentWeapon())
			return;

		Print(string.Format("[RK29] spawn equip did not hold for player %1 - hands still empty"
			+ " %2s after spawning. Not correcting (too late to matter); investigate the equip"
			+ " path", playerId, DRAW_VERIFY_MS / 1000), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	//! This player's controller as ours, or null - the ordinary state for a player
	//! mid-disconnect.
	protected static SCR_PlayerController PlayerControllerOf(int playerId)
	{
		return SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
	}

	//------------------------------------------------------------------------------------------------
	//! Tell the client what the server settled on. Every selection change comes through here -
	//! apply, seed, rejoin - or the client's stash stays empty and its mannequin composes
	//! locally.
	protected void NotifyKitSaved_S(int playerId, notnull RK29_PlayerSelection sel)
	{
		SCR_PlayerController pc = PlayerControllerOf(playerId);
		if (pc)
			pc.RK29_NotifyKitSaved_S(sel.m_sKitName, RK29_KitResolve.EncodePicks(sel.m_aPicks));
	}

	//------------------------------------------------------------------------------------------------
	protected static RplId RplIdOf(IEntity entity)
	{
		if (!entity)
			return RplId.Invalid();

		RplComponent rpl = RplComponent.Cast(entity.FindComponent(RplComponent));
		if (!rpl)
			return RplId.Invalid();

		return rpl.Id();
	}

	//------------------------------------------------------------------------------------------------
	//! Network id of the weapon now in `slotIndex`, so the client waits for that entity rather
	//! than for a slot to look occupied: the previous weapon may not be reaped client-side yet,
	//! and the same prefab handed back is indistinguishable by name. `throwable` picks which of
	//! the two slots sharing an index is meant (grenade vs hand-weapon, both 3).
	protected static RplId WeaponRplIdInSlot(IEntity character, int slotIndex, bool throwable)
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
			if (!slot || slot.GetWeaponSlotIndex() != slotIndex || IsThrowableSlot(slot) != throwable)
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

	//------------------------------------------------------------------------------------------------
	//! The two vanilla grenade slot classes derive from WeaponSlotComponent directly, so both casts.
	protected static bool IsThrowableSlot(notnull WeaponSlotComponent slot)
	{
		return CharacterGrenadeSlotComponent.Cast(slot) != null
			|| GrenadeSlotComponent.Cast(slot) != null;
	}

	//------------------------------------------------------------------------------------------------
	//! One item per line, by the user's rule for every list of gear the player is shown - the menu's
	//! overflow tip reads the same way. Same prefab dropped N times folds to "Nx Name" so the
	//! hint stays one line per item rather than one per copy.
	protected void NotifyDropped_S(int playerId, array<ResourceName> droppedItems)
	{
		if (!droppedItems || droppedItems.IsEmpty())
			return;
		SCR_PlayerController pc = PlayerControllerOf(playerId);
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
				itemList += "\n";
			int n = counts.Get(name);
			if (n > 1)
				itemList += n.ToString() + "x " + name;
			else
				itemList += name;
		}
		pc.RK29_NotifyItemsDropped_S(droppedItems.Count(), itemList);
	}

	// ================================================================= resolution & preview

	//------------------------------------------------------------------------------------------------
	//! Selection -> edited struct + everything the apply pass has to hang off the guns.
	//! loadedMags is always a map, empty when nothing was chosen; each weapon slot keys a list,
	//! because an underbarrel launcher is a second chamber on the same gun. orders is the one
	//! attachment channel - optics, bayonets and Nones alike - each bound to the weapon slot of
	//! the gun whose definition owns the group; three parallel outputs could only speak about the
	//! primary, which is why a SMAW in slot 1 never got its sight. resolvedOptic is for the
	//! client preview only.
	protected void ResolveSelection(RK29_KitStruct kit, RK29_ClassSetup cls, RK29_PlayerSelection sel,
		out RK29_KitStruct edited,
		out map<int, ref array<ref RK29_LoadedPick>> loadedMags,
		out array<ref RK29_AttachmentOrder> orders,
		out ResourceName resolvedOptic = ResourceName.Empty)
	{
		orders = {};
		resolvedOptic = ResourceName.Empty;
		loadedMags = new map<int, ref array<ref RK29_LoadedPick>>();

		RK29_KitStruct base = m_mKitsBase.Get(kit.m_sKitName);
		if (!base)
			base = kit;

		// the copy is the point: the group path mutates what it is handed, and the base is shared
		edited = base.DeepCopy();

		array<ref RK29_ResolvedGroup> offer = {};
		// the claim Apply just made, asked again: the irons-only guarantee speaks per slot
		map<string, int> weaponIdSlots = new map<string, int>();
		if (cls)
		{
			RK29_KitResolve.BuildOffer(cls, m_Setup, sel.m_aPicks, offer);
			if (!offer.IsEmpty())
			{
				RK29_KitResolve.Apply(edited, offer, sel.m_aPicks, m_Setup, loadedMags, orders,
					resolvedOptic);

				map<string, int> groupSlots = new map<string, int>();
				RK29_KitResolve.BuildWeaponSlotMap(offer, sel.m_aPicks, m_Setup,
					edited.m_sFactionKey, groupSlots, weaponIdSlots);
			}
		}

		AddIronsOnlyOrders(offer, weaponIdSlots, orders);
	}

	//------------------------------------------------------------------------------------------------
	//! The irons-only guarantee, per weapon slot. A gun whose offer names no optics group for it
	//! gets an unqualified None on its own slot, stripping whatever sight its prefab spawned
	//! with, so config stays the whole truth. Coverage is per slot because ownership is: one
	//! kit-wide "somebody offers optics" bool read a launcher's group as covering the primary.
	protected void AddIronsOnlyOrders(notnull array<ref RK29_ResolvedGroup> offer,
		notnull map<string, int> weaponIdSlots, notnull array<ref RK29_AttachmentOrder> orders)
	{
		array<int> claimedSlots = {};
		for (int i = 0, n = weaponIdSlots.Count(); i < n; i++)
		{
			int claimed = weaponIdSlots.GetElement(i);
			if (!claimedSlots.Contains(claimed))
				claimedSlots.Insert(claimed);
		}
		// an offer that claimed no slot is a kit whose weapons came from its composition, and the
		// primary is still the one gun it can have an opinion about
		if (claimedSlots.IsEmpty())
			claimedSlots.Insert(0);

		array<int> coveredSlots = {};
		foreach (RK29_ResolvedGroup opticGroup : offer)
		{
			if (!opticGroup || !opticGroup.m_bIsOpticsPoint)
				continue;

			int coveredSlot;
			if (!CoveredSlotOf(opticGroup, weaponIdSlots, coveredSlot))
				continue;

			if (!coveredSlots.Contains(coveredSlot))
				coveredSlots.Insert(coveredSlot);
		}

		foreach (int weaponSlot : claimedSlots)
		{
			if (coveredSlots.Contains(weaponSlot))
				continue;

			RK29_AttachmentOrder ironsOnly = new RK29_AttachmentOrder();
			ironsOnly.m_iOwnerSlot = weaponSlot;
			orders.Insert(ironsOnly);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Which body slot an optics group speaks for; false when it speaks for nothing. An ownerless
	//! group means slot 0, an owner that claimed no slot covers nothing, and the irons-only
	//! guarantee and the magnified tally must run on this one reading.
	protected bool CoveredSlotOf(notnull RK29_ResolvedGroup g, notnull map<string, int> weaponIdSlots,
		out int slot)
	{
		slot = 0;
		if (g.m_sOwnerWeapon == "")
			return true;

		int ownerSlot;
		if (!weaponIdSlots.Find(g.m_sOwnerWeapon, ownerSlot))
			return false;

		slot = ownerSlot;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Client-side preview only - nothing is stored and nothing is sent; the selection it builds
	//! never reaches m_mSelections, the stash or the wire. It answers on a client with no round
	//! trip because m_mKits, m_mKitsBase and m_Setup are built on every machine at boot. The
	//! orders and loaded magazines come back out because the mannequin is dressed by the real
	//! apply pass and then weighed.
	bool RK29_ResolvePreviewKit(string kitName, array<ref RK29_ChoicePick> picks,
		out RK29_KitStruct edited, out ResourceName previewOptic,
		out array<ref RK29_AttachmentOrder> orders,
		out map<int, ref array<ref RK29_LoadedPick>> loadedMags)
	{
		edited = null;
		previewOptic = ResourceName.Empty;
		orders = null;
		loadedMags = null;

		if (!m_Setup || kitName == "")
			return false;

		RK29_KitStruct kit = m_mKits.Get(kitName);
		if (!kit)
			return false;

		// copied rather than aliased, so the resolver cannot hand the menu's live array on
		RK29_PlayerSelection sel = new RK29_PlayerSelection();
		sel.m_sKitName = kitName;
		if (picks)
		{
			foreach (RK29_ChoicePick pick : picks)
			{
				if (pick)
					sel.m_aPicks.Insert(pick);
			}
		}

		map<int, ref array<ref RK29_LoadedPick>> resolvedMags;
		array<ref RK29_AttachmentOrder> resolvedOrders;
		ResolveSelection(kit, m_Setup.FindClass(kitName), sel, edited, resolvedMags, resolvedOrders,
			previewOptic);
		orders = resolvedOrders;
		loadedMags = resolvedMags;
		return edited != null;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsCurrentKitLoadoutName(string loadoutName)
	{
		if (loadoutName == "")
			return false;
		return m_aCurrentKitNames.Contains(loadoutName);
	}

	//------------------------------------------------------------------------------------------------
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

	//------------------------------------------------------------------------------------------------
	//! Assigned loadout, with "Current Kit" resolved to the stashed kit. A stash from
	//! another faction (player switched sides) never resolves.
	protected string WornKitName(int playerId)
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

	//------------------------------------------------------------------------------------------------
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

	//------------------------------------------------------------------------------------------------
	//! The kit Current Kit would spawn for this player on this side, stash or not - a player who
	//! never opened the picker still deploys through it. Order: the stash when it is this
	//! faction's, then the side's configured default, then the side's first kit; "" only when the
	//! side has no kits. The authority answers from m_mSelections; a client has none and resolves
	//! through the default path. The id check matters because on a listen host the authority runs
	//! this same code.
	string EffectiveKitFor(int playerId, string factionKey)
	{
		string stash;
		RK29_PlayerSelection sel = m_mSelections.Get(playerId);
		if (sel)
			stash = sel.m_sKitName;

		if (stash != "")
		{
			RK29_KitStruct kit = m_mKits.Get(stash);
			if (kit && (factionKey == "" || kit.m_sFactionKey == factionKey))
			{
				array<string> stashOffered = {};
				GetOfferedKits(kit.m_sFactionKey, stashOffered);
				if (stashOffered.Contains(stash))
					return stash;
			}
		}

		// without a faction there is no side to default on, so the caller must name one
		if (factionKey == "")
			return "";

		return DefaultKit(factionKey);
	}

	//------------------------------------------------------------------------------------------------
	//! The client-side half of the test UsableSelection_S makes on the server: a stash that
	//! outlived a side change still names a real kit, so both read sites ask this before trusting
	//! it.
	bool IsKitOffered(string kitName)
	{
		RK29_KitStruct kit = m_mKits.Get(kitName);
		if (!kit)
			return false;
		array<string> offered = {};
		GetOfferedKits(kit.m_sFactionKey, offered);
		return offered.Contains(kitName);
	}

	//------------------------------------------------------------------------------------------------
	//! The side's starting kit, falling back to its first so there is always a spawnable answer.
	protected string DefaultKit(string factionKey)
	{
		array<string> offered = {};
		GetOfferedKits(factionKey, offered);
		if (offered.IsEmpty())
			return "";

		string configured = m_Setup.DefaultKitName(factionKey);
		if (configured != "" && offered.Contains(configured))
			return configured;

		if (configured != "" && !m_aDefaultlessFactionsNoted.Contains(factionKey))
		{
			m_aDefaultlessFactionsNoted.Insert(factionKey);
			RK29_Log.Trace(string.Format("[RK29] side '%1' default kit '%2' is not a kit on this"
				+ " side - Current Kit starts on '%3' instead",
				factionKey, configured, offered[0]));
		}

		return offered[0];
	}

	// ===================================================================== player lifecycle

	//------------------------------------------------------------------------------------------------
	//! The player lifecycle, off the game mode's own invokers. Subscribed rather than forwarded,
	//! so the shared game mode need not know which manager is live. Server only, outside shadow
	//! mode.
	protected void SubscribeLifecycle_S()
	{
		SCR_BaseGameMode gm = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (!gm)
		{
			Print("[RK29] no game mode at boot - lifecycle events will not reach the manager",
				LogLevel.ERROR);
			return;
		}

		if (gm.GetOnPlayerSpawned())
			gm.GetOnPlayerSpawned().Insert(OnPlayerSpawned_S);
		if (gm.GetOnPlayerKilled())
			gm.GetOnPlayerKilled().Insert(OnPlayerKilled_S);
		if (gm.GetOnPlayerDeleted())
			gm.GetOnPlayerDeleted().Insert(OnPlayerDeleted_S);
		if (gm.GetOnPlayerDisconnected())
			gm.GetOnPlayerDisconnected().Insert(OnPlayerDisconnected_Event_S);
		if (gm.GetOnPlayerAuditSuccess())
			gm.GetOnPlayerAuditSuccess().Insert(OnPlayerAuditSuccess_S);

		// a side change is announced nowhere the game mode's invokers reach - the one gap the
		// deleted recompute poll covered
		SCR_FactionManager fm = SCR_FactionManager.Cast(GetGame().GetFactionManager());
		if (fm && fm.GetOnPlayerFactionChanged_S())
			fm.GetOnPlayerFactionChanged_S().Insert(OnPlayerFactionChanged_S);
	}

	//------------------------------------------------------------------------------------------------
	//! A side change moves a player between tallies, and the stash stops being usable -
	//! UsableSelection_S.
	void OnPlayerFactionChanged_S(int playerId, SCR_PlayerFactionAffiliationComponent comp,
		Faction faction)
	{
		if (s_Instance != this)
			return;

		QueueRecompute_S();
	}

	//------------------------------------------------------------------------------------------------
	//! A controlled body reaped without a death takes its player out of the alive tally.
	protected void OnPlayerDeleted_S(int playerId, IEntity player)
	{
		if (s_Instance != this)
			return;

		QueueRecompute_S();
	}

	//------------------------------------------------------------------------------------------------
	//! Player ids can be recycled - a newcomer must never inherit a stranger's stash - so the
	//! leaver's stash parks under their identity uid for reconnect (bodies persist).
	protected void OnPlayerDisconnected_Event_S(int playerId, KickCauseCode cause, int timeout)
	{
		if (!Replication.IsServer() || s_Instance != this)
			return;

		RK29_PlayerSelection sel = m_mSelections.Get(playerId);
		m_mSelections.Remove(playerId);
		// the generation is bumped, not dropped: a draw check still queued for this id compares
		// against it, and a newcomer on the same id must not count back up to it
		int retiredGen;
		if (m_mApplyGen_S.Find(playerId, retiredGen))
			m_mApplyGen_S.Set(playerId, retiredGen + 1);
		m_mPendingApply_S.Remove(playerId);
		m_mLastRequestMs_S.Remove(playerId);
		m_mSpawnApplied_S.Remove(playerId);
		if (sel && sel.m_sIdentityUid != "")
			m_mParkedSelections.Set(sel.m_sIdentityUid, sel);

		QueueRecompute_S();
	}

	//------------------------------------------------------------------------------------------------
	//! A death changes the alive tally and nothing else here covers it. Guarded on the current
	//! instance because a ScriptInvoker holds the object it was given and Boot replaces
	//! s_Instance per world.
	protected void OnPlayerKilled_S(notnull SCR_InstigatorContextData instigatorContextData)
	{
		if (s_Instance != this)
			return;

		QueueRecompute_S();
	}

	//------------------------------------------------------------------------------------------------
	//! Reconnect: restore a parked stash to the (possibly new) player id. The deferred
	//! re-assignment covers rejoin-on-body, where the assignment died with the old controller.
	protected void OnPlayerAuditSuccess_S(int playerId)
	{
		if (!Replication.IsServer() || s_Instance != this)
			return;

		string uid = SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId);
		if (uid == "")
			return;

		RK29_PlayerSelection sel = m_mParkedSelections.Get(uid);
		if (!sel)
			return;
		m_mParkedSelections.Remove(uid);
		m_mSelections.Set(playerId, sel);

		NotifyKitSaved_S(playerId, sel);

		GetGame().GetCallqueue().CallLater(RestoreRejoinIdentity, 2000, false, playerId, uid);
	}

	//------------------------------------------------------------------------------------------------
	//! `uid` is who this deferral was scheduled for. Player ids recycle, so 2s later the id may
	//! belong to somebody else entirely - re-check before touching their loadout identity.
	protected void RestoreRejoinIdentity(int playerId, string uid)
	{
		if (SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId) != uid)
			return;

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
		QueueRecompute_S();
	}

	//------------------------------------------------------------------------------------------------
	//! Loadout to record the player under after an apply: normally the faction's "Current Kit"
	//! entry, which is what makes the stash survive a respawn. Without one the stock entry keeps
	//! the class but loses the stash on death - loud, because the symptom shows up a death later.
	protected string IdentityKitFor(string factionKey, string kitName)
	{
		string identityKit = CurrentKitLoadoutName(factionKey);
		if (identityKit != "")
			return identityKit;

		Print(string.Format("[RK29] config WARNING - faction '%1' has no Current Kit loadout;"
			+ " customization will not survive respawn", factionKey), LogLevel.WARNING);
		return kitName;
	}

	//------------------------------------------------------------------------------------------------
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

		plc.RK29_AssignLoadout_S(loadout);
	}

	// ============================================================================= counting

	//------------------------------------------------------------------------------------------------
	//! Counts are rebuilt on the edge into briefing, the one moment no player event marks. The
	//! edge only: a rebuild per tick during briefing would cost a full offer build per living
	//! player every second, and the player events already cover every change inside the phase.
	protected void WatchPhase_S()
	{
		if (!Replication.IsServer() || s_Instance != this)
			return;

		bool briefing = IsBriefing();
		if (briefing && !m_bWasBriefing_S)
			QueueRecompute_S();
		m_bWasBriefing_S = briefing;
	}

	//------------------------------------------------------------------------------------------------
	//! One rebuild per frame however many events asked for it - see m_bRecomputeQueued_S.
	protected void QueueRecompute_S()
	{
		if (!Replication.IsServer() || m_bRecomputeQueued_S)
			return;
		m_bRecomputeQueued_S = true;
		GetGame().GetCallqueue().CallLater(Recompute_S, 0, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Always rebuilds from scratch - never increment counters.
	protected void Recompute_S()
	{
		m_bRecomputeQueued_S = false;
		if (!Replication.IsServer())
			return;

		// Nobody can see these outside briefing and they are not cheap: the magnified tally costs
		// a full BuildOffer per living player, and a death is what most often triggers it in a
		// live round. Nothing is cleared on the way out; the first pass after briefing returns
		// overwrites it.
		if (!IsBriefing())
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

			int idx = KitIndexByName(WornKitName(pid));
			if (idx < 0 || idx >= n || m_aIndexToKit[idx] == "")
				continue;

			alive[idx] = alive[idx] + 1;

			// the tally counts optics, not men: one player can be carrying two magnified sights
			magnified[idx] = magnified[idx] + MagnifiedCount(pid, m_aIndexToKit[idx]);
		}

		gm.RK29_SetCounts_S(alive, magnified);
	}

	//------------------------------------------------------------------------------------------------
	//! How many magnified optics this player's kit seats - a count, not a flag. A player who
	//! picked nothing answers with the offered groups' defaults. Counted per weapon slot, not per
	//! group: several groups can offer glass for one seat, and OR-ing them onto the seat stops
	//! one rail counting twice. Whether a seat's glass counts is IsMagnifiedEntry's answer and
	//! only its.
	protected int MagnifiedCount(int playerId, string kitName)
	{
		RK29_ClassSetup cls = m_Setup.FindClass(kitName);
		if (!cls)
			return 0;

		array<ref RK29_ChoicePick> picks = null;
		RK29_PlayerSelection sel = m_mSelections.Get(playerId);
		if (sel && sel.m_sKitName == kitName)
			picks = sel.m_aPicks;

		array<ref RK29_ResolvedGroup> offer = {};
		RK29_KitResolve.BuildOffer(cls, m_Setup, picks, offer);
		if (offer.IsEmpty())
			return 0;

		// an optics group names its owner by weapon id and a seat is a body slot, so both go
		// through one walk
		map<string, int> groupSlots = new map<string, int>();
		map<string, int> weaponIdSlots = new map<string, int>();
		RK29_KitResolve.BuildWeaponSlotMap(offer, picks, m_Setup, cls.m_sSideFactionKey,
			groupSlots, weaponIdSlots);

		array<int> magnifiedSlots = {};
		foreach (RK29_ResolvedGroup g : offer)
		{
			if (!g || !g.m_bIsOpticsPoint)
				continue;

			int slot;
			if (!CoveredSlotOf(g, weaponIdSlots, slot))
				continue;
			if (magnifiedSlots.Contains(slot))
				continue;

			RK29_ResolvedEntry chosen = RK29_KitResolve.PickedEntry(g, picks);
			if (!chosen)
				continue;

			if (RK29_KitResolve.IsMagnifiedEntry(m_Setup, g, chosen))
				magnifiedSlots.Insert(slot);
		}

		return magnifiedSlots.Count();
	}

	// =========================================================================== body stamp

	//------------------------------------------------------------------------------------------------
	//! The kit index for a name, for the apply pass to put on the body. -1 when the manager is
	//! not up or the name is not a kit, which is exactly the value that means "nothing stamped".
	static int KitIndexOf(string kitName)
	{
		RK29_KitManager mgr = GetInstance();
		if (!mgr)
			return -1;
		return mgr.KitIndexByName(kitName);
	}

	//------------------------------------------------------------------------------------------------
	//! Stamp one body from the kit index it carries. The client path: a machine that is not the
	//! server cannot work out what anyone is wearing, so the body tells it. Takes the component,
	//! not the entity.
	void StampEditableByIndex(SCR_EditableEntityComponent editable, int kitIndex)
	{
		if (!editable || kitIndex < 0 || kitIndex >= m_aIndexToKit.Count())
			return;

		RK29_KitStruct kit = m_mKits.Get(m_aIndexToKit[kitIndex]);
		if (kit)
			StampBody(editable, kit);
	}

	//------------------------------------------------------------------------------------------------
	//! Take the registration hook if SCR_EditableEntityCore is up yet - it is where a character
	//! announces itself on every machine, which is what a local stamp needs. Safe to call
	//! repeatedly.
	protected void SubscribeStamp()
	{
		if (m_bStampSubscribed)
			return;

		SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(
			SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
		if (!core || !core.Event_OnEntityRegistered)
			return;

		core.Event_OnEntityRegistered.Insert(OnEditableRegistered);
		m_bStampSubscribed = true;
	}

	//------------------------------------------------------------------------------------------------
	//! A body appearing on this machine, stamped from what it already carries. The wire's
	//! callback covers a kit changing; this covers a body arriving with the answer already set,
	//! since streaming in is only promised to synchronise state.
	protected void OnEditableRegistered(SCR_EditableEntityComponent editable)
	{
		if (s_Instance != this || !editable)
			return;

		SCR_EditableCharacterComponent character = SCR_EditableCharacterComponent.Cast(editable);
		if (!character)
			return;

		StampEditableByIndex(character, character.RK29_GetKitIndex());
	}

	//------------------------------------------------------------------------------------------------
	//! Every body that already carries a kit index, stamped. The backstop behind the two hooks
	//! above.
	protected void StampAll()
	{
		if (s_Instance != this)
			return;

		// the core comes up after the game mode, so Init's attempt misses and the hook lands here
		SubscribeStamp();

		int before = m_iStampsLanded;

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		array<int> players = {};
		pm.GetPlayers(players);
		foreach (int pid : players)
		{
			IEntity body = pm.GetPlayerControlledEntity(pid);
			if (!body)
				continue;

			// One component lookup: the character component is an editable entity component
			SCR_EditableCharacterComponent character = SCR_EditableCharacterComponent.Cast(
				body.FindComponent(SCR_EditableCharacterComponent));
			if (character)
				StampEditableByIndex(character, character.RK29_GetKitIndex());
		}

		// Whether this poll is earning its keep. Anything left for the backstop means an event
		// hook did not fire, the only candidate being onRplName on an initial sync - so a session
		// that never prints this is one where the poll can go. Only meaningful once the hook is
		// in.
		if (m_bStampSubscribed && m_iStampsLanded > before)
			Print(string.Format("[RK29] stamp backstop caught %1 body(s) the event hooks"
				+ " missed", m_iStampsLanded - before), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	//! The manager-owned identity for one kit, shared by every body wearing it because consumers
	//! cache the reference: a body that swaps kit is re-pointed rather than having its own
	//! mutated.
	protected RK29_KitUIInfo KitInfo(notnull RK29_KitStruct kit)
	{
		RK29_KitUIInfo info = m_mKitInfos.Get(kit.m_sKitName);
		if (info)
			return info;

		info = RK29_KitUIInfo.RK29_Create(kit);
		m_mKitInfos.Set(kit.m_sKitName, info);
		return info;
	}

	//------------------------------------------------------------------------------------------------
	protected void StampBody(SCR_EditableEntityComponent editable, notnull RK29_KitStruct kit)
	{
		if (!editable)
			return;

		RK29_KitUIInfo ours = KitInfo(kit);

		// an unstamped body answers with the prefab's own info, so the first stamp always lands
		if (RK29_KitUIInfo.Cast(editable.GetInfo()) == ours)
			return;

		editable.SetInfoInstance(ours);
		m_iStampsLanded++;
	}

	//------------------------------------------------------------------------------------------------
	//! Kits a side may take: every roster class on it, in loadout order. The one chokepoint the
	//! request check, the deploy row, the default kit and the picker all read. Ordered through
	//! m_aIndexToKit so the "first kit" fallback matches on every machine. A kit with no roster
	//! class is deploy-only: in m_mKits for the counts and the stamp, out of the picker, and
	//! refused if requested.
	int GetOfferedKits(string factionKey, notnull array<string> outKitNames)
	{
		outKitNames.Clear();
		foreach (string kitName : m_aIndexToKit)
		{
			RK29_KitStruct kit = m_mKits.Get(kitName);
			if (!kit || kit.m_sFactionKey != factionKey)
				continue;
			if (!m_Setup || !m_Setup.FindClass(kitName))
				continue;
			outKitNames.Insert(kitName);
		}
		return outKitNames.Count();
	}

	// ====================================================================== loadout lookups

	//------------------------------------------------------------------------------------------------
	//! Through our own null-safe lookup, not vanilla's GetLoadoutByName - see GM29_KitLoadouts.c.
	protected SCR_BasePlayerLoadout FindLoadoutByName(string kitName)
	{
		SCR_LoadoutManager lm = GetGame().GetLoadoutManager();
		if (!lm)
			return null;
		return lm.RK29_FindLoadoutByName(kitName);
	}

	//------------------------------------------------------------------------------------------------
	protected string CurrentLoadoutName(int playerId)
	{
		SCR_LoadoutManager lm = GetGame().GetLoadoutManager();
		if (!lm)
			return "";
		SCR_BasePlayerLoadout loadout = lm.GetPlayerLoadout(playerId);
		if (!loadout)
			return "";
		return loadout.GetLoadoutName();
	}
}
