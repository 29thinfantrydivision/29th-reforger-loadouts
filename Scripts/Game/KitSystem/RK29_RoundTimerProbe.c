//------------------------------------------------------------------------------------------------
//! Soft Round Timer integration - reads the RT phase field off the game mode by name, no dependency.
//! The name mirrors the RT source; a rename there silently drops to the config fallback.
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

	protected bool m_bReported;

	//--------------------------------------------------------------------------------------------
	void Probe()
	{
		m_bActive       = false;
		m_iIdxPhase     = -1;

		m_GameMode = GetGame().GetGameMode();
		if (!m_GameMode)
			return;

		typename t = m_GameMode.Type();
		for (int i = 0, n = t.GetVariableCount(); i < n; i++)
		{
			string name = t.GetVariableName(i);
			if (name == "m_eRTPhase")
			{
				m_iIdxPhase = i;
				break;
			}
		}

		// fields are type-level: one look at a live game mode settles it for this world
		m_bResolved = true;
		m_bActive = m_iIdxPhase >= 0;

		if (!m_bReported)
		{
			m_bReported = true;
			if (m_bActive)
				Print("[RK29] round timer probe ACTIVE - phase field found on " + m_GameMode.Type().ToString(), LogLevel.NORMAL);
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
