//------------------------------------------------------------------------------------------------
//! Per-body role UIInfo. One instance per body, mutated on class swap - consumers cache the
//! returned reference, so never replace the instance. Overrides must cover every accessor
//! consumers call or they read blank wrapper defaults.
//------------------------------------------------------------------------------------------------
class RK29_KitUIInfo : SCR_EditableEntityUIInfo
{
	protected ref SCR_UIInfo m_RK29_Delegate;

	string m_sRK29_KitName;

	//--------------------------------------------------------------------------------------------
	static RK29_KitUIInfo RK29_Create(notnull RK29_KitStruct kit)
	{
		RK29_KitUIInfo info = new RK29_KitUIInfo();
		info.RK29_SetKit(kit);
		return info;
	}

	//--------------------------------------------------------------------------------------------
	void RK29_SetKit(notnull RK29_KitStruct kit)
	{
		m_sRK29_KitName = kit.m_sKitName;
		m_RK29_Delegate = kit.m_UIInfo;

		if (!m_RK29_Delegate)
			Print("[RK29] kit '" + kit.m_sKitName + "' has no usable m_UIInfo - icon will be blank", LogLevel.WARNING);
	}

	//--------------------------------------------------------------------------------------------
	override string GetName()
	{
		if (m_RK29_Delegate)
			return m_RK29_Delegate.GetName();
		return super.GetName();
	}

	//--------------------------------------------------------------------------------------------
	override bool SetIconTo(ImageWidget imageWidget)
	{
		if (m_RK29_Delegate)
			return m_RK29_Delegate.SetIconTo(imageWidget);
		return super.SetIconTo(imageWidget);
	}

	//--------------------------------------------------------------------------------------------
	override bool SetNameTo(TextWidget textWidget)
	{
		if (m_RK29_Delegate)
			return m_RK29_Delegate.SetNameTo(textWidget);
		return super.SetNameTo(textWidget);
	}

	//--------------------------------------------------------------------------------------------
	protected SCR_EditableEntityUIInfo RK29_EditableDelegate()
	{
		return SCR_EditableEntityUIInfo.Cast(m_RK29_Delegate);
	}

	//--------------------------------------------------------------------------------------------
	override bool HasEntityLabel(EEditableEntityLabel label)
	{
		SCR_EditableEntityUIInfo d = RK29_EditableDelegate();
		if (d)
			return d.HasEntityLabel(label);
		return super.HasEntityLabel(label);
	}

	//--------------------------------------------------------------------------------------------
	override int GetEntityLabels(out notnull array<EEditableEntityLabel> entityLabels)
	{
		SCR_EditableEntityUIInfo d = RK29_EditableDelegate();
		if (d)
			return d.GetEntityLabels(entityLabels);
		return super.GetEntityLabels(entityLabels);
	}

	//--------------------------------------------------------------------------------------------
	override ResourceName GetImage()
	{
		SCR_EditableEntityUIInfo d = RK29_EditableDelegate();
		if (d)
			return d.GetImage();
		return super.GetImage();
	}

	//--------------------------------------------------------------------------------------------
	override FactionKey GetFactionKey()
	{
		SCR_EditableEntityUIInfo d = RK29_EditableDelegate();
		if (d)
			return d.GetFactionKey();
		return super.GetFactionKey();
	}

	//--------------------------------------------------------------------------------------------
	override EEditableEntityType GetEntityType()
	{
		SCR_EditableEntityUIInfo d = RK29_EditableDelegate();
		if (d)
			return d.GetEntityType();
		return super.GetEntityType();
	}
}
