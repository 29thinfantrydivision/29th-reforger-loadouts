modded class SCR_LoadoutButton
{
	//------------------------------------------------------------------------------------------------
	override SCR_EditableEntityUIInfo GetUIInfo()
	{
		SCR_EditableEntityUIInfo stashed = RK29_StashedLoadoutUIInfo.Resolve(GetLoadout());
		if (stashed)
			return stashed;
		return super.GetUIInfo();
	}
}
