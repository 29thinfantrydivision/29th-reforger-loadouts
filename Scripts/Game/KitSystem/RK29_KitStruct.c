//------------------------------------------------------------------------------------------------
//! One kit: identity from RK29_KitCapture, gear from RK29_KitCompose and RK29_KitResolve.
//------------------------------------------------------------------------------------------------

class RK29_KitItemBatch
{
	//! Placement preference, best first, from the item entry then the alias. Empty = the apply
	//! pass decides.
	ref array<string> m_aPreferred;

	//! How important this is when the kit does not fit: lower keeps its place, higher gives way to
	//! it (RK29_KitApply.MakeRoomByRank). Not an insertion order - the solver still places by fit.
	static const int KEEP_RANK_DEFAULT = 50;
	int m_iKeepRank = KEEP_RANK_DEFAULT;

	ref array<ResourceName> m_aPrefabs = {};
}

//------------------------------------------------------------------------------------------------
class RK29_KitStruct
{
	//! Matches m_sLoadoutName in GM29_Kits.conf - the identity key everywhere.
	string m_sKitName;

	string m_sFactionKey;

	//! loadout slot name -> prefab (names for logs only; slots are type-gated at apply)
	ref map<string, ResourceName> m_mClothing = new map<string, ResourceName>();

	//! equipment storage slot name -> prefab (WristwatchSlot, BinocularSlot, ...)
	ref map<string, ResourceName> m_mEquipment = new map<string, ResourceName>();

	//! "<loadout slot>/<slot on that garment>" -> prefab (Hat/NVG), see GarmentSlotKey. Seated by
	//! RK29_KitApply.DressGarmentAttachments after the garment itself is dressed.
	ref map<string, ResourceName> m_mGarmentAttachments = new map<string, ResourceName>();

	//! weapon slot index -> prefab, as RK29_KitResolve.ClaimSlot numbers them (0/1 rifles, 2 sidearm).
	//! Grenades are never here: they are items, primed into the throwable slot by the apply pass.
	ref map<int, ResourceName> m_mWeapons = new map<int, ResourceName>();

	ResourceName m_sPrimaryWeapon;

	ref array<ref RK29_KitItemBatch> m_aItems = {};

	//! Granted as character labels at apply. Composition-owned: a weapon choice never adds or
	//! drops one.
	ref array<RK29_ETrait> m_aTraits = {};

	//! Instanced at capture, shared read-only. Do not store the BaseContainer instead -
	//! containers die with their resource even behind a held ref Resource.
	ref SCR_UIInfo m_UIInfo;

	//------------------------------------------------------------------------------------------------
	RK29_KitStruct DeepCopy()
	{
		RK29_KitStruct c = new RK29_KitStruct();
		c.m_sKitName      = m_sKitName;
		c.m_sFactionKey   = m_sFactionKey;
		c.m_UIInfo        = m_UIInfo;
		c.m_sPrimaryWeapon = m_sPrimaryWeapon;

		foreach (string slot, ResourceName res : m_mClothing)
			c.m_mClothing.Set(slot, res);

		foreach (RK29_ETrait trait : m_aTraits)
			c.m_aTraits.Insert(trait);

		foreach (string eqSlot, ResourceName eqRes : m_mEquipment)
			c.m_mEquipment.Set(eqSlot, eqRes);

		foreach (string attKey, ResourceName attRes : m_mGarmentAttachments)
			c.m_mGarmentAttachments.Set(attKey, attRes);

		foreach (int idx, ResourceName res : m_mWeapons)
			c.m_mWeapons.Set(idx, res);

		foreach (RK29_KitItemBatch batch : m_aItems)
		{
			RK29_KitItemBatch nb = new RK29_KitItemBatch();
			nb.m_aPreferred  = batch.m_aPreferred;
			nb.m_iKeepRank   = batch.m_iKeepRank;
			foreach (ResourceName item : batch.m_aPrefabs)
				nb.m_aPrefabs.Insert(item);
			if (!nb.m_aPrefabs.IsEmpty())
				c.m_aItems.Insert(nb);
		}

		return c;
	}

	//------------------------------------------------------------------------------------------------
	int CountItems()
	{
		int n = 0;
		foreach (RK29_KitItemBatch batch : m_aItems)
			n += batch.m_aPrefabs.Count();
		return n;
	}

	//------------------------------------------------------------------------------------------------
	//! The m_mGarmentAttachments key. Slash-joined like every other two-part address here
	//! ("group/entry"), so a second row for the same garment slot replaces the first, as a later
	//! clothing group on a slot replaces an earlier one.
	static string GarmentSlotKey(string garmentSlot, string slot)
	{
		return garmentSlot + "/" + slot;
	}

	//------------------------------------------------------------------------------------------------
	static bool SplitGarmentSlotKey(string key, out string garmentSlot, out string slot)
	{
		int cut = key.IndexOf("/");
		if (cut <= 0 || cut >= key.Length() - 1)
			return false;
		garmentSlot = key.Substring(0, cut);
		slot = key.Substring(cut + 1, key.Length() - cut - 1);
		return true;
	}
}

//------------------------------------------------------------------------------------------------
//! How much of one ammo type a kit carries with one weapon. The alias is resolved by the weapon
//! definition, so "belt" means one magazine on an M249 and another on a PKM.
//! Runtime DTO, not config - built only by RK29_KitResolve.ApplyItemGroup for
//! RK29_KitCompose.EmitAmmo, so it carries no [Attribute]s.
class RK29_WeaponAmmo
{
	string m_sAlias;

	//! Set only by a worn entry: the slot this row goes to instead of a container.
	string m_sSlot;

	//! Which map that slot lives in - worn clothing, or the equipment slots (binoculars, watch).
	bool m_bClothing;

	//! Set only by a garment-attachment group's row: the loadout slot of the garment m_sSlot is on,
	//! which routes the row to the garment-attachment map instead of either of the two above.
	string m_sGarmentSlot;

	//! Magazine variant from RK29_Magazines.conf, resolved through the weapon's magazine well.
	string m_sVariant;

	//! Literal prefab, for ammo that is not a magazine of this weapon. Wins over everything else.
	ResourceName m_sPrefab;

	int m_iCount = 1;

	//! The entry's own keep rank, else its group's; -1 = neither stated, so EmitCarriedRow falls
	//! back to the alias's or the weapon's.
	int m_iKeepRank = -1;
}
