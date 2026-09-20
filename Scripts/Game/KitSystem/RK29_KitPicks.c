//------------------------------------------------------------------------------------------------
//! Resolved player decisions: the picks, the rounds seated in each muzzle, and the attachment
//! orders the apply pass seats on a gun.
//------------------------------------------------------------------------------------------------

//! One player decision: which entry of a group, and how many where counts apply.
class RK29_ChoicePick
{
	string m_sGroup;
	string m_sEntry; // "" on an allow-empty EXCLUSIVE group = deliberately bare
	int m_iCount = 1;
}

//------------------------------------------------------------------------------------------------
//! One seated round and the muzzle that takes it. A rifle with an underbarrel launcher makes two
//! of these against the same weapon slot, so the destination is stated, never inferred.
//!
//! An empty prefab is an order to clear the muzzle, not an absence: a freshly spawned weapon
//! arrives with its own authored magazine already in the well, so sending nothing is not enough.
class RK29_LoadedPick
{
	ResourceName m_sPrefab;
	bool m_bUnderbarrel;
}

//------------------------------------------------------------------------------------------------
//! One answered attachment group, bound to the weapon that owns it - not necessarily the one in
//! slot 0 (the SMAW is the AT kit's second weapon), so the apply pass seats each group on the gun
//! that offered it. Only answered groups emit an order: several groups can share one seat (the 1x
//! and magnified tiers of the same optics point), so silence is not an instruction.
//!
//! m_sPrefab empty is an order to empty the seat - the only way a group removes anything, since
//! mounting is additive. m_sProbe names that seat by a prefab whose mount types match it (a named
//! attachment is its own probe); empty only on the KitManager's irons-only order, which has no
//! group behind it and falls back to "whatever sight is mounted".
class RK29_AttachmentOrder
{
	//! "" for a kit-level group and the irons-only order, both of which speak for the primary.
	//! Log only - m_iOwnerSlot is what the apply pass resolves by.
	string m_sOwnerWeaponId;

	//! Same key space the loaded-magazine picks use - the one thing the apply pass can map back to
	//! a live weapon entity.
	int m_iOwnerSlot;

	ResourceName m_sPrefab; // empty = evict whatever the probe's seat holds
	ResourceName m_sProbe;  // the seat this order speaks for, named by a prefab
}
