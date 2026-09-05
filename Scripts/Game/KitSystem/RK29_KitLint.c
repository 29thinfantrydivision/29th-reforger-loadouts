//------------------------------------------------------------------------------------------------
//! Boot-time config sweep: everything that reads RK29_KitSetup.conf and its catalogs, looking for
//! authoring mistakes the runtime would answer silently. Reads only, logs only, once from
//! LoadSetup.
//------------------------------------------------------------------------------------------------
class RK29_KitLint
{
	//------------------------------------------------------------------------------------------------
	//! Compositions and prefab reads come through the caches RK29_KitCompose and RK29_KitResolve
	//! keep, so this costs loads the first offer build would pay anyway.
	static void Run(notnull RK29_KitSetup setup)
	{
		VerifyAttachmentSeats(setup);
		VerifySubstitutions(setup);
		VerifyOverrideTargets(setup);
		VerifyDuplicateEntries(setup);
		VerifyFactionKeys(setup);
		VerifyExclusions(setup);
		VerifyGarmentAttachments(setup);
		VerifyChamberIds(setup);
	}

	//------------------------------------------------------------------------------------------------
	//! A garment-attachment group names two slots, and an empty one fails silently: no garment slot
	//! routes its rows to the equipment map, where a helmet attachment never equips; no slot on the
	//! garment makes them carried items, where night vision does nothing. And a kit offering one must
	//! dress the garment slot it rides on, or EnforceGarmentSlots greys the whole group with nothing
	//! to name.
	protected static void VerifyGarmentAttachments(notnull RK29_KitSetup setup)
	{
		int bad = 0;
		foreach (RK29_ChoiceGroup g : setup.m_aChoiceGroups)
			bad += ComplainIfHalfAddressed(g, "catalog");

		array<RK29_ChoiceGroup> inlineGroups = {};
		array<string> inlineKits = {};
		CollectInlineGroups(setup, inlineGroups, inlineKits);
		foreach (int i, RK29_ChoiceGroup g : inlineGroups)
			bad += ComplainIfHalfAddressed(g, "kit '" + inlineKits[i] + "' inline group");

		foreach (RK29_ClassSetup cls : setup.m_aClasses)
			bad += ComplainIfHostUndressed(setup, cls);

		if (bad > 0)
			Print(string.Format("[RK29] config ERROR - %1 garment-attachment group(s) cannot be worn"
				+ " as written", bad), LogLevel.ERROR);
	}

	//------------------------------------------------------------------------------------------------
	protected static int ComplainIfHalfAddressed(RK29_ChoiceGroup g, string where)
	{
		RK29_GarmentAttachmentGroup on = RK29_GarmentAttachmentGroup.Cast(g);
		if (!on || (on.m_sGarmentSlot != "" && on.m_sSlot != ""))
			return 0;

		Print(string.Format("[RK29] config ERROR - %1 garment-attachment group '%2' lacks a garment"
			+ " slot or a slot on it - nothing in it can be worn", where, on.m_sId), LogLevel.ERROR);
		return 1;
	}

	//------------------------------------------------------------------------------------------------
	//! References resolved through the catalog: a kit states the shared hat by reference. A kit's own
	//! hat on the same slot counts too - it evicts the shared one but still dresses the slot.
	protected static int ComplainIfHostUndressed(notnull RK29_KitSetup setup, RK29_ClassSetup cls)
	{
		if (!cls || cls.m_sComposition == ResourceName.Empty)
			return 0;
		RK29_KitComposition comp = RK29_KitCompose.LoadComposition(cls.m_sComposition);
		if (!comp)
			return 0;

		array<RK29_ChoiceGroup> groups = {};
		comp.Collect(groups);

		int bad = 0;
		foreach (RK29_ChoiceGroup g : groups)
		{
			RK29_GarmentAttachmentGroup on = RK29_GarmentAttachmentGroup.Cast(DefinitionOf(setup, g));
			if (!on || on.m_sGarmentSlot == "")
				continue;

			bool dressed;
			foreach (RK29_ChoiceGroup other : groups)
			{
				RK29_ClothingGroup worn = RK29_ClothingGroup.Cast(DefinitionOf(setup, other));
				if (worn && worn.m_sSlot == on.m_sGarmentSlot)
				{
					dressed = true;
					break;
				}
			}
			if (dressed)
				continue;

			Print(string.Format("[RK29] config ERROR - kit '%1' offers garment-attachment group '%2'"
				+ " on slot %3 but dresses nothing there - every row in it will be greyed",
				cls.m_sKitName, on.m_sId, on.m_sGarmentSlot), LogLevel.ERROR);
			bad++;
		}
		return bad;
	}

	//------------------------------------------------------------------------------------------------
	//! The definition behind a composition element: the catalog group a *Ref names, else itself.
	protected static RK29_ChoiceGroup DefinitionOf(notnull RK29_KitSetup setup, RK29_ChoiceGroup g)
	{
		if (!g)
			return null;
		if (g.RefId() != "")
			return setup.FindChoiceGroup(g.RefId());
		return g;
	}

