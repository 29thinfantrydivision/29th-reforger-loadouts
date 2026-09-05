//------------------------------------------------------------------------------------------------
//! Post-override view of one entry. m_Def is the catalog object and is never mutated - the
//! numbers overrides adjust live here as copies.
class RK29_ResolvedEntry
{
	string m_sId;
	bool m_bDefault;
	ref RK29_ChoiceEntryBase m_Def;
	int m_iMin;
	int m_iDefault;
	//! -1 = uncapped. Only ITEM entries copy a cap, so weapon/attachment entries must not default
	//! to 0: 0 means "not offered" and the zero-cap drop would delete every weapon group.
	int m_iMax = -1;
	int m_iCost = 1;

	//! Set by EnforceAttachmentLegality: the entry stays in the offer and on screen, marked rather
	//! than removed. Stored as who, not as a sentence - the menu writes the wording. Empty
	//! m_sBlockedGroup means the weapon itself is the reason (a welded-on M203), so there is no
	//! row to point at.
	bool m_bBlocked;
	bool m_bBlockedMissing;   // true = it needs something absent; false = something present is in the way
	string m_sBlockedGroup;
	string m_sBlockedEntry;
	//! Exclusion blocks only: the count the culprit had to exceed ("over 4"). -1 = not an exclusion block.
	int m_iBlockedOver = -1;

	//! What taking this entry rules out, as an authored "group/entry" path. Filled whether or not
	//! the exclusion is firing - the player must see the consequence before the click, so it cannot be
	//! derived from what is already blocked.
	string m_sExcludes;
}
