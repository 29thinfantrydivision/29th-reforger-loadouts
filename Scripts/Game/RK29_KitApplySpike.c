//------------------------------------------------------------------------------------------------
//! 29th Infantry Division - M1 APPLY SPIKE (throwaway)
//!
//! Proves that SCR_PlayerArsenalLoadout's reconcile path can re-kit a LIVING character.
//!
//! Warmup (server, once):  spawn kit prefab -> wait -> ReadLoadoutString -> cache string -> delete
//! Apply   (server, on demand):  LoadFromString -> ApplyLoadoutString(playerEntity)
//!
//! HARD DEPENDENCY: SCR_PlayerArsenalLoadout.ARSENALLOADOUT_COMPONENTS_TO_CHECK is populated by
//! SCR_ArsenalManagerComponent.OnPostInit. If the game mode has no arsenal manager, that list
//! stays empty, ShouldSaveStorage() returns false for every storage, and serialization silently
//! yields an empty loadout. StartWarmup() aborts loudly rather than caching garbage.
//!
//! Nothing here is intended to survive M1.
//------------------------------------------------------------------------------------------------
class RK29_KitApplySpike
{
	//! Verbose logging. Spike only - real components default this off.
	protected static const bool RK29_DEBUG = true;

	//! Delay before serializing a donor. InitialInventoryItems are not guaranteed present on the
	//! spawn frame. If kits come back short, raise this first before suspecting anything else.
	protected static const int DONOR_READ_DELAY_MS = 500;

	//! Somewhere nothing will interact with the donor for half a second.
	protected static const vector DONOR_SPAWN_POS = "0 -1000 0";

	//! Any serialized kit shorter than this is almost certainly a silent failure.
	protected static const int SUSPICIOUS_STRING_LENGTH = 200;

	protected static ref RK29_KitApplySpike s_Instance;

	//! Kits under test. GUIDs lifted straight from GM29_Kits.conf.
	protected ref array<ResourceName> m_aSpikeKits = {
		"{118CADEB5F4E4F38}Prefabs/Characters/Factions/BLUFOR/29th_US/29th_Character_US_MG.et",
		"{505B0F46348BC94C}Prefabs/Characters/Factions/BLUFOR/29th_US/29th_Character_US_LAT.et",
		"{5C0BCD473FB7ED03}Prefabs/Characters/Factions/BLUFOR/29th_US/29th_Character_US_Crew.et"
	};

	//! Serialized loadout strings, index-aligned to m_aSpikeKits.
	protected ref array<string> m_aKitStrings;

	protected bool m_bWarmupComplete;

	//------------------------------------------------------------------------------------------------
	static RK29_KitApplySpike GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	//! Call from modded SCR_LoadoutManager.EOnInit(). Server only.
	static void Boot()
	{
		Print(string.Format("[RK29Spike] Boot() entered | IsServer=%1 | existing instance=%2",
			Replication.IsServer(), s_Instance != null), LogLevel.NORMAL);

		if (s_Instance)
			return;

		s_Instance = new RK29_KitApplySpike();

		// Deliberately late. The arsenal manager must have run OnPostInit before we touch its
		// statics, and replication is not reliably up during entity init.
		GetGame().GetCallqueue().CallLater(s_Instance.StartWarmup, 2000, false);

		Print("[RK29Spike] instance created, warmup scheduled in 2000ms", LogLevel.NORMAL);
	}


	//------------------------------------------------------------------------------------------------
	protected void StartWarmup()
	{
		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		bool isMaster = (gameMode != null && gameMode.IsMaster());

		Print(string.Format("[RK29Spike] StartWarmup | IsServer=%1 | IsMaster=%2", Replication.IsServer(), isMaster), LogLevel.NORMAL);

		if (!isMaster)
		{
			Print("[RK29Spike] not authority - no donors cached here", LogLevel.NORMAL);
			return;
		}

		array<typename> componentsToCheck = SCR_PlayerArsenalLoadout.ARSENALLOADOUT_COMPONENTS_TO_CHECK;
		if (!componentsToCheck || componentsToCheck.IsEmpty())
		{
			Print("[RK29Spike] ABORT - ARSENALLOADOUT_COMPONENTS_TO_CHECK is empty. The game mode has no SCR_ArsenalManagerComponent, so every storage would serialize as skipped. Add the component to the game mode before retrying.", LogLevel.ERROR);
			return;
		}

		Print("[RK29Spike] storage type list holds " + componentsToCheck.Count().ToString() + " entries - proceeding", LogLevel.NORMAL);

		m_aKitStrings = {};
		for (int i = 0; i < m_aSpikeKits.Count(); i++)
		{
			m_aKitStrings.Insert(string.Empty);
		}

		CacheKit(0);
	}

