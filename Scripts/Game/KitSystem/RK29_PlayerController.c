//------------------------------------------------------------------------------------------------
//! Client -> server kit request bridge.
//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController
{
	//--------------------------------------------------------------------------------------------
	//! Empty weapon = authored weapon; empty optic = irons.
	void RK29_RequestKit(string kitName, ResourceName weapon, ResourceName optic)
	{
		// Put a held gadget away BEFORE asking, rather than after the re-dress. A gadget in the
		// hand is engine-attached instead of stored, so it is invisible to the server's strip
		// and survives the re-kit on top of the fresh one the dress pass spawns - two compasses,
		// since utility.conf gives every kit map/compass/flashlight/radio. Stowed here it is an
		// ordinary stored item by the time the request lands and the strip treats it like any
		// other.
		//
		// Straight to the controller, not SCR_GadgetManagerComponent.RemoveHeldGadget(): the
		// manager has no skip-animation entry point at all - SetGadgetMode hardcodes
		// RemoveGadgetFromHand(false) for anything that is not going to the ground - so going
		// through it means watching a putaway play out after you already pressed apply. Same
		// pair vanilla uses to clear the hand before a loiter command
		// (SCR_CharacterControllerComponent.StartLoitering), which is likewise owner-side.
		// The manager stays in sync either way: it learns the gadget left the hand from the
		// controller's own m_OnGadgetStateChangedInvoker, which fires whichever route took it
		// out. The one thing bypassing it costs is closing a raised/ADS'd gadget, and
		// SetGadgetRaisedModeWanted covers that half.
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(GetControlledEntity());
		if (character)
		{
			CharacterControllerComponent ctrl = character.GetCharacterController();
			if (ctrl && ctrl.IsGadgetInHands())
			{
				ctrl.SetGadgetRaisedModeWanted(false);
				ctrl.RemoveGadgetFromHand(true);
			}
		}

		Rpc(RK29_RpcAskKit, kitName, weapon, optic);
	}

	//--------------------------------------------------------------------------------------------
	override void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		super.OnControlledEntityChanged(from, to);

		if (to && GetGame().GetPlayerController() == this)
			RK29_KitPicker.RegisterListeners();
	}

	//--------------------------------------------------------------------------------------------
	void RK29_NotifyItemsDropped_S(int count, string itemList)
	{
		Rpc(RK29_RpcItemsDropped, count, itemList);
	}

	//--------------------------------------------------------------------------------------------
	void RK29_NotifyKitSaved_S(string kitName, ResourceName optic, string loadoutWire)
	{
		Rpc(RK29_RpcKitSaved, kitName, optic, loadoutWire);
	}

	//--------------------------------------------------------------------------------------------
	void RK29_NotifyRestoreState_S(int stance, float dynStance, RplId weaponId)
	{
		Rpc(RK29_RpcRestoreState, stance, dynStance, weaponId);
	}

	//! How long to wait for the re-dressed weapon to replicate in before giving up on drawing it.
	protected static const float RESTORE_TIMEOUT_MS = 500;

	//--------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcRestoreState(int stance, float dynStance, RplId weaponId)
	{
		// Poll per frame instead of sleeping a flat 500ms. The wait is for the re-dressed weapon
		// to replicate in, and how long that takes is a network question - a fixed sleep is
		// either too long (the common case: the weapon is here in a frame or two and the player
		// stands empty-handed for the rest of it) or too short. RESTORE_TIMEOUT_MS is now only
		// the giving-up point, not the normal path.
		float deadline = GetGame().GetWorld().GetWorldTime() + RESTORE_TIMEOUT_MS;
		GetGame().GetCallqueue().Remove(RK29_PollRestore);
		GetGame().GetCallqueue().CallLater(RK29_PollRestore, 0, true, stance, dynStance, weaponId, deadline);
	}

	//--------------------------------------------------------------------------------------------
	//! Per-frame until the weapon the server named exists here, or we run out of patience.
	//! Cheap enough at this duration: one id lookup a frame for at most half a second, once per
	//! kit apply. (Replication.FindItem warns against tight loops - this is not one.)
	//!
	//! An invalid id means nothing was in hands to restore, so there is nothing to wait for and
	//! the restore runs on the first tick. On timeout we simply do not draw: re-drawing is a
	//! convenience, this only ever runs out of combat, and the player can pull the weapon out
	//! themselves. Guessing from the slot instead would risk handing them the outgoing weapon.
	protected void RK29_PollRestore(int stance, float dynStance, RplId weaponId, float deadline)
	{
		IEntity weapon;
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(weaponId));
		if (rpl)
			weapon = rpl.GetEntity();

		if (!weapon && weaponId.IsValid() && GetGame().GetWorld().GetWorldTime() < deadline)
			return;

		GetGame().GetCallqueue().Remove(RK29_PollRestore);
		if (!weapon && weaponId.IsValid())
			Print("[RK29] restore timed out waiting for the re-dressed weapon - not drawing", LogLevel.WARNING);
		RK29_RestoreState(stance, dynStance, weapon);
	}

	//--------------------------------------------------------------------------------------------
	//! `weapon` is the entity the server named, once it exists here. Null means nothing was in
	//! hands to restore, or the wait timed out - either way the right hand is re-seated empty.
	protected void RK29_RestoreState(int stance, float dynStance, IEntity weapon)
	{
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(GetControlledEntity());
		if (!character)
			return;
		CharacterControllerComponent ctrl = character.GetCharacterController();
		if (!ctrl)
			return;

		// No gadget put-away here: RK29_RequestKit clears the hand before the request even
		// leaves. Doing it at this end meant starting an item change one line before the weapon
		// snap below, which is the competing-change class that jams in the first place.
		if (ctrl.GetStance() != stance)
		{
			int change = ECharacterStanceChange.STANCECHANGE_TOERECTED;
			if (stance == ECharacterStance.CROUCH)
				change = ECharacterStanceChange.STANCECHANGE_TOCROUCH;
			else if (stance == ECharacterStance.PRONE)
				change = ECharacterStanceChange.STANCECHANGE_TOPRONE;
			if (ctrl.CanChangeStance(change))
				ctrl.SetStanceChange(change);
		}
		if (ctrl.CanSetDynamicStance(dynStance))
			ctrl.SetDynamicStance(dynStance);

		// The weapon that was in hands got deleted out from under the animation graph, so the
		// item state here is stale whatever we do next. SelectWeapon() re-draws WITH the
		// switching animation, which is a transition out of that stale state - and it is the
		// one that sticks (weapon in hands, wrong pose, no ADS, until you swap manually).
		// TryEquipRightHandItem with swap = no animation at all: the item is snapped into
		// hands and the graph is re-seated instead of transitioned. Same call vanilla's own
		// live re-dress uses after it deletes and respawns a character's whole loadout
		// (SCR_PlayerArsenalLoadout.ApplyCharacterDataLoadoutString).
		//
		// null is not a no-op: an empty-handed player still needs the graph re-seated, since
		// the strip may have deleted the throwable they were holding.
		if (weapon)
			ctrl.TryEquipRightHandItem(weapon, EEquipItemType.EEquipTypeWeapon, true);
		else
			ctrl.TryEquipRightHandItem(null, EEquipItemType.EEquipTypeUnarmedDeliberate, true);

		// Always low ready, whatever the player was doing before. The weapon they were holding
		// no longer exists - re-dressing deletes and respawns it - so "restoring" a raised
		// weapon means drawing a fresh one already aimed, which is how a re-kit ends with a
		// round in a teammate.
		ctrl.SetWeaponRaised(false);
	}

	//--------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcKitSaved(string kitName, ResourceName optic, string loadoutWire)
	{
		RK29_KitPicker.MarkLocalStash(kitName, optic, loadoutWire);
	}

	//--------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcItemsDropped(int count, string itemList)
	{
		string body = count.ToString() + " item(s) did not fit in your inventory and were left out";
		if (itemList != string.Empty)
			body += ":\n" + itemList;
		else
			body += ".";
		SCR_HintManagerComponent.ShowCustomHint(body, "KIT APPLIED", 10);
	}

	//--------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RK29_RpcAskKit(string kitName, ResourceName weapon, ResourceName optic)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
		{
			Print("[RK29] kit request dropped - manager never booted", LogLevel.ERROR);
			return;
		}
		mgr.HandleKitRequest_S(GetPlayerId(), kitName, weapon, optic);
	}
}
