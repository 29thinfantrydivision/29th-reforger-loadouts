//------------------------------------------------------------------------------------------------
//! In-place strip and re-dress of a living character. Server only. Fails soft per item.
//------------------------------------------------------------------------------------------------

class RK29_SpawnCallback : SCR_AITakeItemFromArsenal_InventoryCallback
{
	IEntity RK29_GetSpawned()
	{
		return GetEntity();
	}
}

//------------------------------------------------------------------------------------------------
class RK29_KitApply
{
	//--------------------------------------------------------------------------------------------
	//! Re-dress character as kit, then attach mounts + optic (empty = irons). False only on
	//! hard failure. `mounts` seats before the optic; weapon-variant optics never reach here
	//! (the manager swaps the weapon instead).
	static bool Apply(notnull IEntity character, notnull RK29_KitStruct kit, ResourceName optic, array<ResourceName> mounts, out int droppedItems)
	{
		droppedItems = 0;
		if (!Replication.IsServer())
			return false;

		SCR_InventoryStorageManagerComponent manager = SCR_InventoryStorageManagerComponent.Cast(
			character.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!manager)
		{
			Print("[RK29] apply FAILED - no inventory storage manager on character", LogLevel.ERROR);
			return false;
		}

		EquipedLoadoutStorageComponent loadoutStorage = EquipedLoadoutStorageComponent.Cast(
			character.FindComponent(EquipedLoadoutStorageComponent));
		EquipedWeaponStorageComponent weaponStorage = EquipedWeaponStorageComponent.Cast(
			character.FindComponent(EquipedWeaponStorageComponent));

		Print("[RK29] apply '" + kit.m_sKitName + "' begins", LogLevel.NORMAL);

		// same-prefab weapons/garments are kept in place - their contents still get swapped:
		// StripLooseItems empties kept garments, DressItems refills them. Kit gear authored
		// INSIDE a garment prefab does not survive a keep (house rule: author at character level).
		map<int, IEntity> keptWeapons = new map<int, IEntity>();
		StripWeapons(manager, weaponStorage, kit, keptWeapons);

		// loose items BEFORE clothing - GetItems includes worn garments, so run while only
		// old gear is on the body; then swap clothing slot-by-slot so the outfit-faction
		// score never hits zero (a full strip flips perceived faction to null = disguise popup)
		StripLooseItems(manager, weaponStorage);
		ReplaceClothing(manager, loadoutStorage, kit);

		IEntity primaryWeapon = DressWeapons(manager, weaponStorage, kit, keptWeapons);
		droppedItems = DressItems(manager, character, kit);

		// optic last - weapon must be fully spawned
		if (primaryWeapon)
		{
			if (mounts)
			{
				foreach (ResourceName mount : mounts)
				{
					if (mount == ResourceName.Empty || HasAttachment(primaryWeapon, mount))
						continue;
					if (!InsertAttachment(manager, primaryWeapon, mount))
						Print("[RK29] mount did not fit weapon: " + mount, LogLevel.WARNING);
				}
			}
			ApplyOptic(manager, primaryWeapon, optic);
		}
		else if (optic != ResourceName.Empty)
			Print("[RK29] optic skipped - no primary weapon spawned", LogLevel.WARNING);

		// full rebuild of the perceived-faction outfit scores - the strip can skip per-item
		// remove hooks, and the score map is incremental (drifts forever without this)
		SCR_CharacterFactionAffiliationComponent affiliation = SCR_CharacterFactionAffiliationComponent.Cast(
			character.FindComponent(SCR_CharacterFactionAffiliationComponent));
		if (affiliation)
			affiliation.InitPlayerOutfitFaction_S();

		Print("[RK29] apply '" + kit.m_sKitName + "' done", LogLevel.NORMAL);
		return true;
	}

