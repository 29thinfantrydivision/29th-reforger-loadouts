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

	[Attribute("0", desc: "These optics are magnified - drives the picker's magnification badge and the HUD tally", category: "29th")]
	bool m_bMagnified;

	[Attribute("0", desc: "Magnified, but never counted in the HUD tally - a sniper's scope is the squad's sniper, not one of its magnified riflemen", category: "29th")]
	bool m_bTallyExempt;

	[Attribute(desc: "Optic choices in this category", category: "29th")]
	ref array<ref RK29_OpticOption> m_aOptics;
}

//------------------------------------------------------------------------------------------------
//! One ammo type a weapon can field. The alias is the name blocks use ("belt", "mag",
//! "rocket"); how it resolves is the weapon's business - a magazine variant through its own
//! magazine well, or a literal prefab for loads that are not magazines (a spare M72 tube).
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

//! One faction's prefab for a weapon id shared across factions - the sidearm is an M9 for
//! the US and a Makarov for the Soviets, so a shared role can name it once.
[BaseContainerProps(), BaseContainerCustomTitleField("m_sFactionKey")]
class RK29_WeaponFactionPrefab
{
	[Attribute(desc: "Faction key, e.g. US", category: "29th")]
	string m_sFactionKey;

	[Attribute(desc: "Prefab for that faction", params: "et", category: "29th")]
	ResourceName m_sPrefab;
}

//------------------------------------------------------------------------------------------------
//! A weapon, defined once and referenced by id. Holds only what is true of the weapon
//! everywhere - gear belongs to the class fielding it, since rifles are shared and would
//! otherwise fight over dress.
[BaseContainerProps(), BaseContainerCustomTitleField("m_sId")]
class RK29_WeaponDef
{
	[Attribute(desc: "Id options refer to, e.g. \"m249\"", category: "29th")]
	string m_sId;

	[Attribute(desc: "Weapon prefab. Leave empty and use m_aPerFaction when each faction fields a different weapon under this id", params: "et", category: "29th")]
	ResourceName m_sPrefab;

	[Attribute(desc: "Per-faction prefabs, for an id both sides field differently (sidearm)", category: "29th")]
	ref array<ref RK29_WeaponFactionPrefab> m_aPerFaction;

	[Attribute(desc: "OVERRIDE only. Leave empty and the picker uses the weapon's own in-game name, which is localised. Set this solely to say something the game does not, e.g. distinguishing two variants of one rifle", category: "29th")]
	string m_sName;

	[Attribute(desc: "Ammo types this weapon can field", category: "29th")]
	ref array<ref RK29_WeaponAmmoDef> m_aAmmo;

	[Attribute(desc: "Where THIS WEAPON'S magazines should go, best first - same tokens as an item's preferred containers (uniform, trouser, rig, pack, or part of a container name). Anything left off the list is still used, but only once everything listed is full: leaving \"pack\" off is how a magazine ends up in the backpack last rather than first", category: "29th")]
	ref array<string> m_aPreferredContainers;
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
//! Workbench shows array rows by title only, and an unset int has nothing to show - so a
//! slot-0 group would render blank. Name the slot instead of numbering it.
class RK29_WeaponSlotTitle : BaseContainerCustomTitle
{
	override bool _WB_GetCustomTitle(BaseContainer source, out string title)
	{
		int slot = 0;
		source.Get("m_iSlot", slot);

		string name;
		if (slot == 0)
			name = "primary";
		else if (slot == 1)
			name = "launcher";
		else if (slot == 2)
			name = "sidearm";
		else
			name = "slot " + slot.ToString();

		int count = 0;
		BaseContainerList options = source.GetObjectArray("m_aOptions");
		if (options)
			count = options.Count();

		if (count > 1)
			title = name + " - " + count.ToString() + " choices";
		else
			title = name;
		return true;
	}
}

//------------------------------------------------------------------------------------------------
//! Rows in a weapon slot read as the weapon they field.
class RK29_WeaponOptionTitle : BaseContainerCustomTitle
{
	override bool _WB_GetCustomTitle(BaseContainer source, out string title)
	{
		source.Get("m_sWeapon", title);
		if (title == "")
			title = "(no weapon)";
		return true;
	}
}

//------------------------------------------------------------------------------------------------
//! One weapon slot and everything that may fill it. Slot-keyed like clothing: a later
//! declaration REPLACES the whole group, so a kit can change its sidearm without restating
//! its rifle, and two groups can never fight over one slot.
[BaseContainerProps(), RK29_WeaponSlotTitle()]
class RK29_WeaponSlot
{
	[Attribute("0", desc: "0 primary, 1 launcher, 2 sidearm", category: "29th")]
	int m_iSlot;

