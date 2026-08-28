//------------------------------------------------------------------------------------------------
//! The choice model: groups and entries live flat in catalogs, referenced by id from compositions
//! and weapon definitions. Per-class variance never edits a shared definition - it goes through
//! RK29_Override. A group declares its kind by its class; RK29_KitResolve.ResolveGroup reads that
//! once and downstream reads the marker it leaves, never the entries that survived filtering.
//! Addressing is "group" / "group/entry", the same spelling in overrides, wire and validator output.
//------------------------------------------------------------------------------------------------

//! How a group constrains what may be taken from it.
enum RK29_EChoiceKind
{
	//! Pick exactly one entry (an attachment point, the weapon of a slot).
	EXCLUSIVE = 0,
	//! Per-entry counts within each entry's min/max (ammo).
	COUNTED = 1,
	//! Per-entry counts, and the cost-weighted sum must fit m_iBudget (mines and demo).
	BUDGETED = 2,
}

//------------------------------------------------------------------------------------------------
class RK29_ChoiceEntryTitle : BaseContainerCustomTitle
{
	//------------------------------------------------------------------------------------------------
	override bool _WB_GetCustomTitle(BaseContainer source, out string title)
	{
		title = PayloadTitle(source);

		// Get leaves the local alone when the conf never mentions the flag - default must be true
		bool enabled = true;
		if (source.Get("m_bEnabled", enabled) && !enabled)
			title = "(parked) " + title;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected string PayloadTitle(BaseContainer source)
	{
		string id;
		source.Get("m_sId", id);
		if (id != "")
			return id;

		// most specific first
		string s;
		if (source.Get("m_sWeapon", s) && s != "")
			return s;
		if (source.Get("m_sAttachment", s) && s != "")
			return s;
		if (source.Get("m_sAlias", s) && s != "")
			return s;
		if (source.Get("m_sVariant", s) && s != "")
			return s;

		ResourceName rn;
		if (source.Get("m_sPrefab", rn) && rn != ResourceName.Empty)
			return FilePath.StripPath(rn);

		return "(empty)";
	}
}

//------------------------------------------------------------------------------------------------
//! What every choice shares. Quantities belong to countable payloads only, so an attachment entry
//! never shows dead count fields.
[BaseContainerProps(), RK29_ChoiceEntryTitle()]
class RK29_ChoiceEntryBase
{
	[Attribute(desc: "Id overrides and the wire address this entry by (\"group/entry\"). Empty = derived from the payload - an item stating BOTH a prefab and an alias is addressed by the ALIAS while the prefab still supplies what is issued - fine until an override needs to name it", category: "29th")]
	string m_sId;

	[Attribute("0", desc: "Where this entry sits in its group's list. Higher sinks; equal numbers keep authored order (own entries, then includes). Bands: ammo 100 ball / 200 AP / 300 tracer / 400 mixed; optics = max zoom x10; packs = capacity/100", category: "29th")]
	int m_iOrder;

	// Second job: RK29_KitResolve.SynthesizeLoadedGroup copies this flag onto the "<id>_loaded"
	// selector, so on a weapon's COUNTED ammo group it also names the chambered round - invisible
	// from the group it is authored on, which resolves its own contents by the counts.
	[Attribute("0", desc: "The canonical pick, in three cases. EXCLUSIVE: what the kit resolves to when the player changes nothing (first flagged wins, else the first entry). A weapon's COUNTED ammo group: the synthesized \"<id>_loaded\" selector inherits this flag, so it also names the round the gun starts chambered with. Any other COUNTED/BUDGETED group: nothing reads it - counts decide", category: "29th")]
	bool m_bDefault;

	[Attribute(desc: "Picker label override. Empty = the resolved item's own display name", category: "29th")]
	string m_sDisplayName;

	[Attribute(desc: "Offer this entry only to the factions named (e.g. US) - how a shared role authors several factions' choices in one group. EMPTY = every faction. A LIST rather than one key so an entry two factions share is written once instead of copied per faction and left to drift; a default flag per faction is fine, filtering runs first", category: "29th")]
	ref array<string> m_aFactionKeys;

