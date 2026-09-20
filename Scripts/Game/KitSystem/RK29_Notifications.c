//------------------------------------------------------------------------------------------------
//! Mid-round re-kit announcement, sent by the server through vanilla's notification feed.
//! Text, icon and colour live in Configs/Notifications/Notifications.conf, which squats the
//! vanilla config's GUID: a GUID'd object array under a bare brace merges with vanilla's list.
//------------------------------------------------------------------------------------------------
modded enum ENotification
{
	//! param1 = player id. Sent to everyone from RK29_KitManager.ApplyWhenHandsFree_S.
	RK29_LIVE_REKIT,
}
