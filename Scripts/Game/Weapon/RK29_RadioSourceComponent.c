//------------------------------------------------------------------------------------------------
//! RHS queues Register for the next frame in OnPostInit and cancels it in OnDelete only when
//! Replication.IsServer(). A client that deletes a radio before that frame - RK29_KitApply's
//! MountAccepts fit-test probe does, on the first dress of a kit per session - runs Register on a
//! dead owner and throws at RHS_RadioSourceComponent.c:91 (copied into crash.log). Cancelling on
//! every machine is the server path RHS already has.
//------------------------------------------------------------------------------------------------
modded class RHS_RadioSourceComponent
{
	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(Register);

		super.OnDelete(owner);
	}
}