	// Dropped exactly where a wrong-faction entry is, before anything downstream can see it.
	[Attribute("1", desc: "Offer this entry. Clear it to PARK the entry - it stays authored, with its numbers and its id, but resolves as if absent. For shelving content (a future weapon, an ammo type) without deleting the authoring", category: "29th")]
	bool m_bEnabled;
}

//------------------------------------------------------------------------------------------------
//! An item taken in quantity. Payload precedence: literal prefab beats magazine variant beats
//! item alias.
[BaseContainerProps(), RK29_ChoiceEntryTitle()]
class RK29_EntryItem : RK29_ChoiceEntryBase
{
	[Attribute(desc: "Item alias (faction-resolved through the alias catalog)", category: "29th")]
	string m_sAlias;

	[Attribute(desc: "Magazine variant, resolved through the owning weapon's magazine well", category: "29th")]
	string m_sVariant;

	[Attribute(desc: "Literal prefab. Wins over everything", params: "et", category: "29th")]
	ResourceName m_sPrefab;

	// A kit-level EXCLUSIVE item group is picked, not counted (ApplyItemGroup dispatches on kind) -
	// but only a clothing or garment-attachment group carries an allow-empty flag, so an exclusive
	// plain item group always issues its answer and can never offer None.
	[Attribute("0", desc: "COUNTED/BUDGETED only (inert in EXCLUSIVE): fewest the player may take", category: "29th")]
	int m_iMin;

	[Attribute("0", desc: "COUNTED/BUDGETED only (inert in EXCLUSIVE): how many the default kit carries. Also what a BUDGETED group falls back to wholesale when picks overspend its budget", category: "29th")]
	int m_iDefault;

	[Attribute("-1", desc: "COUNTED/BUDGETED only (inert in EXCLUSIVE): most the player may take. -1 (the default) = no cap authored, which implies a ceiling of twice the default and at least 4, so an entry you never capped still cannot be asked for in absurd numbers. ZERO means zero: the entry is not offered and its row never appears, which is how a catalog can hold something nobody gets by default and let individual kits opt in by raising the cap. A hard ceiling caps every entry regardless, and the group budget, if any, still binds", category: "29th")]
	int m_iMax;

	[Attribute("1", desc: "BUDGETED only: what one costs against the group's m_iBudget. Ignored by EXCLUSIVE and COUNTED groups, which have no budget to spend against", category: "29th")]
	int m_iCost;

	[Attribute("-1", desc: "How important THIS ROW is when the kit does not fit: lower keeps its place, higher gives way to it. -1 (the default) = not stated here; the group's number applies, else the alias's, else the owning weapon's, else 50. Most specific wins. State it only where one row differs from its group", category: "29th")]
	int m_iKeepRank;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), RK29_ChoiceEntryTitle()]
class RK29_EntryWeapon : RK29_ChoiceEntryBase
{
	[Attribute(desc: "Weapon id from the weapon catalog. The weapon's own groups (ammo, attachment points) come along implicitly", category: "29th")]
	string m_sWeapon;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), RK29_ChoiceEntryTitle()]
class RK29_EntryAttachment : RK29_ChoiceEntryBase
{
	[Attribute(desc: "Attachment id from the attachment catalog", category: "29th")]
	string m_sAttachment;
}

//------------------------------------------------------------------------------------------------
//! Where the round a group seats goes. A property of the group, not of where the group was
//! authored - deriving it from inline-vs-referenced made one authored count mean two things and
//! forced ten weapon definitions to copy their ammo blocks.
enum RK29_ELoadedSeat
{
	//! Counts are plain spares: no selector offered, nothing deducted.
	NONE = 0,
	//! The weapon's own chamber. The default, and why the attribute's defvalue is 1.
	OWN_MUZZLE = 1,
	//! The underbarrel launcher's chamber; the host rifle seats its own magazine from a second group.
	UNDERBARREL = 2,
}