	//------------------------------------------------------------------------------------------------
	//! One id space per gun. Ammo pools feeding one muzzle share one loaded selector
	//! (RK29_KitResolve.OfferLoadedSelectors merges them), and the selector, the loaded toggle and
	//! the saved pick all know a round by its entry id alone. Asked across every counted group a
	//! weapon owns, not per muzzle: the muzzle is only derived at resolve time. Faction-aware like
	//! the duplicate check: "he" for US and "he" for USSR are one row to any one player.
	protected static void VerifyChamberIds(notnull RK29_KitSetup setup)
	{
		if (!setup.m_aWeaponDefs)
			return;

		int bad = 0;
		foreach (RK29_WeaponDef def : setup.m_aWeaponDefs)
		{
			if (!def)
				continue;

			array<RK29_ChoiceEntryBase> entries = {};
			array<string> homes = {};
			AppendCountedEntries(setup, def.m_AmmoGroup, def.m_sId + " inline ammo", entries, homes);
			if (def.m_aGroups)
			{
				foreach (string gid : def.m_aGroups)
					AppendCountedEntries(setup, setup.FindChoiceGroup(gid), gid, entries, homes);
			}

			int n = entries.Count();
			for (int i = 0; i < n; i++)
			{
				string aid = RK29_KitResolve.EntryIdOf(entries[i]);
				if (aid == "")
					continue;
				for (int j = i + 1; j < n; j++)
				{
					if (homes[j] == homes[i] || RK29_KitResolve.EntryIdOf(entries[j]) != aid)
						continue;
					if (!RK29_KitResolve.FactionsOverlap(entries[i], entries[j]))
						continue;

					Print(string.Format("[RK29] config ERROR - weapon '%1' owns counted groups '%2'"
						+ " and '%3' that both name an entry '%4' for one faction - the loaded"
						+ " selector they share cannot tell them apart",
						def.m_sId, homes[i], homes[j], aid), LogLevel.ERROR);
					bad++;
				}
			}
		}

		if (bad > 0)
			Print(string.Format("[RK29] config ERROR - %1 entry id(s) shared between a weapon's"
				+ " ammo pools", bad), LogLevel.ERROR);
	}