	//--------------------------------------------------------------------------------------------
	//! Attach/replace the optic on a live weapon. Empty optic = remove (None/irons).
	//! Searches the whole attachment tree - mounted rails expose their own optic slots.
	static bool ApplyOptic(notnull SCR_InventoryStorageManagerComponent manager, notnull IEntity weapon, ResourceName optic)
	{
		IEntity current = FindMountedOpticRecursive(weapon);
		if (current)
		{
			EntityPrefabData epd = current.GetPrefabData();
			if (epd && epd.GetPrefabName() == optic)
				return true;

			if (!manager.TryDeleteItem(current))
				SCR_EntityHelper.DeleteEntityAndChildren(current);
		}

		if (optic == ResourceName.Empty)
			return true;

		if (!InsertAttachment(manager, weapon, optic))
		{
			Print("[RK29] optic did not fit weapon: " + optic, LogLevel.WARNING);
			return false;
		}
		return true;
	}

	//--------------------------------------------------------------------------------------------
	//! Insert an attachment into the weapon or anything already attached to it.
	static bool InsertAttachment(notnull SCR_InventoryStorageManagerComponent manager, notnull IEntity weapon, ResourceName prefab)
	{
		array<BaseInventoryStorageComponent> storages = {};
		CollectAttachmentStorages(weapon, storages, 0);

		foreach (BaseInventoryStorageComponent storage : storages)
		{
			RK29_SpawnCallback cb = new RK29_SpawnCallback();
			if (manager.TrySpawnPrefabToStorage(prefab, storage, -1, cb: cb))
				return true;
		}
		return false;
	}

	//--------------------------------------------------------------------------------------------
	protected static void CollectAttachmentStorages(IEntity entity, notnull array<BaseInventoryStorageComponent> outStorages, int depth)
	{
		if (!entity || depth > 3)
			return;

		SCR_WeaponAttachmentsStorageComponent attachStorage = SCR_WeaponAttachmentsStorageComponent.Cast(
			entity.FindComponent(SCR_WeaponAttachmentsStorageComponent));
		if (!attachStorage)
			return;

		outStorages.Insert(attachStorage);
		for (int i = 0, n = attachStorage.GetSlotsCount(); i < n; i++)
		{
			InventoryStorageSlot slot = attachStorage.GetSlot(i);
			if (slot)
				CollectAttachmentStorages(slot.GetAttachedEntity(), outStorages, depth + 1);
		}
	}

	//--------------------------------------------------------------------------------------------
	protected static bool HasAttachment(IEntity entity, ResourceName prefab, int depth = 0)
	{
		if (!entity || depth > 3)
			return false;

		SCR_WeaponAttachmentsStorageComponent attachStorage = SCR_WeaponAttachmentsStorageComponent.Cast(
			entity.FindComponent(SCR_WeaponAttachmentsStorageComponent));
		if (!attachStorage)
			return false;

		for (int i = 0, n = attachStorage.GetSlotsCount(); i < n; i++)
		{
			InventoryStorageSlot slot = attachStorage.GetSlot(i);
			if (!slot)
				continue;
			IEntity attached = slot.GetAttachedEntity();
			if (!attached)
				continue;

			EntityPrefabData epd = attached.GetPrefabData();
			if (epd && epd.GetPrefabName() == prefab)
				return true;

			if (HasAttachment(attached, prefab, depth + 1))
				return true;
		}
		return false;
	}

	//--------------------------------------------------------------------------------------------
	protected static IEntity FindMountedOpticRecursive(IEntity entity, int depth = 0)
	{
		if (!entity || depth > 3)
			return null;

		SCR_WeaponAttachmentsStorageComponent attachStorage = SCR_WeaponAttachmentsStorageComponent.Cast(
			entity.FindComponent(SCR_WeaponAttachmentsStorageComponent));
		if (!attachStorage)
			return null;

		for (int i = 0, n = attachStorage.GetSlotsCount(); i < n; i++)
		{
			InventoryStorageSlot slot = attachStorage.GetSlot(i);
			if (!slot)
				continue;

			AttachmentSlotComponent asc = AttachmentSlotComponent.Cast(slot.GetParentContainer());
			if (asc && asc.GetAttachmentSlotType() && asc.GetAttachmentSlotType().Type().IsInherited(AttachmentOptics))
			{
				IEntity attached = slot.GetAttachedEntity();
				if (attached)
					return attached;
			}

			IEntity deeper = FindMountedOpticRecursive(slot.GetAttachedEntity(), depth + 1);
			if (deeper)
				return deeper;
		}
		return null;
	}

