//------------------------------------------------------------------------------------------------
//! Unit badges a choice entry can be marked with in the picker (RK29_ChoiceEntryBase.m_eBadge).
//! A mark and nothing more, like Donor Only: no resolver, validator or apply path reads it.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! A new member needs a case in NameOf and in IconOf; until it has an icon its badge never shows.
enum RK29_EBadge
{
	NONE,				//!< nothing - a fresh entry reads as unmarked
	SHARPSHOOTER_RIFLE
}

//------------------------------------------------------------------------------------------------
class RK29_Badges
{
	protected static const ResourceName SHARPSHOOTER_RIFLE_ICON
		= "{AB29C0FFEE2B4D01}UI/Textures/KitSystem/29th_Badge_Sharpshooter_Rifle.edds";

	//------------------------------------------------------------------------------------------------
	//! The badge's own name, as the unit writes it: the hover tip, the Workbench dropdown and the
	//! entry's title in the config tree all read this.
	static string NameOf(RK29_EBadge badge)
	{
		switch (badge)
		{
			case RK29_EBadge.NONE:					return "None";
			case RK29_EBadge.SHARPSHOOTER_RIFLE:	return "Sharpshooter Badge: Rifle";
		}

		return typename.EnumToString(RK29_EBadge, badge);
	}

	//------------------------------------------------------------------------------------------------
	static ResourceName IconOf(RK29_EBadge badge)
	{
		switch (badge)
		{
			case RK29_EBadge.SHARPSHOOTER_RIFLE:	return SHARPSHOOTER_RIFLE_ICON;
		}

		return ResourceName.Empty;
	}

	//------------------------------------------------------------------------------------------------
	//! The m_eBadge dropdown, labelled by NameOf rather than the enum's shouting names.
	static ParamEnumArray ParamEnums()
	{
		ParamEnumArray params = new ParamEnumArray();

		array<int> values = {};
		SCR_Enum.GetEnumValues(RK29_EBadge, values);
		foreach (int value : values)
		{
			params.Insert(new ParamEnum(NameOf(value), value.ToString()));
		}

		return params;
	}
}