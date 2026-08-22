//------------------------------------------------------------------------------------------------
//! Customization config (Configs/KitSystem/RK29_KitSetup.conf + Rosters/ + Catalogs/).
//! m_sKitName must match m_sLoadoutName in GM29_Kits.conf. No entry = kit not customizable.
//------------------------------------------------------------------------------------------------

[BaseContainerProps()]
class RK29_OpticOption
{
	[Attribute(desc: "The optic attachment prefab - identity of this choice", params: "et", category: "29th")]
	ResourceName m_sOpticPrefab;

	[Attribute(desc: "Mounts/rails seated onto the weapon before the optic, in order. Empty = optic attaches directly", category: "29th")]
	ref array<ResourceName> m_aRequiredAttachments;

	[Attribute(desc: "Escape hatch: swap the primary to this pre-authored weapon+optic variant instead of attaching anything", params: "et", category: "29th")]
	ResourceName m_sWeaponVariantPrefab;

	[Attribute(desc: "Picker label override. Empty = the prefab's in-game display name", category: "29th")]
	string m_sDisplayName;
}

[BaseContainerProps()]
class RK29_OpticCategory
{
	[Attribute(desc: "Category name shown to players, e.g. '1x' or 'Magnified'", category: "29th")]
	string m_sName;

	[Attribute("0", desc: "Counts toward the HUD magnified tally", category: "29th")]
	bool m_bMagnified;

	[Attribute(desc: "Optic choices in this category", category: "29th")]
	ref array<ref RK29_OpticOption> m_aOptics;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps()]
class RK29_WeaponOption
{
	[Attribute(desc: "Weapon prefab", params: "et", category: "29th")]
	ResourceName m_sWeaponPrefab;

	[Attribute(desc: "This weapon's magazine prefab - the mag auto-swap target", params: "et", category: "29th")]
	ResourceName m_sMagazinePrefab;

	[Attribute("0", desc: "Magazines carried with this weapon. 0 = one-for-one swap of the kit's captured mags", category: "29th")]
	int m_iMagazineCount;

	[Attribute(desc: "Apply this kit (body, rig, items) instead of the class's own when this weapon is chosen. Empty = class kit", category: "29th")]
	string m_sSourceKitName;

	[Attribute(desc: "Short display name for the picker", category: "29th")]
	string m_sDisplayName;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps()]
class RK29_ClassSetup
{
	[Attribute(desc: "Must match m_sLoadoutName in GM29_Kits.conf", category: "29th")]
	string m_sKitName;

	[Attribute(desc: "Short display name for picker/HUD rows (kit names are long)", category: "29th")]
	string m_sDisplayName;

	[Attribute(desc: "Selectable primary weapons. Empty = weapon locked to the authored one", category: "29th")]
	ref array<ref RK29_WeaponOption> m_aWeapons;

	[Attribute(desc: "Optic category NAMES this class may pick from. Empty = optic locked to kit default", category: "29th")]
	ref array<string> m_aOpticCategories;

	[Attribute(desc: "Optic prefab the picker pre-selects (opt-OUT for scoped classes). Empty = None/irons. Must be inside an allowed category", params: "et", category: "29th")]
	ResourceName m_sDefaultOptic;

	[Attribute(desc: "Specific optics removed from the referenced categories for this class", params: "et", category: "29th")]
	ref array<ResourceName> m_aOpticExclude;

	[Attribute(desc: "Specific optics allowed on top of the categories. Must be defined in some library category (badge/mounts come from there)", params: "et", category: "29th")]
	ref array<ResourceName> m_aOpticInclude;

	[Attribute("0", desc: "Legacy kit: deploy-menu selectable, hidden in the picker, HUD-counted under its display name", category: "29th")]
	bool m_bLegacyHidden;

	[Attribute(desc: "This kit's composition. Empty = capture the kit prefab", params: "conf class=RK29_KitComposition", category: "29th")]
	ResourceName m_sComposition;
}

//------------------------------------------------------------------------------------------------
//! One faction's classes - Configs/KitSystem/Rosters/RK29_Roster_*.conf
[BaseContainerProps(configRoot: true)]
class RK29_SideSetup
{
	[Attribute(desc: "Faction key this file covers, e.g. US", category: "29th")]
	string m_sFactionKey;

	[Attribute(desc: "This side's classes", category: "29th")]
	ref array<ref RK29_ClassSetup> m_aClasses;
}

//------------------------------------------------------------------------------------------------
//! Kits offered to one squad, keyed by the squad's group name ("29th Squad" etc.).
[BaseContainerProps(), BaseContainerCustomTitleField("m_sGroupName")]
class RK29_SquadKits
{
	[Attribute(desc: "Squad group name from the group presets, e.g. '29th Squad'. '*' = default for squads without an entry", category: "29th")]
	string m_sGroupName;

