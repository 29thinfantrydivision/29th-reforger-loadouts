//------------------------------------------------------------------------------------------------
//! Mid-round re-kit announcement.
//!
//! A live re-kit is allowed at any point in the round. Once the round is LIVE the server
//! announces it instead of refusing it, so the whole server can see who re-dressed. The line
//! goes through vanilla's notification feed - SCR_NotificationsComponent, the same log the kill
//! feed writes to - because that feed is the one thing every player already watches, and it
//! only ever arrives by the server's own RPC, so nothing here trusts the client.
//!
//! Only the player is named, so vanilla's own SCR_NotificationPlayer display data does the
//! rendering (param1 = player id). Text, icon and colour live in
//! Configs/Notifications/Notifications.conf - a file that squats the vanilla config's GUID so
//! this one entry merges into vanilla's list (a GUID'd object array under a bare brace merges,
//! never replaces; RHS ships its notifications the same way).
//------------------------------------------------------------------------------------------------
modded enum ENotification
{
	//! param1 = player id. Sent to everyone from RK29_KitManager.ApplyWhenHandsFree_S.
	RK29_LIVE_REKIT,
}
