//------------------------------------------------------------------------------------------------
//! Per-kit role UIInfo, created and owned by RK29_KitManager (m_mKitInfos) and shared by every
//! body wearing that kit - SetInfoInstance stores only a weak reference, so an instance nobody
//! holds dies on the spot. Never mutated once built, so consumers may cache it. Every accessor a
//! consumer calls must be overridden or it reads blank wrapper defaults.
//------------------------------------------------------------------------------------------------
class RK29_KitUIInfo : SCR_EditableEntityUIInfo
{
	protected ref SCR_UIInfo m_RK29_Delegate;

	//------------------------------------------------------------------------------------------------
	static RK29_KitUIInfo RK29_Create(notnull RK29_KitStruct kit)
	{
		RK29_KitUIInfo info = new RK29_KitUIInfo();
		info.m_RK29_Delegate = kit.m_UIInfo;

		if (!info.m_RK29_Delegate)
			Print(string.Format("[RK29] kit '%1' has no usable m_UIInfo - icon will be blank",
				kit.m_sKitName), LogLevel.WARNING);

		return info;
	}

	//------------------------------------------------------------------------------------------------
	override string GetName()
	{
		if (m_RK29_Delegate)
			return m_RK29_Delegate.GetName();
		return super.GetName();
	}

	//------------------------------------------------------------------------------------------------
	override bool SetIconTo(ImageWidget imageWidget)
	{
		if (m_RK29_Delegate)
			return m_RK29_Delegate.SetIconTo(imageWidget);
		return super.SetIconTo(imageWidget);
	}

	//------------------------------------------------------------------------------------------------
	override bool SetNameTo(TextWidget textWidget)
	{
		if (m_RK29_Delegate)
			return m_RK29_Delegate.SetNameTo(textWidget);
		return super.SetNameTo(textWidget);
	}

	//------------------------------------------------------------------------------------------------
	protected SCR_EditableEntityUIInfo RK29_EditableDelegate()
	{
		return SCR_EditableEntityUIInfo.Cast(m_RK29_Delegate);
	}

	//------------------------------------------------------------------------------------------------
	override bool HasEntityLabel(EEditableEntityLabel label)
	{
		SCR_EditableEntityUIInfo d = RK29_EditableDelegate();
		if (d)
			return d.HasEntityLabel(label);
		return super.HasEntityLabel(label);
	}

	//------------------------------------------------------------------------------------------------
	override int GetEntityLabels(out notnull array<EEditableEntityLabel> entityLabels)
	{
		SCR_EditableEntityUIInfo d = RK29_EditableDelegate();
		if (d)
			return d.GetEntityLabels(entityLabels);
		return super.GetEntityLabels(entityLabels);
	}

	//------------------------------------------------------------------------------------------------
	override ResourceName GetImage()
	{
		SCR_EditableEntityUIInfo d = RK29_EditableDelegate();
		if (d)
			return d.GetImage();
		return super.GetImage();
	}

	//------------------------------------------------------------------------------------------------
	override FactionKey GetFactionKey()
	{
		SCR_EditableEntityUIInfo d = RK29_EditableDelegate();
		if (d)
			return d.GetFactionKey();
		return super.GetFactionKey();
	}

	//------------------------------------------------------------------------------------------------
	//! The three budget accessors must all be overridden or the editor budget sweep crashes:
	//! SCR_EditableEntityUIInfo keeps its budget arrays private and materialises them from
	//! attributes, so an instance built with new (as this one is) has NULL ones and MergeBudgetCosts
	//! foreaches unguarded. Private means a subclass cannot fill them in.
	override bool GetEntityBudgetCost(out notnull array<ref SCR_EntityBudgetValue> outBudgets)
	{
		SCR_EditableEntityUIInfo d = RK29_EditableDelegate();
		if (d)
			return d.GetEntityBudgetCost(outBudgets);

		// false means "cost me by entity type", which is what an unbudgeted role should read as
		return false;
	}

	//------------------------------------------------------------------------------------------------
	override void GetEntityAndChildrenBudgetCost(out notnull array<ref SCR_EntityBudgetValue> outBudgets)
	{
		SCR_EditableEntityUIInfo d = RK29_EditableDelegate();
		if (d)
			d.GetEntityAndChildrenBudgetCost(outBudgets);
	}

	//------------------------------------------------------------------------------------------------
	override void GetEntityChildrenBudgetCost(out notnull array<ref SCR_EntityBudgetValue> outBudgets)
	{
		SCR_EditableEntityUIInfo d = RK29_EditableDelegate();
		if (d)
			d.GetEntityChildrenBudgetCost(outBudgets);
	}

	//------------------------------------------------------------------------------------------------
	override EEditableEntityType GetEntityType()
	{
		SCR_EditableEntityUIInfo d = RK29_EditableDelegate();
		if (d)
			return d.GetEntityType();
		return super.GetEntityType();
	}
}
