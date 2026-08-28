//------------------------------------------------------------------------------------------------
//! Saved kit presets: a player's own named pick sets, per class, in $profile:RK29_KitPresets.json.
//!
//! DO NOT move this back into a ModuleGameSettings. The engine parses ReforgerGameSettings.conf
//! once per script instance, and joining or leaving a modded server bounces the client through a
//! VANILLA-ONLY instance (server browser / main menu) where no mod class is registered. There every
//! mod-owned module is logged as "Unknown keyword/data '<class>'" and dropped from the in-memory
//! container, so the next SaveUserSettings - and connecting to a server is itself one, it stamps
//! the last IP and port - rewrites the file without it. Mods whose settings survive that (CSI,
//! EC29) only survive because they rewrite their module at every session init; a store written
//! solely when the player presses Save cannot. Confirmed from console.log 2026-09-07: the module
//! was in the file at 14:07:11 with the mod unloaded, and the save 0.3s later erased it. Same root
//! cause as the keybind wipe RK29_KeybindPrefs exists to undo; a profile file is the fix for both.
//!
//! A preset stores only the wire string - group/entry ids and counts, never a ResourceName - and is
//! re-resolved and re-clamped against the current offer, then re-clamped again server-side. A
//! hand-edited profile file is no more dangerous than a typed chat command.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! One saved pick set. Keyed by class as well as by name, because presets of every class share the
//! one flat array.
//!
//! Two version numbers guard different things. RK29_KitPresetStorage.VERSION is the file shape: a
//! mismatch means the whole file is from another era, so ReadFromStorage discards every preset at
//! once. m_iFormat is the wire dialect of m_sPicks (the grammar EncodePicks/ParsePicks agree on): a
//! mismatch is one preset's problem, refused by CanLoad with the row left listed and deletable.
//! They move independently.
class RK29_KitPreset
{
	//! trimmed and capped - see RK29_KitPresetStorage.Save
	string m_sName;

	//! A pick names a group of one class's offer and means nothing against another.
	string m_sKitName;

	//! exactly as RK29_KitResolve.EncodePicks wrote them
	string m_sPicks;

	//! Zero is what a hand-added record without the field reads as, and is refused a load like any
	//! other dialect this build does not speak.
	int m_iFormat;
}

//------------------------------------------------------------------------------------------------
//! The on-disk shape, modelled on RK29_KeybindPrefs: a version and a flat array of records, because
//! JsonApiStruct auto-processes registered scalars and string arrays and nothing else without
//! hand-written pack/expand events. One record is "format|kitName|picks|name" - see EncodeRecord.
class RK29_KitPresetFile : JsonApiStruct
{
	int m_iVersion;
	ref array<string> m_aRecords = {};

	//------------------------------------------------------------------------------------------------
	void RK29_KitPresetFile()
	{
		RegV("m_iVersion");
		RegV("m_aRecords");
	}
}

//------------------------------------------------------------------------------------------------
//! The store itself: every saved preset, of every class, in one profile file.
class RK29_KitPresetStorage
{
	//! Save order, and the order the section lists rows in.
	protected ref array<ref RK29_KitPreset> m_aPresets;

	//! File shape this build writes. See RK29_KitPreset for what it does not guard.
	static const int VERSION = 1;

	//! Wire dialect this build speaks. Public where VERSION need not be, because the menu names it
	//! when it refuses a preset.
	static const int PICKS_FORMAT = 1;

	protected static const string PRESETS_FILE = "$profile:RK29_KitPresets.json";

	//! Fields in a record. The name is last so a typed "|" cannot shift the ones before it.
	protected static const int RECORD_FIELDS = 4;

	//! Per class. Capped because the section is stamped into the info band, which does not scroll.
	static const int MAX_PER_KIT = 12;

	static const int MAX_NAME_LENGTH = 24;

	//! One instance per session is what keeps the in-RAM array and the file from disagreeing.
	protected static ref RK29_KitPresetStorage s_Instance;

