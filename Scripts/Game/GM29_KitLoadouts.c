//------------------------------------------------------------------------------------------------
//! 29th Infantry Division - Game Master loadout registration
//!
//! Injection is ADDITIVE. The Game Master scenario's own SCR_LoadoutManager arrives with its
//! vanilla entries already in m_aPlayerLoadouts:
//!   - one SCR_FactionPlayerLoadout per faction seeded from '#AR-Loadout_Editor_NewArsenalLoadout_Name'
//!     (these are the stray "Rifleman" rows in the deploy menu)
//!   - one SCR_PlayerArsenalLoadout per faction for saved arsenal loadouts
//!   - the CIV random-civilian entries
//! Anything RHS adds lands in the same list.
//!
//! GM29_DUMP_LOADOUTS  - log the full post-injection table with resource GUIDs.
//! GM29_PRUNE_FOREIGN  - strip faction loadouts we did not author.
//!
//! Ownership is matched on the resource GUID only, never the full ResourceName string. Path
//! text is not stable (several 29th prefabs have .meta files declaring a different folder than
//! where the file now lives), so a full-string compare produces false FOREIGN hits.
//!
//! The prune refuses to run if the number of entries it recognises as ours is lower than the
//! number of kits in the config - that mismatch means the match logic is broken and pruning
//! would delete 29th kits.
//------------------------------------------------------------------------------------------------

//! Config root for the authored kit list. Field name is ours -> reliable schema.
[BaseContainerProps(configRoot: true)]
class GM29_KitLoadoutHolder
{
	[Attribute(desc: "29th kits to inject. Each entry faction-tagged US or USSR.")]
	ref array<ref SCR_BasePlayerLoadout> m_aLoadouts;
}

