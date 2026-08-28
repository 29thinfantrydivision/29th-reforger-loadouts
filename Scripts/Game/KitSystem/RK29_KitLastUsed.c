//------------------------------------------------------------------------------------------------
//! What each class was last APPLIED as, per kit, in $profile:RK29_KitLastUsed.json - so a player
//! who built a kit once does not rebuild it every session.
//!
//! A separate file from RK29_KitPresets.json on purpose. That store's ReadFromStorage discards
//! every record when m_iVersion does not match and has no migration branch, so adding a field to it
//! would wipe every player's saved kits on the update; its record grammar has no room either, since
//! DecodeRecord rejoins everything past the name to keep a typed "|" legal. Two files, no shared
//! failure.
//!
//! Stores the WIRE, not a preset name: a name can be deleted or renamed out from under this, and
//! the wire is what the player actually wore. A wire that happens to match a saved preset lights
//! that preset's row through the comparison the menu already makes - no work here.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! The on-disk shape, modelled on RK29_KitPresetFile: JsonApiStruct auto-processes registered
//! scalars and string arrays and nothing else. One record is "format|picks|kitName".
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
//! One wire per kit. Write-through: every applied kit rewrites the file, which is what makes the
//! record survive a client that never shuts down cleanly.
//------------------------------------------------------------------------------------------------
class RK29_KitLastUsedStore
{
	//! kit name -> the wire that kit was last applied with. An empty wire is never stored: the kit
	//! standing at its authored defaults is the absence of a record, which is also how clicking
	//! Standard and applying forgets a kit.
	protected ref map<string, string> m_mWires = new map<string, string>();

	//! File shape this build writes. A mismatch discards the file, which costs one session's
	//! convenience and nothing else - unlike the preset store, there is nothing here worth migrating.
	static const int VERSION = 1;

	protected static const string STORE_FILE = "$profile:RK29_KitLastUsed.json";

	//! Fields in a record. The kit name is last so a "|" in an authored name cannot shift the ones
	//! before it - the same discipline RK29_KitPresetStorage.EncodeRecord uses for the preset name.
	protected static const int RECORD_FIELDS = 3;

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
	//! Remember what this kit was applied as. Called from the server's own confirmation, so what is
	//! stored is what the server settled on rather than what the menu asked for.
	void Mark(string kitName, string picksWire)
	{
		if (kitName == "")
			return;

		// the authored defaults are the absence of a record, not a record of nothing
		if (picksWire == "")
		{
			if (!m_mWires.Contains(kitName))
				return;

			m_mWires.Remove(kitName);
			WriteToStorage();
			return;
		}

		string held;
		if (m_mWires.Find(kitName, held) && held == picksWire)
			return;

		if (!m_mWires.Contains(kitName) && m_mWires.Count() >= MAX_KITS)
		{
			Print(string.Format("[RK29] last-used kits: already holding %1 kits - '%2' is not"
				+ " remembered", MAX_KITS, kitName), LogLevel.NORMAL);
			return;
		}

		m_mWires.Set(kitName, picksWire);
		WriteToStorage();
	}

	//------------------------------------------------------------------------------------------------
	//! "" when this kit was never applied, or was last applied at its defaults. Callers treat that
	//! exactly as they treat no memory at all.
	string WireFor(string kitName)
	{
		if (kitName == "")
			return "";

		string wire;
		if (m_mWires.Find(kitName, wire))
			return wire;

		return "";
	}

	//------------------------------------------------------------------------------------------------
	//! An absent or unreadable file is the first run and answers as an empty store. The file is
	//! plain text and user-editable, so nothing in it is treated as proven: a malformed record, one
	//! in a dialect this build does not speak, or overflow past MAX_KITS is dropped.
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
			string kitName, wire;
			if (!DecodeRecord(record, kitName, wire) || m_mWires.Count() >= MAX_KITS)
			{
				dropped = true;
				continue;
			}

			m_mWires.Set(kitName, wire);
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

		for (int i = 0, count = m_mWires.Count(); i < count; i++)
			file.m_aRecords.Insert(EncodeRecord(m_mWires.GetKey(i), m_mWires.GetElement(i)));

		if (!file.PackToFile(STORE_FILE))
		{
			Print("[RK29] last-used kits: could not write " + STORE_FILE
				+ " - kits are session-only", LogLevel.WARNING);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! "format|picks|kitName". The first two are machine-written and hold no "|" - a picks wire is
	//! built from ";", "=" and ":" alone - so the authored kit name goes last and keeps whatever it
	//! holds. The format is the PICKS dialect, shared with the preset store rather than invented
	//! again: both files hold the same kind of string and must go stale together.
	protected static string EncodeRecord(string kitName, string wire)
	{
		return RK29_KitPresetStorage.PICKS_FORMAT.ToString() + "|" + wire + "|" + kitName;
	}

	//------------------------------------------------------------------------------------------------
	//! False for anything that is not a record this build can read. The tail is rejoined rather than
	//! taken as one field, because Split cuts at every separator and a "|" is legal in a kit name.
	protected static bool DecodeRecord(string record, out string kitName, out string wire)
	{
		kitName = "";
		wire = "";

		array<string> parts = {};
		record.Split("|", parts, true);
		if (parts.Count() < RECORD_FIELDS)
			return false;

		if (parts[0].ToInt() != RK29_KitPresetStorage.PICKS_FORMAT)
			return false;

		string name = parts[RECORD_FIELDS - 1];
		for (int i = RECORD_FIELDS; i < parts.Count(); i++)
			name += "|" + parts[i];

		if (name == "" || parts[1] == "")
			return false;

		kitName = name;
		wire = parts[1];
		return true;
	}
}
