//------------------------------------------------------------------------------------------------
//! One kit's complete content story - Configs/KitSystem/Kits/**.conf. A kit's groups are its five
//! sections: a *Ref element points at a catalog group by id, anything else is defined inline. A
//! later clothing group on a slot replaces an earlier one, and parent-conf elements come before
//! the child's, so a kit's own hat beats the shared hat. A worn slot nothing answers stays empty.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! Compositions may inherit each other (conf-from-conf).
[BaseContainerProps(configRoot: true)]
class RK29_KitComposition : RK29_ChoiceGroupSet
{
	[Attribute(desc: "This kit's doctrine, in ONE ordered list: an RK29_Override written inline, or an RK29_OverrideRef naming one from an override catalog. Applied in list order, so a role can set doctrine (mag bounds on 'assault_ammo') that every inheriting faction kit gets and a faction kit appends its exceptions with '+'. Later wins for adjustments, FIRST wins for substitutions - the position in this list decides both, whether the step is inline or referenced", category: "29th")]
	ref array<ref RK29_OverrideStep> m_aOverrides;

	[Attribute(desc: "Choices this kit rules out on account of other choices - the belt gun forbidding the magazine rig. Applied in array order after the offer is whole, and a blocked entry counts as nothing, so an exclusion can never be answered by something an earlier exclusion already ruled out", category: "29th")]
	ref array<ref RK29_Exclusion> m_aExclusions;

	[Attribute(desc: "Icon, preview image and browser labels for this role - what it IS, stated beside its traits rather than in the roster. Faction kits inherit it and add only what differs (preview image, faction). The roster keeps m_sDisplayName, the short picker label", category: "29th")]
	ref SCR_EditableEntityUIInfo m_UIInfo;

	[Attribute(uiwidget: UIWidgets.ComboBox, desc: "What this role is qualified at - a medic dresses wounds faster, a sapper builds faster. Declared on the shared role file so every faction's kits inherit it; a faction kit restating the list REPLACES it, and '+' appends. Applied to the body at kit apply, so re-kitting to another class drops them", enums: ParamEnumArray.FromEnum(RK29_ETrait), category: "29th")]
	ref array<RK29_ETrait> m_aTraits;
}
