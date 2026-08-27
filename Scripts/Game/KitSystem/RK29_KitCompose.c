//------------------------------------------------------------------------------------------------
//! Builds RK29_KitStruct from config blocks. Dress, identity and items are config-owned; the
//! captured body supplies only its weapons, which the composition then overlays slot-wise.
//! Also home of the /kitdigest tooling.
//------------------------------------------------------------------------------------------------
class RK29_KitCompose
{
	protected static ref map<ResourceName, ResourceName> s_mDefaultMagCache = new map<ResourceName, ResourceName>();
	protected static ref map<ResourceName, ref array<string>> s_mWellsCache = new map<ResourceName, ref array<string>>();

	//--------------------------------------------------------------------------------------------
	static RK29_KitStruct Compose(notnull RK29_ClassSetup cls, notnull RK29_KitStruct captured, notnull RK29_KitSetup setup,
		out array<ref RK29_WeaponSlot> outSlots)
	{
		RK29_KitComposition comp = LoadComposition(cls.m_sComposition);
		if (!comp)
			return null;

		// weapons are declared per slot - one option is a fixed weapon, several make a picker
		// column - so the composition hands the groups back rather than applying them here
		outSlots = comp.m_aWeaponSlots;

		RK29_KitStruct kit = new RK29_KitStruct();
		kit.m_sKitName      = captured.m_sKitName;
		kit.m_sFactionKey   = captured.m_sFactionKey;
		kit.m_sSourcePrefab = captured.m_sSourcePrefab;
		kit.m_UIInfo        = captured.m_UIInfo;

		// Identity, nearest statement wins: the captured body is the fallback, the composition
		// chain states what the ROLE looks like (and a faction kit refines it), and a roster
		// class still overrides outright - which is what composition-less legacy entries use.
		// This is the seam that lets classes share a body: icon, image and name follow the kit,
		// so the picker, the HUD and the stamped body all read the right one.
		if (comp.m_UIInfo)
			kit.m_UIInfo = comp.m_UIInfo;
		if (cls.m_UIInfo)
			kit.m_UIInfo = cls.m_UIInfo;

		// traits come off the composition chain alone - the captured prefab's own labels are
		// merged by the engine at read time and stay whatever the prefab author made them
		if (comp.m_aTraits)
		{
			foreach (RK29_ETrait trait : comp.m_aTraits)
			{
				if (trait == RK29_ETrait.NONE)
				{
					Print("[RK29] '" + kit.m_sKitName + "' declares an unset trait row in "
						+ FileOf(cls.m_sComposition), LogLevel.WARNING);
					continue;
				}
				if (!kit.m_aTraits.Contains(trait))
					kit.m_aTraits.Insert(trait);
			}
		}

		// Dress is NOT seeded from the body. Apply deletes every garment and clears every
		// equipment slot before re-dressing, so the composition is the whole truth: a slot
		// nobody declares ends up empty rather than inheriting whichever body this kit spawned
		// on. That is what makes the body interchangeable.
		foreach (int idx, ResourceName weapon : captured.m_mWeapons)
		{
			// NOT the throwable slot: a config kit counts its grenades in m_aItems, and apply
			// promotes one of those into the slot. Carrying the prefab's slot grenade over as
			// well spawns it a second time - one extra grenade or smoke on every kit.
			if (idx == RK29_KitStruct.GRENADE_SLOT)
				continue;
			kit.m_mWeapons.Set(idx, weapon);
		}

		// pass 1: clothing, blocks then the composition's own (later-wins)
		if (comp.m_aBlocks)
		{
			foreach (RK29_BlockRef bref : comp.m_aBlocks)
			{
				RK29_KitBlock block = LoadBlock(bref);
				if (!block)
					continue;

				if (block.m_aClothing)
				{
					foreach (RK29_BlockClothingEntry bc : block.m_aClothing)
						ApplyClothingEntry(bc, kit, setup);
				}

				if (block.m_aEquipment)
				{
					foreach (RK29_BlockClothingEntry be : block.m_aEquipment)
						ApplyEquipmentEntry(be, kit, setup);
				}
			}
		}
		if (comp.m_aClothing)
		{
			foreach (RK29_BlockClothingEntry cc : comp.m_aClothing)
				ApplyClothingEntry(cc, kit, setup);
		}
		if (comp.m_aEquipment)
		{
			foreach (RK29_BlockClothingEntry ce : comp.m_aEquipment)
				ApplyEquipmentEntry(ce, kit, setup);
		}

		ResourceName primary;
		if (!kit.m_mWeapons.Find(0, primary))
			primary = ResourceName.Empty;
		kit.m_sPrimaryWeapon = primary;


		// pass 2: items - block items (with their ref's overrides), then the
		// composition's own inline items
		if (comp.m_aBlocks)
		{
		foreach (RK29_BlockRef iref : comp.m_aBlocks)
		{
			RK29_KitBlock block = LoadBlock(iref);
			if (!block || !block.m_aItems)
				continue;

			foreach (RK29_BlockItemEntry entry : block.m_aItems)
			{
				if (!entry)
					continue;

				int count = entry.m_iCount;
				if (count < 1)
					count = 1;

				// overrides match identity literally and apply before resolution, so
				// "set 0" silences entries a kit has no weapon for
				int overrideCount = OverrideCountFor(iref, entry);
				if (overrideCount >= 0)
					count = overrideCount;
				if (count <= 0)
					continue;

				ResourceName prefab = ResolveEntry(entry, kit, setup);
				if (prefab == ResourceName.Empty)
					continue;
				RK29_KitItemBatch batch = new RK29_KitItemBatch();
				batch.m_sTargetHint = entry.m_sTargetHint;
				batch.m_aPreferred = PreferenceFor(entry, kit, setup);
				batch.m_bPrimaryAttachment = entry.m_bOnlyIfPrimaryTakesIt;
				for (int i = 0; i < count; i++)
					batch.m_aPrefabs.Insert(prefab);
				kit.m_aItems.Insert(batch);
			}
		}
		}

		if (comp.m_aItems)
		{
			foreach (RK29_BlockItemEntry own : comp.m_aItems)
			{
				if (!own)
					continue;
				int ownCount = own.m_iCount;
				if (ownCount < 1)
					ownCount = 1;
				ResourceName ownPrefab = ResolveEntry(own, kit, setup);
				if (ownPrefab == ResourceName.Empty)
					continue;
				RK29_KitItemBatch ownBatch = new RK29_KitItemBatch();
				ownBatch.m_sTargetHint = own.m_sTargetHint;
				ownBatch.m_aPreferred = PreferenceFor(own, kit, setup);
				ownBatch.m_bPrimaryAttachment = own.m_bOnlyIfPrimaryTakesIt;
				for (int i = 0; i < ownCount; i++)
					ownBatch.m_aPrefabs.Insert(ownPrefab);
				kit.m_aItems.Insert(ownBatch);
			}
		}

		return kit;
	}