	[Attribute(desc: "Kit names offered to this squad. Empty = all faction kits", category: "29th")]
	ref array<string> m_aKitNames;
}

//! Squad kit catalog - Configs/KitSystem/Catalogs/RK29_Squads.conf
[BaseContainerProps(configRoot: true)]
class RK29_SquadKitCatalog
{
	[Attribute(desc: "Per-squad kit lists", category: "29th")]
	ref array<ref RK29_SquadKits> m_aSquads;
}

//------------------------------------------------------------------------------------------------
//! Optic category definitions - Configs/KitSystem/Catalogs/*.conf
[BaseContainerProps(configRoot: true)]
class RK29_OpticLibrary
{
	[Attribute(desc: "Optic categories", category: "29th")]
	ref array<ref RK29_OpticCategory> m_aOpticCategories;
}

[BaseContainerProps(configRoot: true)]
class RK29_KitSetup
{
	[Attribute("0", desc: "Behavior when the 29th Round Timer is NOT loaded: 1 = open (HUD + live re-kit always), 0 = closed (deploy-only kit choice)", category: "29th")]
	bool m_bNoTimerOpen;

	[Attribute("0", desc: "1 = print the per-item apply trace (every strip, container weighing and placement). Debugging aid - leave off on a live server, a briefing would print thousands of lines", category: "29th")]
	bool m_bVerboseLogging;

	[Attribute(desc: "Per-side class configs (Sides folder)", params: "conf", category: "29th")]
	ref array<ResourceName> m_aSideConfigs;

	[Attribute(desc: "Optic library configs (Helpers folder)", params: "conf", category: "29th")]
	ref array<ResourceName> m_aOpticConfigs;

	[Attribute(desc: "Item alias catalogs (Helpers folder)", params: "conf class=RK29_ItemAliasCatalog", category: "29th")]
	ref array<ResourceName> m_aAliasConfigs;

	[Attribute(desc: "Magazine set catalogs (Helpers folder)", params: "conf class=RK29_MagazineSetCatalog", category: "29th")]
	ref array<ResourceName> m_aMagSetConfigs;

	[Attribute(desc: "Squad kit catalogs (Helpers folder)", params: "conf class=RK29_SquadKitCatalog", category: "29th")]
	ref array<ResourceName> m_aSquadConfigs;

	[Attribute(desc: "Squad kit lists - usually loaded from Helpers configs", category: "29th")]
	ref array<ref RK29_SquadKits> m_aSquads;

	//! Merged at load from the referenced catalogs.
	[Attribute(desc: "Item aliases - usually loaded from Helpers configs", category: "29th")]
	ref array<ref RK29_ItemAlias> m_aAliases;

	[Attribute(desc: "Magazine variant sets - usually loaded from Helpers configs", category: "29th")]
	ref array<ref RK29_MagazineSet> m_aMagazineSets;

	//! Merged at load from the referenced configs (inline entries here also allowed).
	[Attribute(desc: "Optic categories - usually loaded from Helpers configs", category: "29th")]
	ref array<ref RK29_OpticCategory> m_aOpticCategories;

	[Attribute(desc: "Classes - usually loaded from Sides configs", category: "29th")]
	ref array<ref RK29_ClassSetup> m_aClasses;

	//--------------------------------------------------------------------------------------------
	RK29_ClassSetup FindClass(string kitName)
	{
		if (!m_aClasses)
			return null;
		foreach (RK29_ClassSetup c : m_aClasses)
		{
			if (c && c.m_sKitName == kitName)
				return c;
		}
		return null;
	}

	//--------------------------------------------------------------------------------------------
	//! Squad entry by group name; falls back to the "*" default entry, else null.
	RK29_SquadKits FindSquadKits(string groupName)
	{
		if (!m_aSquads)
			return null;
		RK29_SquadKits fallback;
		foreach (RK29_SquadKits sq : m_aSquads)
		{
			if (!sq)
				continue;
			if (sq.m_sGroupName == groupName)
				return sq;
			if (sq.m_sGroupName == "*")
				fallback = sq;
		}
		return fallback;
	}

	//--------------------------------------------------------------------------------------------
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

	//--------------------------------------------------------------------------------------------
	//! Alias -> prefab for a faction. Empty result = unresolved (caller logs).
	//! Placement preference authored on an alias for this faction, or null.
	array<string> ResolveAliasPreference(string alias, string factionKey)
	{
		if (!m_aAliases)
			return null;
		foreach (RK29_ItemAlias a : m_aAliases)
		{
			if (!a || a.m_sAlias != alias || !a.m_aPerFaction)
				continue;
			foreach (RK29_ItemAliasEntry e : a.m_aPerFaction)
			{
				if (e && e.m_sFactionKey == factionKey)
					return e.m_aPreferredContainers;
			}
			return null;
		}
		return null;
	}

