//------------------------------------------------------------------------------------------------
//! Dressing a preview body - the deploy row's (RK29_LoadoutPreview) and the F4 menu's
//! (RK29_MannequinView). Everything is local: SpawnEntityPrefabLocal, nothing replicated, nothing
//! in anyone's inventory. Every slot is accounted for on every pass - a slot the kit does not name
//! is emptied - because the deploy row hands back vanilla's shared cached preview entity.
//------------------------------------------------------------------------------------------------
class RK29_MannequinDress
{
	//------------------------------------------------------------------------------------------------
	//! Dress the body, arm it, sight the primary and shoulder it. Only the primary's sight is seated
	//! here - a caller holding the resolver's per-weapon orders dresses through ApplyLoaded instead.
	//! A sight is its own seat probe, so it goes on through the same seat model an attachment order
	//! uses. A body with no weapon storage returns without reporting: it was never a soldier.
	static void Apply(notnull IEntity body, notnull map<string, ResourceName> dress,
		notnull map<int, ResourceName> weapons, ResourceName optic, string subject)
	{
		string unresolved;
		int dressed = Dress(body, dress, unresolved);

		IEntity primary;
		int armed = Arm(body, weapons, primary, unresolved);
		if (armed < 0)
			return;

		if (primary)
		{
			ApplyOrder(primary, optic, optic);
			SelectPrimary(body, primary);
		}

		Report(subject, dressed, dress.Count(), armed, weapons.Count(), unresolved);
	}

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
		if (!RK29_KitApply.Place(body, kit, dropped, loadedMags, orders, true))
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
	//! Re-dress slot by slot, matched by slot source name - the key space the kit capture reads out
	//! of BaseLoadoutManagerComponent.Slots. AttachEntity returns void, so a fit cannot be tested and
	//! backed out of: hence the dry run, or an unmatched name set dresses the body in nothing.
	protected static int Dress(notnull IEntity body, notnull map<string, ResourceName> dress, inout string unresolved)
	{
		EquipedLoadoutStorageComponent loadoutStorage = EquipedLoadoutStorageComponent.Cast(
			body.FindComponent(EquipedLoadoutStorageComponent));
		if (!loadoutStorage)
		{
			unresolved = unresolved + " no-loadout-storage";
			return 0;
		}

		int slotCount = loadoutStorage.GetSlotsCount();
		int recognised = 0;
		string seen;
		for (int probe = 0; probe < slotCount; probe++)
		{
			InventoryStorageSlot slot = loadoutStorage.GetSlot(probe);
			if (!slot)
				continue;
			seen = seen + " '" + slot.GetSourceName() + "'";
			ResourceName known;
			if (dress.Find(slot.GetSourceName(), known))
				recognised++;
		}
		if (recognised == 0)
		{
			// no vanilla code reads a loadout slot's name, so "source name == capture key" is the
			// one assumption here that cannot be checked against the corpus - say so out loud
			Print(string.Format("[RK29] preview dress skipped - no loadout slot name matched the"
				+ " kit. Slots saw:%1 | kit wants: %2", seen, ClothingKeys(dress)),
				LogLevel.WARNING);
			return 0;
		}

		int dressed = 0;

		for (int i = 0; i < slotCount; i++)
		{
			InventoryStorageSlot slot = loadoutStorage.GetSlot(i);
			if (!slot)
				continue;

			ResourceName wanted;
			dress.Find(slot.GetSourceName(), wanted);

			if (SeatWanted(body, slot, wanted, unresolved))
				dressed++;
		}
		return dressed;
	}

	//------------------------------------------------------------------------------------------------
	//! One slot reconciled against the prefab the kit wants in it: keep what is already right,
	//! delete what is not, spawn what is missing. Returns whatever stands in the slot afterwards;
	//! null for a slot the kit leaves empty and for a prefab that would not load or spawn, both
	//! named in `unresolved`. Keeping what is right is not an optimisation - a fully-correct
	//! re-hover that skipped its kept garments reported 0/7, and respawning them flickers the
	//! render. Deleted with children, or a sight seated by an earlier hover is left loose in the world.
	protected static IEntity SeatWanted(notnull IEntity body, notnull InventoryStorageSlot slot,
		ResourceName wanted, inout string unresolved)
	{
		IEntity current = slot.GetAttachedEntity();
		if (current)
		{
			EntityPrefabData epd = current.GetPrefabData();
			if (epd && epd.GetPrefabName() == wanted)
				return current;

			SCR_EntityHelper.DeleteEntityAndChildren(current);
		}

		if (wanted == ResourceName.Empty)
			return null;

		// an uncached prefab can be invalid on the first open and load by the second - the
		// intermittently naked mannequin
		Resource res = Resource.Load(wanted);
		if (!res.IsValid())
		{
			unresolved = unresolved + " " + FilePath.StripPath("" + wanted);
			return null;
		}

		IEntity spawned = GetGame().SpawnEntityPrefabLocal(res, body.GetWorld());
		if (!spawned)
		{
			unresolved = unresolved + " spawn-failed:" + FilePath.StripPath("" + wanted);
			return null;
		}

		slot.AttachEntity(spawned);
		return spawned;
	}