	//--------------------------------------------------------------------------------------------
	//! Lays a weapon option over a BASE kit (the composition without any weapon applied).
	//! The option seats its weapon and brings its blocks: gear deltas, ammo counts, whatever
	//! grenade set goes with that weapon. Always applied to the base, never to an already
	//! optioned kit, so re-picking a weapon cannot stack a second copy of its blocks.
	static RK29_KitStruct ApplyWeaponOption(notnull RK29_KitStruct base,
		notnull RK29_WeaponOption option, int slot, notnull RK29_KitSetup setup)
	{
		RK29_KitStruct kit = base.DeepCopy();

		ResourceName weapon = setup.WeaponPrefabOf(option, base.m_sFactionKey);
		if (weapon == ResourceName.Empty)
		{
			Print("[RK29] config ERROR - weapon option '" + option.m_sWeapon + "' resolves to no prefab ("
				+ kit.m_sKitName + ")", LogLevel.ERROR);
			return kit;
		}

		kit.m_mWeapons.Set(slot, weapon);
		if (slot == 0)
			kit.m_sPrimaryWeapon = weapon;

		// ammo first: it is the one thing every weapon brings, so it lives on the option
		// rather than in a block that would otherwise hold nothing else
		RK29_WeaponDef def = setup.FindWeaponDef(option.m_sWeapon);
		EmitAmmo(kit, weapon, def, option.m_aAmmo, setup);

		if (!option.m_aBlocks)
			return kit;

		foreach (RK29_BlockRef bref : option.m_aBlocks)
		{
			RK29_KitBlock block = LoadBlock(bref);
			if (!block)
				continue;

			if (block.m_aClothing)
			{
				foreach (RK29_BlockClothingEntry bc : block.m_aClothing)
					ApplyClothingEntry(bc, kit, setup);
			}
			if (block.m_aEquipment)
			{
				foreach (RK29_BlockClothingEntry be : block.m_aEquipment)
					ApplyEquipmentEntry(be, kit, setup);
			}
			if (!block.m_aItems)
				continue;

			foreach (RK29_BlockItemEntry entry : block.m_aItems)
			{
				if (!entry)
					continue;

				int count = entry.m_iCount;
				if (count < 1)
					count = 1;
				int overrideCount = OverrideCountFor(bref, entry);
				if (overrideCount >= 0)
					count = overrideCount;
				if (count <= 0)
					continue;

				ResourceName prefab = ResolveEntry(entry, kit, setup);
				if (prefab == ResourceName.Empty)
					continue;
				RK29_KitItemBatch batch = new RK29_KitItemBatch();
				batch.m_sTargetHint = entry.m_sTargetHint;
				batch.m_aPreferred  = PreferenceFor(entry, kit, setup);
				batch.m_bPrimaryAttachment = entry.m_bOnlyIfPrimaryTakesIt;
				for (int i = 0; i < count; i++)
					batch.m_aPrefabs.Insert(prefab);
				kit.m_aItems.Insert(batch);
			}
		}

		return kit;
	}

