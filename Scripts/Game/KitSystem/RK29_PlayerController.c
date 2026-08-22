//------------------------------------------------------------------------------------------------
//! Client -> server kit request bridge.
//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController
{
	//--------------------------------------------------------------------------------------------
	//! Empty weapon = authored weapon; empty optic = irons.
	void RK29_RequestKit(string kitName, ResourceName weapon, ResourceName optic)
	{
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
	void RK29_NotifyKitSaved_S(string kitName, ResourceName optic)
	{
		Rpc(RK29_RpcKitSaved, kitName, optic);
	}

	//--------------------------------------------------------------------------------------------
	void RK29_NotifyRestoreState_S(int slotIndex, int stance, float dynStance)
	{
		Rpc(RK29_RpcRestoreState, slotIndex, stance, dynStance);
	}

	//--------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcRestoreState(int slotIndex, int stance, float dynStance)
	{
		// deferred so the swapped weapon entity has replicated in before we draw it
		GetGame().GetCallqueue().CallLater(RK29_RestoreState, 500, false, slotIndex, stance, dynStance);
	}

	//--------------------------------------------------------------------------------------------
	protected void RK29_RestoreState(int slotIndex, int stance, float dynStance)
	{
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(GetControlledEntity());
		if (!character)
			return;
		CharacterControllerComponent ctrl = character.GetCharacterController();
		if (!ctrl)
			return;

		// re-dressing deletes whatever gadget the character was holding and the gadget
		// system latches onto the replacement that arrives - so a kit swap ends with a
		// compass in your hands you never asked for. Put it away; the held WEAPON is
		// restored deliberately below.
		SCR_GadgetManagerComponent gadgets = SCR_GadgetManagerComponent.Cast(
			character.FindComponent(SCR_GadgetManagerComponent));
		if (gadgets && gadgets.GetHeldGadget())
			gadgets.RemoveHeldGadget();

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

		if (slotIndex < 0)
			return;
		BaseWeaponManagerComponent wm = ctrl.GetWeaponManagerComponent();
		if (!wm)
			return;

		array<WeaponSlotComponent> slots = {};
		wm.GetWeaponsSlots(slots);
		foreach (WeaponSlotComponent slot : slots)
		{
			if (slot && slot.GetWeaponSlotIndex() == slotIndex && slot.GetWeaponEntity())
			{
				ctrl.SelectWeapon(slot);
				return;
			}
		}
	}

	//--------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcKitSaved(string kitName, ResourceName optic)
	{
		RK29_KitPicker.MarkLocalStash(kitName, optic);
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
