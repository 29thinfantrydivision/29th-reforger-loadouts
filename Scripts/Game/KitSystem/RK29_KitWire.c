//------------------------------------------------------------------------------------------------
//! The resolved kit, in a form a client can be handed.
//!
//! Vanilla's arsenal keeps the player's actual loadout client-side and draws the mannequin from
//! it. We were sending three loose fields - kit name, optic, weapon - and letting the client
//! reassemble the rest off whatever prefab the preview happened to spawn. That is why a class
//! with two weapon options previewed the wrong gun: the client was guessing, and the server
//! never told it the answer.
//!
//! So send the answer. Dress by slot name, weapons by slot INDEX (the same space apply uses, so
//! no heuristic has to work out which one is the rifle). Items are left out - they never reach
//! a mannequin. A string rather than a struct because that is what the RPC layer takes, and
//! because vanilla does the same thing with ApplyLoadoutString.
//------------------------------------------------------------------------------------------------
class RK29_KitWire
{
	protected static const string SECTION = "|";
	protected static const string ENTRY   = ";";
	protected static const string PAIR    = "=";

	//--------------------------------------------------------------------------------------------
	//! "Hat={GUID}path;Jacket={GUID}path|0={GUID}rifle;2={GUID}pistol"
	//! ResourceNames contain none of the delimiters, so no escaping is needed.
	static string Pack(RK29_KitStruct kit)
	{
		if (!kit)
			return "";

		string dress;
		foreach (string slot, ResourceName garment : kit.m_mClothing)
		{
			if (garment == ResourceName.Empty)
				continue;
			if (dress != "")
				dress = dress + ENTRY;
			dress = dress + slot + PAIR + garment;
		}

		string guns;
		foreach (int idx, ResourceName weapon : kit.m_mWeapons)
		{
			if (weapon == ResourceName.Empty || idx == RK29_KitStruct.GRENADE_SLOT)
				continue;
			if (guns != "")
				guns = guns + ENTRY;
			guns = guns + idx.ToString() + PAIR + weapon;
		}

		return dress + SECTION + guns;
	}

	//--------------------------------------------------------------------------------------------
	//! Fills the caller's maps. Silent on a malformed entry: a preview is not worth a hard fail.
	static void Unpack(string wire, notnull out map<string, ResourceName> outDress,
		notnull out map<int, ResourceName> outWeapons)
	{
		outDress.Clear();
		outWeapons.Clear();
		if (wire == "")
			return;

		array<string> sections = {};
		wire.Split(SECTION, sections, false);

		if (sections.Count() > 0)
		{
			array<string> entries = {};
			sections[0].Split(ENTRY, entries, true);
			foreach (string entry : entries)
			{
				array<string> pair = {};
				entry.Split(PAIR, pair, true);
				if (pair.Count() == 2 && pair[0] != "")
					outDress.Set(pair[0], pair[1]);
			}
		}

		if (sections.Count() > 1)
		{
			array<string> gunEntries = {};
			sections[1].Split(ENTRY, gunEntries, true);
			foreach (string gunEntry : gunEntries)
			{
				array<string> gunPair = {};
				gunEntry.Split(PAIR, gunPair, true);
				if (gunPair.Count() == 2 && gunPair[0] != "")
					outWeapons.Set(gunPair[0].ToInt(), gunPair[1]);
			}
		}
	}
}