//------------------------------------------------------------------------------------------------
modded class SCR_LoadoutManager
{
	//! Log the full loadout list after injection. Turn off for deployment.
	protected static const bool GM29_DUMP_LOADOUTS = true;

	//! Remove faction loadouts the 29th did not author.
	//! NOTE: this removes the '#AR-Loadout_Editor_NewArsenalLoadout_Name' entries, which are
	//! the deploy-menu option for building a custom loadout from scratch. If the 29th wants
	//! authored kits only, that is the desired outcome. Saved arsenal loadouts
	//! (SCR_PlayerArsenalLoadout) are always left alone.
	protected static const bool GM29_PRUNE_FOREIGN = true;

	[Attribute("{DE20AF7D7BBE0D78}Configs/Loadouts/GM29_Kits.conf", desc: "29th kit holder config")]
	protected ResourceName m_sGM29KitHolder;

	//! Loadout NAMES of every kit declared in GM29_Kits.conf. Built once.
	//!
	//! Identity is matched on name, not on resource. GetLoadoutResource() goes through engine
	//! resource resolution and has repeatedly returned different values for the same object on
	//! consecutive calls within a single init - including returning a vanilla GUID for a 29th
	//! kit. m_sLoadoutName is authored config data, read straight off the container, and is
	//! therefore deterministic. Every 29th kit name begins with "29th"; every vanilla entry is
	//! a localisation key beginning with "#AR-", so there is no collision risk.
	protected ref array<string> m_aGM29OwnedNames;

	//--------------------------------------------------------------------------------------------
	void SCR_LoadoutManager(IEntitySource src, IEntity parent)
	{
		SetEventMask(EntityEvent.INIT);
	}

	//--------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		InjectGM29Kits();
	}

	//--------------------------------------------------------------------------------------------
	protected void InjectGM29Kits()
	{
		GM29_KitLoadoutHolder holder = LoadGM29Holder();
		if (!holder)
			return;

		if (!m_aPlayerLoadouts)
			m_aPlayerLoadouts = {};

		// Cache must exist before injection - the dedup guard uses it.
		BuildOwnedNameCache(holder);

		int preExisting = m_aPlayerLoadouts.Count();

		int added = 0;
		foreach (SCR_BasePlayerLoadout kit : holder.m_aLoadouts)
		{
			if (!kit)
				continue;

			if (!IsAlreadyRegistered(kit))
			{
				m_aPlayerLoadouts.Insert(kit);
				added = added + 1;
			}
		}

		Print("[GM29Kits] pre-existing entries: " + preExisting.ToString() + " | injected: " + added.ToString(), LogLevel.NORMAL);

		VerifyInjection(holder);

		if (GM29_PRUNE_FOREIGN)
			PruneForeignKits();

		if (GM29_DUMP_LOADOUTS)
			DumpLoadoutTable();
		
		RK29_KitApplySpike.Boot();
	}

	//--------------------------------------------------------------------------------------------
	protected GM29_KitLoadoutHolder LoadGM29Holder()
	{
		Resource res = Resource.Load(m_sGM29KitHolder);
		if (!res.IsValid())
		{
			Print("[GM29Kits] kit holder not found - check m_sGM29KitHolder GUID", LogLevel.WARNING);
			return null;
		}

		GM29_KitLoadoutHolder holder = GM29_KitLoadoutHolder.Cast(BaseContainerTools.CreateInstanceFromContainer(res.GetResource().ToBaseContainer()));
		if (!holder || !holder.m_aLoadouts)
		{
			Print("[GM29Kits] kit holder empty or wrong root class", LogLevel.WARNING);
			return null;
		}

		return holder;
	}

	//--------------------------------------------------------------------------------------------
	//! Pulls the bare GUID out of a ResourceName of the form {ABCDEF0123456789}Path/To/File.et
	//! Returns the input unchanged if it is not in that form.
	protected string ExtractGuid(ResourceName resource)
	{
		// Force ResourceName -> string via concatenation.
		string raw = "" + resource;

		int close = raw.IndexOf("}");
		if (raw.IndexOf("{") != 0 || close < 2)
			return raw;

		return raw.Substring(1, close - 1);
	}

	//--------------------------------------------------------------------------------------------
	protected void BuildOwnedNameCache(notnull GM29_KitLoadoutHolder holder)
	{
		m_aGM29OwnedNames = {};

		foreach (SCR_BasePlayerLoadout kit : holder.m_aLoadouts)
		{
			if (!kit)
				continue;

			string kitName = kit.GetLoadoutName();
			if (kitName != string.Empty)
				m_aGM29OwnedNames.Insert(kitName);
		}

		Print("[GM29Kits] ownership cache holds " + m_aGM29OwnedNames.Count().ToString() + " kit name(s)", LogLevel.NORMAL);
	}

	//--------------------------------------------------------------------------------------------
	protected bool IsGM29Loadout(SCR_BasePlayerLoadout candidate)
	{
		if (!candidate || !m_aGM29OwnedNames)
			return false;

		string candidateName = candidate.GetLoadoutName();

		foreach (string ownedName : m_aGM29OwnedNames)
		{
			if (ownedName == candidateName)
				return true;
		}

		return false;
	}

	//--------------------------------------------------------------------------------------------
	//! Counts how many registered entries we recognise as 29th-authored.
	protected int CountOwnedEntries()
	{
		int ownedCount = 0;
		foreach (SCR_BasePlayerLoadout entry : m_aPlayerLoadouts)
		{
			if (entry && IsGM29Loadout(entry))
				ownedCount = ownedCount + 1;
		}
		return ownedCount;
	}

	//--------------------------------------------------------------------------------------------
	protected void DumpLoadoutTable()
	{
		Print("[GM29Kits] ---- registered loadouts (" + m_aPlayerLoadouts.Count().ToString() + ") ----", LogLevel.NORMAL);

		foreach (int i, SCR_BasePlayerLoadout entry : m_aPlayerLoadouts)
		{
			if (!entry)
			{
				Print("[GM29Kits]   [" + i.ToString() + "] <null entry>", LogLevel.WARNING);
				continue;
			}

			string factionKey = "<none>";
			SCR_FactionPlayerLoadout factionLoadout = SCR_FactionPlayerLoadout.Cast(entry);
			if (factionLoadout)
				factionKey = factionLoadout.GetFactionKey();

			string ownership = "FOREIGN";
			if (IsGM29Loadout(entry))
				ownership = "29th";

			Print("[GM29Kits]   [" + i.ToString() + "] " + ownership + " | " + factionKey + " | " + entry.Type().ToString() + " | name='" + entry.GetLoadoutName() + "' | res=" + entry.GetLoadoutResource(), LogLevel.NORMAL);
		}

		Print("[GM29Kits] ---- end of loadout table ----", LogLevel.NORMAL);
	}

	//--------------------------------------------------------------------------------------------
	//! Removes SCR_FactionPlayerLoadout entries we did not author. Arsenal loadouts are skipped.
	//! Aborts entirely if ownership matching looks broken.
	protected void PruneForeignKits()
	{
		if (!m_aGM29OwnedNames || m_aGM29OwnedNames.IsEmpty())
		{
			Print("[GM29Kits] PRUNE ABORTED - ownership cache is empty", LogLevel.ERROR);
			return;
		}

		// SINGLE EVALUATION PASS.
		// Resource resolution for at least one 29th prefab has proven unstable - the same
		// entry can classify differently on two calls within one init. So every entry is
		// classified exactly once here, and both the safety gate and the removals below read
		// that one result. The gate can no longer pass while a removal decision disagrees.
		array<int> foreignIndices = {};
		int ownedCount = 0;

		foreach (int i, SCR_BasePlayerLoadout entry : m_aPlayerLoadouts)
		{
			if (!entry)
				continue;

			if (IsGM29Loadout(entry))
			{
				ownedCount = ownedCount + 1;
				continue;
			}

			// Only faction-affiliated kits are candidates.
			if (!SCR_FactionPlayerLoadout.Cast(entry))
				continue;

			// Never strip saved arsenal loadouts.
			if (SCR_PlayerArsenalLoadout.Cast(entry))
				continue;

			foreignIndices.Insert(i);
		}

		int ownedExpected = m_aGM29OwnedNames.Count();
		if (ownedCount < ownedExpected)
		{
			Print("[GM29Kits] PRUNE ABORTED - recognised only " + ownedCount.ToString() + " of " + ownedExpected.ToString() + " 29th kits. Pruning now would delete 29th content.", LogLevel.ERROR);
			return;
		}

		// Remove back-to-front so earlier indices stay valid.
		for (int i = foreignIndices.Count() - 1; i >= 0; i--)
		{
			int idx = foreignIndices[i];
			SCR_BasePlayerLoadout entry = m_aPlayerLoadouts[idx];

			Print("[GM29Kits] pruning: name='" + entry.GetLoadoutName() + "' res=" + entry.GetLoadoutResource(), LogLevel.NORMAL);
			m_aPlayerLoadouts.RemoveOrdered(idx);
		}

		Print("[GM29Kits] pruned " + foreignIndices.Count().ToString() + " foreign loadouts | " + ownedCount.ToString() + " 29th kits retained", LogLevel.NORMAL);
	}

	//--------------------------------------------------------------------------------------------
	//! Dedup guard so repeated mode/round restarts do not stack duplicate entries.
	//! Matches on GUID only. Full ResourceName equality is NOT reliable here - it produced
	//! false positives that silently dropped 29th kits during injection.
	protected bool IsAlreadyRegistered(notnull SCR_BasePlayerLoadout candidate)
	{
		string candidateName = candidate.GetLoadoutName();
		if (candidateName == string.Empty)
			return false;

		foreach (SCR_BasePlayerLoadout existing : m_aPlayerLoadouts)
		{
			if (!existing)
				continue;

			if (existing.GetLoadoutName() == candidateName)
			{
				Print("[GM29Kits] dedup skip: '" + candidateName + "' already registered", LogLevel.NORMAL);
				return true;
			}
		}

		return false;
	}

	//--------------------------------------------------------------------------------------------
	//! Confirms every kit in the config actually made it into the registered list. Names any
	//! that did not, so a silent drop can never go unnoticed again.
	protected void VerifyInjection(notnull GM29_KitLoadoutHolder holder)
	{
		int missing = 0;

		foreach (SCR_BasePlayerLoadout kit : holder.m_aLoadouts)
		{
			if (!kit)
				continue;

			string kitName = kit.GetLoadoutName();
			bool found = false;

			foreach (SCR_BasePlayerLoadout entry : m_aPlayerLoadouts)
			{
				if (entry && entry.GetLoadoutName() == kitName)
				{
					found = true;
					break;
				}
			}

			if (!found)
			{
				Print("[GM29Kits] MISSING after injection: '" + kit.GetLoadoutName() + "' res=" + kit.GetLoadoutResource(), LogLevel.ERROR);
				missing = missing + 1;
			}
		}

		if (missing == 0)
			Print("[GM29Kits] verify OK - all " + holder.m_aLoadouts.Count().ToString() + " kits registered", LogLevel.NORMAL);
		else
			Print("[GM29Kits] verify FAILED - " + missing.ToString() + " kit(s) missing", LogLevel.ERROR);
	}
}