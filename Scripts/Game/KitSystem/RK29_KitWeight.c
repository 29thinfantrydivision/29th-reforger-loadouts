//------------------------------------------------------------------------------------------------
//! What a kit weighs: ask the dressed body through vanilla's own
//! SCR_InventoryStorageManagerComponent.GetTotalWeightOfAllStorages() - the call
//! SCR_InventoryMenuUI:713 makes - and format it. Never re-implement that walk, or the readout
//! and the inventory screen will disagree.
//------------------------------------------------------------------------------------------------
class RK29_KitWeight
{
	//! What LiveTotal answers when there is no body to ask. Not zero - zero kilograms is a claim
	//! about a loadout. Callers test for negative and stamp nothing
	//! (RK29_MenuInfoBand.BuildWeightSection).
	static const float NO_WEIGHT = -1;

	//------------------------------------------------------------------------------------------------
	//! Kilograms carried by `body`, which must already be dressed for the current picks - this
	//! reads a body, not a kit. RK29_LoadoutMenu dresses the mannequin at the head of every flow
	//! that moves the offer, then stamps the info band.
	//!
	//! GetTotalWeightOfAllStorages is the whole of it - weapon storage plus own storage, not
	//! GetStorages(), which double-counts. It already includes rounds in magazines, the round in
	//! each gun, every attachment, and prefab-authored contents of a vest's cloth slots.
	//!
	//! NO_WEIGHT (not zero) for a missing body or one with no storage manager.
	static float LiveTotal(IEntity body)
	{
		if (!body)
			return NO_WEIGHT;

		SCR_InventoryStorageManagerComponent manager = SCR_InventoryStorageManagerComponent.Cast(
			body.FindComponent(SCR_InventoryStorageManagerComponent));

		if (!manager)
			return NO_WEIGHT;

		return manager.GetTotalWeightOfAllStorages();
	}

	//------------------------------------------------------------------------------------------------
	static string WeightLabel(float kilograms)
	{
		return WeightDecimal(kilograms) + " kg";
	}

	//------------------------------------------------------------------------------------------------
	//! Two decimals, matching the inventory screen and the resolution attachments are authored at
	//! (a flash hider is 0.05 kg). Done in integer hundredths because script string formatting has
	//! no precision control ("12.399999"); the fraction is zero-padded or 12.05 prints as "12.5".
	protected static string WeightDecimal(float kilograms)
	{
		int hundredths = Math.Round(kilograms * 100);
		if (hundredths < 0)
			hundredths = 0;

		string fraction = (hundredths % 100).ToString();
		if (fraction.Length() < 2)
			fraction = "0" + fraction;

		return (hundredths / 100).ToString() + "." + fraction;
	}
}
