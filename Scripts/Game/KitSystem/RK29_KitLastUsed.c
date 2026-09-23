//------------------------------------------------------------------------------------------------
//! Which SAVED KIT each class was last applied from, per kit, in $profile:RK29_KitLastUsed.json -
//! so a player who built a kit once does not rebuild it every session.
//!
//! A separate file from RK29_KitPresets.json on purpose. That store's ReadFromStorage discards
//! every record when m_iVersion does not match and has no migration branch, so adding a field to it
//! would wipe every player's saved kits on the update; its record grammar has no room either, since
//! DecodeRecord rejoins everything past the name to keep a typed "|" legal. Two files, no shared
//! failure.
//!
//! Stores the saved kit's NAME, never the wire it held when it was worn. The name is what the
//! player chose, and it keeps meaning what they mean by it: a saved kit edited since it was last
//! worn is seeded as it now reads rather than as a copy taken when it was, and one deleted or
//! renamed since is not seeded at all - the class starts at its authored defaults, which is the
//! Standard row. The wire is read back out of the preset store at the moment it is wanted, so
//! nothing here can hold a stale kit.
//!
//! A kit applied from picks no saved kit holds - anything built in the columns and never saved -
//! is deliberately not remembered: there is no name to remember it under, and the record is cleared
//! so the next session starts at Standard rather than at a kit the player walked away from.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! The on-disk shape, modelled on RK29_KitPresetFile: JsonApiStruct auto-processes registered
//! scalars and string arrays and nothing else. One record is "kitName|presetName".
class RK29_KitLastUsedFile : JsonApiStruct
{
	int m_iVersion;
	ref array<string> m_aRecords = {};

	//------------------------------------------------------------------------------------------------
	void RK29_KitLastUsedFile()
	{
		RegV("m_iVersion");
		RegV("m_aRecords");
	}
}

//------------------------------------------------------------------------------------------------
//! One saved-kit name per class. Write-through: every applied kit rewrites the file, which is what
//! makes the record survive a client that never shuts down cleanly.
//------------------------------------------------------------------------------------------------
class RK29_KitLastUsedStore
{
	//! kit name -> the name of the saved kit that class was last applied from. A class standing at
	//! its authored defaults is the absence of a record, which is also how clicking Standard and
	//! applying forgets one.
	protected ref map<string, string> m_mPresets = new map<string, string>();

	//! File shape this build writes. A mismatch discards the file, which costs one session's
	//! convenience and nothing else - unlike the preset store, there is nothing here worth
	//! migrating. Version 1 held wires rather than names, and is discarded by this.
	static const int VERSION = 2;

	protected static const string STORE_FILE = "$profile:RK29_KitLastUsed.json";

	//! Fields in a record. The saved kit's name is last so a typed "|" in it cannot shift the field
	//! before it - the same discipline RK29_KitPresetStorage.EncodeRecord uses.
	protected static const int RECORD_FIELDS = 2;

	//! A hand-edited file cannot make this grow without bound. The real ceiling is the roster.
	protected static const int MAX_KITS = 32;

	//! One instance per session is what keeps the in-RAM map and the file from disagreeing.
	protected static ref RK29_KitLastUsedStore s_Instance;