//------------------------------------------------------------------------------------------------
class RK29_ChoiceGroupTitle : BaseContainerCustomTitle
{
	//------------------------------------------------------------------------------------------------
	override bool _WB_GetCustomTitle(BaseContainer source, out string title)
	{
		source.Get("m_sId", title);
		if (title == "")
			title = "(unnamed group)";

		int kind = 0;
		source.Get("m_eKind", kind);
		if (kind == RK29_EChoiceKind.COUNTED)
			title += " [counted]";
		else if (kind == RK29_EChoiceKind.BUDGETED)
			title += " [budgeted]";

		// Get leaves the local alone when the conf never mentions the flag - default must be true
		bool enabled = true;
		if (source.Get("m_bEnabled", enabled) && !enabled)
			title = "(parked) " + title;
		return true;
	}
}

//------------------------------------------------------------------------------------------------
//! What every group shares. Abstract - author a concrete subclass; every list that holds groups is
//! typed to one, so the bare class is unreachable from the Workbench tree. A conf naming it by hand
//! would deserialize with none of the subclass fields and read as a group that lost its kind and
//! budget rather than as an error.
[BaseContainerProps(insertable: false), RK29_ChoiceGroupTitle()]
class RK29_ChoiceGroup
{
	[Attribute(desc: "Id everything references this group by", category: "29th")]
	string m_sId;

	[Attribute(desc: "Section header in the picker. Empty = the id", category: "29th")]
	string m_sDisplayName;

	// A parked group is not a missing one: every reference to it resolves as absent, silently.
	[Attribute("1", desc: "Offer this group. Clear it to PARK the group and everything in it - the authoring stays, but every reference to it resolves as if absent. For shelving content (a future weapon, an ammo type) without deleting the authoring", category: "29th")]
	bool m_bEnabled;

	[Attribute(desc: "The choices. Entry type states what a choice resolves to, and must match the group's class - item entries in an RK29_ItemGroup, weapon entries in an RK29_WeaponGroup, attachment entries in an RK29_AttachmentGroup", category: "29th")]
	ref array<ref RK29_ChoiceEntryBase> m_aEntries;

	[Attribute(desc: "Catalog group ids whose entries are MERGED into this group at resolve time, after this group's own - a kit offering both assault rifles and carbines in one slot composes the two groups instead of copying their entries. Duplicate entry ids keep the first; the first default-flagged entry (include order matters) is the default. ONE LEVEL ONLY: an include that itself includes is refused with a config ERROR, and a parked include is skipped silently", category: "29th")]
	ref array<string> m_aIncludeGroups;

	[Attribute("0", desc: "Where this group sits in the detail column. Higher SINKS; groups sharing a number keep the order they were offered in, which is the authored order for kit groups and the weapon order after them. Exists because the offer is built in resolution order - weapon groups arrive last - and that is not the order a player reads in. Utilities sits at the bottom this way", category: "29th")]
	int m_iOrder;

	//------------------------------------------------------------------------------------------------
	//! The catalog id this element points at, on a *Ref subclass. "" means the group is defined
	//! right here, and every reader tells the two apart by this and nothing else.
	string RefId()
	{
		return "";
	}
}

//------------------------------------------------------------------------------------------------
//! An item the kit wears rather than carries. Subclasses RK29_EntryItem on purpose, so every
//! RK29_EntryItem.Cast in the resolver and the menu keeps matching and no case is added for it.
//! Only the destination differs, at exactly one place: RK29_KitCompose.EmitAmmo. A slot holds one
//! thing, so author min/default/max as 1/1/1.
[BaseContainerProps(), RK29_ChoiceEntryTitle()]
class RK29_EntryEquipment : RK29_EntryItem
{
	[Attribute(desc: "SCR_EquipmentStorageSlot name off the character prefab - BinocularSlot, WristwatchSlot. This is what makes the entry worn instead of carried; empty would make it an ordinary item, so do not leave it empty", category: "29th")]
	string m_sSlot;
}

