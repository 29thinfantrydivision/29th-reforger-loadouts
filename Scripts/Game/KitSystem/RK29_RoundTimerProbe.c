//------------------------------------------------------------------------------------------------
//! Soft Round Timer integration - reads the RT fields off the game mode by name, no dependency.
//! The names mirror the RT source; a rename there silently drops to the config fallback (phase)
//! or hides the countdown (timer fields).
//------------------------------------------------------------------------------------------------
class RK29_RoundTimerProbe
{
	static const int PHASE_NONE     = 0;
	static const int PHASE_BRIEFING = 1;
	static const int PHASE_LIVE     = 2;
	static const int PHASE_ENDED    = 3;

	protected bool m_bActive;
	protected bool m_bResolved;
	protected BaseGameMode m_GameMode;
	protected int m_iIdxPhase     = -1;

	// countdown fields - optional on top of the phase; any missing just hides the HUD timer
	protected int m_iIdxStartTs          = -1;
	protected int m_iIdxDurationS        = -1;
	protected int m_iIdxPaused           = -1;
	protected int m_iIdxPausedRemainingS = -1;

	protected bool m_bReported;

	//--------------------------------------------------------------------------------------------
	void Probe()
	{
		m_bActive       = false;
		m_iIdxPhase     = -1;
		m_iIdxStartTs          = -1;
		m_iIdxDurationS        = -1;
		m_iIdxPaused           = -1;
		m_iIdxPausedRemainingS = -1;

		m_GameMode = GetGame().GetGameMode();
		if (!m_GameMode)
			return;

		typename t = m_GameMode.Type();
		for (int i = 0, n = t.GetVariableCount(); i < n; i++)
		{
			string name = t.GetVariableName(i);
			if (name == "m_eRTPhase")
				m_iIdxPhase = i;
			else if (name == "m_RTPhaseStartTs")
				m_iIdxStartTs = i;
			else if (name == "m_iRTPhaseDurationS")
				m_iIdxDurationS = i;
			else if (name == "m_bRTPaused")
				m_iIdxPaused = i;
			else if (name == "m_iRTPausedRemainingS")
				m_iIdxPausedRemainingS = i;
		}

		// fields are type-level: one look at a live game mode settles it for this world
		m_bResolved = true;
		m_bActive = m_iIdxPhase >= 0;

		if (!m_bReported)
		{
			m_bReported = true;
			if (m_bActive)
			{
				string countdown = "countdown fields resolved";
				if (m_iIdxStartTs < 0 || m_iIdxDurationS < 0 || m_iIdxPaused < 0 || m_iIdxPausedRemainingS < 0)
					countdown = "countdown fields MISSING - HUD clock hidden";
				Print("[RK29] round timer probe ACTIVE - phase field found on " + m_GameMode.Type().ToString() + " | " + countdown, LogLevel.NORMAL);
			}
			else
				Print("[RK29] round timer not present - config fallback in effect", LogLevel.NORMAL);
		}
	}

	//--------------------------------------------------------------------------------------------
	//! Retries only until a game mode exists to inspect.
	void EnsureProbed()
	{
		if (!m_bResolved)
			Probe();
	}

	//--------------------------------------------------------------------------------------------
	int GetPhase()
	{
		if (!m_bActive || !m_GameMode)
			return PHASE_NONE;

		typename t = m_GameMode.Type();
		int phase;
		if (!t.GetVariableValue(m_GameMode, m_iIdxPhase, phase))
			return PHASE_NONE;
		return phase;
	}

	//--------------------------------------------------------------------------------------------
	//! Whole seconds left in the current RT phase, clamped to [0, duration], mirroring the
	//! timer's own display math. -1 when the timer is absent or any field failed to resolve
	//! or read - callers hide the countdown on -1 rather than showing a wrong number.
	int GetRemainingSeconds()
	{
		if (!m_bActive || !m_GameMode)
			return -1;
		if (m_iIdxStartTs < 0 || m_iIdxDurationS < 0 || m_iIdxPaused < 0 || m_iIdxPausedRemainingS < 0)
			return -1;

		typename t = m_GameMode.Type();

		bool paused;
		if (!t.GetVariableValue(m_GameMode, m_iIdxPaused, paused))
			return -1;

		if (paused)
		{
			int pausedRemaining;
			if (!t.GetVariableValue(m_GameMode, m_iIdxPausedRemainingS, pausedRemaining))
				return -1;
			return pausedRemaining;
		}

		int durationS;
		if (!t.GetVariableValue(m_GameMode, m_iIdxDurationS, durationS))
			return -1;

		WorldTimestamp startTs;
		if (!t.GetVariableValue(m_GameMode, m_iIdxStartTs, startTs))
			return -1;

		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		if (!world)
			return -1;

		float remaining = (durationS * 1000 - world.GetServerTimestamp().DiffMilliseconds(startTs)) / 1000;
		if (remaining < 0)
			remaining = 0;
		if (durationS > 0 && remaining > durationS)
			remaining = durationS;
		return remaining;
	}

	//--------------------------------------------------------------------------------------------
	//! Open whenever the round is not LIVE. Timer absent = config fallback.
	bool IsPreround(bool noTimerOpen)
	{
		if (!m_bActive)
			return noTimerOpen;
		return GetPhase() != PHASE_LIVE;
	}

	//--------------------------------------------------------------------------------------------
	//! Strictly the briefing phase. Timer absent = config fallback.
	bool IsBriefing(bool noTimerOpen)
	{
		if (!m_bActive)
			return noTimerOpen;
		return GetPhase() == PHASE_BRIEFING;
	}
}
