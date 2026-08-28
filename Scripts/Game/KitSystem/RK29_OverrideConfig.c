//------------------------------------------------------------------------------------------------
//! Override: the three verbs a kit uses to bend a shared offer to its own doctrine - substitute a
//! group, adjust its numbers, add another - plus the exclusions one choice imposes on another.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! One targeted change to a group or entry a kit offers. -1 = leave that bound alone. The path
//! decides which fields do anything: bare "group" reads only m_iBudget, "group/entry" only bounds.
[BaseContainerProps(), BaseContainerCustomTitleField("m_sTarget")]
class RK29_OverrideAdjust
{
	[Attribute(desc: "What this adjusts, in three forms. \"group\" - the group of that id, whichever weapon offers it, and the only form there was: every target authored before the weapon axis still means exactly what it always did. \"weapon:group\" - only the group owned by THAT catalog weapon id, which is how a kit carrying two optic-capable guns speaks to one of them. \"group/entry\" or \"weapon:group/entry\" - one entry within it. The weapon axis is ':', the entry axis is '/'", category: "29th")]
	string m_sTarget;

	[Attribute("-1", desc: "ENTRY targets: new min for the entry (-1 = keep)", category: "29th")]
	int m_iMin;

	[Attribute("-1", desc: "ENTRY targets: new default count for the entry (-1 = keep)", category: "29th")]
	int m_iDefault;

	[Attribute("-1", desc: "ENTRY targets: new max for the entry (-1 = keep)", category: "29th")]
	int m_iMax;

	[Attribute("0", desc: "ENTRY targets on an EXCLUSIVE group: make THIS entry the one the kit starts on. Counts mean nothing to an exclusive group - it answers with an entry - so this is the only adjustment that reaches one. Setting it clears the flag from the group's other entries, because the default is whichever flagged entry comes first", category: "29th")]
	bool m_bDefault;

	[Attribute("-1", desc: "GROUP targets: new budget for a BUDGETED group (-1 = keep)", category: "29th")]
	int m_iBudget;
}

//------------------------------------------------------------------------------------------------
class RK29_OverrideSubstituteTitle : BaseContainerCustomTitle
{
	//------------------------------------------------------------------------------------------------
	override bool _WB_GetCustomTitle(BaseContainer source, out string title)
	{
		// Get leaves the out-param untouched when the conf never authored the attribute, so
		// "absent" has to be a value already held here
		string target = "";
		source.Get("m_sTarget", target);
		if (target == "")
			target = "(no target)";

		string replaceWith = "";
		source.Get("m_sReplaceWith", replaceWith);
		if (replaceWith == "")
			title = target + " -> (removed)";
		else
			title = target + " -> " + replaceWith;
		return true;
	}
}

//------------------------------------------------------------------------------------------------
//! One group a kit offers something else in place of - "let this weapon have 4x and that one not",
//! which the numeric adjustments cannot say.
//!
//! Runs at group-build time, not with the adjustments: the replacement must be capability-pruned
//! against the owning weapon, and pruning is over by the time the numbers are applied. See
//! RK29_KitResolve.BuildOffer for the order. Substituting in a group flagged m_bMagnifiedExempt is
//! also how a kit gets glass that neither badges nor counts toward the squad tally.
[BaseContainerProps(), RK29_OverrideSubstituteTitle()]
class RK29_OverrideSubstitute
{
	[Attribute(desc: "Which group reference to swap, addressed exactly as an adjustment is: \"group\" for that id whichever weapon offers it, \"weapon:group\" for the one owned by that catalog weapon id. An entry axis is meaningless here - a substitution swaps whole groups", category: "29th")]
	string m_sTarget;

	[Attribute(desc: "Catalog group id to offer instead. EMPTY REMOVES the group outright, which is how a kit says 'this weapon takes irons' - and it is legal authoring, not an omission", category: "29th")]
	string m_sReplaceWith;
}

