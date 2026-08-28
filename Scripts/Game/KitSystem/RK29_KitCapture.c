//------------------------------------------------------------------------------------------------
//! Reads a kit's identity off its body prefab - the UIInfo the roster falls back on when the
//! composition sets none. Nothing else: gear is config-owned (RK29_KitCompose, RK29_KitResolve).
//! Entity sources present the fully merged view - inherited components enumerate and their values
//! resolve - so one pass over the prefab layer sees everything; never walk the ancestry here.
//------------------------------------------------------------------------------------------------
class RK29_KitCapture
{
	//------------------------------------------------------------------------------------------------
	static RK29_KitStruct Capture(string kitName, string factionKey, ResourceName kitPrefab)
	{
		Resource res = Resource.Load(kitPrefab);
		if (!res.IsValid())
		{
			Print(string.Format("[RK29] capture FAILED - cannot load %1", kitPrefab),
				LogLevel.ERROR);
			return null;
		}

		IEntitySource src = res.GetResource().ToEntitySource();
		if (!src)
		{
			Print(string.Format("[RK29] capture FAILED - no entity source in %1", kitPrefab),
				LogLevel.ERROR);
			return null;
		}

		RK29_KitStruct kit = new RK29_KitStruct();
		kit.m_sKitName    = kitName;
		kit.m_sFactionKey = factionKey;

		int nComp = src.GetComponentCount();
		for (int i = 0; i < nComp; i++)
		{
			IEntityComponentSource comp = src.GetComponent(i);
			if (!comp || comp.GetClassName() != "SCR_EditableCharacterComponent")
				continue;

			// instance now, while res is alive - containers die with the resource
			BaseContainer infoSrc = comp.GetObject("m_UIInfo");
			if (infoSrc)
				kit.m_UIInfo = SCR_UIInfo.Cast(BaseContainerTools.CreateInstanceFromContainer(infoSrc));
			break;
		}

		if (!kit.m_UIInfo)
			Print(string.Format("[RK29] captured '%1': body has no UIInfo", kitName), LogLevel.NORMAL);

		return kit;
	}
}