	// ============================================================================ strip

	//--------------------------------------------------------------------------------------------
	//! Deletes weapons the kit doesn't reuse; same-prefab ones stay, keyed by kit slot.
	protected static void StripWeapons(SCR_InventoryStorageManagerComponent manager, EquipedWeaponStorageComponent weaponStorage, RK29_KitStruct kit, notnull map<int, IEntity> outKept)
	{
		if (!weaponStorage)
			return;

		for (int i = 0, n = weaponStorage.GetSlotsCount(); i < n; i++)
		{
			InventoryStorageSlot slot = weaponStorage.GetSlot(i);
			if (!slot)
				continue;
			IEntity item = slot.GetAttachedEntity();
			if (!item)
				continue;

			ResourceName prefab;
			EntityPrefabData epd = item.GetPrefabData();
			if (epd)
				prefab = epd.GetPrefabName();

			bool kept = false;
			foreach (int kitSlot, ResourceName wanted : kit.m_mWeapons)
			{
				if (wanted == prefab && !outKept.Contains(kitSlot))
				{
					outKept.Set(kitSlot, item);
					kept = true;
					break;
				}
			}
			if (kept)
				continue;

			if (!manager.TryDeleteItem(item))
			{
				slot.DetachEntity();
				SCR_EntityHelper.DeleteEntityAndChildren(item);
			}
		}
	}

	//--------------------------------------------------------------------------------------------
	//! Empties the inventory. Worn garments stay (ReplaceClothing swaps those); kept weapons
	//! and their internals stay (weapon pass business).
	protected static void StripLooseItems(SCR_InventoryStorageManagerComponent manager, EquipedWeaponStorageComponent weaponStorage)
	{
		array<IEntity> items = {};
		manager.GetItems(items);
		foreach (IEntity item : items)
		{
			if (!item || item.FindComponent(BaseLoadoutClothComponent))
				continue;
			if (IsWeaponOrInsideWeapon(item, weaponStorage))
				continue;
			if (!manager.TryDeleteItem(item))
				SCR_EntityHelper.DeleteEntityAndChildren(item);
		}
	}

	//--------------------------------------------------------------------------------------------
	protected static bool IsWeaponOrInsideWeapon(IEntity item, EquipedWeaponStorageComponent weaponStorage)
	{
		if (weaponStorage)
		{
			InventoryItemComponent iic = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
			if (iic)
			{
				InventoryStorageSlot slot = iic.GetParentSlot();
				if (slot && slot.GetStorage() == weaponStorage)
					return true;
			}
		}

		IEntity p = item.GetParent();
		while (p)
		{
			if (p.FindComponent(WeaponComponent))
				return true;
			p = p.GetParent();
		}
		return false;
	}

	// ============================================================================ dress