	//------------------------------------------------------------------------------------------------
	//! Seat the kit's weapons by slot index - the same space the apply pass uses. Every slot is
	//! walked, not just the ones the kit names, or a cached body keeps the previous kit's weapon
	//! wherever this one is silent. Returns -1, and reports nothing, for an entity with no weapon
	//! storage: that is not a soldier.
	protected static int Arm(notnull IEntity body, notnull map<int, ResourceName> weapons, out IEntity outPrimary,
		inout string unresolved)
	{
		outPrimary = null;

		EquipedWeaponStorageComponent weaponStorage = EquipedWeaponStorageComponent.Cast(
			body.FindComponent(EquipedWeaponStorageComponent));
		if (!weaponStorage)
			return -1;

		int armed = 0;
		for (int slotIdx = 0, slotCount = weaponStorage.GetSlotsCount(); slotIdx < slotCount; slotIdx++)
		{
			ResourceName wanted;
			weapons.Find(slotIdx, wanted);

			InventoryStorageSlot slot = weaponStorage.GetSlot(slotIdx);
			if (!slot)
			{
				if (wanted != ResourceName.Empty)
					unresolved = unresolved + " no-slot-" + slotIdx.ToString();
				continue;
			}

			IEntity seated = SeatWanted(body, slot, wanted, unresolved);
			if (!seated)
				continue;

			armed++;
			if (slotIdx == 0)
				outPrimary = seated;
		}

		return armed;
	}

	//------------------------------------------------------------------------------------------------
	//! The weapon standing in one of the body's weapon slots, asked off the body: a held pointer into
	//! a body that may be deleted and respawned is the one thing a caller must not keep. Body-slot
	//! indices, the same space Arm and the kit's weapon map use. Null for no weapon storage, for an
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
	//! Nothing selects a weapon on a freshly built mannequin, so it stands there with the rifle slung.
	//! It leaves a binding nothing can clear, and each caller earns the right to it a different way.
	//! Apply is safe on a cached body because Arm re-arms with slot-level calls the storage manager
	//! never sees and so cannot refuse (vanilla's SCR_LoadoutPreviewComponent.SetPreviewedLoadout
	//! does the same). ApplyLoaded re-arms through RK29_KitApply.DressWeapons, which is refused, so
	//! it must respawn its body for every dress - see ApplyLoaded.
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
	//! One attachment order on the mannequin - the preview's mirror of RK29_KitApply.ApplyAttachment,
	//! spawning locally instead of routing through an inventory manager. An empty `prefab` is the
	//! order to empty the seat, not a no-op; `probe` names that seat (a None borrows one from its
	//! group's contents, a probe-less order falls back to the sight seat by type). The duplicate test
	//! is RK29_KitApply.HasAttachment, the wider one: the seat search prefers an empty seat to the
	//! occupied one already holding the thing, so a cached body with two compatible seats would gain
	//! a second copy on every re-hover. The seat itself is RK29_KitApply.DecideSeatAt over
	//! SeatForProbe - the same reading the apply makes.
	//!
	//! Two deliberate divergences from the live path, both in the direction of showing less rather
	//! than guessing: RK29_KitApply.InsertAttachment falls back to letting the engine route an
	//! attachment with no matching seat into any of the weapon's attachment storages, where this
	//! gives up on a seatless order; and the live path puts the displaced occupant back when the new
	//! attachment will not fit, where this has already deleted it and does not.
	protected static bool ApplyOrder(notnull IEntity weapon, ResourceName prefab, ResourceName probe)
	{
		if (prefab != ResourceName.Empty && RK29_KitApply.HasAttachment(weapon, prefab))
			return true;

		RK29_SeatDecision decision = RK29_KitApply.DecideSeatAt(SeatForProbe(weapon, probe));
		if (!decision.m_Seat)
			return false;

		if (decision.m_Occupant)
			SCR_EntityHelper.DeleteEntityAndChildren(decision.m_Occupant);

		if (prefab == ResourceName.Empty)
			return true;

		// trace, not warning: this pass runs on every pick change and the same misconfiguration
		// warns once, for real, from RK29_KitApply.ApplyAttachment
		Resource res = Resource.Load(prefab);
		if (!res.IsValid())
		{
			RK29_Log.Trace(string.Format("[RK29] preview: attachment did not load - %1", prefab));
			return false;
		}

		IEntity spawned = GetGame().SpawnEntityPrefabLocal(res, weapon.GetWorld());
		if (!spawned)
		{
			RK29_Log.Trace(string.Format("[RK29] preview: attachment spawn failed - %1", prefab));
			return false;
		}

		// AttachEntity returns nothing and no vanilla code reads the seat back, so nor does this:
		// a wrong read would delete a good sight
		decision.m_Seat.AttachEntity(spawned);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! RK29_KitApply.SeatFor's answer, plus one last fallback: a probe naming a seat this gun does not
	//! have means "evict nothing" on the apply side, where the preview would rather show the sight
	//! seat than nothing at all.
	protected static InventoryStorageSlot SeatForProbe(notnull IEntity weapon, ResourceName probe)
	{
		InventoryStorageSlot seat = RK29_KitApply.SeatFor(weapon, ResourceName.Empty, probe);
		if (seat)
			return seat;

		return RK29_KitApply.OpticsSeatByType(weapon);
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

	//------------------------------------------------------------------------------------------------
	protected static string ClothingKeys(notnull map<string, ResourceName> dress)
	{
		string keys;
		foreach (string slotName, ResourceName prefab : dress)
			keys = keys + " '" + slotName + "'";
		return keys;
	}
}
