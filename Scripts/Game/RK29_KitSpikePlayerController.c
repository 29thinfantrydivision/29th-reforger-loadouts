//------------------------------------------------------------------------------------------------
//! 29th Infantry Division - M1 APPLY SPIKE, client->server bridge (throwaway)
//!
//! Same shape as RT_PlayerController.c: the game mode is server-owned, the player controller is
//! owned by its client, so a Server RPC from here reaches the authority.
//!
//! NO ADMIN GATING. This is a dev spike - anyone can re-kit themselves while it is loaded. Do not
//! ship this file.
//!
//! Additive modded class with RK29_-prefixed members, so it stacks with the round timer mod's own
//! SCR_PlayerController modding.
//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController
{
	//------------------------------------------------------------------------------------------------
	void RK29_RequestApplyKit(int kitIdx)
	{
		Rpc(RK29_RpcAskApplyKit, kitIdx);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RK29_RpcAskApplyKit(int kitIdx)
	{
		Print(string.Format("[RK29Spike] PC RPC AskApplyKit player=%1 kit=%2", GetPlayerId(), kitIdx), LogLevel.NORMAL);

		RK29_KitApplySpike spike = RK29_KitApplySpike.GetInstance();
		if (!spike)
		{
			Print("[RK29Spike] no spike instance - Boot() never ran", LogLevel.ERROR);
			return;
		}

		spike.ApplyKit(GetPlayerId(), kitIdx);
	}

	//------------------------------------------------------------------------------------------------
	void RK29_RequestKitList()
	{
		Rpc(RK29_RpcAskKitList);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RK29_RpcAskKitList()
	{
		RK29_KitApplySpike spike = RK29_KitApplySpike.GetInstance();
		if (!spike)
		{
			Print("[RK29Spike] no spike instance - Boot() never ran", LogLevel.ERROR);
			return;
		}

		Print(string.Format("[RK29Spike] spike holds %1 kit slot(s) - valid indices 0..%2", spike.GetKitCount(), spike.GetKitCount() - 1), LogLevel.NORMAL);
	}
}