	//------------------------------------------------------------------------------------------------
	static RK29_KitPresetStorage GetInstance()
	{
		if (!s_Instance)
			s_Instance = new RK29_KitPresetStorage();

		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	//! Reads the file into RAM at construction: every mutation writes the whole file back, so this
	//! must start out being all of it.
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
	//! overwritten in place so the row keeps its position. The cap refuses rather than evicts -
	//! false, and nothing changed, is what the menu turns into a visible refusal. A blank name
	//! returns false too.
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
	//! An absent or unreadable file is the first run and answers as an empty store; nothing is
	//! written back here, the next Save persists. A file of another shape is discarded whole, which
	//! is what VERSION is for.
	//!
	//! The file is plain text and user-editable, so the walk that follows treats nothing in it as
	//! proven: a malformed or nameless record is dropped, and more than MAX_PER_KIT of one class -
	//! which only a text editor can produce - loses the overflow from the end, keeping the presets
	//! saved first.
	protected void ReadFromStorage()
	{
		m_aPresets = {};

		RK29_KitPresetFile file = new RK29_KitPresetFile();
		if (!file.LoadFromFile(PRESETS_FILE))
		{
			RK29_Log.Trace("[RK29] kit presets: " + PRESETS_FILE + " did not load - starting empty"
				+ " (first run, or the file is unreadable)");
			return;
		}

		if (file.m_iVersion != VERSION)
		{
			Print(string.Format("[RK29] kit presets: %1 is file version %2 and this build writes"
				+ " %3 - stored presets discarded", PRESETS_FILE, file.m_iVersion, VERSION),
				LogLevel.WARNING);
			return;
		}

		if (!file.m_aRecords)
			return;

		map<string, int> perKit = new map<string, int>();
		bool dropped = false;

		foreach (string record : file.m_aRecords)
		{
			RK29_KitPreset preset = DecodeRecord(record);
			if (!preset)
			{
				dropped = true;
				continue;
			}

			int held = 0;
			perKit.Find(preset.m_sKitName, held);
			if (held >= MAX_PER_KIT)
			{
				dropped = true;
				continue;
			}

			perKit.Set(preset.m_sKitName, held + 1);
			m_aPresets.Insert(preset);
		}

		if (dropped)
		{
			Print("[RK29] kit presets: " + PRESETS_FILE + " held unusable records - they were"
				+ " dropped on load", LogLevel.NORMAL);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Writes the whole store. PackToFile, not SaveToFile: SaveToFile only rewrites data a previous
	//! load left on the struct, and a struct just built by hand has none.
	protected void WriteToStorage()
	{
		RK29_KitPresetFile file = new RK29_KitPresetFile();
		file.m_iVersion = VERSION;

		foreach (RK29_KitPreset preset : m_aPresets)
		{
			if (preset)
				file.m_aRecords.Insert(EncodeRecord(preset));
		}

		if (!file.PackToFile(PRESETS_FILE))
		{
			Print("[RK29] kit presets: could not write " + PRESETS_FILE
				+ " - presets are session-only", LogLevel.WARNING);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! "format|kitName|picks|name". The first three fields are machine-written and hold no "|" -
	//! the picks wire is built from ";", "=" and ":" alone - so the name, the one field a player
	//! types, goes last and keeps whatever it holds.
	protected static string EncodeRecord(notnull RK29_KitPreset preset)
	{
		return preset.m_iFormat.ToString() + "|" + preset.m_sKitName + "|" + preset.m_sPicks + "|"
			+ preset.m_sName;
	}

	//------------------------------------------------------------------------------------------------
	//! Null for anything that is not a record this build wrote. The tail is rejoined rather than
	//! taken as one field, because Split cuts at every separator and a typed "|" is legal in a name.
	protected static RK29_KitPreset DecodeRecord(string record)
	{
		array<string> parts = {};
		record.Split("|", parts, true);
		if (parts.Count() < RECORD_FIELDS)
			return null;

		string name = parts[RECORD_FIELDS - 1];
		for (int i = RECORD_FIELDS; i < parts.Count(); i++)
			name += "|" + parts[i];

		if (name == "" || parts[1] == "")
			return null;

		RK29_KitPreset preset = new RK29_KitPreset();
		preset.m_iFormat = parts[0].ToInt();
		preset.m_sKitName = parts[1];
		preset.m_sPicks = parts[2];
		preset.m_sName = name;
		return preset;
	}
}
