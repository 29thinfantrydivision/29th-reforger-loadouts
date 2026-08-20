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
	void RK29_NotifyItemsDropped_S(int count)
	{
		Rpc(RK29_RpcItemsDropped, count);
	}

	//--------------------------------------------------------------------------------------------
	void RK29_NotifyKitSaved_S()
	{
		Rpc(RK29_RpcKitSaved);
	}

	//--------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcKitSaved()
	{
		RK29_KitPicker.MarkLocalStash();
	}

	//--------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcItemsDropped(int count)
	{
		SCR_HintManagerComponent.ShowCustomHint(
			count.ToString() + " item(s) did not fit in your inventory and were left out.",
			"KIT APPLIED", 8);
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
