//------------------------------------------------------------------------------------------------
//! RK29_KeybindPrefs
//!
//! Mod-owned backup of the user's rebinds for the RK29 actions, in a file the engine never
//! rewrites ($profile:RK29_Keybinds.json). Same lesson as SPEC29_SpectatorPrefs: engine-owned
//! stores are re-serialised from what the engine can currently see, and the user-binding
//! overlay (profile settings/InputUserSettings.conf) is engine-owned - a settings save in a
//! session WITHOUT this mod loaded can silently drop our actions' user bindings. A file we own
//! just sits there until we come back.
//!
//! Flow: RestoreOnce() at game start puts back any user rebind the engine lost (engine reports
//! default while the backup holds a custom bind), then mirrors the engine - now authoritative
//! either way - into the backup. SyncFromEngine() re-mirrors after the user leaves the vanilla
//! keybind tab (RK29_KeybindMenu), so a reset-to-default done with the mod loaded clears the
//! backup instead of being resurrected next launch. Our category only exists in the menu while
//! the mod is loaded, so every deliberate edit of our binds goes through that sync.
//!
//! Records are flat "action|device|bind|filter" strings - bind strings are engine input names
//! ("keyboard:KC_F4", "mouse:button0") and never contain '|'. A pair whose user binding has
//! zero binds (deliberately unbound) is kept as a single record with the <unbound> marker;
//! a pair absent from the file is at engine default. Combo binds round-trip only as far as
//! GetBindings/AddBinding preserve their string form.
//------------------------------------------------------------------------------------------------
class RK29_KeybindPrefs : JsonApiStruct
{
	ref array<string> m_aRecords = {};

	protected static const string RK29_PREFS_FILE = "$profile:RK29_Keybinds.json";
	protected static const string RK29_UNBOUND    = "<unbound>";

	protected static bool s_bRestoreDone;

	//--------------------------------------------------------------------------------------------
	void RK29_KeybindPrefs()
	{
		RegV("m_aRecords");
	}

	//--------------------------------------------------------------------------------------------
	//! Game-start entry point (kit input system and the keybind submenu both call it; first
	//! caller wins). Restores lost rebinds, then mirrors the engine state into the backup.
	static void RestoreOnce()
	{
		if (s_bRestoreDone)
			return;

		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		InputBinding binding = im.CreateUserBinding();
		if (!binding)
			return;

		s_bRestoreDone = true;

		RK29_KeybindPrefs prefs = new RK29_KeybindPrefs();
		if (prefs.LoadFromFile(RK29_PREFS_FILE))
			prefs.RK29_RestoreLost(binding);

		SyncFromEngine(binding);
	}

	//--------------------------------------------------------------------------------------------
	//! Mirror the engine's current user-binding state for our actions into the backup file.
	//! Passing null looks the binding interface up fresh (the keybind-tab hook does this).
	static void SyncFromEngine(InputBinding binding = null)
	{
		if (!binding)
		{
			InputManager im = GetGame().GetInputManager();
			if (!im)
				return;

			binding = im.CreateUserBinding();
			if (!binding)
				return;
		}

		array<string> actions = {};
		array<EInputDeviceType> devices = {};
		RK29_GetActions(actions);
		RK29_GetDevices(devices);

		RK29_KeybindPrefs prefs = new RK29_KeybindPrefs();
		foreach (string action : actions)
		{
			foreach (EInputDeviceType device : devices)
			{
				if (binding.IsDefault(action, device))
					continue;

				string deviceName = RK29_DeviceName(device);
				array<string> raw = {};
				binding.GetBindings(action, raw, device, string.Empty, false);
				if (raw.IsEmpty())
				{
					prefs.m_aRecords.Insert(action + "|" + deviceName + "|" + RK29_UNBOUND + "|");
					continue;
				}

				foreach (int i, string bind : raw)
				{
					string filter = binding.GetFilter(action, device, string.Empty, i);
					prefs.m_aRecords.Insert(action + "|" + deviceName + "|" + bind + "|" + filter);
				}
			}
		}

		prefs.PackToFile(RK29_PREFS_FILE);
	}

	//--------------------------------------------------------------------------------------------
	//! For every (action, device) the backup holds a user binding for while the engine reports
	//! default - the wipe signature - rebuild the user binding from the records. An engine-side
	//! non-default binding always wins untouched: the user rebound more recently than we saved.
	protected void RK29_RestoreLost(InputBinding binding)
	{
		array<string> actions = {};
		array<EInputDeviceType> devices = {};
		RK29_GetActions(actions);
		RK29_GetDevices(devices);

		bool changed = false;
		foreach (string action : actions)
		{
			foreach (EInputDeviceType device : devices)
			{
				if (!binding.IsDefault(action, device))
					continue;

				array<string> binds = {};
				array<string> filters = {};
				if (!RK29_Collect(action, RK29_DeviceName(device), binds, filters))
					continue;

				binding.CreateUserBinding(action, device);
				for (int i = binding.GetBindingsCount(action, device) - 1; i >= 0; i--)
					binding.RemoveBinding(action, device, string.Empty, i);

				foreach (int bindIdx, string bind : binds)
				{
					if (bind != RK29_UNBOUND)
						binding.AddBinding(action, string.Empty, bind, filters[bindIdx]);
				}

				changed = true;
			}
		}

		if (changed)
			binding.Save();
	}

	//--------------------------------------------------------------------------------------------
	//! Pull this pair's records out of the loaded file. False means the pair is absent, i.e. at
	//! engine default - distinct from present-but-<unbound>, which returns true with the marker.
	protected bool RK29_Collect(string action, string deviceName, out notnull array<string> binds, out notnull array<string> filters)
	{
		string prefix = action + "|" + deviceName + "|";
		foreach (string record : m_aRecords)
		{
			if (!record.StartsWith(prefix))
				continue;

			string payload = record.Substring(prefix.Length(), record.Length() - prefix.Length());
			int sep = payload.LastIndexOf("|");
			if (sep < 0)
				continue;

			binds.Insert(payload.Substring(0, sep));
			filters.Insert(payload.Substring(sep + 1, payload.Length() - sep - 1));
		}

		return !binds.IsEmpty();
	}

	//--------------------------------------------------------------------------------------------
	protected static void RK29_GetActions(out notnull array<string> actions)
	{
		actions.Insert("RK29_ToggleKitMenu");
		actions.Insert("RK29_DialogApply");
	}

	//--------------------------------------------------------------------------------------------
	//! The devices the vanilla keybind menu can write user bindings for on our rows.
	protected static void RK29_GetDevices(out notnull array<EInputDeviceType> devices)
	{
		devices.Insert(EInputDeviceType.KEYBOARD);
		devices.Insert(EInputDeviceType.MOUSE);
		devices.Insert(EInputDeviceType.GAMEPAD);
	}

	//--------------------------------------------------------------------------------------------
	//! Stored as names, not enum ints - the file must survive engine enum reshuffles.
	protected static string RK29_DeviceName(EInputDeviceType device)
	{
		switch (device)
		{
			case EInputDeviceType.KEYBOARD: return "KEYBOARD";
			case EInputDeviceType.MOUSE:    return "MOUSE";
			case EInputDeviceType.GAMEPAD:  return "GAMEPAD";
		}

		return "UNKNOWN";
	}
}