	//--------------------------------------------------------------------------------------------
	//! Every weapon slot a class offers, applied to the base kit in slot order. The player
	//! picks the primary; every other slot takes its class default. A stashed weapon that is
	//! no longer offered falls back to the default rather than composing a weaponless kit.
	static RK29_KitStruct ApplyWeaponChoices(notnull RK29_KitStruct base,
		array<ref RK29_WeaponSlot> slots, ResourceName chosenPrimary, notnull RK29_KitSetup setup)
	{
		RK29_KitStruct kit = base;
		bool applied = false;

		for (int slot = 0; slot <= 2; slot++)
		{
			RK29_WeaponOption option;
			if (slot == 0 && chosenPrimary != ResourceName.Empty)
			{
				option = setup.FindWeapon(slots, chosenPrimary, base.m_sFactionKey);
			}
			if (!option)
				option = setup.DefaultWeapon(slots, slot);
			if (!option)
				continue;

			kit = ApplyWeaponOption(kit, option, slot, setup);
			applied = true;
		}

		// classes without options keep their composition's own weapons untouched
		if (!applied)
			kit = base.DeepCopy();

		ResolveAttachmentGates(kit);
		return kit;
	}

	//--------------------------------------------------------------------------------------------
	//! Drops attachments the FINAL primary cannot take, once every slot is settled. Deciding
	//! this at compose time was wrong for option classes: the base kit still carries the
	//! prefab's weapon, so a class offering one rifle with a lug and one without would answer
	//! for the wrong gun. Obstruction counts too - a mounted M203 rules out the bayonet.
	protected static void ResolveAttachmentGates(notnull RK29_KitStruct kit)
	{
		for (int i = kit.m_aItems.Count() - 1; i >= 0; i--)
		{
			RK29_KitItemBatch batch = kit.m_aItems[i];
			if (!batch || !batch.m_bPrimaryAttachment || batch.m_aPrefabs.IsEmpty())
				continue;

			ResourceName item = batch.m_aPrefabs[0];
			if (PrimaryTakesAttachment(kit.m_sPrimaryWeapon, item)
				&& !AttachmentObstructed(kit.m_sPrimaryWeapon, item))
				continue;

			Print("[RK29] " + kit.m_sKitName + ": " + FileOf(item) + " gated off - "
				+ FileOf(kit.m_sPrimaryWeapon) + " cannot mount it", LogLevel.NORMAL);
			kit.m_aItems.Remove(i);
		}
	}

