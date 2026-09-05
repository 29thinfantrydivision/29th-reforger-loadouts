//------------------------------------------------------------------------------------------------
//! Choice resolution: expands a class's offered choice groups - the composition's plus whatever
//! the chosen weapons bring, with override applied - then lays validated picks (or the authored
//! defaults) over a composed kit. BuildOffer's pass order is load-bearing; read its note before
//! moving anything. Picks travel the wire as "group=entry:count;group=entry;group=".
//------------------------------------------------------------------------------------------------
class RK29_KitResolve
{
	//! merged WeaponSlotType per weapon prefab - the answer cannot change inside a session
	protected static ref map<ResourceName, string> s_mSlotTypeCache = new map<ResourceName, string>();

	//! The sanity ceiling that cannot be forgotten in config: no request, however built, turns into
	//! an absurd number of entities. Config is exactly where a cap gets omitted.
	static const int COUNT_HARD_CEILING = 100;

	//! The door on a pick wire, which arrives from a client: a real request is ~26 picks and ~1.2 KB
	//! at the very worst the config allows. Over either cap the wire is refused whole, so a hostile
	//! string cannot buy log lines or picks in proportion to its length.
	static const int WIRE_MAX_CHARS = 4096;
	static const int WIRE_MAX_PICKS = 64;

	//! An unset m_iMax implies "about what this row already carries" - never unlimited. Two is the
	//! ratio the config itself uses where a cap is stated (the rifle ball row is 2/6/12).
	protected static const int COUNT_IMPLIED_FACTOR = 2;

	//! ...but a row with no default and no min would imply zero and become untakeable, which is a
	//! silent gear loss. Optional extras (the RPG's PG-7VL/VR rows) need this usable floor.
	protected static const int COUNT_IMPLIED_FLOOR = 4;

	//============================================================================================
	// Offer
	//============================================================================================

	//------------------------------------------------------------------------------------------------
	//! The groups a class offers. Override runs over the kit-level set before weapons are chosen and
	//! again once weapon-owned groups are in: a step can only reach a group already in the offer.
	//!
	//! The pipeline is stated here and nowhere else - the passes below carry no numbering of their
	//! own. Call order:
	//!   CollectKitLevelGroups -> CollectOverrideAdds -> ApplyOverrides -> DropZeroCeilingEntries
	//!   -> EnforceExclusions (weapon targets only) -> PullWeaponOwnedGroups -> FinalisePasses
	//!   (ApplyOverrides, DropZeroCeilingEntries, EnforceAttachmentLegality, EnforceExclusions (the
	//!   rest), OfferLoadedSelectors, DropEmptyGroups, SortOfferByOrder).
	//!
	//! What each kind of pass may do:
	//!   Remove - PruneUnmountable and, through it, PruneMissingVariants drop entries (only over a
	//!     weapon's referenced groups, in OfferWeaponReferencedGroups); DropZeroCeilingEntries drops
	//!     entries anywhere; DropEmptyGroups drops whole groups. Nothing else removes anything.
	//!   Mark only - EnforceAttachmentLegality sets m_bBlocked and leaves the row on screen;
	//!     EnforceExclusions does the same for the kit's own rulings and stamps m_sExcludes on the
	//!     rows that cause them.
	//!   Reorder - SortEntriesByOrder inside ResolveGroup (entries), SortOfferByOrder last (groups).
	//!
	//! Substitution rewrites each group reference - to what override says it draws from, or to nothing
	//! at all - before that group is resolved, so a swapped-in group is screened exactly as the
	//! authored one would have been. The numeric adjustments cannot move up to join it: they name
	//! entries, and which entries exist is not settled until the prunes have run.
	//!
	//! The ApplyOverrides/DropZeroCeilingEntries pair before PullWeaponOwnedGroups is deliberate: a
	//! weapon capped at zero must leave the offer before its ammo and attachment groups are pulled
	//! in. Both are idempotent, which is why FinalisePasses runs them again over the widened offer.
	//! An exclusion that blocks a WEAPON runs there too, for the same reason: blocked after the pull,
	//! the gun's ammo would already be in the offer and the substitute would spawn without any.
	//! Such an exclusion can only be triggered by a kit-level group - the lint refuses the other kind.
	//!
	//! DeriveSeatTypes runs three times on purpose, once after each point that changes a group's
	//! entries: end of ResolveGroup, end of PruneUnmountable, and after BlockIllegalEntries.
	//!
	//! Same-slot eviction happens at insert (EvictSameSlot); emptiness is judged once, late, at
	//! DropEmptyGroups. Neither can do the other's job.
	//!
	//! Loaded selectors are synthesized once, late, and directly beside their ammo group, so no row
	//! can offer to chamber a round a later pass took away.
	//!
	//! Defaults are not a pass - DefaultEntry walks m_aEntries when asked, so an entry the owning
	//! weapon refuses is gone before anything can resolve to it.
	static void BuildOffer(notnull RK29_ClassSetup cls, notnull RK29_KitSetup setup,
		array<ref RK29_ChoicePick> picks, notnull array<ref RK29_ResolvedGroup> outGroups)
	{
		string factionKey = cls.m_sSideFactionKey;

		RK29_KitComposition comp = RK29_KitCompose.LoadComposition(cls.m_sComposition);

		// gathered once for the whole build. An exclusion naming nothing the kit offers is complained
		// about at boot, not here, where the answer depends on which weapon was picked
		array<RK29_OverrideSubstitute> subs = {};
		CollectSubstitutions(comp, setup, subs);

		CollectKitLevelGroups(cls, comp, setup, subs, factionKey, outGroups);
		CollectOverrideAdds(cls, comp, setup, subs, factionKey, outGroups);

		ApplyOverrides(comp, setup, outGroups);

		// a zero-capped weapon must leave before the loop below pulls its ammo and attachment groups
		// in, or those stay owned by a gun the offer no longer fields and the substitute rifle spawns
		// with no magazines - see the header
		DropZeroCeilingEntries(outGroups);

		// same reason, for an exclusion that rules a WEAPON out: the pull below must not see it
		EnforceExclusions(outGroups, picks, comp, true);

		PullWeaponOwnedGroups(cls, setup, subs, picks, factionKey, outGroups);

		FinalisePasses(comp, setup, picks, factionKey, outGroups);
	}

