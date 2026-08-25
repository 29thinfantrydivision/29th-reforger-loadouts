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
//! One solved placement. The container key is owner prefab + ordinal, which is stable for
//! a config-dressed body (same kit -> same rig -> same containers in the same order).
class RK29_PlanEntry
{
	ResourceName m_sPrefab;
	string m_sContainerKey;
}

//------------------------------------------------------------------------------------------------
class RK29_KitApply
{
	//! Solved placements per kit variant, kept for the session. Cleared when the manager
	//! re-boots, since a plan is only valid for the kit and rig it was solved against.
	//! Above this many of one item, "keep the stack together" stops outranking where the
	//! item is supposed to live. Ammo is meant to be distributed; consumables are not.
	protected static const int COHESION_MAX_STACK = 3;

	protected static ref map<string, ref array<ref RK29_PlanEntry>> s_mPlanCache = new map<string, ref array<ref RK29_PlanEntry>>();

	//--------------------------------------------------------------------------------------------
	static void ClearPlans()
	{
		s_mPlanCache.Clear();
	}

	//! Items that DID find a home, but only by displacing something else or by falling
	//! through to "anywhere it fits". A kit can report zero drops and still be over-stuffed;
	//! this is the difference between "nothing hit the floor" and "everything has a home".
	//! Valid until the next Apply.
	protected static ref array<ResourceName> s_aCrammed = {};

	//! "PaperMap_01.et -> Pants_US_BDU.et#0" for the last Apply. The audit needs to know where
	//! the solver BELIEVED it put an item, because "solver sent it to the trousers" and "the
	//! trousers refused it" are different bugs from "the solver never considered the pack".
	protected static ref array<string> s_aLastSent = {};

	//--------------------------------------------------------------------------------------------
	static array<string> LastSent()
	{
		return s_aLastSent;
	}

	//--------------------------------------------------------------------------------------------
	static array<ResourceName> LastCrammed()
	{
		return s_aCrammed;
	}

	//--------------------------------------------------------------------------------------------
	//! Re-dress character as kit, then attach mounts + optic (empty = irons). False only on
	//! hard failure. `mounts` seats before the optic; weapon-variant optics never reach here
	//! (the manager swaps the weapon instead).
	static bool Apply(notnull IEntity character, notnull RK29_KitStruct kit, ResourceName optic, array<ResourceName> mounts, out array<ResourceName> droppedItems)
	{
		droppedItems = {};
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

		// anchor the outfit-faction tally so the perceived faction cannot flip to UNKNOWN
		// server-side mid-apply (garment swaps fire per-item recalcs). Clients are already
		// safe because the perceived faction is an end-of-frame RplProp and this whole
		// apply is single-frame - the anchor also covers AI perception and FULL_OUTFIT
		// modes. The final InitPlayerOutfitFaction_S below clears and rebuilds the tally,
		// erasing the anchor.
		SCR_CharacterFactionAffiliationComponent anchorAffiliation = SCR_CharacterFactionAffiliationComponent.Cast(
			character.FindComponent(SCR_CharacterFactionAffiliationComponent));
		if (anchorAffiliation && anchorAffiliation.GetAffiliatedFaction())
			anchorAffiliation.AddFactionOutfitValue(anchorAffiliation.GetAffiliatedFaction(), 100000, false);

		// the player's quick-slot bindings are muscle memory - remember them by prefab
		// before the strip so the re-dressed kit lands on the same keys
		array<ResourceName> quickSlotPrefabs = {};
		CaptureQuickSlots(character, quickSlotPrefabs);

		// weapons are replaced wholesale, like garments. Carrying one over would carry its
		// state over too: a part-used magazine, and worse, an empty chamber - the engine
		// exposes IsCurrentBarrelChambered and ClearChamber but no way to SEAT a round, so
		// a kept weapon can end up full-mag-but-unchambered with no way back. A fresh spawn
		// is always in its authored state.
		StripWeapons(manager, weaponStorage);
		StripGrenadeSlots(manager, character, kit);

		// strip BEFORE clothing (while only old gear is on the body), then swap clothing
		// slot-by-slot so the outfit-faction score never hits zero (a full garment strip
		// flips perceived faction to null = disguise popup)
		StripStorages(manager, character);
		ReplaceClothing(manager, loadoutStorage, kit);

		// freshly spawned garments arrive with their prefab-authored slot spawns (canteen,
		// gadget straps, ...) - sweep once more so the config stays the single source of
		// truth for gear. The pass is idempotent; kept garments were already emptied.
		StripStorages(manager, character);
		CensusRemaining(manager, character);
		DressEquipment(manager, character, kit);

		IEntity primaryWeapon = DressWeapons(manager, weaponStorage, kit);
		DressItems(manager, weaponStorage, character, kit, droppedItems);

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

		RestoreQuickSlots(character, quickSlotPrefabs);

		// full rebuild of the perceived-faction outfit scores - the strip can skip per-item
		// remove hooks, and the score map is incremental (drifts forever without this)
		SCR_CharacterFactionAffiliationComponent affiliation = SCR_CharacterFactionAffiliationComponent.Cast(
			character.FindComponent(SCR_CharacterFactionAffiliationComponent));
		if (affiliation)
			affiliation.InitPlayerOutfitFaction_S();

		ApplyTraits(character, kit);

		Print("[RK29] apply '" + kit.m_sKitName + "' done", LogLevel.NORMAL);
		return true;
	}

	//--------------------------------------------------------------------------------------------
	//! The kit's role qualifications, as instance labels on the body. Vanilla user actions and
	//! consumables read these for their qualified-personnel speed bonus - a medic's field
	//! dressing, a sapper's building. Written on every apply, empty list included, so a re-kit
	//! never leaves the previous class's traits behind. The write also marks the body kit-owned
	//! (RK29_CharacterLabels.c), which is what stops the prefab's own labels counting - so a kit
	//! with no traits really does grant none, whatever body it spawned on.
	//! Public because the spawn path calls it alone, without the re-dress, for stock spawns.
	static void ApplyTraits(notnull IEntity character, notnull RK29_KitStruct kit)
	{
		SCR_EditableCharacterComponent editable = SCR_EditableCharacterComponent.Cast(
			character.FindComponent(SCR_EditableCharacterComponent));
		if (!editable)
		{
			if (kit.m_aTraits && !kit.m_aTraits.IsEmpty())
				Print("[RK29] traits skipped - no SCR_EditableCharacterComponent on the character", LogLevel.WARNING);
			return;
		}

		array<EEditableEntityLabel> labels = {};
		string named;
		if (kit.m_aTraits)
		{
			foreach (RK29_ETrait trait : kit.m_aTraits)
			{
				EEditableEntityLabel label = RK29_Traits.LabelOf(trait);
				if (label == EEditableEntityLabel.NONE || labels.Contains(label))
					continue;
				labels.Insert(label);
				named = named + " " + RK29_Traits.NameOf(trait);
			}
		}

		// RK29_SetTraits_S, not the vanilla setter: it also marks the body kit-owned, so the
		// prefab's own labels stop counting. Without that a body could only ever gain traits.
		editable.RK29_SetTraits_S(labels);
		if (labels.IsEmpty())
			named = " none";
		// logged even when empty: "did the medic trait come off on the swap" is otherwise only
		// answerable by timing a bandage
		Print("[RK29] traits:" + named, LogLevel.NORMAL);
	}