//------------------------------------------------------------------------------------------------
//! Things taken in quantity. Entries are RK29_EntryItem; the two fields below are what makes a
//! group countable and are inert on the other two group kinds.
[BaseContainerProps(), RK29_ChoiceGroupTitle()]
class RK29_ItemGroup : RK29_ChoiceGroup
{
	[Attribute("0", uiwidget: UIWidgets.ComboBox, desc: "Selection rule: pick one / per-entry counts / cost-weighted budget", enums: ParamEnumArray.FromEnum(RK29_EChoiceKind), category: "29th")]
	RK29_EChoiceKind m_eKind;
	[Attribute("0", desc: "BUDGETED: the point pool. Must cover the entries' default spend", category: "29th")]
	int m_iBudget;

	[Attribute("-1", desc: "How important everything in this group is when the kit does not fit: lower keeps its place, higher gives way to it. -1 (the default) = not stated; each row then takes its alias's number, else its owning weapon's, else 50. An entry's own number overrides this. A pool usually answers as one - the 40mm groups state 80 here, beside the launcher rockets and demolitions", category: "29th")]
	int m_iKeepRank;
}

//------------------------------------------------------------------------------------------------
//! A garment slot the class dresses. The group states the slot once; entries are ordinary
//! RK29_EntryItem. Unlike RK29_EntryEquipment, which keeps its slot per entry because utilities
//! holds binoculars and a wristwatch in different slots. Author these EXCLUSIVE.
[BaseContainerProps(), RK29_ChoiceGroupTitle()]
class RK29_ClothingGroup : RK29_ItemGroup
{
	[Attribute(desc: "Loadout slot every entry of this group dresses - Hat, Jacket, ArmoredVest, Vest, Back, Pants, Boots. Stated once here rather than on each entry, because a group whose entries disagreed about the slot would be answering two questions at once", category: "29th")]
	string m_sSlot;

	//! Answering None is a deliberate pick, distinct from not having answered: ApplyItemGroup
	//! empties the slot only on the explicit bare pick, and dresses the default for an untouched
	//! group.
	[Attribute("0", desc: "Taking nothing is a legal pick on this slot - rendered as a 'None' row. Off means the slot must be filled", category: "29th")]
	bool m_bAllowEmpty;
}

//------------------------------------------------------------------------------------------------
//! A slot on a garment the class fills: RHS night vision seats in the NVG slot of the helmet's
//! cloth-node storage, which no character slot can hold. Shaped like RK29_ClothingGroup: the group
//! states its slot once, entries are ordinary RK29_EntryItem, author it EXCLUSIVE. Only the
//! destination differs from any other item row - RK29_KitCompose.EmitWornRow files the pick under
//! the garment and RK29_KitApply.DressGarmentAttachments seats it once the garment is worn. A host
//! without the slot enabled (the vanilla PASGT under RHS) greys every row: EnforceGarmentSlots.
[BaseContainerProps(), RK29_ChoiceGroupTitle()]
class RK29_GarmentAttachmentGroup : RK29_ItemGroup
{
	[Attribute(desc: "Loadout slot of the garment every entry rides on - Hat for a helmet. Not the slot on the garment; that is m_sSlot", category: "29th")]
	string m_sGarmentSlot;

	[Attribute(desc: "Slot ON that garment, by source name - NVG, BackVelcro, Rail_Right on an RHS helmet. Both names are required; the lint refuses a group missing either", category: "29th")]
	string m_sSlot;

	//! Same contract as the clothing flag: None is a deliberate pick, distinct from not answering.
	[Attribute("0", desc: "Taking nothing is a legal pick on this slot - rendered as a 'None' row. Off means the slot must be filled", category: "29th")]
	bool m_bAllowEmpty;
}

