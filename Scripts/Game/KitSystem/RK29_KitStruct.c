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

	//! Instanced at capture, shared read-only. Do NOT store the BaseContainer instead -
	//! containers die with their resource even behind a held ref Resource.
	ref SCR_UIInfo m_UIInfo;

	//--------------------------------------------------------------------------------------------
	//! Deep copy with weapon choice + mag auto-swap applied. Optic is handled at apply, not here.
	//! magCount > 0 replaces the kit's captured mag load with exactly that many of chosenMag.
	RK29_KitStruct CloneWithChoices(ResourceName chosenWeapon, ResourceName chosenMag, array<ResourceName> classMags, int magCount = 0)
	{
		RK29_KitStruct c = new RK29_KitStruct();
		c.m_sKitName      = m_sKitName;
		c.m_sFactionKey   = m_sFactionKey;
		c.m_sSourcePrefab = m_sSourcePrefab;
		c.m_UIInfo        = m_UIInfo;
		c.m_sPrimaryWeapon = m_sPrimaryWeapon;

		foreach (string slot, ResourceName res : m_mClothing)
			c.m_mClothing.Set(slot, res);

		foreach (string eqSlot, ResourceName eqRes : m_mEquipment)
			c.m_mEquipment.Set(eqSlot, eqRes);

		foreach (int idx, ResourceName res : m_mWeapons)
		{
			if (idx == 0 && chosenWeapon != ResourceName.Empty)
			{
				c.m_mWeapons.Set(idx, chosenWeapon);
				c.m_sPrimaryWeapon = chosenWeapon;
			}
			else
				c.m_mWeapons.Set(idx, res);
		}

		bool swapMags = chosenMag != ResourceName.Empty && classMags != null && !classMags.IsEmpty();
		bool fixedCount = swapMags && magCount > 0;

		foreach (RK29_KitItemBatch batch : m_aItems)
		{
			RK29_KitItemBatch nb = new RK29_KitItemBatch();
			nb.m_sTargetHint = batch.m_sTargetHint;
			nb.m_aPreferred  = batch.m_aPreferred;          // placement prefs survive a swap
			nb.m_bPrimaryAttachment = batch.m_bPrimaryAttachment;
			foreach (ResourceName item : batch.m_aPrefabs)
			{
				if (swapMags && classMags.Contains(item))
				{
					if (!fixedCount)
						nb.m_aPrefabs.Insert(chosenMag);
					// fixedCount: captured mags dropped here, re-added as one batch below
				}
				else
				{
					nb.m_aPrefabs.Insert(item);
				}
			}
			if (!nb.m_aPrefabs.IsEmpty())
				c.m_aItems.Insert(nb);
		}

		if (fixedCount)
		{
			RK29_KitItemBatch mags = new RK29_KitItemBatch();
			for (int i = 0; i < magCount; i++)
				mags.m_aPrefabs.Insert(chosenMag);
			c.m_aItems.Insert(mags);
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
