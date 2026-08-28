//------------------------------------------------------------------------------------------------
//! Client world system owning the kit menu keybind. Registered in ChimeraSystemsConfig.conf.
//------------------------------------------------------------------------------------------------
class RK29_KitInputSystem extends WorldSystem
{
	//------------------------------------------------------------------------------------------------
	override static void InitInfo(WorldSystemInfo outInfo)
	{
		super.InitInfo(outInfo);
		outInfo
			.SetAbstract(false)
			.SetUnique(true)
			.SetLocation(WorldSystemLocation.Client);
	}

	//------------------------------------------------------------------------------------------------
	override void OnInit()
	{
		// before any listener fires: put back user rebinds a mod-less session may have wiped
		RK29_KeybindPrefs.RestoreOnce();
		RK29_LoadoutMenu.RegisterListeners();
	}
}