	//--------------------------------------------------------------------------------------------
	//! Turns an ammo list into item batches for a weapon. Shared by both places a weapon can
	//! be declared - a composition's weapon entry and a class's weapon option - so there is
	//! one way to say "this is what feeds that".
	protected static void EmitAmmo(notnull RK29_KitStruct kit, ResourceName weapon, RK29_WeaponDef def,
		array<ref RK29_WeaponAmmo> ammoList, notnull RK29_KitSetup setup)
	{
		if (!ammoList)
			return;

		foreach (RK29_WeaponAmmo ammo : ammoList)
		{
			if (!ammo)
				continue;

			// literal, then the weapon's own ammo table, then the faction item catalog
			// (a shared role naming "gl_shell" gets M406 for the US and VOG-25 for the
			// Soviets), then a magazine variant, then the weapon's authored default
			ResourceName round;
			if (ammo.m_sPrefab != ResourceName.Empty)
				round = ammo.m_sPrefab;
			else if (ammo.m_sAlias != "" && def && DeclaresAmmo(def, ammo.m_sAlias))
				round = ResolveAmmo(def, weapon, ammo.m_sAlias, kit, setup);
			else if (ammo.m_sAlias != "")
			{
				round = setup.ResolveAlias(ammo.m_sAlias, kit.m_sFactionKey);
				if (round == ResourceName.Empty)
					Print("[RK29] config ERROR - ammo '" + ammo.m_sAlias + "' is neither declared by "
						+ FileOf(weapon) + " nor an item alias (" + kit.m_sKitName + ")", LogLevel.ERROR);
			}
			else if (ammo.m_sVariant != "")
			{
				round = setup.FindMagVariant(WellsOf(weapon), ammo.m_sVariant);
				if (round == ResourceName.Empty)
					Print("[RK29] config ERROR - ammo variant '" + ammo.m_sVariant + "' not defined for "
						+ FileOf(weapon) + " (" + kit.m_sKitName + ")", LogLevel.ERROR);
			}
			else
				round = DefaultMagOf(weapon);

			if (round == ResourceName.Empty)
				continue;

			int rounds = ammo.m_iCount;
			if (rounds < 1)
				rounds = 1;

			RK29_KitItemBatch batch = new RK29_KitItemBatch();

			// Without this every magazine in the mod reached the placement solver with no
			// opinion at all, which let the "start a stack where all of it fits" rule pull small
			// stacks into the biggest container - pistol magazines in the backpack while the
			// pockets sat empty. Declared on the weapon because a pistol magazine belongs in the
			// same place whichever of the nine kits is carrying the pistol.
			if (def && def.m_aPreferredContainers && !def.m_aPreferredContainers.IsEmpty())
				batch.m_aPreferred = def.m_aPreferredContainers;

			for (int i = 0; i < rounds; i++)
				batch.m_aPrefabs.Insert(round);
			kit.m_aItems.Insert(batch);
		}
	}

	//--------------------------------------------------------------------------------------------
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

	//--------------------------------------------------------------------------------------------
	//! An AMMO alias means whatever the WEAPON says it means: a magazine variant through the
	//! weapon's own magazine well, a literal prefab for ammo that is not a magazine, or the
	//! weapon's default magazine when the definition names neither.
	protected static ResourceName ResolveAmmo(RK29_WeaponDef def, ResourceName weapon, string alias,
		RK29_KitStruct kit, notnull RK29_KitSetup setup)
	{
		if (!def || !def.m_aAmmo)
		{
			Print("[RK29] config ERROR - AMMO \"" + alias + "\" but the weapon has no catalog entry ("
				+ kit.m_sKitName + ")", LogLevel.ERROR);
			return ResourceName.Empty;
		}

		foreach (RK29_WeaponAmmoDef ammo : def.m_aAmmo)
		{
			if (!ammo || ammo.m_sAlias != alias)
				continue;

			if (ammo.m_sPrefab != ResourceName.Empty)
				return ammo.m_sPrefab;
			if (ammo.m_sVariant != "")
			{
				ResourceName variant = setup.FindMagVariant(WellsOf(weapon), ammo.m_sVariant);
				if (variant == ResourceName.Empty)
					Print("[RK29] config ERROR - ammo variant '" + ammo.m_sVariant + "' not defined for "
						+ FileOf(weapon) + " (" + kit.m_sKitName + ")", LogLevel.ERROR);
				return variant;
			}
			return DefaultMagOf(weapon);
		}

		Print("[RK29] config ERROR - AMMO \"" + alias + "\" is not declared by " + def.m_sId
			+ " (" + kit.m_sKitName + ")", LogLevel.ERROR);
		return ResourceName.Empty;
	}

	//--------------------------------------------------------------------------------------------
	//! The entry's own placement list wins; otherwise the alias carries the default for
	//! this faction - the US flashlight has an ALICE mount, the Soviet one does not.
	protected static array<string> PreferenceFor(RK29_BlockItemEntry entry, RK29_KitStruct kit, RK29_KitSetup setup)
	{
		if (entry.m_aPreferredContainers && !entry.m_aPreferredContainers.IsEmpty())
			return entry.m_aPreferredContainers;
		if (entry.m_eSource == RK29_EItemSource.ALIAS && entry.m_sAlias != "")
			return setup.ResolveAliasPreference(entry.m_sAlias, kit.m_sFactionKey);
		return null;
	}


