//------------------------------------------------------------------------------------------------
//! 29th Game Master loadout registration. Injection is additive: the scenario's
//! SCR_LoadoutManager already holds vanilla's '#AR-Loadout_Editor_NewArsenalLoadout_Name' rows,
//! its arsenal loadouts, the CIV entries and anything RHS adds. PruneForeignKits strips the
//! faction ones we did not author, and refuses to run when it recognises fewer entries than the
//! config declares - that means the match logic is broken and pruning would delete 29th kits.
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
	[Attribute("{DE20AF7D7BBE0D78}Configs/Loadouts/GM29_Kits.conf", desc: "29th kit holder config")]
	protected ResourceName m_sGM29KitHolder;

	//! Loadout names of every kit declared in GM29_Kits.conf, built once. Identity is matched on
	//! name, never on resource: GetLoadoutResource() goes through engine resolution and has returned
	//! different values for the same object on consecutive calls within one init, including a vanilla
	//! GUID for a 29th kit. 29th names start "29th", vanilla's are keys starting "#AR-", so they
	//! cannot collide.
	protected ref array<string> m_aGM29OwnedNames;

	//------------------------------------------------------------------------------------------------
	void SCR_LoadoutManager(IEntitySource src, IEntity parent)
	{
		SetEventMask(EntityEvent.INIT);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		InjectGM29Kits();
	}

	//------------------------------------------------------------------------------------------------
	protected void InjectGM29Kits()
	{
		GM29_KitLoadoutHolder holder = LoadGM29Holder();
		if (!holder)
			return;

		if (!m_aPlayerLoadouts)
			m_aPlayerLoadouts = {};

		// cache must exist before injection - the dedup guard uses it
		BuildOwnedNameCache(holder);

		int preExisting = m_aPlayerLoadouts.Count();

		// counted as we go, so VerifyInjection needs no second walk. Dedup matches on the loadout
		// name, never the resource (see m_aGM29OwnedNames); an unnamed entry is never a
		// duplicate.
		int added = 0;
		int registered = 0;
		foreach (SCR_BasePlayerLoadout kit : holder.m_aLoadouts)
		{
			if (!kit)
				continue;

			string kitName = kit.GetLoadoutName();
			if (kitName != string.Empty && RK29_FindLoadoutByName(kitName))
			{
				Print(string.Format("[GM29Kits] dedup skip: '%1' already registered", kitName),
					LogLevel.NORMAL);
				registered = registered + 1;
				continue;
			}

			m_aPlayerLoadouts.Insert(kit);
			added = added + 1;
			registered = registered + 1;
		}

		Print(string.Format("[GM29Kits] pre-existing entries: %1 | injected: %2",
			preExisting, added), LogLevel.NORMAL);

		VerifyInjection(holder, registered);

		PruneForeignKits();

		// kit system boots here - the loadout list is final at this point
		RK29_KitManager.Boot(m_aPlayerLoadouts);
	}

	//------------------------------------------------------------------------------------------------
	//! Registered loadout of this name, or null. not vanilla's GetLoadoutByName, which derefs both
	//! m_aPlayerLoadouts and every element unguarded - and this list demonstrably holds nulls.
	SCR_BasePlayerLoadout RK29_FindLoadoutByName(string loadoutName)
	{
		if (!m_aPlayerLoadouts)
			return null;

		foreach (SCR_BasePlayerLoadout entry : m_aPlayerLoadouts)
		{
			if (entry && entry.GetLoadoutName() == loadoutName)
				return entry;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
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

	//------------------------------------------------------------------------------------------------
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

		Print(string.Format("[GM29Kits] ownership cache holds %1 kit name(s)",
			m_aGM29OwnedNames.Count()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
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

	//------------------------------------------------------------------------------------------------
	//! Removes SCR_FactionPlayerLoadout entries we did not author - including the
	//! '#AR-Loadout_Editor_NewArsenalLoadout_Name' rows, the deploy-menu option for building a custom
	//! loadout. Saved arsenal loadouts are kept. Aborts entirely if ownership matching looks broken.
	protected void PruneForeignKits()
	{
		if (!m_aGM29OwnedNames || m_aGM29OwnedNames.IsEmpty())
		{
			Print("[GM29Kits] PRUNE ABORTED - ownership cache is empty", LogLevel.ERROR);
			return;
		}

		// Single evaluation pass: resource resolution for at least one 29th prefab has proven unstable
		// within one init, so every entry is classified once and both the gate and the removals read that
		// result.
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

			if (!SCR_FactionPlayerLoadout.Cast(entry))
				continue;

			if (SCR_PlayerArsenalLoadout.Cast(entry))
				continue;

			foreignIndices.Insert(i);
		}

		int ownedExpected = m_aGM29OwnedNames.Count();
		if (ownedCount < ownedExpected)
		{
			Print(string.Format("[GM29Kits] PRUNE ABORTED - recognised only %1 of %2 29th kits."
				+ " Pruning now would delete 29th content.",
				ownedCount, ownedExpected), LogLevel.ERROR);
			return;
		}

		// back-to-front so earlier indices stay valid
		for (int i = foreignIndices.Count() - 1; i >= 0; i--)
		{
			int idx = foreignIndices[i];
			SCR_BasePlayerLoadout entry = m_aPlayerLoadouts[idx];

			Print(string.Format("[GM29Kits] pruning: name='%1' res=%2",
				entry.GetLoadoutName(), entry.GetLoadoutResource()), LogLevel.NORMAL);
			m_aPlayerLoadouts.RemoveOrdered(idx);
		}

		Print(string.Format("[GM29Kits] pruned %1 foreign loadouts | %2 29th kits retained",
			foreignIndices.Count(), ownedCount), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! `registered` is what the injection loop counted as it went. The only way to fall short of the
	//! authored count is a NULL entry in the holder, which has no name to report.
	protected void VerifyInjection(notnull GM29_KitLoadoutHolder holder, int registered)
	{
		int authored = holder.m_aLoadouts.Count();
		if (registered >= authored)
		{
			Print(string.Format("[GM29Kits] verify OK - all %1 kits registered", authored),
				LogLevel.NORMAL);
			return;
		}

		Print(string.Format("[GM29Kits] verify FAILED - %1 of %2 kit(s) missing; a null entry in"
			+ " GM29_Kits.conf is the only way this happens",
			authored - registered, authored), LogLevel.ERROR);
	}
}
