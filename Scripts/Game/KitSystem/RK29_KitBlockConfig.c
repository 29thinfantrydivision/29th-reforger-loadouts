//------------------------------------------------------------------------------------------------
//! Kit content blocks + item alias / magazine variant catalogs.
//! See docs/29th-kit-config-backend-design.md. Blocks are purely additive; use-site
//! overrides on RK29_BlockRef SET counts and match entry identity literally.
//------------------------------------------------------------------------------------------------

enum RK29_EItemSource
{
	PREFAB,
	ALIAS
}

//------------------------------------------------------------------------------------------------
//! What a role is QUALIFIED at, as opposed to what it carries. Each trait is one vanilla
//! EEditableEntityLabel that user actions and consumables read for their qualified-personnel
//! speed bonus, so the effect is whatever the base game does with that label - we grant it,
//! we do not set the multipliers.
enum RK29_ETrait
{
	NONE,			//!< nothing - a fresh row reads as unset rather than as a medic
	MEDIC,			//!< field dressing 1.5x, tourniquet 1.2x, casualty inspect/load/heal at a station 2x
	SAPPER,			//!< building 2x, deploying multi-part fortifications 2x, vehicle repair 2x
	VEHICLE_CREW,	//!< vehicle repair, refuel, rearm and supply unloading 2x
	HELI_CREW,		//!< the same stations as VEHICLE_CREW, minus repair
	LOGISTICS		//!< loading supplies into a vehicle 2x
}

//------------------------------------------------------------------------------------------------
//! Trait -> the vanilla label carrying it. The indirection is the point: the config names a
//! job, not an engine enum, so the 150-entry label list never reaches the kit author.
class RK29_Traits
{
	//--------------------------------------------------------------------------------------------
	static EEditableEntityLabel LabelOf(RK29_ETrait trait)
	{
		switch (trait)
		{
			case RK29_ETrait.MEDIC:			return EEditableEntityLabel.ROLE_MEDIC;
			case RK29_ETrait.SAPPER:		return EEditableEntityLabel.ROLE_SAPPER;
			case RK29_ETrait.VEHICLE_CREW:	return EEditableEntityLabel.TRAIT_VEHICLE_CREW;
			case RK29_ETrait.HELI_CREW:		return EEditableEntityLabel.TRAIT_HELI_CREW;
			case RK29_ETrait.LOGISTICS:		return EEditableEntityLabel.TRAIT_LOGISTICS;
		}
		return EEditableEntityLabel.NONE;
	}

	//--------------------------------------------------------------------------------------------
	static string NameOf(RK29_ETrait trait)
	{
		return typename.EnumToString(RK29_ETrait, trait);
	}
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

	[Attribute(desc: "Garment prefab. Empty = clear the slot (ignored when an alias is set)", params: "et", category: "29th")]
	ResourceName m_sPrefab;

	[Attribute(desc: "Item alias to resolve for this kit's faction instead of a literal prefab - lets one shared entry dress both sides (binoculars, watch)", category: "29th")]
	string m_sAlias;
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

	[Attribute(desc: "Every weapon slot this kit fills. Slot-keyed later-wins: declaring a slot again replaces that group, leaving other slots inherited", category: "29th")]
	ref array<ref RK29_WeaponSlot> m_aWeaponSlots;

	[Attribute(desc: "This kit's own items (added after block items)", category: "29th")]
	ref array<ref RK29_BlockItemEntry> m_aItems;

	[Attribute(desc: "This kit's dress by slot (applied after block clothing, later-wins; unlisted slots fall back to the kit prefab)", category: "29th")]
	ref array<ref RK29_BlockClothingEntry> m_aClothing;

	[Attribute(desc: "This kit's equipment storage slots (WristwatchSlot, BinocularSlot, ...), later-wins", category: "29th")]
	ref array<ref RK29_BlockClothingEntry> m_aEquipment;

	[Attribute(uiwidget: UIWidgets.ComboBox, desc: "What this role is qualified at - a medic dresses wounds faster, a sapper builds faster. Declared on the shared role file so both factions' kits inherit it; a faction kit restating the list REPLACES it, and '+' appends. Applied to the body at kit apply, so re-kitting to another class drops them", enums: ParamEnumArray.FromEnum(RK29_ETrait), category: "29th")]
	ref array<RK29_ETrait> m_aTraits;
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

	[Attribute(desc: "Resolve through another alias when this one has nothing for the faction - lets a role name (backpack_ce) track a general one (backpack_medium) instead of repeating its prefab", category: "29th")]
	string m_sSameAs;

	[Attribute(desc: "Per-faction prefabs. Anything stated here wins over m_sSameAs", category: "29th")]
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
	[Attribute(desc: "Variant name referenced by m_sVariant on a weapon ammo entry, e.g. tracer, ap", category: "29th")]
	string m_sName;

	[Attribute(desc: "Magazine/ammo prefab", params: "et", category: "29th")]
	ResourceName m_sPrefab;
}

//! Variants per magazine well - an ammo entry naming a variant resolves through the
//! slot weapon's wells, so one declaration covers every weapon sharing that well.
[BaseContainerProps(), BaseContainerCustomTitleField("m_sMagazineWell")]
class RK29_MagazineSet
{
	[Attribute(desc: "MagazineWell class name, e.g. MagazineWellStanag556, MagazineWellAK545", category: "29th")]
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
