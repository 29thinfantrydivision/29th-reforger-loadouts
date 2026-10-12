//------------------------------------------------------------------------------------------------
//! The mod's entries in vanilla's notification feed. Text, icon and colour live in
//! Configs/Notifications/Notifications.conf, which squats the vanilla config's GUID: a GUID'd
//! object array under a bare brace merges with vanilla's list.
//------------------------------------------------------------------------------------------------
modded enum ENotification
{
	//! param1 = player id. Sent to everyone from RK29_KitManager.ApplyWhenHandsFree_S.
	RK29_LIVE_REKIT,

	//! param1 = RK29_EHandsBusy. Local, from SCR_PlayerController.RK29_PollSendKit.
	RK29_KIT_WAITING_HANDS,

	//! No params. Local, for a cancel on either side - see SCR_PlayerController.RK29_ShowApplyCancelled.
	RK29_KIT_NOT_APPLIED,
}

//------------------------------------------------------------------------------------------------
//! %1 = " - <reason>" from param1 as an RK29_EHandsBusy, dash included, so a reason with no
//! honest short phrase leaves the line bare instead of ending on a dangling dash.
[BaseContainerProps(), SCR_BaseContainerCustomTitleEnum(ENotification, "m_NotificationKey")]
class RK29_NotificationHandsBusy : SCR_NotificationDisplayData
{
	//------------------------------------------------------------------------------------------------
	override string GetText(SCR_NotificationData data)
	{
		int reason;
		data.GetParams(reason);
		string text = ReasonText(reason);
		if (text != "")
			text = " - " + text;
		data.SetNotificationTextEntries(text);
		return super.GetText(data);
	}

	//------------------------------------------------------------------------------------------------
	protected static string ReasonText(RK29_EHandsBusy reason)
	{
		switch (reason)
		{
			case RK29_EHandsBusy.RELOADING:     return "reloading";
			case RK29_EHandsBusy.CHANGING_ITEM: return "switching items";
			case RK29_EHandsBusy.USING_ITEM:    return "using an item";
			case RK29_EHandsBusy.GESTURE:       return "finishing an action";
			case RK29_EHandsBusy.VEHICLE_DOOR:  return "getting in or out of a vehicle";
		}
		return "";
	}
}
