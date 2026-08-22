//------------------------------------------------------------------------------------------------
//! Reads a kit's authored gear straight off its prefab container.
//! Entity sources present the fully merged view: inherited components enumerate and
//! their values resolve, so one pass over the kit prefab layer sees everything.
//------------------------------------------------------------------------------------------------
class RK29_KitCapture
{
	//--------------------------------------------------------------------------------------------
	static RK29_KitStruct Capture(string kitName, string factionKey, ResourceName kitPrefab)
	{
		Resource res = Resource.Load(kitPrefab);
		if (!res.IsValid())
		{
			Print("[RK29] capture FAILED - cannot load " + kitPrefab, LogLevel.ERROR);
			return null;
		}

		IEntitySource src = res.GetResource().ToEntitySource();
		if (!src)
		{
			Print("[RK29] capture FAILED - no entity source in " + kitPrefab, LogLevel.ERROR);
			return null;
		}

		RK29_KitStruct kit = new RK29_KitStruct();
		kit.m_sKitName      = kitName;
		kit.m_sFactionKey   = factionKey;
		kit.m_sSourcePrefab = kitPrefab;

		int nComp = src.GetComponentCount();
		for (int i = 0; i < nComp; i++)
		{
			IEntityComponentSource comp = src.GetComponent(i);
			if (!comp)
				continue;

			string cls = comp.GetClassName();

			if (cls == "CharacterWeaponSlotComponent")
				ReadWeaponSlot(comp, kit);
			else if (cls == "CharacterGrenadeSlotComponent")
				ReadGrenadeSlot(comp, kit);
			else if (cls == "BaseLoadoutManagerComponent")
				ReadClothing(comp, kit);
			else if (cls == "SCR_InventoryStorageManagerComponent")
				ReadInitialItems(comp, kit);
			else if (cls == "SCR_CharacterInventoryStorageComponent")
				ReadEquipmentSlots(comp, kit);
			else if (cls == "SCR_EditableCharacterComponent")
			{
				// instance now, while res is alive - containers die with the resource
				BaseContainer infoSrc = comp.GetObject("m_UIInfo");
				if (infoSrc)
					kit.m_UIInfo = SCR_UIInfo.Cast(BaseContainerTools.CreateInstanceFromContainer(infoSrc));
			}
		}

		Print(string.Format("[RK29] captured '%1': %2 clothing, %3 weapons, %4 items%5",
			kitName, kit.m_mClothing.Count(), kit.m_mWeapons.Count(), kit.CountItems(),
			SelectString(kit.m_UIInfo != null, "", " (NO UIInfo)")), LogLevel.NORMAL);

		return kit;
	}

	//--------------------------------------------------------------------------------------------
	protected static void ReadWeaponSlot(IEntityComponentSource comp, RK29_KitStruct kit)
	{
		ResourceName weapon;
		comp.Get("WeaponTemplate", weapon);
		if (weapon == ResourceName.Empty)
			return;

		int slotIdx = 0;
		comp.Get("WeaponSlotIndex", slotIdx);

		kit.m_mWeapons.Set(slotIdx, weapon);
		if (slotIdx == 0)
			kit.m_sPrimaryWeapon = weapon;
	}

	//--------------------------------------------------------------------------------------------
	protected static void ReadGrenadeSlot(IEntityComponentSource comp, RK29_KitStruct kit)
	{
		ResourceName grenade;
		comp.Get("WeaponTemplate", grenade);
		if (grenade != ResourceName.Empty)
			kit.m_mWeapons.Set(RK29_KitStruct.GRENADE_SLOT, grenade);
	}

	//--------------------------------------------------------------------------------------------
	protected static void ReadClothing(IEntityComponentSource comp, RK29_KitStruct kit)
	{
		BaseContainerList slots = comp.GetObjectArray("Slots");
		if (!slots)
			return;

		for (int i = 0, n = slots.Count(); i < n; i++)
		{
			BaseContainer slot = slots.Get(i);
			if (!slot)
				continue;

			ResourceName prefab;
			slot.Get("Prefab", prefab);
			if (prefab == ResourceName.Empty)
				continue;

			kit.m_mClothing.Set(slot.GetName(), prefab);
		}
	}

	//--------------------------------------------------------------------------------------------
	//! Equipment lives on a nested SCR_EquipmentStorageComponent (watch, binoculars) -
	//! a third authoring channel beside dress slots and the item-init list.
	protected static void ReadEquipmentSlots(IEntityComponentSource comp, RK29_KitStruct kit)
	{
		BaseContainerList comps = comp.GetObjectArray("components");
		if (!comps)
			return;

		for (int i = 0, n = comps.Count(); i < n; i++)
		{
			BaseContainer sub = comps.Get(i);
			if (!sub || sub.GetClassName() != "SCR_EquipmentStorageComponent")
				continue;

			BaseContainerList slots = sub.GetObjectArray("InitialStorageSlots");
			if (!slots)
				continue;

			for (int s = 0, sn = slots.Count(); s < sn; s++)
			{
				BaseContainer slot = slots.Get(s);
				if (!slot)
					continue;

				ResourceName prefab;
				slot.Get("Prefab", prefab);
				if (prefab != ResourceName.Empty)
					kit.m_mEquipment.Set(slot.GetName(), prefab);
			}
		}
	}

	//--------------------------------------------------------------------------------------------
	protected static void ReadInitialItems(IEntityComponentSource comp, RK29_KitStruct kit)
	{
		BaseContainerList items = comp.GetObjectArray("InitialInventoryItems");
		if (!items)
			return;

		for (int i = 0, n = items.Count(); i < n; i++)
		{
			BaseContainer entry = items.Get(i);
			if (!entry)
				continue;

			bool enabled = true;
			entry.Get("Enabled", enabled);
			if (!enabled)
				continue;

			RK29_KitItemBatch batch = new RK29_KitItemBatch();
			entry.Get("TargetStorage", batch.m_sTargetHint);

			array<ResourceName> prefabs = {};
			entry.Get("PrefabsToSpawn", prefabs);
			if (prefabs.IsEmpty())
				continue;

			foreach (ResourceName p : prefabs)
			{
				if (p != ResourceName.Empty)
					batch.m_aPrefabs.Insert(p);
			}

			if (!batch.m_aPrefabs.IsEmpty())
				kit.m_aItems.Insert(batch);
		}
	}

	//--------------------------------------------------------------------------------------------
	protected static string SelectString(bool cond, string a, string b)
	{
		if (cond)
			return a;
		return b;
	}
}
