//------------------------------------------------------------------------------------------------
//! 29th predefined squad registration. GM29_GroupPresetHolder is the config root
//! Configs/Groups/GM29_Groups.conf deserialises into; modded SCR_Faction.Init merges its presets
//! into every faction's m_aGroupRolePresetConfigs, each faction independently.
//! SCR_GroupRolePresetConfig rather than m_aPredefinedGroups because it also carries
//! m_aLoadoutResources, which restricts the loadouts offered inside the group. Those lists hold
//! both factions' resources per role, which is safe unfiltered: IsLoadoutInGroup() is only
//! checked against a loadout the player already has.
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

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity owner)
	{
		super.Init(owner);

		// vanilla SCR_Faction.Init() early-returns in edit mode before it touches catalogs; injecting
		// presets while the World Editor is open mutates the in-editor faction instances.
		// Unconfirmed fix: reasoned from vanilla's guard, never seen fail or succeed.
		if (SCR_Global.IsEditMode())
			return;

		InjectGM29Groups();
	}

	//------------------------------------------------------------------------------------------------
	protected void InjectGM29Groups()
	{
		// vanilla's GetGroupRolePresetConfigs / IsGroupRolesConfigured deref this array with no
		// null check, so it must exist even when the holder below fails to load
		if (!m_aGroupRolePresetConfigs)
			m_aGroupRolePresetConfigs = {};

		Resource res = Resource.Load(m_sGM29GroupHolder);
		if (!res.IsValid())
		{
			Print("[GM29Groups] group holder not found - check m_sGM29GroupHolder GUID", LogLevel.WARNING);
			return;
		}

		GM29_GroupPresetHolder holder = GM29_GroupPresetHolder.Cast(BaseContainerTools.CreateInstanceFromContainer(res.GetResource().ToBaseContainer()));
		if (!holder || !holder.m_aGroupPresets)
		{
			Print("[GM29Groups] group holder empty or wrong root class", LogLevel.WARNING);
			return;
		}

		int added = 0;
		int replaced = 0;
		foreach (SCR_GroupRolePresetConfig preset : holder.m_aGroupPresets)
		{
			if (!preset)
				continue;

			// Replace by role, do not append: every consumer resolves a role by taking the first preset that
			// declares it (CreateNewGroup, GetGroupRoleName, IsLoadoutInGroup), and a campaign faction conf
			// already ships ASSAULT/MECHANIZED/COMMANDER/RESERVES - so an appended 29th preset is dead config
			// that silently applies vanilla's.
			int existingIdx = FindGroupPresetIndexByRole(preset.GetGroupRole());
			if (existingIdx < 0)
			{
				m_aGroupRolePresetConfigs.Insert(preset);
				added = added + 1;
				continue;
			}

			// re-running (mode/round restart) overwrites our own entry, so injection stays
			// idempotent
			m_aGroupRolePresetConfigs.Set(existingIdx, preset);
			replaced = replaced + 1;
		}

		Print(string.Format("[GM29Groups] %1: added %2 group role presets, replaced %3",
			GetFactionKey(), added, replaced), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! Role is the key rather than the group name: the name is what the 29th preset is trying to
	//! set, so matching on it would never find the vanilla entry it needs to displace.
	protected int FindGroupPresetIndexByRole(SCR_EGroupRole role)
	{
		for (int i = 0, count = m_aGroupRolePresetConfigs.Count(); i < count; i++)
		{
			SCR_GroupRolePresetConfig existing = m_aGroupRolePresetConfigs[i];
			if (existing && existing.GetGroupRole() == role)
				return i;
		}
		return -1;
	}
}
