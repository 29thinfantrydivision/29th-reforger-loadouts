//------------------------------------------------------------------------------------------------
//! Kit content blocks + item alias / magazine variant catalogs.
//! See docs/29th-kit-config-backend-design.md. Blocks are purely additive; use-site
//! overrides on RK29_BlockRef SET counts and match entry identity literally.
//------------------------------------------------------------------------------------------------

enum RK29_EItemSource
{
	PREFAB,
	ALIAS,
	MAG_PRIMARY,
	MAG_LAUNCHER,
	MAG_SIDEARM
}

//------------------------------------------------------------------------------------------------
class RK29_BlockItemTitle : BaseContainerCustomTitle
{
	override bool _WB_GetCustomTitle(BaseContainer source, out string title)
	{
		int count = 1;
		source.Get("m_iCount", count);

		int src = 0;
		source.Get("m_eSource", src);

		string label;
		if (src == RK29_EItemSource.ALIAS)
		{
			source.Get("m_sAlias", label);
		}
		else if (src == RK29_EItemSource.PREFAB)
		{
			ResourceName prefab;
			source.Get("m_sPrefab", prefab);
			label = "" + prefab;
			int slash = label.LastIndexOf("/");
			if (slash >= 0)
				label = label.Substring(slash + 1, label.Length() - slash - 1);
		}
		else
		{
			label = typename.EnumToString(RK29_EItemSource, src);
			string variant;
			source.Get("m_sVariant", variant);
			if (variant != "")
				label = label + ":" + variant;
		}

		title = count.ToString() + "x " + label;
		return true;
	}
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), RK29_BlockItemTitle()]
class RK29_BlockItemEntry
{
	[Attribute("0", UIWidgets.ComboBox, "Where the item comes from: a prefab, a catalog alias, or the slot weapon's magazine", "", ParamEnumArray.FromEnum(RK29_EItemSource), category: "29th")]
	RK29_EItemSource m_eSource;

	[Attribute(desc: "Item prefab (source PREFAB)", params: "et", category: "29th")]
	ResourceName m_sPrefab;

	[Attribute(desc: "Catalog alias name (source ALIAS), e.g. bandage", category: "29th")]
	string m_sAlias;

	[Attribute(desc: "Magazine variant name (MAG_* sources), e.g. tracer. Empty = the weapon's default", category: "29th")]
	string m_sVariant;

	[Attribute("1", desc: "How many. On a block-ref override this SETS the final count (0 = remove)", category: "29th")]
	int m_iCount;

	[Attribute(desc: "Optional container filename hint. Empty = engine routes", category: "29th")]
	string m_sTargetHint;

	[Attribute("0", desc: "Include only when the kit's primary weapon has an attachment slot accepting this item's AttachmentType (bayonets: only rifles with a lug get one)", category: "29th")]
	bool m_bOnlyIfPrimaryTakesIt;

	[Attribute(desc: "Where this item should go, best first. Each token matches a container kind (uniform, trouser, rig, pack), a named slot (FlashlightSlot, Etool), or part of a container's prefab name (Pouch_ALICE_200rnd_M249). Anything not listed is still used if nothing preferred fits. Empty = fall back to the alias, then the category default", category: "29th")]
	ref array<string> m_aPreferredContainers;
}

//------------------------------------------------------------------------------------------------
class RK29_BlockWeaponTitle : BaseContainerCustomTitle
{
	override bool _WB_GetCustomTitle(BaseContainer source, out string title)
	{
		int slot;
		source.Get("m_iSlotIndex", slot);

		string label;
		source.Get("m_sAlias", label);
		if (label == "")
		{
			ResourceName prefab;
			source.Get("m_sPrefab", prefab);
			label = "" + prefab;
			int slash = label.LastIndexOf("/");
			if (slash >= 0)
				label = label.Substring(slash + 1, label.Length() - slash - 1);
			if (label == "")
				label = "(clear slot)";
		}

		title = "slot " + slot.ToString() + ": " + label;
		return true;
	}
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), RK29_BlockWeaponTitle()]
class RK29_BlockWeaponEntry
{
	[Attribute("0", desc: "Weapon slot: 0 primary, 1 launcher, 2 sidearm; 100 = grenade slot", category: "29th")]
	int m_iSlotIndex;

	[Attribute(desc: "Weapon prefab. Both empty = clear the slot", params: "et", category: "29th")]
	ResourceName m_sPrefab;

	[Attribute(desc: "Catalog alias resolved per faction (e.g. handgun, frag, smoke). Empty = use prefab", category: "29th")]
	string m_sAlias;
}

//------------------------------------------------------------------------------------------------
class RK29_BlockClothingTitle : BaseContainerCustomTitle
{
	override bool _WB_GetCustomTitle(BaseContainer source, out string title)
	{
		string slot;
		source.Get("m_sSlot", slot);

		ResourceName prefab;
		source.Get("m_sPrefab", prefab);
		string label = "" + prefab;
		int slash = label.LastIndexOf("/");
		if (slash >= 0)
			label = label.Substring(slash + 1, label.Length() - slash - 1);
		if (label == "")
			label = "(clear slot)";

		title = slot + ": " + label;
		return true;
	}
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), RK29_BlockClothingTitle()]
class RK29_BlockClothingEntry
{
	[Attribute(desc: "Loadout slot name as authored on the character prefab: Jacket, ArmoredVest, Vest, Pants, Boots, Hat, Back", category: "29th")]
	string m_sSlot;

