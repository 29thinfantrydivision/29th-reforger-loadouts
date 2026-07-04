//------------------------------------------------------------------------------------------------
//! 29th Infantry Division - predefined squad registration
//!
//! Same shape as GM29_KitLoadouts.c:
//!   1. GM29_GroupPresetHolder - the config root that Configs/Groups/GM29_Groups.conf
//!      deserialises into. We own this class, so its field name (m_aGroupPresets) is stable.
//!   2. modded SCR_Faction - appends the presets in that holder to m_aGroupRolePresetConfigs
//!      on every faction instance (US and USSR both pick these up, since each faction's
//!      Init() runs this independently). No per-faction editing of the base BLUFOR/OPFOR conf.
//!
//! Revised from the plain SCR_GroupPreset version: SCR_GroupRolePresetConfig extends
//! SCR_GroupPreset and adds m_aLoadoutResources, so a single preset both defines the joinable
//! group AND restricts which of our existing GM29_Kits.conf loadouts are offered inside it.
//! It lives in a different array on SCR_Faction (m_aGroupRolePresetConfigs, not
//! m_aPredefinedGroups) - this replaces the earlier m_aPredefinedGroups injection.
//!
//! The loadout resource lists in GM29_Groups.conf hold both factions' resources per role
//! (e.g. "Squad" contains US and USSR rifleman/AR/MG/etc). This is safe to share across
//! factions unfiltered - IsLoadoutInGroup() only ever gets checked against a loadout the
//! player already has, and SCR_FactionPlayerLoadout is itself faction-affiliated elsewhere,
//! so a USSR player is never offered a US loadout in the first place.
//!
//! There is exactly one spot to confirm before this runs - marked "VERIFY" below.
//------------------------------------------------------------------------------------------------

//! Config root for the authored group list. Field name is ours -> reliable schema.
[BaseContainerProps(configRoot: true)]
class GM29_GroupPresetHolder
{
	[Attribute(desc: "29th predefined squads to inject into every faction (HQ, Crew, Squad).")]
	ref array<ref SCR_GroupRolePresetConfig> m_aGroupPresets;
}

//------------------------------------------------------------------------------------------------
modded class SCR_Faction
{
	// Real GUID of Configs/Groups/GM29_Groups.conf (from its .conf.meta). If you ever move or
	// recreate that config, update this to the new GUID shown in the .meta / Workbench.
	[Attribute("{4F499D4957373C95}Configs/Groups/GM29_Groups.conf", desc: "29th predefined squad holder config")]
	protected ResourceName m_sGM29GroupHolder;

	//--------------------------------------------------------------------------------------------
	override void Init(IEntity owner)
	{
		super.Init(owner);
		InjectGM29Groups();
	}

	//--------------------------------------------------------------------------------------------
	protected void InjectGM29Groups()
	{
		Resource res = Resource.Load(m_sGM29GroupHolder);
		if (!res.IsValid())
		{
			Print("[GM29Groups] group holder not found - check m_sGM29GroupHolder GUID (VERIFY)", LogLevel.WARNING);
			return;
		}

		GM29_GroupPresetHolder holder = GM29_GroupPresetHolder.Cast(BaseContainerTools.CreateInstanceFromContainer(res.GetResource().ToBaseContainer()));
		if (!holder || !holder.m_aGroupPresets)
		{
			Print("[GM29Groups] group holder empty or wrong root class", LogLevel.WARNING);
			return;
		}

		if (!m_aGroupRolePresetConfigs)
			m_aGroupRolePresetConfigs = {};

		int added = 0;
		foreach (SCR_GroupRolePresetConfig preset : holder.m_aGroupPresets)
		{
			if (!preset)
				continue;

			if (!IsAlreadyRegisteredGroup(preset))
			{
				m_aGroupRolePresetConfigs.Insert(preset);
				added = added + 1;
			}
		}

		// Plain concatenation - no ternary inside Print(), per your guardrails.
		Print("[GM29Groups] injected " + added.ToString() + " group role presets into " + GetFactionKey(), LogLevel.NORMAL);
	}

	//--------------------------------------------------------------------------------------------
	//! Dedup guard, same reasoning as SCR_LoadoutManager's IsAlreadyRegistered - repeated
	//! mode/round restarts should not stack duplicate group entries. Matches on group name
	//! since SCR_GroupPreset does not expose a resource/GUID accessor to match on.
	protected bool IsAlreadyRegisteredGroup(notnull SCR_GroupRolePresetConfig candidate)
	{
		string candidateName = candidate.GetGroupName();
		foreach (SCR_GroupRolePresetConfig existing : m_aGroupRolePresetConfigs)
		{
			if (existing && existing.GetGroupName() == candidateName)
				return true;
		}
		return false;
	}
}
