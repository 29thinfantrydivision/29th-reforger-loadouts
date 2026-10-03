//------------------------------------------------------------------------------------------------
//! Saved kit presets: a player's own named pick sets, per class, in $profile:RK29_KitPresets_v2.json.
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
//! A preset stores the FULL expanded pick list (RK29_KitResolve.ExpandPicks) - an explicit answer
//! for every group its own picks offer - so a default moved in config cannot reach it. Whether
//! today's config would answer it differently is one comparison, RK29_KitResolve.ChangedCount.
//! Group/entry ids and counts only, never a ResourceName, and re-resolved server-side like any
//! other request: a hand-edited profile file is no more dangerous than a typed chat command.
//!
//! One file per store version, and a store writes ONLY its own. A new record shape is a new file
//! suffix with a converter from the one before, never a bump of the in-file VERSION: the old build
//! keeps reading and writing its own file, so running both or rolling back cannot wipe anything.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! One saved pick set. Keyed by class as well as by name, because presets of every class share the
//! one flat array.
//!
//! m_iFormat is the record's dialect; anything but PICKS_FORMAT is refused a load by CanLoad, with
//! the row left listed and deletable, and is written back exactly as it was read (m_sRaw).
class RK29_KitPreset
{
	//! trimmed and capped - see RK29_KitPresetStorage.Save
	string m_sName;

	//! A pick names a group of one class's offer and means nothing against another.
	string m_sKitName;

	//! exactly as RK29_KitResolve.EncodePicks wrote them
	string m_sPicks;

	int m_iFormat;

	//! Trailing "key=value" fields this build does not know, kept verbatim and written back, so a
	//! later per-kit field can ship without a new file.
	ref array<string> m_aExtra = {};

	//! The record as read, for one this build cannot speak: it goes back to disk untouched.
	string m_sRaw;
}

