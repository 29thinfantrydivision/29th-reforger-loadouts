//------------------------------------------------------------------------------------------------
//! Customization config (Configs/KitSystem/RK29_KitSetup.conf + Rosters/ + Catalogs/).
//! A class row here is what makes a kit exist. A matching m_sLoadoutName in GM29_Kits.conf gives
//! it a deploy-menu row too; without one it is a picker-only kit. No class row = no kit.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! One ammo type a weapon can field. The alias is the name blocks use ("belt", "mag"); how it
//! resolves is the weapon's business.
[BaseContainerProps(), BaseContainerCustomTitleField("m_sAlias")]
class RK29_WeaponAmmoDef
{
	[Attribute(desc: "Name blocks refer to this ammo by, within this weapon", category: "29th")]
	string m_sAlias;

	[Attribute(desc: "Magazine variant from RK29_Magazines.conf, resolved through this weapon's magazine well. Empty and no prefab = the weapon's default magazine", category: "29th")]
	string m_sVariant;

	[Attribute(desc: "Literal prefab. Wins over the variant - use for ammo that is not a magazine", params: "et", category: "29th")]
	ResourceName m_sPrefab;
}

[BaseContainerProps(), SCR_BaseContainerCustomTitleFields({"m_sFactionKey", "m_sPrefab"}, "%1 - %2")]
class RK29_WeaponFactionPrefab
{
	[Attribute(desc: "Faction key, e.g. US", category: "29th")]
	string m_sFactionKey;

	[Attribute(desc: "Prefab for that faction", params: "et", category: "29th")]
	ResourceName m_sPrefab;
}

//------------------------------------------------------------------------------------------------
//! A weapon, defined once and referenced by id. Holds only what is true of the weapon everywhere -
//! gear belongs to the class fielding it, since shared rifles would otherwise fight over dress.
[BaseContainerProps(), BaseContainerCustomTitleField("m_sId")]
class RK29_WeaponDef
{
	[Attribute(desc: "Id options refer to, e.g. \"m249\"", category: "29th")]
	string m_sId;

	[Attribute(desc: "Weapon prefab. Leave empty and use m_aPerFaction when each faction fields a different weapon under this id", params: "et", category: "29th")]
	ResourceName m_sPrefab;

	[Attribute(desc: "Per-faction prefabs, for an id both sides field differently (sidearm)", category: "29th")]
	ref array<ref RK29_WeaponFactionPrefab> m_aPerFaction;

	[Attribute(desc: "Ammo types this weapon can field", category: "29th")]
	ref array<ref RK29_WeaponAmmoDef> m_aAmmo;

	[Attribute(desc: "Where THIS WEAPON'S magazines should go, best first - same tokens as an item's preferred containers (uniform, trouser, rig, pack, or part of a container name). Anything left off the list is still used, but only once everything listed is full: leaving \"pack\" off is how a magazine ends up in the backpack last rather than first", category: "29th")]
	ref array<string> m_aPreferredContainers;

	[Attribute("50", desc: "How important THIS WEAPON'S ammunition is when the kit does not fit: lower keeps its place, higher gives way to it. 50 is the default and the fighting load stays there. Utilities are authored at 10 and medical at 20 on their aliases, smokes and flares at 60, frags at 70; the AT launchers state 80 here so their rounds are the first to give way. A choice group's or entry's own number overrides this - the 40mm pools say 80 that way, so a grenadier's rifle magazines stay at 50", category: "29th")]
	int m_iKeepRank;

	// Typed to RK29_AmmoGroup on purpose: a weapon group or attachment point here would deserialize
	// happily and then offer the gun no magazines.
	[Attribute(desc: "This weapon's ammo choices, inline - ammo is intrinsic to the gun, so it lives here rather than in a catalog. Entry counts are the line-standard defaults; classes deviate through overrides. Empty group id = \"<weaponId>_ammo\", the name overrides address it by", category: "29th")]
	ref RK29_AmmoGroup m_AmmoGroup;

	[Attribute(desc: "SHARED choice groups this weapon owns by reference - attachment points and anything genuinely cross-weapon. Capability, stated once per gun; what a class actually offers of it is the overrides' business", category: "29th")]
	ref array<string> m_aGroups;
}

//------------------------------------------------------------------------------------------------
//! Weapon catalog - Configs/KitSystem/Catalogs/RK29_Weapons.conf
[BaseContainerProps(configRoot: true)]
class RK29_WeaponCatalog
{
	[Attribute(desc: "Weapon definitions", category: "29th")]
	ref array<ref RK29_WeaponDef> m_aWeapons;
}

