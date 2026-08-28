//------------------------------------------------------------------------------------------------
//! Runtime shadow of the authored group class; nothing authors this. ITEM must stay the zero value
//! - the synthesized "<id>_loaded" selector has no authored group behind it and its rows are item
//! entries.
enum RK29_EGroupType
{
	ITEM = 0,
	WEAPON = 1,
	ATTACHMENT = 2,
	CLOTHING = 3,
	GARMENT_ATTACHMENT = 4,
}

//------------------------------------------------------------------------------------------------
//! Post-override view of one group as offered to one class. Deliberately does not split the way the
//! authored classes do: picker, wire, apply pass and validator all walk a mixed array of these, and
//! carrying every field is what keeps those loops flat.
class RK29_ResolvedGroup
{
	string m_sId;
	string m_sDisplayName;

	//! What the config declared. Copied in ResolveGroup and never derived again - sniffing whichever
	//! entries survived filtering let a group answer to two kinds at once.
	RK29_EGroupType m_eGroupType;

	RK29_EChoiceKind m_eKind;
	int m_iBudget;
	int m_iKeepRank = -1;    // item groups: the pool's keep rank, -1 = not stated (RowFor reads it)
	bool m_bAllowEmpty;      // taking nothing is an answer: a bare seat, or a weapon slot left empty
	bool m_bIsOpticsPoint;   // attachment groups: this point holds sights - badge, zoom, tally
	bool m_bMagnifiedExempt; // attachment groups: an optic taken here is not magnified DOCTRINE
	bool m_bCarryWhenUnfitted; // attachment groups: a None answer stows the item rather than dropping it
	string m_sOwnerWeapon;   // weapon id whose definition owns this group; "" = kit-level
	bool m_bLoaded;          // synthesized loaded-magazine selector, not authored config
	RK29_ELoadedSeat m_eLoadedSeat; // whether this group seats a round, and which muzzle takes it
	int m_iOrder;                    // detail-column placement; higher sinks
	string m_sWornSlot;              // CLOTHING groups: the garment slot every entry fills
	string m_sGarmentSlot;           // GARMENT_ATTACHMENT groups: loadout slot of the host garment (Hat)
	string m_sSlotOnGarment;         // GARMENT_ATTACHMENT groups: the slot ON that garment (NVG)
	ref array<ref RK29_ResolvedEntry> m_aEntries = {};

	//! Catalog groups this one absorbed through m_aIncludeGroups, by id. An override or exclusion addressed
	//! to "grenades" must still reach the superset that swallowed it (grenades_signal). Empty on a
	//! group that includes nothing.
	ref array<string> m_aIncludedGroups = {};

	//! Every mount type this group's surviving entries declare, filled by
	//! RK29_KitResolve.DeriveSeatTypes and authored by nobody - a hand-stated slot type could be
	//! wrong (smaw_optics said AttachmentOptics for a sight seating in RHS's AttachmentMBS).
	//! Empty on every non-attachment group, which is what makes SharesSeatWith safe across a whole
	//! offer.
	ref array<string> m_aSeatTypes = {};

	//------------------------------------------------------------------------------------------------
	RK29_ResolvedEntry FindEntry(string id)
	{
		foreach (RK29_ResolvedEntry e : m_aEntries)
		{
			if (e && e.m_sId == id)
				return e;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! The canonical pick: the flagged entry, else bare (when legal), else the first entry.
	RK29_ResolvedEntry DefaultEntry()
	{
		foreach (RK29_ResolvedEntry e : m_aEntries)
		{
			if (e && e.m_bDefault && !e.m_bBlocked)
				return e;
		}
		if (m_bAllowEmpty)
			return null;

		// the first surviving entry, not simply the first: where two exclusions block each other's
		// answer, the flagged default is exactly the one that got blocked
		return FirstUnblockedEntry();
	}

	//------------------------------------------------------------------------------------------------
	//! DefaultEntry's fallback, exposed for callers that must name something where DefaultEntry
	//! legitimately answers null (the bare seat whose bayonet is stowed rather than fitted).
	RK29_ResolvedEntry FirstUnblockedEntry()
	{
		foreach (RK29_ResolvedEntry e : m_aEntries)
		{
			if (e && !e.m_bBlocked)
				return e;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	bool IsWeaponGroup()
	{
		return m_eGroupType == RK29_EGroupType.WEAPON;
	}

	//------------------------------------------------------------------------------------------------
	bool IsClothingGroup()
	{
		return m_eGroupType == RK29_EGroupType.CLOTHING;
	}

	//------------------------------------------------------------------------------------------------
	bool IsAttachmentGroup()
	{
		return m_eGroupType == RK29_EGroupType.ATTACHMENT;
	}

	//------------------------------------------------------------------------------------------------
	bool IsGarmentAttachmentGroup()
	{
		return m_eGroupType == RK29_EGroupType.GARMENT_ATTACHMENT;
	}

	//------------------------------------------------------------------------------------------------
	//! Do these two groups compete for one seat on one gun - optics_1x and optics_magnified over a
	//! rifle's single rail - so seating out of one has to bare the other? Mount-compatible rather
	//! than string-identical, tried both ways round through RK29_KitResolve.SharesSeatWithType.
	bool SharesSeatWith(notnull RK29_ResolvedGroup other)
	{
		if (m_sOwnerWeapon != other.m_sOwnerWeapon)
			return false;

		foreach (string theirs : other.m_aSeatTypes)
		{
			if (RK29_KitResolve.SharesSeatWithType(this, theirs))
				return true;
		}
		return false;
	}
}