	//------------------------------------------------------------------------------------------------
	static RK29_KitLastUsedStore GetInstance()
	{
		if (!s_Instance)
			s_Instance = new RK29_KitLastUsedStore();

		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	void RK29_KitLastUsedStore()
	{
		ReadFromStorage();
	}

	//------------------------------------------------------------------------------------------------
	//! Remember which saved kit this class was applied from. Called from the server's own
	//! confirmation, so the wire asked about is what the server settled on rather than what the menu
	//! asked for; the name stored is the saved kit holding exactly that wire, and picks no saved kit
	//! holds clear the record. A kit saved while the class stood at its defaults holds the empty
	//! wire and is cleared rather than named - seeding nothing reaches the same defaults.
	void Mark(string kitName, string picksWire)
	{
		if (kitName == "")
			return;

		if (picksWire == "")
		{
			MarkPreset(kitName, "");
			return;
		}

		RK29_KitPresetStorage presets = RK29_KitPresetStorage.GetInstance();
		if (!presets)
			return;

		MarkPreset(kitName, presets.NameForPicks(kitName, picksWire));
	}

	//------------------------------------------------------------------------------------------------
	//! The name straight in, for the one caller that knows it without a wire to match it by: saving
	//! the kit on the player's back under a new name makes that name the one they last wore. An
	//! empty name forgets the class.
	void MarkPreset(string kitName, string presetName)
	{
		if (kitName == "")
			return;

		if (presetName == "")
		{
			if (!m_mPresets.Contains(kitName))
				return;

			m_mPresets.Remove(kitName);
			WriteToStorage();
			return;
		}

		string held;
		if (m_mPresets.Find(kitName, held) && held == presetName)
			return;

		if (!m_mPresets.Contains(kitName) && m_mPresets.Count() >= MAX_KITS)
		{
			Print(string.Format("[RK29] last-used kits: already holding %1 kits - '%2' is not"
				+ " remembered", MAX_KITS, kitName), LogLevel.NORMAL);
			return;
		}

		m_mPresets.Set(kitName, presetName);
		WriteToStorage();
	}

	//------------------------------------------------------------------------------------------------
	//! What that saved kit holds NOW - which is why the name is what is stored. "" when this class
	//! was never applied from a saved kit, when the one it names has since been deleted or renamed,
	//! or when that kit is in a dialect this build cannot read. Callers treat "" exactly as they
	//! treat no memory at all: the authored defaults, the Standard row.
	string WireFor(string kitName)
	{
		if (kitName == "")
			return "";

		string presetName;
		if (!m_mPresets.Find(kitName, presetName))
			return "";

		RK29_KitPresetStorage presets = RK29_KitPresetStorage.GetInstance();
		if (!presets)
			return "";

		RK29_KitPreset preset = presets.Find(kitName, presetName);
		if (!preset || !RK29_KitPresetStorage.CanLoad(preset))
			return "";

		return preset.m_sPicks;
	}

	//------------------------------------------------------------------------------------------------
	//! An absent or unreadable file is the first run and answers as an empty store. The file is
	//! plain text and user-editable, so nothing in it is treated as proven: a malformed record, or
	//! overflow past MAX_KITS, is dropped. A name no saved kit answers to is NOT dropped here -
	//! WireFor answers "" for it, and deleting the record would turn a preset file that failed to
	//! load once into a forgotten kit.
	protected void ReadFromStorage()
	{
		RK29_KitLastUsedFile file = new RK29_KitLastUsedFile();
		if (!file.LoadFromFile(STORE_FILE))
		{
			RK29_Log.Trace("[RK29] last-used kits: " + STORE_FILE + " did not load - starting empty"
				+ " (first run, or the file is unreadable)");
			return;
		}

		if (file.m_iVersion != VERSION)
		{
			Print(string.Format("[RK29] last-used kits: %1 is file version %2 and this build writes"
				+ " %3 - stored kits discarded", STORE_FILE, file.m_iVersion, VERSION),
				LogLevel.NORMAL);
			return;
		}

		if (!file.m_aRecords)
			return;

		bool dropped = false;
		foreach (string record : file.m_aRecords)
		{
			string kitName, presetName;
			if (!DecodeRecord(record, kitName, presetName) || m_mPresets.Count() >= MAX_KITS)
			{
				dropped = true;
				continue;
			}

			m_mPresets.Set(kitName, presetName);
		}

		if (dropped)
		{
			Print("[RK29] last-used kits: " + STORE_FILE + " held unusable records - they were"
				+ " dropped on load", LogLevel.NORMAL);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Writes the whole store. PackToFile, not SaveToFile: SaveToFile only rewrites data a previous
	//! load left on the struct, and a struct just built by hand has none.
	protected void WriteToStorage()
	{
		RK29_KitLastUsedFile file = new RK29_KitLastUsedFile();
		file.m_iVersion = VERSION;

		for (int i = 0, count = m_mPresets.Count(); i < count; i++)
			file.m_aRecords.Insert(EncodeRecord(m_mPresets.GetKey(i), m_mPresets.GetElement(i)));

		if (!file.PackToFile(STORE_FILE))
		{
			Print("[RK29] last-used kits: could not write " + STORE_FILE
				+ " - kits are session-only", LogLevel.WARNING);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! "kitName|presetName". The class name is authored and holds no "|" - the preset store's own
	//! records rest on the same fact - so the saved kit's name, the one field a player types, goes
	//! last and keeps whatever it holds.
	protected static string EncodeRecord(string kitName, string presetName)
	{
		return kitName + "|" + presetName;
	}

	//------------------------------------------------------------------------------------------------
	//! False for anything that is not a record this build can read. The tail is rejoined rather than
	//! taken as one field, because Split cuts at every separator and a typed "|" is legal in a name.
	//! skipEmptyEntries is false for the same reason RK29_KitPresetStorage.DecodeRecord keeps it
	//! false: a skipped empty field shifts every field after it and shortens the record.
	protected static bool DecodeRecord(string record, out string kitName, out string presetName)
	{
		kitName = "";
		presetName = "";

		array<string> parts = {};
		record.Split("|", parts, false);
		if (parts.Count() < RECORD_FIELDS)
			return false;

		string name = parts[RECORD_FIELDS - 1];
		for (int i = RECORD_FIELDS; i < parts.Count(); i++)
			name += "|" + parts[i];

		if (name == "" || parts[0] == "")
			return false;

		kitName = parts[0];
		presetName = name;
		return true;
	}
}