	//--------------------------------------------------------------------------------------------
	//! Attach/replace the optic on a live weapon. Empty optic = remove (None/irons).
	//! Searches the whole attachment tree - mounted rails expose their own optic slots.
	static bool ApplyOptic(notnull SCR_InventoryStorageManagerComponent manager, notnull IEntity weapon, ResourceName optic)
	{
		ResourceName oldOptic;
		IEntity current = FindMountedOpticRecursive(weapon);
		if (current)
		{
			EntityPrefabData epd = current.GetPrefabData();
			if (epd)
			{
				if (epd.GetPrefabName() == optic)
					return true;
				oldOptic = epd.GetPrefabName();
			}

			if (!manager.TryDeleteItem(current))
				SCR_EntityHelper.DeleteEntityAndChildren(current);
		}

		if (optic == ResourceName.Empty)
			return true;

		if (!InsertAttachment(manager, weapon, optic))
		{
			Print("[RK29] optic did not fit weapon: " + optic, LogLevel.WARNING);
			// misconfiguration turns cosmetic: put the authored optic back instead of irons
			if (oldOptic != ResourceName.Empty)
				InsertAttachment(manager, weapon, oldOptic);
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
	static bool HasAttachment(IEntity entity, ResourceName prefab, int depth = 0)
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
	static IEntity FindMountedOpticRecursive(IEntity entity, int depth = 0)
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
	protected static void StripWeapons(SCR_InventoryStorageManagerComponent manager, EquipedWeaponStorageComponent weaponStorage)
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

			if (!manager.TryDeleteItem(item))
			{
				slot.DetachEntity();
				SCR_EntityHelper.DeleteEntityAndChildren(item);
			}
		}
	}

	//--------------------------------------------------------------------------------------------
	//! The throwable slot is a CharacterGrenadeSlotComponent - NOT enumerated by the
	//! equipped-weapon storage (proven empirically: a primed smoke survives every apply).
	//! Config kits carry grenades as items only, so any slot occupant the kit doesn't
	//! author goes; the engine re-equips a throwable from inventory on demand.
	protected static void StripGrenadeSlots(SCR_InventoryStorageManagerComponent manager, IEntity character, RK29_KitStruct kit)
	{
		array<Managed> slots = {};
		character.FindComponents(CharacterGrenadeSlotComponent, slots);
		foreach (Managed m : slots)
		{
			CharacterGrenadeSlotComponent slot = CharacterGrenadeSlotComponent.Cast(m);
			if (!slot)
				continue;
			IEntity grenade = slot.GetWeaponEntity();
			if (!grenade)
				continue;

			ResourceName wanted;
			kit.m_mWeapons.Find(RK29_KitStruct.GRENADE_SLOT, wanted);
			ResourceName current;
			EntityPrefabData epd = grenade.GetPrefabData();
			if (epd)
				current = epd.GetPrefabName();
			if (wanted != ResourceName.Empty && current == wanted)
				continue;

			RK29_Log.Trace("[RK29] strip: " + FileNameOf(grenade) + " from grenade slot");
			if (!manager.TryDeleteItem(grenade))
			{
				InventoryStorageSlot slotInfo = slot.GetSlotInfo();
				if (slotInfo && slotInfo.GetAttachedEntity() == grenade)
					slotInfo.DetachEntity();
				SCR_EntityHelper.DeleteEntityAndChildren(grenade);
			}
		}
	}

	//--------------------------------------------------------------------------------------------
	//! ONE strip rule instead of per-category patches: walk every storage the manager knows
	//! (pouches, garment cloth nodes, gadget straps on suspenders/belts - the whole tree)
	//! and delete every slot occupant, except storages another pass owns and occupants that
	//! are structure. No item-category or slot-name knowledge - anything gear-like that
	//! occupies a storage slot anywhere on the body dies here and DressX rebuilds from config.
	//! Skipped storage domains:
	//!  - garment slots (ReplaceClothing) and weapon slots (StripWeapons/DressWeapons)
	//!  - storages on weapons (a weapon's own mags/attachments are its business)
	//!  - the character's managed equipment storage (DressEquipment delta-swaps it)
	//!  - medical/identity storages (applied tourniquets, dogtags - never ours to touch)
	//! Kept occupants: containers (pouches, suspenders, buttpacks - capacity, emptied via
	//! their own storages) and clothing pieces (belt dummies and other cosmetic garb).
	protected static void StripStorages(SCR_InventoryStorageManagerComponent manager, IEntity character)
	{
		// entity-tree walk, NOT manager.GetStorages(): the manager filters by servable
		// purpose and never returns PURPOSE_EQUIPMENT_ATTACHMENT storages (gadget straps
		// on suspenders/belts) - exactly where duplicated radios/e-tools were hiding
		array<BaseInventoryStorageComponent> storages = {};
		CollectBodyStorages(character, storages, 0);
		array<IEntity> doomed = {};
		array<InventoryStorageSlot> doomedSlots = {};
		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (!storage)
				continue;
			if (EquipedLoadoutStorageComponent.Cast(storage)
				|| EquipedWeaponStorageComponent.Cast(storage))
				continue; // garments / weapons - ReplaceClothing and the weapon passes own these
			if (SCR_IdentityItemStorageComponent.Cast(storage)
				|| SCR_SalineStorageComponent.Cast(storage)
				|| SCR_TourniquetStorageComponent.Cast(storage))
				continue; // body state (applied tourniquets, dogtags), not loadout
			// Nothing owned by the CHARACTER ENTITY is cargo. Real cargo lives in worn garments,
			// which are separate entities - the character's own storages are the hands and the
			// gadget/offhand slot, which report a capacity of 1 and accept anything offered.
			// ChooseContainer scored them like any other container and happily sent the map,
			// compass, bandages and morphine there; capacity 1 means each new item displaced the
			// last, so all but one silently disappeared, and whatever survived got taken into the
			// left hand on spawn. That is the auto-equipped compass. Slots on WORN GEAR (the
			// suspender flashlight strap, a bayonet scabbard) are owned by the garment, so this
			// leaves them alone.
			if (storage.GetOwner() == character)
				continue; // watch/binoculars - DressEquipment delta-swaps these
			if (IsOnWeapon(storage))
				continue; // a weapon owns its mags and attachments

			// collect first, delete after: universal storages reshuffle their slot list on
			// removal, so deleting while indexing skips survivors (log-proven: one item
			// stripped per pouch, the rest left behind to duplicate)
			for (int i = 0, n = storage.GetSlotsCount(); i < n; i++)
			{
				InventoryStorageSlot slot = storage.GetSlot(i);
				if (!slot)
					continue;
				IEntity occupant = slot.GetAttachedEntity();
				if (!occupant)
					continue;
				// THE keep rule - no item-type taxonomy. An occupant is structure iff the
				// slot's authored template spawned it (pouches, suspenders, belt dummies,
				// scabbards - the garment prefab decides). Everything else in any slot is
				// cargo and gets rebuilt from config. BI's quirks (a shovel authored as
				// clothing) cannot leak through a rule that never asks what the item is.
				// authored AND structural (container or cloth piece): pouches, suspenders,
				// belt dummies, scabbard sheaths. Authored NON-structure - the bayonet the
				// scabbard auto-spawns, vest canteens - is gear the config didn't ask for.
				if (IsSlotAuthored(slot, occupant)
					&& (occupant.FindComponent(BaseInventoryStorageComponent)
						|| occupant.FindComponent(BaseLoadoutClothComponent)))
				{
					RK29_Log.Trace("[RK29] kept(authored): " + FileNameOf(occupant) + " @ " + OwnerFileName(storage) + "/" + slot.GetSourceName());
					continue;
				}

				doomed.Insert(occupant);
				doomedSlots.Insert(slot);
				RK29_Log.Trace("[RK29] strip: " + FileNameOf(occupant) + " from " + OwnerFileName(storage) + "/" + slot.GetSourceName());
			}
		}