	//------------------------------------------------------------------------------------------------
	//! Parked authoring resolves as if never written, and silently - a deliberately switched-off
	//! group is not the missing reference a config complaint is about.
	protected static bool IsParked(RK29_ChoiceGroup def)
	{
		if (!def)
			return false;
		return !def.m_bEnabled;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsEntryParked(RK29_ChoiceEntryBase e)
	{
		if (!e)
			return false;
		return !e.m_bEnabled;
	}

	//------------------------------------------------------------------------------------------------
	//! One group per worn slot: a later clothing group on the same slot replaces the earlier one.
	//! Kit-level groups are collected in authored order and weapon-owned groups after all of them,
	//! so a kit's own hat beats the shared hat it was written after - this is the only override
	//! mechanism for dress.
	protected static void EvictSameSlot(notnull array<ref RK29_ResolvedGroup> outGroups, notnull RK29_ChoiceGroup def)
	{
		RK29_ClothingGroup worn = RK29_ClothingGroup.Cast(def);
		if (!worn || worn.m_sSlot == "")
			return;

		for (int i = outGroups.Count() - 1; i >= 0; i--)
		{
			if (outGroups[i] && outGroups[i].m_sWornSlot == worn.m_sSlot)
				outGroups.RemoveOrdered(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! A duplicate id keeps its first appearance: Apply walks the array, so a second copy would be
	//! issued twice while every by-id reader saw only the first.
	protected static void TryInsertGroup(notnull array<ref RK29_ResolvedGroup> outGroups,
		notnull RK29_ChoiceGroup def, string factionKey, notnull RK29_KitSetup setup)
	{
		if (FindGroup(outGroups, def.m_sId))
			return;

		EvictSameSlot(outGroups, def);

		outGroups.Insert(ResolveGroup(def, "", factionKey, setup));
	}

	//------------------------------------------------------------------------------------------------
	//! The groups the composition states, in one walk over its five sections, authored
	//! order: a *Ref element resolves through the catalogs, anything else is the definition
	//! itself. That order is the eviction order - EvictSameSlot can only displace what is already
	//! in outGroups - so a group written later beats one written earlier on the same worn slot.
	//! Every id, inline ones included, goes through substitution.
	protected static void CollectKitLevelGroups(notnull RK29_ClassSetup cls, RK29_KitComposition comp,
		notnull RK29_KitSetup setup, notnull array<RK29_OverrideSubstitute> subs, string factionKey,
		notnull array<ref RK29_ResolvedGroup> outGroups)
	{
		if (!comp)
			return;

		array<RK29_ChoiceGroup> all = {};
		comp.Collect(all);

		foreach (RK29_ChoiceGroup g : all)
		{
			if (!g)
				continue;

			string refId = g.RefId();
			if (refId != "")
			{
				string useId;
				if (!SubstitutedGroupId(subs, "", refId, useId))
					continue;

				RK29_ChoiceGroup def = setup.FindChoiceGroup(useId);
				if (!def)
				{
					ComplainOnce(string.Format("[RK29] config ERROR - choice group '%1' referenced"
						+ " by %2 is not in any catalog", useId, cls.m_sKitName), LogLevel.ERROR);
					continue;
				}
				if (IsParked(def))
					continue;
				TryInsertGroup(outGroups, def, factionKey, setup);
				continue;
			}

			// parked before anything is asked of it: a shelved inline group owes no id
			if (IsParked(g))
				continue;
			if (g.m_sId == "")
			{
				ComplainOnce(string.Format("[RK29] config ERROR - inline choice group without an"
					+ " id in %1's composition - skipped, it cannot be addressed",
					cls.m_sKitName), LogLevel.ERROR);
				continue;
			}

			string ownUseId;
			if (!SubstitutedGroupId(subs, "", g.m_sId, ownUseId))
				continue;
			if (ownUseId != g.m_sId)
			{
				RK29_ChoiceGroup swapped = setup.FindChoiceGroup(ownUseId);
				if (!swapped)
				{
					ComplainOnce(string.Format("[RK29] config ERROR - choice group '%1'"
						+ " substituted for '%2' in %3 is not in any catalog",
						ownUseId, g.m_sId, cls.m_sKitName), LogLevel.ERROR);
					continue;
				}
				if (IsParked(swapped))
					continue;
				TryInsertGroup(outGroups, swapped, factionKey, setup);
				continue;
			}

			TryInsertGroup(outGroups, g, factionKey, setup);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! What the kit's overrides bring in, last of the kit-level groups, so an override-added
	//! group never out-ranks one the composition listed itself.
	protected static void CollectOverrideAdds(notnull RK29_ClassSetup cls, RK29_KitComposition comp,
		notnull RK29_KitSetup setup, notnull array<RK29_OverrideSubstitute> subs, string factionKey,
		notnull array<ref RK29_ResolvedGroup> outGroups)
	{
		array<RK29_Override> overrides = {};
		CollectOverrides(comp, setup, overrides);

		foreach (RK29_Override step : overrides)
		{
			if (!step.m_aAddGroups)
				continue;
			foreach (string addId : step.m_aAddGroups)
			{
				string useAddId;
				if (!SubstitutedGroupId(subs, "", addId, useAddId))
					continue;
				RK29_ChoiceGroup added = setup.FindChoiceGroup(useAddId);
				if (!added)
				{
					ComplainOnce(string.Format("[RK29] config ERROR - choice group '%1' added by"
						+ " override '%2' for %3 is not in any catalog", useAddId, step.m_sId,
						cls.m_sKitName), LogLevel.ERROR);
					continue;
				}
				if (IsParked(added))
					continue;
				TryInsertGroup(outGroups, added, factionKey, setup);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The groups each chosen weapon brings: its inline ammo, then its m_aGroups. The bound is
	//! snapshotted before the walk - what this pass appends must not itself be walked.
	protected static void PullWeaponOwnedGroups(notnull RK29_ClassSetup cls,
		notnull RK29_KitSetup setup, notnull array<RK29_OverrideSubstitute> subs,
		array<ref RK29_ChoicePick> picks, string factionKey,
		notnull array<ref RK29_ResolvedGroup> outGroups)
	{
		// a snapshot, not a count: OfferWeaponReferencedGroups can EvictSameSlot a kit-level dress
		// group mid-walk (a gun that brings its own rig), which shifts the rest down - an index walk
		// then skips a weapon group and, on a second eviction, reads past Count()
		array<ref RK29_ResolvedGroup> kitLevel = {};
		foreach (RK29_ResolvedGroup snap : outGroups)
			kitLevel.Insert(snap);
		foreach (RK29_ResolvedGroup g : kitLevel)
		{
			if (!g || !g.IsWeaponGroup())
				continue;

			RK29_ResolvedEntry chosen = PickedWeaponEntry(g, picks);
			if (!chosen)
				continue;
			RK29_EntryWeapon weaponEntry = RK29_EntryWeapon.Cast(chosen.m_Def);
			if (!weaponEntry)
				continue;

			RK29_WeaponDef def = setup.FindWeaponDef(weaponEntry.m_sWeapon);
			if (!def)
				continue;

			OfferWeaponAmmoGroup(cls, def, setup, subs, factionKey, outGroups);
			OfferWeaponReferencedGroups(cls, def, setup, subs, factionKey, outGroups);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One weapon's inline ammo group. A parked group takes its loaded-round selector with it:
	//! never inserted here, never walked by OfferLoadedSelectors, nothing to chamber.
	protected static void OfferWeaponAmmoGroup(notnull RK29_ClassSetup cls,
		notnull RK29_WeaponDef def, notnull RK29_KitSetup setup,
		notnull array<RK29_OverrideSubstitute> subs, string factionKey,
		notnull array<ref RK29_ResolvedGroup> outGroups)
	{
		RK29_ChoiceGroup ammoDef = def.m_AmmoGroup;
		if (IsParked(ammoDef))
			return;

		if (ammoDef && ammoDef.m_sId != "")
		{
			string ammoUseId;
			if (!SubstitutedGroupId(subs, def.m_sId, ammoDef.m_sId, ammoUseId))
			{
				ammoDef = null;
			}
			else if (ammoUseId != ammoDef.m_sId)
			{
				ammoDef = setup.FindChoiceGroup(ammoUseId);
				if (!ammoDef)
					ComplainOnce(string.Format("[RK29] config ERROR - choice group '%1'"
						+ " substituted for weapon '%2's ammo is not in any catalog",
						ammoUseId, def.m_sId), LogLevel.ERROR);
			}
		}

		if (IsParked(ammoDef) || !ammoDef)
			return;

		RK29_ResolvedGroup ammoGroup = ResolveGroup(ammoDef, def.m_sId, factionKey, setup);
		if (ammoGroup.m_sId == "")
		{
			ammoGroup.m_sId = AmmoIdOf(def.m_sId);
			if (ammoGroup.m_sDisplayName == "")
				ammoGroup.m_sDisplayName = ammoGroup.m_sId;
		}

		// TryInsertGroup's duplicate-id rule, spelled out because the id is only settled above and
		// because the second weapon silently gets no magazines and no selector
		if (FindGroup(outGroups, ammoGroup.m_sId))
		{
			ComplainOnce(string.Format("[RK29] config WARNING - ammo group '%1' offered twice"
				+ " (%2), keeping the first - '%3' gets no magazines of its own",
				ammoGroup.m_sId, cls.m_sKitName, def.m_sId), LogLevel.WARNING);
			return;
		}

		outGroups.Insert(ammoGroup);
	}

	//------------------------------------------------------------------------------------------------
	//! The shared groups a weapon definition references by id. Capability-screened against this
	//! weapon's own prefab.
	protected static void OfferWeaponReferencedGroups(notnull RK29_ClassSetup cls,
		notnull RK29_WeaponDef def, notnull RK29_KitSetup setup,
		notnull array<RK29_OverrideSubstitute> subs, string factionKey,
		notnull array<ref RK29_ResolvedGroup> outGroups)
	{
		if (!def.m_aGroups)
			return;

		foreach (string gid : def.m_aGroups)
		{
			// substitution before the duplicate check: the offer knows a group by the id it ends up
			// with, so two guns naming one id stop colliding once a substitution moves one
			string useId;
			if (!SubstitutedGroupId(subs, def.m_sId, gid, useId))
				continue;

			if (FindGroup(outGroups, useId))
			{
				ComplainOnce(string.Format("[RK29] config WARNING - group '%1' offered twice (%2),"
					+ " keeping the first", useId, cls.m_sKitName), LogLevel.WARNING);
				continue;
			}
			RK29_ChoiceGroup gdef = setup.FindChoiceGroup(useId);
			if (!gdef)
			{
				ComplainOnce(string.Format("[RK29] config ERROR - choice group '%1' referenced by"
					+ " weapon '%2' is not in any catalog",
					useId, def.m_sId), LogLevel.ERROR);
				continue;
			}
			if (IsParked(gdef))
				continue;
			RK29_ResolvedGroup weaponGroup = ResolveGroup(gdef, def.m_sId, factionKey, setup);

			// what makes broad group refs safe to author: an M60 given the rifle optics group
			// simply shows no optic section
			if (!PruneUnmountable(weaponGroup,
				WeaponPrefabOfId(setup, def.m_sId, factionKey), setup))
				continue;

			EvictSameSlot(outGroups, gdef);
			outGroups.Insert(weaponGroup);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The passes over a whole offer, in this order and no other. BuildOffer's header states the
	//! order and why ApplyOverrides and DropZeroCeilingEntries repeat here.
	protected static void FinalisePasses(RK29_KitComposition comp, notnull RK29_KitSetup setup, array<ref RK29_ChoicePick> picks, string factionKey,
		notnull array<ref RK29_ResolvedGroup> outGroups)
	{
		ApplyOverrides(comp, setup, outGroups);
		DropZeroCeilingEntries(outGroups);
		EnforceAttachmentLegality(outGroups, picks, setup, factionKey);
		EnforceGarmentSlots(outGroups, picks, setup, factionKey);
		EnforceExclusions(outGroups, picks, comp, false);
		OfferLoadedSelectors(outGroups);
		DropEmptyGroups(outGroups);
		SortOfferByOrder(outGroups);
	}

	//------------------------------------------------------------------------------------------------
	//! A garment attachment is refused by the garment, not by an exclusion: RHS night vision seats only in
	//! a helmet whose cloth-node NVG slot is enabled, and the vanilla PASGT under RHS declares it
	//! off. Asked of the picked garment's prefab, so it follows the hat pick on every rebuild, and
	//! marked rather than removed like every other legality block. Before the late exclusions, so a row
	//! the garment refuses cannot satisfy one. The culprit named is the garment answer itself - the
	//! row the player can go and change; no answer at all (None, or no group dressing the slot)
	//! leaves the entry empty and the menu says what is needed instead.
	protected static void EnforceGarmentSlots(notnull array<ref RK29_ResolvedGroup> groups,
		array<ref RK29_ChoicePick> picks, notnull RK29_KitSetup setup, string factionKey)
	{
		foreach (RK29_ResolvedGroup g : groups)
		{
			if (!g || !g.IsGarmentAttachmentGroup())
				continue;

			string hostId, wornId;
			bool offered;
			RK29_ResolvedGroup host = FindWornSlotGroup(groups, g.m_sGarmentSlot);
			if (host)
			{
				hostId = host.m_sId;
				RK29_ResolvedEntry worn = ExclusiveAnswer(host, picks);
				if (worn)
				{
					wornId = worn.m_sId;
					ResourceName garment = ResolveItemPrefabFor(RK29_EntryItem.Cast(worn.m_Def),
						ResourceName.Empty, null, factionKey, setup);
					offered = RK29_KitCompose.GarmentOffersSlot(garment, g.m_sSlotOnGarment);
				}
			}
			if (offered)
				continue;

			foreach (RK29_ResolvedEntry entry : g.m_aEntries)
			{
				if (!entry || entry.m_bBlocked)
					continue;
				entry.m_bBlocked = true;
				entry.m_bBlockedMissing = true;
				entry.m_sBlockedGroup = hostId;
				entry.m_sBlockedEntry = wornId;
				entry.m_iBlockedOver = -1;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The clothing group dressing that loadout slot, or null. One per slot: EvictSameSlot keeps it so.
	protected static RK29_ResolvedGroup FindWornSlotGroup(notnull array<ref RK29_ResolvedGroup> groups,
		string slot)
	{
		if (slot == "")
			return null;
		foreach (RK29_ResolvedGroup g : groups)
		{
			if (g && g.IsClothingGroup() && g.m_sWornSlot == slot)
				return g;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! An EXCLUSIVE group's answer as the apply reads it: the pick, unless it names a blocked entry
	//! (the kit has to agree with the greyed row the player was shown); else the default; null for
	//! a deliberate None where the group allows one. NONE is an answer and must be told apart from
	//! not having answered - PickedEntry returns null for both, and only the pick itself can say
	//! which. Shared by the apply and by every pass that must agree with it.
	static RK29_ResolvedEntry ExclusiveAnswer(notnull RK29_ResolvedGroup g, array<ref RK29_ChoicePick> picks)
	{
		RK29_ChoicePick bare = FindPick(picks, g.m_sId);
		if (g.m_bAllowEmpty && bare && bare.m_sEntry == "")
			return null;

		RK29_ResolvedEntry chosen = PickedEntry(g, picks);
		if (chosen && chosen.m_bBlocked)
			chosen = null;
		if (!chosen)
			chosen = g.DefaultEntry();
		return chosen;
	}

	//------------------------------------------------------------------------------------------------
	//! What the weapon will actually carry once applied: the prefab's own fittings, minus whatever
	//! the kit's groups replace (a group replaces a fitting when their types share a seat), plus
	//! what those groups picked. skip is the group about to be judged, and leaving it out is not an
	//! optimisation: its own alternatives replace each other, so picking the suppressor would
	//! otherwise prune the flash hider that would have taken its place.
	protected static array<string> EffectiveMountedTypes(notnull array<ref RK29_ResolvedGroup> groups,
		array<ref RK29_ChoicePick> picks, notnull RK29_KitSetup setup, string owner,
		ResourceName weapon, RK29_ResolvedGroup skip)
	{
		// a copy: MountedTypesOf hands back its cache entry, and this pass edits what it gets
		array<string> mounted = {};
		foreach (string t : RK29_KitCompose.MountedTypesOf(weapon))
			mounted.Insert(t);

		foreach (RK29_ResolvedGroup g : groups)
		{
			if (!g || g == skip || !g.IsAttachmentGroup() || g.m_sOwnerWeapon != owner)
				continue;

			for (int i = mounted.Count() - 1; i >= 0; i--)
			{
				if (SharesSeatWithType(g, mounted[i]))
					mounted.RemoveOrdered(i);
			}

			RK29_ResolvedEntry chosen = PickedEntry(g, picks);
			if (!chosen)
				continue;
			RK29_EntryAttachment att = RK29_EntryAttachment.Cast(chosen.m_Def);
			if (!att)
				continue;
			RK29_AttachmentDef adef = setup.FindAttachmentDef(att.m_sAttachment);
			if (!adef || adef.m_sPrefab == ResourceName.Empty)
				continue;

			foreach (string t : RK29_KitCompose.AttachTypesOf(adef.m_sPrefab))
			{
				if (!mounted.Contains(t))
					mounted.Insert(t);
			}
		}
		return mounted;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether one mount type sits in the seat this group speaks for. Both directions, because
	//! either side may be the broader type (AttachmentOpticsRIS1913Short vs ...Medium on one rail).
	//! RK29_ResolvedGroup.SharesSeatWith is this run over another group's seats - one body.
	static bool SharesSeatWithType(notnull RK29_ResolvedGroup g, string mountedType)
	{
		foreach (string seat : g.m_aSeatTypes)
		{
			if (RK29_KitCompose.MountFits(seat, mountedType)
				|| RK29_KitCompose.MountFits(mountedType, seat))
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Entry id whose attachment declares `type`, or "". `only` narrows to a single entry (the
	//! group's current pick); null searches every entry.
	protected static string EntryOfferingType(notnull RK29_ResolvedGroup g,
		notnull RK29_KitSetup setup, string type, RK29_ResolvedEntry only)
	{
		foreach (RK29_ResolvedEntry e : g.m_aEntries)
		{
			if (!e || (only && e != only))
				continue;
			RK29_EntryAttachment att = RK29_EntryAttachment.Cast(e.m_Def);
			if (!att)
				continue;
			RK29_AttachmentDef adef = setup.FindAttachmentDef(att.m_sAttachment);
			if (!adef || adef.m_sPrefab == ResourceName.Empty)
				continue;
			if (!RK29_KitCompose.AttachTypesOf(adef.m_sPrefab).Contains(type))
				continue;

			return e.m_sId;
		}
		return "";
	}

	//------------------------------------------------------------------------------------------------
	//! Which of this weapon's groups put a type on the gun - the row an obstructed entry points the
	//! player at. Empty when the type came off the weapon prefab and no group speaks for it.
	protected static void FindPickedTypeSource(notnull array<ref RK29_ResolvedGroup> groups,
		array<ref RK29_ChoicePick> picks, notnull RK29_KitSetup setup, string owner,
		string type, out string outGroup, out string outEntry)
	{
		outGroup = "";
		outEntry = "";

		foreach (RK29_ResolvedGroup g : groups)
		{
			if (!g || !g.IsAttachmentGroup() || g.m_sOwnerWeapon != owner)
				continue;

			RK29_ResolvedEntry chosen = PickedEntry(g, picks);
			if (!chosen)
				continue;

			string entryId = EntryOfferingType(g, setup, type, chosen);
			if (entryId == "")
				continue;

			outGroup = g.m_sId;
			outEntry = entryId;
			return;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Which of this weapon's groups could still supply a type - the row a missing prerequisite
	//! points at, picked or not. Empty when none offers it.
	protected static void FindOfferableTypeSource(notnull array<ref RK29_ResolvedGroup> groups,
		notnull RK29_KitSetup setup, string owner, string type,
		out string outGroup, out string outEntry)
	{
		outGroup = "";
		outEntry = "";

		foreach (RK29_ResolvedGroup g : groups)
		{
			if (!g || !g.IsAttachmentGroup() || g.m_sOwnerWeapon != owner)
				continue;

			string entryId = EntryOfferingType(g, setup, type, null);
			if (entryId == "")
				continue;

			outGroup = g.m_sId;
			outEntry = entryId;
			return;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! PruneUnmountable screens against the gun as authored and cannot see a conflict one pick
	//! creates for another (clear the muzzle and the M9 bayonet loses its required flash hider),
	//! which the engine then refuses at apply, silently. So this runs last, offer whole and every
	//! pick known. It clears no pick - a pick naming a blocked entry resolves to nothing on its own
	//! and comes back if the conflict does - and pick order does not decide the outcome.
	protected static void EnforceAttachmentLegality(notnull array<ref RK29_ResolvedGroup> groups,
		array<ref RK29_ChoicePick> picks, notnull RK29_KitSetup setup, string factionKey)
	{
		array<string> owners = {};
		foreach (RK29_ResolvedGroup g : groups)
		{
			if (g && g.IsAttachmentGroup() && g.m_sOwnerWeapon != ""
				&& !owners.Contains(g.m_sOwnerWeapon))
				owners.Insert(g.m_sOwnerWeapon);
		}

		foreach (string owner : owners)
		{
			ResourceName weapon = WeaponPrefabOfId(setup, owner, factionKey);
			if (weapon == ResourceName.Empty)
				continue;

			foreach (RK29_ResolvedGroup g : groups)
			{
				if (!g || !g.IsAttachmentGroup() || g.m_sOwnerWeapon != owner)
					continue;

				// re-asked per group, because each is judged against a set that leaves its seat out
				array<string> mounted = EffectiveMountedTypes(groups, picks, setup, owner, weapon, g);
				BlockIllegalEntries(g, mounted, groups, picks, setup, owner);

				DeriveSeatTypes(g, setup);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Mark every entry of one group that cannot be taken alongside `mounted`, and name what stands
	//! in its way. Nothing is removed - the row stays on screen, greyed.
	protected static void BlockIllegalEntries(notnull RK29_ResolvedGroup g,
		notnull array<string> mounted, notnull array<ref RK29_ResolvedGroup> groups,
		array<ref RK29_ChoicePick> picks, notnull RK29_KitSetup setup, string owner)
	{
		foreach (RK29_ResolvedEntry entry : g.m_aEntries)
		{
			if (!entry)
				continue;
			RK29_EntryAttachment att = RK29_EntryAttachment.Cast(entry.m_Def);
			if (!att)
				continue;
			RK29_AttachmentDef adef = setup.FindAttachmentDef(att.m_sAttachment);
			if (!adef || adef.m_sPrefab == ResourceName.Empty)
				continue;

			bool missing;
			string type;
			if (!RK29_KitCompose.AttachmentIllegalGiven(mounted, adef.m_sPrefab, missing, type))
				continue;

			entry.m_bBlocked = true;
			entry.m_bBlockedMissing = missing;
			// an obstruction is owned by whichever group picked the offending type; a missing
			// prerequisite by whichever group could still supply it
			if (missing)
				FindOfferableTypeSource(groups, setup, owner, type,
					entry.m_sBlockedGroup, entry.m_sBlockedEntry);
			else
				FindPickedTypeSource(groups, picks, setup, owner, type,
					entry.m_sBlockedGroup, entry.m_sBlockedEntry);
			// no log: this runs on every pick change, and the menu already names the culprit
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The kit's own rulings. Two calls: `weaponTargets` true runs before the weapon pull over the
	//! exclusions that block a WEAPON group (kit-level, so present), false runs late, after the attachment
	//! legality pass, over every other exclusion - a thing that pass blocked must not then satisfy an exclusion
	//! here. PickedCount answering zero for a blocked entry makes that true everywhere at once, and
	//! is why two exclusions pointing at each other terminate. Array order is priority.
	protected static void EnforceExclusions(notnull array<ref RK29_ResolvedGroup> groups,
		array<ref RK29_ChoicePick> picks, RK29_KitComposition comp, bool weaponTargets)
	{
		if (!comp || !comp.m_aExclusions)
			return;

		foreach (RK29_Exclusion exclusion : comp.m_aExclusions)
		{
			if (!exclusion || exclusion.m_sWhen == "" || exclusion.m_sBlock == "")
				continue;

			// the target decides which of the two calls this exclusion belongs to; one not in the offer
			// (a weapon-owned group before the pull) waits for the late call
			string blockWeapon, blockGroup, blockEntry;
			SplitTarget(exclusion.m_sBlock, blockWeapon, blockGroup, blockEntry);
			RK29_ResolvedGroup target = FindGroupOwnedBy(groups, blockWeapon, blockGroup);
			if (!target || target.IsWeaponGroup() != weaponTargets)
				continue;

			string whenWeapon, whenGroup, whenEntry;
			SplitTarget(exclusion.m_sWhen, whenWeapon, whenGroup, whenEntry);
			RK29_ResolvedGroup source = FindGroupOwnedBy(groups, whenWeapon, whenGroup);
			if (!source)
				continue;

			int have = 0;
			if (source.m_eKind == RK29_EChoiceKind.EXCLUSIVE || source.IsWeaponGroup())
			{
				// An EXCLUSIVE group is held by its answer, picked or defaulted. PickedCount is the
				// wrong question: an exclusive entry authors no default count, so with no pick yet
				// made every gun answered zero and the exclusion slept until the tile was touched. A
				// weapon group answers through PickedWeaponEntry, the same read the pull makes.
				RK29_ResolvedEntry chosen;
				if (source.IsWeaponGroup())
					chosen = PickedWeaponEntry(source, picks);
				else
					chosen = PickedEntry(source, picks);
				if (chosen && !chosen.m_bBlocked && (whenEntry == "" || chosen.m_sId == whenEntry))
					have = 1;
			}
			else
			{
				foreach (RK29_ResolvedEntry candidate : source.m_aEntries)
				{
					if (!candidate)
						continue;
					if (whenEntry != "" && candidate.m_sId != whenEntry)
						continue;
					have += PickedCount(source, candidate, picks);
				}
			}
			// marked before the threshold test: the warning is about what this choice would do.
			// First exclusion wins, matching the array-order priority the exclusions run under
			foreach (RK29_ResolvedEntry trigger : source.m_aEntries)
			{
				if (!trigger || trigger.m_sExcludes != "")
					continue;
				if (whenEntry != "" && trigger.m_sId != whenEntry)
					continue;
				trigger.m_sExcludes = exclusion.m_sBlock;
			}

			if (have <= exclusion.m_iOver)
				continue;

			foreach (RK29_ResolvedEntry victim : target.m_aEntries)
			{
				if (!victim || victim.m_bBlocked)
					continue;
				if (blockEntry != "" && victim.m_sId != blockEntry)
					continue;

				victim.m_bBlocked = true;
				victim.m_bBlockedMissing = false;
				victim.m_sBlockedGroup = source.m_sId;
				victim.m_sBlockedEntry = whenEntry;
				victim.m_iBlockedOver = exclusion.m_iOver;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The loaded-round selector each surviving totals group earns, built from its final
	//! entries. Late on purpose: a selector is a copy of its group's entries, so synthesizing it any
	//! earlier leaves a ghost row offering to chamber a round a later pass took away. Every pass that
	//! removes an entry has run by here - the two Enforce passes mark rows, they never remove them.
	//!
	//! Only a weapon-owned group can earn one: DeriveGroupLoadedSeat leaves a kit-level group at NONE
	//! (nothing to chamber into), and an EXCLUSIVE group states no total for a seated round to come
	//! out of - which is also what keeps a weapon's attachment points out.
	//!
	//! Directly after its totals group, not appended: the pair shares an m_iOrder, so their array
	//! order is what the stable SortOfferByOrder preserves, and Apply emits in that same order.
	//!
	//! One selector per chamber, not per group: a later group feeding the same muzzle of the same
	//! gun (the 40mm smoke and flare pools after the explosive pool) adds its rows to the selector
	//! the first one earned, so three pools share one loaded mark, one seat order and one
	//! deduction. The selector keeps the first group's id and place; the lint holds every pool a
	//! gun owns to one entry-id space, which is what lets a row be found by id alone.
	protected static void OfferLoadedSelectors(notnull array<ref RK29_ResolvedGroup> groups)
	{
		for (int i = 0; i < groups.Count(); i++)
		{
			RK29_ResolvedGroup g = groups[i];
			if (!g || g.m_bLoaded || g.m_eLoadedSeat == RK29_ELoadedSeat.NONE
				|| g.m_eKind == RK29_EChoiceKind.EXCLUSIVE)
				continue;

			RK29_ResolvedGroup shared = FindLoadedSelector(groups, g.m_sOwnerWeapon, g.m_eLoadedSeat);
			if (shared)
			{
				AppendSelectorEntries(shared, g);
				continue;
			}

			// null when nothing is left to chamber - the group itself then goes to DropEmptyGroups
			RK29_ResolvedGroup loaded = SynthesizeLoadedGroup(g);
			if (!loaded || FindGroup(groups, loaded.m_sId))
				continue;

			// InsertAt's contract wants an index below Count(), so the last group appends instead
			if (i + 1 < groups.Count())
				groups.InsertAt(loaded, i + 1);
			else
				groups.Insert(loaded);

			// over the selector just inserted, which is no candidate of its own
			i++;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Drops the entries of an attachment group the given weapon cannot mount. Returns FALSE when
	//! the group is left with no mountable entry - the caller then skips it entirely.
	protected static bool PruneUnmountable(notnull RK29_ResolvedGroup group,
		ResourceName weaponPrefab, notnull RK29_KitSetup setup)
	{
		if (weaponPrefab == ResourceName.Empty)
			return true;

		// Ammo is a capability too: a variant resolves against the owning weapon's well, so a well
		// without it means the gun cannot be issued that entry - a prune, not an emit-time config
		// error. Gated on the variant, not on the group being an ammo group: ammo groups resolve to
		// plain ITEM like any other, and the variant is what needs a well to mean anything.
		if (!PruneMissingVariants(group, weaponPrefab, setup))
			return false;

		if (!group.IsAttachmentGroup())
			return true;

		for (int i = group.m_aEntries.Count() - 1; i >= 0; i--)
		{
			RK29_ResolvedEntry entry = group.m_aEntries[i];
			if (!entry)
				continue;
			RK29_EntryAttachment att = RK29_EntryAttachment.Cast(entry.m_Def);
			if (!att)
				continue;

			RK29_AttachmentDef adef = setup.FindAttachmentDef(att.m_sAttachment);
			if (adef && adef.m_sPrefab != ResourceName.Empty
				&& !RK29_KitCompose.WeaponRejectsAttachment(weaponPrefab, adef.m_sPrefab))
				continue;

			// RemoveOrdered, not Remove: Remove swap-drops the last entry into the hole, and entry
			// order is the row order the player reads
			group.m_aEntries.RemoveOrdered(i);
		}

		// the seat this group speaks for is the seat its survivors fill
		DeriveSeatTypes(group, setup);

		return !group.m_aEntries.IsEmpty();
	}

	//------------------------------------------------------------------------------------------------
	//! Ammo entries this weapon has no magazine for. An ammo group is shared across weapons whose
	//! wells differ (br_ammo spans the M14 and the SVD), so a row added to one is a bet that every
	//! well behind it has that variant; losing the bet would be a config error on every kit build.
	//! Entries carrying an explicit prefab or alias are left alone.
	protected static bool PruneMissingVariants(notnull RK29_ResolvedGroup group,
		ResourceName weaponPrefab, notnull RK29_KitSetup setup)
	{
		array<string> wells = RK29_KitCompose.WellsOf(weaponPrefab);
		if (!wells || wells.IsEmpty())
			return !group.m_aEntries.IsEmpty();

		for (int i = group.m_aEntries.Count() - 1; i >= 0; i--)
		{
			RK29_ResolvedEntry entry = group.m_aEntries[i];
			if (!entry)
				continue;
			RK29_EntryItem item = RK29_EntryItem.Cast(entry.m_Def);
			if (!item || item.m_sVariant == "")
				continue;
			if (setup.FindMagVariant(wells, item.m_sVariant) != ResourceName.Empty)
				continue;

			group.m_aEntries.RemoveOrdered(i);
		}

		return !group.m_aEntries.IsEmpty();
	}

	//------------------------------------------------------------------------------------------------
	//! The suffix is stated here only - the selector's id, asked through LoadedIdOf. Never used to
	//! find a selector: LoadedSiblingOf goes by chamber.
	static const string LOADED_SUFFIX = "_loaded";

	//------------------------------------------------------------------------------------------------
	protected static string LoadedIdOf(string groupId)
	{
		return groupId + LOADED_SUFFIX;
	}

	//------------------------------------------------------------------------------------------------
	//! The suffix is stated here only - the id a weapon's inline ammo group is addressed by when it
	//! authors none of its own. RK29_KitLint reads this constant rather than spelling it again.
	static const string AMMO_SUFFIX = "_ammo";

	//------------------------------------------------------------------------------------------------
	protected static string AmmoIdOf(string weaponId)
	{
		return weaponId + AMMO_SUFFIX;
	}

	//------------------------------------------------------------------------------------------------
	//! Null when the offer carries no selector for this group - its counts are then plain spares.
	//! By chamber, not by name: the selector over a gun's muzzle is shared by every counted group
	//! feeding it, and only the first of them gave it its id.
	static RK29_ResolvedGroup LoadedSiblingOf(notnull array<ref RK29_ResolvedGroup> groups, notnull RK29_ResolvedGroup g)
	{
		if (g.m_bLoaded || g.m_eKind == RK29_EChoiceKind.EXCLUSIVE
			|| g.m_eLoadedSeat == RK29_ELoadedSeat.NONE)
			return null;
		return FindLoadedSelector(groups, g.m_sOwnerWeapon, g.m_eLoadedSeat);
	}

	//------------------------------------------------------------------------------------------------
	//! The synthesized selector standing over one gun's muzzle, or null. The pair (owner, seat) is
	//! the chamber's identity; a rifle with a launcher has two.
	protected static RK29_ResolvedGroup FindLoadedSelector(notnull array<ref RK29_ResolvedGroup> groups,
		string owner, RK29_ELoadedSeat seat)
	{
		foreach (RK29_ResolvedGroup g : groups)
		{
			if (g && g.m_bLoaded && g.m_sOwnerWeapon == owner && g.m_eLoadedSeat == seat)
				return g;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! The one place this is decided - the menu's badge and the HUD's magnified tally both ask here.
	//! The catalog's m_bMagnified read through the point's exemption: an exempt point answers no
	//! whatever it wears.
	static bool IsMagnifiedEntry(notnull RK29_KitSetup setup, notnull RK29_ResolvedGroup g, notnull RK29_ResolvedEntry e)
	{
		if (g.m_bMagnifiedExempt)
			return false;

		RK29_AttachmentDef def = AttachmentDefOf(setup, e);
		if (!def)
			return false;
		return def.m_bMagnified;
	}

	//------------------------------------------------------------------------------------------------
	//! By payload id first, entry id only where the payload names nothing: reading the entry id
	//! alone misses an entry authoring an explicit m_sId, and an undefined scope reads as
	//! unmagnified. Null for an entry that is not an attachment.
	static RK29_AttachmentDef AttachmentDefOf(notnull RK29_KitSetup setup, notnull RK29_ResolvedEntry e)
	{
		RK29_EntryAttachment att = RK29_EntryAttachment.Cast(e.m_Def);
		if (!att)
			return null;

		if (att.m_sAttachment != "")
			return setup.FindAttachmentDef(att.m_sAttachment);
		return setup.FindAttachmentDef(e.m_sId);
	}

	//------------------------------------------------------------------------------------------------
	protected static ref array<string> s_aComplained = {};

	//------------------------------------------------------------------------------------------------
	//! Printed once a session: BuildOffer runs on every pick the menu takes. The message is the key,
	//! so include whatever distinguishes one instance of a fault from another.
	protected static void ComplainOnce(string message, LogLevel level)
	{
		if (s_aComplained.Contains(message))
			return;

		s_aComplained.Insert(message);
		Print(message, level);
	}

	//------------------------------------------------------------------------------------------------
	//! The complaint ledger and the prefab read behind it. Called at every world start beside
	//! RK29_KitCompose.ClearCaches, so a config fixed between sessions is read afresh.
	static void ClearSessionState()
	{
		s_aComplained.Clear();
		s_mSlotTypeCache.Clear();
	}

	//------------------------------------------------------------------------------------------------
	static RK29_ResolvedGroup FindGroup(notnull array<ref RK29_ResolvedGroup> groups, string id)
	{
		foreach (RK29_ResolvedGroup g : groups)
		{
			if (g && g.m_sId == id)
				return g;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Catalog group -> runtime view: entries copied, so the numbers become adjustable. Wrong-faction
	//! and parked entries never make the copy, so nothing downstream can know they were authored.
	protected static RK29_ResolvedGroup ResolveGroup(notnull RK29_ChoiceGroup def, string ownerWeapon,
		string factionKey, notnull RK29_KitSetup setup)
	{
		RK29_ResolvedGroup g = new RK29_ResolvedGroup();
		g.m_sId = def.m_sId;
		g.m_sDisplayName = def.m_sDisplayName;
		if (g.m_sDisplayName == "")
			g.m_sDisplayName = def.m_sId;
		g.m_sOwnerWeapon = ownerWeapon;
		g.m_iOrder = def.m_iOrder;

		CopyGroupShape(def, g);

		if (def.m_aEntries)
		{
			foreach (RK29_ChoiceEntryBase e : def.m_aEntries)
			{
				if (!e)
					continue;
				if (!EntryServesFaction(e, factionKey))
					continue;
				if (IsEntryParked(e))
					continue;
				// first-wins, the same rule the include merge below uses. The twin this catches is
				// usually not a typo: bare-brace arrays in a child conf merge with the parent's
				// entries, so restating one to "override" it appends a second copy
				string ownId = EntryIdOf(e);
				if (ownId != "" && g.FindEntry(ownId))
					continue;
				g.m_aEntries.Insert(ResolveEntry(e));
			}
		}

		MergeIncludedGroups(def, g, factionKey, setup);

		// entries are final here, and this is the one place their order is decided
		SortEntriesByOrder(g);

		// last: the seat is read off the entries the includes have only just finished contributing
		DeriveSeatTypes(g, setup);
		DeriveGroupLoadedSeat(g, setup, factionKey);

		return g;
	}

	//------------------------------------------------------------------------------------------------
	//! The one place the authored group class is read; the type it establishes is what every
	//! consumer downstream reads instead of guessing from the entries. The fallthrough is a bare
	//! RK29_ChoiceGroup, left at the inert reading (EXCLUSIVE, no budget, no seat) - unauthorable
	//! now that every list that holds groups is typed to a concrete subclass.
	protected static void CopyGroupShape(notnull RK29_ChoiceGroup def, notnull RK29_ResolvedGroup g)
	{
		RK29_ItemGroup itemDef = RK29_ItemGroup.Cast(def);
		RK29_AttachmentGroup attDef = RK29_AttachmentGroup.Cast(def);
		if (itemDef)
		{
			g.m_eGroupType = RK29_EGroupType.ITEM;

			// a clothing group is an item group and casts as one, so take the narrower reading first
			RK29_ClothingGroup wornDef = RK29_ClothingGroup.Cast(def);
			if (wornDef)
			{
				g.m_eGroupType = RK29_EGroupType.CLOTHING;
				g.m_sWornSlot = wornDef.m_sSlot;
				g.m_bAllowEmpty = wornDef.m_bAllowEmpty;
			}
			// likewise a garment-attachment group; a sibling of the clothing class, never both
			RK29_GarmentAttachmentGroup onDef = RK29_GarmentAttachmentGroup.Cast(def);
			if (onDef)
			{
				g.m_eGroupType = RK29_EGroupType.GARMENT_ATTACHMENT;
				g.m_sGarmentSlot = onDef.m_sGarmentSlot;
				g.m_sSlotOnGarment = onDef.m_sSlot;
				g.m_bAllowEmpty = onDef.m_bAllowEmpty;
			}
			g.m_eKind = itemDef.m_eKind;
			g.m_iBudget = itemDef.m_iBudget;
			g.m_iKeepRank = itemDef.m_iKeepRank;
			// the seat is not copied - DeriveGroupLoadedSeat derives it once the entries are known
			return;
		}

		if (attDef)
		{
			g.m_eGroupType = RK29_EGroupType.ATTACHMENT;
			g.m_bAllowEmpty = attDef.m_bAllowEmpty;
			g.m_bIsOpticsPoint = attDef.m_bIsOpticsPoint;
			g.m_bCarryWhenUnfitted = attDef.m_bCarryWhenUnfitted;
			g.m_bMagnifiedExempt = attDef.m_bMagnifiedExempt;
			return;
		}

		// nothing to copy for a weapon group; EXCLUSIVE is already the zero value of m_eKind
		if (RK29_WeaponGroup.Cast(def))
			g.m_eGroupType = RK29_EGroupType.WEAPON;
	}

	//------------------------------------------------------------------------------------------------
	//! Included catalog groups contribute their entries after this group's own. First duplicate id
	//! wins; the first default flag in merged order is the default. one level only - an include that
	//! itself includes is skipped with a complaint rather than recursed.
	protected static void MergeIncludedGroups(notnull RK29_ChoiceGroup def,
		notnull RK29_ResolvedGroup g, string factionKey, notnull RK29_KitSetup setup)
	{
		if (!def.m_aIncludeGroups)
			return;

		foreach (string includeId : def.m_aIncludeGroups)
		{
			RK29_ChoiceGroup inc = setup.FindChoiceGroup(includeId);
			if (!inc)
			{
				ComplainOnce(string.Format("[RK29] config ERROR - include group '%1' of '%2'"
					+ " is not in any catalog", includeId, def.m_sId), LogLevel.ERROR);
				continue;
			}
			if (IsParked(inc))
				continue;
			if (inc.m_aIncludeGroups && !inc.m_aIncludeGroups.IsEmpty())
			{
				ComplainOnce(string.Format("[RK29] config ERROR - include group '%1' has"
					+ " includes of its own - nested includes are not resolved",
					includeId), LogLevel.ERROR);
				continue;
			}
			if (!inc.m_aEntries)
				continue;

			g.m_aIncludedGroups.Insert(inc.m_sId);
			// its own copy loop, not a recursive ResolveGroup, so the filters are restated here
			foreach (RK29_ChoiceEntryBase incEntry : inc.m_aEntries)
			{
				if (!incEntry)
					continue;
				if (!EntryServesFaction(incEntry, factionKey))
					continue;
				if (IsEntryParked(incEntry))
					continue;
				RK29_ResolvedEntry merged = ResolveEntry(incEntry);
				if (g.FindEntry(merged.m_sId))
					continue;
				g.m_aEntries.Insert(merged);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! A group's derived seat - the union of the mount types its entries' prefabs declare. Called at
	//! the three points the entry list is settled: end of ResolveGroup (includes contributed), end
	//! of PruneUnmountable (a group speaks for the seat its survivors fill), and after
	//! BlockIllegalEntries. Never per query.
	//!
	//! Non-attachment groups keep an empty list, which is what lets SharesSeatWith be asked of any
	//! pair of groups.
	protected static void DeriveSeatTypes(notnull RK29_ResolvedGroup g, notnull RK29_KitSetup setup)
	{
		g.m_aSeatTypes.Clear();
		if (!g.IsAttachmentGroup())
			return;

		foreach (RK29_ResolvedEntry e : g.m_aEntries)
		{
			if (!e)
				continue;
			RK29_EntryAttachment att = RK29_EntryAttachment.Cast(e.m_Def);
			if (!att)
				continue;

			RK29_AttachmentDef def = setup.FindAttachmentDef(att.m_sAttachment);
			if (!def || def.m_sPrefab == ResourceName.Empty)
				continue;

			// the cached array, read and never written - see AttachTypesOf
			array<string> types = RK29_KitCompose.AttachTypesOf(def.m_sPrefab);
			foreach (string t : types)
			{
				if (!g.m_aSeatTypes.Contains(t))
					g.m_aSeatTypes.Insert(t);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The round the weapon starts with, as an EXCLUSIVE group over the ammo group's entries. no
	//! counts of its own - the kit's stated total includes the loaded one, so ApplyItemGroup deducts
	//! the seated magazine from the ammo group. Null when the ammo group offers no items. Marking a
	//! row the kit carries NONE of is legal and means an empty chamber. Reads the resolved ammo
	//! group, so it needs no faction or parked filtering of its own.
	protected static RK29_ResolvedGroup SynthesizeLoadedGroup(notnull RK29_ResolvedGroup ammoGroup)
	{
		RK29_ResolvedGroup g = new RK29_ResolvedGroup();
		g.m_sId = LoadedIdOf(ammoGroup.m_sId);
		g.m_sDisplayName = "Loaded magazine";
		g.m_eKind = RK29_EChoiceKind.EXCLUSIVE;
		// no authored group behind this one, so the type is stated rather than copied
		g.m_eGroupType = RK29_EGroupType.ITEM;
		g.m_bLoaded = true;
		// the destination travels with the selector: an underbarrel group's selector seats there
		g.m_eLoadedSeat = ammoGroup.m_eLoadedSeat;
		g.m_sOwnerWeapon = ammoGroup.m_sOwnerWeapon;
		// same band as the group it spends from, so the stable sort keeps it directly after it
		g.m_iOrder = ammoGroup.m_iOrder;

		AppendSelectorEntries(g, ammoGroup);

		if (g.m_aEntries.IsEmpty())
			return null;
		return g;
	}

	//------------------------------------------------------------------------------------------------
	//! One ammo group's rows into a selector - the group's own at synthesis, and every later group
	//! on the same chamber's after it. A row already offered by id is not offered twice: the lint
	//! forbids that case, so this is the runtime's half of the same contract.
	protected static void AppendSelectorEntries(notnull RK29_ResolvedGroup loaded,
		notnull RK29_ResolvedGroup ammoGroup)
	{
		foreach (RK29_ResolvedEntry src : ammoGroup.m_aEntries)
		{
			if (!src || !RK29_EntryItem.Cast(src.m_Def) || loaded.FindEntry(src.m_sId))
				continue;

			// the payload only: the numbers stay with the ammo group this spends from. The block
			// travels: a round an exclusion took away cannot be offered as the one in the chamber
			RK29_ResolvedEntry e = new RK29_ResolvedEntry();
			e.m_sId = src.m_sId;
			e.m_bDefault = src.m_bDefault;
			e.m_Def = src.m_Def;
			e.m_bBlocked = src.m_bBlocked;
			e.m_bBlockedMissing = src.m_bBlockedMissing;
			e.m_sBlockedGroup = src.m_sBlockedGroup;
			e.m_sBlockedEntry = src.m_sBlockedEntry;
			e.m_iBlockedOver = src.m_iBlockedOver;
			e.m_iMin = 0;
			e.m_iDefault = 0;
			// -1, not 0: these numbers are inert here, and 0 now means zero
			e.m_iMax = -1;
			loaded.m_aEntries.Insert(e);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static RK29_ResolvedEntry ResolveEntry(notnull RK29_ChoiceEntryBase def)
	{
		RK29_ResolvedEntry e = new RK29_ResolvedEntry();
		e.m_sId = EntryIdOf(def);
		e.m_bDefault = def.m_bDefault;
		e.m_Def = def;

		RK29_EntryItem item = RK29_EntryItem.Cast(def);
		if (item)
		{
			e.m_iMin = item.m_iMin;
			e.m_iDefault = item.m_iDefault;
			e.m_iMax = item.m_iMax;
			e.m_iCost = item.m_iCost;
			if (e.m_iCost < 1)
				e.m_iCost = 1;
		}
		return e;
	}

	//------------------------------------------------------------------------------------------------
	//! An empty list is "everybody", not "nobody". A list rather than one key because factions
	//! overlap: a rifle two factions field is one entry naming both, not a copy per faction.
	protected static bool EntryServesFaction(notnull RK29_ChoiceEntryBase e, string factionKey)
	{
		if (!e.m_aFactionKeys || e.m_aFactionKeys.IsEmpty())
			return true;

		return e.m_aFactionKeys.Contains(factionKey);
	}

	//------------------------------------------------------------------------------------------------
	//! Whether two entries could ever reach the same class - what separates a genuine duplicate id
	//! from a faction pair (sniper_ammo states "ball" twice, once per faction). Either list empty
	//! answers to everyone and so overlaps with anything.
	static bool FactionsOverlap(notnull RK29_ChoiceEntryBase a, notnull RK29_ChoiceEntryBase b)
	{
		if (!a.m_aFactionKeys || a.m_aFactionKeys.IsEmpty())
			return true;
		if (!b.m_aFactionKeys || b.m_aFactionKeys.IsEmpty())
			return true;

		foreach (string key : a.m_aFactionKeys)
		{
			if (b.m_aFactionKeys.Contains(key))
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Does this group seat a round, and in which muzzle - asked of the weapon and the rounds rather
	//! than authored. A group chambers if any of its rounds does. Only a weapon-owned group can
	//! chamber anything; a kit-level group stays at NONE.
	protected static void DeriveGroupLoadedSeat(notnull RK29_ResolvedGroup g,
		notnull RK29_KitSetup setup, string factionKey)
	{
		g.m_eLoadedSeat = RK29_ELoadedSeat.NONE;
		if (g.m_sOwnerWeapon == "" || g.m_eGroupType != RK29_EGroupType.ITEM)
			return;

		ResourceName weapon = WeaponPrefabOfId(setup, g.m_sOwnerWeapon, factionKey);
		if (weapon == ResourceName.Empty)
			return;

		RK29_WeaponDef ownerDef = setup.FindWeaponDef(g.m_sOwnerWeapon);
		foreach (RK29_ResolvedEntry e : g.m_aEntries)
		{
			if (!e)
				continue;
			RK29_EntryItem item = RK29_EntryItem.Cast(e.m_Def);
			if (!item)
				continue;

			ResourceName round = ResolveItemPrefabFor(item, weapon, ownerDef, factionKey, setup);
			RK29_ELoadedSeat seat = RK29_KitCompose.DeriveLoadedSeat(weapon, round);
			if (seat != RK29_ELoadedSeat.NONE)
			{
				g.m_eLoadedSeat = seat;
				return;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! m_iOrder is the one statement about where a section sits, and this is the only place it is
	//! honoured. Insertion sort, stable by construction (an element moves only past a strictly
	//! greater order): a synthesized "<id>_loaded" selector sits directly after its ammo group, and
	//! an unstable sort would part them.
	protected static void SortOfferByOrder(notnull array<ref RK29_ResolvedGroup> groups)
	{
		for (int i = 1, n = groups.Count(); i < n; i++)
		{
			RK29_ResolvedGroup g = groups[i];
			if (!g)
				continue;

			int j = i - 1;
			while (j >= 0 && groups[j] && groups[j].m_iOrder > g.m_iOrder)
			{
				groups.Set(j + 1, groups[j]);
				j--;
			}
			groups.Set(j + 1, g);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The entry-level twin of SortOfferByOrder, stable for the same reason: entries sharing a
	//! number keep the order they arrived in (own entries, then includes). One key, authored - no
	//! derived tie-break on capacity, zoom or name; a shared number is an authoring omission.
	protected static void SortEntriesByOrder(notnull RK29_ResolvedGroup g)
	{
		array<ref RK29_ResolvedEntry> entries = g.m_aEntries;
		for (int i = 1, n = entries.Count(); i < n; i++)
		{
			RK29_ResolvedEntry e = entries[i];
			int order = EntryOrderOf(e);
			int j = i - 1;
			while (j >= 0 && EntryOrderOf(entries[j]) > order)
			{
				entries.Set(j + 1, entries[j]);
				j--;
			}
			entries.Set(j + 1, e);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static int EntryOrderOf(RK29_ResolvedEntry e)
	{
		if (!e || !e.m_Def)
			return 0;
		return e.m_Def.m_iOrder;
	}

	//------------------------------------------------------------------------------------------------
	//! An entry capped at zero is not offered at all (-1 carries "no cap authored") - what makes one
	//! shared group per family workable, each kit raising only its own item off zero. Runs after
	//! overrides and over every kind, which makes it the only way to take something out of an offer.
	protected static void DropZeroCeilingEntries(notnull array<ref RK29_ResolvedGroup> groups)
	{
		foreach (RK29_ResolvedGroup g : groups)
		{
			if (!g)
				continue;

			for (int i = g.m_aEntries.Count() - 1; i >= 0; i--)
			{
				RK29_ResolvedEntry e = g.m_aEntries[i];
				if (e && e.m_iMax == 0)
					g.m_aEntries.RemoveOrdered(i);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! A group with nothing left in it is not offered.
	protected static void DropEmptyGroups(notnull array<ref RK29_ResolvedGroup> groups)
	{
		for (int i = groups.Count() - 1; i >= 0; i--)
		{
			RK29_ResolvedGroup g = groups[i];
			if (!g)
			{
				groups.RemoveOrdered(i);
				continue;
			}
			// an entry-less worn-slot group that allows None is not empty: None is its whole offer,
			// and applying it clears the slot the body prefab would otherwise dress
			if (g.m_aEntries.IsEmpty() && !(g.m_sWornSlot != "" && g.m_bAllowEmpty))
				groups.RemoveOrdered(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One resolved entry as an emit row, shared by the counted and exclusive paths. The slot comes
	//! from the group for a clothing or garment-attachment group; an equipment entry carries its
	//! own. The maps are not interchangeable - a garment routed to m_mEquipment simply never appears.
	protected static RK29_WeaponAmmo RowFor(notnull RK29_ResolvedEntry entry, int count,
		notnull RK29_ResolvedGroup g)
	{
		RK29_EntryItem item = RK29_EntryItem.Cast(entry.m_Def);
		if (!item)
			return null;

		RK29_WeaponAmmo row = new RK29_WeaponAmmo();
		row.m_sAlias = item.m_sAlias;
		row.m_sVariant = item.m_sVariant;
		row.m_sPrefab = item.m_sPrefab;
		row.m_iCount = count;

		// most specific wins: the row's own rank, else the pool's; the alias and weapon fallbacks
		// are EmitCarriedRow's, which sees neither the entry nor the group
		row.m_iKeepRank = item.m_iKeepRank;
		if (row.m_iKeepRank < 0)
			row.m_iKeepRank = g.m_iKeepRank;

		RK29_EntryEquipment worn = RK29_EntryEquipment.Cast(entry.m_Def);
		if (worn)
			row.m_sSlot = worn.m_sSlot;

		if (g.m_sWornSlot != "")
		{
			row.m_sSlot = g.m_sWornSlot;
			row.m_bClothing = true;
		}
		else if (g.m_sGarmentSlot != "")
		{
			row.m_sSlot = g.m_sSlotOnGarment;
			row.m_sGarmentSlot = g.m_sGarmentSlot;
		}
		return row;
	}

	//------------------------------------------------------------------------------------------------
	//! The item a bare seat still issues, as cargo: the entry that would have been fitted - the
	//! flagged default, else the first that survived. An entry the legality pass blocked is not
	//! stowed: a bayonet the suppressor keeps off the rifle leaves the kit, so a blocked group
	//! here issues nothing.
	protected static void StowUnfitted(notnull RK29_KitStruct kit, notnull RK29_ResolvedGroup g,
		notnull RK29_KitSetup setup, ResourceName ownerPrefab)
	{
		RK29_ResolvedEntry chosen = g.DefaultEntry();
		if (!chosen)
			chosen = g.FirstUnblockedEntry();
		if (!chosen)
			return;

		RK29_EntryAttachment att = RK29_EntryAttachment.Cast(chosen.m_Def);
		if (!att)
			return;
		RK29_AttachmentDef def = setup.FindAttachmentDef(att.m_sAttachment);
		if (!def || def.m_sPrefab == ResourceName.Empty)
			return;

		RK29_WeaponAmmo row = new RK29_WeaponAmmo();
		row.m_sPrefab = def.m_sPrefab;
		row.m_iCount = 1;

		array<ref RK29_WeaponAmmo> single = {};
		single.Insert(row);
		RK29_KitCompose.EmitAmmo(kit, ownerPrefab, setup.FindWeaponDef(g.m_sOwnerWeapon),
			single, setup);
	}

	//------------------------------------------------------------------------------------------------
	//! The authored id, else one derived from the payload in the order below. An item entry stating
	//! both a prefab and an alias is addressed by the alias while the prefab supplies what is issued.
	static string EntryIdOf(notnull RK29_ChoiceEntryBase def)
	{
		if (def.m_sId != "")
			return def.m_sId;

		RK29_EntryWeapon w = RK29_EntryWeapon.Cast(def);
		if (w)
			return w.m_sWeapon;
		RK29_EntryAttachment a = RK29_EntryAttachment.Cast(def);
		if (a)
			return a.m_sAttachment;
		RK29_EntryItem it = RK29_EntryItem.Cast(def);
		if (it)
		{
			if (it.m_sAlias != "")
				return it.m_sAlias;
			if (it.m_sVariant != "")
				return it.m_sVariant;
			if (it.m_sPrefab != ResourceName.Empty)
				return FilePath.StripPath(it.m_sPrefab);
		}
		return "";
	}

	//============================================================================================
	// Override
	//============================================================================================

	//------------------------------------------------------------------------------------------------
	//! Composition-chain doctrine in list order: adjusts assign rather than merge, so the last
	//! statement wins - which is the one nearest the kit.
	static void ApplyOverrides(RK29_KitComposition comp,
		notnull RK29_KitSetup setup, notnull array<ref RK29_ResolvedGroup> groups)
	{
		array<RK29_Override> overrides = {};
		CollectOverrides(comp, setup, overrides);

		foreach (RK29_Override p : overrides)
		{
			ApplyOverride(p, groups);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void ApplyOverride(notnull RK29_Override p, notnull array<ref RK29_ResolvedGroup> groups)
	{
		if (p.m_aAdjust)
		{
			foreach (RK29_OverrideAdjust adj : p.m_aAdjust)
			{
				if (!adj)
					continue;
				string weaponId, groupId, entryId;
				SplitTarget(adj.m_sTarget, weaponId, groupId, entryId);
				RK29_ResolvedGroup g = FindGroupOwnedBy(groups, weaponId, groupId);
				if (!g)
					continue;

				if (entryId == "")
				{
					if (adj.m_iBudget >= 0)
						g.m_iBudget = adj.m_iBudget;
					continue;
				}

				RK29_ResolvedEntry e = g.FindEntry(entryId);
				if (!e)
					continue;
				if (adj.m_iMin >= 0)
					e.m_iMin = adj.m_iMin;
				if (adj.m_iDefault >= 0)
					e.m_iDefault = adj.m_iDefault;
				if (adj.m_iMax >= 0)
					e.m_iMax = adj.m_iMax;

				// the other flags must be cleared: DefaultEntry returns the first flagged entry, so
				// setting one without clearing the rest is answered by whichever came first
				if (adj.m_bDefault)
				{
					foreach (RK29_ResolvedEntry other : g.m_aEntries)
					{
						if (other)
							other.m_bDefault = false;
					}
					e.m_bDefault = true;
				}
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Four spellings, one grammar: "group", "group/entry", "weapon:group", "weapon:group/entry" -
	//! ':' is the weapon axis, '/' the entry axis, and an empty weaponId means "any weapon". Parsed
	//! weapon-first because a group id may not contain ':' but an entry id might.
	static void SplitTarget(string path, out string weaponId, out string groupId, out string entryId)
	{
		weaponId = "";
		groupId = "";
		entryId = "";

		string rest = path;
		int colon = rest.IndexOf(":");
		if (colon >= 0)
		{
			weaponId = rest.Substring(0, colon);
			rest = rest.Substring(colon + 1, rest.Length() - colon - 1);
		}

		int slash = rest.IndexOf("/");
		if (slash >= 0)
		{
			entryId = rest.Substring(slash + 1, rest.Length() - slash - 1);
			rest = rest.Substring(0, slash);
		}

		groupId = rest;
	}

	//------------------------------------------------------------------------------------------------
	//! FindGroup with the weapon axis honoured; an empty weaponId matches any owner. A target
	//! follows includes: when no offered group carries the id itself, the group that absorbed it
	//! through m_aIncludeGroups answers instead. The group's own id still wins. Public because the
	//! menu's exclusion note must land on the same group EnforceExclusions will block.
	static RK29_ResolvedGroup FindGroupOwnedBy(notnull array<ref RK29_ResolvedGroup> groups,
		string weaponId, string id)
	{
		foreach (RK29_ResolvedGroup g : groups)
		{
			if (!g || g.m_sId != id)
				continue;
			if (weaponId != "" && g.m_sOwnerWeapon != weaponId)
				continue;
			return g;
		}
		foreach (RK29_ResolvedGroup g : groups)
		{
			if (!g || !g.m_aIncludedGroups.Contains(id))
				continue;
			if (weaponId != "" && g.m_sOwnerWeapon != weaponId)
				continue;
			return g;
		}
		return null;
	}

	//============================================================================================
	// Substitution
	//============================================================================================

	//------------------------------------------------------------------------------------------------
	//! The one reader of a composition's override list, in authored order - an inline step and a
	//! reference are ordered against each other by where they sit, not by their kind. Every walk
	//! of a kit's doctrine comes through here, so adjust order (last wins) and substitution order
	//! (first wins) can never disagree about what came first.
	//! Borrowed pointers: the catalog and the composition both outlive the resolve.
	static void CollectOverrides(RK29_KitComposition comp, RK29_KitSetup setup,
		notnull array<RK29_Override> outOverrides)
	{
		if (!comp || !comp.m_aOverrides)
			return;

		foreach (RK29_OverrideStep step : comp.m_aOverrides)
		{
			if (!step)
				continue;

			RK29_Override inlineStep = RK29_Override.Cast(step);
			if (inlineStep)
			{
				outOverrides.Insert(inlineStep);
				continue;
			}

			RK29_OverrideRef byName = RK29_OverrideRef.Cast(step);
			if (!byName || byName.m_sOverride == "" || !setup)
				continue;

			RK29_Override shared = setup.FindOverride(byName.m_sOverride);
			if (shared)
				outOverrides.Insert(shared);
			else
				ComplainOnce(string.Format("[RK29] a kit names override '%1', which is in no override"
					+ " catalog", byName.m_sOverride), LogLevel.ERROR);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Every substitution this composition's overrides state, flattened in authored override order.
	//! Substitutions are first-wins where adjustments are last-wins; both read the one list in the
	//! one order. Borrowed pointers: read, never written, and outliving the resolve.
	protected static void CollectSubstitutions(RK29_KitComposition comp,
		RK29_KitSetup setup, notnull array<RK29_OverrideSubstitute> outSubs)
	{
		array<RK29_Override> overrides = {};
		CollectOverrides(comp, setup, overrides);
		CollectFrom(overrides, outSubs);
	}

	//------------------------------------------------------------------------------------------------
	protected static void CollectFrom(array<RK29_Override> steps,
		notnull array<RK29_OverrideSubstitute> outSubs)
	{
		if (!steps)
			return;

		foreach (RK29_Override step : steps)
		{
			if (!step || !step.m_aSubstitute)
				continue;
			foreach (RK29_OverrideSubstitute sub : step.m_aSubstitute)
			{
				// an empty target would match the ammo group of any weapon whose group id is empty
				if (sub && sub.m_sTarget != "")
					outSubs.Insert(sub);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! True and the id to offer, or FALSE when a substitution removes the reference outright.
	//!
	//! First-wins: the earliest step whose target covers this reference decides. A second substitution over
	//! the same reference, and an entry axis (ignored - a substitution swaps whole groups), are both
	//! named at boot by RK29_KitLint.VerifySubstitutions, never here.
	protected static bool SubstitutedGroupId(notnull array<RK29_OverrideSubstitute> subs,
		string ownerWeapon, string groupId, out string outGroupId)
	{
		outGroupId = groupId;

		foreach (RK29_OverrideSubstitute sub : subs)
		{
			string subWeapon, subGroup, subEntry;
			SplitTarget(sub.m_sTarget, subWeapon, subGroup, subEntry);
			if (subEntry != "")
				continue;
			if (subGroup != groupId)
				continue;
			if (subWeapon != "" && subWeapon != ownerWeapon)
				continue;

			outGroupId = sub.m_sReplaceWith;
			return sub.m_sReplaceWith != "";
		}

		return true;
	}

	//============================================================================================
	// Picks
	//============================================================================================

	//------------------------------------------------------------------------------------------------
	protected static RK29_ChoicePick FindPick(array<ref RK29_ChoicePick> picks, string group)
	{
		if (!picks)
			return null;
		foreach (RK29_ChoicePick p : picks)
		{
			if (p && p.m_sGroup == group)
				return p;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! A pick that deliberately names nothing, as opposed to no pick at all. PickedEntry answers
	//! null for both; only the explicit one is an instruction, which is what lets several groups
	//! share one attachment seat without the unpicked ones clearing it.
	protected static bool HasExplicitBarePick(array<ref RK29_ChoicePick> picks, string group)
	{
		RK29_ChoicePick pick = FindPick(picks, group);
		if (!pick)
			return false;

		return pick.m_sEntry == "";
	}

	//------------------------------------------------------------------------------------------------
	//! EXCLUSIVE resolution: the pick when it names a real entry, deliberate bare when the group
	//! allows it, else the default. A pick naming a removed entry falls back to the default.
	static RK29_ResolvedEntry PickedEntry(notnull RK29_ResolvedGroup group, array<ref RK29_ChoicePick> picks)
	{
		RK29_ChoicePick pick = FindPick(picks, group.m_sId);
		if (pick)
		{
			if (pick.m_sEntry == "" && group.m_bAllowEmpty)
				return null;
			RK29_ResolvedEntry e = group.FindEntry(pick.m_sEntry);
			if (e)
				return e;
			// Trace, not a warning: hot path, and a stale pick is the ordinary consequence of an
			// entry leaving the offer
			RK29_Log.Trace(string.Format("[RK29] pick '%1/%2' is not offered - using the default",
				group.m_sId, pick.m_sEntry));
		}
		return group.DefaultEntry();
	}

	//------------------------------------------------------------------------------------------------
	//! The gun a WEAPON group fields: PickedEntry, except that a pick naming a blocked weapon falls to
	//! the default like any other blocked exclusive pick - the menu greys the row rather than
	//! removing it, so a stored pick can still name it. Null when nothing unblocked is left. Every
	//! weapon-group read (the pull, the claim, the menu's slot map) comes through here, or the ammo
	//! could be pulled for one gun and the body dressed with another.
	static RK29_ResolvedEntry PickedWeaponEntry(notnull RK29_ResolvedGroup group,
		array<ref RK29_ChoicePick> picks)
	{
		RK29_ResolvedEntry chosen = PickedEntry(group, picks);
		if (chosen && chosen.m_bBlocked)
			chosen = group.DefaultEntry();
		if (chosen && chosen.m_bBlocked)
			return null;
		return chosen;
	}

	//------------------------------------------------------------------------------------------------
	//! COUNTED/BUDGETED per-entry count: the pick clamped to the entry's bounds, else the authored
	//! default. The ceiling is CeilingOf - an unset m_iMax implies one, never waives it.
	static int PickedCount(notnull RK29_ResolvedGroup group, notnull RK29_ResolvedEntry entry,
		array<ref RK29_ChoicePick> picks)
	{
		// Blocked is zero, said once here: the row shows nothing carried, the emit issues nothing,
		// and an exclusion keyed on this entry sees nothing to fire on - which stops two exclusions looping
		if (entry.m_bBlocked)
			return 0;

		int count = entry.m_iDefault;

		RK29_ChoicePick pick = null;
		if (picks)
		{
			foreach (RK29_ChoicePick p : picks)
			{
				if (p && p.m_sGroup == group.m_sId && p.m_sEntry == entry.m_sId)
				{
					pick = p;
					break;
				}
			}
		}
		if (pick)
			count = pick.m_iCount;

		if (count < entry.m_iMin)
			count = entry.m_iMin;

		int ceiling = CeilingOf(entry);
		if (count > ceiling)
			count = ceiling;

		if (count < 0)
			count = 0;
		return count;
	}

	//------------------------------------------------------------------------------------------------
	//! The count an entry cannot be stepped below - a negative authored minimum means none.
	static int FloorOf(notnull RK29_ResolvedEntry entry)
	{
		if (entry.m_iMin < 0)
			return 0;
		return entry.m_iMin;
	}

	//------------------------------------------------------------------------------------------------
	//! Three rules in order: an authored m_iMax wins; an entry authoring none gets one implied from
	//! what it carries; the hard ceiling caps both. the one funnel every count passes through - the
	//! menu's steppers and the server's resolve both read it, so the two cannot disagree.
	static int CeilingOf(notnull RK29_ResolvedEntry entry)
	{
		int ceiling = entry.m_iMax;
		if (ceiling < 0)
		{
			int carried = entry.m_iDefault;
			if (entry.m_iMin > carried)
				carried = entry.m_iMin;

			ceiling = carried * COUNT_IMPLIED_FACTOR;
			if (ceiling < COUNT_IMPLIED_FLOOR)
				ceiling = COUNT_IMPLIED_FLOOR;
		}

		if (ceiling > COUNT_HARD_CEILING)
			ceiling = COUNT_HARD_CEILING;

		return ceiling;
	}

	//------------------------------------------------------------------------------------------------
	//! "group=entry:count;group=entry;group=" - empty entry means deliberately bare.
	static string EncodePicks(array<ref RK29_ChoicePick> picks)
	{
		if (!picks)
			return "";
		// One spelling per set of picks: presets are matched by comparing this string, so the same
		// kit reached by a different sequence of clicks must encode the same. Sorted; nothing reads
		// the wire in order
		array<string> parts = {};
		foreach (RK29_ChoicePick p : picks)
		{
			if (!p || p.m_sGroup == "")
				continue;
			string part = p.m_sGroup + "=" + p.m_sEntry;
			if (p.m_iCount != 1)
				part += ":" + p.m_iCount.ToString();
			parts.Insert(part);
		}
		parts.Sort();
		string s = "";
		foreach (string part : parts)
		{
			if (s != "")
				s += ";";
			s += part;
		}
		return s;
	}

	//------------------------------------------------------------------------------------------------
	static void ParsePicks(string wire, notnull array<ref RK29_ChoicePick> outPicks)
	{
		if (wire == "")
			return;

		if (wire.Length() > WIRE_MAX_CHARS)
		{
			Print(string.Format("[RK29] pick wire refused - %1 chars, cap %2", wire.Length(),
				WIRE_MAX_CHARS), LogLevel.WARNING);
			return;
		}

		array<string> parts = {};
		wire.Split(";", parts, true);
		if (parts.Count() > WIRE_MAX_PICKS)
		{
			Print(string.Format("[RK29] pick wire refused - %1 picks, cap %2", parts.Count(),
				WIRE_MAX_PICKS), LogLevel.WARNING);
			return;
		}

		int clamped = 0;
		foreach (string part : parts)
		{
			int eq = part.IndexOf("=");
			if (eq <= 0)
				continue;

			RK29_ChoicePick pick = new RK29_ChoicePick();
			pick.m_sGroup = part.Substring(0, eq);

			string rest = part.Substring(eq + 1, part.Length() - eq - 1);
			int colon = rest.IndexOf(":");
			if (colon >= 0)
			{
				pick.m_sEntry = rest.Substring(0, colon);

				// Bounded at the door: nothing about this string proves it came from the menu. The
				// entry's own ceiling is applied later by PickedCount, which needs the resolved
				// offer; this only refuses the absurd
				int asked = rest.Substring(colon + 1, rest.Length() - colon - 1).ToInt();
				pick.m_iCount = asked;
				if (pick.m_iCount < 0)
					pick.m_iCount = 0;
				if (pick.m_iCount > COUNT_HARD_CEILING)
					pick.m_iCount = COUNT_HARD_CEILING;
				if (asked != pick.m_iCount)
					clamped++;
			}
			else
			{
				pick.m_sEntry = rest;
			}
			outPicks.Insert(pick);
		}

		// one line per wire, not per pick: the content is the client's and is not echoed
		if (clamped > 0)
			Print(string.Format("[RK29] %1 pick count(s) out of range - held to bounds", clamped),
				LogLevel.WARNING);
	}

	//============================================================================================
	// Apply
	//============================================================================================

	//------------------------------------------------------------------------------------------------
	//! Lays the offer's resolution over a composed kit. Weapons first (their prefabs decide their
	//! body slots), then item counts, then one attachment order per answered attachment group.
	//!
	//! outLoadedMags: weapon slot index -> the rounds that start in that gun. A list per slot,
	//! because one weapon can seat two (a magazine in the rifle's well and a grenade in its
	//! underbarrel). An empty prefab is the order to empty that muzzle.
	//!
	//! outAttachmentOrders is one channel for everything that goes on a gun - mounts, optics and
	//! Nones alike - keyed on the owner's weapon slot, the same key space outLoadedMags uses.
	//!
	//! Every order carries a seat probe, never mounted and never carried: FindSeatFor answers which
	//! seat this group's contents would take, which is what still points at a seat on a None. A
	//! prefab, not a slot-type string, because one string cannot carry both a group's seat and its
	//! optics identity - the SMAW's MBS sits in RHS's AttachmentMBS, which descends from no optics
	//! class, so a group declaring "AttachmentOptics" could never empty it.
	//!
	//! outPreviewOptic is for the client preview alone - the primary's optics group and nothing
	//! else. Empty means irons.
	static void Apply(notnull RK29_KitStruct kit, notnull array<ref RK29_ResolvedGroup> groups,
		array<ref RK29_ChoicePick> picks, notnull RK29_KitSetup setup,
		notnull map<int, ref array<ref RK29_LoadedPick>> outLoadedMags,
		out array<ref RK29_AttachmentOrder> outAttachmentOrders, out ResourceName outPreviewOptic)
	{
		outAttachmentOrders = {};
		outPreviewOptic = ResourceName.Empty;

		map<string, ResourceName> weaponPrefabs = new map<string, ResourceName>();
		map<string, int> weaponSlots = new map<string, int>();

		ClaimWeaponSlots(kit, groups, picks, setup, weaponPrefabs, weaponSlots);

		foreach (RK29_ResolvedGroup g : groups)
		{
			if (!g || g.IsWeaponGroup())
				continue;

			// a group owned by a weapon that did not make it into the kit offers nothing
			ResourceName ownerPrefab;
			if (g.m_sOwnerWeapon != "" && !weaponPrefabs.Find(g.m_sOwnerWeapon, ownerPrefab))
				continue;

			// the loaded magazine is neither cargo nor an attachment: no batches, its own channel
			if (g.m_bLoaded)
			{
				ApplyLoadedGroup(kit, g, groups, picks, setup, ownerPrefab, weaponSlots, outLoadedMags);
				continue;
			}

			if (g.IsAttachmentGroup())
			{
				// several groups can speak for one gun's seat (a 1x tier and a magnified tier), so
				// they are read as competing answers: a tier naming an attachment takes the seat, a
				// tier deliberately set to None empties it, and a tier with no pick at all must say
				// nothing - otherwise the tier listed last silently unseats the one before it.
				RK29_AttachmentOrder order = AttachmentOrderFor(kit, g, picks, setup, ownerPrefab, weaponSlots);
				if (order)
				{
					outAttachmentOrders.Insert(order);

					// a named answer wins: each optics tier over one seat emits an order, and a
					// tier answering None says nothing about what another tier mounted. Mirrors the
					// apply pass, which runs every clear before any mount
					if (g.m_bIsOpticsPoint && order.m_iOwnerSlot == 0
						&& order.m_sPrefab != ResourceName.Empty)
						outPreviewOptic = order.m_sPrefab;
				}

				// nothing fitted, but the kit still carries it. No capability check on purpose: a
				// weapon that cannot seat this at all lost the whole group to PruneUnmountable, and
				// one that merely cannot fit it right now should still be given it to stow
				if (g.m_bCarryWhenUnfitted && (!order || order.m_sPrefab == ResourceName.Empty))
					StowUnfitted(kit, g, setup, ownerPrefab);
				continue;
			}
			ApplyItemGroup(kit, g, groups, picks, setup, ownerPrefab);
		}

		ClearUnfedMuzzles(groups, weaponPrefabs, weaponSlots, outLoadedMags);

		// Nothing is deducted from the kit's cargo for a mounted attachment: no kit authors an
		// attachment as cargo, so the attachment group is the only source of one.
	}

	//------------------------------------------------------------------------------------------------
	//! Every weapon group's pick into a body slot, plus the two maps the rest of Apply binds by:
	//! weapon id -> prefab, weapon id -> claimed slot. Also dresses the kit (m_mWeapons, the primary,
	//! the chosen weapon's blocks), which is why it is not a call to BuildWeaponSlotMap - see the
	//! note there.
	protected static void ClaimWeaponSlots(notnull RK29_KitStruct kit,
		notnull array<ref RK29_ResolvedGroup> groups, array<ref RK29_ChoicePick> picks,
		notnull RK29_KitSetup setup, notnull map<string, ResourceName> outWeaponPrefabs,
		notnull map<string, int> outWeaponSlots)
	{
		array<int> claimed = {};

		foreach (RK29_ResolvedGroup wg : groups)
		{
			if (!wg || !wg.IsWeaponGroup())
				continue;

			RK29_ResolvedEntry chosen = PickedWeaponEntry(wg, picks);
			if (!chosen)
				continue;
			RK29_EntryWeapon weaponEntry = RK29_EntryWeapon.Cast(chosen.m_Def);
			if (!weaponEntry)
				continue;

			ResourceName prefab = WeaponPrefabOfId(setup, weaponEntry.m_sWeapon, kit.m_sFactionKey);
			if (prefab == ResourceName.Empty)
				continue;

			int slot = ClaimSlot(prefab, claimed);
			if (slot < 0)
			{
				Print(string.Format("[RK29] config ERROR - no free weapon slot for '%1' (%2)",
					weaponEntry.m_sWeapon, kit.m_sKitName), LogLevel.ERROR);
				continue;
			}

			kit.m_mWeapons.Set(slot, prefab);
			if (slot == 0)
				kit.m_sPrimaryWeapon = prefab;
			outWeaponPrefabs.Set(weaponEntry.m_sWeapon, prefab);

			// First claim wins: two groups may offer the same weapon id and each claim a slot, but
			// everything binding by weapon id can only mean one of them - letting the second
			// overwrite would move that gun's orders and loaded rounds onto the other body slot
			if (outWeaponSlots.Contains(weaponEntry.m_sWeapon))
			{
				Print(string.Format("[RK29] weapon id '%1' is claimed by more than one group"
					+ " (%2) - attachment orders and loaded-round picks bind to the first claim. The"
					+ " second claim is in no weaponSlots entry, so ClearUnfedMuzzles never clears that"
					+ " gun's chamber and AttachmentOrderFor drops its orders",
					weaponEntry.m_sWeapon, kit.m_sKitName), LogLevel.WARNING);
			}
			else
			{
				outWeaponSlots.Set(weaponEntry.m_sWeapon, slot);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The empty-chamber guarantee, per weapon slot: a gun whose offer names no ammunition gets an
	//! empty-prefab clearing pick on its own slot. A weapon prefab arrives with its authored
	//! MagazineTemplate seated, so silence is not "no magazine" but "the prefab's magazine" (the
	//! parade M21 spawned chambered without this).
	//!
	//! Coverage is a group speaking for the slot, not an answer arriving from one: a weapon owning
	//! an item group that chambers in its own muzzle (m_eLoadedSeat, derived from the metal) is
	//! left alone whatever that group resolves to - a picked row at zero is an authored empty
	//! chamber, handled there. A group that chambers nothing (an accessory pool) or only in the
	//! underbarrel (a UGL-only pool) does not cover: the pick emitted here empties the gun's own
	//! muzzle and nothing else, so only a pool feeding that muzzle can stand in for it. A gun with
	//! no magazine well at all is never cleared - the M72's rocket is not a magazine in a well
	//! (SCR_MuzzleInMagComponent) and must not be touched; a prefab that cannot be read is NOT
	//! taken as well-less, the guarantee holds and the clear runs.
	protected static void ClearUnfedMuzzles(notnull array<ref RK29_ResolvedGroup> groups,
		notnull map<string, ResourceName> weaponPrefabs, notnull map<string, int> weaponSlots,
		notnull map<int, ref array<ref RK29_LoadedPick>> outLoadedMags)
	{
		array<int> coveredSlots = {};
		foreach (RK29_ResolvedGroup g : groups)
		{
			// an ownerless group is a kit-level statement and speaks for no gun's chamber
			if (!g || g.m_sOwnerWeapon == "")
				continue;
			// weapons and attachments are not ammunition; the "<id>_loaded" selector covers its
			// owner exactly as the totals group it stands over does
			if (g.IsWeaponGroup() || g.IsAttachmentGroup())
				continue;
			if (g.m_eLoadedSeat != RK29_ELoadedSeat.OWN_MUZZLE)
				continue;

			int coveredSlot = 0;
			if (!weaponSlots.Find(g.m_sOwnerWeapon, coveredSlot))
				continue;
			if (!coveredSlots.Contains(coveredSlot))
				coveredSlots.Insert(coveredSlot);
		}

		foreach (string weaponId, int slot : weaponSlots)
		{
			if (coveredSlots.Contains(slot))
				continue;

			ResourceName prefab;
			if (weaponPrefabs.Find(weaponId, prefab) && RK29_KitCompose.PresentsNoWell(prefab))
				continue;

			// an empty prefab and the gun's own muzzle: both fields already are that
			RK29_LoadedPick clear = new RK29_LoadedPick();

			array<ref RK29_LoadedPick> forSlot = outLoadedMags.Get(slot);
			if (!forSlot)
			{
				forSlot = {};
				outLoadedMags.Set(slot, forSlot);
			}
			forSlot.Insert(clear);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One attachment group's answer as an order, or NULL when the group did not answer. Answering
	//! is a named attachment the owner can mount, or a deliberate None (HasExplicitBarePick).
	//! Everything else says nothing, so groups sharing a seat cannot unseat each other.
	//!
	//! Bound to the owner's weapon slot; a group with no owner means the primary.
	protected static RK29_AttachmentOrder AttachmentOrderFor(notnull RK29_KitStruct kit,
		notnull RK29_ResolvedGroup g, array<ref RK29_ChoicePick> picks, notnull RK29_KitSetup setup,
		ResourceName ownerPrefab, notnull map<string, int> weaponSlots)
	{
		ResourceName prefab = ResolveAttachmentChoice(kit, g, picks, setup, ownerPrefab);
		if (prefab == ResourceName.Empty && !HasExplicitBarePick(picks, g.m_sId))
			return null;

		ResourceName probe = prefab;
		if (probe == ResourceName.Empty)
			probe = SeatProbeOf(g, setup);
		if (probe == ResourceName.Empty)
			return null;

		int slot = 0;
		if (g.m_sOwnerWeapon != "")
		{
			if (!weaponSlots.Find(g.m_sOwnerWeapon, slot))
				return null;
		}
		else if (!AnyWeaponClaimedSlot(weaponSlots, 0))
		{
			// no weapon in slot 0, so there is no gun for this kit-level statement to speak about;
			// bound anyway it would aim at whatever the apply pass happens to seat there
			ComplainOnce(string.Format("[RK29] ownerless attachment group '%1': no primary claimed"
				+ " slot 0 - dropping (%2)", g.m_sId, kit.m_sKitName), LogLevel.WARNING);
			return null;
		}

		RK29_AttachmentOrder order = new RK29_AttachmentOrder();
		order.m_sOwnerWeaponId = g.m_sOwnerWeapon;
		order.m_iOwnerSlot = slot;
		order.m_sPrefab = prefab;
		order.m_sProbe = probe;
		return order;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool AnyWeaponClaimedSlot(notnull map<string, int> weaponSlots, int slot)
	{
		for (int i = 0, n = weaponSlots.Count(); i < n; i++)
		{
			if (weaponSlots.GetElement(i) == slot)
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! The chosen round for one weapon, against that weapon's body slot. Emits nothing into the kit
	//! - ApplyItemGroup has already taken this round off the ammo group's count.
	//!
	//! A mark on a row the kit carries NONE of is an authored empty chamber: the pick goes out with
	//! an empty prefab, since emitting no pick at all means "leave the prefab default". Appended,
	//! not assigned: an M203 rifle answers two loaded groups on the same weapon slot.
	protected static void ApplyLoadedGroup(notnull RK29_KitStruct kit, notnull RK29_ResolvedGroup g,
		notnull array<ref RK29_ResolvedGroup> groups, array<ref RK29_ChoicePick> picks,
		notnull RK29_KitSetup setup, ResourceName ownerPrefab,
		notnull map<string, int> weaponSlots, notnull map<int, ref array<ref RK29_LoadedPick>> outLoadedMags)
	{
		int slot;
		if (!weaponSlots.Find(g.m_sOwnerWeapon, slot))
			return;

		// PickedEntry ends in the group's default and a synthesized selector never allows a bare
		// answer, so null means the selector has nothing left to chamber
		RK29_ResolvedEntry chosen = PickedEntry(g, picks);
		if (!chosen)
			return;

		RK29_EntryItem item = RK29_EntryItem.Cast(chosen.m_Def);
		if (!item)
			return;

		// The seat is asked of the round this entry names, whether or not any are carried: an empty
		// pick still has to name the muzzle it empties, and with no round to ask the seat fell to
		// none, sending a zero-count grenade pick at the rifle's own well instead of the launcher.
		// The group's own seat is the fallback for an entry naming no round at all.
		ResourceName round = ResolveItemPrefabFor(item, ownerPrefab,
			setup.FindWeaponDef(g.m_sOwnerWeapon), kit.m_sFactionKey, setup);
		RK29_ELoadedSeat seat = RK29_KitCompose.DeriveLoadedSeat(ownerPrefab, round);
		if (seat == RK29_ELoadedSeat.NONE)
			seat = g.m_eLoadedSeat;

		ResourceName prefab = ResourceName.Empty;
		if (CarriedCountOfLoaded(groups, g, chosen, picks) > 0)
		{
			prefab = round;
			// asked for but unresolvable - a config fault, so nothing is ordered and the weapon
			// keeps whatever its prefab spawned with
			if (prefab == ResourceName.Empty)
			{
				ComplainOnce(string.Format("[RK29] loaded magazine '%1' did not resolve for"
					+ " '%2' (%3)", chosen.m_sId, g.m_sOwnerWeapon, kit.m_sKitName),
					LogLevel.WARNING);
				return;
			}
		}
		else
		{
			// nothing chambers here, so there is no muzzle to empty and no order to give
			if (seat == RK29_ELoadedSeat.NONE)
				return;

			RK29_Log.Trace(string.Format("[RK29] '%1' is carried zero times - '%2' starts empty (%3)",
				chosen.m_sId, g.m_sOwnerWeapon, kit.m_sKitName));
		}

		RK29_LoadedPick decided = new RK29_LoadedPick();
		decided.m_sPrefab = prefab;
		decided.m_bUnderbarrel = seat == RK29_ELoadedSeat.UNDERBARREL;

		array<ref RK29_LoadedPick> forSlot = outLoadedMags.Get(slot);
		if (!forSlot)
		{
			forSlot = {};
			outLoadedMags.Set(slot, forSlot);
		}
		forSlot.Insert(decided);
	}

	//------------------------------------------------------------------------------------------------
	//! One item entry's prefab, in the order RK29_KitCompose.EmitAmmo resolves an ammo row: literal
	//! prefab, else alias - the owning weapon's own ammo table first, the faction alias catalog only
	//! where that table does not name it - else magazine variant through the weapon's well, else the
	//! weapon's default magazine. An alias the weapon's table does name never reaches the catalog:
	//! that row's own prefab, variant or default is the answer. Takes a faction key rather than a
	//! composed kit, so the menu can ask before anything is composed.
	//!
	//! A second statement of the tail RK29_KitCompose.RoundFrom owns, kept apart because this one
	//! must stay silent about what it cannot resolve while the emit path logs a config error.
	static ResourceName ResolveItemPrefabFor(RK29_EntryItem item, ResourceName ownerPrefab,
		RK29_WeaponDef ownerDef, string factionKey, notnull RK29_KitSetup setup)
	{
		if (!item)
			return ResourceName.Empty;

		if (item.m_sPrefab != ResourceName.Empty)
			return item.m_sPrefab;

		if (item.m_sAlias != "")
		{
			if (ownerDef && ownerDef.m_aAmmo)
			{
				foreach (RK29_WeaponAmmoDef declared : ownerDef.m_aAmmo)
				{
					if (!declared || declared.m_sAlias != item.m_sAlias)
						continue;
					if (declared.m_sPrefab != ResourceName.Empty)
						return declared.m_sPrefab;
					if (declared.m_sVariant != "")
						return setup.FindMagVariant(RK29_KitCompose.WellsOf(ownerPrefab), declared.m_sVariant);
					return RK29_KitCompose.DefaultMagOf(ownerPrefab);
				}
			}
			return setup.ResolveAlias(item.m_sAlias, factionKey);
		}

		if (item.m_sVariant != "")
			return setup.FindMagVariant(RK29_KitCompose.WellsOf(ownerPrefab), item.m_sVariant);

		return RK29_KitCompose.DefaultMagOf(ownerPrefab);
	}

	//------------------------------------------------------------------------------------------------
	//! What a group's picks cost against its budget - the one sum both sides use: the menu greys
	//! the plus button on it, and ResolveCounts resets the group to defaults when it overruns.
	static int SpendOf(notnull RK29_ResolvedGroup g, array<ref RK29_ChoicePick> picks)
	{
		int spend = 0;
		foreach (RK29_ResolvedEntry e : g.m_aEntries)
		{
			if (e)
				spend += PickedCount(g, e, picks) * e.m_iCost;
		}
		return spend;
	}

	//------------------------------------------------------------------------------------------------
	//! Would this spend overrun the group's budget? False for anything that is not BUDGETED.
	static bool OverBudget(notnull RK29_ResolvedGroup g, int spend)
	{
		return g.m_eKind == RK29_EChoiceKind.BUDGETED && g.m_iBudget > 0 && spend > g.m_iBudget;
	}

	//------------------------------------------------------------------------------------------------
	//! Per-entry counts, index-aligned to g.m_aEntries: each pick clamped to its entry's bounds, or
	//! the authored defaults wholesale when a BUDGETED group's picks overspend. Shared so the ammo
	//! group and its loaded-round selector cannot disagree about how many of a row is carried.
	//! outOverspend is what the picks would have spent, 0 when within budget - reported rather than
	//! logged because the same group is resolved twice per apply.
	protected static void ResolveCounts(notnull RK29_ResolvedGroup g, array<ref RK29_ChoicePick> picks,
		notnull array<int> outCounts, out int outOverspend)
	{
		outCounts.Clear();
		outOverspend = 0;

		foreach (RK29_ResolvedEntry e : g.m_aEntries)
		{
			int c = 0;
			if (e)
				c = PickedCount(g, e, picks);
			outCounts.Insert(c);
		}

		int spend = SpendOf(g, picks);
		if (!OverBudget(g, spend))
			return;

		outOverspend = spend;

		// through PickedCount with no picks, not the raw m_iDefault: the fallback must still honour
		// blocked-is-zero and the ceiling, or an exclusion-blocked row would be issued anyway
		outCounts.Clear();
		foreach (RK29_ResolvedEntry d : g.m_aEntries)
		{
			if (d)
				outCounts.Insert(PickedCount(g, d, null));
			else
				outCounts.Insert(0);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! How many of the chambered row the kit carries, off whichever ammo group on this chamber
	//! offers that row. Zero means an empty chamber. Answers 1 when no group states a total for the
	//! row - nothing says the round is not carried, so it seats.
	protected static int CarriedCountOfLoaded(notnull array<ref RK29_ResolvedGroup> groups,
		notnull RK29_ResolvedGroup loadedGroup, notnull RK29_ResolvedEntry chosen,
		array<ref RK29_ChoicePick> picks)
	{
		foreach (RK29_ResolvedGroup g : groups)
		{
			if (!g || LoadedSiblingOf(groups, g) != loadedGroup)
				continue;

			array<int> counts = {};
			int overspend;
			ResolveCounts(g, picks, counts, overspend);
			foreach (int idx, RK29_ResolvedEntry e : g.m_aEntries)
			{
				if (e && e.m_sId == chosen.m_sId)
					return counts[idx];
			}
		}
		return 1;
	}

	//------------------------------------------------------------------------------------------------
	//! One item group -> batches, through EmitAmmo so alias/variant/prefab resolution and container
	//! preferences stay one code path. Two shapes: an EXCLUSIVE item group is answered with one
	//! entry, and the counted path would emit nothing for it (an exclusive entry authors no default,
	//! so every count resolves to zero).
	protected static void ApplyItemGroup(notnull RK29_KitStruct kit, notnull RK29_ResolvedGroup g,
		notnull array<ref RK29_ResolvedGroup> groups, array<ref RK29_ChoicePick> picks,
		notnull RK29_KitSetup setup, ResourceName ownerPrefab)
	{
		if (g.m_eKind == RK29_EChoiceKind.EXCLUSIVE)
			ApplyExclusiveItemGroup(kit, g, picks, setup, ownerPrefab);
		else
			ApplyCountedItemGroup(kit, g, groups, picks, setup, ownerPrefab);
	}

	//------------------------------------------------------------------------------------------------
	protected static void ApplyExclusiveItemGroup(notnull RK29_KitStruct kit,
		notnull RK29_ResolvedGroup g, array<ref RK29_ChoicePick> picks, notnull RK29_KitSetup setup,
		ResourceName ownerPrefab)
	{
		RK29_ResolvedEntry chosen = ExclusiveAnswer(g, picks);
		if (!chosen)
		{
			// None on a worn slot is a decision, not an absence: the slot is cleared, or the body
			// prefab's own garment would stay on under a picker that says None
			if (g.m_sWornSlot != "")
				kit.m_mClothing.Remove(g.m_sWornSlot);
			return;
		}

		RK29_WeaponAmmo one = RowFor(chosen, 1, g);
		if (!one)
			return;

		array<ref RK29_WeaponAmmo> single = {};
		single.Insert(one);
		RK29_KitCompose.EmitAmmo(kit, ownerPrefab, setup.FindWeaponDef(g.m_sOwnerWeapon),
			single, setup);
	}

	//------------------------------------------------------------------------------------------------
	//! A COUNTED or BUDGETED group's per-entry counts, issued as batches. Takes the whole offer
	//! because a group with a loaded-magazine sibling states totals: the seated magazine comes off
	//! the cargo count here, when one is seated at all.
	protected static void ApplyCountedItemGroup(notnull RK29_KitStruct kit,
		notnull RK29_ResolvedGroup g, notnull array<ref RK29_ResolvedGroup> groups,
		array<ref RK29_ChoicePick> picks, notnull RK29_KitSetup setup, ResourceName ownerPrefab)
	{
		array<int> counts = {};
		int overspend;
		ResolveCounts(g, picks, counts, overspend);
		if (overspend > 0)
			Print(string.Format("[RK29] picks for '%1' spend %2/%3 - using the defaults (%4)",
				g.m_sId, overspend, g.m_iBudget, kit.m_sKitName), LogLevel.WARNING);

		// the magazine in the gun is spent from this group, never added on top: a kit that says
		// seven magazines fields seven, one of them loaded. Read after the budget fallback inside
		// ResolveCounts, so the player is held to the total they asked for. The selector is shared
		// by every pool on this chamber; the loop below deducts only when the seated row is ours,
		// so one round leaves one pool
		RK29_ResolvedGroup loadedGroup = LoadedSiblingOf(groups, g);
		if (loadedGroup)
		{
			RK29_ResolvedEntry seated = PickedEntry(loadedGroup, picks);
			if (seated)
			{
				foreach (int seatedIdx, RK29_ResolvedEntry candidate : g.m_aEntries)
				{
					if (!candidate || candidate.m_sId != seated.m_sId)
						continue;
					// a row the player took none of gives up nothing - an authored empty chamber -
					// and the guard also keeps the count off -1 with no spares to spend
					if (counts[seatedIdx] > 0)
						counts[seatedIdx] = counts[seatedIdx] - 1;
					break;
				}
			}
		}

		RK29_WeaponDef ownerDef = setup.FindWeaponDef(g.m_sOwnerWeapon);
		array<ref RK29_WeaponAmmo> ammo = {};
		foreach (int idx, RK29_ResolvedEntry entry : g.m_aEntries)
		{
			if (!entry || counts[idx] <= 0)
				continue;
			RK29_WeaponAmmo row = RowFor(entry, counts[idx], g);
			if (row)
				ammo.Insert(row);
		}

		if (!ammo.IsEmpty())
			RK29_KitCompose.EmitAmmo(kit, ownerPrefab, ownerDef, ammo, setup);
	}

	//------------------------------------------------------------------------------------------------
	//! The prefab one attachment group answers with, gated by what the group's own weapon accepts;
	//! an ownerless group gates against the primary. Empty for a deliberate None, an unresolvable
	//! pick, and anything the gun refuses alike - the caller cannot tell those apart.
	protected static ResourceName ResolveAttachmentChoice(notnull RK29_KitStruct kit,
		notnull RK29_ResolvedGroup g, array<ref RK29_ChoicePick> picks,
		notnull RK29_KitSetup setup, ResourceName ownerPrefab)
	{
		// the menu greys a blocked row rather than removing it, so a stale pick can still name it.
		// It falls to the default, as ApplyExclusiveItemGroup's does: the menu drops the pick and
		// previews the default, so answering None here would clear a seat the player saw filled
		RK29_ResolvedEntry chosen = PickedEntry(g, picks);
		if (chosen && chosen.m_bBlocked)
			chosen = g.DefaultEntry();
		if (!chosen || chosen.m_bBlocked)
			return ResourceName.Empty;

		RK29_EntryAttachment att = RK29_EntryAttachment.Cast(chosen.m_Def);
		if (!att)
			return ResourceName.Empty;

		RK29_AttachmentDef def = setup.FindAttachmentDef(att.m_sAttachment);
		if (!def || def.m_sPrefab == ResourceName.Empty)
		{
			Print(string.Format("[RK29] config ERROR - attachment '%1' has no catalog prefab"
				+ " (%2)", att.m_sAttachment, kit.m_sKitName), LogLevel.ERROR);
			return ResourceName.Empty;
		}

		ResourceName gun = ownerPrefab;
		if (gun == ResourceName.Empty)
			gun = kit.m_sPrimaryWeapon;

		// WeaponRejectsAttachment answers all three refusals: no seat, obstructed, prerequisite
		// missing
		if (RK29_KitCompose.WeaponRejectsAttachment(gun, def.m_sPrefab))
		{
			Print(string.Format("[RK29] attachment '%1' dropped - %2 cannot mount it (%3)",
				att.m_sAttachment, RK29_ItemNames.Get(gun), kit.m_sKitName), LogLevel.WARNING);
			return ResourceName.Empty;
		}

		return def.m_sPrefab;
	}

	//------------------------------------------------------------------------------------------------
	//! A prefab standing for this group's seat rather than for any answer to it: the first entry
	//! whose attachment definition resolves. Never mounted or carried - the apply pass asks
	//! FindSeatFor where it would go. Only a None needs it; a named attachment is its own probe.
	//!
	//! A prefab and not a declared slot-type string, because a string can be left empty, misspelt or
	//! pointed at the wrong family (smaw_optics did exactly that). Empty when the group offers no
	//! resolvable attachment, which callers read as "no seat to speak of".
	protected static ResourceName SeatProbeOf(notnull RK29_ResolvedGroup g, notnull RK29_KitSetup setup)
	{
		foreach (RK29_ResolvedEntry e : g.m_aEntries)
		{
			if (!e)
				continue;
			RK29_EntryAttachment att = RK29_EntryAttachment.Cast(e.m_Def);
			if (!att)
				continue;

			RK29_AttachmentDef def = setup.FindAttachmentDef(att.m_sAttachment);
			if (def && def.m_sPrefab != ResourceName.Empty)
				return def.m_sPrefab;
		}
		return ResourceName.Empty;
	}

	//============================================================================================
	// Weapon slots
	//============================================================================================

	//------------------------------------------------------------------------------------------------
	static ResourceName WeaponPrefabOfId(notnull RK29_KitSetup setup, string id, string factionKey)
	{
		RK29_WeaponDef def = setup.FindWeaponDef(id);
		if (!def)
		{
			// ComplainOnce, not Print: the menu runs this per weapon group on every pick
			ComplainOnce(string.Format("[RK29] config ERROR - weapon id '%1' is not in the weapon"
				+ " catalog", id), LogLevel.ERROR);
			return ResourceName.Empty;
		}
		if (def.m_aPerFaction)
		{
			foreach (RK29_WeaponFactionPrefab entry : def.m_aPerFaction)
			{
				if (entry && entry.m_sFactionKey == factionKey)
					return entry.m_sPrefab;
			}
		}
		return def.m_sPrefab;
	}

	//------------------------------------------------------------------------------------------------
	//! Which body slot each weapon group claims, keyed by group id - the same walk ClaimWeaponSlots
	//! makes, for a caller that wants the answer without the kit. Deliberately not folded into it:
	//! that one also dresses the kit, keys by weapon id and logs, while this runs on every keystroke
	//! and must stay silent. outWeaponIdSlots optionally returns weapon id -> slot, first claim wins
	//! as Apply records it. A group is absent wherever Apply would have skipped it.
	static void BuildWeaponSlotMap(notnull array<ref RK29_ResolvedGroup> groups,
		array<ref RK29_ChoicePick> picks, notnull RK29_KitSetup setup, string factionKey,
		notnull map<string, int> outSlots, map<string, int> outWeaponIdSlots = null)
	{
		array<int> claimed = {};

		foreach (RK29_ResolvedGroup g : groups)
		{
			if (!g || !g.IsWeaponGroup())
				continue;

			RK29_ResolvedEntry chosen = PickedWeaponEntry(g, picks);
			if (!chosen)
				continue;

			RK29_EntryWeapon weaponEntry = RK29_EntryWeapon.Cast(chosen.m_Def);
			if (!weaponEntry)
				continue;

			ResourceName prefab = WeaponPrefabOfId(setup, weaponEntry.m_sWeapon, factionKey);
			if (prefab == ResourceName.Empty)
				continue;

			int slot = ClaimSlot(prefab, claimed);
			if (slot < 0)
				continue;

			outSlots.Set(g.m_sId, slot);

			if (outWeaponIdSlots && !outWeaponIdSlots.Contains(weaponEntry.m_sWeapon))
				outWeaponIdSlots.Set(weaponEntry.m_sWeapon, slot);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The body slot a weapon dresses, from its merged WeaponSlotType: "secondary" is the sidearm
	//! slot, "primary" takes the first unclaimed rifle slot (0 then 1) in offer order. Returns -1
	//! when there is nowhere to go.
	//!
	//! A throwable is not a weapon entry - grenades are issued through item groups and primed into
	//! the throwable slot by the apply pass. A throwable weapon definition claiming the grenade slot
	//! here got spawned a second time on top of the primed one; refused with a config error now.
	protected static int ClaimSlot(ResourceName weapon, notnull array<int> claimed)
	{
		string slotType = SlotTypeOf(weapon);

		if (slotType == "secondary")
		{
			if (claimed.Contains(2))
				return -1;
			claimed.Insert(2);
			return 2;
		}
		if (slotType == "grenade" || slotType == "throwable")
		{
			// the message carries the weapon, so ComplainOnce's ledger keys per weapon
			ComplainOnce(string.Format("[RK29] config ERROR - weapon definition '%1' is a throwable;"
				+ " grenades are item groups, not weapon entries - it is not equipped",
				weapon), LogLevel.ERROR);
			return -1;
		}

		if (!claimed.Contains(0))
		{
			claimed.Insert(0);
			return 0;
		}
		if (!claimed.Contains(1))
		{
			claimed.Insert(1);
			return 1;
		}
		return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! Merged WeaponSlotType off the weapon prefab (Weapon_Base authors "primary", handguns
	//! "secondary"). One read per prefab per session.
	protected static string SlotTypeOf(ResourceName weapon)
	{
		string cached;
		if (s_mSlotTypeCache.Find(weapon, cached))
			return cached;

		string slotType = "primary";
		Resource res = Resource.Load(weapon);
		if (res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			if (src)
			{
				for (int i = 0, n = src.GetComponentCount(); i < n; i++)
				{
					IEntityComponentSource comp = src.GetComponent(i);
					if (!comp || comp.GetClassName() != "WeaponComponent")
						continue;
					string authored;
					if (comp.Get("WeaponSlotType", authored) && authored != "")
						slotType = authored;
					break;
				}
			}
		}

		s_mSlotTypeCache.Set(weapon, slotType);
		return slotType;
	}
}