	//------------------------------------------------------------------------------------------------
	//! Sequential, one donor alive at a time. Keeps the log readable and avoids three characters
	//! stacked at the same point.
	protected void CacheKit(int idx)
	{
		if (idx >= m_aSpikeKits.Count())
		{
			ReportWarmup();
			return;
		}

		ResourceName kitRes = m_aSpikeKits[idx];

		Resource res = Resource.Load(kitRes);
		if (!res.IsValid())
		{
			Print("[RK29Spike] [" + idx.ToString() + "] resource failed to load: " + kitRes, LogLevel.ERROR);
			CacheKit(idx + 1);
			return;
		}

		EntitySpawnParams spawnParams = new EntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		spawnParams.Transform[3] = DONOR_SPAWN_POS;

		IEntity donor = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), spawnParams);
		if (!donor)
		{
			Print("[RK29Spike] [" + idx.ToString() + "] donor spawn returned null: " + kitRes, LogLevel.ERROR);
			CacheKit(idx + 1);
			return;
		}

		if (RK29_DEBUG)
			Print("[RK29Spike] [" + idx.ToString() + "] donor spawned, reading in " + DONOR_READ_DELAY_MS.ToString() + "ms", LogLevel.NORMAL);

		GetGame().GetCallqueue().CallLater(SerializeDonor, DONOR_READ_DELAY_MS, false, idx, donor);
	}

	//------------------------------------------------------------------------------------------------
	protected void SerializeDonor(int idx, IEntity donor)
	{
		if (!donor)
		{
			Print("[RK29Spike] [" + idx.ToString() + "] donor vanished before serialization", LogLevel.ERROR);
			CacheKit(idx + 1);
			return;
		}

		JsonSaveContext context = new JsonSaveContext();

		if (SCR_PlayerArsenalLoadout.ReadLoadoutString(donor, context))
		{
			string loadoutString = context.SaveToString();
			m_aKitStrings[idx] = loadoutString;

			int len = loadoutString.Length();
			if (len < SUSPICIOUS_STRING_LENGTH)
				Print("[RK29Spike] [" + idx.ToString() + "] SUSPICIOUS - serialized to only " + len.ToString() + " chars. Raise DONOR_READ_DELAY_MS or check the prefab's TargetStorage paths.", LogLevel.WARNING);
			else
				Print("[RK29Spike] [" + idx.ToString() + "] cached " + len.ToString() + " chars", LogLevel.NORMAL);
		}
		else
		{
			Print("[RK29Spike] [" + idx.ToString() + "] ReadLoadoutString FAILED", LogLevel.ERROR);
		}

		SCR_EntityHelper.DeleteEntityAndChildren(donor);

		CacheKit(idx + 1);
	}

	//------------------------------------------------------------------------------------------------
	protected void ReportWarmup()
	{
		int cached = 0;
		foreach (string s : m_aKitStrings)
		{
			if (s != string.Empty)
				cached = cached + 1;
		}

		m_bWarmupComplete = true;

		Print("[RK29Spike] warmup complete - " + cached.ToString() + " of " + m_aSpikeKits.Count().ToString() + " kits cached", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! The entire point of M1. Server only.
	//! \param[in] playerId target player
	//! \param[in] kitIdx index into m_aSpikeKits
	bool ApplyKit(int playerId, int kitIdx)
	{
		if (!Replication.IsServer())
			return false;

		if (!m_bWarmupComplete)
		{
			Print("[RK29Spike] apply rejected - warmup has not finished", LogLevel.WARNING);
			return false;
		}

		if (!m_aKitStrings || kitIdx < 0 || kitIdx >= m_aKitStrings.Count())
		{
			Print("[RK29Spike] apply rejected - kit index " + kitIdx.ToString() + " out of range", LogLevel.WARNING);
			return false;
		}

		string kitString = m_aKitStrings[kitIdx];
		if (kitString == string.Empty)
		{
			Print("[RK29Spike] apply rejected - kit " + kitIdx.ToString() + " has no cached string", LogLevel.WARNING);
			return false;
		}

		IEntity playerEntity = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!playerEntity)
		{
			Print("[RK29Spike] apply rejected - no controlled entity for player " + playerId.ToString(), LogLevel.WARNING);
			return false;
		}
		
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(playerEntity);
		if (!character)
		{
			Print(string.Format("[RK29Spike] apply rejected - controlled entity is %1, not a character", playerEntity.ClassName()), LogLevel.WARNING);
			return false;
		}

		CharacterControllerComponent charController = character.GetCharacterController();
		if (!charController || charController.IsDead())
		{
			Print("[RK29Spike] apply rejected - character is dead", LogLevel.WARNING);
			return false;
		}
		
		JsonLoadContext context = new JsonLoadContext();
		if (!context.LoadFromString(kitString))
		{
			Print("[RK29Spike] apply FAILED - could not parse cached string for kit " + kitIdx.ToString(), LogLevel.ERROR);
			return false;
		}

		bool applied = SCR_PlayerArsenalLoadout.ApplyLoadoutString(playerEntity, context);

		if (applied)
			Print("[RK29Spike] applied kit " + kitIdx.ToString() + " to player " + playerId.ToString(), LogLevel.NORMAL);
		else
			Print("[RK29Spike] ApplyLoadoutString returned FALSE for kit " + kitIdx.ToString() + " on player " + playerId.ToString(), LogLevel.ERROR);

		return applied;
	}

	//------------------------------------------------------------------------------------------------
	int GetKitCount()
	{
		return m_aSpikeKits.Count();
	}
}