	//--------------------------------------------------------------------------------------------
	//! An alias resolves per faction, so one shared entry can dress both sides. An alias that
	//! does not resolve is a config error, NOT an instruction to strip the slot - only a
	//! literal empty prefab means "clear this".
	protected static ResourceName SlotPrefabOf(RK29_BlockClothingEntry c, RK29_KitStruct kit,
		notnull RK29_KitSetup setup, out bool clear)
	{
		clear = false;
		if (c.m_sAlias == "")
		{
			clear = c.m_sPrefab == ResourceName.Empty;
			return c.m_sPrefab;
		}

		ResourceName resolved = setup.ResolveAlias(c.m_sAlias, kit.m_sFactionKey);
		if (resolved == ResourceName.Empty)
			Print("[RK29] config ERROR - slot '" + c.m_sSlot + "' alias '" + c.m_sAlias
				+ "' does not resolve for faction " + kit.m_sFactionKey + " (" + kit.m_sKitName + ")",
				LogLevel.ERROR);
		return resolved;
	}

	//--------------------------------------------------------------------------------------------
	//! Slot-keyed later-wins over the prefab-captured dress; empty prefab clears the slot.
	protected static void ApplyClothingEntry(RK29_BlockClothingEntry c, RK29_KitStruct kit,
		notnull RK29_KitSetup setup)
	{
		if (!c || c.m_sSlot == "")
			return;

		bool clear;
		ResourceName prefab = SlotPrefabOf(c, kit, setup, clear);
		if (clear)
		{
			kit.m_mClothing.Remove(c.m_sSlot);
			return;
		}
		if (prefab == ResourceName.Empty)
			return;
		kit.m_mClothing.Set(c.m_sSlot, prefab);
	}

	//--------------------------------------------------------------------------------------------
	protected static void ApplyEquipmentEntry(RK29_BlockClothingEntry c, RK29_KitStruct kit,
		notnull RK29_KitSetup setup)
	{
		if (!c || c.m_sSlot == "")
			return;

		bool clear;
		ResourceName prefab = SlotPrefabOf(c, kit, setup, clear);
		if (clear)
		{
			kit.m_mEquipment.Remove(c.m_sSlot);
			return;
		}
		if (prefab == ResourceName.Empty)
			return;
		kit.m_mEquipment.Set(c.m_sSlot, prefab);
	}

	protected static ref map<ResourceName, ref array<string>> s_mWeaponAttachTypeCache = new map<ResourceName, ref array<string>>();
	protected static ref map<ResourceName, string> s_mItemAttachTypeCache = new map<ResourceName, string>();
	protected static ref map<ResourceName, ref array<string>> s_mObstructedCache = new map<ResourceName, ref array<string>>();
	protected static ref map<ResourceName, ref array<string>> s_mMountedCache = new map<ResourceName, ref array<string>>();
	protected static ref map<ResourceName, bool> s_mReadableCache = new map<ResourceName, bool>();

	//--------------------------------------------------------------------------------------------
	//! True when the weapon's prefab chain declares an attachment slot whose AttachmentType
	//! class matches the item's declared AttachmentType class (bayonet lug check and kin).
	static bool PrimaryTakesAttachment(ResourceName weapon, ResourceName item)
	{
		if (weapon == ResourceName.Empty || item == ResourceName.Empty)
			return false;

		string itemType;
		if (!s_mItemAttachTypeCache.Find(item, itemType))
		{
			array<string> types = {};
			CollectAttachmentTypes(item, types);
			if (types.IsEmpty())
				itemType = "";
			else
				itemType = types[0];
			s_mItemAttachTypeCache.Set(item, itemType);
		}
		if (itemType == "")
			return false;

		array<string> weaponTypes = s_mWeaponAttachTypeCache.Get(weapon);
		if (!weaponTypes)
		{
			weaponTypes = {};
			CollectAttachmentTypes(weapon, weaponTypes);
			s_mWeaponAttachTypeCache.Set(weapon, weaponTypes);
		}
		bool fits;
		foreach (string slot : weaponTypes)
		{
			if (MountFits(itemType, slot))
			{
				fits = true;
				break;
			}
		}
		if (!fits)
			return false;

		// the lug existing is not the same as the lug being usable
		return !AttachmentObstructed(weapon, item);
	}

	//--------------------------------------------------------------------------------------------
	//! True when the weapon is KNOWN not to take this attachment: the attachment declared
	//! mount types the weapon does not answer. A weapon that reads fine and declares no
	//! mounts at all takes nothing - the M249 and the M60 have neither an optic rail nor a
	//! bayonet lug. A prefab we cannot READ still answers false: an unreadable resource must
	//! never silently empty a picker column.
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

