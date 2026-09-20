//------------------------------------------------------------------------------------------------
//! Builds RK29_KitStruct from a kit's composition, and holds the prefab reads the offer and apply
//! passes share. Identity and traits are composition-owned; the captured body supplies only its
//! weapons. Dress and items are not composed here: every garment and item is a choice group, and
//! the resolver lands them through EmitAmmo.
//!
//! Do not factor the repeated three-line prefab loads into a helper that returns the source.
//! Resource is a scope-bound handle, so the local is what keeps the prefab alive; a helper hands
//! back a source into freed memory that reads as a prefab declaring nothing - no wells, no slots,
//! no attachment types - and every reader answers its empty case instead of failing. Tried once,
//! and it silently took out every ammo variant and weapon slot in the mod.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
class RK29_ZoomRange
{
	float m_fMin;
	float m_fMax;
}

//------------------------------------------------------------------------------------------------
class RK29_KitCompose
{
	//============================================================================================
	// Session caches. Each answers a question about a prefab or conf that cannot change inside a
	// session; ClearCaches empties every one at world start, since authors edit these between
	// sessions. Add a cache here and add it to ClearCaches.
	//
	// Every accessor over these hands back the live cache entry, never a copy: read it, never edit
	// it. A pass that needs to change one copies it first, as
	// RK29_KitResolve.EffectiveMountedTypes does.
	//============================================================================================

	//! Prefab component-tree walks stop here - a guard against a cyclic hierarchy, not a tuning knob.
	protected static const int PREFAB_WALK_DEPTH = 10;

	protected static ref map<ResourceName, ResourceName> s_mDefaultMagCache = new map<ResourceName, ResourceName>();
	protected static ref map<ResourceName, ref array<string>> s_mWellsCache = new map<ResourceName, ref array<string>>();
	protected static ref map<ResourceName, bool> s_mMagazineCache = new map<ResourceName, bool>();
	protected static ref map<ResourceName, string> s_mMagWellCache = new map<ResourceName, string>();
	protected static ref map<ResourceName, ref array<string>> s_mSeatedWellsCache = new map<ResourceName, ref array<string>>();
	protected static ref map<ResourceName, ref array<string>> s_mWeaponAttachTypeCache = new map<ResourceName, ref array<string>>();
	protected static ref map<ResourceName, ref array<string>> s_mObstructedCache = new map<ResourceName, ref array<string>>();
	protected static ref map<ResourceName, ref array<string>> s_mRequiredCache = new map<ResourceName, ref array<string>>();
	protected static ref map<ResourceName, ref array<string>> s_mMountedCache = new map<ResourceName, ref array<string>>();
	protected static ref map<ResourceName, bool> s_mReadableCache = new map<ResourceName, bool>();
	protected static ref map<ResourceName, ref RK29_ZoomRange> s_mZoomCache = new map<ResourceName, ref RK29_ZoomRange>();
	protected static ref map<ResourceName, ref array<string>> s_mClothSlotCache = new map<ResourceName, ref array<string>>();

	//! Read-only contract: the instance handed back is shared and lives for the session. Nothing may
	//! write to a composition or to anything hanging off one - entries, choice groups, override steps,
	//! UIInfo. A pass that needs to change one copies it first, as ResolveGroup does.
	protected static ref map<ResourceName, ref RK29_KitComposition> s_mCompositionCache
		= new map<ResourceName, ref RK29_KitComposition>();

	//------------------------------------------------------------------------------------------------
	static RK29_KitStruct Compose(notnull RK29_ClassSetup cls, notnull RK29_KitStruct captured)
	{
		RK29_KitComposition comp = LoadComposition(cls.m_sComposition);
		if (!comp)
		{
			Print(string.Format("[RK29] config ERROR - class '%1' composes nothing: '%2' did not"
				+ " load as an RK29_KitComposition - the kit is not built",
				cls.m_sKitName, cls.m_sComposition), LogLevel.ERROR);
			return null;
		}

		RK29_KitStruct kit = new RK29_KitStruct();
		kit.m_sKitName    = captured.m_sKitName;
		kit.m_sFactionKey = captured.m_sFactionKey;
		kit.m_UIInfo      = captured.m_UIInfo;

		// the composition chain's statement wins over the captured body's
		if (comp.m_UIInfo)
			kit.m_UIInfo = comp.m_UIInfo;

		CopyTraits(comp, kit, cls.m_sComposition);

		// Nothing else is seeded from the body: gear comes from the choice groups at resolve, and
		// apply strips every garment and equipment slot first, so a slot no group answers ends up
		// empty.
		return kit;
	}

