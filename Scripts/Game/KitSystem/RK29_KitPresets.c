//------------------------------------------------------------------------------------------------
//! Saved kit presets: a player's own named pick sets, per class, in the engine's user settings.
//! Modelled on vanilla's SCR_WorkshopAddonManagerPresetStorage - a ModuleGameSettings read in the
//! constructor and written back whole on every mutation.
//!
//! The two engine calls are named backwards: BaseContainerTools.ReadFromInstance(this, container)
//! is the save, WriteToInstance(this, container) is the load.
//!
//! A preset stores only the wire string - group/entry ids and counts, never a ResourceName - and is
//! re-resolved and re-clamped against the current offer, then re-clamped again server-side. A
//! hand-edited settings file is no more dangerous than a typed chat command.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! One saved pick set. Keyed by class as well as by name, because presets of every class share the
//! one flat array.
//!
//! Two version numbers guard different things. RK29_KitPresetStorage.VERSION is the container
//! shape: a mismatch means the whole file is from another era, so ReadFromStorage discards every
//! preset at once. m_iFormat is the wire dialect of m_sPicks (the grammar EncodePicks/ParsePicks
//! agree on): a mismatch is one preset's problem, refused by CanLoad with the row left listed and
//! deletable. They move independently.
[BaseContainerProps()]
class RK29_KitPreset
{
	//! trimmed and capped - see RK29_KitPresetStorage.Save
	[Attribute()]
	string m_sName;

	//! A pick names a group of one class's offer and means nothing against another.
	[Attribute()]
	string m_sKitName;

	//! exactly as RK29_KitResolve.EncodePicks wrote them
	[Attribute()]
	string m_sPicks;

	//! Zero is what a hand-added entry without the field reads as, and is refused a load like any
	//! other dialect this build does not speak.
	[Attribute()]
	int m_iFormat;
}

//------------------------------------------------------------------------------------------------
//! The store itself: every player's saved preset, of every class, in one settings module.
class RK29_KitPresetStorage : ModuleGameSettings
{
	[Attribute()]
	protected int m_iVersion;

	//! Save order, and the order the section lists rows in.
	[Attribute()]
	protected ref array<ref RK29_KitPreset> m_aPresets;

	//! Container shape this build writes. See RK29_KitPreset for what it does not guard.
	const int VERSION = 1;

	//! Wire dialect this build speaks. Static where VERSION is not, because the menu names it when
	//! it refuses a preset.
	static const int PICKS_FORMAT = 1;

	//! Must equal the class name: UserSettings.GetModule's parameter is
	//! `string engineUserConfigClassName` (core/scripts/Core/generated/Containers/UserSettings.c:14).
	//! A name matching no registered class resolves to nothing on both sides and presets silently
	//! never persist.
	protected static const string MODULE_NAME = "RK29_KitPresetStorage";

	//! Per class. Capped because the section is stamped into the info band, which does not scroll.
	static const int MAX_PER_KIT = 12;

	static const int MAX_NAME_LENGTH = 24;

	//! One instance per session is what keeps the in-RAM array and the settings file from
	//! disagreeing (SCR_AddonManager.c:755, GetPresetStorage :986).
	protected static ref RK29_KitPresetStorage s_Instance;

