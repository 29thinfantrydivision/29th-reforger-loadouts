//------------------------------------------------------------------------------------------------
//! Verbosity switch for the kit system's per-item tracing: Trace() prints only when
//! m_bVerboseLogging is set in RK29_KitSetup.conf. Warnings and one-per-apply lines print
//! unconditionally - a briefing applies kits for the whole roster at once.
//------------------------------------------------------------------------------------------------
class RK29_Log
{
	//! set from RK29_KitSetup at manager init
	static bool s_bVerbose;

	//------------------------------------------------------------------------------------------------
	static void Trace(string msg)
	{
		if (s_bVerbose)
			Print(msg, LogLevel.NORMAL);
	}
}