		foreach (string mount : attachTypes)
		{
			foreach (string slot : weaponTypes)
			{
				if (MountFits(mount, slot))
					return AttachmentObstructed(weapon, attachment);
			}
		}
		return true;
	}

	//--------------------------------------------------------------------------------------------
	//! A SLOT names the loosest mount it accepts; an ATTACHMENT names its own, which may be a
	//! subclass of that. Vanilla happens to spell the same leaf class on both sides, so string
	//! equality carried us until the first modded weapon: RHS types the M40's rail
	//! AttachmentOpticsRIS1913 and every scope that fits it AttachmentOpticsRIS1913Short. The
	//! engine mounts them fine - only this pre-filter disagreed - so ask the type system rather
	//! than compare spellings. Prefix matching would be wrong: AttachmentOpticsDovetailUK59
	//! reads like a DovetailAK sibling but inherits straight from AttachmentOptics.
	protected static bool MountFits(string attachType, string slotType)
	{
		if (attachType == slotType)
			return true;

		typename attach = attachType.ToType();
		typename slot   = slotType.ToType();
		if (!attach || !slot)
			return false;

		return attach.IsInherited(slot);
	}

	//--------------------------------------------------------------------------------------------
	//! True when the game's own data says this attachment cannot sit on the weapon AS BUILT.
	//! Attachments declare what blocks them - Bayonet_M9 lists AttachmentUnderBarrelM203 and
	//! AttachmentUnderBarrelM203Carbine, Bayonet_6Kh4 lists AttachmentUnderBarrelGP25 - so a
	//! grenadier's rifle rules its own bayonet out and nobody maintains a list of exceptions.
	static bool AttachmentObstructed(ResourceName weapon, ResourceName attachment)
	{
		array<string> obstructedBy = s_mObstructedCache.Get(attachment);
		if (!obstructedBy)
		{
			obstructedBy = {};
			Resource res = Resource.Load(attachment);
			if (res.IsValid())
			{
				IEntitySource src = res.GetResource().ToEntitySource();
				if (src)
				{
					for (int i = 0, n = src.GetComponentCount(); i < n; i++)
						WalkObstructedTypes(src.GetComponent(i), obstructedBy, 0);
				}
			}
			s_mObstructedCache.Set(attachment, obstructedBy);
		}
		if (obstructedBy.IsEmpty())
			return false;

		array<string> mounted = MountedTypesOf(weapon);
		foreach (string blocker : obstructedBy)
		{
			if (mounted.Contains(blocker))
				return true;
		}
		return false;
	}

	//--------------------------------------------------------------------------------------------
	//! Types of what is actually SEATED on the weapon by its prefab, not the slots it merely
	//! offers. An AK-74N declares a GP-25 slot standing empty; the grenadier's rifle declares
	//! the same slot with a launcher in it. Only the second obstructs anything.
	protected static array<string> MountedTypesOf(ResourceName weapon)
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

	//--------------------------------------------------------------------------------------------
	protected static void WalkObstructedTypes(BaseContainer c, notnull array<string> outTypes, int depth)
	{
		if (!c || depth > 10)
			return;

		BaseContainerList blockers = c.GetObjectArray("m_aObstructedAttachmentTypes");
		if (blockers)
		{
			for (int b = 0, nb = blockers.Count(); b < nb; b++)
			{
				BaseContainer blocker = blockers.Get(b);
				if (blocker)
					outTypes.Insert(blocker.GetClassName());
			}
		}

		for (int i = 0, n = c.GetNumVars(); i < n; i++)
		{
			string varName = c.GetVarName(i);
			if (varName == "m_aObstructedAttachmentTypes")
				continue;
			BaseContainer sub = c.GetObject(varName);
			if (sub)
				WalkObstructedTypes(sub, outTypes, depth + 1);
			BaseContainerList list = c.GetObjectArray(varName);
			if (!list)
				continue;
			for (int j = 0, m = list.Count(); j < m; j++)
				WalkObstructedTypes(list.Get(j), outTypes, depth + 1);
		}
	}

	//--------------------------------------------------------------------------------------------
	protected static void WalkMountedTypes(BaseContainer c, notnull array<string> outTypes, int depth)
	{
		if (!c || depth > 10)
			return;

		ResourceName seated;
		if (c.Get("Prefab", seated) && seated != ResourceName.Empty)
		{
			array<string> seatedTypes = AttachTypesOf(seated);
			foreach (string t : seatedTypes)
				outTypes.Insert(t);
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

	//--------------------------------------------------------------------------------------------
	//! A weapon's slots and an attachment's own mount are the same AttachmentType classes,
	//! which is what makes intersecting the two sides meaningful.
	protected static array<string> AttachTypesOf(ResourceName prefab)
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

	//--------------------------------------------------------------------------------------------
	//! Whether the prefab loads at all, so "declares no mounts" can be told apart from
	//! "could not be read". Cached: the answer cannot change inside a session.
	protected static bool PrefabReadable(ResourceName prefab)
	{
		bool readable;
		if (s_mReadableCache.Find(prefab, readable))
			return readable;

		readable = false;
		Resource res = Resource.Load(prefab);
		if (res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			readable = src != null;
		}

		s_mReadableCache.Set(prefab, readable);
		return readable;
	}

	//--------------------------------------------------------------------------------------------
	//! All AttachmentType object classes in a prefab's merged container tree.
	protected static void CollectAttachmentTypes(ResourceName prefab, notnull array<string> outTypes)
	{
		Resource res = Resource.Load(prefab);
		if (!res.IsValid())
			return;
		IEntitySource src = res.GetResource().ToEntitySource();
		if (!src)
			return;
		for (int i = 0, n = src.GetComponentCount(); i < n; i++)
			WalkAttachmentTypes(src.GetComponent(i), outTypes, 0);
	}

	//--------------------------------------------------------------------------------------------
	protected static void WalkAttachmentTypes(BaseContainer c, notnull array<string> outTypes, int depth)
	{
		if (!c || depth > 10)
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

	//--------------------------------------------------------------------------------------------
	protected static RK29_KitComposition LoadComposition(ResourceName res)
	{
		if (res == ResourceName.Empty)
			return null;
		Resource r = Resource.Load(res);
		if (!r.IsValid())
		{
			Print("[RK29] config ERROR - composition not found: " + res, LogLevel.ERROR);
			return null;
		}
		RK29_KitComposition comp = RK29_KitComposition.Cast(BaseContainerTools.CreateInstanceFromContainer(r.GetResource().ToBaseContainer()));
		if (!comp)
			Print("[RK29] config ERROR - not an RK29_KitComposition: " + res, LogLevel.ERROR);
		return comp;
	}

	//--------------------------------------------------------------------------------------------
	protected static RK29_KitBlock LoadBlock(RK29_BlockRef bref)
	{
		if (!bref || bref.m_sBlock == ResourceName.Empty)
			return null;
		Resource res = Resource.Load(bref.m_sBlock);
		if (!res.IsValid())
		{
			Print("[RK29] config ERROR - block not found: " + bref.m_sBlock, LogLevel.ERROR);
			return null;
		}
		RK29_KitBlock block = RK29_KitBlock.Cast(BaseContainerTools.CreateInstanceFromContainer(res.GetResource().ToBaseContainer()));
		if (!block)
			Print("[RK29] config ERROR - not an RK29_KitBlock: " + bref.m_sBlock, LogLevel.ERROR);
		return block;
	}

	//--------------------------------------------------------------------------------------------
	protected static ResourceName ResolveEntry(RK29_BlockItemEntry entry, RK29_KitStruct kit, RK29_KitSetup setup)
	{
		if (entry.m_eSource == RK29_EItemSource.PREFAB)
		{
			if (entry.m_sPrefab == ResourceName.Empty)
				Print("[RK29] config ERROR - PREFAB entry with no prefab (" + kit.m_sKitName + ")", LogLevel.ERROR);
			return entry.m_sPrefab;
		}

		if (entry.m_eSource == RK29_EItemSource.ALIAS)
		{
			ResourceName resolved = setup.ResolveAlias(entry.m_sAlias, kit.m_sFactionKey);
			if (resolved == ResourceName.Empty)
				Print("[RK29] config ERROR - alias '" + entry.m_sAlias + "' unresolved for " + kit.m_sFactionKey + " (" + kit.m_sKitName + ")", LogLevel.ERROR);
			return resolved;
		}

		Print("[RK29] config ERROR - item entry with no prefab or alias (" + kit.m_sKitName + ")", LogLevel.ERROR);
		return ResourceName.Empty;
	}


	//--------------------------------------------------------------------------------------------
	//! Literal-identity override: same source plus same alias/prefab. -1 = none.
	protected static int OverrideCountFor(RK29_BlockRef bref, RK29_BlockItemEntry entry)
	{
		if (!bref.m_aItemOverrides)
			return -1;
		foreach (RK29_BlockItemEntry ov : bref.m_aItemOverrides)
		{
			if (!ov || ov.m_eSource != entry.m_eSource)
				continue;
			if (entry.m_eSource == RK29_EItemSource.PREFAB && ov.m_sPrefab != entry.m_sPrefab)
				continue;
			if (entry.m_eSource == RK29_EItemSource.ALIAS && ov.m_sAlias != entry.m_sAlias)
				continue;
			return ov.m_iCount;
		}
		return -1;
	}

	//--------------------------------------------------------------------------------------------
	//! MagazineWell class names on a weapon prefab (all muzzles), cached.
	static array<string> WellsOf(ResourceName weapon)
	{
		array<string> wells;
		if (s_mWellsCache.Find(weapon, wells))
			return wells;

		wells = {};
		Resource res = Resource.Load(weapon);
		if (res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			if (src)
			{
				for (int i = 0, n = src.GetComponentCount(); i < n; i++)
					CollectWells(src.GetComponent(i), wells, 0);
			}
		}
		s_mWellsCache.Set(weapon, wells);
		return wells;
	}

	//--------------------------------------------------------------------------------------------
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

	//--------------------------------------------------------------------------------------------
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

	//--------------------------------------------------------------------------------------------
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

	//--------------------------------------------------------------------------------------------
	protected static string FileOf(ResourceName res)
	{
		string raw = "" + res;
		int slash = raw.LastIndexOf("/");
		if (slash >= 0)
			raw = raw.Substring(slash + 1, raw.Length() - slash - 1);
		return raw;
	}

	// ============================================================================== tooling

	//--------------------------------------------------------------------------------------------
	static void Digest(string kitName)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return;

		RK29_KitStruct kit = mgr.m_mKits.Get(kitName);
		if (!kit)
		{
			Print("[RK29] digest - unknown kit '" + kitName + "'", LogLevel.WARNING);
			return;
		}

		Print("[RK29] === digest '" + kitName + "' (" + kit.m_sFactionKey + ") ===", LogLevel.NORMAL);
		Print("[RK29] primary: " + FileOf(kit.m_sPrimaryWeapon), LogLevel.NORMAL);
		foreach (int idx, ResourceName weapon : kit.m_mWeapons)
			Print(string.Format("[RK29]  weapon slot %1: %2", idx, FileOf(weapon)), LogLevel.NORMAL);
		foreach (string slot, ResourceName garment : kit.m_mClothing)
			Print(string.Format("[RK29]  clothing %1: %2", slot, FileOf(garment)), LogLevel.NORMAL);
		foreach (RK29_ETrait trait : kit.m_aTraits)
			Print("[RK29]  trait: " + RK29_Traits.NameOf(trait), LogLevel.NORMAL);

		map<string, int> totals = new map<string, int>();
		CountItems(kit, totals);
		foreach (string file, int n : totals)
			Print(string.Format("[RK29]  %1x %2", n, file), LogLevel.NORMAL);

		// which alias produced what, chain and all - an item that arrived through
		// backpack_ce -> backpack_medium is otherwise indistinguishable from a literal
		if (mgr.m_Setup && mgr.m_Setup.m_aAliases)
		{
			foreach (RK29_ItemAlias alias : mgr.m_Setup.m_aAliases)
			{
				if (!alias)
					continue;
				ResourceName resolved = mgr.m_Setup.ResolveAlias(alias.m_sAlias, kit.m_sFactionKey);
				if (resolved == ResourceName.Empty)
					continue;
				int have;
				if (!totals.Find(FileOf(resolved), have))
					continue;

				array<string> chain = {};
				mgr.m_Setup.AliasChain(alias.m_sAlias, chain);
				string path;
				foreach (string link : chain)
				{
					if (path != string.Empty)
						path += " -> ";
					path += link;
				}
				Print(string.Format("[RK29]  alias %1 = %2", path, FileOf(resolved)), LogLevel.NORMAL);
			}
		}

		Print("[RK29] === " + kit.CountItems().ToString() + " item(s) total ===", LogLevel.NORMAL);
	}

	//--------------------------------------------------------------------------------------------
	protected static void CountItems(RK29_KitStruct kit, notnull map<string, int> totals)
	{
		foreach (RK29_KitItemBatch batch : kit.m_aItems)
		{
			foreach (ResourceName p : batch.m_aPrefabs)
			{
				string file = FileOf(p);
				int n;
				totals.Find(file, n);
				totals.Set(file, n + 1);
			}
		}
	}



}
