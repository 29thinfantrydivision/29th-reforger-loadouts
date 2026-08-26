//------------------------------------------------------------------------------------------------
//! Captured kit data, mirroring the prefab's authoring format. Built by RK29_KitCapture.
//------------------------------------------------------------------------------------------------

class RK29_KitItemBatch
{
	//! TargetStorage path from the prefab - placement hint only.
	string m_sTargetHint;

	//! Authored placement preference, best first. Resolved at compose time from the item
	//! entry, then the alias. Empty means the apply pass decides.
	ref array<string> m_aPreferred;

	ref array<ResourceName> m_aPrefabs = {};

	//! "only if the primary can take it" - a bayonet. Decided AFTER the weapon is chosen,
	//! not at compose time: the base kit still carries the prefab's weapon, and a class can
	//! offer one weapon with a lug and another without.
	bool m_bPrimaryAttachment;
}

//------------------------------------------------------------------------------------------------
class RK29_KitStruct
{
	//! Matches m_sLoadoutName in GM29_Kits.conf - the identity key everywhere.
	string m_sKitName;

	string m_sFactionKey;

	ResourceName m_sSourcePrefab;

	//! loadout slot name -> prefab (names for logs only; slots are type-gated at apply)
	ref map<string, ResourceName> m_mClothing = new map<string, ResourceName>();

	//! equipment storage slot name -> prefab (WristwatchSlot, BinocularSlot, ...)
	ref map<string, ResourceName> m_mEquipment = new map<string, ResourceName>();

	//! weapon slot index -> prefab; grenade slot stored as 100
	ref map<int, ResourceName> m_mWeapons = new map<int, ResourceName>();
	static const int GRENADE_SLOT = 100;

	ResourceName m_sPrimaryWeapon;

	ref array<ref RK29_KitItemBatch> m_aItems = {};

	//! Role qualifications, granted as character labels at apply (medic, sapper, ...).
	//! Composition-owned: a weapon choice can never add or drop one.
	ref array<RK29_ETrait> m_aTraits = {};

	//! Instanced at capture, shared read-only. Do NOT store the BaseContainer instead -
	//! containers die with their resource even behind a held ref Resource.
	ref SCR_UIInfo m_UIInfo;

	//--------------------------------------------------------------------------------------------
	//! Deep copy. Weapon, mag and optic choices are all laid over the clone by the caller
	//! (RK29_KitCompose) - this used to take them as parameters, but no caller ever did.
	RK29_KitStruct Clone()
	{
		RK29_KitStruct c = new RK29_KitStruct();
		c.m_sKitName      = m_sKitName;
		c.m_sFactionKey   = m_sFactionKey;
		c.m_sSourcePrefab = m_sSourcePrefab;
		c.m_UIInfo        = m_UIInfo;
		c.m_sPrimaryWeapon = m_sPrimaryWeapon;

		foreach (string slot, ResourceName res : m_mClothing)
			c.m_mClothing.Set(slot, res);

		foreach (RK29_ETrait trait : m_aTraits)
			c.m_aTraits.Insert(trait);

		foreach (string eqSlot, ResourceName eqRes : m_mEquipment)
			c.m_mEquipment.Set(eqSlot, eqRes);

		foreach (int idx, ResourceName res : m_mWeapons)
			c.m_mWeapons.Set(idx, res);

		foreach (RK29_KitItemBatch batch : m_aItems)
		{
			RK29_KitItemBatch nb = new RK29_KitItemBatch();
			nb.m_sTargetHint = batch.m_sTargetHint;
			nb.m_aPreferred  = batch.m_aPreferred;
			nb.m_bPrimaryAttachment = batch.m_bPrimaryAttachment;
			foreach (ResourceName item : batch.m_aPrefabs)
				nb.m_aPrefabs.Insert(item);
			if (!nb.m_aPrefabs.IsEmpty())
				c.m_aItems.Insert(nb);
		}

		return c;
	}

	//--------------------------------------------------------------------------------------------
	int CountItems()
	{
		int n = 0;
		foreach (RK29_KitItemBatch batch : m_aItems)
			n += batch.m_aPrefabs.Count();
		return n;
	}
}