	[Attribute(desc: "What may fill this slot. One entry = a fixed weapon; several = a picker column, first is the default", category: "29th")]
	ref array<ref RK29_WeaponOption> m_aOptions;
}

//------------------------------------------------------------------------------------------------
//! A weapon a class may field. The option is where a class says what IT does with a weapon:
//! which blocks come along (gear, ammo counts, grenade set) and which slot it fills. One
//! option for a slot = a fixed weapon; several = a picker column.
//! How much of one ammo type this class carries with this weapon. The alias is resolved by
//! the weapon definition, so "belt" means one magazine on an M249 and another on a PKM.
[BaseContainerProps(), BaseContainerCustomTitleField("m_sAlias")]
class RK29_WeaponAmmo
{
	[Attribute(desc: "Ammo alias declared by the weapon definition. For a weapon named directly rather than through the catalog, leave empty and use the variant", category: "29th")]
	string m_sAlias;

	[Attribute(desc: "Magazine variant from RK29_Magazines.conf, resolved through the weapon's own magazine well. Empty and no alias = the weapon's default magazine", category: "29th")]
	string m_sVariant;

	[Attribute(desc: "Literal prefab, for ammo that is not a magazine of this weapon (spare disposable launchers, a parade kit's exact magazine). Wins over everything else", params: "et", category: "29th")]
	ResourceName m_sPrefab;

	[Attribute("1", desc: "How many are carried", category: "29th")]
	int m_iCount;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), RK29_WeaponOptionTitle()]
class RK29_WeaponOption
{
	[Attribute(desc: "Weapon id from RK29_Weapons.conf", category: "29th")]
	string m_sWeapon;

	[Attribute(desc: "Ammo carried with this weapon, for this class", category: "29th")]
	ref array<ref RK29_WeaponAmmo> m_aAmmo;

	[Attribute(desc: "Blocks this weapon brings for this class - gear deltas, extra items, grenade set. Only needed when the weapon changes more than its ammo", category: "29th")]
	ref array<ref RK29_BlockRef> m_aBlocks;

	[Attribute(desc: "Overrides the catalog's display name", category: "29th")]
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

	[Attribute(desc: "Weapon catalogs (Catalogs folder)", params: "conf class=RK29_WeaponCatalog", category: "29th")]
	ref array<ResourceName> m_aWeaponConfigs;

	[Attribute(desc: "Weapon definitions - usually loaded from Catalogs configs", category: "29th")]
	ref array<ref RK29_WeaponDef> m_aWeaponDefs;

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
	//! Placement preference down the same chain: an item entry's own list beats the alias it
	//! names, which beats whatever that alias defers to.
	array<string> ResolveAliasPreference(string alias, string factionKey)
	{
		array<string> chain = {};
		AliasChain(alias, chain);
		foreach (string link : chain)
		{
			RK29_ItemAlias a = FindAlias(link);
			if (!a || !a.m_aPerFaction)
				continue;
			foreach (RK29_ItemAliasEntry e : a.m_aPerFaction)
			{
				if (e && e.m_sFactionKey == factionKey && e.m_aPreferredContainers
					&& !e.m_aPreferredContainers.IsEmpty())
					return e.m_aPreferredContainers;
			}
		}
		return null;
	}

	//--------------------------------------------------------------------------------------------
	//! An alias may defer to another (backpack_ce -> backpack_medium). Whatever the nearer
	//! alias states wins; it falls through only where it is silent. Same precedence as
	//! everywhere else in the config: the statement closest to the use site.
	protected static const int ALIAS_MAX_HOPS = 8;

	//--------------------------------------------------------------------------------------------
	RK29_ItemAlias FindAlias(string alias)
	{
		if (!m_aAliases)
			return null;
		foreach (RK29_ItemAlias a : m_aAliases)
		{
			if (a && a.m_sAlias == alias)
				return a;
		}
		return null;
	}

	//--------------------------------------------------------------------------------------------
	//! The alias chain, nearest first. Stops on a cycle rather than hanging the boot.
	void AliasChain(string alias, notnull array<string> outChain)
	{
		string current = alias;
		for (int hop = 0; hop < ALIAS_MAX_HOPS && current != ""; hop++)
		{
			if (outChain.Contains(current))
			{
				Print("[RK29] config ERROR - alias cycle at '" + current + "'", LogLevel.ERROR);
				return;
			}
			outChain.Insert(current);

			RK29_ItemAlias a = FindAlias(current);
			if (!a)
				return;
			current = a.m_sSameAs;
		}
	}