	//--------------------------------------------------------------------------------------------
	//! Delete one slot's garment, immediately refill it (slots are type-gated, only the matching
	//! replacement lands), then place leftovers into previously empty slots.
	protected static void ReplaceClothing(SCR_InventoryStorageManagerComponent manager, EquipedLoadoutStorageComponent loadoutStorage, RK29_KitStruct kit)
	{
		if (!loadoutStorage)
		{
			Print("[RK29] no loadout storage - clothing skipped", LogLevel.WARNING);
			return;
		}

		array<ResourceName> pending = {};
		foreach (string slotName, ResourceName prefab : kit.m_mClothing)
			pending.Insert(prefab);

		// snapshot first - the slot id below is only a routing hint, a garment can land in a
		// later slot; deleting whatever occupies each slot would then eat just-equipped gear
		int slotCount = loadoutStorage.GetSlotsCount();
		array<IEntity> oldGarments = {};
		for (int i = 0; i < slotCount; i++)
		{
			InventoryStorageSlot slot = loadoutStorage.GetSlot(i);
			if (slot)
				oldGarments.Insert(slot.GetAttachedEntity());
			else
				oldGarments.Insert(null);
		}

		for (int i = 0; i < slotCount; i++)
		{
			IEntity occupant = oldGarments[i];
			if (!occupant)
				continue;

			EntityPrefabData epd = occupant.GetPrefabData();
			if (epd)
			{
				int same = pending.Find(epd.GetPrefabName());
				if (same != -1)
				{
					pending.RemoveOrdered(same);
					continue;
				}
			}

			if (!manager.TryDeleteItem(occupant))
			{
				InventoryStorageSlot slot = loadoutStorage.GetSlot(i);
				if (slot && slot.GetAttachedEntity() == occupant)
					slot.DetachEntity();
				SCR_EntityHelper.DeleteEntityAndChildren(occupant);
			}

			for (int p = 0; p < pending.Count(); p++)
			{
				RK29_SpawnCallback cb = new RK29_SpawnCallback();
				if (manager.TrySpawnPrefabToStorage(pending[p], loadoutStorage, i, cb: cb))
				{
					pending.RemoveOrdered(p);
					break;
				}
			}
		}

		// garments for slots that were empty on the old kit
		foreach (ResourceName prefab : pending)
		{
			RK29_SpawnCallback cb = new RK29_SpawnCallback();
			if (!manager.TrySpawnPrefabToStorage(prefab, loadoutStorage, -1, cb: cb))
				Print("[RK29] clothing did not equip: " + prefab, LogLevel.WARNING);
		}
	}

	//--------------------------------------------------------------------------------------------
	//! Returns the primary weapon (kept or spawned) for the optic pass.
	protected static IEntity DressWeapons(SCR_InventoryStorageManagerComponent manager, EquipedWeaponStorageComponent weaponStorage, RK29_KitStruct kit, notnull map<int, IEntity> kept)
	{
		if (!weaponStorage)
		{
			Print("[RK29] no weapon storage - weapons skipped", LogLevel.WARNING);
			return null;
		}

		IEntity primary = kept.Get(0);
		foreach (int slotIdx, ResourceName prefab : kit.m_mWeapons)
		{
			if (kept.Contains(slotIdx))
				continue;

			int targetSlot = slotIdx;
			if (slotIdx == RK29_KitStruct.GRENADE_SLOT)
				targetSlot = -1;

			RK29_SpawnCallback cb = new RK29_SpawnCallback();
			bool ok = manager.TrySpawnPrefabToStorage(prefab, weaponStorage, targetSlot, cb: cb);
			if (!ok && targetSlot != -1)
			{
				// retry auto-routed on slot mismatch
				cb = new RK29_SpawnCallback();
				ok = manager.TrySpawnPrefabToStorage(prefab, weaponStorage, -1, cb: cb);
			}
			if (!ok)
			{
				Print("[RK29] weapon did not equip: " + prefab, LogLevel.WARNING);
				continue;
			}
			if (slotIdx == 0)
				primary = cb.RK29_GetSpawned();
		}
		return primary;
	}