	//------------------------------------------------------------------------------------------------
	//! Composition chain alone - the captured prefab's own labels are the engine's business. NONE is
	//! the zero value, so an unfilled row reads as one and is logged as a config fault.
	protected static void CopyTraits(notnull RK29_KitComposition comp, notnull RK29_KitStruct kit,
		ResourceName composition)
	{
		if (!comp.m_aTraits)
			return;

		foreach (RK29_ETrait trait : comp.m_aTraits)
		{
			if (trait == RK29_ETrait.NONE)
			{
				Print(string.Format("[RK29] '%1' declares an unset trait row in %2",
					kit.m_sKitName, FilePath.StripPath(composition)), LogLevel.WARNING);
				continue;
			}
			if (!kit.m_aTraits.Contains(trait))
				kit.m_aTraits.Insert(trait);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! An ammo list into item batches, and the choice path's only exit - every group's picks arrive
	//! here through RK29_KitResolve.ApplyItemGroup - so placement is decided once.
	//!
	//! Placement precedence: the weapon definition wins, and an item alias's own
	//! m_aPreferredContainers fills in only where the weapon states nothing. The alias half is
	//! load-bearing: kit-level groups have no weapon definition, so without it every choice item
	//! (bandages, frags, smokes) was placed by nothing at all.
	static void EmitAmmo(notnull RK29_KitStruct kit, ResourceName weapon, RK29_WeaponDef def,
		array<ref RK29_WeaponAmmo> ammoList, notnull RK29_KitSetup setup)
	{
		if (!ammoList)
			return;

		foreach (RK29_WeaponAmmo ammo : ammoList)
		{
			if (!ammo)
				continue;

			// a worn row never becomes a batch and never reaches the placement solver - it goes
			// straight to the slot map it named. One place turns a resolved row into kit content.
			if (ammo.m_sSlot != "")
				EmitWornRow(kit, ammo, setup);
			else
				EmitCarriedRow(kit, weapon, def, ammo, setup);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void EmitWornRow(notnull RK29_KitStruct kit, notnull RK29_WeaponAmmo ammo,
		notnull RK29_KitSetup setup)
	{
		ResourceName worn = ammo.m_sPrefab;
		if (worn == ResourceName.Empty && ammo.m_sAlias != "")
			worn = setup.ResolveAlias(ammo.m_sAlias, kit.m_sFactionKey);
		if (worn == ResourceName.Empty)
		{
			Print(string.Format("[RK29] config ERROR - worn entry for slot '%1' does"
				+ " not resolve for faction %2 (%3)",
				ammo.m_sSlot, kit.m_sFactionKey, kit.m_sKitName), LogLevel.ERROR);
			return;
		}

		if (ammo.m_bClothing)
			kit.m_mClothing.Set(ammo.m_sSlot, worn);
		else if (ammo.m_sGarmentSlot != "")
			kit.m_mGarmentAttachments.Set(RK29_KitStruct.GarmentSlotKey(ammo.m_sGarmentSlot, ammo.m_sSlot), worn);
		else
			kit.m_mEquipment.Set(ammo.m_sSlot, worn);
	}

	//------------------------------------------------------------------------------------------------
	//! Placement follows EmitAmmo's placement precedence note.
	protected static void EmitCarriedRow(notnull RK29_KitStruct kit, ResourceName weapon,
		RK29_WeaponDef def, notnull RK29_WeaponAmmo ammo, notnull RK29_KitSetup setup)
	{
		// literal, then the weapon's own ammo table, then the faction item catalog, then a
		// magazine variant, then the weapon's authored default
		ResourceName round;
		array<string> aliasPreferred = null;
		int aliasRank = RK29_KitItemBatch.KEEP_RANK_DEFAULT;
		bool viaAlias = false;
		if (ammo.m_sPrefab != ResourceName.Empty)
			round = ammo.m_sPrefab;
		else if (ammo.m_sAlias != "" && def && DeclaresAmmo(def, ammo.m_sAlias))
			round = ResolveAmmo(def, weapon, ammo.m_sAlias, kit, setup);
		else if (ammo.m_sAlias != "")
		{
			round = setup.ResolveAlias(ammo.m_sAlias, kit.m_sFactionKey);
			// the same alias that produced the prefab, so a row is never placed by another
			aliasPreferred = setup.ResolveAliasPreference(ammo.m_sAlias, kit.m_sFactionKey);
			aliasRank = setup.ResolveAliasKeepRank(ammo.m_sAlias);
			viaAlias = true;
			if (round == ResourceName.Empty)
				Print(string.Format("[RK29] config ERROR - ammo '%1' is neither declared by"
					+ " %2 nor an item alias (%3)",
					ammo.m_sAlias, FilePath.StripPath(weapon), kit.m_sKitName), LogLevel.ERROR);
		}
		else
		{
			round = RoundFrom(ResourceName.Empty, ammo.m_sVariant, weapon, kit, setup);
		}

		if (round == ResourceName.Empty)
			return;

		int rounds = ammo.m_iCount;
		if (rounds < 1)
			rounds = 1;

		RK29_KitItemBatch batch = new RK29_KitItemBatch();

		// without a preference the solver's "start a stack where all of it fits" rule pulls small
		// stacks into the biggest container - pistol magazines in the pack, pockets empty
		if (def && def.m_aPreferredContainers && !def.m_aPreferredContainers.IsEmpty())
			batch.m_aPreferred = def.m_aPreferredContainers;
		else if (aliasPreferred && !aliasPreferred.IsEmpty())
			batch.m_aPreferred = aliasPreferred;

		// Most specific wins: a rank the entry or its group stated, else the alias's for an
		// alias-resolved round, else the gun's for its own ammunition, else the default
		if (ammo.m_iKeepRank >= 0)
			batch.m_iKeepRank = ammo.m_iKeepRank;
		else if (viaAlias)
			batch.m_iKeepRank = aliasRank;
		else if (def)
			batch.m_iKeepRank = def.m_iKeepRank;

		for (int i = 0; i < rounds; i++)
			batch.m_aPrefabs.Insert(round);
		kit.m_aItems.Insert(batch);
	}

	//------------------------------------------------------------------------------------------------
	protected static bool DeclaresAmmo(RK29_WeaponDef def, string alias)
	{
		if (!def || !def.m_aAmmo)
			return false;
		foreach (RK29_WeaponAmmoDef ammo : def.m_aAmmo)
		{
			if (ammo && ammo.m_sAlias == alias)
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static ResourceName ResolveAmmo(RK29_WeaponDef def, ResourceName weapon, string alias,
		RK29_KitStruct kit, notnull RK29_KitSetup setup)
	{
		if (!def || !def.m_aAmmo)
		{
			Print(string.Format("[RK29] config ERROR - AMMO \"%1\" but the weapon has no"
				+ " catalog entry (%2)", alias, kit.m_sKitName), LogLevel.ERROR);
			return ResourceName.Empty;
		}

		foreach (RK29_WeaponAmmoDef ammo : def.m_aAmmo)
		{
			if (!ammo || ammo.m_sAlias != alias)
				continue;

			return RoundFrom(ammo.m_sPrefab, ammo.m_sVariant, weapon, kit, setup);
		}

		Print(string.Format("[RK29] config ERROR - AMMO \"%1\" is not declared by %2 (%3)",
			alias, def.m_sId, kit.m_sKitName), LogLevel.ERROR);
		return ResourceName.Empty;
	}

	//------------------------------------------------------------------------------------------------
	//! The ammo precedence rule, stated once: literal prefab, then a magazine variant through the
	//! weapon's own well, then the weapon's authored default magazine. Both ways in land here, so a
	//! weapon-table row and a resolved choice row cannot answer differently.
	protected static ResourceName RoundFrom(ResourceName prefab, string variant, ResourceName weapon,
		RK29_KitStruct kit, notnull RK29_KitSetup setup)
	{
		if (prefab != ResourceName.Empty)
			return prefab;

		if (variant != "")
		{
			ResourceName resolved = setup.FindMagVariant(WellsOf(weapon), variant);
			if (resolved == ResourceName.Empty)
				Print(string.Format("[RK29] config ERROR - ammo variant '%1' not defined"
					+ " for %2 (%3)",
					variant, FilePath.StripPath(weapon), kit.m_sKitName), LogLevel.ERROR);
			return resolved;
		}

		return DefaultMagOf(weapon);
	}

	//------------------------------------------------------------------------------------------------
	//! True when the weapon is known not to take this attachment. A prefab that cannot be read
	//! answers false - an unreadable resource must never silently empty a picker column.
	//!
	//! The offer-side shell over MountFits (RK29_KitApply.FindSeatFor is the apply-side one). It
	//! reads the weapon prefab's own merged tree, so slots on a mounted sub-entity (a UGL's own
	//! rail) are invisible here; the live search looks deeper on purpose, so it may seat something
	//! this never promised but can never refuse something it did.
	//!
	//! Three refusals: no seat fits, something on the gun obstructs it, or something required is
	//! absent - the engine enforces the last two in WeaponAttachmentsStorageComponent.CanStoreItem.
	static bool WeaponRejectsAttachment(ResourceName weapon, ResourceName attachment)
	{
		if (weapon == ResourceName.Empty || attachment == ResourceName.Empty)
			return false;

		array<string> attachTypes = AttachTypesOf(attachment);
		if (attachTypes.IsEmpty())
			return false;
		array<string> weaponTypes = AttachTypesOf(weapon);
		if (weaponTypes.IsEmpty())
			return PrefabReadable(weapon);

		if (!MountsAnywhereOn(weapon, attachment))
			return true;

		// obstruction and prerequisite rules against what this weapon's prefab already carries -
		// same body the legality pass runs over a kit's picked set
		bool missing;
		string type;
		return AttachmentIllegalGiven(MountedTypesOf(weapon), attachment, missing, type);
	}

	//------------------------------------------------------------------------------------------------
	protected static bool MountsAnywhereOn(ResourceName weapon, ResourceName attachment)
	{
		array<string> seats = {};
		SeatsTakenOn(weapon, attachment, seats);
		return !seats.IsEmpty();
	}

	//------------------------------------------------------------------------------------------------
	//! The prerequisite and obstruction rules, against a mounted set the caller supplies - the
	//! weapon prefab's own for WeaponRejectsAttachment, the set a kit's picks will build for the
	//! legality pass. Attachments declare both lists on one
	//! SCR_WeaponAttachmentObstructionAttributes, so nobody maintains a list of exceptions.
	//!
	//! Exact type match, not MountFits, on both halves: this asks whether a named part is on the
	//! gun, and letting a subclass answer for its base would invent a rule the data does not make.
	//!
	//! Out-params carry which type and which way round, never a sentence. outType "" and outMissing
	//! false on a false return.
	static bool AttachmentIllegalGiven(notnull array<string> mounted, ResourceName attachment,
		out bool outMissing, out string outType)
	{
		outMissing = false;
		outType = "";
		if (attachment == ResourceName.Empty)
			return false;

		foreach (string need : RequiredTypesOf(attachment))
		{
			if (!mounted.Contains(need))
			{
				outMissing = true;
				outType = need;
				return true;
			}
		}

		foreach (string blocker : ObstructedTypesOf(attachment))
		{
			if (mounted.Contains(blocker))
			{
				outType = blocker;
				return true;
			}
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! The attachment-to-slot compatibility rule, and the only one in the mod. A slot names the
	//! loosest mount it accepts; an attachment names its own, which may be a subclass of that - RHS
	//! types the M40's rail AttachmentOpticsRIS1913 and its scopes AttachmentOpticsRIS1913Short, and
	//! the engine mounts them fine. So ask the type system, never compare spellings. Prefix matching
	//! is wrong too: AttachmentOpticsDovetailUK59 inherits straight from AttachmentOptics.
	//!
	//! Spelling equality is tested before ToType() on purpose: a class the script type system does
	//! not know resolves to no typename, and two identical spellings still have to match.
	static bool MountFits(string attachType, string slotType)
	{
		if (attachType == slotType)
			return true;

		typename attach = attachType.ToType();
		typename slot   = slotType.ToType();
		if (!attach || !slot)
			return false;

		return attach.IsInherited(slot);
	}

	//------------------------------------------------------------------------------------------------
	//! Which of the weapon's declared seat types this attachment answers. Weapon-relative on
	//! purpose: one group may legitimately offer a carry-handle sight and an AK dovetail sight that
	//! never fit the same gun. RK29_KitLint.VerifyAttachmentSeats compares these lists to catch a
	//! group whose entries fill disjoint seats - the one incoherence a derived seat cannot repair.
	static void SeatsTakenOn(ResourceName weapon, ResourceName attachment, notnull array<string> outSeats)
	{
		outSeats.Clear();

		array<string> attachTypes = AttachTypesOf(attachment);
		array<string> weaponTypes = AttachTypesOf(weapon);
		foreach (string slot : weaponTypes)
		{
			if (outSeats.Contains(slot))
				continue;
			foreach (string mount : attachTypes)
			{
				if (MountFits(mount, slot))
				{
					outSeats.Insert(slot);
					break;
				}
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Never null - an unreadable prefab caches an empty list.
	protected static array<string> ObstructedTypesOf(ResourceName attachment)
	{
		LoadAttachmentRules(attachment);
		return s_mObstructedCache.Get(attachment);
	}

	//------------------------------------------------------------------------------------------------
	protected static array<string> RequiredTypesOf(ResourceName attachment)
	{
		LoadAttachmentRules(attachment);
		return s_mRequiredCache.Get(attachment);
	}

	//------------------------------------------------------------------------------------------------
	//! One read fills both rule lists - they are authored side by side on the same
	//! SCR_WeaponAttachmentObstructionAttributes. The two caches are written together and only
	//! together, which is what lets the obstructed one answer the "already loaded?" question.
	protected static void LoadAttachmentRules(ResourceName attachment)
	{
		array<string> cached = s_mObstructedCache.Get(attachment);
		if (cached)
			return;

		array<string> obstructed = {};
		array<string> required = {};
		Resource res = Resource.Load(attachment);
		if (res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			if (src)
			{
				for (int i = 0, n = src.GetComponentCount(); i < n; i++)
					WalkAttachmentRules(src.GetComponent(i), obstructed, required, 0);
			}
		}

		s_mObstructedCache.Set(attachment, obstructed);
		s_mRequiredCache.Set(attachment, required);
	}

	//------------------------------------------------------------------------------------------------
	//! What is actually seated on the weapon by its prefab, not the slots it merely offers: an
	//! AK-74N declares an empty GP-25 slot, the grenadier's rifle the same slot with a launcher in
	//! it, and only the second obstructs anything.
	static array<string> MountedTypesOf(ResourceName weapon)
	{
		array<string> types = s_mMountedCache.Get(weapon);
		if (types)
			return types;

		types = {};
		Resource res = Resource.Load(weapon);
		if (res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			if (src)
			{
				for (int i = 0, n = src.GetComponentCount(); i < n; i++)
					WalkMountedTypes(src.GetComponent(i), types, 0);
			}
		}
		s_mMountedCache.Set(weapon, types);
		return types;
	}

	//------------------------------------------------------------------------------------------------
	//! Both rule arrays out of one recursion. Neither is descended into: its members are the
	//! AttachmentType classes, so recursing finds nothing while costing the walk.
	protected static void WalkAttachmentRules(BaseContainer c, notnull array<string> outObstructed,
		notnull array<string> outRequired, int depth)
	{
		if (!c || depth > PREFAB_WALK_DEPTH)
			return;

		CollectTypeList(c, "m_aObstructedAttachmentTypes", outObstructed);
		CollectTypeList(c, "m_aRequiredAttachmentTypes", outRequired);

		for (int i = 0, n = c.GetNumVars(); i < n; i++)
		{
			string varName = c.GetVarName(i);
			if (varName == "m_aObstructedAttachmentTypes" || varName == "m_aRequiredAttachmentTypes")
				continue;
			BaseContainer sub = c.GetObject(varName);
			if (sub)
				WalkAttachmentRules(sub, outObstructed, outRequired, depth + 1);
			BaseContainerList list = c.GetObjectArray(varName);
			if (!list)
				continue;
			for (int j = 0, m = list.Count(); j < m; j++)
				WalkAttachmentRules(list.Get(j), outObstructed, outRequired, depth + 1);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void CollectTypeList(notnull BaseContainer c, string varName, notnull array<string> outTypes)
	{
		BaseContainerList entries = c.GetObjectArray(varName);
		if (!entries)
			return;

		for (int i = 0, n = entries.Count(); i < n; i++)
		{
			BaseContainer entry = entries.Get(i);
			if (entry)
				outTypes.Insert(entry.GetClassName());
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void WalkMountedTypes(BaseContainer c, notnull array<string> outTypes, int depth)
	{
		if (!c || depth > PREFAB_WALK_DEPTH)
			return;

		ResourceName seated;
		if (c.Get("Prefab", seated) && seated != ResourceName.Empty)
		{
			array<string> seatedTypes = AttachTypesOf(seated);
			foreach (string t : seatedTypes)
			{
				// guarded as WalkSeatedWells guards: the walk reaches one seated prefab down more
				// than one var, and a type listed twice is still one seat
				if (!outTypes.Contains(t))
					outTypes.Insert(t);
			}
		}

		for (int i = 0, n = c.GetNumVars(); i < n; i++)
		{
			BaseContainer sub = c.GetObject(c.GetVarName(i));
			if (sub)
				WalkMountedTypes(sub, outTypes, depth + 1);
			BaseContainerList list = c.GetObjectArray(c.GetVarName(i));
			if (!list)
				continue;
			for (int j = 0, m = list.Count(); j < m; j++)
				WalkMountedTypes(list.Get(j), outTypes, depth + 1);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! A weapon's slots and an attachment's own mount are the same AttachmentType classes, which is
	//! what makes intersecting the two sides meaningful.
	static array<string> AttachTypesOf(ResourceName prefab)
	{
		array<string> types = s_mWeaponAttachTypeCache.Get(prefab);
		if (!types)
		{
			types = {};
			CollectAttachmentTypes(prefab, types);
			s_mWeaponAttachTypeCache.Set(prefab, types);
		}
		return types;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether the prefab loads at all, so "declares no mounts" can be told apart from "could not be
	//! read". Every reader in this file answers an unreadable prefab with its own empty case, so a
	//! caller that must not treat unreadable as an answer asks this first.
	static bool PrefabReadable(ResourceName prefab)
	{
		bool readable;
		if (s_mReadableCache.Find(prefab, readable))
			return readable;

		readable = false;
		if (prefab != ResourceName.Empty)
		{
			Resource res = Resource.Load(prefab);
			if (res.IsValid())
				readable = res.GetResource().ToEntitySource() != null;
		}

		s_mReadableCache.Set(prefab, readable);
		return readable;
	}

	//------------------------------------------------------------------------------------------------
	//! Does this garment prefab offer that cloth slot to an attachment? Static, like every read
	//! here: the slot list is authored, and RHS's runtime slot blocking touches velcro slots only.
	static bool GarmentOffersSlot(ResourceName garment, string slotName)
	{
		return EnabledClothSlotsOf(garment).Contains(slotName);
	}

	//------------------------------------------------------------------------------------------------
	//! The cloth slots a garment prefab offers: every LoadoutSlotInfo under its cloth component that
	//! is not disabled, provided the cloth-node storage an attachment is inserted into exists and
	//! is on. Both halves matter - RHS's override of the vanilla PASGT declares the NVG slot and the
	//! storage, each Enabled 0. An unstated flag is enabled: vanilla goggles slots state nothing.
	//! Merged read, no ancestry walk. Empty for an unreadable prefab.
	static array<string> EnabledClothSlotsOf(ResourceName garment)
	{
		array<string> slots;
		if (s_mClothSlotCache.Find(garment, slots))
			return slots;

		slots = {};
		if (garment != ResourceName.Empty)
		{
			Resource res = Resource.Load(garment);
			if (res.IsValid())
				CollectEnabledClothSlots(res.GetResource().ToEntitySource(), slots);
		}

		s_mClothSlotCache.Set(garment, slots);
		return slots;
	}

	//------------------------------------------------------------------------------------------------
	protected static void CollectEnabledClothSlots(IEntitySource src, notnull array<string> outSlots)
	{
		if (!src)
			return;

		// the typename overload: RHS's helmet storage is a subclass of the engine's
		IEntityComponentSource nodes = SCR_BaseContainerTools.FindComponentSource(src, ClothNodeStorageComponent);
		if (!nodes || !SourceEnabled(nodes))
			return;

		IEntityComponentSource cloth = SCR_BaseContainerTools.FindComponentSource(src, BaseLoadoutClothComponent);
		if (!cloth)
			return;

		BaseContainerList list = cloth.GetObjectArray("Slots");
		if (!list)
			return;

		for (int i = 0, n = list.Count(); i < n; i++)
		{
			BaseContainer slot = list.Get(i);
			if (!slot || !SourceEnabled(slot))
				continue;
			string name = slot.GetName();
			if (name != "" && !outSlots.Contains(name))
				outSlots.Insert(name);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! "Enabled" as authored, TRUE when unstated - Get leaves the local alone then.
	protected static bool SourceEnabled(notnull BaseContainer c)
	{
		bool enabled = true;
		c.Get("Enabled", enabled);
		return enabled;
	}

	//------------------------------------------------------------------------------------------------
	protected static void CollectAttachmentTypes(ResourceName prefab, notnull array<string> outTypes)
	{
		if (prefab == ResourceName.Empty)
			return;

		Resource res = Resource.Load(prefab);
		if (!res.IsValid())
			return;

		IEntitySource src = res.GetResource().ToEntitySource();
		if (!src)
			return;

		for (int i = 0, n = src.GetComponentCount(); i < n; i++)
			WalkAttachmentTypes(src.GetComponent(i), outTypes, 0);
	}

	//------------------------------------------------------------------------------------------------
	protected static void WalkAttachmentTypes(BaseContainer c, notnull array<string> outTypes, int depth)
	{
		if (!c || depth > PREFAB_WALK_DEPTH)
			return;

		BaseContainer typeObj = c.GetObject("AttachmentType");
		if (typeObj)
			outTypes.Insert(typeObj.GetClassName());

		for (int i = 0, n = c.GetNumVars(); i < n; i++)
		{
			string varName = c.GetVarName(i);
			if (varName == "AttachmentType")
				continue;
			BaseContainer sub = c.GetObject(varName);
			if (sub)
				WalkAttachmentTypes(sub, outTypes, depth + 1);
			BaseContainerList list = c.GetObjectArray(varName);
			if (!list)
				continue;
			for (int j = 0, m = list.Count(); j < m; j++)
				WalkAttachmentTypes(list.Get(j), outTypes, depth + 1);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Every cache declared at the head of this class - one left out composes last session's
	//! configs while the author debugs the new ones.
	static void ClearCaches()
	{
		s_mDefaultMagCache.Clear();
		s_mWellsCache.Clear();
		s_mMagazineCache.Clear();
		s_mMagWellCache.Clear();
		s_mSeatedWellsCache.Clear();
		s_mWeaponAttachTypeCache.Clear();
		s_mObstructedCache.Clear();
		s_mRequiredCache.Clear();
		s_mMountedCache.Clear();
		s_mReadableCache.Clear();
		s_mZoomCache.Clear();
		s_mClothSlotCache.Clear();
		s_mCompositionCache.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! Read once per session - see the read-only contract on s_mCompositionCache.
	static RK29_KitComposition LoadComposition(ResourceName res)
	{
		if (res == ResourceName.Empty)
			return null;

		RK29_KitComposition cached;
		if (s_mCompositionCache.Find(res, cached))
			return cached;

		Resource r = Resource.Load(res);
		if (!r.IsValid())
		{
			Print(string.Format("[RK29] config ERROR - composition not found: %1",
				res), LogLevel.ERROR);
			return null;
		}
		RK29_KitComposition comp = RK29_KitComposition.Cast(BaseContainerTools.CreateInstanceFromContainer(r.GetResource().ToBaseContainer()));
		if (!comp)
		{
			Print(string.Format("[RK29] config ERROR - not an RK29_KitComposition: %1",
				res), LogLevel.ERROR);
			return null;
		}

		s_mCompositionCache.Set(res, comp);
		return comp;
	}

	//------------------------------------------------------------------------------------------------
	//! The well an item presents, not the wells a weapon accepts. Do not reuse WellsOf: both are
	//! authored as a MagazineWell object, so a spare Launcher_M72A3 - itself a weapon - would hand
	//! back its own muzzle's well and read as a rocket that chambers somewhere. The discriminator is
	//! the owning component: an item presents its well from a MagazineComponent.
	protected static string MagazineWellOf(ResourceName round)
	{
		string well;
		if (s_mMagWellCache.Find(round, well))
			return well;

		well = "";
		Resource res = Resource.Load(round);
		if (res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			if (src)
			{
				for (int i = 0, n = src.GetComponentCount(); i < n; i++)
					CollectMagazineWell(src.GetComponent(i), well, 0);
			}
		}
		s_mMagWellCache.Set(round, well);
		return well;
	}

	//------------------------------------------------------------------------------------------------
	//! inout, not out: the value is read on entry - a well already found stops the recursion.
	protected static void CollectMagazineWell(BaseContainer c, inout string inoutWell, int depth)
	{
		if (!c || depth > 4 || inoutWell != "")
			return;

		if (c.GetClassName() == "MagazineComponent")
		{
			BaseContainer well = c.GetObject("MagazineWell");
			if (well)
			{
				inoutWell = well.GetClassName();
				return;
			}
		}

		BaseContainerList children = c.GetObjectArray("components");
		if (!children)
			return;
		for (int i = 0, n = children.Count(); i < n; i++)
			CollectMagazineWell(children.Get(i), inoutWell, depth + 1);
	}

	//------------------------------------------------------------------------------------------------
	//! Wells of prefabs seated in this weapon's slots, not the weapon's own. The M203 is not a
	//! component of the rifle - the rifle authors an AttachmentSlot whose default Prefab is
	//! UGL_M203_short, and the 40mm well lives in that file.
	protected static array<string> SeatedWellsOf(ResourceName weapon)
	{
		array<string> wells = s_mSeatedWellsCache.Get(weapon);
		if (wells)
			return wells;

		wells = {};
		Resource res = Resource.Load(weapon);
		if (res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			if (src)
			{
				for (int i = 0, n = src.GetComponentCount(); i < n; i++)
					WalkSeatedWells(src.GetComponent(i), wells, 0);
			}
		}
		s_mSeatedWellsCache.Set(weapon, wells);
		return wells;
	}

	//------------------------------------------------------------------------------------------------
	protected static void WalkSeatedWells(BaseContainer c, notnull array<string> outWells, int depth)
	{
		if (!c || depth > PREFAB_WALK_DEPTH)
			return;

		ResourceName seated;
		if (c.Get("Prefab", seated) && seated != ResourceName.Empty)
		{
			foreach (string w : WellsOf(seated))
			{
				if (!outWells.Contains(w))
					outWells.Insert(w);
			}
		}

		for (int i = 0, n = c.GetNumVars(); i < n; i++)
		{
			BaseContainer sub = c.GetObject(c.GetVarName(i));
			if (sub)
				WalkSeatedWells(sub, outWells, depth + 1);
			BaseContainerList list = c.GetObjectArray(c.GetVarName(i));
			if (!list)
				continue;
			for (int j = 0, m = list.Count(); j < m; j++)
				WalkSeatedWells(list.Get(j), outWells, depth + 1);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Where a round chambers, asked of the metal instead of authored: 5.56 matches the rifle's own
	//! muzzle, 40mm matches the UGL seated in its M203 slot, and the M72's "rocket" is a whole
	//! launcher with no MagazineComponent, so it presents no well and chambers nowhere.
	static RK29_ELoadedSeat DeriveLoadedSeat(ResourceName weapon, ResourceName round)
	{
		if (weapon == ResourceName.Empty || round == ResourceName.Empty)
			return RK29_ELoadedSeat.NONE;

		string well = MagazineWellOf(round);
		if (well == "")
			return RK29_ELoadedSeat.NONE;

		if (WellsOf(weapon).Contains(well))
			return RK29_ELoadedSeat.OWN_MUZZLE;
		if (SeatedWellsOf(weapon).Contains(well))
			return RK29_ELoadedSeat.UNDERBARREL;

		return RK29_ELoadedSeat.NONE;
	}

	//------------------------------------------------------------------------------------------------
	//! MagazineWell class names a weapon prefab accepts, across all its muzzles. A prefab that
	//! cannot be read answers empty but is NOT cached: an unreadable resource must never be
	//! remembered as a gun with no well (PresentsNoWell reads the cache to tell the two apart).
	static array<string> WellsOf(ResourceName weapon)
	{
		array<string> wells;
		if (s_mWellsCache.Find(weapon, wells))
			return wells;

		wells = {};
		Resource res = Resource.Load(weapon);
		if (!res.IsValid())
			return wells;

		IEntitySource src = res.GetResource().ToEntitySource();
		if (src)
		{
			for (int i = 0, n = src.GetComponentCount(); i < n; i++)
				CollectWells(src.GetComponent(i), wells, 0);
		}
		s_mWellsCache.Set(weapon, wells);
		return wells;
	}

	//------------------------------------------------------------------------------------------------
	//! True only for a prefab that was read and has no magazine well (the M72, whose rocket is not
	//! a magazine). An unreadable prefab answers false, so a caller that would skip a safeguard
	//! for a well-less gun keeps the safeguard where the truth is unknown.
	static bool PresentsNoWell(ResourceName weapon)
	{
		return WellsOf(weapon).IsEmpty() && s_mWellsCache.Contains(weapon);
	}

	//------------------------------------------------------------------------------------------------
	protected static void CollectWells(BaseContainer container, notnull array<string> wells, int depth)
	{
		if (!container || depth > 4)
			return;

		BaseContainer well = container.GetObject("MagazineWell");
		if (well)
		{
			string cls = well.GetClassName();
			if (cls != "" && !wells.Contains(cls))
				wells.Insert(cls);
		}

		BaseContainerList children = container.GetObjectArray("components");
		if (!children)
			return;
		for (int i = 0, n = children.Count(); i < n; i++)
			CollectWells(children.Get(i), wells, depth + 1);
	}

	//------------------------------------------------------------------------------------------------
	//! The weapon prefab's own authored magazine (MagazineTemplate, merged view), cached.
	static ResourceName DefaultMagOf(ResourceName weapon)
	{
		if (weapon == ResourceName.Empty)
			return ResourceName.Empty;

		ResourceName mag;
		if (s_mDefaultMagCache.Find(weapon, mag))
			return mag;

		Resource res = Resource.Load(weapon);
		if (res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			if (src)
			{
				for (int i = 0, n = src.GetComponentCount(); i < n; i++)
				{
					mag = FindMagTemplate(src.GetComponent(i), 0);
					if (mag != ResourceName.Empty)
						break;
				}
			}
		}

		s_mDefaultMagCache.Set(weapon, mag);
		return mag;
	}

	//------------------------------------------------------------------------------------------------
	protected static ResourceName FindMagTemplate(BaseContainer container, int depth)
	{
		if (!container || depth > 4)
			return ResourceName.Empty;

		ResourceName mag;
		container.Get("MagazineTemplate", mag);
		if (mag != ResourceName.Empty)
			return mag;

		BaseContainerList children = container.GetObjectArray("components");
		if (!children)
			return ResourceName.Empty;
		for (int i = 0, n = children.Count(); i < n; i++)
		{
			mag = FindMagTemplate(children.Get(i), depth + 1);
			if (mag != ResourceName.Empty)
				return mag;
		}
		return ResourceName.Empty;
	}

	//------------------------------------------------------------------------------------------------
	//! Is this prefab a magazine - asked of its components, never of the folder it was filed in:
	//! the SMAW's rounds sit under Weapons/Magazines and the RPG's under Weapons/Ammo, so a path
	//! test packed two launchers' rockets by opposite rules (RK29_KitApply.StacksTogether).
	//!
	//! One flat pass over the merged component list, never an ancestry walk. Matched on the base
	//! class so a modded subclass of BaseMagazineComponent counts, spelling first because a class
	//! the type system does not know resolves to no typename.
	//!
	//! FALSE for a prefab that cannot be read, which is why the caller asks PrefabReadable first.
	static bool IsMagazine(ResourceName prefab)
	{
		if (prefab == ResourceName.Empty)
			return false;

		bool magazine;
		if (s_mMagazineCache.Find(prefab, magazine))
			return magazine;

		magazine = false;
		Resource res = Resource.Load(prefab);
		if (res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			if (src)
			{
				for (int i = 0, n = src.GetComponentCount(); i < n; i++)
				{
					IEntityComponentSource comp = src.GetComponent(i);
					if (!comp || !IsMagazineClass(comp.GetClassName()))
						continue;

					magazine = true;
					break;
				}
			}
		}

		s_mMagazineCache.Set(prefab, magazine);
		return magazine;
	}

	//------------------------------------------------------------------------------------------------
	//! Prefab-source scanning reports the class a component was authored as, so the test has to be
	//! the type system's rather than a name list.
	protected static bool IsMagazineClass(string className)
	{
		if (className == "MagazineComponent")
			return true;

		typename cls = className.ToType();
		if (!cls)
			return false;

		return cls.IsInherited(BaseMagazineComponent);
	}

	//------------------------------------------------------------------------------------------------
	//! An optic's zoom band off its own prefab source; 1 and 1 for anything that magnifies nothing.
	//! Never authored. One flat pass over the merged component list, never an ancestry walk.
	//! Rules, verified against the vanilla and RHS prefabs we field:
	//!  - `Enabled 0` is skipped, absent means enabled (the MBS carries a switched-off backup).
	//!  - a CollimatorSights component is a red dot: 1x, with no magnification to read.
	//!  - everything else contributes only if it has the sights fields, matching on what a component
	//!    carries rather than a class-name list (RHS ships RHS_2DSightsComponentV2).
	//!  - the greatest ceiling wins across components.
	static void ReadZoomRange(ResourceName prefab, out float minZoom, out float maxZoom)
	{
		minZoom = 1;
		maxZoom = 1;
		if (prefab == ResourceName.Empty)
			return;

		RK29_ZoomRange cached = s_mZoomCache.Get(prefab);
		if (cached)
		{
			minZoom = cached.m_fMin;
			maxZoom = cached.m_fMax;
			return;
		}

		float bestMin = 1;
		float bestMax = 1;
		bool found = false;
		bool read = false;

		Resource res = Resource.Load(prefab);
		if (res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			if (src)
			{
				read = true;
				for (int i = 0, n = src.GetComponentCount(); i < n; i++)
				{
					IEntityComponentSource comp = src.GetComponent(i);
					if (!comp)
						continue;

					// absent = enabled: only an authored zero switches a component off
					bool enabled = true;
					comp.Get("Enabled", enabled);
					if (!enabled)
						continue;

					float compMin;
					float compMax;
					if (!ComponentZoom(comp, compMin, compMax))
						continue;

					if (found && compMax <= bestMax)
						continue;

					found = true;
					bestMin = compMin;
					bestMax = compMax;
				}
			}
		}

		minZoom = bestMin;
		maxZoom = bestMax;

		// only a real read is remembered: a prefab that would not load is not an answer of 1x, and
		// caching one would make the failure permanent for the session
		if (!read)
			return;

		RK29_ZoomRange range = new RK29_ZoomRange();
		range.m_fMin = bestMin;
		range.m_fMax = bestMax;
		s_mZoomCache.Set(prefab, range);
	}

	//------------------------------------------------------------------------------------------------
	//! One component's own zoom range, false when it is not a sight at all.
	//!
	//! m_fMagnification starts at 1 and is overwritten only on a successful Get: its class default
	//! is 10, so falling back to that would have every non-magnifying component claim 10x. A
	//! variable scope states its range in SightsFOVInfo (a fixed one authors base zoom alone - the
	//! TA648MDO states 6 and no maximum), and the VC18DSCO authors m_fMagnification 1 beside a
	//! zoomMax of 8 - which is why the ceiling is the greatest of the three, not the last read.
	protected static bool ComponentZoom(notnull IEntityComponentSource comp, out float compMin, out float compMax)
	{
		compMin = 1;
		compMax = 1;

		if (comp.GetClassName().Contains("CollimatorSights"))
			return true;

		float mag = 1.0;
		bool sights = comp.Get("m_fMagnification", mag);

		float baseZoom = 0;
		float zoomMax = 0;
		BaseContainer fov = comp.GetObject("SightsFOVInfo");
		if (fov)
		{
			if (fov.Get("m_fBaseZoom", baseZoom))
				sights = true;
			if (fov.Get("m_fZoomMax", zoomMax))
				sights = true;
		}

		if (!sights)
			return false;

		compMin = mag;
		if (baseZoom > 0)
			compMin = baseZoom;
		compMax = Math.Max(compMin, Math.Max(zoomMax, mag));
		return true;
	}
}