	//--------------------------------------------------------------------------------------------
	ResourceName ResolveAlias(string alias, string factionKey)
	{
		array<string> chain = {};
		AliasChain(alias, chain);
		foreach (string link : chain)
		{
			RK29_ItemAlias a = FindAlias(link);
			if (!a || !a.m_aPerFaction)
				continue;
			foreach (RK29_ItemAliasEntry e : a.m_aPerFaction)
			{
				if (e && e.m_sFactionKey == factionKey && e.m_sPrefab != ResourceName.Empty)
					return e.m_sPrefab;
			}
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
			if (!cat || !cat.m_bMagnified || cat.m_bTallyExempt || !cat.m_aOptics)
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
	//--------------------------------------------------------------------------------------------
	//! Catalog id for a prefab, so tooling can emit config in the shape kits actually take.
	string WeaponIdOf(ResourceName prefab, string factionKey)
	{
		if (!m_aWeaponDefs)
			return "";
		foreach (RK29_WeaponDef def : m_aWeaponDefs)
		{
			if (!def)
				continue;
			if (def.m_sPrefab == prefab)
				return def.m_sId;
			if (!def.m_aPerFaction)
				continue;
			foreach (RK29_WeaponFactionPrefab entry : def.m_aPerFaction)
			{
				if (entry && entry.m_sFactionKey == factionKey && entry.m_sPrefab == prefab)
					return def.m_sId;
			}
		}
		return "";
	}

	//--------------------------------------------------------------------------------------------
	RK29_WeaponDef FindWeaponDef(string id)
	{
		if (id == "" || !m_aWeaponDefs)
			return null;
		foreach (RK29_WeaponDef def : m_aWeaponDefs)
		{
			if (def && def.m_sId == id)
				return def;
		}
		return null;
	}

	//--------------------------------------------------------------------------------------------
	//! The prefab an option fields: its catalog entry, else its literal.
	ResourceName WeaponPrefabOf(RK29_WeaponOption option, string factionKey = "")
	{
		if (!option)
			return ResourceName.Empty;

		RK29_WeaponDef def = FindWeaponDef(option.m_sWeapon);
		if (!def)
		{
			Print("[RK29] config ERROR - weapon id '" + option.m_sWeapon + "' is not in the weapon catalog", LogLevel.ERROR);
			return ResourceName.Empty;
		}

		// an id both factions field differently resolves per faction; everything else has
		// a single prefab
		if (def.m_aPerFaction)
		{
			foreach (RK29_WeaponFactionPrefab entry : def.m_aPerFaction)
			{
				if (entry && entry.m_sFactionKey == factionKey)
					return entry.m_sPrefab;
			}
		}
		return def.m_sPrefab;
	}

	//--------------------------------------------------------------------------------------------
	//! The group that owns a slot. Last declaration wins, so an inherited slot can be
	//! replaced outright by a kit that declares it again.
	RK29_WeaponSlot FindSlot(array<ref RK29_WeaponSlot> slots, int slot)
	{
		RK29_WeaponSlot found;
		if (!slots)
			return null;
		foreach (RK29_WeaponSlot group : slots)
		{
			if (group && group.m_iSlot == slot)
				found = group;
		}
		return found;
	}

	//--------------------------------------------------------------------------------------------
	//! First option in a slot - its default, and what boot composes.
	RK29_WeaponOption DefaultWeapon(array<ref RK29_WeaponSlot> slots, int slot = 0)
	{
		RK29_WeaponSlot group = FindSlot(slots, slot);
		if (!group || !group.m_aOptions || group.m_aOptions.IsEmpty())
			return null;
		return group.m_aOptions[0];
	}

	//--------------------------------------------------------------------------------------------
	//! Slot-scoped on purpose: a selection's "weapon" always means the primary, so naming a
	//! fixed launcher or the sidearm must not pass as a valid choice.
	RK29_WeaponOption FindWeapon(array<ref RK29_WeaponSlot> slots, ResourceName weapon, string factionKey, int slot = 0)
	{
		RK29_WeaponSlot group = FindSlot(slots, slot);
		if (!group || !group.m_aOptions)
			return null;
		foreach (RK29_WeaponOption w : group.m_aOptions)
		{
			if (w && WeaponPrefabOf(w, factionKey) == weapon)
				return w;
		}
		return null;
	}

}