	//--------------------------------------------------------------------------------------------
	//! Largest-first so bulky items claim pouch space before filler. Returns dropped count.
	protected static int DressItems(SCR_InventoryStorageManagerComponent manager, IEntity character, RK29_KitStruct kit)
	{
		// gathered after clothing spawned - the pouches live on the vest
		array<BaseInventoryStorageComponent> storages = {};
		manager.GetStorages(storages);

		array<ResourceName> items = {};
		array<string> hints = {};
		array<int> volumes = {};
		foreach (RK29_KitItemBatch batch : kit.m_aItems)
		{
			foreach (ResourceName item : batch.m_aPrefabs)
			{
				int vol = ItemVolume(item);
				int at = items.Count();
				for (int i = 0, n = items.Count(); i < n; i++)
				{
					if (volumes[i] < vol)
					{
						at = i;
						break;
					}
				}
				items.InsertAt(item, at);
				hints.InsertAt(batch.m_sTargetHint, at);
				volumes.InsertAt(vol, at);
			}
		}

		int dropped = 0;
		for (int i = 0, n = items.Count(); i < n; i++)
		{
			if (!SpawnItemSomewhere(manager, storages, items[i], hints[i]))
			{
				dropped++;
				Print("[RK29] dropped: " + items[i], LogLevel.WARNING);
			}
		}
		if (dropped > 0)
			Print("[RK29] " + dropped.ToString() + " item(s) did not fit and were dropped", LogLevel.WARNING);
		return dropped;
	}

	protected static ref map<ResourceName, int> s_mVolumeCache = new map<ResourceName, int>();

	//--------------------------------------------------------------------------------------------
	//! Variant prefabs inherit their volume from a base - walk ancestors until found.
	protected static int ItemVolume(ResourceName prefab)
	{
		int vol;
		if (s_mVolumeCache.Find(prefab, vol))
			return vol;

		Resource res = Resource.Load(prefab);
		if (res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			while (src && vol == 0)
			{
				for (int i = 0, n = src.GetComponentCount(); i < n; i++)
				{
					IEntityComponentSource comp = src.GetComponent(i);
					if (!comp || !comp.GetClassName().Contains("InventoryItemComponent"))
						continue;
					BaseContainer attr = comp.GetObject("Attributes");
					if (attr)
					{
						BaseContainer phys = attr.GetObject("ItemPhysAttributes");
						if (phys)
							phys.Get("ItemVolume", vol);
					}
					break;
				}
				src = src.GetAncestor();
			}
		}

		s_mVolumeCache.Set(prefab, vol);
		return vol;
	}

	//--------------------------------------------------------------------------------------------
	//! Exact authored container first, then anything on the hint path, then engine routing.
	protected static bool SpawnItemSomewhere(SCR_InventoryStorageManagerComponent manager, array<BaseInventoryStorageComponent> storages, ResourceName item, string targetHint)
	{
		if (targetHint != string.Empty)
		{
			string leaf = targetHint;
			int lastSlash = leaf.LastIndexOf("/");
			if (lastSlash >= 0)
				leaf = leaf.Substring(lastSlash + 1, leaf.Length() - lastSlash - 1);

			foreach (BaseInventoryStorageComponent storage : storages)
			{
				if (!storage || OwnerFileName(storage) != leaf)
					continue;
				RK29_SpawnCallback cb = new RK29_SpawnCallback();
				if (manager.TrySpawnPrefabToStorage(item, storage, -1, cb: cb))
					return true;
			}

			foreach (BaseInventoryStorageComponent storage : storages)
			{
				if (!storage)
					continue;
				string fname = OwnerFileName(storage);
				if (fname == string.Empty || fname == leaf || !targetHint.Contains(fname))
					continue;
				RK29_SpawnCallback cb = new RK29_SpawnCallback();
				if (manager.TrySpawnPrefabToStorage(item, storage, -1, cb: cb))
					return true;
			}
		}

		RK29_SpawnCallback cb = new RK29_SpawnCallback();
		return manager.TrySpawnPrefabToStorage(item, null, -1, EStoragePurpose.PURPOSE_DEPOSIT, cb);
	}

	//--------------------------------------------------------------------------------------------
	protected static string OwnerFileName(BaseInventoryStorageComponent storage)
	{
		IEntity owner = storage.GetOwner();
		if (!owner)
			return string.Empty;
		EntityPrefabData epd = owner.GetPrefabData();
		if (!epd)
			return string.Empty;

		string raw = "" + epd.GetPrefabName();
		int lastSlash = raw.LastIndexOf("/");
		if (lastSlash >= 0)
			raw = raw.Substring(lastSlash + 1, raw.Length() - lastSlash - 1);
		return raw;
	}
}