//------------------------------------------------------------------------------------------------
//! One class in a side's roster. A class row states no doctrine: optics come from the
//! m_bIsOpticsPoint groups its weapons offer, override from the composition's m_aOverrides, and the
//! magnified exemption from the attachment group.
[BaseContainerProps(), SCR_BaseContainerCustomTitleFields({"m_sDisplayName", "m_sKitName"}, "%1 - %2")]
class RK29_ClassSetup
{
	[Attribute(desc: "This kit's name. Matching m_sLoadoutName in GM29_Kits.conf gives it a deploy-menu row of its own; with no match it is a picker-only kit - offered in the kit menu, spawned through the Current Kit row", category: "29th")]
	string m_sKitName;

	[Attribute(desc: "Short display name for picker/HUD rows (kit names are long)", category: "29th")]
	string m_sDisplayName;

	[Attribute(desc: "This kit's composition. Empty = capture the kit prefab", params: "conf class=RK29_KitComposition", category: "29th")]
	ResourceName m_sComposition;

	//! The three below are stamped at load from the owning side config - a class row names neither
	//! its faction nor its body.
	string m_sSideFactionKey;

	ResourceName m_sSideBodyPrefab;

	string m_sSideDefaultKit;

	//------------------------------------------------------------------------------------------------
	//! A method rather than a plain field read, so a per-class body override can come back in one
	//! place if a body ever differs in something config cannot dress (an extra weapon slot).
	ResourceName BodyPrefab()
	{
		return m_sSideBodyPrefab;
	}
}

//------------------------------------------------------------------------------------------------
//! One faction's classes - Configs/KitSystem/Rosters/RK29_Roster_*.conf
[BaseContainerProps(configRoot: true)]
class RK29_SideSetup
{
	[Attribute(desc: "Faction key this file covers, e.g. US", category: "29th")]
	string m_sFactionKey;

	[Attribute(desc: "Body every class on this side spawns as. Faction identity - affiliation, voices, identity - lives here and config cannot dress it, which is the one thing that genuinely needs a prefab per side", params: "et", category: "29th")]
	ResourceName m_sBodyPrefab;

	[Attribute(desc: "Kit a player of this side gets before they have ever picked one - what Current Kit spawns on a first deploy. Must be a kit name from m_aClasses. Ignored when it names no class of this side (Current Kit then starts on the side's first class)", category: "29th")]
	string m_sDefaultKitName;

	[Attribute(desc: "This side's classes", category: "29th")]
	ref array<ref RK29_ClassSetup> m_aClasses;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(configRoot: true)]
class RK29_KitSetup
{
	[Attribute("0", desc: "Phase assumed when the 29th Round Timer is NOT loaded: 1 = preround (briefing HUD shown, a live re-kit heals and goes unannounced), 0 = live (no HUD, every live re-kit skips the heal and is announced to the whole server)", category: "29th")]
	bool m_bNoTimerOpen;

	[Attribute("0", desc: "1 = print the per-item apply trace (every strip, container weighing and placement). Debugging aid - leave off on a live server, a briefing would print thousands of lines", category: "29th")]
	bool m_bVerboseLogging;

	[Attribute(desc: "Per-side class configs (Rosters folder)", params: "conf", category: "29th")]
	ref array<ResourceName> m_aSideConfigs;

	[Attribute(desc: "Item alias catalogs (Catalogs folder)", params: "conf class=RK29_ItemAliasCatalog", category: "29th")]
	ref array<ResourceName> m_aAliasConfigs;

	[Attribute(desc: "Magazine set catalogs (Catalogs folder)", params: "conf class=RK29_MagazineSetCatalog", category: "29th")]
	ref array<ResourceName> m_aMagSetConfigs;

	[Attribute(desc: "Weapon catalogs (Catalogs folder)", params: "conf class=RK29_WeaponCatalog", category: "29th")]
	ref array<ResourceName> m_aWeaponConfigs;

	[Attribute(desc: "Choice group catalogs (Catalogs folder)", params: "conf class=RK29_ChoiceGroupCatalog", category: "29th")]
	ref array<ResourceName> m_aChoiceConfigs;

	[Attribute(desc: "Attachment catalogs (Catalogs folder)", params: "conf class=RK29_AttachmentCatalog", category: "29th")]
	ref array<ResourceName> m_aAttachmentConfigs;

	[Attribute(desc: "Override catalogs - steps a kit names by id rather than copying", category: "29th")]
	ref array<ResourceName> m_aOverrideConfigs;

	//============================================================================================
	// Runtime - merge destinations. RK29_KitManager.LoadSetup appends to these at boot, so
	// authoring one here is a duplicate sitting ahead of the catalog's, not an override. Hidden
	// (uiwidget None) rather than deleted so old data still deserializes - vanilla does the same
	// with SCR_PowerPole.m_aSlots. Kept as ref arrays because LoadSetup null-guards them.
	//============================================================================================