//------------------------------------------------------------------------------------------------
//! An item group a weapon is fed from. States nothing of its own - the muzzle is asked of the metal
//! by RK29_KitCompose.DeriveLoadedSeat - but the type is load-bearing: RK29_WeaponDef.m_AmmoGroup
//! is declared as this, so a weapon's ammo cannot be authored as a weapon or attachment group.
[BaseContainerProps(), RK29_ChoiceGroupTitle()]
class RK29_AmmoGroup : RK29_ItemGroup
{
}

//------------------------------------------------------------------------------------------------
//! A weapon slot the class fills. Entries are RK29_EntryWeapon and answering is always exactly one,
//! so nothing is stated beyond the base. No slot field on purpose: the winner's prefab declares its
//! WeaponSlotType and the captured body says which slot carries that type.
[BaseContainerProps(), RK29_ChoiceGroupTitle()]
class RK29_WeaponGroup : RK29_ChoiceGroup
{
}

//------------------------------------------------------------------------------------------------
//! A physical attachment point on a weapon, as an exclusive group. Whatever cannot mount on the
//! owning weapon drops out at validation, so a broad group can be offered to a gun taking half.
//!
//! Author weapon-owned, through a weapon definition's m_aGroups. Only a weapon-owned group is
//! capability-screened (PruneUnmountable reads the owning weapon's prefab and nothing else), and
//! an ownerless kit-level group is read downstream as the primary's - seated on weapon slot 0.
//!
//! There is no slot type here: the seat is derived from the mount types of the entries by
//! RK29_KitResolve.DeriveSeatTypes into RK29_ResolvedGroup.m_aSeatTypes. The fields below are
//! author doctrine only.
[BaseContainerProps(), RK29_ChoiceGroupTitle()]
class RK29_AttachmentGroup : RK29_ChoiceGroup
{
	// No prefab can answer this: the SMAW's MBS seats in RHS's AttachmentMBS, which descends from
	// no optics class, and is a sight all the same.
	[Attribute("0", desc: "This point holds SIGHTS: its picks badge as magnified, carry zoom text, sort first among the gun's attachment sections, feed the mannequin's preview sight, and count toward the squad magnified tally. DOCTRINE - the physical seat is derived from the entries and is never authored", category: "29th")]
	bool m_bIsOpticsPoint;

	[Attribute("0", desc: "Taking nothing is a legal pick on this point - a bare seat, rendered as a 'None' row. Off means the point must be filled", category: "29th")]
	bool m_bAllowEmpty;

	[Attribute("0", desc: "Answering None STOWS the item instead of leaving it behind - a bayonet not fixed to the rifle is on the belt. Covers the deliberate None only: an item another pick makes unfittable (the suppressor blocking the bayonet) is dropped from the kit, not stowed. Do not set this on a point that is genuinely optional. Needs no capability check of its own: a weapon that cannot seat the thing loses this whole group to pruning, so there is nothing left to stow", category: "29th")]
	bool m_bCarryWhenUnfitted;

	[Attribute("0", desc: "Optics offered through this group neither badge as magnified in the picker nor count toward the HUD magnified tally, even when the glass physically magnifies. Per-group, so the same optic can count in one class's offer and not in another's", category: "29th")]
	bool m_bMagnifiedExempt;
}

//------------------------------------------------------------------------------------------------
//! Title for the by-id references: shows "<id> (catalog)".
class RK29_GroupRefTitle : BaseContainerCustomTitle
{
	//------------------------------------------------------------------------------------------------
	override bool _WB_GetCustomTitle(BaseContainer source, out string title)
	{
		string r;
		source.Get("m_sRef", r);
		title = r + " (catalog)";
		return true;
	}
}

//------------------------------------------------------------------------------------------------
//! A shared catalog group named by id, sitting in the same section as the kit's inline groups.
//! One class per kind because each section's list is typed to its kind. Only m_sRef is read - the
//! group fields inherited below are dead on a reference, and the definition it names answers for
//! all of them, its parked flag included.
[BaseContainerProps(), RK29_GroupRefTitle()]
class RK29_WeaponGroupRef : RK29_WeaponGroup
{
	[Attribute(desc: "Id of a weapon group in a choice catalog", category: "29th")]
	string m_sRef;