	[Attribute(desc: "Garment prefab. Empty = clear the slot", params: "et", category: "29th")]
	ResourceName m_sPrefab;
}

//------------------------------------------------------------------------------------------------
//! One building block - Configs/KitSystem/Blocks/**.conf. Variants inherit conf-from-conf.
[BaseContainerProps(configRoot: true)]
class RK29_KitBlock
{
	[Attribute(desc: "Dress by slot (applied over the kit prefab's dress, later-wins)", category: "29th")]
	ref array<ref RK29_BlockClothingEntry> m_aClothing;

	[Attribute(desc: "Equipment storage slots (WristwatchSlot, BinocularSlot, ...), later-wins", category: "29th")]
	ref array<ref RK29_BlockClothingEntry> m_aEquipment;

	[Attribute(desc: "Weapon slots", category: "29th")]
	ref array<ref RK29_BlockWeaponEntry> m_aWeapons;

	[Attribute(desc: "Items", category: "29th")]
	ref array<ref RK29_BlockItemEntry> m_aItems;
}

//------------------------------------------------------------------------------------------------
//! How kits reference blocks: block + optional use-site overrides, scoped to this
//! block's own contribution. Overrides match entry identity literally (same source +
//! alias/variant/prefab), never by resolution.
[BaseContainerProps(), SCR_BaseContainerCustomTitleResourceName("m_sBlock", true)]
class RK29_BlockRef
{
	[Attribute(desc: "The block", params: "conf class=RK29_KitBlock", category: "29th")]
	ResourceName m_sBlock;

	[Attribute(desc: "SET the final count of a matching entry from this block (0 = remove)", category: "29th")]
	ref array<ref RK29_BlockItemEntry> m_aItemOverrides;
}

//------------------------------------------------------------------------------------------------
//! One kit's complete content story - Configs/KitSystem/Kits/**.conf. Shared bases by
//! reference, the kit's own weapons/items inline. Compositions may inherit each other
//! (conf-from-conf); blocks stay leaf-only.
[BaseContainerProps(configRoot: true)]
class RK29_KitComposition
{
	[Attribute(desc: "Shared blocks, applied in order, with optional use-site overrides", category: "29th")]
	ref array<ref RK29_BlockRef> m_aBlocks;

	[Attribute(desc: "This kit's weapon slots (applied after blocks, later-wins)", category: "29th")]
	ref array<ref RK29_BlockWeaponEntry> m_aWeapons;

	[Attribute(desc: "This kit's own items (added after block items)", category: "29th")]
	ref array<ref RK29_BlockItemEntry> m_aItems;

	[Attribute(desc: "This kit's dress by slot (applied after block clothing, later-wins; unlisted slots fall back to the kit prefab)", category: "29th")]
	ref array<ref RK29_BlockClothingEntry> m_aClothing;

	[Attribute(desc: "This kit's equipment storage slots (WristwatchSlot, BinocularSlot, ...), later-wins", category: "29th")]
	ref array<ref RK29_BlockClothingEntry> m_aEquipment;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), BaseContainerCustomTitleField("m_sFactionKey")]
class RK29_ItemAliasEntry
{
	[Attribute(desc: "Faction key, e.g. US", category: "29th")]
	string m_sFactionKey;

	[Attribute(desc: "Prefab for that faction", params: "et", category: "29th")]
	ResourceName m_sPrefab;

	[Attribute(desc: "Default placement for this faction's version, best first - e.g. the US flashlight belongs in the ALICE FlashlightSlot. Overridden by an item entry's own list", category: "29th")]
	ref array<string> m_aPreferredContainers;
}

[BaseContainerProps(), BaseContainerCustomTitleField("m_sAlias")]
class RK29_ItemAlias
{
	[Attribute(desc: "Name blocks refer to, e.g. bandage", category: "29th")]
	string m_sAlias;

	[Attribute(desc: "Per-faction prefabs", category: "29th")]
	ref array<ref RK29_ItemAliasEntry> m_aPerFaction;
}

//! Alias catalog - Configs/KitSystem/Catalogs/RK29_Aliases.conf
[BaseContainerProps(configRoot: true)]
class RK29_ItemAliasCatalog
{
	[Attribute(desc: "Aliases", category: "29th")]
	ref array<ref RK29_ItemAlias> m_aAliases;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), BaseContainerCustomTitleField("m_sName")]
class RK29_MagVariant
{
	[Attribute(desc: "Variant name used by MAG_* entries, e.g. tracer, ap, sniper", category: "29th")]
	string m_sName;

	[Attribute(desc: "Magazine/ammo prefab", params: "et", category: "29th")]
	ResourceName m_sPrefab;
}

//! Variants per magazine well - a MAG_* entry with a variant resolves through the
//! slot weapon's wells.
[BaseContainerProps(), BaseContainerCustomTitleField("m_sMagazineWell")]
class RK29_MagazineSet
{
	[Attribute(desc: "MagazineWell class name, e.g. MagazineWellM16Stanag", category: "29th")]
	string m_sMagazineWell;

	[Attribute(desc: "Named variants", category: "29th")]
	ref array<ref RK29_MagVariant> m_aVariants;
}

//! Magazine set catalog - Configs/KitSystem/Catalogs/RK29_Magazines.conf
[BaseContainerProps(configRoot: true)]
class RK29_MagazineSetCatalog
{
	[Attribute(desc: "Magazine variant sets, keyed by magazine well", category: "29th")]
	ref array<ref RK29_MagazineSet> m_aMagazineSets;
}