	//--------------------------------------------------------------------------------------------
	ResourceName ResolveAlias(string alias, string factionKey)
	{
		if (!m_aAliases)
			return ResourceName.Empty;
		foreach (RK29_ItemAlias a : m_aAliases)
		{
			if (!a || a.m_sAlias != alias || !a.m_aPerFaction)
				continue;
			foreach (RK29_ItemAliasEntry e : a.m_aPerFaction)
			{
				if (e && e.m_sFactionKey == factionKey)
					return e.m_sPrefab;
			}
			return ResourceName.Empty;
		}
		return ResourceName.Empty;
	}

	//--------------------------------------------------------------------------------------------
	RK29_OpticCategory FindCategory(string name)
	{
		if (!m_aOpticCategories)
			return null;
		foreach (RK29_OpticCategory cat : m_aOpticCategories)
		{
			if (cat && cat.m_sName == name)
				return cat;
		}
		return null;
	}

	//--------------------------------------------------------------------------------------------
	//! Option for this optic prefab within the class's allowed set, null if not allowed.
	//! Allowed = (referenced categories minus m_aOpticExclude) plus m_aOpticInclude.
	RK29_OpticOption FindOpticOption(RK29_ClassSetup cls, ResourceName optic)
	{
		if (optic == ResourceName.Empty || !cls)
			return null;

		if (cls.m_aOpticInclude && cls.m_aOpticInclude.Contains(optic))
			return FindOpticOptionAnywhere(optic);

		if (cls.m_aOpticExclude && cls.m_aOpticExclude.Contains(optic))
			return null;

		if (!cls.m_aOpticCategories)
			return null;
		foreach (string catName : cls.m_aOpticCategories)
		{
			RK29_OpticCategory cat = FindCategory(catName);
			if (!cat || !cat.m_aOptics)
				continue;
			foreach (RK29_OpticOption opt : cat.m_aOptics)
			{
				if (opt && opt.m_sOpticPrefab == optic)
					return opt;
			}
		}
		return null;
	}

	//--------------------------------------------------------------------------------------------
	//! Option definition from any library category (metadata source for class includes).
	RK29_OpticOption FindOpticOptionAnywhere(ResourceName optic)
	{
		if (optic == ResourceName.Empty || !m_aOpticCategories)
			return null;
		foreach (RK29_OpticCategory cat : m_aOpticCategories)
		{
			if (!cat || !cat.m_aOptics)
				continue;
			foreach (RK29_OpticOption opt : cat.m_aOptics)
			{
				if (opt && opt.m_sOpticPrefab == optic)
					return opt;
			}
		}
		return null;
	}

	//--------------------------------------------------------------------------------------------
	//! Category holding this optic (badge source), null when unknown.
	RK29_OpticCategory CategoryOf(ResourceName optic)
	{
		if (optic == ResourceName.Empty || !m_aOpticCategories)
			return null;
		foreach (RK29_OpticCategory cat : m_aOpticCategories)
		{
			if (!cat || !cat.m_aOptics)
				continue;
			foreach (RK29_OpticOption opt : cat.m_aOptics)
			{
				if (opt && opt.m_sOpticPrefab == optic)
					return cat;
			}
		}
		return null;
	}

	//--------------------------------------------------------------------------------------------
	bool IsOpticAllowed(RK29_ClassSetup cls, ResourceName optic)
	{
		if (optic == ResourceName.Empty)
			return true; // None is always a legal choice
		return FindOpticOption(cls, optic) != null;
	}

	//--------------------------------------------------------------------------------------------
	bool IsOpticMagnified(ResourceName optic)
	{
		if (optic == ResourceName.Empty || !m_aOpticCategories)
			return false;
		foreach (RK29_OpticCategory cat : m_aOpticCategories)
		{
			if (!cat || !cat.m_bMagnified || !cat.m_aOptics)
				continue;
			foreach (RK29_OpticOption opt : cat.m_aOptics)
			{
				if (opt && opt.m_sOpticPrefab == optic)
					return true;
			}
		}
		return false;
	}

	//--------------------------------------------------------------------------------------------
	RK29_WeaponOption FindWeapon(RK29_ClassSetup cls, ResourceName weapon)
	{
		if (!cls || !cls.m_aWeapons)
			return null;
		foreach (RK29_WeaponOption w : cls.m_aWeapons)
		{
			if (w && w.m_sWeaponPrefab == weapon)
				return w;
		}
		return null;
	}

	//--------------------------------------------------------------------------------------------
	//! All magazine prefabs of this class's weapons - the mag auto-swap strip set.
	void GetClassMagazines(RK29_ClassSetup cls, notnull array<ResourceName> outMags)
	{
		if (!cls || !cls.m_aWeapons)
			return;
		foreach (RK29_WeaponOption w : cls.m_aWeapons)
		{
			if (w && w.m_sMagazinePrefab != ResourceName.Empty && !outMags.Contains(w.m_sMagazinePrefab))
				outMags.Insert(w.m_sMagazinePrefab);
		}
	}
}
