//------------------------------------------------------------------------------------------------
//! Customization config (Configs/KitSystem/RK29_KitSetup.conf + Sides/ + Helpers/).
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

	[Attribute("0", desc: "Legacy kit: deploy-menu selectable, hidden in the picker, HUD-counted under its display name", category: "29th")]
	bool m_bLegacyHidden;
}

//------------------------------------------------------------------------------------------------
//! One faction's classes - Configs/KitSystem/Sides/*.conf
[BaseContainerProps(configRoot: true)]
class RK29_SideSetup
{
	[Attribute(desc: "Faction key this file covers, e.g. US", category: "29th")]
	string m_sFactionKey;

	[Attribute(desc: "This side's classes", category: "29th")]
	ref array<ref RK29_ClassSetup> m_aClasses;
}

//! Optic category definitions - Configs/KitSystem/Helpers/*.conf
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

	[Attribute(desc: "Per-side class configs (Sides folder)", params: "conf", category: "29th")]
	ref array<ResourceName> m_aSideConfigs;

	[Attribute(desc: "Optic library configs (Helpers folder)", params: "conf", category: "29th")]
	ref array<ResourceName> m_aOpticConfigs;

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
	//! Option for this optic prefab within the class's allowed categories, null if not allowed.
	RK29_OpticOption FindOpticOption(RK29_ClassSetup cls, ResourceName optic)
	{
		if (optic == ResourceName.Empty || !cls || !cls.m_aOpticCategories)
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
