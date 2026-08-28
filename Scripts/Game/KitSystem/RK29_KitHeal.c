//------------------------------------------------------------------------------------------------
//! Full heal, run on the server when a kit lands on a living body. Preround re-kits only - a
//! mid-round kit change must not double as a free full heal. No permission check: this runs
//! inside the kit RPC handler on the server.
//!
//! Order matters, and each step covers what the others cannot:
//! 1. Tourniquets are items, not damage, so no heal can see them - and a tourniquetted limb
//! counts as 70% damaged however healthy its hitzones are.
//! 2. FullHeal(false) is the whole medical model in one call. Route through it rather than
//! walking hitzones: it is the virtual entry point medical mods override (ACE Medical Prototypes
//! resets cardiac output there), so a hitzone walk can leave a "fully healed" player in cardiac
//! arrest.
//! 3. FullHeal only touches a hitzone whose damage state is not UNDAMAGED - thresholds 0.75 on
//! Health, 0.7 on limbs - so alone it leaves a player at 76% health, and physical hitzones
//! regenerate over 1200s. Top up anything below full.
//! 4. Wake up, rather than trusting a hitzone state change to have done it.
//------------------------------------------------------------------------------------------------
class RK29_KitHeal
{
	//------------------------------------------------------------------------------------------------
	//! Safe on an undamaged or an unconscious body; leaves a dead one alone.
	static void Heal_S(notnull IEntity character)
	{
		if (!Replication.IsServer())
			return;

		ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
		if (!chimera)
			return;

		SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(
			chimera.GetDamageManager());
		if (!damage || damage.GetState() == EDamageState.DESTROYED)
			return;

		RemoveTourniquets(character, damage);

		damage.FullHeal(false);

		array<HitZone> hitZones = {};
		damage.GetAllHitZonesInHierarchy(hitZones);
		foreach (HitZone hitZone : hitZones)
		{
			if (hitZone && hitZone.GetHealthScaled() < 1)
				hitZone.SetHealthScaled(1);
		}

		damage.UpdateConsciousness();
	}

	//------------------------------------------------------------------------------------------------
	//! A tourniquet is two pieces of state: the item in its slot, and a flag on the damage manager
	//! that is what everything reads. The slot hook keeps them in step, so the sweep after the delete
	//! catches a flag with no item behind it. Deleted rather than handed back, since the apply
	//! rebuilds the inventory anyway.
	protected static void RemoveTourniquets(notnull IEntity character,
		notnull SCR_CharacterDamageManagerComponent damage)
	{
		SCR_TourniquetStorageComponent storage = SCR_TourniquetStorageComponent.Cast(
			character.FindComponent(SCR_TourniquetStorageComponent));
		SCR_InventoryStorageManagerComponent manager = SCR_InventoryStorageManagerComponent.Cast(
			character.FindComponent(SCR_InventoryStorageManagerComponent));

		if (storage)
		{
			// fixed hitzone-keyed slots - unlike a cargo storage this list does not reshuffle as
			// occupants leave, so deleting while indexing is safe
			for (int i = 0, n = storage.GetSlotsCount(); i < n; i++)
			{
				InventoryStorageSlot slot = storage.GetSlot(i);
				if (!slot)
					continue;
				IEntity tourniquet = slot.GetAttachedEntity();
				if (!tourniquet)
					continue;
				if (manager && manager.TryDeleteItem(tourniquet))
					continue;

				slot.DetachEntity();
				SCR_EntityHelper.DeleteEntityAndChildren(tourniquet);
			}
		}

		array<ECharacterHitZoneGroup> limbs = {};
		SCR_CharacterDamageManagerComponent.GetAllLimbs(limbs);
		foreach (ECharacterHitZoneGroup limb : limbs)
		{
			if (damage.GetGroupTourniquetted(limb))
				damage.SetTourniquettedGroup(limb, false);
		}
	}
}