//------------------------------------------------------------------------------------------------
//! One choice ruling out another - kit doctrine, not physics (physics is read off the prefabs by
//! the attachment legality pass).
//!
//! A blocked entry counts as zero, which stops two exclusions feeding each other: once one fires its
//! target can no longer satisfy anything. Exclusions run in array order, so where two could each rule
//! out the other, the earlier wins and the later never fires.
//!
//! Do not add a "requires": a requirement that blocks makes kit-building order-dependent (take the
//! mine before the detonator and the mine is dead). If ever wanted, it must warn, not forbid.
[BaseContainerProps(), SCR_BaseContainerCustomTitleFields({"m_sWhen", "m_sBlock"}, "%1 blocks %2")]
class RK29_Exclusion
{
	[Attribute(desc: "What to look at: \"group/entry\", weapon-qualified as \"m16a3:optics/4x20\" where the group belongs to a gun. Naming a group without an entry counts everything in it", category: "29th")]
	string m_sWhen;

	[Attribute("0", desc: "Fires when the count above EXCEEDS this. 0 = picked at all, which is the usual case; a number expresses \"more than four frags\" without a second kind of condition", category: "29th")]
	int m_iOver;

	[Attribute(desc: "What that rules out: \"group/entry\" for one option, or a bare \"group\" to rule out all of it. Same weapon-qualified form", category: "29th")]
	string m_sBlock;
}

//------------------------------------------------------------------------------------------------
//! One element of a composition's override list: the override itself written inline, or a reference to
//! one in a catalog. Abstract - author RK29_Override or RK29_OverrideRef.
[BaseContainerProps(insertable: false)]
class RK29_OverrideStep
{
}

//------------------------------------------------------------------------------------------------
//! One doctrine step over the groups a kit offers. Applied in list order, later wins - and the list
//! is one list, so an inline step and a reference are ordered against each other by where they were
//! authored, not by which kind they are.
//!
//! The two verb sets do not run at the same moment: substitutions decide which group a reference
//! resolves to and run before capability pruning, adjustments change numbers in groups that already
//! exist and run after. Both still read in step order, so a later step's substitution loses to an
//! earlier one over the same reference - first-wins.
[BaseContainerProps(), BaseContainerCustomTitleField("m_sId")]
class RK29_Override : RK29_OverrideStep
{
	[Attribute(desc: "Name for this step, for reading the config - nothing addresses a step by it", category: "29th")]
	string m_sId;

	[Attribute(desc: "Group swaps - a weapon draws from a different group, or from none. Applied at group-build time, BEFORE the capability prune, so the replacement is screened against the owning weapon like any other offer", category: "29th")]
	ref array<ref RK29_OverrideSubstitute> m_aSubstitute;

	[Attribute(desc: "Targeted bound/default changes", category: "29th")]
	ref array<ref RK29_OverrideAdjust> m_aAdjust;

	[Attribute(desc: "Catalog choice groups this override ADDS to the kit's offer, by id - after the groups the composition states, same catalog lookup, same substitution grammar, and a duplicate id keeps the first appearance", category: "29th")]
	ref array<string> m_aAddGroups;
}

//------------------------------------------------------------------------------------------------
//! An override taken by name from a catalog, standing in the kit's list exactly where an inline step
//! would - so which of the two runs first is where they sit in that list.
[BaseContainerProps(), BaseContainerCustomTitleField("m_sOverride")]
class RK29_OverrideRef : RK29_OverrideStep
{
	[Attribute(desc: "Id of an override in an override catalog", category: "29th")]
	string m_sOverride;
}

//------------------------------------------------------------------------------------------------
//! Override catalog - Configs/KitSystem/Catalogs/*.conf. Override steps a kit can name instead of
//! copying. Referenced by ID: two library overrides sharing an m_sId make every reference ambiguous.
[BaseContainerProps(configRoot: true)]
class RK29_OverrideCatalog
{
	[Attribute(desc: "Override steps any kit may name with an RK29_OverrideRef in its m_aOverrides", category: "29th")]
	ref array<ref RK29_Override> m_aOverrides;
}
