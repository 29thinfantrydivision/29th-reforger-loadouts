//------------------------------------------------------------------------------------------------
//! Client world system owning the kit menu keybind. Registered in
//! Configs/Systems/ChimeraSystemsConfig.conf.
//------------------------------------------------------------------------------------------------
class RK29_KitInputSystem extends WorldSystem
{
	//--------------------------------------------------------------------------------------------
	override static void InitInfo(WorldSystemInfo outInfo)
	{
		super.InitInfo(outInfo);
		outInfo
			.SetAbstract(false)
			.SetUnique(true)
			.SetLocation(WorldSystemLocation.Client);
	}

	//--------------------------------------------------------------------------------------------
	override void OnInit()
	{
		RK29_KitPicker.RegisterListeners();
	}
}