		for (int d = 0, dn = doomed.Count(); d < dn; d++)
		{
			IEntity victim = doomed[d];
			if (!victim)
				continue;
			if (!manager.TryDeleteItem(victim))
			{
				InventoryStorageSlot vslot = doomedSlots[d];
				if (vslot && vslot.GetAttachedEntity() == victim)
					vslot.DetachEntity();
				SCR_EntityHelper.DeleteEntityAndChildren(victim);
			}
		}
	}

	//--------------------------------------------------------------------------------------------
	//! Enforcement backstop: after the strip, ANY loose item still on the body is
	//! unwanted by definition - the dress passes rebuild everything from config. Logs
	//! where each survivor hid (so leaks stay diagnosable), then deletes it. This is
	//! what finally stops gadget-system relocations (the stacking e-tool) from
	//! accumulating, wherever they hide.
	protected static void CensusRemaining(SCR_InventoryStorageManagerComponent manager, IEntity character)
	{
		array<IEntity> items = {};
		manager.GetItems(items);
		foreach (IEntity item : items)
		{
			if (!item || item.FindComponent(WeaponComponent))
				continue;
			if (IsInsideWeapon(item))
				continue; // a weapon owns its loaded mags/attachments

			InventoryItemComponent iic = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
			// no InventoryItemComponent = not gear at all, but cosmetic cloth hung on a
			// garment's LoadoutSlotInfo - the ALICE AR vest's e-tool carrier is the one on the
			// roster. It has no parent slot to test, so every keep rule below is unreachable for
			// it and it fell through to the delete: the MG lost the shovel pouch off the vest on
			// every apply. Same rule as the !slot case - not something the inventory owns.
			if (!iic)
				continue;

			InventoryStorageSlot slot = iic.GetParentSlot();
			if (!slot)
				continue; // not slotted anywhere the inventory owns - leave it
			// same keep rule as the strip: authored AND structural
			if (IsSlotAuthored(slot, item)
				&& (item.FindComponent(BaseInventoryStorageComponent)
					|| item.FindComponent(BaseLoadoutClothComponent)))
				continue;
			BaseInventoryStorageComponent slotStorage = slot.GetStorage();
			if (slotStorage)
			{
				if (EquipedLoadoutStorageComponent.Cast(slotStorage))
					continue; // worn garment
				if (IsManagedEquipmentStorage(slotStorage, character))
					continue; // DressEquipment's delta domain
				if (SCR_IdentityItemStorageComponent.Cast(slotStorage)
					|| SCR_SalineStorageComponent.Cast(slotStorage)
					|| SCR_TourniquetStorageComponent.Cast(slotStorage))
					continue; // body state
			}
			string where;
			if (slotStorage)
				where = slotStorage.ClassName() + " on " + OwnerFileName(slotStorage) + "/" + slot.GetSourceName();
			else
				where = "slot-without-storage/" + slot.GetSourceName();
			string parentName = "none";
			if (item.GetParent())
				parentName = FileNameOf(item.GetParent());
			Print("[RK29] UNSWEPT: " + FileNameOf(item) + " | " + where + " | parent=" + parentName, LogLevel.WARNING);

			if (!manager.TryDeleteItem(item))
			{
				if (slot.GetAttachedEntity() == item)
					slot.DetachEntity();
				SCR_EntityHelper.DeleteEntityAndChildren(item);
			}
		}
	}

	//--------------------------------------------------------------------------------------------
	protected static bool IsInsideWeapon(IEntity item)
	{
		IEntity p = item.GetParent();
		while (p)
		{
			if (p.FindComponent(WeaponComponent))
				return true;
			p = p.GetParent();
		}
		return false;
	}

	//--------------------------------------------------------------------------------------------
	//! Does this slot's authored template match the occupant - i.e. did the garment
	//! prefab itself put it there (structure), or did something clip it in later (gear)?
	protected static bool IsSlotAuthored(InventoryStorageSlot slot, IEntity occupant)
	{
		if (!slot || !occupant)
			return false;
		ResourceName tmpl = slot.GetSlotTemplate();
		if (tmpl == ResourceName.Empty)
			return false;
		EntityPrefabData epd = occupant.GetPrefabData();
		return epd && epd.GetPrefabName() == tmpl;
	}

	//--------------------------------------------------------------------------------------------
	//! Every storage component on the character and all attached descendants (garments,
	//! suspenders, pouches, scabbards, weapons) - manager registration plays no part.
	protected static void CollectBodyStorages(IEntity entity, notnull array<BaseInventoryStorageComponent> outStorages, int depth)
	{
		if (!entity || depth > 8)
			return;

		array<Managed> comps = {};
		entity.FindComponents(BaseInventoryStorageComponent, comps);
		foreach (Managed m : comps)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(m);
			if (storage)
				outStorages.Insert(storage);
		}

		IEntity child = entity.GetChildren();
		while (child)
		{
			CollectBodyStorages(child, outStorages, depth + 1);
			child = child.GetSibling();
		}
	}

	//--------------------------------------------------------------------------------------------
	protected static string FileNameOf(IEntity entity)
	{
		if (!entity)
			return "?";
		EntityPrefabData epd = entity.GetPrefabData();
		if (!epd)
			return "?";
		string raw = "" + epd.GetPrefabName();
		int lastSlash = raw.LastIndexOf("/");
		if (lastSlash >= 0)
			raw = raw.Substring(lastSlash + 1, raw.Length() - lastSlash - 1);
		return raw;
	}

	//--------------------------------------------------------------------------------------------
	protected static bool IsOnWeapon(BaseInventoryStorageComponent storage)
	{
		IEntity e = storage.GetOwner();
		while (e)
		{
			if (e.FindComponent(WeaponComponent))
				return true;
			e = e.GetParent();
		}
		return false;
	}

	//--------------------------------------------------------------------------------------------
	//! The CHARACTER-carried plain equipment storage (watch/binocular slots) that
	//! DressEquipment manages. Garment strap storages (e-tool/flashlight straps on vests
	//! and belts) are also SCR_EquipmentStorageComponent but belong to the item pass, and
	//! the medical/identity subclasses (saline, tourniquet, dogtags) are never touched.
	protected static bool IsManagedEquipmentStorage(BaseInventoryStorageComponent storage, IEntity character)
	{
		if (!SCR_EquipmentStorageComponent.Cast(storage))
			return false;
		if (SCR_IdentityItemStorageComponent.Cast(storage)
			|| SCR_SalineStorageComponent.Cast(storage)
			|| SCR_TourniquetStorageComponent.Cast(storage))
			return false;
		return storage.GetOwner() == character;
	}

	//--------------------------------------------------------------------------------------------
	//! Delta-swap the character's equipment storage (watch, binoculars) to the kit's
	//! declared set. Same-prefab occupants stay; slots the kit doesn't declare get emptied.
	protected static void DressEquipment(SCR_InventoryStorageManagerComponent manager, IEntity character, RK29_KitStruct kit)
	{
		// entity-tree walk for the same reason as StripStorages: equipment storages are
		// invisible to manager.GetStorages()
		array<BaseInventoryStorageComponent> storages = {};
		CollectBodyStorages(character, storages, 0);

		map<string, bool> satisfied = new map<string, bool>();
		array<IEntity> eqDoomed = {};
		array<InventoryStorageSlot> eqDoomedSlots = {};
		BaseInventoryStorageComponent equipStorage;
		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (!IsManagedEquipmentStorage(storage, character))
				continue;
			equipStorage = storage;

			for (int i = 0, n = storage.GetSlotsCount(); i < n; i++)
			{
				InventoryStorageSlot slot = storage.GetSlot(i);
				if (!slot)
					continue;
				IEntity occupant = slot.GetAttachedEntity();
				if (!occupant)
					continue;

				ResourceName wanted;
				kit.m_mEquipment.Find(slot.GetSourceName(), wanted);

				ResourceName current;
				EntityPrefabData epd = occupant.GetPrefabData();
				if (epd)
					current = epd.GetPrefabName();

				if (wanted != ResourceName.Empty && current == wanted)
				{
					satisfied.Set(slot.GetSourceName(), true);
					continue;
				}

				RK29_Log.Trace("[RK29] equip-clear: " + FileNameOf(occupant) + " from " + slot.GetSourceName());
				eqDoomed.Insert(occupant);
				eqDoomedSlots.Insert(slot);
			}
		}

		for (int d = 0, dn = eqDoomed.Count(); d < dn; d++)
		{
			IEntity victim = eqDoomed[d];
			if (!victim)
				continue;
			if (!manager.TryDeleteItem(victim))
			{
				InventoryStorageSlot vslot = eqDoomedSlots[d];
				if (vslot && vslot.GetAttachedEntity() == victim)
					vslot.DetachEntity();
				SCR_EntityHelper.DeleteEntityAndChildren(victim);
			}
		}

		if (!equipStorage)
		{
			if (!kit.m_mEquipment.IsEmpty())
				Print("[RK29] no equipment storage - equipment skipped", LogLevel.WARNING);
			return;
		}

		foreach (string slotName, ResourceName prefab : kit.m_mEquipment)
		{
			if (satisfied.Contains(slotName))
				continue;
			RK29_SpawnCallback cb = new RK29_SpawnCallback();
			if (!manager.TrySpawnPrefabToStorage(prefab, equipStorage, -1, cb: cb))
				Print("[RK29] equipment did not equip: " + prefab, LogLevel.WARNING);
		}
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

		// scorched earth by decision: garments are ALWAYS deleted and respawned, never
		// kept - a deleted garment takes every child with it, so nothing hiding in a
		// garment (dynamic-slot template quirks included) can survive an apply. The
		// disguise system tolerates this because the whole apply is single-frame, the
		// outfit-faction anchor pins the tally, and InitPlayerOutfitFaction_S rebuilds.
		for (int i = 0; i < slotCount; i++)
		{
			IEntity occupant = oldGarments[i];
			if (!occupant)
				continue;

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
	//! Returns the spawned primary weapon for the optic pass.
	protected static IEntity DressWeapons(SCR_InventoryStorageManagerComponent manager, EquipedWeaponStorageComponent weaponStorage, RK29_KitStruct kit)
	{
		if (!weaponStorage)
		{
			Print("[RK29] no weapon storage - weapons skipped", LogLevel.WARNING);
			return null;
		}

		IEntity primary;
		foreach (int slotIdx, ResourceName prefab : kit.m_mWeapons)
		{
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
	//! Largest-first so bulky items claim pouch space before filler. Prefabs that fit
	//! nowhere land in droppedItems.
	protected static void DressItems(SCR_InventoryStorageManagerComponent manager, EquipedWeaponStorageComponent weaponStorage, IEntity character, RK29_KitStruct kit, array<ResourceName> droppedItems)
	{
		// one grenade rides the throwable slot instead of cargo - authored kits always
		// primed one, and that slot is free capacity (prevents pouch overfill). Frags
		// first to mirror authored priming; the sniper's smoke-primed kit still works
		// because its only grenades ARE smokes.
		ResourceName primed = PrimeGrenadeSlot(manager, weaponStorage, character, kit);

		array<BaseInventoryStorageComponent> containers = {};
		array<int> slotIds = {};
		array<string> keys = {};
		array<string> kinds = {};
		CollectCargoContainers(character, kit, containers, slotIds, keys, kinds);

		array<ResourceName> items = {};
		array<string> hints = {};
		array<ref array<string>> prefs = {};
		bool primedSkipped = primed == ResourceName.Empty;
		foreach (RK29_KitItemBatch batch : kit.m_aItems)
		{
			foreach (ResourceName item : batch.m_aPrefabs)
			{
				// exactly one copy: the one already riding the throwable slot
				if (!primedSkipped && item == primed)
				{
					primedSkipped = true;
					continue;
				}
				items.Insert(item);
				hints.Insert(batch.m_sTargetHint);
				prefs.Insert(batch.m_aPreferred);
			}
		}

		s_aCrammed.Clear();
		s_aLastSent.Clear();

		string planKey = kit.m_sKitName + "|" + kit.m_sPrimaryWeapon;
		array<ref RK29_PlanEntry> plan = s_mPlanCache.Get(planKey);
		if (plan)
		{
			ReplayPlan(manager, containers, slotIds, keys, items, hints, plan, droppedItems);
		}
		else
		{
			plan = SolvePlacement(manager, containers, slotIds, keys, kinds, items, prefs, droppedItems);
			s_mPlanCache.Set(planKey, plan);
			RK29_Log.Trace(string.Format("[RK29] placement solved for '%1' - %2 entries cached", planKey, plan.Count()));
		}

		if (!droppedItems.IsEmpty())
			Print("[RK29] " + droppedItems.Count().ToString() + " item(s) did not fit and were dropped", LogLevel.WARNING);
	}

	//--------------------------------------------------------------------------------------------
	//! Quick-slot bindings by prefab, index-aligned to the slot they sit in. Only gadgets
	//! and consumables matter here; weapons re-bind themselves from their weapon slot.
	protected static void CaptureQuickSlots(IEntity character, notnull array<ResourceName> outPrefabs)
	{
		SCR_CharacterInventoryStorageComponent charStorage = SCR_CharacterInventoryStorageComponent.Cast(
			character.FindComponent(SCR_CharacterInventoryStorageComponent));
		if (!charStorage)
			return;

		array<IEntity> bound = charStorage.GetQuickSlotEntitiesOnly();
		foreach (IEntity item : bound)
		{
			ResourceName prefab;
			if (item)
			{
				EntityPrefabData epd = item.GetPrefabData();
				if (epd)
					prefab = epd.GetPrefabName();
			}
			outPrefabs.Insert(prefab);
		}
	}

	//--------------------------------------------------------------------------------------------
	//! Put the same prefabs back on the same keys. Anything the new kit no longer carries
	//! simply leaves its slot empty rather than being filled by whatever arrived first -
	//! which is what made a kit swap start handing out the wrong gadget.
	protected static void RestoreQuickSlots(IEntity character, array<ResourceName> prefabs)
	{
		if (!prefabs || prefabs.IsEmpty())
			return;

		SCR_CharacterInventoryStorageComponent charStorage = SCR_CharacterInventoryStorageComponent.Cast(
			character.FindComponent(SCR_CharacterInventoryStorageComponent));
		SCR_InventoryStorageManagerComponent manager = SCR_InventoryStorageManagerComponent.Cast(
			character.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!charStorage || !manager)
			return;

		array<IEntity> carried = {};
		manager.GetItems(carried);

		for (int slot = 0, n = prefabs.Count(); slot < n; slot++)
		{
			if (prefabs[slot] == ResourceName.Empty)
				continue;

			foreach (IEntity candidate : carried)
			{
				if (!candidate)
					continue;
				EntityPrefabData epd = candidate.GetPrefabData();
				if (!epd || epd.GetPrefabName() != prefabs[slot])
					continue;
				if (charStorage.GetEntityIndexInQuickslots(candidate) != -1)
					continue; // already bound somewhere

				charStorage.StoreItemToQuickSlot(candidate, slot, true);
				break;
			}
		}
	}

	//--------------------------------------------------------------------------------------------
	//! Cargo containers on the body - pouches, uniform pockets, pack - in a deterministic
	//! order. Storages another pass owns (garment slots, weapon slots and anything mounted
	//! on a weapon, the watch/binocular domain, body-state storages) are not cargo.
	protected static void CollectCargoContainers(IEntity character, RK29_KitStruct kit, notnull array<BaseInventoryStorageComponent> outContainers, notnull array<int> outSlotIds, notnull array<string> outKeys, notnull array<string> outKinds)
	{
		map<string, string> garmentKind = new map<string, string>();
		foreach (string slotName, ResourceName garment : kit.m_mClothing)
			garmentKind.Set(FileOf29(garment), KindOfDressSlot(slotName));

		array<BaseInventoryStorageComponent> all = {};
		CollectBodyStorages(character, all, 0);

		array<BaseInventoryStorageComponent> rawContainers = {};
		array<int> rawSlotIds = {};
		array<string> rawKeys = {};
		array<string> rawKinds = {};
		array<int> rawPrios = {};

		map<string, int> seen = new map<string, int>();
		foreach (BaseInventoryStorageComponent storage : all)
		{
			if (!storage)
				continue;
			if (EquipedLoadoutStorageComponent.Cast(storage) || EquipedWeaponStorageComponent.Cast(storage))
				continue;
			// Nothing owned by the CHARACTER ENTITY is cargo. Real cargo lives in worn garments,
			// which are separate entities - the character's own storages are the hands and the
			// gadget/offhand slot, which report a capacity of 1 and accept anything offered.
			// ChooseContainer scored them like any other container and happily sent the map,
			// compass, bandages and morphine there; capacity 1 means each new item displaced the
			// last, so all but one silently disappeared, and whatever survived got taken into the
			// left hand on spawn. That is the auto-equipped compass. Slots on WORN GEAR (the
			// suspender flashlight strap, a bayonet scabbard) are owned by the garment, so this
			// leaves them alone.
			if (storage.GetOwner() == character)
				continue;
			if (SCR_IdentityItemStorageComponent.Cast(storage)
				|| SCR_SalineStorageComponent.Cast(storage)
				|| SCR_TourniquetStorageComponent.Cast(storage))
				continue;
			if (IsOnWeapon(storage))
				continue;

			string owner = OwnerFileName(storage);
			int ordinal = 0;
			seen.Find(owner, ordinal);
			seen.Set(owner, ordinal + 1);

			string kind = KindOfContainer(storage, garmentKind);

			// equipment storages are NAMED TYPED slots - the flashlight strap on the ALICE
			// suspenders, a scabbard mount. They accept an item only when asked for that
			// specific slot, never as a bag, so each slot becomes its own container here.
			if (EquipmentStorageComponent.Cast(storage))
			{
				for (int s = 0, sn = storage.GetSlotsCount(); s < sn; s++)
				{
					InventoryStorageSlot namedSlot = storage.GetSlot(s);
					if (!namedSlot)
						continue;

					rawContainers.Insert(storage);
					rawSlotIds.Insert(namedSlot.GetID());
					rawKeys.Insert(owner + "#" + ordinal.ToString() + "/" + namedSlot.GetSourceName());
					rawKinds.Insert(kind);
					rawPrios.Insert(storage.GetPriority());
				}
				continue;
			}

			rawContainers.Insert(storage);
			rawSlotIds.Insert(-1);
			rawKeys.Insert(owner + "#" + ordinal.ToString());
			rawKinds.Insert(kind);
			rawPrios.Insert(storage.GetPriority());
		}

		// Highest engine storage Priority first, body-traversal order among equals. Pressing R
		// walks the body the same way and takes the FIRST compatible magazine it finds, so a
		// container early in this list is a container the weapon reaches for early. The solver
		// fills the list in order, which is what leaves the last-declared magazines - the
		// tracers - in the containers the reload search reaches last.
		int nRaw = rawContainers.Count();
		array<bool> emitted = {};
		for (int i = 0; i < nRaw; i++)
			emitted.Insert(false);

		for (int e = 0; e < nRaw; e++)
		{
			int pick = -1;
			for (int i = 0; i < nRaw; i++)
			{
				if (emitted[i])
					continue;
				if (pick == -1 || rawPrios[i] > rawPrios[pick])
					pick = i;
			}
			if (pick == -1)
				break;

			emitted[pick] = true;
			outContainers.Insert(rawContainers[pick]);
			outSlotIds.Insert(rawSlotIds[pick]);
			outKeys.Insert(rawKeys[pick]);
			outKinds.Insert(rawKinds[pick]);
			RK29_Log.Trace(string.Format("[RK29] container %1 priority=%2 %3", e, rawPrios[pick], rawKeys[pick]));
		}
	}

	//--------------------------------------------------------------------------------------------
	protected static string KindOfDressSlot(string slotName)
	{
		if (slotName == "Vest" || slotName == "ArmoredVest")
			return "rig";
		if (slotName == "Back")
			return "pack";
		if (slotName == "Pants")
			return "trouser";
		if (slotName == "Jacket")
			return "uniform";
		return "other";
	}

	//--------------------------------------------------------------------------------------------
	//! A container inherits the kind of the garment it hangs on, so preferences can be
	//! written the way people say them ("bandages in the uniform") instead of by filename.
	protected static string KindOfContainer(BaseInventoryStorageComponent storage, map<string, string> garmentKind)
	{
		// climb by containment, not by entity parent: everything worn is parented to the
		// character, so a mag pouch only finds its vest by asking which storage holds it
		IEntity e = storage.GetOwner();
		int guard = 0;
		while (e && guard < 8)
		{
			string kind;
			if (garmentKind.Find(FileNameOf(e), kind))
				return kind;

			InventoryItemComponent iic = InventoryItemComponent.Cast(e.FindComponent(InventoryItemComponent));
			if (!iic)
				break;
			InventoryStorageSlot parentSlot = iic.GetParentSlot();
			if (!parentSlot || !parentSlot.GetStorage())
				break;

			e = parentSlot.GetStorage().GetOwner();
			guard++;
		}
		return "other";
	}

	//--------------------------------------------------------------------------------------------
	//! Where this kind of item would ideally live, best first. Only a tie-break: feasibility
	//! and scarcity decide first, so a preference can never cost an item its place.
	protected static array<string> PreferredKinds(ResourceName item)
	{
		string path = "" + item;
		array<string> kinds = {};

		// fighting load rides the rig and works outward; everything you only reach for
		// between fights sits the other way round, so the rig stays free for ammo
		bool combat = path.Contains("/Magazines/") || path.Contains("/Ammo/")
			|| path.Contains("/Grenades/") || path.Contains("/Flares/");

		if (path.Contains("/Launchers/") || path.Contains("Ammo_Rocket"))
		{
			kinds.Insert("pack");
			kinds.Insert("rig");
			kinds.Insert("trouser");
			kinds.Insert("uniform");
		}
		else if (combat)
		{
			kinds.Insert("rig");
			kinds.Insert("pack");
			kinds.Insert("trouser");
			kinds.Insert("uniform");
		}
		else
		{
			// medical, gadgets, papers - the reverse walk
			kinds.Insert("uniform");
			kinds.Insert("trouser");
			kinds.Insert("pack");
			kinds.Insert("rig");
		}
		return kinds;
	}

	//--------------------------------------------------------------------------------------------
	//! Constraint-first placement. Eligibility is the engine's own fit test, so no item or
	//! container type knowledge lives here. Items with the fewest homes go first (a belt box
	//! fits two pouches, a bandage fits everything), and among containers that accept an item
	//! we avoid space that still-unplaced scarce items depend on. That is what keeps pistol
	//! magazines out of the belt pouches without anything in the code knowing what either is.
	protected static array<ref RK29_PlanEntry> SolvePlacement(SCR_InventoryStorageManagerComponent manager, array<BaseInventoryStorageComponent> containers, array<int> slotIds, array<string> keys, array<string> kinds, array<ResourceName> items, array<ref array<string>> prefs, array<ResourceName> droppedItems)
	{
		array<ref RK29_PlanEntry> plan = {};
		int nItems = items.Count();
		int nCont = containers.Count();

		array<ref array<int>> eligible = {};
		for (int i = 0; i < nItems; i++)
		{
			array<int> fits = {};
			for (int c = 0; c < nCont; c++)
			{
				if (manager.CanInsertResourceInStorage(items[i], containers[c], slotIds[c]))
					fits.Insert(c);
			}
			eligible.Insert(fits);
		}

		// how many DISTINCT items of this kit a container can take. A container only one
		// item type can use is that item's dedicated home - the flashlight strap on the
		// ALICE suspenders, a bayonet scabbard, a belt-box pouch. Found by asking the
		// engine what fits where, so it needs no list of special slots.
		array<int> acceptCount = {};
		for (int c = 0; c < nCont; c++)
		{
			array<ResourceName> distinct = {};
			for (int i = 0; i < nItems; i++)
			{
				if (eligible[i].Contains(c) && !distinct.Contains(items[i]))
					distinct.Insert(items[i]);
			}
			acceptCount.Insert(distinct.Count());
		}

		// one line per container the first time a kit is solved: what it is, what it hangs
		// on, and how many of this kit's items the ENGINE says it accepts. A named slot that
		// takes exactly one item is a dedicated home; a zero means nothing fits and the
		// container is dead weight.
		for (int c = 0; c < nCont; c++)
			RK29_Log.Trace(string.Format("[RK29] container %1 [%2] slot=%3 accepts=%4", keys[c], kinds[c], slotIds[c], acceptCount[c]));

		// most constrained first; ties by physically largest, which is what container
		// MaxItemSize gates on
		array<int> order = {};
		for (int i = 0; i < nItems; i++)
		{
			int at = order.Count();
			for (int o = 0, on = order.Count(); o < on; o++)
			{
				int j = order[o];
				bool fewer = eligible[i].Count() < eligible[j].Count();
				bool sameButBigger = eligible[i].Count() == eligible[j].Count()
					&& ItemMaxDimension(items[i]) > ItemMaxDimension(items[j]);
				if (fewer || sameButBigger)
				{
					at = o;
					break;
				}
			}
			order.InsertAt(i, at);
		}

		array<bool> placed = {};
		for (int i = 0; i < nItems; i++)
			placed.Insert(false);

		// where each prefab's stack ended up, so the rest of it can follow
		map<ResourceName, int> stackHome = new map<ResourceName, int>();

		foreach (int idx : order)
		{
			int chosen = ChooseContainer(manager, containers, slotIds, keys, kinds, items, prefs, eligible, placed, stackHome, idx);
			if (chosen == -1)
			{
				chosen = EvictAndPlace(manager, containers, slotIds, items[idx], eligible[idx]);
				if (chosen != -1)
					s_aCrammed.Insert(items[idx]);   // nothing wanted it; room was made
			}

			if (chosen == -1)
			{
				droppedItems.Insert(items[idx]);
				Print("[RK29] dropped: " + items[idx], LogLevel.WARNING);
				continue;
			}

			RK29_SpawnCallback cb = new RK29_SpawnCallback();
			if (!manager.TrySpawnPrefabToStorage(items[idx], containers[chosen], slotIds[chosen], cb: cb))
			{
				droppedItems.Insert(items[idx]);
				Print("[RK29] dropped (insert refused): " + items[idx], LogLevel.WARNING);
				continue;
			}

			placed[idx] = true;
			stackHome.Set(items[idx], chosen);
			s_aLastSent.Insert(FileOf29(items[idx]) + " -> " + keys[chosen]);
			RK29_Log.Trace("[RK29] placed: " + FileOf29(items[idx]) + " -> " + keys[chosen]);

			RK29_PlanEntry entry = new RK29_PlanEntry();
			entry.m_sPrefab = items[idx];
			entry.m_sContainerKey = keys[chosen];
			plan.Insert(entry);
		}
		return plan;
	}

	//--------------------------------------------------------------------------------------------
	//! Among containers that accept the item right now: protect space that scarce unplaced
	//! items still need, then honour the category preference.
	protected static int ChooseContainer(SCR_InventoryStorageManagerComponent manager, array<BaseInventoryStorageComponent> containers, array<int> slotIds, array<string> keys, array<string> kinds, array<ResourceName> items, array<ref array<string>> itemPrefs, array<ref array<int>> eligible, array<bool> placed, map<ResourceName, int> stackHome, int idx)
	{
		// how much of this item is still to come - a stack would rather live in one place
		int remaining = 0;
		for (int r = 0, rn = items.Count(); r < rn; r++)
		{
			if (!placed[r] && items[r] == items[idx])
				remaining++;
		}

		int home = -1;
		stackHome.Find(items[idx], home);
		// authored placement wins; otherwise the category default
		array<string> wanted = itemPrefs[idx];
		if (!wanted || wanted.IsEmpty())
			wanted = PreferredKinds(items[idx]);

		// an item with barely anywhere to go is already first in line - it should take the
		// space it needs rather than politely avoiding it, or belt boxes would step aside
		// for each other and end up in the pack
		bool itemIsScarce = eligible[idx].Count() <= 2;

		// magazines pack strictly best-container-first instead of clumping - see StacksTogether()
		bool stacks = StacksTogether(items[idx]);

		int best = -1;
		int bestPenalty = int.MAX;
		int bestTier = int.MAX;
		int bestListed = int.MAX;
		int bestCohesion = int.MAX;
		int bestWhole = int.MAX;
		int bestDetail = int.MAX;

		foreach (int c : eligible[idx])
		{
			if (!manager.CanInsertResourceInStorage(items[idx], containers[c], slotIds[c]))
				continue;

			int penalty = 0;
			if (!itemIsScarce)
			{
				for (int j = 0, n = items.Count(); j < n; j++)
				{
					if (j == idx || placed[j])
						continue;
					if (eligible[j].Count() <= 2 && eligible[j].Contains(c))
						penalty++;
				}
			}

			// a named slot is a purpose-built mount: it costs no cargo volume, so taking
			// it is free capacity rather than a matter of taste. Between two mounts the
			// outer layer wins - a flashlight belongs on the vest where you can see it,
			// not inside the jacket. Cargo containers fall back to authored preference.
			int tier = 1;
			int detail = PreferenceRank(wanted, keys[c], kinds[c]);
			int listed = 1;
			if (detail < 99)
				listed = 0; // somewhere the item was actually said to belong
			if (slotIds[c] != -1)
			{
				tier = 0;
				detail = LayerRank(kinds[c]);
				listed = 0;
			}

			// keep a stack together when nothing more important disagrees: first join the
			// container the stack already lives in, else favour one that can swallow what
			// is left of it. Both sit below scarcity, mounts and preference, so neither can
			// push an item somewhere it should not go. Magazines sit both rules out.
			int cohesion = 1;
			if (stacks && home == c)
				cohesion = 0;

			// keeping a stack whole only outranks preference for SMALL stacks - a pair of
			// tourniquets belongs in one pocket, but seven magazines belong in the mag
			// pouches even when that splits them across two.
			int whole = 1;
			if (stacks
				&& remaining <= COHESION_MAX_STACK
				&& containers[c].GetEstimatedCountFitForResource(items[idx]) >= remaining)
				whole = 0;

			// order of authority, strongest first:
			//   penalty  - never strand a scarce item
			//   tier     - a mount is free capacity
			//   listed   - stay somewhere the item was said to belong
			//   cohesion - join the rest of your stack (magazines: never)
			//   whole    - start a stack where all of it fits (this is what stops two
			//              tourniquets splitting when the trousers could hold both;
			//              magazines: never)
			//   detail   - only then the fine order: uniform before trouser, outer mounts
			// Everything below that is a tie, and a tie goes to the earliest container in the
			// list - which CollectCargoContainers ordered by storage priority.
			bool better = false;
			if (best == -1)
				better = true;
			else if (penalty != bestPenalty)
				better = penalty < bestPenalty;
			else if (tier != bestTier)
				better = tier < bestTier;
			else if (listed != bestListed)
				better = listed < bestListed;
			else if (cohesion != bestCohesion)
				better = cohesion < bestCohesion;
			else if (whole != bestWhole)
				better = whole < bestWhole;
			else if (detail != bestDetail)
				better = detail < bestDetail;

			if (better)
			{
				best = c;
				bestPenalty = penalty;
				bestTier = tier;
				bestListed = listed;
				bestCohesion = cohesion;
				bestWhole = whole;
				bestDetail = detail;
			}
		}
		return best;
	}

	//--------------------------------------------------------------------------------------------
	//! Whether a stack of this item wants to live in one container. Magazines are the one
	//! item class whose ORDER survives the apply and matters afterwards: pressing R walks the
	//! body by storage priority and loads the first compatible magazine it finds, so the kit
	//! has to fill containers strictly best-first in declaration order. Cohesion would break
	//! that - a small last-declared stack (the two tracers) would claim a fresh high-priority
	//! pouch of its own while the ball ahead of it sat in a lower one, and the tracers would
	//! be first up the spout. Medical, gadgets and papers still stack.
	protected static bool StacksTogether(ResourceName item)
	{
		return !("" + item).Contains("/Magazines/");
	}

	//--------------------------------------------------------------------------------------------
	//! Outermost first. Used to pick between competing mounts: what hangs on the rig is
	//! visible and reachable, what sits inside a jacket is neither.
	protected static int LayerRank(string kind)
	{
		if (kind == "rig")
			return 0;
		if (kind == "pack")
			return 1;
		if (kind == "uniform")
			return 2;
		if (kind == "trouser")
			return 3;
		return 4;
	}

	//--------------------------------------------------------------------------------------------
	//! How well a container answers an authored preference list. A token matches a
	//! container kind (uniform, trouser, rig, pack), a named slot (FlashlightSlot, Etool),
	//! or any part of the container's own name (Pouch_ALICE_200rnd_M249, Backpack). Lower
	//! is better; unlisted containers rank last but stay usable.
	protected static int PreferenceRank(array<string> wanted, string containerKey, string kind)
	{
		if (!wanted)
			return 99;

		for (int i = 0, n = wanted.Count(); i < n; i++)
		{
			string token = wanted[i];
			if (token == "")
				continue;
			if (token == kind)
				return i;

			// "owner/slot" pins both halves without depending on the ordinal in the key,
			// so "suspenders/FlashlightSlot" means that mount and not the jacket's
			array<string> parts = {};
			token.Split("/", parts, true);

			bool all = true;
			foreach (string part : parts)
			{
				if (part != "" && !containerKey.Contains(part))
				{
					all = false;
					break;
				}
			}
			if (all)
				return i;
		}
		return 99;
	}

	//--------------------------------------------------------------------------------------------
	//! Last resort before dropping: an item that fits nowhere may still fit once something
	//! smaller vacates a container it did not need. One level deep is plenty at kit scale.
	protected static int EvictAndPlace(SCR_InventoryStorageManagerComponent manager, array<BaseInventoryStorageComponent> containers, array<int> slotIds, ResourceName item, array<int> eligibleSet)
	{
		foreach (int c : eligibleSet)
		{
			array<IEntity> occupants = {};
			containers[c].GetAll(occupants);
			foreach (IEntity occupant : occupants)
			{
				if (!occupant)
					continue;

				for (int d = 0, n = containers.Count(); d < n; d++)
				{
					if (d == c || !manager.CanInsertItemInStorage(occupant, containers[d], slotIds[d]))
						continue;
					if (!manager.TryInsertItemInStorage(occupant, containers[d], slotIds[d]))
						continue;

					if (manager.CanInsertResourceInStorage(item, containers[c], slotIds[c]))
					{
						RK29_Log.Trace("[RK29] evicted " + FileNameOf(occupant) + " to make room for " + FileOf29(item));
						return c;
					}
					break; // the move did not help; inventory is still consistent
				}
			}
		}
		return -1;
	}

	//--------------------------------------------------------------------------------------------
	//! Cached plan: each item goes straight to its solved container. A step that fails (rig
	//! differs from when it was solved) falls back to a live choice for that item only, so a
	//! stale plan degrades per item instead of poisoning the apply.
	protected static void ReplayPlan(SCR_InventoryStorageManagerComponent manager, array<BaseInventoryStorageComponent> containers, array<int> slotIds, array<string> keys, array<ResourceName> items, array<string> hints, array<ref RK29_PlanEntry> plan, array<ResourceName> droppedItems)
	{
		array<int> pending = {};
		for (int i = 0, n = items.Count(); i < n; i++)
			pending.Insert(i);

		foreach (RK29_PlanEntry entry : plan)
		{
			int idx = -1;
			foreach (int p : pending)
			{
				if (items[p] == entry.m_sPrefab)
				{
					idx = p;
					break;
				}
			}
			if (idx == -1)
				continue;

			int c = keys.Find(entry.m_sContainerKey);
			if (c == -1)
				continue;

			RK29_SpawnCallback cb = new RK29_SpawnCallback();
			if (manager.TrySpawnPrefabToStorage(entry.m_sPrefab, containers[c], slotIds[c], cb: cb))
				pending.RemoveItem(idx);
		}

		foreach (int leftover : pending)
		{
			if (!SpawnItemSomewhere(manager, containers, items[leftover], hints[leftover]))
			{
				droppedItems.Insert(items[leftover]);
				Print("[RK29] dropped: " + items[leftover], LogLevel.WARNING);
			}
			else
			{
				s_aCrammed.Insert(items[leftover]);   // the plan had no home for it
			}
		}
	}

	protected static ref map<ResourceName, float> s_mDimCache = new map<ResourceName, float>();

	//--------------------------------------------------------------------------------------------
	//! Largest authored side, in cm. Containers gate on this (MaxItemSize), not on volume,
	//! so it is what decides how scarce an item's eligible space is. Variant prefabs inherit
	//! their dimensions from a base - walk ancestors until found.
	protected static float ItemMaxDimension(ResourceName prefab)
	{
		float dim;
		if (s_mDimCache.Find(prefab, dim))
			return dim;

		Resource res = Resource.Load(prefab);
		if (res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			while (src && dim == 0)
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
						{
							vector dims;
							if (phys.Get("ItemDimensions", dims))
								dim = Math.Max(dims[0], Math.Max(dims[1], dims[2]));
						}
					}
					break;
				}
				src = src.GetAncestor();
			}
		}

		s_mDimCache.Set(prefab, dim);
		return dim;
	}

	//--------------------------------------------------------------------------------------------
	//! Exact authored container first, then anything on the hint path, then engine routing.
	protected static bool SpawnItemSomewhere(SCR_InventoryStorageManagerComponent manager, array<BaseInventoryStorageComponent> storages, ResourceName item, string targetHint)
	{
		bool ok = SpawnItemSomewhereInner(manager, storages, item, targetHint);
		return ok;
	}

	//--------------------------------------------------------------------------------------------
	protected static bool SpawnItemSomewhereInner(SCR_InventoryStorageManagerComponent manager, array<BaseInventoryStorageComponent> storages, ResourceName item, string targetHint)
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
				{
					RK29_Log.Trace("[RK29] placed(hint): " + FileOf29(item) + " -> " + OwnerFileName(storage));
					return true;
				}
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
				{
					RK29_Log.Trace("[RK29] placed(hint2): " + FileOf29(item) + " -> " + fname);
					return true;
				}
			}
		}

		// category preference spreads gear the way the hand-authored kits did, without
		// per-item hints in the configs: try storages whose owner garment matches, in order
		array<string> prefs = PreferredStorageTokens(item);
		foreach (string pref : prefs)
		{
			foreach (BaseInventoryStorageComponent prefStorage : storages)
			{
				if (!prefStorage || !OwnerFileName(prefStorage).Contains(pref))
					continue;
				RK29_SpawnCallback pcb = new RK29_SpawnCallback();
				if (manager.TrySpawnPrefabToStorage(item, prefStorage, -1, cb: pcb))
				{
					RK29_Log.Trace("[RK29] placed(pref): " + FileOf29(item) + " -> " + OwnerFileName(prefStorage));
					return true;
				}
			}
		}

		// No engine-routed deposit here. A null storage lets the engine pick, and the only
		// containers it can reach that the sweep below cannot are the ones we deliberately
		// exclude - the hands and gadget/offhand slot, the identity/saline/tourniquet
		// storages, weapon storages. So its sole distinct ability was to put the last item of
		// an over-stuffed kit somewhere we never want it. CollectBodyStorages already recurses
		// the whole entity tree, and the sweep asks with PURPOSE_ANY rather than
		// PURPOSE_DEPOSIT, so it is the broader request against the same real containers.

		// last resort: walk every real container on the body before calling the item dropped
		foreach (BaseInventoryStorageComponent anyStorage : storages)
		{
			if (!anyStorage)
				continue;
			RK29_SpawnCallback cb = new RK29_SpawnCallback();
			if (manager.TrySpawnPrefabToStorage(item, anyStorage, -1, cb: cb))
			{
				RK29_Log.Trace("[RK29] placed(sweep): " + FileOf29(item) + " -> " + OwnerFileName(anyStorage));
				return true;
			}
		}
		return false;
	}

	//--------------------------------------------------------------------------------------------
	protected static string FileOf29(ResourceName res)
	{
		string raw = "" + res;
		int lastSlash = raw.LastIndexOf("/");
		if (lastSlash >= 0)
			raw = raw.Substring(lastSlash + 1, raw.Length() - lastSlash - 1);
		return raw;
	}

	//--------------------------------------------------------------------------------------------
	//! If the throwable slot is empty and the kit carries grenades, put one there (frag
	//! preferred). Returns the prefab primed so the caller can leave it out of the cargo pass
	//! and keep counts exact. It must NOT be removed from the batches: for a kit with no weapon
	//! options ApplyWeaponChoices hands back the SHARED struct, and editing that would make the
	//! kit lose a grenade on every apply for the rest of the session.
	protected static ResourceName PrimeGrenadeSlot(SCR_InventoryStorageManagerComponent manager, EquipedWeaponStorageComponent weaponStorage, IEntity character, RK29_KitStruct kit)
	{
		if (!weaponStorage)
			return ResourceName.Empty;

		CharacterGrenadeSlotComponent grenadeSlot = CharacterGrenadeSlotComponent.Cast(
			character.FindComponent(CharacterGrenadeSlotComponent));
		if (!grenadeSlot || grenadeSlot.GetWeaponEntity())
			return ResourceName.Empty; // no slot, or already occupied (kit-authored keep)

		RK29_KitItemBatch bestBatch;
		int bestIdx = -1;
		bool bestIsFrag = false;
		foreach (RK29_KitItemBatch batch : kit.m_aItems)
		{
			for (int i = 0, n = batch.m_aPrefabs.Count(); i < n; i++)
			{
				string path = "" + batch.m_aPrefabs[i];
				if (!path.Contains("/Grenades/"))
					continue;
				int slash = path.LastIndexOf("/");
				bool isFrag = path.IndexOfFrom(slash + 1, "Grenade_") == slash + 1;
				if (bestIdx == -1 || (isFrag && !bestIsFrag))
				{
					bestBatch = batch;
					bestIdx = i;
					bestIsFrag = isFrag;
				}
				if (bestIsFrag)
					break;
			}
			if (bestIsFrag)
				break;
		}
		if (bestIdx == -1)
			return ResourceName.Empty;

		ResourceName grenade = bestBatch.m_aPrefabs[bestIdx];
		RK29_SpawnCallback cb = new RK29_SpawnCallback();
		if (!manager.TrySpawnPrefabToStorage(grenade, weaponStorage, -1, cb: cb))
			return ResourceName.Empty;

		RK29_Log.Trace("[RK29] primed grenade slot: " + FileOf29(grenade));
		return grenade;
	}

	//--------------------------------------------------------------------------------------------
	//! Storage-owner filename fragments to try, in order, per item category (by prefab path).
	//! Mirrors how the hand-authored kits spread gear: ammo in mag pouches, medical in
	//! uniform pockets, grenades on the rig, gadgets in pants/jacket.
	protected static array<string> PreferredStorageTokens(ResourceName item)
	{
		string path = "" + item;
		array<string> prefs = {};
		if (path.Contains("/Launchers/") || path.Contains("Ammo_Rocket"))
		{
			// disposable launchers and spare rockets belong in the backpack
			prefs.Insert("Backpack");
			prefs.Insert("Back");
		}
		else if (path.Contains("/Magazines/") || path.Contains("/Ammo/"))
		{
			prefs.Insert("Pouch");
			prefs.Insert("Vest");
		}
		else if (path.Contains("/Medicine/"))
		{
			prefs.Insert("Jacket");
			prefs.Insert("Pants");
		}
		else if (path.Contains("/Grenades/") || path.Contains("/Flares/"))
		{
			prefs.Insert("Vest");
			prefs.Insert("Pouch");
			prefs.Insert("Jacket");
		}
		else if (path.Contains("/Equipment/"))
		{
			prefs.Insert("Pants");
			prefs.Insert("Jacket");
		}
		return prefs;
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
