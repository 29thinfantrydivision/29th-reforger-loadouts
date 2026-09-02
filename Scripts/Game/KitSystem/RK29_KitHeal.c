//------------------------------------------------------------------------------------------------
//! Full heal, run on the server when a kit lands on a living body.
//!
//! Runs for a PREROUND re-kit only, so whatever is wrong with the body is staging-area damage - a
//! drop off a wall, a negligent discharge, a medic who stopped bandaging halfway. A clean loadout
//! deserves a clean body. A mid-round re-kit is allowed too but skips this: those wounds are the
//! round's own, and a kit change must not double as a free full heal.
//!
//! Four steps. Each covers something none of the others can:
//!
//!  1. TOURNIQUETS are items, not damage, so no heal can see them - and a tourniquetted limb
//!     counts as 70% damaged for movement and aiming however healthy its hitzones are.
//!  2. FullHeal(false) is the whole medical model in one call: terminate every persistent effect,
//!     drop every damage-over-time, put fires out, restore hitzones. The false is the one
//!     deliberate difference from the Game Master's heal, which takes the default: a GM heals a
//!     casualty who may have saline running and wants to keep it, we are resetting a body that is
//!     about to be re-dressed from config. It also saves enumerating which effects are DOTs and
//!     which are not - bleedings, poison, blisters and tourniquet markers all go the same way.
//!  3. FullHeal only touches a hitzone whose damage STATE is not UNDAMAGED, and the first
//!     threshold is 0.75 on Health and 0.7 on limbs - so on its own it leaves a player at 76%
//!     health with an arm at 71% and calls it done. Health is the DEFAULT hitzone: its 100 points
//!     are the ones that run out when you die, so 76% is 24% less life to spend on the round, and
//!     a limb at 71% sits 20 points nearer the threshold where it starts bleeding and spilling
//!     into the torso. Physical hitzones regenerate over 1200s, so none of that is going away
//!     before the round starts. Top up anything below full.
//!     (Movement and aiming penalties are NOT part of this - UpdateCharacterGroupDamage runs off
//!     OnDamageStateChanged, so sub-threshold limb damage never reaches them. It is the buffer
//!     that is short, not the handling.)
//!  4. Wake up, rather than trusting a hitzone state change to have done it.
//!
//! FullHeal is the only full-heal vanilla has - the GM's heal action, the tutorial's reset and
//! the debug key are its only callers; every other "heal" in the game (support stations,
//! bandages, the editor's remove-bleeding action) is partial and targeted by design. So it is
//! both the reference implementation and the virtual entry point medical mods override, which is
//! what makes routing through it the compatible choice rather than walking hitzones ourselves.
//!
//! Checked module by module against what the 29th actually loads - ACE Medical Core (which
//! carries bleeding, pain and second chance) and ACE Medical Hitzones. None of them overrides
//! FullHeal, so vanilla's body is what runs. Its extra hitzones (pain, neck, hips, heart,
//! femoral artery, organs) are named hitzones on the same damage manager, so both hitzone passes
//! reach them. Its bleeding module rebuilds the blood hitzone's total rate as each bleeding
//! effect is terminated, so that bookkeeping unwinds itself. Its OnDamageStateChanged hooks only
//! fire on the way INTO destroyed, so healing a destroyed hitzone back to full is quiet. And
//! waking the player is the ALIVE transition it clears second-chance history on.
//!
//! ACE Medical Prototypes is not loaded here, but it is the case that proves the rule and the
//! reason not to hand-roll this if the medical stack ever changes: it DOES override FullHeal, to
//! hand its cardiovascular and medication systems an OnFullHeal that resets cardiac output,
//! vascular resistance, blood pressure and vital state. That state lives in components, not
//! hitzones - nothing but this call reaches it. A hitzone walk would leave a "fully healed"
//! player in cardiac arrest.
//!
//! No client permission anywhere: this runs inside the kit RPC handler on the server, which has
//! already vetted faction, weapon and optic.
//------------------------------------------------------------------------------------------------
class RK29_KitHeal
{
	//--------------------------------------------------------------------------------------------
	//! Safe on an undamaged or an unconscious body; leaves a dead one alone.
	static void Heal(notnull IEntity character)
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

	//--------------------------------------------------------------------------------------------
	//! A tourniquet is two pieces of state: the item in its slot, and a flag on the damage manager
	//! that is what everything actually reads. The slot hook keeps the two in step, so deleting
	//! the item is normally the whole job - the sweep after it is for a flag with no item behind
	//! it (the scenario framework can set one directly), and no-ops on a group that is not flagged.
	//!
	//! Deleted rather than handed back the way RemoveTourniquetFromSlot does it: the apply that
	//! follows this heal rebuilds the inventory from config, so a returned tourniquet dies anyway.
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
