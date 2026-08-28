//------------------------------------------------------------------------------------------------
//! Role qualifications a composition grants. Each trait is one vanilla EEditableEntityLabel; the
//! speed bonus is whatever the base game does with that label - we grant it, we set no multipliers.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
enum RK29_ETrait
{
	NONE,			//!< nothing - a fresh row reads as unset rather than as a medic
	MEDIC,			//!< field dressing 1.5x, tourniquet 1.2x, casualty inspect/load/heal at a station 2x
	SAPPER,			//!< building 2x, deploying multi-part fortifications 2x, vehicle repair 2x
	VEHICLE_CREW	//!< vehicle repair, refuel, rearm and supply unloading 2x
}

//------------------------------------------------------------------------------------------------
class RK29_Traits
{
	//------------------------------------------------------------------------------------------------
	static EEditableEntityLabel LabelOf(RK29_ETrait trait)
	{
		switch (trait)
		{
			case RK29_ETrait.MEDIC:			return EEditableEntityLabel.ROLE_MEDIC;
			case RK29_ETrait.SAPPER:		return EEditableEntityLabel.ROLE_SAPPER;
			case RK29_ETrait.VEHICLE_CREW:	return EEditableEntityLabel.TRAIT_VEHICLE_CREW;
		}
		return EEditableEntityLabel.NONE;
	}

	//------------------------------------------------------------------------------------------------
	static string NameOf(RK29_ETrait trait)
	{
		return typename.EnumToString(RK29_ETrait, trait);
	}
}
