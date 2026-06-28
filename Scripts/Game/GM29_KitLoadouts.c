//------------------------------------------------------------------------------------------------
//! 29th Infantry Division - Game Master loadout registration
//!
//! Two pieces:
//!   1. GM29_KitLoadoutHolder - the config root that Configs/Loadouts/GM29_Kits.conf
//!      deserialises into. We own this class, so its field name (m_aLoadouts) is stable.
//!   2. modded SCR_LoadoutManager - appends the kits in that holder to whatever game mode
//!      (Game Master included) spins up the loadout manager. No per-map editing: the modded
//!      class is the active definition for every SCR_LoadoutManager instance once this mod
//!      is in the load order.
//!
//! There are exactly two spots to confirm before this runs - both marked "VERIFY" below.
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
	// ===========================================================================================
	// VERIFY #1 - config GUID
	// After you create Configs/Loadouts/GM29_Kits.conf in Workbench, copy the GUID Workbench
	// assigns to it and paste it between the braces below (keep the path). Until then the loader
	// no-ops cleanly and logs a warning - it will not break anything.
	// ===========================================================================================
	[Attribute("{DE20AF7D7BBE0D78}Configs/Loadouts/GM29_Kits.conf", desc: "29th kit holder config")]
	protected ResourceName m_sGM29KitHolder;

	//--------------------------------------------------------------------------------------------
	// VERIFY #2 - init hook
	// EOnInit only fires if the INIT event is enabled on the entity. Setting it here in the
	// constructor makes that guaranteed regardless of what the base class does. If your server
	// log never prints the "[GM29] injected ..." line, this is the first thing to check against
	// the "Game Master Loadouts" reference mod - the base may populate its list in a different
	// pass, in which case move the InjectGM29Kits() call to match.
	//--------------------------------------------------------------------------------------------
	void SCR_LoadoutManager(IEntitySource src, IEntity parent)
	{
		// SetEventMask is a method on the entity (this) and takes only the mask.
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
		Resource res = Resource.Load(m_sGM29KitHolder);
		if (!res.IsValid())
		{
			Print("[GM29] kit holder not found - check m_sGM29KitHolder GUID (VERIFY #1)", LogLevel.WARNING);
			return;
		}

		GM29_KitLoadoutHolder holder = GM29_KitLoadoutHolder.Cast(BaseContainerTools.CreateInstanceFromContainer(res.GetResource().ToBaseContainer()));
		if (!holder || !holder.m_aLoadouts)
		{
			Print("[GM29] kit holder empty or wrong root class", LogLevel.WARNING);
			return;
		}

		if (!m_aPlayerLoadouts)
			m_aPlayerLoadouts = {};

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

		// Plain concatenation - no ternary inside Print(), per your guardrails.
		Print("[GM29] injected " + added.ToString() + " kit loadouts into the loadout manager", LogLevel.NORMAL);
	}

	//--------------------------------------------------------------------------------------------
	//! Dedup guard so repeated mode/round restarts (your RT_GameModeEditor resets) do not stack
	//! duplicate entries. Matches on the loadout's character resource.
	protected bool IsAlreadyRegistered(notnull SCR_BasePlayerLoadout candidate)
	{
		ResourceName candidateRes = candidate.GetLoadoutResource();
		foreach (SCR_BasePlayerLoadout existing : m_aPlayerLoadouts)
		{
			if (existing && existing.GetLoadoutResource() == candidateRes)
				return true;
		}
		return false;
	}
}
