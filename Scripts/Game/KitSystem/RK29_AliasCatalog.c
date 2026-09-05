//------------------------------------------------------------------------------------------------
//! Item alias catalog - Configs/KitSystem/Catalogs/RK29_Aliases.conf. One name per item, resolved
//! per faction, so a shared block or entry dresses both sides from a single row.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), SCR_BaseContainerCustomTitleFields({"m_sFactionKey", "m_sPrefab"}, "%1 - %2")]
class RK29_ItemAliasEntry
{
	[Attribute(desc: "Faction key, e.g. US. EMPTY = every faction, used only where that faction states nothing of its own - so a third faction inherits the generic compass and needs an entry only for what actually differs. An exact match always wins, wherever it sits in the list", category: "29th")]
	string m_sFactionKey;

	[Attribute(desc: "Prefab for that faction, or for every faction when the key above is empty", params: "et", category: "29th")]
	ResourceName m_sPrefab;

	[Attribute(desc: "Default placement for this faction's version, best first - e.g. the US flashlight belongs in the ALICE FlashlightSlot. Overridden by an item entry's own list", category: "29th")]
	ref array<string> m_aPreferredContainers;
}

[BaseContainerProps(), BaseContainerCustomTitleField("m_sAlias")]
class RK29_ItemAlias
{
	[Attribute(desc: "Name blocks refer to, e.g. bandage", category: "29th")]
	string m_sAlias;

	[Attribute(desc: "Per-faction prefabs. An alias with nothing for this kit's faction does not resolve, which the caller logs as a config error", category: "29th")]
	ref array<ref RK29_ItemAliasEntry> m_aPerFaction;

	[Attribute("50", desc: "How important everything issued through this alias is when the kit does not fit: lower keeps its place, higher gives way to it. 50 is the default. Utilities sit at 10 and medical at 20, smokes and flares at 60, frags at 70, demolitions, mines and launcher rounds at 80. Placement itself is still solved by fit; this only decides what is taken out when something more important has no room. A choice group's own number overrides this, and an entry's overrides the group's", category: "29th")]
	int m_iKeepRank;
}

[BaseContainerProps(configRoot: true)]
class RK29_ItemAliasCatalog
{
	[Attribute(desc: "Aliases", category: "29th")]
	ref array<ref RK29_ItemAlias> m_aAliases;
}