	[Attribute(uiwidget: UIWidgets.None, desc: "RUNTIME ONLY - merged at boot from m_aWeaponConfigs", category: "29th")]
	ref array<ref RK29_WeaponDef> m_aWeaponDefs;

	[Attribute(uiwidget: UIWidgets.None, desc: "RUNTIME ONLY - merged at boot from m_aAliasConfigs", category: "29th")]
	ref array<ref RK29_ItemAlias> m_aAliases;

	[Attribute(uiwidget: UIWidgets.None, desc: "RUNTIME ONLY - merged at boot from m_aMagSetConfigs", category: "29th")]
	ref array<ref RK29_MagazineSet> m_aMagazineSets;

	[Attribute(uiwidget: UIWidgets.None, desc: "RUNTIME ONLY - merged at boot from m_aSideConfigs", category: "29th")]
	ref array<ref RK29_ClassSetup> m_aClasses;

	[Attribute(uiwidget: UIWidgets.None, desc: "RUNTIME ONLY - merged at boot from m_aChoiceConfigs", category: "29th")]
	ref array<ref RK29_ChoiceGroup> m_aChoiceGroups;

	[Attribute(uiwidget: UIWidgets.None, desc: "RUNTIME ONLY - merged at boot from m_aAttachmentConfigs", category: "29th")]
	ref array<ref RK29_AttachmentDef> m_aAttachments;

	[Attribute(uiwidget: UIWidgets.None, desc: "RUNTIME ONLY - merged at boot from m_aOverrideConfigs", category: "29th")]
	ref array<ref RK29_Override> m_aOverrides;

	//============================================================================================
	// ID indices, built on first ask and never invalidated. Lazy is safe only because
	// RK29_KitManager.LoadSetup finishes every catalog merge before any Find is asked and assigns
	// a fresh RK29_KitSetup each call; nothing else appends to the runtime lists. First match wins
	// - a duplicate id is ignored, not overwritten. Values are weak; the ref arrays above own them.
	//============================================================================================

	protected ref map<string, RK29_ChoiceGroup> m_mChoiceGroupIndex;
	protected ref map<string, RK29_Override> m_mOverrideIndex;
	protected ref map<string, RK29_AttachmentDef> m_mAttachmentIndex;
	protected ref map<string, RK29_ClassSetup> m_mClassIndex;
	protected ref map<string, RK29_ItemAlias> m_mAliasIndex;
	protected ref map<string, RK29_WeaponDef> m_mWeaponDefIndex;

	//------------------------------------------------------------------------------------------------
	RK29_ChoiceGroup FindChoiceGroup(string id)
	{
		if (id == "" || !m_aChoiceGroups)
			return null;

		if (!m_mChoiceGroupIndex)
		{
			m_mChoiceGroupIndex = new map<string, RK29_ChoiceGroup>();
			foreach (RK29_ChoiceGroup g : m_aChoiceGroups)
			{
				if (g && !m_mChoiceGroupIndex.Contains(g.m_sId))
					m_mChoiceGroupIndex.Set(g.m_sId, g);
			}
		}

		return m_mChoiceGroupIndex.Get(id);
	}

	//------------------------------------------------------------------------------------------------
	RK29_Override FindOverride(string id)
	{
		if (id == "" || !m_aOverrides)
			return null;

		if (!m_mOverrideIndex)
		{
			m_mOverrideIndex = new map<string, RK29_Override>();
			foreach (RK29_Override p : m_aOverrides)
			{
				if (p && !m_mOverrideIndex.Contains(p.m_sId))
					m_mOverrideIndex.Set(p.m_sId, p);
			}
		}

		return m_mOverrideIndex.Get(id);
	}

	//------------------------------------------------------------------------------------------------
	RK29_AttachmentDef FindAttachmentDef(string id)
	{
		if (id == "" || !m_aAttachments)
			return null;

		if (!m_mAttachmentIndex)
		{
			m_mAttachmentIndex = new map<string, RK29_AttachmentDef>();
			foreach (RK29_AttachmentDef def : m_aAttachments)
			{
				if (def && !m_mAttachmentIndex.Contains(def.m_sId))
					m_mAttachmentIndex.Set(def.m_sId, def);
			}
		}

		return m_mAttachmentIndex.Get(id);
	}

	//------------------------------------------------------------------------------------------------
	//! No empty-name guard: a class authored with no kit name is findable by "".
	RK29_ClassSetup FindClass(string kitName)
	{
		if (!m_aClasses)
			return null;

		if (!m_mClassIndex)
		{
			m_mClassIndex = new map<string, RK29_ClassSetup>();
			foreach (RK29_ClassSetup c : m_aClasses)
			{
				if (c && !m_mClassIndex.Contains(c.m_sKitName))
					m_mClassIndex.Set(c.m_sKitName, c);
			}
		}

		return m_mClassIndex.Get(kitName);
	}

