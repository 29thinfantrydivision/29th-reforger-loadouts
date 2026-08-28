//------------------------------------------------------------------------------------------------
//! Mod-owned backup of the RK29 keybinds in $profile:RK29_Keybinds.json. A settings save in a
//! session without this mod loaded can silently drop our actions' user bindings from the
//! engine-owned overlay, so RestoreOnce() puts them back at game start and SyncFromEngine()
//! re-mirrors on leaving the vanilla keybind tab. Records: "action|device|bind|filter".
//! Unconfirmed fix: the wipe is reasoned from engine behaviour, not re-tested on 1.8.0.13.
//------------------------------------------------------------------------------------------------
class RK29_KeybindPrefs : JsonApiStruct
{
	ref array<string> m_aRecords = {};

	protected static const string PREFS_FILE = "$profile:RK29_Keybinds.json";
	protected static const string UNBOUND    = "<unbound>";

	protected static bool s_bRestoreDone;

	//------------------------------------------------------------------------------------------------
	void RK29_KeybindPrefs()
	{
		RegV("m_aRecords");
	}

	//------------------------------------------------------------------------------------------------
	//! Game-start entry point; the input system and the keybind submenu both call it, first wins.
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

		// said out loud: a file the parser choked on answers the same as no file at all, and the
		// SyncFromEngine below then overwrites it - the exact wipe this backup exists to undo.
		RK29_KeybindPrefs prefs = new RK29_KeybindPrefs();
		if (prefs.LoadFromFile(PREFS_FILE))
			prefs.RestoreLost(binding);
		else
			Print("[RK29] keybind backup " + PREFS_FILE + " did not load - nothing to restore this"
				+ " session (first run, or the file is unreadable)", LogLevel.NORMAL);

		SyncFromEngine(binding);
	}

	//------------------------------------------------------------------------------------------------
	//! Mirrors the engine's user-binding state for our actions into the backup; a null binding is
	//! looked up fresh. The write is skipped where nothing moved - both sides are built by the same
	//! walk in one fixed order, so a plain walk answers it. An unloadable backup is always rewritten.
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
		GetActions(actions);
		GetDevices(devices);

		RK29_KeybindPrefs prefs = new RK29_KeybindPrefs();
		foreach (string action : actions)
		{
			foreach (EInputDeviceType device : devices)
			{
				if (binding.IsDefault(action, device))
					continue;

				string deviceName = DeviceName(device);
				array<string> raw = {};
				binding.GetBindings(action, raw, device, string.Empty, false);
				if (raw.IsEmpty())
				{
					prefs.m_aRecords.Insert(action + "|" + deviceName + "|" + UNBOUND + "|");
					continue;
				}

				foreach (int i, string bind : raw)
				{
					string filter = binding.GetFilter(action, device, string.Empty, i);
					prefs.m_aRecords.Insert(action + "|" + deviceName + "|" + bind + "|" + filter);
				}
			}
		}

		RK29_KeybindPrefs stored = new RK29_KeybindPrefs();
		if (stored.LoadFromFile(PREFS_FILE) && SameRecords(stored.m_aRecords, prefs.m_aRecords))
			return;

		prefs.PackToFile(PREFS_FILE);
	}

	//------------------------------------------------------------------------------------------------
	//! Order-sensitive: both sides come from the same walk, so different order means different state.
	protected static bool SameRecords(array<string> left, array<string> right)
	{
		if (!left || !right || left.Count() != right.Count())
			return false;

		foreach (int i, string record : left)
		{
			if (record != right[i])
				return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Rebuilds the user binding for every pair the backup holds while the engine reports default -
	//! the wipe signature. A non-default engine binding wins: the user rebound more recently.
	protected void RestoreLost(InputBinding binding)
	{
		array<string> actions = {};
		array<EInputDeviceType> devices = {};
		GetActions(actions);
		GetDevices(devices);

		bool changed = false;
		foreach (string action : actions)
		{
			foreach (EInputDeviceType device : devices)
			{
				if (!binding.IsDefault(action, device))
					continue;

				array<string> binds = {};
				array<string> filters = {};
				if (!Collect(action, DeviceName(device), binds, filters))
					continue;

				binding.CreateUserBinding(action, device);
				for (int i = binding.GetBindingsCount(action, device) - 1; i >= 0; i--)
					binding.RemoveBinding(action, device, string.Empty, i);

				foreach (int bindIdx, string bind : binds)
				{
					if (bind != UNBOUND)
						binding.AddBinding(action, string.Empty, bind, filters[bindIdx]);
				}

				changed = true;
			}
		}

		if (changed)
			binding.Save();
	}

	//------------------------------------------------------------------------------------------------
	//! False means the pair is absent from the file, i.e. at engine default - distinct from
	//! present-but-<unbound>, which returns true with the marker.
	protected bool Collect(string action, string deviceName, out notnull array<string> binds, out notnull array<string> filters)
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

	//------------------------------------------------------------------------------------------------
	protected static void GetActions(out notnull array<string> actions)
	{
		actions.Insert("RK29_ToggleKitMenu");
		actions.Insert("RK29_DialogApply");
		actions.Insert("RK29_StepFive");
		actions.Insert("RK29_StepTen");
	}

	//------------------------------------------------------------------------------------------------
	protected static void GetDevices(out notnull array<EInputDeviceType> devices)
	{
		devices.Insert(EInputDeviceType.KEYBOARD);
		devices.Insert(EInputDeviceType.MOUSE);
		devices.Insert(EInputDeviceType.GAMEPAD);
	}

	//------------------------------------------------------------------------------------------------
	//! Stored as names, not enum ints - the file must survive engine enum reshuffles.
	protected static string DeviceName(EInputDeviceType device)
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
