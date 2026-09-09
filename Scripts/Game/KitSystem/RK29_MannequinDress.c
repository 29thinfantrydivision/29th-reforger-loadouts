//------------------------------------------------------------------------------------------------
//! Dressing a preview body - the F4 menu's (RK29_MannequinView) and the deploy row's
//! (RK29_LoadoutPreview). Both stand up their own body and take the one dress below, so the two
//! screens cannot disagree about what a kit looks like. Everything is local:
//! SpawnEntityPrefabLocal, nothing replicated, nothing in anyone's inventory.
//------------------------------------------------------------------------------------------------
class RK29_MannequinDress
{
	//------------------------------------------------------------------------------------------------
	//! The fully loaded dress: the body carries the whole kit, because the menu's weight row asks the
	//! body (RK29_KitWeight.LiveTotal). Placement is handed whole to RK29_KitApply.Place, so the
	//! menu's number and the inventory screen's agree by construction. `loadedMags` and `orders` may
	//! be null; the guns then keep whatever their prefab spawned with.
	//!
	//! The caller must stand a fresh body up for every dress. SelectPrimary binds the weapon to the
	//! hand slot in a way the next strip cannot undo - the strip deletes the entity but never tells
	//! the weapon manager, so DressWeapons is refused slot 0 and every later dress stands one gun
	//! short with the weight row low by its mass. Emptying the hands with TryEquipRightHandItem,
	//! dropping SelectPrimary and SetSlotWeapon(slot, null) were all tried and all failed on a body
	//! nothing controls; the dependency is removed by respawning one level up. Do not try a fourth way.
	static void ApplyLoaded(notnull IEntity body, notnull RK29_KitStruct kit,
		map<int, ref array<ref RK29_LoadedPick>> loadedMags,
		array<ref RK29_AttachmentOrder> orders, string subject,
		out array<ResourceName> outDropped = null)
	{
		array<ResourceName> dropped;
		outDropped = null;
		// quiet, and local off the authority: the mannequin has no replication, so on a client the
		// inventory manager's requests go nowhere - the naked mannequin every non-host player saw.
		// The host is the authority and keeps the manager route. See RK29_KitApply.s_bLocal.
		bool local = !Replication.IsServer();
		if (!RK29_KitApply.Place(body, kit, dropped, loadedMags, orders, true, local))
		{
			Print(string.Format("[RK29] %1: not a soldier - no inventory storage manager on the"
				+ " body", subject), LogLevel.WARNING);
			return;
		}

		IEntity primary = WeaponAt(body, 0);
		if (primary)
			SelectPrimary(body, primary);

		outDropped = dropped;
		ReportLoaded(subject, body, kit, dropped);
	}

	//------------------------------------------------------------------------------------------------
	//! What the loaded dress actually put on the body, counted off the body, with the live weight -
	//! the one figure that must match the player's own inventory screen. Each weapon the kit names
	//! is asked for by its own slot: counts can coincide, and a body one gun short reported "2/2
	//! weapon(s)" for a week. WARNING rather than a blank readout, because WeaponAt reads by list
	//! position while the dress writes by slot ID.
	protected static void ReportLoaded(string subject, notnull IEntity body,
		notnull RK29_KitStruct kit, array<ResourceName> dropped)
	{
		// this loop's tally, never OccupiedSlots: that storage also holds the grenade, so it read
		// one high - and a gun that failed to seat cancelled it out and the row read complete
		int weaponsWanted = 0;
		int weaponsSeated = 0;
		string missing;
		foreach (int slotIdx, ResourceName weapon : kit.m_mWeapons)
		{
			if (weapon == ResourceName.Empty)
				continue;

			weaponsWanted++;

			ResourceName standing;
			IEntity seated = WeaponAt(body, slotIdx);
			if (seated)
			{
				EntityPrefabData epd = seated.GetPrefabData();
				if (epd)
					standing = epd.GetPrefabName();
			}
			if (standing == weapon)
			{
				weaponsSeated++;
				continue;
			}

			missing = missing + " slot" + slotIdx.ToString() + ":" + FilePath.StripPath("" + weapon);
		}

		int dressed = OccupiedSlots(EquipedLoadoutStorageComponent.Cast(
			body.FindComponent(EquipedLoadoutStorageComponent)));

		string suffix = ", " + RK29_KitWeight.WeightLabel(RK29_KitWeight.LiveTotal(body));

		if (dropped && !dropped.IsEmpty())
			suffix = suffix + " | " + dropped.Count().ToString() + " DROPPED";

		LogLevel level = LogLevel.NORMAL;
		if (missing != "")
		{
			// the weight row is read off this body in the same frame and would be shown as whole
			suffix = suffix + " | WEAPON NOT SEATED - the weight below is short by it:" + missing;
			level = LogLevel.WARNING;
		}

		Report(subject, dressed, kit.m_mClothing.Count(), weaponsSeated, weaponsWanted, "",
			suffix, level);
	}

