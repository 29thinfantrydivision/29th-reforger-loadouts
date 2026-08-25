//------------------------------------------------------------------------------------------------
//! Deploy-menu preview for "Current Kit": the mannequin spawns as the side's BARE body, then is
//! dressed here from the loadout the server resolved and sent - the same thing the character
//! itself will be dressed from - or, before the player has picked anything, from the composed
//! default kit in the local catalog, which is what the server will dress them with. Local
//! entities only, same technique as the vanilla arsenal branch, which builds its mannequin from
//! m_LocalPlayerLoadoutData rather than from a prefab.
//!
//! The preview entity is CACHED per prefab, and every Current Kit now shares one bare body, so
//! whatever the last kit left on it is still there. Every slot is therefore accounted for on
//! each pass - a slot the kit does not name is emptied, not skipped.
//!
//! Deliberately Current Kit ONLY. A stock deploy row never runs apply, so its body IS what the
//! player will wear - previewing that prefab as-authored is correct, not a bug.
//------------------------------------------------------------------------------------------------
modded class SCR_LoadoutPreviewComponent
{
	//--------------------------------------------------------------------------------------------
	override IEntity SetPreviewedLoadout(notnull SCR_BasePlayerLoadout loadout, PreviewRenderAttributes attributes = null)
	{
		IEntity ent = super.SetPreviewedLoadout(loadout, attributes);
		if (!ent)
			return ent;

		// While the player's stash still names the kit this row resolves to, this is the loadout
		// the SERVER resolved and sent - never re-derived here. That is the whole point: the client used to guess which weapon a
		// class with two options had ended up with, and guessed wrong. Before a first pick
		// there is nothing to send yet, so the resolver falls back to the composed default kit
		// - without it a first-time player hovers Current Kit and sees the bare body.
		map<string, ResourceName> dress = new map<string, ResourceName>();
		map<int, ResourceName> weapons = new map<int, ResourceName>();
		ResourceName optic;
		if (!RK29_StashedLoadoutUIInfo.ResolvePreviewLoadout(loadout, dress, weapons, optic))
			return ent;

		string unresolved;
		int dressed = RK29_DressPreview(ent, dress, unresolved);

		EquipedWeaponStorageComponent weaponStorage = EquipedWeaponStorageComponent.Cast(ent.FindComponent(EquipedWeaponStorageComponent));
		if (!weaponStorage)
			return ent;

		// by slot INDEX, the same space apply uses - no "which of these looks like a rifle".
		// Walk every slot, not just the ones the kit names, or the cached mannequin keeps the
		// previous kit's weapon wherever this one is silent.
		IEntity primary;
		int armed = 0;
		for (int slotIdx = 0, weaponSlots2 = weaponStorage.GetSlotsCount(); slotIdx < weaponSlots2; slotIdx++)
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

			IEntity current = slot.GetAttachedEntity();
			if (current)
			{
				EntityPrefabData epd = current.GetPrefabData();
				if (epd && epd.GetPrefabName() == wanted)
				{
					if (slotIdx == 0)
						primary = current;
					armed++;
					continue;
				}
				delete current;
			}

			if (wanted == ResourceName.Empty)
				continue;

			// an uncached prefab can come back invalid on the first open and load by the second,
			// which is exactly what an intermittently naked mannequin looks like
			Resource res = Resource.Load(wanted);
			if (!res.IsValid())
			{
				unresolved = unresolved + " " + FilePart(wanted);
				continue;
			}

			IEntity spawned = GetGame().SpawnEntityPrefabLocal(res, ent.GetWorld());
			if (!spawned)
			{
				unresolved = unresolved + " spawn-failed:" + FilePart(wanted);
				continue;
			}
			slot.AttachEntity(spawned);
			armed++;
			if (slotIdx == 0)
				primary = spawned;
		}

		if (primary)
		{
			RK29_SwapPreviewOptic(primary, optic, 0);

			// nothing selects a weapon on a freshly built mannequin, so it stands there with
			// the rifle slung. Vanilla's arsenal branch does this off its Active flag.
			BaseWeaponManagerComponent weaponManager = BaseWeaponManagerComponent.Cast(
				ent.FindComponent(BaseWeaponManagerComponent));
			if (weaponManager)
			{
				array<WeaponSlotComponent> weaponSlots = {};
				weaponManager.GetWeaponsSlots(weaponSlots);
				foreach (WeaponSlotComponent ws : weaponSlots)
				{
					if (ws && ws.GetWeaponEntity() == primary)
					{
						weaponManager.SelectWeapon(ws);
						break;
					}
				}
			}
		}

		// Vanilla's arsenal branch dresses the mannequin and only THEN hands it to the widget.
		// super() already presented the bare body, so present it again now that it is dressed -
		// otherwise what the player sees depends on when the widget last sampled the entity.
		if (m_PreviewManager && m_wPreview)
			m_PreviewManager.SetPreviewItem(m_wPreview, ent, attributes, true);

		string report = "[RK29] preview '" + RK29_StashedLoadoutUIInfo.ResolveName(loadout) + "': " + dressed.ToString() + "/" + dress.Count().ToString()
			+ " garment(s), " + armed.ToString() + "/" + weapons.Count().ToString() + " weapon(s)";
		if (unresolved != "")
			report = report + " | UNRESOLVED:" + unresolved;
		Print(report, LogLevel.NORMAL);
		return ent;
	}

	//--------------------------------------------------------------------------------------------
	protected string RK29_ClothingKeys(notnull map<string, ResourceName> dress)
	{
		string keys;
		foreach (string slotName, ResourceName prefab : dress)
			keys = keys + " '" + slotName + "'";
		return keys;
	}

	//--------------------------------------------------------------------------------------------
	//! Re-dress the mannequin from the kit, slot by slot. Matched by slot SOURCE NAME, which is
	//! the same key space the capture reads out of BaseLoadoutManagerComponent.Slots - so a slot
	//! the kit does not mention is one the kit leaves empty, and gets emptied here too.
	//!
	//! AttachEntity returns void, so there is no way to test a fit and back out: an unmatched
	//! name would strip the body and dress it in nothing. Hence the dry run first - if not one
	//! slot name lines up, the assumption is wrong and this leaves the mannequin alone rather
	//! than showing the player a naked soldier.
	protected int RK29_DressPreview(notnull IEntity ent, notnull map<string, ResourceName> dress, out string unresolved)
	{
		EquipedLoadoutStorageComponent loadoutStorage = EquipedLoadoutStorageComponent.Cast(
			ent.FindComponent(EquipedLoadoutStorageComponent));
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
			// Loadout slots are authored as "LoadoutSlotInfo Jacket", so their source names
			// should be the same keys the capture reads - but no vanilla code reads a loadout
			// slot's name, so this is the one assumption here that cannot be checked against
			// the corpus. Say so out loud rather than leaving a mannequin quietly undressed.
			Print("[RK29] preview dress skipped - no loadout slot name matched the kit. Slots saw:"
				+ seen + " | kit wants: " + RK29_ClothingKeys(dress), LogLevel.WARNING);
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

			IEntity current = slot.GetAttachedEntity();
			if (current)
			{
				EntityPrefabData epd = current.GetPrefabData();
				if (epd && epd.GetPrefabName() == wanted)
					continue;

				delete current;
			}

			if (wanted == ResourceName.Empty)
				continue;

			Resource res = Resource.Load(wanted);
			if (!res.IsValid())
			{
				unresolved = unresolved + " " + FilePart(wanted);
				continue;
			}

			IEntity cloth = GetGame().SpawnEntityPrefabLocal(res, ent.GetWorld());
			if (!cloth)
			{
				unresolved = unresolved + " spawn-failed:" + FilePart(wanted);
				continue;
			}
			slot.AttachEntity(cloth);
			dressed++;
		}
		return dressed;
	}

	//--------------------------------------------------------------------------------------------
	protected string FilePart(ResourceName res)
	{
		string s = "" + res;
		int slash = s.LastIndexOf("/");
		if (slash >= 0)
			s = s.Substring(slash + 1, s.Length() - slash - 1);
		return s;
	}

	//--------------------------------------------------------------------------------------------
	protected bool RK29_SwapPreviewOptic(IEntity entity, ResourceName optic, int depth)
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

			AttachmentSlotComponent asc = AttachmentSlotComponent.Cast(slot.GetParentContainer());
			if (asc && asc.GetAttachmentSlotType() && asc.GetAttachmentSlotType().Type().IsInherited(AttachmentOptics))
			{
				IEntity current = slot.GetAttachedEntity();
				if (current)
				{
					EntityPrefabData epd = current.GetPrefabData();
					if (epd && epd.GetPrefabName() == optic)
						return true;
					delete current;
				}

				if (optic == ResourceName.Empty)
					return true;

				Resource res = Resource.Load(optic);
				if (res.IsValid())
				{
					IEntity spawned = GetGame().SpawnEntityPrefabLocal(res, entity.GetWorld());
					if (spawned)
						slot.AttachEntity(spawned);
				}
				return true;
			}

			if (RK29_SwapPreviewOptic(slot.GetAttachedEntity(), optic, depth + 1))
				return true;
		}
		return false;
	}
}
