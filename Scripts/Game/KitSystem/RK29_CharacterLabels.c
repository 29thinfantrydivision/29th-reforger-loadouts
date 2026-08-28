//------------------------------------------------------------------------------------------------
//! Makes a kitted body's traits the whole truth about its labels. Vanilla unions the per-instance
//! label list with the ones baked into the prefab, so a body can only ever gain labels -
//! re-kitting medic to rifleman would leave the player a medic. The flag scopes that to bodies
//! the kit system dressed. Reach is small: only SCR_ChimeraCharacter.HasLabel and
//! SCR_PlayerArsenalLoadout's save read these labels.
//! Its second job: this is the owner of the replicated m_iRK29_KitIndex channel, the body-carried
//! index every machine's local UIInfo stamp hangs off.
//------------------------------------------------------------------------------------------------
modded class SCR_EditableCharacterComponent
{
	//! Replicated beside the labels themselves - see RK29_SetTraits_S.
	[RplProp()]
	protected bool m_bRK29_TraitsAuthoritative;

	//! Index into RK29_KitManager's kit index space (KitNameAt), the space the counts use too. It rides
	//! the body because SetInfoInstance is a local call - every machine has to stamp for itself - and
	//! an entity field cannot arrive later than the entity it describes. A proxy gets it two ways: in
	//! the body's own snapshot when it streams in (RplLoad), and through onRplName when it changes
	//! while the body is here.
	[RplProp(onRplName: "RK29_OnKitIndexChanged")]
	protected int m_iRK29_KitIndex = -1;

	//------------------------------------------------------------------------------------------------
	//! Authority only. One write and one bump, so the labels and the fact that they are complete can
	//! never arrive apart. An empty list means the kit grants nothing; kitIndex -1 means no kit is
	//! stamped here. The sole owner of the body stamp on the authority, and also the path a stock
	//! spawn takes via RK29_KitApply.ApplyTraits_S.
	void RK29_SetTraits_S(notnull array<EEditableEntityLabel> labels, int kitIndex)
	{
		m_aCustomInstanceLabels = labels;
		m_bRK29_TraitsAuthoritative = true;
		m_iRK29_KitIndex = kitIndex;
		Replication.BumpMe();

		// the authority gets no Rpl callback for its own write
		RK29_OnKitIndexChanged();
	}

	//------------------------------------------------------------------------------------------------
	//! The stamp on every path: the authority after its own write, onRplName for a change, RplLoad
	//! for a body that streams in already kitted. onRplName is promised for updates only, never for
	//! the state a proxy is created with - which is why the snapshot carries the index as well.
	void RK29_OnKitIndexChanged()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (mgr)
			mgr.StampEditableByIndex(this, m_iRK29_KitIndex);
	}

	//------------------------------------------------------------------------------------------------
	//! -1 until a kit has been applied to this body.
	int RK29_GetKitIndex()
	{
		return m_iRK29_KitIndex;
	}

	//------------------------------------------------------------------------------------------------
	//! Written ahead of vanilla's data so the body already wears its kit identity when
	//! super.RplLoad registers it with the editor core. The order must match RplLoad exactly.
	override bool RplSave(ScriptBitWriter writer)
	{
		writer.WriteInt(m_iRK29_KitIndex);
		return super.RplSave(writer);
	}

	//------------------------------------------------------------------------------------------------
	override bool RplLoad(ScriptBitReader reader)
	{
		if (!reader.ReadInt(m_iRK29_KitIndex))
			return false;

		RK29_OnKitIndexChanged();
		return super.RplLoad(reader);
	}

	//------------------------------------------------------------------------------------------------
	override bool GetAllCharacterLabels(notnull out array<EEditableEntityLabel> labels)
	{
		if (!m_bRK29_TraitsAuthoritative)
			return super.GetAllCharacterLabels(labels);

		GetCustomCharacterLabels(labels);
		return !labels.IsEmpty();
	}
}
