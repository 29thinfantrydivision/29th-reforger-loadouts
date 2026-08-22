//------------------------------------------------------------------------------------------------
//! Deploy-menu preview for "Current Kit": the mannequin already spawns as the stashed
//! kit's prefab (RK29_CurrentKitLoadout.GetLoadoutResource); this renders the optic
//! choice onto it, local entities only - same technique as the vanilla arsenal branch.
//------------------------------------------------------------------------------------------------
modded class SCR_LoadoutPreviewComponent
{
	//--------------------------------------------------------------------------------------------
	override IEntity SetPreviewedLoadout(notnull SCR_BasePlayerLoadout loadout, PreviewRenderAttributes attributes = null)
	{
		IEntity ent = super.SetPreviewedLoadout(loadout, attributes);
		if (!ent || !RK29_CurrentKitLoadout.Cast(loadout) || !RK29_KitPicker.HasLocalStash())
			return ent;

		EquipedWeaponStorageComponent weaponStorage = EquipedWeaponStorageComponent.Cast(ent.FindComponent(EquipedWeaponStorageComponent));
		if (!weaponStorage)
			return ent;

		// first weapon with an optics slot = the rifle; sidearms have none
		ResourceName optic = RK29_KitPicker.LocalStashOptic();
		for (int i = 0, n = weaponStorage.GetSlotsCount(); i < n; i++)
		{
			InventoryStorageSlot slot = weaponStorage.GetSlot(i);
			if (!slot)
				continue;
			if (RK29_SwapPreviewOptic(slot.GetAttachedEntity(), optic, 0))
				break;
		}
		return ent;
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
