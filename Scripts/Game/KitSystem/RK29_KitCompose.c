//------------------------------------------------------------------------------------------------
//! Builds RK29_KitStruct from config blocks. Dress and identity come from the captured
//! prefab kit; weapons overlay it slot-wise; items come from blocks only.
//! Also home of the /kitdigest /kitdump /kitcompare tooling.
//------------------------------------------------------------------------------------------------
class RK29_KitCompose
{
	protected static const int LAUNCHER_SLOT = 1;
	protected static const int SIDEARM_SLOT = 2;
	protected static const string COMPARE_REPORT = "$profile:RK29_KitCompare.txt";

	protected static ref map<ResourceName, ResourceName> s_mDefaultMagCache = new map<ResourceName, ResourceName>();
	protected static ref map<ResourceName, ref array<string>> s_mWellsCache = new map<ResourceName, ref array<string>>();

	//--------------------------------------------------------------------------------------------
	static RK29_KitStruct Compose(notnull RK29_ClassSetup cls, notnull RK29_KitStruct captured, notnull RK29_KitSetup setup)
	{
		RK29_KitComposition comp = LoadComposition(cls.m_sComposition);
		if (!comp)
			return null;

		RK29_KitStruct kit = new RK29_KitStruct();
		kit.m_sKitName      = captured.m_sKitName;
		kit.m_sFactionKey   = captured.m_sFactionKey;
		kit.m_sSourcePrefab = captured.m_sSourcePrefab;
		kit.m_UIInfo        = captured.m_UIInfo;

		foreach (string slot, ResourceName garment : captured.m_mClothing)
			kit.m_mClothing.Set(slot, garment);
		foreach (string eqSlot, ResourceName eqItem : captured.m_mEquipment)
			kit.m_mEquipment.Set(eqSlot, eqItem);
		foreach (int idx, ResourceName weapon : captured.m_mWeapons)
		{
			// NOT the throwable slot: a config kit counts its grenades in m_aItems, and apply
			// promotes one of those into the slot. Carrying the prefab's slot grenade over as
			// well spawns it a second time - one extra grenade or smoke on every kit.
			if (idx == RK29_KitStruct.GRENADE_SLOT)
				continue;
			kit.m_mWeapons.Set(idx, weapon);
		}

		// pass 1: weapons + clothing (blocks, then the composition's own, later-wins),
		// so PRIMARY_MAG can resolve off the final primary
		if (comp.m_aBlocks)
		{
			foreach (RK29_BlockRef bref : comp.m_aBlocks)
			{
				RK29_KitBlock block = LoadBlock(bref);
				if (!block)
					continue;

				if (block.m_aWeapons)
				{
					foreach (RK29_BlockWeaponEntry w : block.m_aWeapons)
						ApplyWeaponEntry(w, kit, setup);
				}

				if (block.m_aClothing)
				{
					foreach (RK29_BlockClothingEntry bc : block.m_aClothing)
						ApplyClothingEntry(bc, kit);
				}

				if (block.m_aEquipment)
				{
					foreach (RK29_BlockClothingEntry be : block.m_aEquipment)
						ApplyEquipmentEntry(be, kit);
				}
			}
		}
		if (comp.m_aWeapons)
		{
			foreach (RK29_BlockWeaponEntry cw : comp.m_aWeapons)
				ApplyWeaponEntry(cw, kit, setup);
		}
		if (comp.m_aClothing)
		{
			foreach (RK29_BlockClothingEntry cc : comp.m_aClothing)
				ApplyClothingEntry(cc, kit);
		}
		if (comp.m_aEquipment)
		{
			foreach (RK29_BlockClothingEntry ce : comp.m_aEquipment)
				ApplyEquipmentEntry(ce, kit);
		}

		ResourceName primary;
		if (!kit.m_mWeapons.Find(0, primary))
			primary = ResourceName.Empty;
		kit.m_sPrimaryWeapon = primary;

		// explicit weapon-option mag wins for the plain primary token; weapons' own
		// authored defaults otherwise
		ResourceName primaryMag;
		RK29_WeaponOption wo = setup.FindWeapon(cls, primary);
		if (wo)
			primaryMag = wo.m_sMagazinePrefab;
		if (primaryMag == ResourceName.Empty)
			primaryMag = DefaultMagOf(primary);

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

				ResourceName prefab = ResolveEntry(entry, kit, primaryMag, setup);
				if (prefab == ResourceName.Empty)
					continue;
				if (entry.m_bOnlyIfPrimaryTakesIt && !PrimaryTakesAttachment(kit.m_sPrimaryWeapon, prefab))
				{
					Print("[RK29] " + kit.m_sKitName + ": " + FileOf(prefab) + " gated off - primary has no matching attachment slot", LogLevel.NORMAL);
					continue;
				}

				RK29_KitItemBatch batch = new RK29_KitItemBatch();
				batch.m_sTargetHint = entry.m_sTargetHint;
				batch.m_aPreferred = PreferenceFor(entry, kit, setup);
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
				ResourceName ownPrefab = ResolveEntry(own, kit, primaryMag, setup);
				if (ownPrefab == ResourceName.Empty)
					continue;
				if (own.m_bOnlyIfPrimaryTakesIt && !PrimaryTakesAttachment(kit.m_sPrimaryWeapon, ownPrefab))
				{
					Print("[RK29] " + kit.m_sKitName + ": " + FileOf(ownPrefab) + " gated off - primary has no matching attachment slot", LogLevel.NORMAL);
					continue;
				}
				RK29_KitItemBatch ownBatch = new RK29_KitItemBatch();
				ownBatch.m_sTargetHint = own.m_sTargetHint;
				ownBatch.m_aPreferred = PreferenceFor(own, kit, setup);
				for (int i = 0; i < ownCount; i++)
					ownBatch.m_aPrefabs.Insert(ownPrefab);
				kit.m_aItems.Insert(ownBatch);
			}
		}

		return kit;
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
	//! Prefab or faction alias; both empty clears the slot (kits without a sidearm etc.)
	protected static void ApplyWeaponEntry(RK29_BlockWeaponEntry w, RK29_KitStruct kit, RK29_KitSetup setup)
	{
		if (!w)
			return;

		ResourceName prefab = w.m_sPrefab;
		if (prefab == ResourceName.Empty && w.m_sAlias != "")
		{
			prefab = setup.ResolveAlias(w.m_sAlias, kit.m_sFactionKey);
			if (prefab == ResourceName.Empty)
			{
				Print("[RK29] config ERROR - weapon alias '" + w.m_sAlias + "' unresolved for " + kit.m_sFactionKey + " (" + kit.m_sKitName + ")", LogLevel.ERROR);
				return;
			}
		}

		if (prefab == ResourceName.Empty)
		{
			kit.m_mWeapons.Remove(w.m_iSlotIndex);
			return;
		}
		kit.m_mWeapons.Set(w.m_iSlotIndex, prefab);
	}

	//--------------------------------------------------------------------------------------------
	//! Slot-keyed later-wins over the prefab-captured dress; empty prefab clears the slot.
	protected static void ApplyClothingEntry(RK29_BlockClothingEntry c, RK29_KitStruct kit)
	{
		if (!c || c.m_sSlot == "")
			return;

		if (c.m_sPrefab == ResourceName.Empty)
		{
			kit.m_mClothing.Remove(c.m_sSlot);
			return;
		}
		kit.m_mClothing.Set(c.m_sSlot, c.m_sPrefab);
	}

	//--------------------------------------------------------------------------------------------
	protected static void ApplyEquipmentEntry(RK29_BlockClothingEntry c, RK29_KitStruct kit)
	{
		if (!c || c.m_sSlot == "")
			return;

		if (c.m_sPrefab == ResourceName.Empty)
		{
			kit.m_mEquipment.Remove(c.m_sSlot);
			return;
		}
		kit.m_mEquipment.Set(c.m_sSlot, c.m_sPrefab);
	}

	protected static ref map<ResourceName, ref array<string>> s_mWeaponAttachTypeCache = new map<ResourceName, ref array<string>>();
	protected static ref map<ResourceName, string> s_mItemAttachTypeCache = new map<ResourceName, string>();
	protected static ref map<ResourceName, ref array<string>> s_mObstructedCache = new map<ResourceName, ref array<string>>();
	protected static ref map<ResourceName, ref array<string>> s_mMountedCache = new map<ResourceName, ref array<string>>();

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
		if (!weaponTypes.Contains(itemType))
			return false;

		// the lug existing is not the same as the lug being usable
		return !AttachmentObstructed(weapon, item);
	}

	//--------------------------------------------------------------------------------------------
	//! True when the weapon is KNOWN not to take this attachment: both sides declared mount
	//! types and they share none. A prefab we cannot read answers false - an unreadable
	//! resource must never silently empty a picker column.
	static bool WeaponRejectsAttachment(ResourceName weapon, ResourceName attachment)
	{
		if (weapon == ResourceName.Empty || attachment == ResourceName.Empty)
			return false;

		array<string> attachTypes = AttachTypesOf(attachment);
		if (attachTypes.IsEmpty())
			return false;
		array<string> weaponTypes = AttachTypesOf(weapon);
		if (weaponTypes.IsEmpty())
			return false;

		foreach (string mount : attachTypes)
		{
			if (weaponTypes.Contains(mount))
				return AttachmentObstructed(weapon, attachment);
		}
		return true;
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
	protected static ResourceName ResolveEntry(RK29_BlockItemEntry entry, RK29_KitStruct kit, ResourceName primaryMag, RK29_KitSetup setup)
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

		return ResolveMag(entry.m_eSource, entry.m_sVariant, kit, primaryMag, setup);
	}

	//--------------------------------------------------------------------------------------------
	protected static ResourceName ResolveMag(RK29_EItemSource source, string variant, RK29_KitStruct kit, ResourceName primaryMag, RK29_KitSetup setup)
	{
		int slot = 0;
		if (source == RK29_EItemSource.MAG_LAUNCHER)
			slot = LAUNCHER_SLOT;
		else if (source == RK29_EItemSource.MAG_SIDEARM)
			slot = SIDEARM_SLOT;

		string tokenName = typename.EnumToString(RK29_EItemSource, source);

		ResourceName weapon;
		kit.m_mWeapons.Find(slot, weapon);
		if (weapon == ResourceName.Empty)
		{
			Print("[RK29] config ERROR - " + tokenName + " but no weapon in slot " + slot.ToString() + " (" + kit.m_sKitName + ")", LogLevel.ERROR);
			return ResourceName.Empty;
		}

		if (variant == "")
		{
			ResourceName mag;
			if (slot == 0)
				mag = primaryMag;
			if (mag == ResourceName.Empty)
				mag = DefaultMagOf(weapon);
			if (mag == ResourceName.Empty)
				Print("[RK29] config ERROR - " + tokenName + " but no magazine resolvable (" + kit.m_sKitName + ")", LogLevel.ERROR);
			return mag;
		}

		ResourceName v = setup.FindMagVariant(WellsOf(weapon), variant);
		if (v == ResourceName.Empty)
			Print("[RK29] config ERROR - mag variant '" + variant + "' not defined for " + FileOf(weapon) + " (" + kit.m_sKitName + ")", LogLevel.ERROR);
		return v;
	}

	//--------------------------------------------------------------------------------------------
	//! Literal-identity override: same source plus same alias/variant/prefab. -1 = none.
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
			if (entry.m_eSource != RK29_EItemSource.PREFAB && entry.m_eSource != RK29_EItemSource.ALIAS
				&& ov.m_sVariant != entry.m_sVariant)
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

		map<string, int> totals = new map<string, int>();
		CountItems(kit, totals);
		foreach (string file, int n : totals)
			Print(string.Format("[RK29]  %1x %2", n, file), LogLevel.NORMAL);
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

	//--------------------------------------------------------------------------------------------
	//! Writes every kit's contents as an RK29_KitBlock conf to $profile:RK29_KitDump/.
	static void Dump()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return;

		FileIO.MakeDirectory("$profile:RK29_KitDump");
		int written = 0;
		foreach (string kitName, RK29_KitStruct composedOrCaptured : mgr.m_mKits)
		{
			// ALWAYS dump prefab truth. m_mKits holds the COMPOSED kit for any class wired
			// to a composition, so dumping that would export our own config back at us -
			// and a regeneration from it would launder every normalization in as if the
			// prefab had authored it.
			RK29_KitStruct kit = mgr.m_mCaptured.Get(kitName);
			if (!kit)
				kit = composedOrCaptured;

			string safe = kitName;
			safe.Replace(" ", "_");
			safe.Replace("-", "");
			FileHandle fh = FileIO.OpenFile("$profile:RK29_KitDump/" + safe + ".conf", FileMode.WRITE);
			if (!fh)
				continue;

			fh.WriteLine("RK29_KitBlock {");
			fh.WriteLine(" m_aClothing {");
			foreach (string clothSlot, ResourceName garment : kit.m_mClothing)
			{
				fh.WriteLine("  RK29_BlockClothingEntry {");
				fh.WriteLine("   m_sSlot \"" + clothSlot + "\"");
				fh.WriteLine("   m_sPrefab \"" + garment + "\"");
				fh.WriteLine("  }");
			}
			fh.WriteLine(" }");
			fh.WriteLine(" m_aEquipment {");
			foreach (string eqSlot, ResourceName eqItem : kit.m_mEquipment)
			{
				fh.WriteLine("  RK29_BlockClothingEntry {");
				fh.WriteLine("   m_sSlot \"" + eqSlot + "\"");
				fh.WriteLine("   m_sPrefab \"" + eqItem + "\"");
				fh.WriteLine("  }");
			}
			fh.WriteLine(" }");
			fh.WriteLine(" m_aWeapons {");
			foreach (int idx, ResourceName weapon : kit.m_mWeapons)
			{
				fh.WriteLine("  RK29_BlockWeaponEntry {");
				fh.WriteLine("   m_iSlotIndex " + idx.ToString());
				fh.WriteLine("   m_sPrefab \"" + weapon + "\"");
				fh.WriteLine("  }");
			}
			fh.WriteLine(" }");
			fh.WriteLine(" m_aItems {");

			// aggregate identical prefab+hint pairs into counted entries
			array<string> keys = {};
			array<ResourceName> prefabs = {};
			array<string> hints = {};
			array<int> counts = {};
			foreach (RK29_KitItemBatch batch : kit.m_aItems)
			{
				foreach (ResourceName p : batch.m_aPrefabs)
				{
					string key = "" + p + "|" + batch.m_sTargetHint;
					int at = keys.Find(key);
					if (at == -1)
					{
						keys.Insert(key);
						prefabs.Insert(p);
						hints.Insert(batch.m_sTargetHint);
						counts.Insert(1);
					}
					else
						counts[at] = counts[at] + 1;
				}
			}
			for (int i = 0, n = keys.Count(); i < n; i++)
			{
				fh.WriteLine("  RK29_BlockItemEntry {");
				fh.WriteLine("   m_sPrefab \"" + prefabs[i] + "\"");
				if (counts[i] != 1)
					fh.WriteLine("   m_iCount " + counts[i].ToString());
				if (hints[i] != "")
					fh.WriteLine("   m_sTargetHint \"" + hints[i] + "\"");
				fh.WriteLine("  }");
			}
			fh.WriteLine(" }");
			fh.WriteLine("}");
			fh.Close();
			written++;
		}
		Print("[RK29] kit dump - " + written.ToString() + " file(s) in $profile:RK29_KitDump/", LogLevel.NORMAL);
	}

	//--------------------------------------------------------------------------------------------
	//! Shallow copy: maps and the batch list are rebuilt, the batches themselves are shared.
	//! Enough for the compare path, which only ever moves entries between the two.
	protected static RK29_KitStruct CopyForCompare(notnull RK29_KitStruct src)
	{
		RK29_KitStruct copy = new RK29_KitStruct();
		copy.m_sKitName      = src.m_sKitName;
		copy.m_sFactionKey   = src.m_sFactionKey;
		copy.m_sSourcePrefab = src.m_sSourcePrefab;
		copy.m_sPrimaryWeapon = src.m_sPrimaryWeapon;
		copy.m_UIInfo        = src.m_UIInfo;

		foreach (string slot, ResourceName garment : src.m_mClothing)
			copy.m_mClothing.Set(slot, garment);
		foreach (string eqSlot, ResourceName eqItem : src.m_mEquipment)
			copy.m_mEquipment.Set(eqSlot, eqItem);
		foreach (int idx, ResourceName weapon : src.m_mWeapons)
			copy.m_mWeapons.Set(idx, weapon);
		foreach (RK29_KitItemBatch batch : src.m_aItems)
			copy.m_aItems.Insert(batch);

		return copy;
	}

	//--------------------------------------------------------------------------------------------
	protected static void NormalizeGrenadeSlot(RK29_KitStruct kit)
	{
		ResourceName grenade;
		if (!kit.m_mWeapons.Find(RK29_KitStruct.GRENADE_SLOT, grenade))
			return;
		kit.m_mWeapons.Remove(RK29_KitStruct.GRENADE_SLOT);
		RK29_KitItemBatch batch = new RK29_KitItemBatch();
		batch.m_aPrefabs.Insert(grenade);
		kit.m_aItems.Insert(batch);
	}

	//--------------------------------------------------------------------------------------------
	//! /kitcompare with no argument. Sweeping every configured kit is the useful default: a kit
	//! author edits one block and wants to know that nothing ELSE moved.
	static void Compare(string kitName)
	{
		kitName.TrimInPlace();
		if (kitName != string.Empty)
		{
			CompareOne(kitName);
			return;
		}

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr || !mgr.m_Setup || !mgr.m_Setup.m_aClasses)
			return;

		array<string> report = {};
		int examined = 0;
		int differing = 0;

		foreach (RK29_ClassSetup cls : mgr.m_Setup.m_aClasses)
		{
			if (!cls || cls.m_sComposition == ResourceName.Empty)
				continue;

			examined++;
			int diffs = CompareOne(cls.m_sKitName);
			if (diffs > 0)
			{
				differing++;
				report.Insert(string.Format("%1  %2 difference(s)", cls.m_sKitName, diffs));
			}
			else if (diffs < 0)
			{
				differing++;
				report.Insert(string.Format("%1  compare failed", cls.m_sKitName));
			}
			else
			{
				report.Insert(string.Format("%1  EQUAL", cls.m_sKitName));
			}
		}

		string summary;
		if (differing == 0)
			summary = string.Format("all %1 kit(s) match their prefabs", examined);
		else
			summary = string.Format("%1 of %2 kit(s) differ", differing, examined);
		Print("[RK29] ===== kit compare: " + summary + " =====", LogLevel.NORMAL);

		FileHandle fh = FileIO.OpenFile(COMPARE_REPORT, FileMode.WRITE);
		if (!fh)
			return;
		fh.WriteLine("29th kit compare - " + summary);
		fh.WriteLine("");
		foreach (string line : report)
			fh.WriteLine(line);
		fh.Close();
		Print("[RK29] report written to " + COMPARE_REPORT, LogLevel.NORMAL);
	}

	//--------------------------------------------------------------------------------------------
	//! Composed-vs-captured diff. EQUAL when config reproduces the prefab byte-for-byte.
	//! Returns the difference count, or -1 if the kit could not be compared at all.
	protected static int CompareOne(string kitName)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr || !mgr.m_Setup)
			return -1;

		RK29_ClassSetup cls = mgr.m_Setup.FindClass(kitName);
		if (!cls || cls.m_sComposition == ResourceName.Empty)
		{
			Print("[RK29] compare - '" + kitName + "' has no composition configured", LogLevel.WARNING);
			return -1;
		}

		// the boot capture, not a re-resolve: RK29_CaptureFresh reaches the loadout through the
		// registry and can hand back parent-only content when a resource is stale in the editor
		RK29_KitStruct stored = mgr.m_mCaptured.Get(kitName);
		if (!stored)
			stored = mgr.RK29_CaptureFresh(kitName);
		if (!stored)
		{
			Print("[RK29] compare - capture failed for '" + kitName + "'", LogLevel.WARNING);
			return -1;
		}

		// the grenade fold below rewrites the struct it is handed, and the boot capture is
		// shared with /kitdump - compare against a copy so a compare never edits the evidence
		RK29_KitStruct captured = CopyForCompare(stored);
		RK29_KitStruct composed = Compose(cls, captured, mgr.m_Setup);
		if (!composed)
			return -1;

		// config kits carry grenades as items only - fold slot grenades into items so
		// the deliberate divergence never reads as a diff
		NormalizeGrenadeSlot(captured);
		NormalizeGrenadeSlot(composed);

		int diffs = 0;
		foreach (int idx, ResourceName weapon : captured.m_mWeapons)
		{
			ResourceName other;
			composed.m_mWeapons.Find(idx, other);
			if (other != weapon)
			{
				Print(string.Format("[RK29]  DIFF weapon slot %1: captured %2 vs composed %3", idx, FileOf(weapon), FileOf(other)), LogLevel.WARNING);
				diffs++;
			}
		}
		foreach (int idx2, ResourceName weapon2 : composed.m_mWeapons)
		{
			if (!captured.m_mWeapons.Contains(idx2))
			{
				Print(string.Format("[RK29]  DIFF weapon slot %1: only in composed (%2)", idx2, FileOf(weapon2)), LogLevel.WARNING);
				diffs++;
			}
		}

		map<string, int> capItems = new map<string, int>();
		map<string, int> comItems = new map<string, int>();
		CountItems(captured, capItems);
		CountItems(composed, comItems);
		foreach (string file, int n : capItems)
		{
			int m;
			comItems.Find(file, m);
			if (m != n)
			{
				Print(string.Format("[RK29]  DIFF item %1: captured %2 vs composed %3", file, n, m), LogLevel.WARNING);
				diffs++;
			}
		}
		foreach (string file2, int n2 : comItems)
		{
			if (!capItems.Contains(file2))
			{
				Print(string.Format("[RK29]  DIFF item %1: only in composed (%2)", file2, n2), LogLevel.WARNING);
				diffs++;
			}
		}

		foreach (string clothSlot, ResourceName garment : captured.m_mClothing)
		{
			ResourceName otherGarment;
			composed.m_mClothing.Find(clothSlot, otherGarment);
			if (otherGarment != garment)
			{
				Print(string.Format("[RK29]  DIFF clothing %1: captured %2 vs composed %3", clothSlot, FileOf(garment), FileOf(otherGarment)), LogLevel.WARNING);
				diffs++;
			}
		}
		foreach (string clothSlot2, ResourceName garment2 : composed.m_mClothing)
		{
			if (!captured.m_mClothing.Contains(clothSlot2))
			{
				Print(string.Format("[RK29]  DIFF clothing %1: only in composed (%2)", clothSlot2, FileOf(garment2)), LogLevel.WARNING);
				diffs++;
			}
		}
		foreach (string eqSlot, ResourceName eqItem : captured.m_mEquipment)
		{
			ResourceName otherEq;
			composed.m_mEquipment.Find(eqSlot, otherEq);
			if (otherEq != eqItem)
			{
				Print(string.Format("[RK29]  DIFF equipment %1: captured %2 vs composed %3", eqSlot, FileOf(eqItem), FileOf(otherEq)), LogLevel.WARNING);
				diffs++;
			}
		}
		foreach (string eqSlot2, ResourceName eqItem2 : composed.m_mEquipment)
		{
			if (!captured.m_mEquipment.Contains(eqSlot2))
			{
				Print(string.Format("[RK29]  DIFF equipment %1: only in composed (%2)", eqSlot2, FileOf(eqItem2)), LogLevel.WARNING);
				diffs++;
			}
		}

		if (diffs == 0)
			Print("[RK29] compare '" + kitName + "': EQUAL", LogLevel.NORMAL);
		else
			Print("[RK29] compare '" + kitName + "': " + diffs.ToString() + " difference(s)", LogLevel.WARNING);

		return diffs;
	}
}
