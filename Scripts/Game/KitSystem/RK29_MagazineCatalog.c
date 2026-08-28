//------------------------------------------------------------------------------------------------
//! Magazine variant catalog - Configs/KitSystem/Catalogs/RK29_Magazines.conf. Named ammo variants
//! per magazine well; an ammo entry's variant resolves through the slot weapon's wells, so one
//! declaration covers every weapon sharing that well.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), BaseContainerCustomTitleField("m_sName")]
class RK29_MagVariant
{
	[Attribute(desc: "Variant name referenced by m_sVariant on a weapon ammo entry, e.g. tracer, ap", category: "29th")]
	string m_sName;

	[Attribute(desc: "Magazine/ammo prefab", params: "et", category: "29th")]
	ResourceName m_sPrefab;
}

[BaseContainerProps(), BaseContainerCustomTitleField("m_sMagazineWell")]
class RK29_MagazineSet
{
	[Attribute(desc: "MagazineWell class name, e.g. MagazineWellStanag556, MagazineWellAK545", category: "29th")]
	string m_sMagazineWell;

	[Attribute(desc: "Named variants", category: "29th")]
	ref array<ref RK29_MagVariant> m_aVariants;
}

[BaseContainerProps(configRoot: true)]
class RK29_MagazineSetCatalog
{
	[Attribute(desc: "Magazine variant sets, keyed by magazine well", category: "29th")]
	ref array<ref RK29_MagazineSet> m_aMagazineSets;
}
