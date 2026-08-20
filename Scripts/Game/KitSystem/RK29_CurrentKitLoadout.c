//------------------------------------------------------------------------------------------------
//! "Current Kit" deploy-menu entry - respawns the player's stashed picker selection.
//! Resource is a placeholder body; the real kit is applied on spawn by the manager.
//------------------------------------------------------------------------------------------------
[BaseContainerProps(configRoot: true), BaseContainerCustomTitleField("m_sLoadoutName")]
class RK29_CurrentKitLoadout : SCR_FactionPlayerLoadout
{
	//--------------------------------------------------------------------------------------------
	override bool IsLoadoutAvailable(int playerId)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		return mgr && mgr.StashOfferedKit(playerId) != "";
	}

	//--------------------------------------------------------------------------------------------
	override bool IsLoadoutAvailableClient()
	{
		return RK29_KitPicker.HasLocalStash();
	}
}