	//------------------------------------------------------------------------------------------------
	//! The enabled entries of one COUNTED or BUDGETED item group a weapon owns, each tagged with the
	//! group it came from, plus those of the groups it includes (one level, as ResolveGroup merges
	//! them - under the including group's name, since that is the group they resolve into).
	protected static void AppendCountedEntries(notnull RK29_KitSetup setup, RK29_ChoiceGroup g,
		string home, notnull array<RK29_ChoiceEntryBase> outEntries, notnull array<string> outHomes)
	{
		RK29_ItemGroup item = RK29_ItemGroup.Cast(g);
		if (!item || !item.m_bEnabled || item.m_eKind == RK29_EChoiceKind.EXCLUSIVE)
			return;

		AppendEnabledEntries(item, home, outEntries, outHomes);

		if (!item.m_aIncludeGroups)
			return;
		foreach (string includeId : item.m_aIncludeGroups)
		{
			RK29_ChoiceGroup inc = setup.FindChoiceGroup(includeId);
			if (inc && inc.m_bEnabled)
				AppendEnabledEntries(inc, home, outEntries, outHomes);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void AppendEnabledEntries(notnull RK29_ChoiceGroup g, string home,
		notnull array<RK29_ChoiceEntryBase> outEntries, notnull array<string> outHomes)
	{
		if (!g.m_aEntries)
			return;
		foreach (RK29_ChoiceEntryBase e : g.m_aEntries)
		{
			if (!e || !e.m_bEnabled)
				continue;
			outEntries.Insert(e);
			outHomes.Insert(home);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One group is one seat. An attachment group's seat is derived from its entries, so an
	//! author cannot name the wrong one - but can name two, and a list holding an optic and a
	//! bayonet has no single seat: SharesSeatWith would call it a sibling of both points at once.
	//! Asked per weapon, since "same seat" has no meaning without one: the optics group offers
	//! carry-handle and AK dovetail sights together and is coherent, so each weapon is run only
	//! against the entries it does not refuse.
	protected static void VerifyAttachmentSeats(notnull RK29_KitSetup setup)
	{
		if (!setup.m_aWeaponDefs)
			return;

		int bad = 0;
		foreach (RK29_WeaponDef wdef : setup.m_aWeaponDefs)
		{
			if (!wdef || !wdef.m_aGroups)
				continue;

			// a per-faction weapon id is two guns wearing one name, and each answers for itself
			array<ResourceName> guns = {};
			if (wdef.m_sPrefab != ResourceName.Empty)
				guns.Insert(wdef.m_sPrefab);
			if (wdef.m_aPerFaction)
			{
				foreach (RK29_WeaponFactionPrefab pf : wdef.m_aPerFaction)
				{
					if (pf && pf.m_sPrefab != ResourceName.Empty && !guns.Contains(pf.m_sPrefab))
						guns.Insert(pf.m_sPrefab);
				}
			}

			foreach (string gid : wdef.m_aGroups)
			{
				RK29_AttachmentGroup att = RK29_AttachmentGroup.Cast(setup.FindChoiceGroup(gid));
				if (!att || !att.m_bEnabled)
					continue;

				array<string> ids = {};
				CollectAttachmentIds(setup, att, ids);
				foreach (ResourceName gun : guns)
				{
					// the harm is entirely in SharesSeatWith, whose only callers clear a sibling
					// group's pick - with no sibling on either seat there is nothing to misfire.
					// The SVD is the live case: RHS bolts slot_akOptics beside the rifle's own
					// Dovetail.
					array<string> siblingSeats = {};
					SeatsOfOtherGroups(setup, wdef, att.m_sId, gun, siblingSeats);
					bad += ComplainIfTwoSeats(setup, wdef.m_sId, att.m_sId, gun, ids, siblingSeats);
				}
			}
		}

		if (bad > 0)
			Print(string.Format("[RK29] config ERROR - %1 attachment group(s) ask about TWO"
				+ " seats on one weapon. A group is ONE point: picking in it bares its sibling"
				+ " tiers on that point, and the inspection screen offers it wherever any entry"
				+ " fits. Split the odd entries into a group of their own", bad), LogLevel.ERROR);
	}

	//------------------------------------------------------------------------------------------------
	//! Every seat, on this gun, that some other attachment group of the same weapon takes. Read
	//! off the weapon def rather than an offer: a group reaches a weapon only through
	//! wdef.m_aGroups.
	protected static void SeatsOfOtherGroups(notnull RK29_KitSetup setup,
		notnull RK29_WeaponDef wdef, string exceptGroupId,
		ResourceName gun, notnull array<string> outSeats)
	{
		foreach (string gid : wdef.m_aGroups)
		{
			if (gid == exceptGroupId)
				continue;
			RK29_AttachmentGroup other = RK29_AttachmentGroup.Cast(setup.FindChoiceGroup(gid));
			if (!other || !other.m_bEnabled)
				continue;

			array<string> ids = {};
			CollectAttachmentIds(setup, other, ids);
			foreach (string aid : ids)
			{
				RK29_AttachmentDef adef = setup.FindAttachmentDef(aid);
				if (!adef || adef.m_sPrefab == ResourceName.Empty)
					continue;
				if (RK29_KitCompose.WeaponRejectsAttachment(gun, adef.m_sPrefab))
					continue;

				array<string> seats = {};
				RK29_KitCompose.SeatsTakenOn(gun, adef.m_sPrefab, seats);
				foreach (string s : seats)
				{
					if (!outSeats.Contains(s))
						outSeats.Insert(s);
				}
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! By the same MountFits-both-ways test RK29_ResolvedGroup.SharesSeatWith uses, so what this
	//! predicts and what clears a sibling's pick at runtime cannot disagree.
	protected static bool AnySeatShared(notnull array<string> a, notnull array<string> b)
	{
		foreach (string mine : a)
		{
			foreach (string theirs : b)
			{
				if (RK29_KitCompose.MountFits(mine, theirs)
					|| RK29_KitCompose.MountFits(theirs, mine))
					return true;
			}
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! 1 and one ERROR when two of this group's entries take seats with nothing in common on this
	//! gun. The first entry that seats anywhere is the anchor, so the first disagreement is the
	//! whole story.
	protected static int ComplainIfTwoSeats(notnull RK29_KitSetup setup, string weaponId,
		string groupId, ResourceName gun,
		notnull array<string> attachmentIds, notnull array<string> siblingSeats)
	{
		string anchorId;
		array<string> anchorSeats = {};

		foreach (string aid : attachmentIds)
		{
			array<string> seats = {};
			RK29_AttachmentDef adef = setup.FindAttachmentDef(aid);
			if (!adef || adef.m_sPrefab == ResourceName.Empty)
				continue;
			// exactly what PruneUnmountable keeps - what this gun refuses is not this group's
			// fault
			if (RK29_KitCompose.WeaponRejectsAttachment(gun, adef.m_sPrefab))
				continue;

			RK29_KitCompose.SeatsTakenOn(gun, adef.m_sPrefab, seats);
			if (seats.IsEmpty())
				continue;

			if (anchorSeats.IsEmpty())
			{
				anchorId = aid;
				foreach (string first : seats)
				{
					anchorSeats.Insert(first);
				}
				continue;
			}

			// through AnySeatShared, not string equality: SharesSeatWith asks MountFits both
			// ways, so an exact seat-name compare would call a pair distinct that the runtime
			// treats as one
			if (AnySeatShared(anchorSeats, seats))
				continue;

			// two seats, but nothing else on this gun speaks for either - see the caller. A later
			// entry may still collide with the anchor, so keep looking.
			if (!AnySeatShared(anchorSeats, siblingSeats) && !AnySeatShared(seats, siblingSeats))
				continue;

			Print(string.Format("[RK29] config ERROR - attachment group '%1' asks about two"
				+ " seats on weapon '%2': '%3' takes %4 while '%5' takes %6",
				groupId, weaponId, anchorId, anchorSeats[0], aid, seats[0]), LogLevel.ERROR);
			return 1;
		}

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Its own entries, then those of the groups it includes - one level, parked entries skipped,
	//! exactly as ResolveGroup merges them. No faction filter: the gun refuses a wrong-faction
	//! entry anyway.
	protected static void CollectAttachmentIds(notnull RK29_KitSetup setup,
		notnull RK29_ChoiceGroup g, notnull array<string> outIds)
	{
		AppendAttachmentIds(g, outIds);

		if (!g.m_aIncludeGroups)
			return;
		foreach (string includeId : g.m_aIncludeGroups)
		{
			RK29_ChoiceGroup inc = setup.FindChoiceGroup(includeId);
			if (!inc || !inc.m_bEnabled)
				continue;
			AppendAttachmentIds(inc, outIds);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void AppendAttachmentIds(notnull RK29_ChoiceGroup g, notnull array<string> outIds)
	{
		if (!g.m_aEntries)
			return;

		foreach (RK29_ChoiceEntryBase e : g.m_aEntries)
		{
			RK29_EntryAttachment att = RK29_EntryAttachment.Cast(e);
			if (!att || !att.m_bEnabled || att.m_sAttachment == "")
				continue;
			if (!outIds.Contains(att.m_sAttachment))
				outIds.Insert(att.m_sAttachment);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! An exclusion naming a group that does not exist can never fire, and the dangerous half is the
	//! block side. Only the group is checked, not the entry: entries are filtered by faction long
	//! before an exclusion runs, so a missing entry is the normal state for half of them.
	//! An exclusion that blocks a weapon runs before the weapons are chosen (RK29_KitResolve.BuildOffer),
	//! so its trigger must be a kit-level group: one a weapon owns does not exist yet.
	protected static void VerifyExclusions(notnull RK29_KitSetup setup)
	{
		int bad = 0;
		foreach (RK29_ClassSetup cls : setup.m_aClasses)
		{
			if (!cls || cls.m_sComposition == ResourceName.Empty)
				continue;
			RK29_KitComposition comp = RK29_KitCompose.LoadComposition(cls.m_sComposition);
			if (!comp || !comp.m_aExclusions)
				continue;

			foreach (RK29_Exclusion exclusion : comp.m_aExclusions)
			{
				if (!exclusion)
					continue;
				bad += ComplainIfNoSuchGroup(setup, cls, exclusion.m_sWhen, "watches");
				bad += ComplainIfNoSuchGroup(setup, cls, exclusion.m_sBlock, "blocks");
				bad += ComplainIfExclusionCannotFire(setup, cls, comp, exclusion);
				if (IsWeaponGroupTarget(setup, comp, exclusion.m_sBlock)
					&& IsWeaponOwnedTarget(setup, exclusion.m_sWhen))
				{
					Print(string.Format("[RK29] config ERROR - kit '%1' exclusion '%2 blocks %3': a"
						+ " weapon can only be ruled out by a KIT-LEVEL choice; its trigger is a"
						+ " group a weapon owns, which exists only after the weapons are chosen",
						cls.m_sKitName, exclusion.m_sWhen, exclusion.m_sBlock), LogLevel.ERROR);
					bad++;
				}
			}
		}

		if (bad > 0)
			Print(string.Format("[RK29] config ERROR - %1 exclusion(s) cannot fire as written",
				bad), LogLevel.ERROR);
	}

	//------------------------------------------------------------------------------------------------
	//! Two shapes of exclusion that pass the group checks and still never do what they say. A threshold
	//! above zero on an EXCLUSIVE or weapon source: such a source holds at most one, so "over 1"
	//! never fires (EnforceExclusions counts it as 0 or 1). An exclusion blocking its own trigger: the block
	//! zeroes the count that fired it, so it flips off again on the next resolve.
	protected static int ComplainIfExclusionCannotFire(notnull RK29_KitSetup setup,
		notnull RK29_ClassSetup cls, notnull RK29_KitComposition comp, notnull RK29_Exclusion exclusion)
	{
		string whenWeapon, whenGroup, whenEntry, blockWeapon, blockGroup, blockEntry;
		RK29_KitResolve.SplitTarget(exclusion.m_sWhen, whenWeapon, whenGroup, whenEntry);
		RK29_KitResolve.SplitTarget(exclusion.m_sBlock, blockWeapon, blockGroup, blockEntry);

		int bad = 0;
		if (exclusion.m_iOver > 0 && SourceIsExclusive(setup, comp, whenGroup))
		{
			Print(string.Format("[RK29] config ERROR - kit '%1' exclusion '%2 blocks %3' asks for more"
				+ " than %4 of an EXCLUSIVE choice, which holds at most one - the exclusion can never"
				+ " fire; use 0", cls.m_sKitName, exclusion.m_sWhen, exclusion.m_sBlock, exclusion.m_iOver),
				LogLevel.ERROR);
			bad++;
		}
		if (whenGroup != "" && whenGroup == blockGroup && whenWeapon == blockWeapon
			&& (whenEntry == "" || blockEntry == "" || whenEntry == blockEntry))
		{
			Print(string.Format("[RK29] config ERROR - kit '%1' exclusion '%2 blocks %3' blocks its own"
				+ " trigger, so it undoes itself on the next resolve",
				cls.m_sKitName, exclusion.m_sWhen, exclusion.m_sBlock), LogLevel.ERROR);
			bad++;
		}
		return bad;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether a group id resolves to a source that holds at most one answer: a weapon group, an
	//! attachment group, or an item group of kind EXCLUSIVE. False for an unknown id - the group
	//! check already complained about that.
	protected static bool SourceIsExclusive(notnull RK29_KitSetup setup,
		notnull RK29_KitComposition comp, string groupId)
	{
		RK29_ChoiceGroup def = FindExclusionGroup(setup, comp, groupId);
		if (!def)
			return false;
		if (RK29_WeaponGroup.Cast(def) || RK29_AttachmentGroup.Cast(def))
			return true;
		RK29_ItemGroup item = RK29_ItemGroup.Cast(def);
		return item && item.m_eKind == RK29_EChoiceKind.EXCLUSIVE;
	}

	//------------------------------------------------------------------------------------------------
	//! An exclusion's group by id: a catalog group, else one the kit authors inline. Null when neither.
	protected static RK29_ChoiceGroup FindExclusionGroup(notnull RK29_KitSetup setup,
		notnull RK29_KitComposition comp, string groupId)
	{
		if (groupId == "")
			return null;

		RK29_ChoiceGroup def = setup.FindChoiceGroup(groupId);
		if (def)
			return def;

		array<RK29_ChoiceGroup> groups = {};
		comp.Collect(groups);
		foreach (RK29_ChoiceGroup g : groups)
		{
			if (g && g.RefId() == "" && g.m_sId == groupId)
				return g;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether an exclusion target names a weapon group - a catalog one, or one the kit authors inline.
	protected static bool IsWeaponGroupTarget(notnull RK29_KitSetup setup,
		notnull RK29_KitComposition comp, string path)
	{
		string weaponId, groupId, entryId;
		RK29_KitResolve.SplitTarget(path, weaponId, groupId, entryId);
		return RK29_WeaponGroup.Cast(FindExclusionGroup(setup, comp, groupId)) != null;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether an exclusion target names a group that only a weapon brings into the offer: spelled with
	//! a weapon prefix, listed in some weapon definition's m_aGroups, or a weapon's inline ammo.
	protected static bool IsWeaponOwnedTarget(notnull RK29_KitSetup setup, string path)
	{
		string weaponId, groupId, entryId;
		RK29_KitResolve.SplitTarget(path, weaponId, groupId, entryId);
		if (weaponId != "")
			return true;
		if (IsSynthesizedAmmoGroup(setup, path))
			return true;
		if (!setup.m_aWeaponDefs)
			return false;
		foreach (RK29_WeaponDef wdef : setup.m_aWeaponDefs)
		{
			if (wdef && wdef.m_aGroups && wdef.m_aGroups.Contains(groupId))
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static int ComplainIfNoSuchGroup(notnull RK29_KitSetup setup,
		notnull RK29_ClassSetup cls, string path, string role)
	{
		string weaponId, groupId, entryId;
		RK29_KitResolve.SplitTarget(path, weaponId, groupId, entryId);
		if (groupId == "")
		{
			Print(string.Format("[RK29] config ERROR - kit '%1' %2 '%3', which names no group"
				+ " at all", cls.m_sKitName, role, path), LogLevel.ERROR);
			return 1;
		}

		// a catalog group, or one the kit declares inline - both are legal targets
		if (setup.FindChoiceGroup(groupId))
			return 0;

		RK29_KitComposition comp = RK29_KitCompose.LoadComposition(cls.m_sComposition);
		if (comp)
		{
			// a *Ref names a catalog group, which the lookup above already answered for
			array<RK29_ChoiceGroup> groups = {};
			comp.Collect(groups);
			foreach (RK29_ChoiceGroup g : groups)
			{
				if (g && g.RefId() == "" && g.m_sId == groupId)
					return 0;
			}
		}

		Print(string.Format("[RK29] config ERROR - kit '%1' %2 group '%3', which this kit never"
			+ " offers - it can never take effect",
			cls.m_sKitName, role, groupId), LogLevel.ERROR);
		return 1;
	}

	//------------------------------------------------------------------------------------------------
	//! Every group a kit authors inline, with the owning kit's name at the same index. *Refs are
	//! skipped: they carry no entries and their targets are swept with the catalogs.
	protected static void CollectInlineGroups(notnull RK29_KitSetup setup,
		notnull array<RK29_ChoiceGroup> outGroups, notnull array<string> outKitNames)
	{
		foreach (RK29_ClassSetup cls : setup.m_aClasses)
		{
			if (!cls || cls.m_sComposition == ResourceName.Empty)
				continue;
			RK29_KitComposition comp = RK29_KitCompose.LoadComposition(cls.m_sComposition);
			if (!comp)
				continue;
			array<RK29_ChoiceGroup> groups = {};
			comp.Collect(groups);
			foreach (RK29_ChoiceGroup g : groups)
			{
				if (!g || g.RefId() != "")
					continue;
				outGroups.Insert(g);
				outKitNames.Insert(cls.m_sKitName);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Faction keys are free text and a misspelt one fails silently - the entry is offered to
	//! nobody, survivable with two factions and not with four. The known set is whatever the
	//! rosters declare; an empty key means every faction.
	protected static int ComplainIfUnknownFaction(RK29_ChoiceGroup g, notnull array<string> known,
		string where)
	{
		if (!g || !g.m_aEntries)
			return 0;

		int bad = 0;
		foreach (RK29_ChoiceEntryBase e : g.m_aEntries)
		{
			if (!e || !e.m_aFactionKeys)
				continue;

			foreach (string key : e.m_aFactionKeys)
			{
				if (key == "" || known.Contains(key))
					continue;

				Print(string.Format("[RK29] config ERROR - %1 group '%2' entry '%3' names"
					+ " faction '%4', which no roster declares - it will be offered to nobody",
					where, g.m_sId, RK29_KitResolve.EntryIdOf(e), key), LogLevel.ERROR);
				bad++;
			}
		}
		return bad;
	}

	//------------------------------------------------------------------------------------------------
	//! Every faction key an author can write: choice entries in all three authoring homes, plus
	//! the alias catalog.
	protected static void VerifyFactionKeys(notnull RK29_KitSetup setup)
	{
		array<string> known = {};
		foreach (RK29_ClassSetup c : setup.m_aClasses)
		{
			if (c && c.m_sSideFactionKey != "" && !known.Contains(c.m_sSideFactionKey))
				known.Insert(c.m_sSideFactionKey);
		}
		if (known.IsEmpty())
			return;

		int bad = 0;
		foreach (RK29_ChoiceGroup g : setup.m_aChoiceGroups)
		{
			bad += ComplainIfUnknownFaction(g, known, "catalog");
		}

		if (setup.m_aWeaponDefs)
		{
			foreach (RK29_WeaponDef wdef : setup.m_aWeaponDefs)
			{
				if (wdef)
					bad += ComplainIfUnknownFaction(wdef.m_AmmoGroup, known,
						"weapon '" + wdef.m_sId + "' ammo");
			}
		}

		array<RK29_ChoiceGroup> inlineGroups = {};
		array<string> inlineKits = {};
		CollectInlineGroups(setup, inlineGroups, inlineKits);
		foreach (int i, RK29_ChoiceGroup g : inlineGroups)
			bad += ComplainIfUnknownFaction(g, known, "kit '" + inlineKits[i] + "' inline group");

		if (setup.m_aAliases)
		{
			foreach (RK29_ItemAlias alias : setup.m_aAliases)
			{
				if (!alias || !alias.m_aPerFaction)
					continue;
				foreach (RK29_ItemAliasEntry e : alias.m_aPerFaction)
				{
					if (!e || e.m_sFactionKey == "" || known.Contains(e.m_sFactionKey))
						continue;

					Print(string.Format("[RK29] config ERROR - alias '%1' names faction '%2',"
						+ " which no roster declares - that entry can never be reached",
						alias.m_sAlias, e.m_sFactionKey), LogLevel.ERROR);
					bad++;
				}
			}
		}

		if (bad > 0)
			Print(string.Format("[RK29] config ERROR - %1 faction key(s) match no roster",
				bad), LogLevel.ERROR);
	}

	//------------------------------------------------------------------------------------------------
	//! Two entries flagged as the default and reachable by one faction: DefaultEntry returns the
	//! first, so the second flag does nothing. Faction-aware because sniper_ammo flags a US and a
	//! USSR round.
	protected static void ComplainIfTwoDefaults(RK29_ChoiceGroup g, string where)
	{
		if (!g || !g.m_aEntries)
			return;
		int n = g.m_aEntries.Count();
		for (int i = 0; i < n; i++)
		{
			RK29_ChoiceEntryBase a = g.m_aEntries[i];
			if (!a || !a.m_bEnabled || !a.m_bDefault)
				continue;
			for (int j = i + 1; j < n; j++)
			{
				RK29_ChoiceEntryBase b = g.m_aEntries[j];
				if (!b || !b.m_bEnabled || !b.m_bDefault)
					continue;
				if (!RK29_KitResolve.FactionsOverlap(a, b))
					continue;
				Print(string.Format("[RK29] config WARNING - %1 group '%2' flags both '%3' and"
					+ " '%4' as the default; the first wins and the second flag does nothing",
					where, g.m_sId, RK29_KitResolve.EntryIdOf(a), RK29_KitResolve.EntryIdOf(b)),
					LogLevel.WARNING);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One entry id stated twice inside a single group. ResolveGroup keeps the first and drops
	//! the rest, so the twin is absorbed in silence. Usually not a typo: bare-brace arrays in a
	//! child conf merge with the parent's entries, so restating an entry to "override" its count
	//! appends a second copy - what that author wants is an override step's m_aAdjust.
	//! Faction-aware, and it has to be: sniper_ammo states "ball" twice and ugl_grenades "he"
	//! twice, split by m_sFactionKey, and those are correct.
	protected static int ComplainIfDuplicateEntries(RK29_ChoiceGroup g, string where)
	{
		if (!g || !g.m_aEntries)
			return 0;

		int found = 0;
		int n = g.m_aEntries.Count();
		for (int i = 0; i < n; i++)
		{
			RK29_ChoiceEntryBase a = g.m_aEntries[i];
			if (!a || !a.m_bEnabled)
				continue;
			string aid = RK29_KitResolve.EntryIdOf(a);
			if (aid == "")
				continue;

			for (int j = i + 1; j < n; j++)
			{
				RK29_ChoiceEntryBase b = g.m_aEntries[j];
				if (!b || !b.m_bEnabled)
					continue;
				if (RK29_KitResolve.EntryIdOf(b) != aid)
					continue;
				if (!RK29_KitResolve.FactionsOverlap(a, b))
					continue;

				Print(string.Format("[RK29] config WARNING - %1 group '%2' states entry '%3'"
					+ " twice; the second is dropped. To change a count or a default, use a"
					+ " override step's m_aAdjust - a bare-brace child array MERGES with the"
					+ " parent's entries, it does not replace them",
					where, g.m_sId, aid), LogLevel.WARNING);
				found++;
			}
		}
		return found;
	}

	//------------------------------------------------------------------------------------------------
	//! Sweeps all three authoring homes: a group can live in a catalog, on a weapon def, or
	//! inline on a composition. A composition's *Ref elements are skipped - the catalog sweep
	//! above already read the definitions they name.
	protected static void VerifyDuplicateEntries(notnull RK29_KitSetup setup)
	{
		int bad = 0;

		foreach (RK29_ChoiceGroup g : setup.m_aChoiceGroups)
		{
			bad += ComplainIfDuplicateEntries(g, "catalog");
			ComplainIfTwoDefaults(g, "catalog");
		}

		if (setup.m_aWeaponDefs)
		{
			foreach (RK29_WeaponDef wdef : setup.m_aWeaponDefs)
			{
				if (!wdef)
					continue;
				bad += ComplainIfDuplicateEntries(wdef.m_AmmoGroup,
					"weapon '" + wdef.m_sId + "' ammo");
				ComplainIfTwoDefaults(wdef.m_AmmoGroup, "weapon '" + wdef.m_sId + "' ammo");
			}
		}

		array<RK29_ChoiceGroup> inlineGroups = {};
		array<string> inlineKits = {};
		CollectInlineGroups(setup, inlineGroups, inlineKits);
		foreach (int i, RK29_ChoiceGroup g : inlineGroups)
		{
			string where = "kit '" + inlineKits[i] + "' inline group";
			bad += ComplainIfDuplicateEntries(g, where);
			ComplainIfTwoDefaults(g, where);
		}

		if (bad > 0)
			Print(string.Format("[RK29] config WARNING - %1 duplicate entry statement(s) were"
				+ " dropped; each is a row an author wrote that the offer never shows",
				bad), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	//! Whether a target names the ammo group a weapon carries inline with no id of its own - the
	//! resolver addresses that one as "<weapon>_ammo", so it is in no catalog and no composition.
	protected static bool IsSynthesizedAmmoGroup(notnull RK29_KitSetup setup, string target)
	{
		string weaponId, groupId, entryId;
		RK29_KitResolve.SplitTarget(target, weaponId, groupId, entryId);
		string ammoSuffix = RK29_KitResolve.AMMO_SUFFIX;
		int suffix = groupId.LastIndexOf(ammoSuffix);
		if (suffix <= 0 || suffix != groupId.Length() - ammoSuffix.Length())
			return false;
		RK29_WeaponDef def = setup.FindWeaponDef(groupId.Substring(0, suffix));
		return def && def.m_AmmoGroup;
	}

	//------------------------------------------------------------------------------------------------
	//! Every adjust target and added group of every override a kit runs has to name a group that
	//! exists: the resolver skips a miss silently at request time, when nobody is reading the
	//! log.
	protected static void VerifyOverrideTargets(notnull RK29_KitSetup setup)
	{
		int bad = 0;
		foreach (RK29_ClassSetup cls : setup.m_aClasses)
		{
			if (!cls || cls.m_sComposition == ResourceName.Empty)
				continue;
			RK29_KitComposition comp = RK29_KitCompose.LoadComposition(cls.m_sComposition);
			if (!comp)
				continue;
			array<RK29_Override> overrides = {};
			RK29_KitResolve.CollectOverrides(comp, setup, overrides);
			foreach (RK29_Override p : overrides)
			{
				if (p.m_aAdjust)
				{
					foreach (RK29_OverrideAdjust adj : p.m_aAdjust)
					{
						if (!adj || IsSynthesizedAmmoGroup(setup, adj.m_sTarget))
							continue;
						bad += ComplainIfNoSuchGroup(setup, cls, adj.m_sTarget, "adjusts");
					}
				}
				if (p.m_aAddGroups)
				{
					foreach (string addId : p.m_aAddGroups)
					{
						if (addId == "" || setup.FindChoiceGroup(addId))
							continue;
						Print(string.Format("[RK29] config ERROR - kit '%1' override '%2' adds group"
							+ " '%3', which is in no catalog", cls.m_sKitName, p.m_sId, addId),
							LogLevel.ERROR);
						bad++;
					}
				}
			}
		}
		if (bad > 0)
			Print(string.Format("[RK29] config ERROR - %1 override target(s) name no group", bad),
				LogLevel.ERROR);
	}

	//------------------------------------------------------------------------------------------------
	//! A substitution that names nothing is the dangerous config error here, and silent by
	//! construction: a step meant to restrict a kit that reaches no group simply does not run,
	//! and the kit looks exactly like the stock one. So every substitution is measured at boot against
	//! the group references its kit can ever make - the composition's catalog ids and inline
	//! groups, plus each resolvable weapon's inline ammo group and m_aGroups, which is exactly
	//! what BuildOffer substitutes at. ERROR: no target, a target naming an entry, or
	//! m_sReplaceWith naming a group no catalog holds. WARNING: a target reaching nothing, or two
	//! substitutions reaching one reference (first wins). An empty m_sReplaceWith is the removal form.
	protected static void VerifySubstitutions(notnull RK29_KitSetup setup)
	{
		int bad = 0;

		foreach (RK29_ClassSetup cls : setup.m_aClasses)
		{
			if (!cls || cls.m_sComposition == ResourceName.Empty)
				continue;
			RK29_KitComposition comp = RK29_KitCompose.LoadComposition(cls.m_sComposition);
			if (!comp)
				continue;

			// the order the resolver reads them in, and therefore what "first wins" is measured
			// against - mirrors RK29_KitResolve.CollectSubstitutions, which is authored order
			array<RK29_OverrideSubstitute> subs = {};
			array<RK29_Override> overrides = {};
			RK29_KitResolve.CollectOverrides(comp, setup, overrides);
			foreach (RK29_Override step : overrides)
			{
				if (!step || !step.m_aSubstitute)
					continue;
				foreach (RK29_OverrideSubstitute sub : step.m_aSubstitute)
				{
					if (sub)
						subs.Insert(sub);
				}
			}
			if (subs.IsEmpty())
				continue;

			array<string> refs = {};
			CollectGroupRefs(setup, comp, refs);

			// references an earlier substitution already took, so the second can say which one it lost to
			array<string> claimed = {};
			foreach (RK29_OverrideSubstitute sub : subs)
			{
				bad += ComplainIfSubstitutionMisses(setup, cls.m_sKitName, sub, refs, claimed);
			}
		}

		if (bad > 0)
			Print(string.Format("[RK29] config ERROR - %1 override substitution(s) cannot run as"
				+ " authored. A substitution names a whole GROUP - \"group\" or \"weapon:group\""
				+ " - and replaces it with another catalog group, or with nothing at all",
				bad), LogLevel.ERROR);
	}

	//------------------------------------------------------------------------------------------------
	//! 1 per substitution that can never run, 0 otherwise. Warnings do not count: one that reaches
	//! nothing is authored wrongly but is not malformed.
	protected static int ComplainIfSubstitutionMisses(notnull RK29_KitSetup setup, string kitName,
		notnull RK29_OverrideSubstitute sub,
		notnull array<string> refs, notnull array<string> claimed)
	{
		if (sub.m_sTarget == "")
		{
			Print(string.Format("[RK29] config ERROR - kit '%1' states a substitution with no"
				+ " target - it addresses nothing", kitName), LogLevel.ERROR);
			return 1;
		}

		int bad = 0;

		// Empty is the removal form, and is how "this weapon takes irons" is authored
		if (sub.m_sReplaceWith != "" && !setup.FindChoiceGroup(sub.m_sReplaceWith))
		{
			Print(string.Format("[RK29] config ERROR - kit '%1' substitutes '%2' with '%3',"
				+ " which is not in any catalog",
				kitName, sub.m_sTarget, sub.m_sReplaceWith), LogLevel.ERROR);
			bad = 1;
		}

		string subWeapon, subGroup, subEntry;
		RK29_KitResolve.SplitTarget(sub.m_sTarget, subWeapon, subGroup, subEntry);
		if (subEntry != "")
		{
			Print(string.Format("[RK29] config ERROR - kit '%1' substitutes '%2', which names"
				+ " an ENTRY - a substitution swaps whole groups, so this substitution will never run",
				kitName, sub.m_sTarget), LogLevel.ERROR);
			return bad + 1;
		}

		int hits = 0;
		foreach (string offered : refs)
		{
			string refWeapon, refGroup, refEntry;
			RK29_KitResolve.SplitTarget(offered, refWeapon, refGroup, refEntry);
			if (refGroup != subGroup)
				continue;
			// an empty weapon axis matches any owner - the reading targets had before the axis
			// existed
			if (subWeapon != "" && subWeapon != refWeapon)
				continue;

			hits++;
			if (claimed.Contains(offered))
			{
				Print(string.Format("[RK29] config WARNING - kit '%1' substitutes '%2' twice -"
					+ " '%3' loses to the earlier step, first wins",
					kitName, offered, sub.m_sTarget), LogLevel.WARNING);
				continue;
			}
			claimed.Insert(offered);
		}

		if (hits == 0)
			Print(string.Format("[RK29] config WARNING - kit '%1' substitutes '%2', which"
				+ " matches no group this kit offers - the step does NOTHING, and a step meant to"
				+ " restrict a kit that does nothing hands the player more than the author"
				+ " intended", kitName, sub.m_sTarget), LogLevel.WARNING);

		return bad;
	}

	//------------------------------------------------------------------------------------------------
	//! Every group reference a kit can make, spelled the way an override target spells it: "group"
	//! for a kit-level one, "weapon:group" for one a weapon definition owns. Not chased: a weapon
	//! group a substitution itself replaces.
	protected static void CollectGroupRefs(notnull RK29_KitSetup setup,
		notnull RK29_KitComposition comp, notnull array<string> outRefs)
	{
		array<RK29_ChoiceGroup> groups = {};

		array<RK29_ChoiceGroup> stated = {};
		comp.Collect(stated);
		foreach (RK29_ChoiceGroup g : stated)
		{
			if (!g)
				continue;

			string refId = g.RefId();
			if (refId != "")
			{
				if (!outRefs.Contains(refId))
					outRefs.Insert(refId);
				RK29_ChoiceGroup def = setup.FindChoiceGroup(refId);
				if (def)
					groups.Insert(def);
				continue;
			}

			if (g.m_sId == "")
				continue;
			if (!outRefs.Contains(g.m_sId))
				outRefs.Insert(g.m_sId);
			groups.Insert(g);
		}

		array<string> weaponIds = {};
		foreach (RK29_ChoiceGroup g : groups)
		{
			CollectWeaponIds(setup, g, weaponIds);
		}

		foreach (string wid : weaponIds)
		{
			RK29_WeaponDef wdef = setup.FindWeaponDef(wid);
			if (!wdef)
				continue;

			// an ammo group with no id of its own is addressed by nobody: the resolver names it
			// "<weapon>_ammo" only after substitution has had its say
			if (wdef.m_AmmoGroup && wdef.m_AmmoGroup.m_sId != "")
				AppendGroupRef(outRefs, wid, wdef.m_AmmoGroup.m_sId);

			if (!wdef.m_aGroups)
				continue;
			foreach (string gid : wdef.m_aGroups)
			{
				AppendGroupRef(outRefs, wid, gid);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void AppendGroupRef(notnull array<string> outRefs, string weaponId, string groupId)
	{
		string path = weaponId + ":" + groupId;
		if (!outRefs.Contains(path))
			outRefs.Insert(path);
	}

	//------------------------------------------------------------------------------------------------
	//! Weapon ids a group can resolve to: its own entries, then one level of includes, the same
	//! merge ResolveGroup performs. No faction filter - this checks authoring, not one side's
	//! offer.
	protected static void CollectWeaponIds(notnull RK29_KitSetup setup,
		notnull RK29_ChoiceGroup g, notnull array<string> outIds)
	{
		AppendWeaponIds(g, outIds);

		if (!g.m_aIncludeGroups)
			return;
		foreach (string includeId : g.m_aIncludeGroups)
		{
			RK29_ChoiceGroup inc = setup.FindChoiceGroup(includeId);
			if (inc)
				AppendWeaponIds(inc, outIds);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void AppendWeaponIds(notnull RK29_ChoiceGroup g, notnull array<string> outIds)
	{
		if (!g.m_aEntries)
			return;

		foreach (RK29_ChoiceEntryBase e : g.m_aEntries)
		{
			RK29_EntryWeapon w = RK29_EntryWeapon.Cast(e);
			if (!w || w.m_sWeapon == "")
				continue;
			if (!outIds.Contains(w.m_sWeapon))
				outIds.Insert(w.m_sWeapon);
		}
	}
}
