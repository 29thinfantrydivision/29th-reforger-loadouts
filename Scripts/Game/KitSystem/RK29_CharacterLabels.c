//------------------------------------------------------------------------------------------------
//! Makes a kitted body's traits the WHOLE truth about its labels.
//!
//! Vanilla unions the per-instance label list with the ones baked into the prefab, so a body can
//! only ever gain labels, never shed them. That is wrong for a kit system: which body a class
//! spawns on is an implementation detail, and re-kitting medic -> rifleman must actually stop the
//! player being a medic. The flag says "the kit system dressed this body"; anything it did not
//! touch - vanilla characters, GM-placed AI wearing 29th prefabs - keeps vanilla behaviour.
//!
//! Reach is deliberately small: GetAllCharacterLabels feeds only SCR_ChimeraCharacter.HasLabel
//! (every qualified-personnel bonus) and SCR_PlayerArsenalLoadout's save, which 29th kits never
//! use. The Game Master content browser reads labels off the UIInfo by another path entirely.
//------------------------------------------------------------------------------------------------
modded class SCR_EditableCharacterComponent
{
	//! Replicated beside the labels themselves - see RK29_SetTraits_S.
	[RplProp()]
	protected bool m_bRK29_TraitsAuthoritative;

	//--------------------------------------------------------------------------------------------
	//! Authority only. One write and one bump, so the labels and the fact that they are complete
	//! can never arrive apart. An empty list is meaningful: it means this kit grants nothing.
	void RK29_SetTraits_S(notnull array<EEditableEntityLabel> labels)
	{
		m_aCustomInstanceLabels = labels;
		m_bRK29_TraitsAuthoritative = true;
		Replication.BumpMe();
	}

	//--------------------------------------------------------------------------------------------
	//! \return true if this body's labels are kit-owned rather than inherited from its prefab
	bool RK29_TraitsAuthoritative()
	{
		return m_bRK29_TraitsAuthoritative;
	}

	//--------------------------------------------------------------------------------------------
	override bool GetAllCharacterLabels(notnull out array<EEditableEntityLabel> labels)
	{
		if (!m_bRK29_TraitsAuthoritative)
			return super.GetAllCharacterLabels(labels);

		GetCustomCharacterLabels(labels);
		return !labels.IsEmpty();
	}
}
