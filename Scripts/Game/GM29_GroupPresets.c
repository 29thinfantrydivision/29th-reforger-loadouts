//------------------------------------------------------------------------------------------------
//! 29th Infantry Division - predefined squad registration
//!
//! Same shape as GM29_KitLoadouts.c:
//!   1. GM29_GroupPresetHolder - the config root that Configs/Groups/GM29_Groups.conf
//!      deserialises into. We own this class, so its field name (m_aGroupPresets) is stable.
//!   2. modded SCR_Faction - merges the presets in that holder into m_aGroupRolePresetConfigs
//!      on every faction instance (US and USSR both pick these up, since each faction's
//!      Init() runs this independently). No per-faction editing of the base BLUFOR/OPFOR conf.
//!      Merge means REPLACE-BY-ROLE, not append: a role is served by the first preset that
//!      declares it, and the campaign faction confs already claim ASSAULT, MECHANIZED,
//!      COMMANDER and RESERVES - so an appended 29th preset never applies. Roles the 29th does
//!      not define (RECON, MEDIC, MORTAR, ...) keep their vanilla presets untouched.
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

		// Vanilla SCR_Faction.Init() early-returns in edit mode before it touches catalogs.
		// Match that: injecting presets while the World Editor is open mutates
		// m_aGroupRolePresetConfigs on the in-editor faction instances and spams warnings.
		if (SCR_Global.IsEditMode())
			return;

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
		int replaced = 0;
		foreach (SCR_GroupRolePresetConfig preset : holder.m_aGroupPresets)
		{
			if (!preset)
				continue;

			// REPLACE by role, do not append. Every consumer of this array resolves a role by
			// taking the FIRST preset that declares it - group creation
			// (SCR_PlayerControllerGroupComponent.CreateNewGroup), the role label
			// (SCR_AIGroup.GetGroupRoleName) and the deploy-menu loadout filter
			// (SCR_AIGroup.IsLoadoutInGroup) all break on the first match. A campaign faction
			// conf already ships presets for ASSAULT/MECHANIZED/COMMANDER/RESERVES, so an
			// appended 29th preset is dead config: creating "29th HQ" would apply vanilla's
			// Commander preset - vanilla name, vanilla size, vanilla loadout list.
			int existingIdx = FindGroupPresetIndexByRole(preset.GetGroupRole());
			if (existingIdx < 0)
			{
				m_aGroupRolePresetConfigs.Insert(preset);
				added = added + 1;
				continue;
			}

			// Re-running (mode/round restart) lands here and overwrites our own entry with an
			// identical one, so injection stays idempotent instead of stacking duplicates.
			m_aGroupRolePresetConfigs.Set(existingIdx, preset);
			replaced = replaced + 1;
		}

		// Plain concatenation - no ternary inside Print(), per your guardrails.
		Print("[GM29Groups] " + GetFactionKey() + ": added " + added.ToString()
			+ " group role presets, replaced " + replaced.ToString(), LogLevel.NORMAL);
	}

	//--------------------------------------------------------------------------------------------
	//! Index of the first registered preset serving this role, -1 when the role is unclaimed.
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