	//------------------------------------------------------------------------------------------------
	protected static int OccupiedSlots(BaseInventoryStorageComponent storage)
	{
		if (!storage)
			return 0;

		int filled = 0;
		for (int i = 0, n = storage.GetSlotsCount(); i < n; i++)
		{
			InventoryStorageSlot slot = storage.GetSlot(i);
			if (slot && slot.GetAttachedEntity())
				filled++;
		}
		return filled;
	}

	//------------------------------------------------------------------------------------------------
	//! The weapon standing in one of the body's weapon slots, asked off the body: a held pointer into
	//! a body that may be deleted and respawned is the one thing a caller must not keep. Body-slot
	//! indices, the same space the kit's weapon map uses. Null for no weapon storage, for an
	//! index outside it and for a slot this kit seats nothing in - all three read as "picture the
	//! prefab instead".
	//! It reads by list position while the dress writes by slot ID, coinciding on every body prefab
	//! we field. A prefab separating them makes the weapon tiles picture the wrong gun; the fix is
	//! to match on slot.GetID() here, not to change what dresses the body.
	static IEntity WeaponAt(notnull IEntity body, int slot)
	{
		EquipedWeaponStorageComponent weaponStorage = EquipedWeaponStorageComponent.Cast(
			body.FindComponent(EquipedWeaponStorageComponent));
		if (!weaponStorage || slot < 0 || slot >= weaponStorage.GetSlotsCount())
			return null;

		InventoryStorageSlot storageSlot = weaponStorage.GetSlot(slot);
		if (!storageSlot)
			return null;

		return storageSlot.GetAttachedEntity();
	}

	//------------------------------------------------------------------------------------------------
	//! The garment worn in one loadout slot of the mannequin, live. WeaponAt's counterpart, and for
	//! the same reason: a tile pointed at the entity rather than at the prefab pictures what is
	//! hanging off it - the optic on the gun, the night vision on the helmet. The lookup is the
	//! apply's own, so the picture cannot disagree with what was dressed.
	static IEntity GarmentAt(notnull IEntity body, string slotName)
	{
		EquipedLoadoutStorageComponent loadoutStorage = EquipedLoadoutStorageComponent.Cast(
			body.FindComponent(EquipedLoadoutStorageComponent));
		if (!loadoutStorage)
			return null;

		return RK29_KitApply.GarmentIn(loadoutStorage, slotName);
	}

	//------------------------------------------------------------------------------------------------
	//! Nothing selects a weapon on a freshly built mannequin, so it stands there with the rifle slung.
	//! It leaves a binding nothing reachable from script can clear, which is why every caller must
	//! stand a fresh body up for each dress - see ApplyLoaded.
	protected static void SelectPrimary(notnull IEntity body, notnull IEntity primary)
	{
		BaseWeaponManagerComponent weaponManager = BaseWeaponManagerComponent.Cast(
			body.FindComponent(BaseWeaponManagerComponent));
		if (!weaponManager)
			return;

		array<WeaponSlotComponent> weaponSlots = {};
		weaponManager.GetWeaponsSlots(weaponSlots);
		foreach (WeaponSlotComponent ws : weaponSlots)
		{
			if (ws && ws.GetWeaponEntity() == primary)
			{
				weaponManager.SelectWeapon(ws);
				return;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One line shape for both passes, or the deploy row's line and the loadout menu's drift apart.
	//! `suffix` is whatever the caller knows and this does not - the live weight, drops, unseated
	//! weapons - appended verbatim, with `level` for a caller whose news is bad.
	protected static void Report(string subject, int dressed, int dressWanted, int armed,
		int weaponsWanted, string unresolved, string suffix = "", LogLevel level = LogLevel.NORMAL)
	{
		string report = "[RK29] " + subject + ": " + dressed.ToString() + "/" + dressWanted.ToString()
			+ " garment(s), " + armed.ToString() + "/" + weaponsWanted.ToString() + " weapon(s)";
		if (unresolved != "")
			report = report + " | UNRESOLVED:" + unresolved;
		Print(report + suffix, level);
	}
}