	//------------------------------------------------------------------------------------------------
	//! The side's configured starting kit, "" when that side never declared one.
	string DefaultKitName(string factionKey)
	{
		if (!m_aClasses)
			return "";
		foreach (RK29_ClassSetup c : m_aClasses)
		{
			if (c && c.m_sSideFactionKey == factionKey && c.m_sSideDefaultKit != "")
				return c.m_sSideDefaultKit;
		}
		return "";
	}

	//------------------------------------------------------------------------------------------------
	//! Variant prefab for any of the given wells, empty when absent.
	ResourceName FindMagVariant(notnull array<string> wells, string variantName)
	{
		if (!m_aMagazineSets)
			return ResourceName.Empty;
		foreach (RK29_MagazineSet magSet : m_aMagazineSets)
		{
			if (!magSet || !magSet.m_aVariants || !wells.Contains(magSet.m_sMagazineWell))
				continue;
			foreach (RK29_MagVariant v : magSet.m_aVariants)
			{
				if (v && v.m_sName == variantName)
					return v.m_sPrefab;
			}
		}
		return ResourceName.Empty;
	}

	//------------------------------------------------------------------------------------------------
	//! Authored keep rank, or KEEP_RANK_DEFAULT for an alias that is not there.
	int ResolveAliasKeepRank(string alias)
	{
		RK29_ItemAlias a = FindAlias(alias);
		if (!a)
			return RK29_KitItemBatch.KEEP_RANK_DEFAULT;
		return a.m_iKeepRank;
	}

	//------------------------------------------------------------------------------------------------
	//! Alias-authored placement preference for this faction, or null. An item entry's own list beats
	//! this - see the placement-precedence note on RK29_KitCompose.EmitAmmo.
	array<string> ResolveAliasPreference(string alias, string factionKey)
	{
		RK29_ItemAlias a = FindAlias(alias);
		if (!a || !a.m_aPerFaction)
			return null;

		// an exact faction entry wins wherever it sits; an entry naming no faction is the fallback
		array<string> factionless;
		foreach (RK29_ItemAliasEntry e : a.m_aPerFaction)
		{
			if (!e || !e.m_aPreferredContainers || e.m_aPreferredContainers.IsEmpty())
				continue;
			if (e.m_sFactionKey == factionKey)
				return e.m_aPreferredContainers;
			if (e.m_sFactionKey == "" && !factionless)
				factionless = e.m_aPreferredContainers;
		}
		return factionless;
	}

	//------------------------------------------------------------------------------------------------
	protected RK29_ItemAlias FindAlias(string alias)
	{
		if (!m_aAliases)
			return null;

		if (!m_mAliasIndex)
		{
			m_mAliasIndex = new map<string, RK29_ItemAlias>();
			foreach (RK29_ItemAlias a : m_aAliases)
			{
				if (a && !m_mAliasIndex.Contains(a.m_sAlias))
					m_mAliasIndex.Set(a.m_sAlias, a);
			}
		}

		return m_mAliasIndex.Get(alias);
	}

	//------------------------------------------------------------------------------------------------
	//! Alias -> prefab for a faction, falling back to an entry that names no faction.
	//! Empty result = unresolved (caller logs).
	ResourceName ResolveAlias(string alias, string factionKey)
	{
		RK29_ItemAlias a = FindAlias(alias);
		if (!a || !a.m_aPerFaction)
			return ResourceName.Empty;

		// An exact faction entry wins wherever it sits; an entry naming no faction is the fallback,
		// so a third faction inherits everything that is genuinely the same.
		ResourceName factionless;
		foreach (RK29_ItemAliasEntry e : a.m_aPerFaction)
		{
			if (!e || e.m_sPrefab == ResourceName.Empty)
				continue;
			if (e.m_sFactionKey == factionKey)
				return e.m_sPrefab;
			if (e.m_sFactionKey == "" && factionless == ResourceName.Empty)
				factionless = e.m_sPrefab;
		}
		return factionless;
	}

	//------------------------------------------------------------------------------------------------
	RK29_WeaponDef FindWeaponDef(string id)
	{
		if (id == "" || !m_aWeaponDefs)
			return null;

		if (!m_mWeaponDefIndex)
		{
			m_mWeaponDefIndex = new map<string, RK29_WeaponDef>();
			foreach (RK29_WeaponDef def : m_aWeaponDefs)
			{
				if (def && !m_mWeaponDefIndex.Contains(def.m_sId))
					m_mWeaponDefIndex.Set(def.m_sId, def);
			}
		}

		return m_mWeaponDefIndex.Get(id);
	}
}
