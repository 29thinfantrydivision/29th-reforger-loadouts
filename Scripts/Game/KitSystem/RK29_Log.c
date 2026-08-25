//------------------------------------------------------------------------------------------------
//! Verbosity switch for the kit system's per-item tracing.
//!
//! A single kit apply narrates roughly a hundred lines - every slot stripped, every container
//! weighed, every item placed. That detail is what made the duplicate-item and placement bugs
//! findable, and it is worth keeping. It is NOT worth printing on a live server, where a
//! briefing applies kits for the whole roster at once. Trace lines therefore stay silent
//! unless m_bVerboseLogging is set in RK29_KitSetup.conf; warnings and one-per-apply lines
//! print unconditionally, because those are what a support ticket needs.
//------------------------------------------------------------------------------------------------
class RK29_Log
{
	//! set from RK29_KitSetup at manager init
	static bool s_bVerbose;

	//--------------------------------------------------------------------------------------------
	static void Trace(string msg)
	{
		if (s_bVerbose)
			Print(msg, LogLevel.NORMAL);
	}
}