	//------------------------------------------------------------------------------------------------
	static RK29_KitPresetStorage GetInstance()
	{
		if (!s_Instance)
			s_Instance = new RK29_KitPresetStorage();

		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	//! Reads the file into RAM at construction (as SCR_WorkshopAddonManagerStorage.c:146-151 does):
	//! every mutation overrides the whole object in storage, so this must start out being all of it.
	void RK29_KitPresetStorage()
	{
		ReadFromStorage();
	}

	//------------------------------------------------------------------------------------------------
	//! Out-param holds the live objects, not copies, and is cleared first so a caller reusing one
	//! array across two classes cannot accumulate.
	void PresetsFor(string kitName, notnull array<RK29_KitPreset> outPresets)
	{
		outPresets.Clear();

		if (kitName == "" || !m_aPresets)
			return;

		foreach (RK29_KitPreset preset : m_aPresets)
		{
			if (preset && preset.m_sKitName == kitName)
				outPresets.Insert(preset);
		}
	}

	//------------------------------------------------------------------------------------------------
	RK29_KitPreset Find(string kitName, string name)
	{
		int index = IndexOf(kitName, name);
		if (index < 0)
			return null;

		return m_aPresets[index];
	}

	//------------------------------------------------------------------------------------------------
	//! Saves one pick set and persists immediately. The name is trimmed then capped rather than
	//! refused for length; two names colliding only after the cap are one name. An existing name is
	//! overwritten in place so the row keeps its position (SCR_WorkshopAddonManagerStorage.c
	//! :186-189). The cap refuses rather than evicts - false, and nothing changed, is what the menu
	//! turns into a visible refusal. A blank name returns false too.
	bool Save(string kitName, string name, string picksWire)
	{
		if (kitName == "")
			return false;

		// the return value, not the receiver: string.Trim answers a new string rather than
		// shortening the one it was called on
		string trimmed = name.Trim();
		if (trimmed == "")
			return false;

		if (trimmed.Length() > MAX_NAME_LENGTH)
			trimmed = trimmed.Substring(0, MAX_NAME_LENGTH);

		if (!m_aPresets)
			m_aPresets = {};

		int index = IndexOf(kitName, trimmed);
		if (index < 0 && CountFor(kitName) >= MAX_PER_KIT)
		{
			Print(string.Format("[RK29] kit presets: '%1' already holds %2 presets"
				+ " - delete one before saving another", kitName, MAX_PER_KIT), LogLevel.NORMAL);
			return false;
		}

		RK29_KitPreset preset = new RK29_KitPreset();
		preset.m_sName = trimmed;
		preset.m_sKitName = kitName;
		preset.m_sPicks = picksWire;
		preset.m_iFormat = PICKS_FORMAT;

		if (index >= 0)
			m_aPresets.Set(index, preset);
		else
			m_aPresets.Insert(preset);

		WriteToStorage();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! RemoveOrdered, not Remove: Remove is a swap-remove that drops the last element into the hole,
	//! and this array's order is the order the section lists its rows in.
	bool Delete(string kitName, string name)
	{
		int index = IndexOf(kitName, name);
		if (index < 0)
			return false;

		m_aPresets.RemoveOrdered(index);
		WriteToStorage();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Format-mismatched presets stay listed and deletable but must not be parsed - the menu asks
	//! this first.
	static bool CanLoad(RK29_KitPreset preset)
	{
		if (!preset)
			return false;

		return preset.m_iFormat == PICKS_FORMAT;
	}

	//------------------------------------------------------------------------------------------------
	protected int CountFor(string kitName)
	{
		if (kitName == "" || !m_aPresets)
			return 0;

		int count = 0;
		foreach (RK29_KitPreset preset : m_aPresets)
		{
			if (preset && preset.m_sKitName == kitName)
				count++;
		}

		return count;
	}

	//------------------------------------------------------------------------------------------------
	//! -1 when absent. Both keys matter: two classes may each hold a preset called "AT loadout".
	protected int IndexOf(string kitName, string name)
	{
		if (kitName == "" || name == "" || !m_aPresets)
			return -1;

		foreach (int i, RK29_KitPreset preset : m_aPresets)
		{
			if (preset && preset.m_sKitName == kitName && preset.m_sName == name)
				return i;
		}

		return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! The load side (WriteToInstance), mirroring
	//! SCR_WorkshopAddonManagerStorage.ReadPresetsFromStorage.
	//!
	//! The container VERSION is checked here and nowhere else: the pre-stamp is only a first-run
	//! default, WriteToInstance puts the file's version in its place, and a mismatch drops every
	//! deserialized array. A legacy file missing the field deserializes as current and is accepted -
	//! version 1 is the first shape there has ever been. Nothing is written to disk here; the next
	//! Save persists the reset. The guard sits ahead of Salvage: a discarded file has nothing left
	//! to salvage.
	protected void ReadFromStorage()
	{
		m_iVersion = VERSION;
		m_aPresets = {};

		UserSettings settings = GetGame().GetGameUserSettings();
		if (!settings)
			return;

		BaseContainer container = settings.GetModule(MODULE_NAME);
		if (!container)
			return;

		BaseContainerTools.WriteToInstance(this, container);

		int stored = m_iVersion;
		if (stored != VERSION)
		{
			// a module never written reads as version 0 with nothing in it (first run, and every
			// Workbench session after a script reload) - that is not a discard
			bool hadPresets = m_aPresets && !m_aPresets.IsEmpty();
			m_iVersion = VERSION;
			m_aPresets = {};
			if (hadPresets)
			{
				Print(string.Format("[RK29] kit presets: settings module '%1' is container"
					+ " version %2 and this build writes %3 - stored presets discarded",
					MODULE_NAME, stored, VERSION), LogLevel.WARNING);
			}
			return;
		}

		Salvage();
	}

	//------------------------------------------------------------------------------------------------
	//! The save side (ReadFromInstance), mirroring
	//! SCR_WorkshopAddonManagerStorage.SavePresetsToStorage (:295-302) - plus the forced
	//! SaveUserSettings vanilla omits. Without it settings are written only at exit, so a crash or a
	//! Workbench script reload loses every preset of the session; keep the forced write.
	//! The two null guards are ours - vanilla guards the fetch on read (:282) but not on write.
	//! Unconfirmed fix: the exit-only write is inferred from vanilla, not re-tested on 1.8.0.13.
	protected void WriteToStorage()
	{
		m_iVersion = VERSION;

		UserSettings settings = GetGame().GetGameUserSettings();
		if (!settings)
			return;

		BaseContainer container = settings.GetModule(MODULE_NAME);
		if (!container)
		{
			Print(string.Format("[RK29] kit presets: settings module '%1' is missing"
				+ " - presets are session-only", MODULE_NAME), LogLevel.WARNING);
			return;
		}

		BaseContainerTools.ReadFromInstance(this, container);
		GetGame().UserSettingsChanged();
		GetGame().SaveUserSettings();
	}

	//------------------------------------------------------------------------------------------------
	//! Makes a just-loaded store safe to walk - the settings file is plain text and user-editable.
	//! Handles a null array (what WriteToInstance leaves when the module holds no such field), null
	//! or unnamed entries, and more than MAX_PER_KIT for one class, which only a text editor can
	//! produce. Overflow is dropped from the end, so the presets made first are kept.
	protected void Salvage()
	{
		if (!m_aPresets)
		{
			m_aPresets = {};
			return;
		}

		map<string, int> perKit = new map<string, int>();
		bool dropped = false;

		for (int i = m_aPresets.Count() - 1; i >= 0; i--)
		{
			RK29_KitPreset preset = m_aPresets[i];
			if (!preset || preset.m_sName == "" || preset.m_sKitName == "")
			{
				m_aPresets.RemoveOrdered(i);
				dropped = true;
			}
		}

		// forwards, so "kept" means the earliest saved - the reverse walk above would count from
		// the end of each class's run
		for (int j = 0; j < m_aPresets.Count(); j++)
		{
			RK29_KitPreset preset = m_aPresets[j];

			int held = 0;
			perKit.Find(preset.m_sKitName, held);
			if (held >= MAX_PER_KIT)
			{
				m_aPresets.RemoveOrdered(j);
				j--;
				dropped = true;
				continue;
			}

			perKit.Set(preset.m_sKitName, held + 1);
		}

		if (dropped)
		{
			Print(string.Format("[RK29] kit presets: settings module '%1' held unusable"
				+ " entries - they were dropped on load", MODULE_NAME), LogLevel.NORMAL);
		}
	}
}