//------------------------------------------------------------------------------------------------
//! The on-disk shape, modelled on RK29_KeybindPrefs: a version and a flat array of records, because
//! JsonApiStruct auto-processes registered scalars and string arrays and nothing else without
//! hand-written pack/expand events. Shared by every store version; each owns its own file.
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
	protected ref array<ref RK29_KitPreset> m_aPresets = {};

	//! Records that could not be decoded at all, written back after the presets. Never dropped: the
	//! next write would erase them for good.
	protected ref array<string> m_aUndecodable = {};

	//! The file's shape. Never bumped - see the header; a new shape is a new PRESETS_FILE.
	static const int VERSION = 1;

	//! Record dialect this build writes and loads: the full expanded list.
	static const int PICKS_FORMAT = 2;

	protected static const string PRESETS_FILE = "$profile:RK29_KitPresets_v2.json";

	//! The newest older store, imported ONCE when PRESETS_FILE does not exist and never touched.
	//! Only v1 today; the next version adds its predecessor here, newest first, and drops the oldest.
	protected static const string V1_FILE = "$profile:RK29_KitPresets.json";
	protected static const int V1_PICKS_FORMAT = 1;

	//! Fields before the optional trailing "key=value" ones: format, kit, picks, escaped name.
	protected static const int RECORD_FIELDS = 4;

	//! Per class shown and savable. Capped because the section is stamped into the info band, which
	//! does not scroll; records past it (a text editor's doing) are kept but not listed.
	static const int MAX_PER_KIT = 12;

	static const int MAX_NAME_LENGTH = 24;

	//! Loading waits for the kit setup, which the import needs, so it happens on the first public
	//! call that has one rather than at construction.
	protected bool m_bLoaded;

	//! Our own file exists and would not read: nothing may be written over it, and nothing is
	//! imported over it either.
	protected bool m_bReadOnly;

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
	//! Out-param holds the live objects, not copies, and is cleared first so a caller reusing one
	//! array across two classes cannot accumulate. At most MAX_PER_KIT, the first saved.
	void PresetsFor(string kitName, notnull array<RK29_KitPreset> outPresets)
	{
		outPresets.Clear();

		if (kitName == "" || !EnsureLoaded())
			return;

		foreach (RK29_KitPreset preset : m_aPresets)
		{
			if (preset && preset.m_sKitName == kitName && outPresets.Count() < MAX_PER_KIT)
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
	//! Which saved kit of this class answers to these picks, "" when none does: one holding exactly
	//! this wire, or one whose re-expansion under today's config is this wire - the second is what
	//! keeps an outdated kit's name when the player applies what it now loads as. Twins answer with
	//! the first; either name seeds the same kit. A dialect this build cannot read is passed over.
	string NameForPicks(string kitName, string picksWire)
	{
		if (kitName == "" || picksWire == "" || !EnsureLoaded())
			return "";

		foreach (RK29_KitPreset preset : m_aPresets)
		{
			if (preset && preset.m_sKitName == kitName && preset.m_sPicks == picksWire
				&& CanLoad(preset))
				return preset.m_sName;
		}

		RK29_KitSetup setup = SetupOrNull();
		if (!setup)
			return "";

		RK29_ClassSetup cls = setup.FindClass(kitName);
		foreach (RK29_KitPreset preset : m_aPresets)
		{
			if (preset && preset.m_sKitName == kitName && CanLoad(preset)
				&& RK29_KitResolve.CachedExpandedWire(cls, setup, preset.m_sPicks) == picksWire)
				return preset.m_sName;
		}

		return "";
	}

	//------------------------------------------------------------------------------------------------
	//! Saves one pick set and persists immediately. The name is trimmed then capped rather than
	//! refused for length; two names colliding only after the cap are one name. An existing name is
	//! overwritten in place so the row keeps its position, and keeps any fields this build does not
	//! know. The cap refuses rather than evicts - false, and nothing changed, is what the menu turns
	//! into a visible refusal. A blank name, or a store that is read-only, returns false too.
	bool Save(string kitName, string name, string picksWire)
	{
		if (kitName == "" || !EnsureWritable())
			return false;

		// the return value, not the receiver: string.Trim answers a new string rather than
		// shortening the one it was called on
		string trimmed = name.Trim();
		if (trimmed == "")
			return false;

		if (trimmed.Length() > MAX_NAME_LENGTH)
			trimmed = trimmed.Substring(0, MAX_NAME_LENGTH);

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
		{
			if (m_aPresets[index].m_sRaw == "")
				preset.m_aExtra = m_aPresets[index].m_aExtra;
			m_aPresets.Set(index, preset);
		}
		else
		{
			m_aPresets.Insert(preset);
		}

		WriteToStorage();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Moves one preset to another place in ITS OWN class's list and persists immediately. newPos is
	//! a position among that class's presets - what the band shows - not an index into the flat
	//! array every class shares, and the two are not the same number: another class's presets sit
	//! between them. So the walk is done in the kit's own indices, and every other class's records
	//! keep the array positions they had.
	//!
	//! RemoveOrdered then InsertAt, and the indices are re-read between the two: the removal shifts
	//! every flat index after it down by one, so an insertion point taken before it would be off by
	//! one whenever a preset moves down its list.
	bool Reorder(string kitName, string name, int newPos)
	{
		if (!EnsureWritable())
			return false;

		array<int> indices = {};
		KitIndices(kitName, indices);

		int from = -1;
		foreach (int i, int flat : indices)
		{
			if (m_aPresets[flat].m_sName == name)
			{
				from = i;
				break;
			}
		}

		if (from < 0)
			return false;

		if (newPos < 0)
			newPos = 0;
		if (newPos > indices.Count() - 1)
			newPos = indices.Count() - 1;
		if (newPos == from)
			return false;

		RK29_KitPreset moved = m_aPresets[indices[from]];
		m_aPresets.RemoveOrdered(indices[from]);

		KitIndices(kitName, indices);
		if (indices.IsEmpty())
			m_aPresets.Insert(moved);			// unreachable: a lone preset cannot move off itself
		else if (newPos < indices.Count())
			m_aPresets.InsertAt(moved, indices[newPos]);
		else
			m_aPresets.InsertAt(moved, indices[indices.Count() - 1] + 1);

		WriteToStorage();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Where one class's presets sit in the flat array, in list order. The bridge between the two
	//! index spaces Reorder has to keep apart.
	protected void KitIndices(string kitName, notnull array<int> outIndices)
	{
		outIndices.Clear();

		if (kitName == "")
			return;

		foreach (int i, RK29_KitPreset preset : m_aPresets)
		{
			if (preset && preset.m_sKitName == kitName)
				outIndices.Insert(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! RemoveOrdered, not Remove: Remove is a swap-remove that drops the last element into the hole,
	//! and this array's order is the order the section lists its rows in.
	bool Delete(string kitName, string name)
	{
		if (!EnsureWritable())
			return false;

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
		if (kitName == "")
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
		if (kitName == "" || name == "" || !EnsureLoaded())
			return -1;

		foreach (int i, RK29_KitPreset preset : m_aPresets)
		{
			if (preset && preset.m_sKitName == kitName && preset.m_sName == name)
				return i;
		}

		return -1;
	}

	//------------------------------------------------------------------------------------------------
	protected static RK29_KitSetup SetupOrNull()
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return null;

		return mgr.Setup();
	}

	//------------------------------------------------------------------------------------------------
	//! False while nothing could be loaded yet - only an import needs the setup, so that is the one
	//! case that waits; every caller answers "no presets" meanwhile rather than guessing.
	protected bool EnsureLoaded()
	{
		if (m_bLoaded)
			return true;

		if (FileIO.FileExists(PRESETS_FILE))
		{
			ReadOwnFile();
			m_bLoaded = true;
			return true;
		}

		RK29_KitSetup setup = SetupOrNull();
		if (!setup)
			return false;

		m_bLoaded = true;
		ImportOlder(setup);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected bool EnsureWritable()
	{
		if (!EnsureLoaded())
			return false;

		if (m_bReadOnly)
		{
			Print("[RK29] kit presets: " + PRESETS_FILE + " could not be read this session - saved"
				+ " kits are read-only until it is fixed or removed", LogLevel.WARNING);
			return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Our own file. One that exists and does not read, or reads as another shape, makes the store
	//! read-only for the session: overwriting it would erase whatever is in it, and re-importing
	//! over it would bring back kits the player has deleted since.
	protected void ReadOwnFile()
	{
		RK29_KitPresetFile file = new RK29_KitPresetFile();
		if (!file.LoadFromFile(PRESETS_FILE) || file.m_iVersion != VERSION)
		{
			m_bReadOnly = true;
			Print("[RK29] kit presets: " + PRESETS_FILE + " exists but could not be read - saved kits"
				+ " are read-only this session and the file is left as it is", LogLevel.WARNING);
			return;
		}

		if (!file.m_aRecords)
			return;

		foreach (string record : file.m_aRecords)
		{
			RK29_KitPreset preset = DecodeRecord(record);
			if (preset)
				m_aPresets.Insert(preset);
			else
				m_aUndecodable.Insert(record);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Our own file does not exist: take the newest older store once, convert it and write our own.
	//! The older file is never written, so the build that owns it keeps working. One that exists but
	//! does not read imports nothing and writes nothing - the next Save starts our file.
	protected void ImportOlder(notnull RK29_KitSetup setup)
	{
		if (!FileIO.FileExists(V1_FILE))
			return;

		RK29_KitPresetFile file = new RK29_KitPresetFile();
		if (!file.LoadFromFile(V1_FILE) || file.m_iVersion != 1 || !file.m_aRecords)
		{
			Print("[RK29] kit presets: " + V1_FILE + " could not be read - nothing imported",
				LogLevel.WARNING);
			return;
		}

		foreach (string record : file.m_aRecords)
		{
			RK29_KitPreset preset = ConvertV1Record(record, setup);
			if (preset)
				m_aPresets.Insert(preset);
			else
				m_aUndecodable.Insert(record);
		}

		Print(string.Format("[RK29] kit presets: imported %1 record(s) from %2 into %3",
			file.m_aRecords.Count(), V1_FILE, PRESETS_FILE), LogLevel.NORMAL);
		WriteToStorage();
	}

	//------------------------------------------------------------------------------------------------
	//! v1 record "1|kitName|picks|name", name last and unescaped. Null for one that does not decode
	//! (kept raw by the caller). One of another dialect, or of a class this config has no longer,
	//! is carried raw rather than converted: it cannot be expanded, and it must not be lost.
	//!
	//! The overlay is what keeps a converted kit honest: expanding alone would bake today's fallback
	//! for every v1 pick the config no longer honours into the kit and call it unchanged. Putting the
	//! v1 pick back over the expansion leaves ChangedCount to see the difference. v1 picks for groups
	//! outside the offer are dropped - indistinguishable from the other rifle's inert leftovers.
	protected static RK29_KitPreset ConvertV1Record(string record, notnull RK29_KitSetup setup)
	{
		array<string> parts = {};
		record.Split("|", parts, false);
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

		RK29_ClassSetup cls = setup.FindClass(preset.m_sKitName);
		if (preset.m_iFormat != V1_PICKS_FORMAT || !cls)
		{
			preset.m_sRaw = record;
			return preset;
		}

		array<ref RK29_ChoicePick> old = {};
		RK29_KitResolve.ParsePicks(preset.m_sPicks, old);

		array<ref RK29_ResolvedGroup> offer = {};
		RK29_KitResolve.BuildOffer(cls, setup, old, offer);

		array<ref RK29_ChoicePick> full = {};
		RK29_KitResolve.ExpandPicks(offer, old, full);

		foreach (RK29_ChoicePick pick : old)
		{
			if (!pick)
				continue;

			RK29_ResolvedGroup g = RK29_KitResolve.FindGroup(offer, pick.m_sGroup);
			if (!g)
				continue;

			bool counted = !g.m_bLoaded && !g.IsWeaponGroup() && !g.IsAttachmentGroup()
				&& g.m_eKind != RK29_EChoiceKind.EXCLUSIVE;
			for (int j = full.Count() - 1; j >= 0; j--)
			{
				if (full[j].m_sGroup == pick.m_sGroup && (!counted || full[j].m_sEntry == pick.m_sEntry))
					full.RemoveOrdered(j);
			}
			full.Insert(pick);
		}

		preset.m_sPicks = RK29_KitResolve.EncodePicks(full);
		preset.m_iFormat = PICKS_FORMAT;
		return preset;
	}

	//------------------------------------------------------------------------------------------------
	//! Writes the whole store. PackToFile, not SaveToFile: SaveToFile only rewrites data a previous
	//! load left on the struct, and a struct just built by hand has none.
	protected void WriteToStorage()
	{
		if (m_bReadOnly)
			return;

		RK29_KitPresetFile file = new RK29_KitPresetFile();
		file.m_iVersion = VERSION;

		foreach (RK29_KitPreset preset : m_aPresets)
		{
			if (preset)
				file.m_aRecords.Insert(EncodeRecord(preset));
		}

		foreach (string raw : m_aUndecodable)
			file.m_aRecords.Insert(raw);

		if (!file.PackToFile(PRESETS_FILE))
		{
			Print("[RK29] kit presets: could not write " + PRESETS_FILE
				+ " - presets are session-only", LogLevel.WARNING);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! "2|kitName|picks|name[|key=value]...". The kit name and picks are machine-written and hold no
	//! "|" (RK29_KitLint refuses one in any authored id or kit name); the name is typed, so it is
	//! escaped instead.
	protected static string EncodeRecord(notnull RK29_KitPreset preset)
	{
		if (preset.m_sRaw != "")
			return preset.m_sRaw;

		string record = preset.m_iFormat.ToString() + "|" + preset.m_sKitName + "|" + preset.m_sPicks
			+ "|" + EscapeName(preset.m_sName);
		foreach (string extra : preset.m_aExtra)
			record += "|" + extra;

		return record;
	}

	//------------------------------------------------------------------------------------------------
	//! Null for anything that is not a record at all. A record of another dialect decodes as far as
	//! its name, for the row, and keeps its raw text for the write.
	//!
	//! skipEmptyEntries is FALSE and must stay false: a picks field can be empty, and skipping it
	//! shifts every field after it - the Split bug that silently dropped kits saved at defaults.
	protected static RK29_KitPreset DecodeRecord(string record)
	{
		array<string> parts = {};
		record.Split("|", parts, false);
		if (parts.Count() < RECORD_FIELDS)
			return null;

		string name = UnescapeName(parts[RECORD_FIELDS - 1]);
		if (name == "" || parts[1] == "")
			return null;

		RK29_KitPreset preset = new RK29_KitPreset();
		preset.m_iFormat = parts[0].ToInt();
		preset.m_sKitName = parts[1];
		preset.m_sPicks = parts[2];
		preset.m_sName = name;

		if (preset.m_iFormat != PICKS_FORMAT)
		{
			preset.m_sRaw = record;
			return preset;
		}

		for (int i = RECORD_FIELDS; i < parts.Count(); i++)
		{
			if (parts[i] != "")
				preset.m_aExtra.Insert(parts[i]);
		}

		return preset;
	}

	//------------------------------------------------------------------------------------------------
	//! "%" first, so the "%" an escape writes is never escaped again.
	protected static string EscapeName(string name)
	{
		string escaped = name;
		escaped.Replace("%", "%25");
		escaped.Replace("|", "%7C");
		return escaped;
	}

	//------------------------------------------------------------------------------------------------
	//! The reverse order: "%7C" before "%25", or a typed "%7C" (stored "%257C") would come back "|".
	protected static string UnescapeName(string escaped)
	{
		string name = escaped;
		name.Replace("%7C", "|");
		name.Replace("%25", "%");
		return name;
	}
}