	//------------------------------------------------------------------------------------------------
	override string RefId()
	{
		return m_sRef;
	}
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), RK29_GroupRefTitle()]
class RK29_ClothingGroupRef : RK29_ClothingGroup
{
	[Attribute(desc: "Id of a clothing group in a choice catalog", category: "29th")]
	string m_sRef;

	//------------------------------------------------------------------------------------------------
	override string RefId()
	{
		return m_sRef;
	}
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), RK29_GroupRefTitle()]
class RK29_ItemGroupRef : RK29_ItemGroup
{
	[Attribute(desc: "Id of an item group in a choice catalog", category: "29th")]
	string m_sRef;

	//------------------------------------------------------------------------------------------------
	override string RefId()
	{
		return m_sRef;
	}
}

//------------------------------------------------------------------------------------------------
//! Groups sorted by kind, so the Workbench "+" menu on each list offers only what belongs there.
//! Sections are an authoring convenience only - Collect flattens them and nothing downstream can
//! tell which list a group came from. A section holds references and inline definitions alike.
[BaseContainerProps()]
class RK29_ChoiceGroupSet
{
	[Attribute(desc: "Weapon groups - what fills a weapon slot", category: "29th")]
	ref array<ref RK29_WeaponGroup> m_aWeapons;

	[Attribute(desc: "Clothing groups - one per worn slot; on a kit, a group written later on a slot replaces one written earlier, so its own hat beats a shared hat listed above it", category: "29th")]
	ref array<ref RK29_ClothingGroup> m_aClothing;

	[Attribute(desc: "Item groups - carried items, counted or pick-one", category: "29th")]
	ref array<ref RK29_ItemGroup> m_aItems;

	//------------------------------------------------------------------------------------------------
	//! Every group in one list, section order weapons, clothing, items (the catalog appends ammo, attachments).
	//! Element-wise: array<ref Sub> is not assignable to array<Base>.
	void Collect(notnull array<RK29_ChoiceGroup> outGroups)
	{
		if (m_aWeapons)
		{
			foreach (RK29_WeaponGroup g : m_aWeapons)
			{
				if (g)
					outGroups.Insert(g);
			}
		}
		if (m_aClothing)
		{
			foreach (RK29_ClothingGroup g : m_aClothing)
			{
				if (g)
					outGroups.Insert(g);
			}
		}
		if (m_aItems)
		{
			foreach (RK29_ItemGroup g : m_aItems)
			{
				if (g)
					outGroups.Insert(g);
			}
		}
	}
}

//------------------------------------------------------------------------------------------------
//! Choice group catalog - Configs/KitSystem/Catalogs/*.conf
[BaseContainerProps(configRoot: true)]
class RK29_ChoiceGroupCatalog : RK29_ChoiceGroupSet
{
	// ammo and attachment groups are weapon-owned by construction (a weapon def names them in
	// m_aGroups or inline), so only a catalog authors them - a kit has no way to seat or feed one
	[Attribute(desc: "Ammo groups - what feeds a weapon; a weapon def names one by id or authors it inline", category: "29th")]
	ref array<ref RK29_AmmoGroup> m_aAmmo;

	[Attribute(desc: "Attachment groups - what mounts on a weapon; a weapon def names them by id", category: "29th")]
	ref array<ref RK29_AttachmentGroup> m_aAttachments;

	//------------------------------------------------------------------------------------------------
	override void Collect(notnull array<RK29_ChoiceGroup> outGroups)
	{
		super.Collect(outGroups);
		if (m_aAmmo)
		{
			foreach (RK29_AmmoGroup g : m_aAmmo)
			{
				if (g)
					outGroups.Insert(g);
			}
		}
		if (m_aAttachments)
		{
			foreach (RK29_AttachmentGroup g : m_aAttachments)
			{
				if (g)
					outGroups.Insert(g);
			}
		}
	}
}
