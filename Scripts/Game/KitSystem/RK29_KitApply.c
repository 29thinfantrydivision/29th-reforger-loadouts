//------------------------------------------------------------------------------------------------
//! In-place strip and re-dress of a character. Fails soft per item.
//! Apply_S is the server's - a living player's soldier, with perceived faction and role traits -
//! and refuses to run elsewhere. Place is the placement alone and runs anywhere: the F4
//! preview mannequin dresses through it so its weight can be read off the body.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! What one attachment order is arriving into - built only by RK29_KitApply.DecideSeat. No seat
//! means the weapon has nowhere for this attachment, and every field below is then null or empty.
//! Says nothing about "already satisfied": both passes ask RK29_KitApply.HasAttachment first.
class RK29_SeatDecision
{
	InventoryStorageSlot m_Seat;
	//! null for an empty seat
	IEntity m_Occupant;
	//! what goes back on when a replacement turns out not to fit
	ResourceName m_sOccupant;
}

//------------------------------------------------------------------------------------------------
//! One placement solve's working state - index-aligned columns of one table, so the per-item steps
//! take one argument rather than a dozen arrays that could be sorted apart.
class RK29_PlacementState
{
	ref array<BaseInventoryStorageComponent> m_aContainers = {};
	ref array<int> m_aSlotIds = {};
	ref array<string> m_aKeys = {};
	ref array<string> m_aKinds = {};
	ref array<ResourceName> m_aItems = {};
	ref array<ref array<string>> m_aPrefs = {};
	ref array<int> m_aRanks = {};
	ref array<ref array<int>> m_aEligible = {};
	ref array<bool> m_aPlaced = {};
	ref array<int> m_aHome = {};
	ref array<IEntity> m_aSpawned = {};
	//! the order items actually went in, so "placed last" means that and not "declared last"
	ref array<int> m_aSeq = {};
	int m_iNextSeq;
	ref map<ResourceName, int> m_mStackHome = new map<ResourceName, int>();
	//! items that gave way to something more important, tried once more at the end
	ref array<int> m_aDisplaced = {};
}

//------------------------------------------------------------------------------------------------
class RK29_KitApply
{
	//! Above this many of one item, "keep the stack together" stops outranking where the item is
	//! supposed to live. Ammo is meant to be distributed; consumables are not.
	protected static const int COHESION_MAX_STACK = 3;

	//! PreferenceRank of a container the preference list never named: after every listed one, still
	//! usable.
	protected static const int PREF_UNLISTED = 99;

	//! How far both attachment walks descend into nested storages; slack, not a design.
	protected static const int ATTACHMENT_WALK_DEPTH = 3;

	//! What a container hangs on, said the way a preference list is authored ("bandages in the
	//! uniform"). Wire values - the .conf preference lists spell these strings out.
	protected static const string KIND_RIG = "rig";
	protected static const string KIND_PACK = "pack";
	protected static const string KIND_TROUSER = "trouser";
	protected static const string KIND_UNIFORM = "uniform";
	protected static const string KIND_OTHER = "other";

	//! Whether the running placement pass is a preview one and should say nothing. Written at the
	//! top of Place and never restored: Place is synchronous and every caller states its own `quiet`.
	//! Only code reached from inside Place may narrate through Note. A helper callable from outside
	//! it - SeatFor, DecideSeatAt, HasAttachment, OpticsSeatByType - would read whatever the last
	//! pass left here, so those must Print or RK29_Log.Trace, never Note.
	protected static bool s_bQuiet;

	//! Largest authored side per prefab, read once - see ItemMaxDimension. Session-scoped: cleared
	//! at world start with the other prefab caches, or a Workbench edit to an item's dimensions
	//! would place by last session's size.
	protected static ref map<ResourceName, float> s_mDimCache = new map<ResourceName, float>();

	//------------------------------------------------------------------------------------------------
	static void ClearCaches()
	{
		s_mDimCache.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! Re-dress character as kit, then run the attachment orders. False only on hard failure.
	//!
	//! `loadedMags` (weapon slot index -> picks) is what each gun starts loaded with; null leaves
	//! every weapon on its prefab-authored magazine. A pick with an empty prefab is the order to
	//! empty that muzzle - without it the authored magazine stands in for a round the kit does not
	//! carry. `orders` binds each attachment decision to the weapon slot of its owner; an empty
	//! prefab there empties the seat the probe names, the only way a None removes anything, since
	//! seating is additive. Either may be null for a caller with no choice groups behind it.
	//!
	//! The placement itself is in Place; what stays here is only true of a real, server-owned,
	//! player-controlled body.
	static bool Apply_S(notnull IEntity character, notnull RK29_KitStruct kit, out array<ResourceName> droppedItems, map<int, ref array<ref RK29_LoadedPick>> loadedMags, array<ref RK29_AttachmentOrder> orders)
	{
		droppedItems = {};
		if (!Replication.IsServer())
			return false;

		Print(string.Format("[RK29] apply '%1' begins", kit.m_sKitName), LogLevel.NORMAL);

		// anchor the outfit-faction tally so the perceived faction cannot flip to unknown mid-apply
		// (garment swaps fire per-item recalcs). The rebuild below erases the anchor.
		SCR_CharacterFactionAffiliationComponent anchorAffiliation = SCR_CharacterFactionAffiliationComponent.Cast(
			character.FindComponent(SCR_CharacterFactionAffiliationComponent));
		if (anchorAffiliation && anchorAffiliation.GetAffiliatedFaction())
			anchorAffiliation.AddFactionOutfitValue(anchorAffiliation.GetAffiliatedFaction(), 100000, false);

		// Unconfirmed fix: adopted on vanilla's stated rule, never seen fail or succeed on a repro.
		// Hands empty first, without animation, so the strip never deletes the weapon the animation
		// graph is holding. Vanilla refuses plain storage ops against the weapon in hands ("it may
		// fail and cause desync" - SCR_InventoryStorageManagerComponent.EquipWeapon). Deliberate,
		// not Contextual: Contextual re-equips "when possible", chasing an about-to-die entity.
		SCR_ChimeraCharacter handsChar = SCR_ChimeraCharacter.Cast(character);
		if (handsChar)
		{
			CharacterControllerComponent handsCtrl = handsChar.GetCharacterController();
			if (handsCtrl)
				handsCtrl.TryEquipRightHandItem(null, EEquipItemType.EEquipTypeUnarmedDeliberate, true);
		}

		bool placed = Place(character, kit, droppedItems, loadedMags, orders);

		// full rebuild of the perceived-faction outfit scores - the strip can skip per-item remove
		// hooks and the score map is incremental. Through the mod's helper, never
		// InitPlayerOutfitFaction_S: vanilla runs that once per life and every extra call stacks
		// another world-scoped subscription. Runs on a refused Place too: the anchor above is
		// already in the tally and only this rebuild takes it out again.
		SCR_CharacterFactionAffiliationComponent affiliation = SCR_CharacterFactionAffiliationComponent.Cast(
			character.FindComponent(SCR_CharacterFactionAffiliationComponent));
		if (affiliation)
			affiliation.RK29_RebuildOutfitFaction_S();

		if (!placed)
			return false;

		ApplyTraits_S(character, kit);

		Print(string.Format("[RK29] apply '%1' done", kit.m_sKitName), LogLevel.NORMAL);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Every item of the kit onto a body and nothing else: strip, clothe, equip, arm, seat the chosen
	//! rounds, place the cargo, mount the attachments. Nothing here is player-, server- or
	//! replication-specific, which is what lets the preview mannequin share it.
	//!
	//! The passes, in this order and no other:
	//!   StripWeapons          - guns go wholesale; one carried over carries its state with it
	//!   StripGrenadeSlots     - the throwable slot, so PrimeGrenadeSlot can refill it
	//!   StripStorages         - before the clothing swap, or the outfit-faction score hits zero
	//!   ReplaceClothing       - slot by slot, for that same reason
	//!   StripStorages (again) - fresh garments arrive with prefab-authored slot spawns of their own
	//!   SweepRemaining        - whatever the storage strips could not reach
	//!   DressEquipment        - the kit's worn equipment
	//!   DressGarmentAttachments - what rides in a slot of a garment (night vision on the helmet)
	//!   DressWeapons          - guns into the weapon slots the orders name
	//!   SeatLoadedMags        - what each gun starts loaded with, already deducted from the cargo
	//!   DressItems            - the cargo solve; see the placement region below
	//!   ApplyAttachmentOrders - last, because the weapons must be fully spawned
	//!
	//! What stayed behind in Apply_S is exactly what a preview body must not run: Replication
	//! .IsServer() (the mannequin is a client), the perceived-faction anchor and rebuild,
	//! ApplyTraits_S, and emptying the hands through the character
	//! controller - safe to omit only because the caller hands over a body nothing was ever selected
	//! on. See RK29_MannequinDress.ApplyLoaded.
	//!
	//! `quiet` silences this pass's narration, for the preview alone - it re-runs on every pick
	//! change. False only on hard failure: a body with no inventory storage manager, not a soldier.
	static bool Place(notnull IEntity character, notnull RK29_KitStruct kit, out array<ResourceName> droppedItems, map<int, ref array<ref RK29_LoadedPick>> loadedMags, array<ref RK29_AttachmentOrder> orders, bool quiet = false)
	{
		if (!droppedItems)
			droppedItems = {};

		s_bQuiet = quiet;

		SCR_InventoryStorageManagerComponent manager = SCR_InventoryStorageManagerComponent.Cast(
			character.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!manager)
		{
			Note("[RK29] apply FAILED - no inventory storage manager on character", LogLevel.ERROR);
			return false;
		}

		EquipedLoadoutStorageComponent loadoutStorage = EquipedLoadoutStorageComponent.Cast(
			character.FindComponent(EquipedLoadoutStorageComponent));
		EquipedWeaponStorageComponent weaponStorage = EquipedWeaponStorageComponent.Cast(
			character.FindComponent(EquipedWeaponStorageComponent));

		// weapons are replaced wholesale: carrying one over carries its state - a part-used magazine,
		// or an empty chamber the engine gives no way to re-seat (ClearChamber exists, no setter)
		StripWeapons(manager, weaponStorage);
		StripGrenadeSlots(manager, character);

		// strip before clothing, then swap clothing slot-by-slot so the outfit-faction score never
		// hits zero (a full garment strip flips perceived faction to null = disguise popup)
		StripStorages(manager, character);
		ReplaceClothing(manager, loadoutStorage, kit);

		// freshly spawned garments arrive with prefab-authored slot spawns (canteens, gadget straps)
		StripStorages(manager, character);
		SweepRemaining(manager, character);
		DressEquipment(manager, character, kit);
		DressGarmentAttachments(manager, loadoutStorage, kit);

		// keyed by the slot an order names; nothing in this pass speaks of "the primary"
		map<int, IEntity> weaponEntities;
		DressWeapons(manager, weaponStorage, character, kit, weaponEntities);

		// before the cargo pass: the loaded magazine is already deducted from DressItems' counts
		if (loadedMags && !loadedMags.IsEmpty())
			SeatLoadedMags(manager, weaponEntities, loadedMags);

		DressItems(manager, weaponStorage, character, kit, droppedItems);

		// attachments last - the weapons must be fully spawned
		ApplyAttachmentOrders(manager, weaponEntities, orders);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! One line of the pass's narration, or nothing when the pass is a preview. Every Print inside
	//! Place's reach goes through here; RK29_Log.Trace lines do not - they have their own switch.
	protected static void Note(string message, LogLevel level)
	{
		if (s_bQuiet)
			return;

		Print(message, level);
	}

	//------------------------------------------------------------------------------------------------
	//! The kit's role qualifications as instance labels (vanilla reads these for the
	//! qualified-personnel speed bonus). Written on every apply, empty list included, so a re-kit
	//! never leaves the previous class's traits behind, and the write marks the body kit-owned
	//! (RK29_CharacterLabels.c), which stops the prefab's own labels counting. Public because the
	//! spawn path calls it alone, without the re-dress, for stock spawns.
	static void ApplyTraits_S(notnull IEntity character, notnull RK29_KitStruct kit)
	{
		SCR_EditableCharacterComponent editable = SCR_EditableCharacterComponent.Cast(
			character.FindComponent(SCR_EditableCharacterComponent));
		if (!editable)
		{
			if (kit.m_aTraits && !kit.m_aTraits.IsEmpty())
				Print("[RK29] traits skipped - no SCR_EditableCharacterComponent on the character", LogLevel.WARNING);
			return;
		}

		array<EEditableEntityLabel> labels = {};
		string named;
		if (kit.m_aTraits)
		{
			foreach (RK29_ETrait trait : kit.m_aTraits)
			{
				EEditableEntityLabel label = RK29_Traits.LabelOf(trait);
				if (label == EEditableEntityLabel.NONE || labels.Contains(label))
					continue;
				labels.Insert(label);
				named = named + " " + RK29_Traits.NameOf(trait);
			}
		}

		// RK29_SetTraits_S, not the vanilla setter: it marks the body kit-owned, without which a
		// body could only ever gain traits. The kit index travels in the same bump as the labels.
		editable.RK29_SetTraits_S(labels, RK29_KitManager.KitIndexOf(kit.m_sKitName));
		if (labels.IsEmpty())
			named = " none";
		// logged even when empty: "did the medic trait come off" is otherwise answered by timing a bandage
		Print(string.Format("[RK29] traits:%1", named), LogLevel.NORMAL);
	}

	// ====================================================================== attachments

	//------------------------------------------------------------------------------------------------
	//! Every attachment decision seated on the gun that owns it. `slotEntities` is DressWeapons'
	//! record of which weapon landed in which body slot; an order whose owner is not on the body is
	//! silently void - DressWeapons already said so.
	//!
	//! Clears before mounts, so a None on one tier cannot evict what another tier just seated.
	//! Order between mounts does not matter today: the one prerequisite in play (Bayonet_M9 needs
	//! AttachmentFlashHiderA2, refused outright by WeaponAttachmentsStorageComponent.CanStoreItem)
	//! is already satisfied by Rifle_M16A2's hard-authored Muzzle default. Three additions break
	//! that: a muzzle attachment evicting the A2 hider (a semantic conflict no order saves); RHS's
	//! Bayonet_6X9-1, needing a 6P20 hider ours does not declare; and RHS's Optic_Infratech1tws,
	//! which carries a live RIS1913-Short seat and would need mount-before-optic ordering.
	protected static void ApplyAttachmentOrders(notnull SCR_InventoryStorageManagerComponent manager,
		notnull map<int, IEntity> slotEntities, array<ref RK29_AttachmentOrder> orders)
	{
		if (!orders)
			return;

		for (int pass = 0; pass < 2; pass++)
		{
			foreach (RK29_AttachmentOrder order : orders)
			{
				if (!order)
					continue;
				bool clears = order.m_sPrefab == ResourceName.Empty;
				if (clears != (pass == 0))
					continue;

				IEntity owner;
				if (!slotEntities.Find(order.m_iOwnerSlot, owner) || !owner)
				{
					// an anomaly: orders are only emitted for slots the offer claimed a weapon for
					Note(string.Format(
						"[RK29] attachment order skipped - weapon slot %1 (%2) is empty",
						order.m_iOwnerSlot, order.m_sOwnerWeaponId), LogLevel.WARNING);
					continue;
				}

				ApplyAttachment(manager, owner, order.m_sPrefab, order.m_sProbe);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Attach or replace one attachment on a live weapon. An empty prefab is the order to take the
	//! sight off (None/irons). Which seat it lands in is SeatFor's answer - the same AttachmentType
	//! hierarchy walk that let the menu offer it, which a "first slot typed AttachmentOptics" test
	//! could never match for the SMAW. Delete-then-insert with the displaced attachment restored on
	//! failure. `probe` is the order's seat probe, any prefab the answering group offers, and is what
	//! an empty attachment has instead of a prefab - the only way a group removes anything at all.
	protected static bool ApplyAttachment(notnull SCR_InventoryStorageManagerComponent manager, notnull IEntity weapon, ResourceName attachment, ResourceName probe)
	{
		// Already there, anywhere on the gun: the weapon prefab may ship with the very thing this
		// order mounts, and nothing below notices - FindSeatFor prefers an empty seat to the
		// occupied one holding it, so the player carries two. Only the mount path asks; a clear
		// needs its seat walk to find what to evict.
		if (attachment != ResourceName.Empty && HasAttachment(weapon, attachment))
			return true;

		RK29_SeatDecision decision = DecideSeat(weapon, attachment, probe);
		if (decision.m_Occupant)
		{
			// off its seat before it dies, or the seat reads as occupied and the pick goes to irons
			ForceDelete(manager, decision.m_Occupant, decision.m_Seat);
		}

		if (attachment == ResourceName.Empty)
			return true;

		if (!InsertAttachment(manager, weapon, attachment))
		{
			Note(string.Format("[RK29] attachment did not fit weapon: %1", attachment),
				LogLevel.WARNING);
			// misconfiguration turns cosmetic: put the displaced attachment back instead of irons
			if (decision.m_sOccupant != ResourceName.Empty)
				InsertAttachment(manager, weapon, decision.m_sOccupant);
			return false;
		}
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! The seat one attachment order speaks for and whoever is standing in it. Both the apply and
	//! the preview ask this, so neither can develop its own idea of which slot a choice group owns.
	protected static RK29_SeatDecision DecideSeat(notnull IEntity weapon, ResourceName attachment, ResourceName probe)
	{
		return DecideSeatAt(SeatFor(weapon, attachment, probe));
	}

	//------------------------------------------------------------------------------------------------
	//! The same for a caller that resolved the seat its own way - the preview, whose seat model has
	//! one extra last-resort rung (RK29_MannequinDress.SeatForProbe). Null seat = empty decision.
	static RK29_SeatDecision DecideSeatAt(InventoryStorageSlot seat)
	{
		RK29_SeatDecision decision = new RK29_SeatDecision();
		if (!seat)
			return decision;

		decision.m_Seat = seat;
		decision.m_Occupant = seat.GetAttachedEntity();
		decision.m_sOccupant = PrefabOf(decision.m_Occupant);
		return decision;
	}

	//------------------------------------------------------------------------------------------------
	//! The seat one attachment order speaks for, asked once so the apply and the preview cannot
	//! disagree about which slot a choice group owns. Three ways to name it, strongest first:
	//!  - a prefab was named and its seat is occupied: that occupant has to leave.
	//!  - the group sent a seat probe, one of its own prefabs: where the group's contents would go is
	//!    the point the group speaks for, and a probe on an empty seat evicts nothing. This rung is
	//!    the only removal path a non-optic group has (seating only inserts, so a bayonet set to
	//!    None came off nothing) and reaches seats a declared slot type cannot - RHS's AttachmentMBS.
	//!  - neither: the weapon's sight seat, all an unqualified None can mean (the KitManager's
	//!    irons-only order, the one with no group behind it). Limited to the AttachmentOptics
	//!    hierarchy by OpticsSeatByType, so glass seated outside it stays on the gun - that weapon
	//!    needs a group.
	static InventoryStorageSlot SeatFor(notnull IEntity weapon, ResourceName attachment, ResourceName probe)
	{
		if (attachment != ResourceName.Empty)
		{
			InventoryStorageSlot seat = FindSeatFor(weapon, attachment);
			if (seat && seat.GetAttachedEntity())
				return seat;
		}

		if (probe != ResourceName.Empty)
			return FindSeatFor(weapon, probe);

		return OpticsSeatByType(weapon);
	}

	//------------------------------------------------------------------------------------------------
	protected static InventoryStorageSlot FindSeatFor(notnull IEntity weapon, ResourceName attachment)
	{
		BaseInventoryStorageComponent ignored;
		return FindSeatFor(weapon, attachment, ignored);
	}

	//------------------------------------------------------------------------------------------------
	//! The slot on this weapon - or on anything already mounted to it - that accepts `attachment`, by
	//! the same hierarchy walk that let the menu offer it (RK29_KitCompose.MountFits). `outStorage`
	//! is the storage owning that slot, since seating is TrySpawnPrefabToStorage(prefab, storage,
	//! slot id) and a seat on a mounted rail belongs to the rail. An empty seat wins over an occupied
	//! one. Null means no declared mount type, an unreadable prefab, or no matching seat - callers
	//! answer all three by falling through to engine routing, never by dropping the attachment.
	//! It searches mounted sub-entities where the offer reads the prefab's merged tree: the apply
	//! may seat what the offer never promised, never refuse what it did.
	protected static InventoryStorageSlot FindSeatFor(notnull IEntity weapon, ResourceName attachment, out BaseInventoryStorageComponent outStorage)
	{
		outStorage = null;
		if (attachment == ResourceName.Empty)
			return null;

		array<string> attachTypes = RK29_KitCompose.AttachTypesOf(attachment);
		if (!attachTypes || attachTypes.IsEmpty())
			return null;

		array<BaseInventoryStorageComponent> storages = {};
		CollectAttachmentStorages(weapon, storages);

		InventoryStorageSlot taken;
		BaseInventoryStorageComponent takenStorage;

		foreach (BaseInventoryStorageComponent storage : storages)
		{
			for (int i = 0, n = storage.GetSlotsCount(); i < n; i++)
			{
				InventoryStorageSlot slot = storage.GetSlot(i);
				if (!slot || !SlotAccepts(slot, attachTypes))
					continue;

				if (!slot.GetAttachedEntity())
				{
					outStorage = storage;
					return slot;
				}

				if (!taken)
				{
					taken = slot;
					takenStorage = storage;
				}
			}
		}

		outStorage = takenStorage;
		return taken;
	}

	//------------------------------------------------------------------------------------------------
	//! Does this live slot take an attachment declaring any of these mount types? The slot's type is
	//! a typename off its AttachmentSlotComponent and the attachment's are class names off its
	//! prefab, so both are spelled out and meet in RK29_KitCompose.MountFits - the offer's own call.
	protected static bool SlotAccepts(notnull InventoryStorageSlot slot, notnull array<string> attachTypes)
	{
		AttachmentSlotComponent asc = AttachmentSlotComponent.Cast(slot.GetParentContainer());
		if (!asc)
			return false;
		BaseAttachmentType slotType = asc.GetAttachmentSlotType();
		if (!slotType)
			return false;

		string slotTypeName = slotType.Type().ToString();
		foreach (string attachType : attachTypes)
		{
			if (RK29_KitCompose.MountFits(attachType, slotTypeName))
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Seat an attachment on the weapon or on anything mounted to it. FindSeatFor's seat is tried
	//! first by slot id; engine routing (slot id -1) stays behind it for attachments whose prefab
	//! declares no mount type - the offer cannot type-check those either, and refusing them here
	//! would only invent a new way to lose gear.
	protected static bool InsertAttachment(notnull SCR_InventoryStorageManagerComponent manager, notnull IEntity weapon, ResourceName prefab)
	{
		BaseInventoryStorageComponent seatStorage;
		InventoryStorageSlot seat = FindSeatFor(weapon, prefab, seatStorage);
		if (seat && seatStorage && manager.TrySpawnPrefabToStorage(prefab, seatStorage, seat.GetID()))
			return true;

		array<BaseInventoryStorageComponent> storages = {};
		CollectAttachmentStorages(weapon, storages);
		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (manager.TrySpawnPrefabToStorage(prefab, storage, -1))
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Every attachment storage reachable from the weapon, pre-order: its own first, then those
	//! carried by whatever is mounted to it. The single tree walk behind the seat search, the
	//! duplicate guard and the sight lookup. Typed on the engine class, not the SCR_ subclass: a
	//! modded weapon may use the base component.
	protected static void CollectAttachmentStorages(IEntity entity, notnull array<BaseInventoryStorageComponent> outStorages)
	{
		WalkAttachmentStorages(entity, outStorages, 0);
	}

	//------------------------------------------------------------------------------------------------
	protected static void WalkAttachmentStorages(IEntity entity, notnull array<BaseInventoryStorageComponent> outStorages, int depth)
	{
		if (!entity || depth > ATTACHMENT_WALK_DEPTH)
			return;

		WeaponAttachmentsStorageComponent attachStorage = WeaponAttachmentsStorageComponent.Cast(
			entity.FindComponent(WeaponAttachmentsStorageComponent));
		if (!attachStorage)
			return;

		outStorages.Insert(attachStorage);
		for (int i = 0, n = attachStorage.GetSlotsCount(); i < n; i++)
		{
			InventoryStorageSlot slot = attachStorage.GetSlot(i);
			if (slot)
				WalkAttachmentStorages(slot.GetAttachedEntity(), outStorages, depth + 1);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Is this exact prefab already mounted anywhere on the weapon - the duplicate guard, by prefab
	//! name. Wider than the seat search on purpose: that looks past an occupied seat to a free one,
	//! so a gun with two compatible seats gains a second copy. The preview asks it for that reason.
	static bool HasAttachment(IEntity entity, ResourceName prefab)
	{
		if (!entity || prefab == ResourceName.Empty)
			return false;

		array<BaseInventoryStorageComponent> storages = {};
		CollectAttachmentStorages(entity, storages);
		foreach (BaseInventoryStorageComponent storage : storages)
		{
			for (int i = 0, n = storage.GetSlotsCount(); i < n; i++)
			{
				InventoryStorageSlot slot = storage.GetSlot(i);
				if (!slot)
					continue;
				if (PrefabOf(slot.GetAttachedEntity()) == prefab)
					return true;
			}
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! The bare-seat fallback: the first slot typed AttachmentOptics, descending nested storages
	//! because a scope can sit on a mount. A guess, reached only when nothing better was said - no
	//! prefab, no probe; a group's None arrives with a probe and takes SeatFor's probe rung instead.
	static InventoryStorageSlot OpticsSeatByType(IEntity weapon)
	{
		return WalkOpticsSeat(weapon, 0);
	}

	//------------------------------------------------------------------------------------------------
	protected static InventoryStorageSlot WalkOpticsSeat(IEntity entity, int depth)
	{
		if (!entity || depth > ATTACHMENT_WALK_DEPTH)
			return null;

		WeaponAttachmentsStorageComponent attachStorage = WeaponAttachmentsStorageComponent.Cast(
			entity.FindComponent(WeaponAttachmentsStorageComponent));
		if (!attachStorage)
			return null;

		for (int i = 0, n = attachStorage.GetSlotsCount(); i < n; i++)
		{
			InventoryStorageSlot slot = attachStorage.GetSlot(i);
			if (!slot)
				continue;

			if (IsOpticsSlot(slot))
				return slot;

			InventoryStorageSlot deeper = WalkOpticsSeat(slot.GetAttachedEntity(), depth + 1);
			if (deeper)
				return deeper;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! The sight seat by type alone: an AttachmentSlotComponent declaring an AttachmentOptics type.
	//! The bare-seat fallback's predicate and nothing else - what an attachment fits is MountFits'.
	protected static bool IsOpticsSlot(InventoryStorageSlot slot)
	{
		if (!slot)
			return false;

		AttachmentSlotComponent asc = AttachmentSlotComponent.Cast(slot.GetParentContainer());
		if (!asc || !asc.GetAttachmentSlotType())
			return false;

		return asc.GetAttachmentSlotType().Type().IsInherited(AttachmentOptics);
	}

	// ============================================================================ strip

	//------------------------------------------------------------------------------------------------
	//! Off the body for good, however the storage manager feels about it. TryDeleteItem answers false
	//! often enough - an item in a slot the manager does not own, a preview body with no replication
	//! - that every strip needs the second route: detach first, because a seat still holding a freed
	//! entity reads as occupied to the next insert. `known` is the seat when the caller has it.
	protected static void ForceDelete(notnull SCR_InventoryStorageManagerComponent manager, notnull IEntity victim, InventoryStorageSlot known = null)
	{
		if (manager.TryDeleteItem(victim))
			return;

		InventoryStorageSlot seat = known;
		if (!seat)
		{
			InventoryItemComponent item = InventoryItemComponent.Cast(
				victim.FindComponent(InventoryItemComponent));
			if (item)
				seat = item.GetParentSlot();
		}
		if (seat && seat.GetAttachedEntity() == victim)
			seat.DetachEntity();

		SCR_EntityHelper.DeleteEntityAndChildren(victim);
	}

	//------------------------------------------------------------------------------------------------
	//! The tail both collect-then-delete passes end in. They collect first because universal storages
	//! reshuffle their slot list on removal, so deleting while indexing skips survivors (log-proven:
	//! one item stripped per pouch, the rest left to duplicate). `doomedSlots` is index-aligned with
	//! `doomed`; a victim a container already took reads non-null until end of frame, hence IsDeleted.
	protected static void DrainDoomed(notnull SCR_InventoryStorageManagerComponent manager, notnull array<IEntity> doomed, notnull array<InventoryStorageSlot> doomedSlots)
	{
		for (int d = 0, dn = doomed.Count(); d < dn; d++)
		{
			IEntity victim = doomed[d];
			if (!victim || victim.IsDeleted())
				continue;

			ForceDelete(manager, victim, doomedSlots[d]);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Deletes every weapon-slot occupant, same-prefab weapons included (rationale in Place).
	protected static void StripWeapons(SCR_InventoryStorageManagerComponent manager, EquipedWeaponStorageComponent weaponStorage)
	{
		if (!weaponStorage)
			return;

		for (int i = 0, n = weaponStorage.GetSlotsCount(); i < n; i++)
		{
			InventoryStorageSlot slot = weaponStorage.GetSlot(i);
			if (!slot)
				continue;
			IEntity item = slot.GetAttachedEntity();
			if (!item)
				continue;

			ForceDelete(manager, item, slot);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The throwable slot is a CharacterGrenadeSlotComponent, not enumerated by the equipped-weapon
	//! storage (proven empirically: a primed smoke survives every apply). Kits carry grenades as
	//! items only and the dress primes one from cargo, so every occupant goes.
	protected static void StripGrenadeSlots(SCR_InventoryStorageManagerComponent manager, IEntity character)
	{
		array<Managed> slots = {};
		character.FindComponents(CharacterGrenadeSlotComponent, slots);
		foreach (Managed m : slots)
		{
			CharacterGrenadeSlotComponent slot = CharacterGrenadeSlotComponent.Cast(m);
			if (!slot)
				continue;
			IEntity grenade = slot.GetWeaponEntity();
			if (!grenade)
				continue;

			RK29_Log.Trace("[RK29] strip: " + FileNameOf(grenade) + " from grenade slot");
			ForceDelete(manager, grenade, slot.GetSlotInfo());
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One strip rule instead of per-category patches: walk every storage on the body and delete
	//! every slot occupant, except storages another pass owns and occupants that are structure. No
	//! item-category or slot-name knowledge - anything gear-like in a storage slot dies here and
	//! DressX rebuilds from config. Skipped domains: garment slots, weapon slots, storages on
	//! weapons, the managed equipment storage, medical/identity. Kept: containers and cloth pieces.
	protected static void StripStorages(SCR_InventoryStorageManagerComponent manager, IEntity character)
	{
		// entity-tree walk rather than manager.GetStorages(): the strip wants everything on the body,
		// managed or not - an unmanaged storage (the Lifchik GL grenade belt) hides stray gear
		array<BaseInventoryStorageComponent> storages = {};
		CollectBodyStorages(character, storages);
		array<IEntity> doomed = {};
		array<InventoryStorageSlot> doomedSlots = {};
		foreach (BaseInventoryStorageComponent storage : storages)
		{
			// the whole skip list is IsCargoStorage - exactly the containers the cargo pass refills
			if (!IsCargoStorage(storage, character))
				continue;

			// collect first, delete after - the reason is on DrainDoomed
			for (int i = 0, n = storage.GetSlotsCount(); i < n; i++)
			{
				InventoryStorageSlot slot = storage.GetSlot(i);
				if (!slot)
					continue;
				IEntity occupant = slot.GetAttachedEntity();
				if (!occupant)
					continue;
				// The keep rule, with no item-type taxonomy: an occupant is structure iff the
				// slot's authored template spawned it and it is a container or a cloth piece.
				// Authored non-structure - the bayonet a scabbard auto-spawns, vest canteens - is
				// gear; BI's quirks (a shovel authored as clothing) cannot leak through.
				if (IsSlotAuthored(slot, occupant)
					&& (occupant.FindComponent(BaseInventoryStorageComponent)
						|| occupant.FindComponent(BaseLoadoutClothComponent)))
				{
					RK29_Log.Trace("[RK29] kept(authored): " + FileNameOf(occupant) + " @ " + OwnerFileName(storage) + "/" + slot.GetSourceName());
					continue;
				}

				doomed.Insert(occupant);
				doomedSlots.Insert(slot);
				RK29_Log.Trace("[RK29] strip: " + FileNameOf(occupant) + " from " + OwnerFileName(storage) + "/" + slot.GetSourceName());
			}
		}

		DrainDoomed(manager, doomed, doomedSlots);
	}

	//------------------------------------------------------------------------------------------------
	//! Enforcement backstop: after the strip, any loose item still on the body is unwanted. Logs
	//! where each survivor hid, then deletes it - what stops the stacking e-tool accumulating.
	protected static void SweepRemaining(SCR_InventoryStorageManagerComponent manager, IEntity character)
	{
		array<IEntity> items = {};
		manager.GetItems(items);
		foreach (IEntity item : items)
		{
			if (!item || item.FindComponent(WeaponComponent))
				continue;
			if (IsInsideWeapon(item))
				continue; // a weapon owns its loaded mags/attachments

			InventoryItemComponent iic = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
			// no InventoryItemComponent = not gear but cosmetic cloth on a garment's LoadoutSlotInfo
			// - the ALICE AR vest's e-tool carrier. With no parent slot every keep rule below is
			// unreachable for it, and it fell through: the AR lost the shovel pouch on every apply.
			if (!iic)
				continue;

			InventoryStorageSlot slot = iic.GetParentSlot();
			if (!slot)
				continue; // not slotted anywhere the inventory owns - leave it
			// same keep rule as the strip: authored and structural
			if (IsSlotAuthored(slot, item)
				&& (item.FindComponent(BaseInventoryStorageComponent)
					|| item.FindComponent(BaseLoadoutClothComponent)))
				continue;
			// Not IsCargoStorage: where that predicate refuses every storage the character entity
			// owns, this pass wants most of them - the hands and the gadget slot are what nothing
			// else clears (the auto-equipped compass). Only the named equipment domain is spared.
			BaseInventoryStorageComponent slotStorage = slot.GetStorage();
			if (slotStorage)
			{
				if (EquipedLoadoutStorageComponent.Cast(slotStorage))
					continue; // worn garment
				if (IsManagedEquipmentStorage(slotStorage, character))
					continue; // DressEquipment's delta domain
				if (IsBodyStateStorage(slotStorage))
					continue;
			}
			if (item.IsDeleted())
				continue; // went with a container deleted a step earlier
			string where;
			if (slotStorage)
				where = slotStorage.ClassName() + " on " + OwnerFileName(slotStorage) + "/" + slot.GetSourceName();
			else
				where = "slot-without-storage/" + slot.GetSourceName();
			string parentName = "none";
			if (item.GetParent())
				parentName = FileNameOf(item.GetParent());
			Note(string.Format("[RK29] UNSWEPT: %1 | %2 | parent=%3", FileNameOf(item),
				where, parentName), LogLevel.WARNING);

			ForceDelete(manager, item, slot);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsInsideWeapon(IEntity item)
	{
		IEntity p = item.GetParent();
		while (p)
		{
			if (p.FindComponent(WeaponComponent))
				return true;
			p = p.GetParent();
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Did the garment prefab put this occupant here (structure), or did something clip it in (gear)?
	protected static bool IsSlotAuthored(InventoryStorageSlot slot, IEntity occupant)
	{
		if (!slot || !occupant)
			return false;
		ResourceName tmpl = slot.GetSlotTemplate();
		if (tmpl == ResourceName.Empty)
			return false;

		return PrefabOf(occupant) == tmpl;
	}

	//------------------------------------------------------------------------------------------------
	//! Every storage on the character and all attached descendants; manager registration plays no part.
	protected static void CollectBodyStorages(IEntity entity, notnull array<BaseInventoryStorageComponent> outStorages)
	{
		WalkBodyStorages(entity, outStorages, 0);
	}

	//------------------------------------------------------------------------------------------------
	//! The depth ceiling is the entity tree's, not the attachment walk's: a pouch on a vest on a body
	//! is three deep already.
	protected static void WalkBodyStorages(IEntity entity, notnull array<BaseInventoryStorageComponent> outStorages, int depth)
	{
		if (!entity || depth > 8)
			return;

		array<Managed> comps = {};
		entity.FindComponents(BaseInventoryStorageComponent, comps);
		foreach (Managed m : comps)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(m);
			if (storage)
				outStorages.Insert(storage);
		}

		IEntity child = entity.GetChildren();
		while (child)
		{
			WalkBodyStorages(child, outStorages, depth + 1);
			child = child.GetSibling();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! An entity's prefab, empty for anything that cannot answer - which can never match a config
	//! ResourceName, so no call site needs a null check of its own.
	protected static ResourceName PrefabOf(IEntity entity)
	{
		if (!entity)
			return ResourceName.Empty;
		EntityPrefabData epd = entity.GetPrefabData();
		if (!epd)
			return ResourceName.Empty;

		return epd.GetPrefabName();
	}

	//------------------------------------------------------------------------------------------------
	//! An entity's prefab file name, "?" for anything that cannot answer - a log line, never a key.
	protected static string FileNameOf(IEntity entity)
	{
		ResourceName prefab = PrefabOf(entity);
		if (prefab == ResourceName.Empty)
			return "?";

		return FilePath.StripPath("" + prefab);
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsOnWeapon(BaseInventoryStorageComponent storage)
	{
		IEntity e = storage.GetOwner();
		while (e)
		{
			if (e.FindComponent(WeaponComponent))
				return true;
			e = e.GetParent();
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! The character-carried plain equipment storage (watch/binocular slots) DressEquipment manages.
	//! Garment strap storages are also SCR_EquipmentStorageComponent but belong to the item pass.
	protected static bool IsManagedEquipmentStorage(BaseInventoryStorageComponent storage, IEntity character)
	{
		if (!SCR_EquipmentStorageComponent.Cast(storage))
			return false;
		if (IsBodyStateStorage(storage))
			return false;
		return storage.GetOwner() == character;
	}

	//------------------------------------------------------------------------------------------------
	//! Applied tourniquets, saline, dogtags: condition and identity, never loadout - never written to.
	protected static bool IsBodyStateStorage(BaseInventoryStorageComponent storage)
	{
		return SCR_IdentityItemStorageComponent.Cast(storage)
			|| SCR_SalineStorageComponent.Cast(storage)
			|| SCR_TourniquetStorageComponent.Cast(storage);
	}

	//------------------------------------------------------------------------------------------------
	//! Is this storage one the cargo pass owns? Asked identically by the strip that empties them and
	//! the collect that fills them, or a container one sees and the other does not is either gear
	//! that survives an apply or gear that vanishes into it. What another pass owns is not cargo:
	//! garment slots, weapon slots, anything mounted on a weapon, the medical/identity storages.
	//! Nothing owned by the character entity is cargo either - those are the hands and the
	//! gadget/offhand slot, capacity 1 and accepting anything: ChooseContainer scored them like any
	//! container, each new item displaced the last, and the survivor was taken into the left hand on
	//! spawn - the auto-equipped compass. Slots on worn gear belong to the garment and are left
	//! alone. A garment's cloth-node storage (RHS helmet NVG/velcro/rail slots) is
	//! DressGarmentAttachments' domain: it accepts only cloth pieces, so as a container it could
	//! only waste probes, and it needs no strip - ReplaceClothing deletes and respawns the garment
	//! whole. CollectCargoContainers adds one exclusion of its own on top of this.
	protected static bool IsCargoStorage(BaseInventoryStorageComponent storage, IEntity character)
	{
		if (!storage)
			return false;
		if (EquipedLoadoutStorageComponent.Cast(storage)
			|| EquipedWeaponStorageComponent.Cast(storage)
			|| ClothNodeStorageComponent.Cast(storage))
			return false;
		if (IsBodyStateStorage(storage))
			return false;
		if (storage.GetOwner() == character)
			return false;

		return !IsOnWeapon(storage);
	}

	//------------------------------------------------------------------------------------------------
	//! Delta-swap the character's equipment storage (watch, binoculars) to the kit's declared set.
	//! Same-prefab occupants stay; slots the kit does not declare get emptied.
	protected static void DressEquipment(SCR_InventoryStorageManagerComponent manager, IEntity character, RK29_KitStruct kit)
	{
		// entity-tree walk: equipment storages are invisible to manager.GetStorages()
		array<BaseInventoryStorageComponent> storages = {};
		CollectBodyStorages(character, storages);

		map<string, bool> satisfied = new map<string, bool>();
		array<IEntity> eqDoomed = {};
		array<InventoryStorageSlot> eqDoomedSlots = {};
		BaseInventoryStorageComponent equipStorage;
		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (!IsManagedEquipmentStorage(storage, character))
				continue;
			equipStorage = storage;

			for (int i = 0, n = storage.GetSlotsCount(); i < n; i++)
			{
				InventoryStorageSlot slot = storage.GetSlot(i);
				if (!slot)
					continue;
				IEntity occupant = slot.GetAttachedEntity();
				if (!occupant)
					continue;

				ResourceName wanted;
				kit.m_mEquipment.Find(slot.GetSourceName(), wanted);

				if (wanted != ResourceName.Empty && PrefabOf(occupant) == wanted)
				{
					satisfied.Set(slot.GetSourceName(), true);
					continue;
				}

				RK29_Log.Trace("[RK29] equip-clear: " + FileNameOf(occupant) + " from " + slot.GetSourceName());
				eqDoomed.Insert(occupant);
				eqDoomedSlots.Insert(slot);
			}
		}

		DrainDoomed(manager, eqDoomed, eqDoomedSlots);

		if (!equipStorage)
		{
			if (!kit.m_mEquipment.IsEmpty())
				Note("[RK29] no equipment storage - equipment skipped", LogLevel.WARNING);
			return;
		}

		foreach (string slotName, ResourceName prefab : kit.m_mEquipment)
		{
			if (satisfied.Contains(slotName))
				continue;
			if (!manager.TrySpawnPrefabToStorage(prefab, equipStorage, -1))
				Note(string.Format("[RK29] equipment did not equip: %1", prefab),
					LogLevel.WARNING);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Attachments worn in a slot of a garment - RHS night vision in the helmet's NVG slot - from
	//! the kit's garment-attachment map, keyed "<loadout slot>/<slot on the garment>". After
	//! ReplaceClothing on purpose: the garment is the kit's own, fresh from its prefab, so a stale
	//! attachment died with the old one and nothing here needs a strip. A garment without that slot
	//! enabled (the vanilla PASGT under RHS) is logged and the attachment is not issued elsewhere:
	//! carried, night vision does nothing.
	protected static void DressGarmentAttachments(SCR_InventoryStorageManagerComponent manager, EquipedLoadoutStorageComponent loadoutStorage, RK29_KitStruct kit)
	{
		if (kit.m_mGarmentAttachments.IsEmpty())
			return;
		if (!loadoutStorage)
		{
			Note("[RK29] no loadout storage - garment attachments skipped", LogLevel.WARNING);
			return;
		}

		foreach (string key, ResourceName prefab : kit.m_mGarmentAttachments)
		{
			string garmentSlot, slotName;
			if (!RK29_KitStruct.SplitGarmentSlotKey(key, garmentSlot, slotName))
			{
				Note("[RK29] config ERROR - garment attachment key '" + key
					+ "' is not <loadout slot>/<slot on the garment>", LogLevel.ERROR);
				continue;
			}

			IEntity garment = GarmentIn(loadoutStorage, garmentSlot);
			if (!garment)
			{
				Note(string.Format("[RK29] garment attachment did not equip: nothing worn in %1 for %2",
					garmentSlot, FilePath.StripPath("" + prefab)), LogLevel.WARNING);
				continue;
			}

			// the base class on purpose: RHS's helmet storage is a subclass and is found through it
			ClothNodeStorageComponent nodes = ClothNodeStorageComponent.Cast(garment.FindComponent(ClothNodeStorageComponent));
			int slotIdx = EnabledSlotIndex(nodes, slotName);
			if (slotIdx < 0)
			{
				Note(string.Format("[RK29] garment attachment did not equip: %1 has no enabled %2 slot for %3",
					FileNameOf(garment), slotName, FilePath.StripPath("" + prefab)), LogLevel.WARNING);
				continue;
			}

			if (!manager.TrySpawnPrefabToStorage(prefab, nodes, slotIdx))
				Note(string.Format("[RK29] garment attachment did not equip: %1 refused %2 in %3",
					FileNameOf(garment), FilePath.StripPath("" + prefab), slotName), LogLevel.WARNING);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The garment worn in the loadout slot of that source name - the key ReplaceClothing dresses by.
	protected static IEntity GarmentIn(notnull EquipedLoadoutStorageComponent loadoutStorage, string slotName)
	{
		for (int i = 0, n = loadoutStorage.GetSlotsCount(); i < n; i++)
		{
			InventoryStorageSlot slot = loadoutStorage.GetSlot(i);
			if (slot && slot.GetSourceName() == slotName)
				return slot.GetAttachedEntity();
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Index of the storage's enabled loadout slot of that source name, -1 for none. The index is what
	//! TrySpawnPrefabToStorage takes for a cloth-node storage - the same number RHS's own attach
	//! action passes, from a GetSlot(i) walk. A disabled slot is one the helmet declares but does
	//! not offer: the vanilla PASGT's NVG slot under RHS.
	protected static int EnabledSlotIndex(ClothNodeStorageComponent storage, string slotName)
	{
		if (!storage)
			return -1;
		for (int i = 0, n = storage.GetSlotsCount(); i < n; i++)
		{
			LoadoutSlotInfo slot = LoadoutSlotInfo.Cast(storage.GetSlot(i));
			if (slot && slot.IsEnabled() && slot.GetSourceName() == slotName)
				return i;
		}
		return -1;
	}

	// ============================================================================ dress

	//------------------------------------------------------------------------------------------------
	//! Every loadout slot: delete its garment, then spawn the garment the kit names for that slot,
	//! addressed by slot source name - the same keys the capture uses and the preview dresses by. A
	//! garment whose slot name matches no slot on this body is still offered with no slot named, so
	//! a body prefab spelling a slot differently costs a routed spawn, not a missing garment.
	protected static void ReplaceClothing(SCR_InventoryStorageManagerComponent manager, EquipedLoadoutStorageComponent loadoutStorage, RK29_KitStruct kit)
	{
		if (!loadoutStorage)
		{
			Note("[RK29] no loadout storage - clothing skipped", LogLevel.WARNING);
			return;
		}

		// snapshot first: a spawn into slot i must never delete a garment spawned a step earlier
		int slotCount = loadoutStorage.GetSlotsCount();
		array<IEntity> oldGarments = {};
		for (int i = 0; i < slotCount; i++)
		{
			InventoryStorageSlot slot = loadoutStorage.GetSlot(i);
			if (slot)
				oldGarments.Insert(slot.GetAttachedEntity());
			else
				oldGarments.Insert(null);
		}

		// scorched earth by decision: garments are always deleted and respawned, so nothing hiding in
		// one survives an apply. Safe for the disguise system only because the apply is
		// single-frame, anchored, and the outfit tally is rebuilt afterwards.
		map<string, bool> placed = new map<string, bool>();
		for (int i = 0; i < slotCount; i++)
		{
			InventoryStorageSlot slot = loadoutStorage.GetSlot(i);
			IEntity occupant = oldGarments[i];
			if (occupant)
				ForceDelete(manager, occupant, slot);
			if (!slot)
				continue;

			ResourceName wanted;
			if (!kit.m_mClothing.Find(slot.GetSourceName(), wanted) || wanted == ResourceName.Empty)
				continue;

			if (manager.TrySpawnPrefabToStorage(wanted, loadoutStorage, i))
				placed.Set(slot.GetSourceName(), true);
			else
				Note(string.Format("[RK29] clothing did not equip in slot %1: %2",
					slot.GetSourceName(), wanted), LogLevel.WARNING);
		}

		// garments whose slot name this body does not spell: routed, and said out loud
		foreach (string slotName, ResourceName prefab : kit.m_mClothing)
		{
			if (prefab == ResourceName.Empty || placed.Contains(slotName))
				continue;
			if (manager.TrySpawnPrefabToStorage(prefab, loadoutStorage, -1))
				Note(string.Format("[RK29] clothing slot '%1' not on this body - %2 routed by the"
					+ " engine", slotName, prefab), LogLevel.WARNING);
			else
				Note(string.Format("[RK29] clothing did not equip: %1", prefab), LogLevel.WARNING);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Fills outSlotEntities with every weapon it spawned, keyed by the kit slot it went into - the
	//! loaded-magazine and attachment passes both address a gun by its slot.
	//! Exact slot or nothing. A retry at slotId -1 lets the engine route a refused weapon anywhere in
	//! the weapon storage, which is how an apply comes out with the rifle and the launcher in each
	//! other's slots; vanilla never blind-routes into weapon storage either. A missing weapon is
	//! diagnosable, a scrambled loadout is not.
	protected static void DressWeapons(SCR_InventoryStorageManagerComponent manager, EquipedWeaponStorageComponent weaponStorage, IEntity character, RK29_KitStruct kit, out map<int, IEntity> outSlotEntities)
	{
		outSlotEntities = new map<int, IEntity>();

		if (!weaponStorage)
		{
			Note("[RK29] no weapon storage - weapons skipped", LogLevel.WARNING);
			return;
		}

		foreach (int slotIdx, ResourceName prefab : kit.m_mWeapons)
		{
			SCR_AITakeItemFromArsenal_InventoryCallback cb = new SCR_AITakeItemFromArsenal_InventoryCallback();
			if (!manager.TrySpawnPrefabToStorage(prefab, weaponStorage, slotIdx, cb: cb))
			{
				// out from under the quiet gate deliberately, the one exception: a slot the kit
				// names refusing its weapon is a fault every time - it stayed silent while the F4
				// mannequin lost its slot-0 gun and the weight row read short. The occupancy string
				// separates "the strip left it there" from "the weapon does not fit that slot".
				Print(string.Format(
					"[RK29] weapon slot %1 refused %2 (%3) - weapon skipped, not rerouted",
					slotIdx, FilePath.StripPath("" + prefab), SlotOccupancy(character, slotIdx)),
					LogLevel.WARNING);
				continue;
			}

			// the callback answers through replication, which a preview body may not have
			IEntity spawned = cb.GetEntity();
			if (!spawned)
			{
				for (int s = 0, sn = weaponStorage.GetSlotsCount(); s < sn; s++)
				{
					InventoryStorageSlot seat = weaponStorage.GetSlot(s);
					if (seat && seat.GetID() == slotIdx)
					{
						spawned = seat.GetAttachedEntity();
						break;
					}
				}
			}
			if (spawned)
				outSlotEntities.Set(slotIdx, spawned);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Seats the chosen loaded rounds. The loaded round is an ordinary entity in the muzzle's
	//! magazine-well slot (vanilla's arsenal swaps it exactly this way): delete what the prefab
	//! spawned, spawn the choice into the same slot. A pick with an empty prefab is the delete half
	//! alone - a freshly spawned weapon arrives with its authored magazine in the well, so doing
	//! nothing leaves a round behind; against an already-empty muzzle it is a no-op. One weapon can
	//! take two picks, each stating its own destination: the magazine and the underbarrel grenade.
	protected static void SeatLoadedMags(notnull SCR_InventoryStorageManagerComponent manager, notnull map<int, IEntity> slotEntities, notnull map<int, ref array<ref RK29_LoadedPick>> loadedMags)
	{
		foreach (int slotIdx, array<ref RK29_LoadedPick> picks : loadedMags)
		{
			if (!picks)
				continue;

			IEntity weapon;
			if (!slotEntities.Find(slotIdx, weapon) || !weapon)
			{
				// the resolve already took this round out of the spares - the log is the only witness
				Note(string.Format("[RK29] loaded magazine skipped - no weapon in slot %1",
					slotIdx), LogLevel.WARNING);
				continue;
			}

			// a weapon's own storage holds both its attachment slots and its magazine wells
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(
				weapon.FindComponent(BaseInventoryStorageComponent));
			if (!storage)
			{
				Note(string.Format("[RK29] loaded magazine skipped - no storage on %1",
					FileNameOf(weapon)), LogLevel.WARNING);
				continue;
			}

			foreach (RK29_LoadedPick pick : picks)
			{
				if (pick)
					SeatOnePick(manager, weapon, storage, pick);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One pick into one muzzle: find the well the pick names, empty it, seat the choice. `storage`
	//! is the weapon's own, holding attachment slots and magazine wells alike; an underbarrel pick is
	//! usually answered by a storage on the launcher, so the destination is resolved per pick.
	protected static void SeatOnePick(notnull SCR_InventoryStorageManagerComponent manager,
		notnull IEntity weapon, notnull BaseInventoryStorageComponent storage, notnull RK29_LoadedPick pick)
	{
		// "empty this muzzle", not "no instruction" - the targeting below is the same either way
		bool clearOnly = pick.m_sPrefab == ResourceName.Empty;

		BaseInventoryStorageComponent destStorage = storage;
		InventoryStorageSlot magSlot;
		if (pick.m_bUnderbarrel)
			magSlot = FindUnderbarrelMuzzleSlot(storage, destStorage);
		else
			magSlot = FindOwnMuzzleSlot(weapon, storage);

		if (!magSlot)
		{
			// an order to empty a muzzle this weapon does not have is already satisfied
			if (clearOnly)
				return;
			if (pick.m_bUnderbarrel)
				Note(string.Format(
					"[RK29] loaded grenade skipped - no underbarrel muzzle on %1",
					FileNameOf(weapon)), LogLevel.WARNING);
			else
				Note(string.Format(
					"[RK29] loaded magazine skipped - no magazine well on %1",
					FileNameOf(weapon)), LogLevel.WARNING);
			return;
		}

		IEntity seated = magSlot.GetAttachedEntity();
		// remembered so a replacement the well refuses does not leave the gun empty
		ResourceName seatedPrefab = PrefabOf(seated);
		if (seated)
		{
			// never asked of a clear: an occupant that cannot name itself must not read as a match
			if (!clearOnly && seatedPrefab == pick.m_sPrefab)
				return;

			ForceDelete(manager, seated, magSlot);
		}
		else if (clearOnly)
			return; // asked for empty, already empty

		if (clearOnly)
		{
			Note(string.Format("[RK29] emptied the muzzle of %1 - the chambered round"
				+ " was picked down to zero", FileNameOf(weapon)), LogLevel.NORMAL);
			return;
		}

		if (manager.TrySpawnPrefabToStorage(pick.m_sPrefab, destStorage, magSlot.GetID()))
		{
			Note(string.Format("[RK29] loaded %1 into %2", FilePath.StripPath("" + pick.m_sPrefab),
				FileNameOf(weapon)), LogLevel.NORMAL);
			return;
		}

		Note(string.Format("[RK29] loaded magazine did not seat in %1: %2",
			FileNameOf(weapon), pick.m_sPrefab), LogLevel.WARNING);
		// the round it came with goes back: the spares are already short by one
		if (seatedPrefab != ResourceName.Empty
			&& manager.TrySpawnPrefabToStorage(seatedPrefab, destStorage, magSlot.GetID()))
			Note(string.Format("[RK29] restored %1 into %2 instead",
				FilePath.StripPath("" + seatedPrefab), FileNameOf(weapon)), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	//! The first slot whose container is a muzzle rather than an attachment point - vanilla's own
	//! test for a magazine well.
	protected static InventoryStorageSlot FindMuzzleSlot(BaseInventoryStorageComponent storage)
	{
		if (!storage)
			return null;

		for (int i = 0, n = storage.GetSlotsCount(); i < n; i++)
		{
			InventoryStorageSlot slot = storage.GetSlot(i);
			if (!slot)
				continue;
			if (BaseMuzzleComponent.Cast(slot.GetParentContainer()))
				return slot;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! The weapon's own magazine well. On an M203/GP-25 rifle the launcher is a separate attached
	//! entity with its own muzzle, so the rifle's well is the one whose muzzle component the rifle
	//! owns - which keeps a rifle magazine out of a 40mm tube. Falls back to the first muzzle slot.
	protected static InventoryStorageSlot FindOwnMuzzleSlot(notnull IEntity weapon, BaseInventoryStorageComponent storage)
	{
		if (!storage)
			return null;

		for (int i = 0, n = storage.GetSlotsCount(); i < n; i++)
		{
			InventoryStorageSlot slot = storage.GetSlot(i);
			if (!slot)
				continue;
			BaseMuzzleComponent muzzle = BaseMuzzleComponent.Cast(slot.GetParentContainer());
			if (muzzle && muzzle.GetOwner() == weapon)
				return slot;
		}
		return FindMuzzleSlot(storage);
	}

	//------------------------------------------------------------------------------------------------
	//! The underbarrel launcher's chamber, and the storage owning it (`outStorage`, usually not the
	//! host's). An integral launcher registers its muzzle-in-magazine slot onto the host, so that is
	//! looked for first; otherwise the first attachment with a muzzle-parented slot - only a
	//! launcher has one.
	protected static InventoryStorageSlot FindUnderbarrelMuzzleSlot(
		notnull BaseInventoryStorageComponent weaponStorage, out BaseInventoryStorageComponent outStorage)
	{
		outStorage = weaponStorage;

		for (int i = 0, n = weaponStorage.GetSlotsCount(); i < n; i++)
		{
			InventoryStorageSlot slot = weaponStorage.GetSlot(i);
			if (!slot)
				continue;
			if (MuzzleInMagComponent.Cast(slot.GetParentContainer()))
				return slot;
		}

		for (int a = 0, an = weaponStorage.GetSlotsCount(); a < an; a++)
		{
			InventoryStorageSlot attachSlot = weaponStorage.GetSlot(a);
			if (!attachSlot)
				continue;
			IEntity attached = attachSlot.GetAttachedEntity();
			if (!attached)
				continue;

			BaseInventoryStorageComponent attachStorage = BaseInventoryStorageComponent.Cast(
				attached.FindComponent(BaseInventoryStorageComponent));
			InventoryStorageSlot found = FindMuzzleSlot(attachStorage);
			if (found)
			{
				outStorage = attachStorage;
				return found;
			}
		}

		outStorage = null;
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Occupancy of the weapon slot with this id, for the refusal log - occupied means the strip
	//! failed, empty means the weapon cannot go in that slot. Through the weapon manager, not
	//! weaponStorage.GetSlot(id): that takes a list position, and a slot ID means GetWeaponSlotIndex().
	protected static string SlotOccupancy(IEntity character, int slotId)
	{
		ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
		if (!chimera || !chimera.GetCharacterController())
			return "occupancy unknown";
		BaseWeaponManagerComponent wm = chimera.GetCharacterController().GetWeaponManagerComponent();
		if (!wm)
			return "occupancy unknown";

		array<WeaponSlotComponent> slots = {};
		wm.GetWeaponsSlots(slots);
		foreach (WeaponSlotComponent ws : slots)
		{
			if (!ws || ws.GetWeaponSlotIndex() != slotId)
				continue;
			IEntity occupant = ws.GetWeaponEntity();
			if (occupant)
				return "still occupied by " + FileNameOf(occupant);
			return "slot is empty - the weapon does not fit it";
		}
		return "no slot with that id on this body";
	}

	// ======================================================================== placement

	//------------------------------------------------------------------------------------------------
	//! The placement model. Containers are the body's cargo storages the inventory manager owns,
	//! emitted highest engine storage priority first; an EquipmentStorageComponent becomes one
	//! container per named slot, because it only ever accepts an item asked for by that slot. Each
	//! inherits the kind of the garment it hangs on - rig, pack, trouser, uniform, other - so a
	//! batch's preference list reads the way people speak ("bandages in the uniform"). Items go in
	//! most-constrained-first, ties to the physically largest: the order is geometry, never
	//! importance, and the authored keep rank decides only what gives way. Among containers that
	//! accept an item, the score protects space scarce items still need, then prefers a free named
	//! mount, then the authored preference, then keeping a small stack together (magazines sit
	//! cohesion out). The ladder per item is ChooseContainer -> EvictAndPlace (one occupant moves
	//! aside) -> MakeRoomByRank (something less important comes out, one at a time and from one
	//! container) -> one last retry of everything displaced, with no right to displace in turn.
	//! Only then is it a drop.

	//------------------------------------------------------------------------------------------------
	//! Largest-first so bulky items claim pouch space before filler. Prefabs that fit nowhere land
	//! in droppedItems.
	protected static void DressItems(SCR_InventoryStorageManagerComponent manager, EquipedWeaponStorageComponent weaponStorage, IEntity character, RK29_KitStruct kit, array<ResourceName> droppedItems)
	{
		// one grenade rides the throwable slot instead of cargo - free capacity that prevents pouch
		// overfill. Frags first to mirror authored priming.
		ResourceName primed = PrimeGrenadeSlot(manager, weaponStorage, character, kit);

		// one state object from here down: index-aligned columns of one table, never loose
		// parallel arrays a sort can separate
		RK29_PlacementState st = new RK29_PlacementState();
		CollectCargoContainers(character, kit, st);

		bool primedSkipped = primed == ResourceName.Empty;
		foreach (RK29_KitItemBatch batch : kit.m_aItems)
		{
			foreach (ResourceName item : batch.m_aPrefabs)
			{
				// exactly one copy: the one already riding the throwable slot
				if (!primedSkipped && item == primed)
				{
					primedSkipped = true;
					continue;
				}
				st.m_aItems.Insert(item);
				st.m_aPrefs.Insert(batch.m_aPreferred);
				st.m_aRanks.Insert(batch.m_iKeepRank);
			}
		}

		// no plan cache: replaying an earlier solve saved a few ms of a fifteen-ms apply - the
		// entity churn is the cost, not the solve - and was a second path that could disagree
		SolvePlacement(manager, st, droppedItems);

		if (!droppedItems.IsEmpty())
			Note(string.Format("[RK29] %1 item(s) did not fit and were dropped",
				droppedItems.Count()), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	//! Cargo containers on the body in a deterministic order. What counts as one is IsCargoStorage,
	//! shared with the strip that empties them; the one exclusion this pass adds is stated below.
	protected static void CollectCargoContainers(IEntity character, RK29_KitStruct kit, notnull RK29_PlacementState st)
	{
		map<string, string> garmentKind = new map<string, string>();
		foreach (string slotName, ResourceName garment : kit.m_mClothing)
			garmentKind.Set(FilePath.StripPath("" + garment), KindOfDressSlot(slotName));

		array<BaseInventoryStorageComponent> all = {};
		CollectBodyStorages(character, all);

		// What the inventory manager owns. The Lifchik GL vest's grenade-belt accessory carries a
		// universal storage the manager never registers: it accepts anything, shows nothing, and
		// everything spawned into it is gone by the next frame - fifteen magazines went there.
		SCR_InventoryStorageManagerComponent manager = SCR_InventoryStorageManagerComponent.Cast(
			character.FindComponent(SCR_InventoryStorageManagerComponent));
		array<BaseInventoryStorageComponent> managed = {};
		if (manager)
			manager.GetStorages(managed);

		array<BaseInventoryStorageComponent> rawContainers = {};
		array<int> rawSlotIds = {};
		array<string> rawKeys = {};
		array<string> rawKinds = {};
		array<int> rawPrios = {};

		map<string, int> seen = new map<string, int>();
		foreach (BaseInventoryStorageComponent storage : all)
		{
			if (!IsCargoStorage(storage, character))
				continue;
			// the one exclusion this pass adds over the shared predicate, for the reason above
			if (manager && !managed.Contains(storage))
			{
				RK29_Log.Trace("[RK29] container skipped - not managed by the inventory: "
					+ OwnerFileName(storage));
				continue;
			}

			string owner = OwnerFileName(storage);
			int ordinal = 0;
			seen.Find(owner, ordinal);
			seen.Set(owner, ordinal + 1);

			string kind = KindOfContainer(storage, garmentKind);

			// equipment storages are named typed slots (the ALICE flashlight strap, a scabbard
			// mount): they accept an item only when asked for that specific slot, never as a bag,
			// so each slot becomes its own container here
			if (EquipmentStorageComponent.Cast(storage))
			{
				for (int s = 0, sn = storage.GetSlotsCount(); s < sn; s++)
				{
					InventoryStorageSlot namedSlot = storage.GetSlot(s);
					if (!namedSlot)
						continue;

					rawContainers.Insert(storage);
					rawSlotIds.Insert(namedSlot.GetID());
					rawKeys.Insert(owner + "#" + ordinal.ToString() + "/" + namedSlot.GetSourceName());
					rawKinds.Insert(kind);
					rawPrios.Insert(storage.GetPriority());
				}
				continue;
			}

			rawContainers.Insert(storage);
			rawSlotIds.Insert(-1);
			rawKeys.Insert(owner + "#" + ordinal.ToString());
			rawKinds.Insert(kind);
			rawPrios.Insert(storage.GetPriority());
		}

		EmitByPriority(st, rawContainers, rawSlotIds, rawKeys, rawKinds, rawPrios);
	}

	//------------------------------------------------------------------------------------------------
	//! The collected containers into the state, highest engine storage priority first and
	//! body-traversal order among equals. Pressing R walks the body the same way and takes the first
	//! compatible magazine, so filling in this order leaves the last-declared magazines - the
	//! tracers - where the reload search reaches them last. A selection sort: five aligned columns.
	protected static void EmitByPriority(notnull RK29_PlacementState st,
		notnull array<BaseInventoryStorageComponent> rawContainers, notnull array<int> rawSlotIds,
		notnull array<string> rawKeys, notnull array<string> rawKinds, notnull array<int> rawPrios)
	{
		int nRaw = rawContainers.Count();
		array<bool> emitted = {};
		for (int i = 0; i < nRaw; i++)
			emitted.Insert(false);

		for (int e = 0; e < nRaw; e++)
		{
			int pick = -1;
			for (int i = 0; i < nRaw; i++)
			{
				if (emitted[i])
					continue;
				if (pick == -1 || rawPrios[i] > rawPrios[pick])
					pick = i;
			}
			if (pick == -1)
				break;

			emitted[pick] = true;
			st.m_aContainers.Insert(rawContainers[pick]);
			st.m_aSlotIds.Insert(rawSlotIds[pick]);
			st.m_aKeys.Insert(rawKeys[pick]);
			st.m_aKinds.Insert(rawKinds[pick]);
			RK29_Log.Trace(string.Format("[RK29] container %1 priority=%2 %3", e, rawPrios[pick], rawKeys[pick]));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The five names are the body prefab's dress slots, as the kit authors them; a slot spelt any
	//! other way degrades to KIND_OTHER, which costs the container its preference, not its use.
	protected static string KindOfDressSlot(string slotName)
	{
		if (slotName == "Vest" || slotName == "ArmoredVest")
			return KIND_RIG;
		if (slotName == "Back")
			return KIND_PACK;
		if (slotName == "Pants")
			return KIND_TROUSER;
		if (slotName == "Jacket")
			return KIND_UNIFORM;
		return KIND_OTHER;
	}

	//------------------------------------------------------------------------------------------------
	//! A container inherits the kind of the garment it hangs on, so preferences can be written the
	//! way people say them ("bandages in the uniform") instead of by filename.
	protected static string KindOfContainer(BaseInventoryStorageComponent storage, map<string, string> garmentKind)
	{
		// climb by containment, not by entity parent: everything worn is parented to the character,
		// so a mag pouch only finds its vest by asking which storage holds it
		IEntity e = storage.GetOwner();
		int guard = 0;
		while (e && guard < 8)
		{
			string kind;
			if (garmentKind.Find(FileNameOf(e), kind))
				return kind;

			InventoryItemComponent iic = InventoryItemComponent.Cast(e.FindComponent(InventoryItemComponent));
			if (!iic)
				break;
			InventoryStorageSlot parentSlot = iic.GetParentSlot();
			if (!parentSlot || !parentSlot.GetStorage())
				break;

			e = parentSlot.GetStorage().GetOwner();
			guard++;
		}
		return KIND_OTHER;
	}

	//------------------------------------------------------------------------------------------------
	//! Where this kind of item would ideally live, best first. Only a tie-break: feasibility and
	//! scarcity decide first, so a preference can never cost an item its place - which is the only
	//! reason the path test below is acceptable. See StacksTogether for what a path test costs
	//! where it actually decides something.
	protected static array<string> PreferredKinds(ResourceName item)
	{
		string path = "" + item;

		// launcher rounds and explosives are bulk: the pack, then the rig, pockets last
		if (path.Contains("/Launchers/") || path.Contains("Ammo_Rocket")
			|| path.Contains("/Explosives/"))
			return { KIND_PACK, KIND_RIG, KIND_TROUSER, KIND_UNIFORM };

		// fighting load rides the rig and works outward; what you reach for between fights sits
		// the other way round, so the rig stays free for ammo
		bool combat = path.Contains("/Magazines/") || path.Contains("/Ammo/")
			|| path.Contains("/Grenades/") || path.Contains("/Flares/");
		if (combat)
			return { KIND_RIG, KIND_PACK, KIND_TROUSER, KIND_UNIFORM };

		return { KIND_UNIFORM, KIND_TROUSER, KIND_PACK, KIND_RIG };
	}

	//------------------------------------------------------------------------------------------------
	//! Constraint-first placement, eligibility from the engine's own fit test so no item or container
	//! type knowledge lives here. Items with the fewest homes go first, and among containers that
	//! accept an item we avoid space still-unplaced scarce items depend on.
	protected static void SolvePlacement(SCR_InventoryStorageManagerComponent manager, notnull RK29_PlacementState st, array<ResourceName> droppedItems)
	{
		BuildEligibility(manager, st);

		array<int> order = {};
		OrderByConstraint(st, order);

		foreach (int idx : order)
		{
			if (!PlaceOne(manager, st, idx, true))
			{
				droppedItems.Insert(st.m_aItems[idx]);
				Note(string.Format("[RK29] dropped: %1", st.m_aItems[idx]), LogLevel.WARNING);
			}
		}

		// what gave way is tried again, without the right to displace anything itself: the body is
		// in its final shape now. Only then is it a drop.
		foreach (int again : st.m_aDisplaced)
		{
			if (st.m_aPlaced[again])
				continue;
			if (!PlaceOne(manager, st, again, false))
			{
				droppedItems.Insert(st.m_aItems[again]);
				Note(string.Format("[RK29] dropped (gave way): %1", st.m_aItems[again]), LogLevel.WARNING);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Which containers each item may go in, asked of the engine's own fit test. Sizes the per-item
	//! columns of the state at the same time, being the first pass that knows how many items exist.
	protected static void BuildEligibility(SCR_InventoryStorageManagerComponent manager, notnull RK29_PlacementState st)
	{
		int nItems = st.m_aItems.Count();
		int nCont = st.m_aContainers.Count();

		for (int i = 0; i < nItems; i++)
		{
			array<int> fits = {};
			for (int c = 0; c < nCont; c++)
			{
				if (manager.CanInsertResourceInStorage(st.m_aItems[i], st.m_aContainers[c], st.m_aSlotIds[c]))
					fits.Insert(c);
			}
			st.m_aEligible.Insert(fits);

			st.m_aPlaced.Insert(false);
			st.m_aHome.Insert(-1);
			st.m_aSpawned.Insert(null);
			st.m_aSeq.Insert(-1);
		}

		// one line per container, with how many distinct items of this kit the engine says it
		// accepts: one means a dedicated home, zero means dead weight. Only counted when it prints.
		if (RK29_Log.s_bVerbose)
		{
			for (int c = 0; c < nCont; c++)
			{
				array<ResourceName> distinct = {};
				for (int i = 0; i < nItems; i++)
				{
					if (st.m_aEligible[i].Contains(c) && !distinct.Contains(st.m_aItems[i]))
						distinct.Insert(st.m_aItems[i]);
				}
				RK29_Log.Trace(string.Format("[RK29] container %1 [%2] slot=%3 accepts=%4", st.m_aKeys[c], st.m_aKinds[c], st.m_aSlotIds[c], distinct.Count()));
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Placement order is geometry, not importance. Most constrained first, ties by physically
	//! largest (what MaxItemSize gates on). The authored keep rank never touches this order - it
	//! decides what gives way (MakeRoomByRank). Ranking the insertion instead was tried and is
	//! exactly wrong: the fattest items are authored last, and last-in on a fragmented body is never
	//! in. Insertion sort, because it is stable.
	protected static void OrderByConstraint(notnull RK29_PlacementState st, notnull array<int> outOrder)
	{
		for (int i = 0, nItems = st.m_aItems.Count(); i < nItems; i++)
		{
			int at = outOrder.Count();
			for (int o = 0, on = outOrder.Count(); o < on; o++)
			{
				int j = outOrder[o];
				bool fewer = st.m_aEligible[i].Count() < st.m_aEligible[j].Count();
				bool sameButBigger = st.m_aEligible[i].Count() == st.m_aEligible[j].Count()
					&& ItemMaxDimension(st.m_aItems[i]) > ItemMaxDimension(st.m_aItems[j]);
				if (fewer || sameButBigger)
				{
					at = o;
					break;
				}
			}
			// InsertAt's contract wants an index below Count(); the last position appends instead
			if (at < outOrder.Count())
				outOrder.InsertAt(i, at);
			else
				outOrder.Insert(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One item into the body: the container the rules choose, else room made by moving an occupant
	//! (EvictAndPlace), else - when allowed - room made by taking out something less important
	//! (MakeRoomByRank). False means it goes nowhere.
	protected static bool PlaceOne(SCR_InventoryStorageManagerComponent manager, notnull RK29_PlacementState st, int idx, bool mayDisplace)
	{
		ResourceName item = st.m_aItems[idx];

		int chosen = ChooseContainer(manager, st, idx);
		if (chosen == -1)
			chosen = EvictAndPlace(manager, st, idx);

		// still nowhere: something less important gives way, one item at a time. Victims come from
		// One container while it has any - room only adds up inside a single container, so emptying
		// a pouch of a frag and the pack of a smoke frees two half-spaces that fit nothing.
		int roomIn = -1;
		while (chosen == -1 && mayDisplace)
		{
			int victim = MakeRoomByRank(manager, st, idx, roomIn);
			if (victim == -1)
				break;
			st.m_aDisplaced.Insert(victim);
			chosen = ChooseContainer(manager, st, idx);
		}

		if (chosen == -1)
			return false;

		SCR_AITakeItemFromArsenal_InventoryCallback cb = new SCR_AITakeItemFromArsenal_InventoryCallback();
		if (!manager.TrySpawnPrefabToStorage(item, st.m_aContainers[chosen], st.m_aSlotIds[chosen], cb: cb))
		{
			Note(string.Format("[RK29] insert refused: %1 -> %2", item, st.m_aKeys[chosen]),
				LogLevel.WARNING);
			return false;
		}

		st.m_aPlaced[idx] = true;
		st.m_aHome[idx] = chosen;
		st.m_aSeq[idx] = st.m_iNextSeq;
		st.m_iNextSeq++;
		// the entity, so it can be taken back out later. The callback answers through replication,
		// which a client-side preview body may not have, so the container is asked directly then.
		IEntity got = cb.GetEntity();
		if (!got)
			got = NewestItemIn(st.m_aContainers[chosen], item, st.m_aSpawned);
		st.m_aSpawned[idx] = got;
		st.m_mStackHome.Set(item, chosen);
		RK29_Log.Trace("[RK29] placed: " + FilePath.StripPath("" + item) + " -> " + st.m_aKeys[chosen]);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! The item of this prefab that no placed index accounts for - the one just spawned. Scanned
	//! from the back, where the newest arrival sits.
	protected static IEntity NewestItemIn(BaseInventoryStorageComponent container, ResourceName prefab, array<IEntity> tracked)
	{
		if (!container)
			return null;
		array<IEntity> held = {};
		container.GetAll(held, false);   // its own slots, not what sits in pouches hung on it
		for (int i = held.Count() - 1; i >= 0; i--)
		{
			IEntity e = held[i];
			if (!e || tracked.Contains(e))
				continue;
			if (PrefabOf(e) == prefab)
				return e;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Takes one placed item out to make room for `idx`: the least important thing (highest keep
	//! rank, strictly above idx's own) in a container idx could use; among equals the one placed
	//! last, so a stack is eaten from its tail. Returns the item index removed, or -1.
	//! `roomIn` is the container the previous victim left, passed back in for a repeat because freed
	//! space only adds up within one container; it comes back as the container this victim left.
	//! The entity comes out through the manager so the bookkeeping is the engine's own; the stack
	//! home goes with it.
	protected static int MakeRoomByRank(SCR_InventoryStorageManagerComponent manager, notnull RK29_PlacementState st, int idx, inout int roomIn)
	{
		int victim = PickVictim(st, idx, roomIn);
		if (victim == -1 && roomIn != -1)
			victim = PickVictim(st, idx, -1);
		if (victim == -1)
			return -1;

		int from = st.m_aHome[victim];
		IEntity leaving = st.m_aSpawned[victim];
		ForceDelete(manager, leaving);

		string homeKey = st.m_aKeys[from];
		string victimFile = FilePath.StripPath("" + st.m_aItems[victim]);
		RK29_Log.Trace("[RK29] gave way: " + victimFile + " (rank " + st.m_aRanks[victim].ToString()
			+ ") out of " + homeKey + " for " + FilePath.StripPath("" + st.m_aItems[idx])
			+ " (rank " + st.m_aRanks[idx].ToString() + ")");

		st.m_aPlaced[victim] = false;
		st.m_aHome[victim] = -1;
		st.m_aSpawned[victim] = null;
		st.m_aSeq[victim] = -1;

		// a stack whose last copy just left a container must not keep steering copies there
		int stackAt;
		if (st.m_mStackHome.Find(st.m_aItems[victim], stackAt) && stackAt == from)
		{
			bool anyLeft = false;
			for (int j = 0, n = st.m_aItems.Count(); j < n && !anyLeft; j++)
				anyLeft = st.m_aPlaced[j] && st.m_aHome[j] == from && st.m_aItems[j] == st.m_aItems[victim];
			if (!anyLeft)
				st.m_mStackHome.Remove(st.m_aItems[victim]);
		}

		roomIn = from;
		return victim;
	}

	//------------------------------------------------------------------------------------------------
	//! The victim MakeRoomByRank would take: placed, strictly less important than idx, in a container
	//! idx could use (and in `onlyIn` when that names one); highest rank first, then placed latest.
	protected static int PickVictim(notnull RK29_PlacementState st, int idx, int onlyIn)
	{
		int victim = -1;
		for (int j = 0, n = st.m_aItems.Count(); j < n; j++)
		{
			if (!st.m_aPlaced[j] || st.m_aRanks[j] <= st.m_aRanks[idx] || !st.m_aSpawned[j])
				continue;
			if (onlyIn != -1 && st.m_aHome[j] != onlyIn)
				continue;
			if (!st.m_aEligible[idx].Contains(st.m_aHome[j]))
				continue;
			if (victim == -1 || st.m_aRanks[j] > st.m_aRanks[victim]
				|| (st.m_aRanks[j] == st.m_aRanks[victim] && st.m_aSeq[j] > st.m_aSeq[victim]))
				victim = j;
		}
		return victim;
	}

	//------------------------------------------------------------------------------------------------
	//! Among containers that accept the item right now: protect space that scarce unplaced
	//! items still need, then honour the category preference.
	protected static int ChooseContainer(SCR_InventoryStorageManagerComponent manager, notnull RK29_PlacementState st, int idx)
	{
		ResourceName item = st.m_aItems[idx];

		// how much of this item is still to come - a stack would rather live in one place
		int remaining = 0;
		for (int r = 0, rn = st.m_aItems.Count(); r < rn; r++)
		{
			if (!st.m_aPlaced[r] && st.m_aItems[r] == item)
				remaining++;
		}

		int home = -1;
		st.m_mStackHome.Find(item, home);
		// authored placement wins; otherwise the category default
		array<string> wanted = st.m_aPrefs[idx];
		if (!wanted || wanted.IsEmpty())
			wanted = PreferredKinds(item);

		// an item with barely anywhere to go should take the space it needs rather than politely
		// avoiding it, or belt boxes would step aside for each other and end up in the pack
		bool itemIsScarce = st.m_aEligible[idx].Count() <= 2;

		// magazines pack strictly best-container-first instead of clumping - see StacksTogether()
		bool stacks = StacksTogether(item);

		// The score, strongest field first; lower wins each field in turn, and a full tie goes to
		// the earliest container - which CollectCargoContainers ordered by storage priority.
		//   [0] penalty  - never strand a scarce item
		//   [1] tier     - a mount is free capacity
		//   [2] listed   - stay somewhere the item was said to belong
		//   [3] cohesion - join the rest of your stack (magazines: never)
		//   [4] whole    - start a stack where all of it fits (magazines: never)
		//   [5] detail   - uniform before trouser, outer mounts first
		array<int> score = { 0, 0, 0, 0, 0, 0 };
		array<int> bestScore = { 0, 0, 0, 0, 0, 0 };
		int best = -1;

		foreach (int c : st.m_aEligible[idx])
		{
			if (!manager.CanInsertResourceInStorage(item, st.m_aContainers[c], st.m_aSlotIds[c]))
				continue;

			int penalty = 0;
			if (!itemIsScarce)
			{
				for (int j = 0, n = st.m_aItems.Count(); j < n; j++)
				{
					if (j == idx || st.m_aPlaced[j])
						continue;
					if (st.m_aEligible[j].Count() <= 2 && st.m_aEligible[j].Contains(c))
						penalty++;
				}
			}

			// a named slot is a purpose-built mount: it costs no cargo volume, so taking it is free
			// capacity rather than a matter of taste. Between two mounts the outer layer wins.
			int tier = 1;
			int detail = PreferenceRank(wanted, st.m_aKeys[c], st.m_aKinds[c]);
			int listed = 1;
			if (detail < PREF_UNLISTED)
				listed = 0; // somewhere the item was actually said to belong
			if (st.m_aSlotIds[c] != -1)
			{
				tier = 0;
				detail = LayerRank(st.m_aKinds[c]);
				listed = 0;
			}

			// keep a stack together when nothing more important disagrees. Below scarcity, mounts
			// and preference, so neither can push an item somewhere it should not go. Magazines
			// sit both rules out.
			int cohesion = 1;
			if (stacks && home == c)
				cohesion = 0;

			// keeping a stack whole only outranks preference for small stacks: a pair of
			// tourniquets belongs in one pocket, seven magazines belong in the mag pouches
			int whole = 1;
			if (stacks
				&& remaining <= COHESION_MAX_STACK
				&& st.m_aContainers[c].GetEstimatedCountFitForResource(item) >= remaining)
				whole = 0;

			score[0] = penalty;
			score[1] = tier;
			score[2] = listed;
			score[3] = cohesion;
			score[4] = whole;
			score[5] = detail;

			bool better = best == -1;
			if (!better)
			{
				for (int s = 0, sn = score.Count(); s < sn; s++)
				{
					if (score[s] == bestScore[s])
						continue;
					better = score[s] < bestScore[s];
					break;
				}
			}

			if (better)
			{
				best = c;
				for (int s = 0, sn = score.Count(); s < sn; s++)
					bestScore[s] = score[s];
			}
		}
		return best;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether a stack of this item wants to live in one container. Magazines are the one class whose
	//! order survives the apply: pressing R walks the body by storage priority and loads the first
	//! compatible magazine, so cohesion would let a small last-declared stack (the two tracers) claim
	//! a fresh high-priority pouch and be first up the spout. Medical and gadgets still stack.
	//! "Is this a magazine" is asked of the prefab (RK29_KitCompose.IsMagazine), never of where it is
	//! filed: the SMAW's rounds live under Weapons/Magazines and the RPG's under Weapons/Ammo, so a
	//! path test packs two launchers' rockets by opposite rules for a filesystem reason. An
	//! unreadable prefab keeps the path answer rather than being declared not-a-magazine; cached.
	protected static bool StacksTogether(ResourceName item)
	{
		if (!RK29_KitCompose.PrefabReadable(item))
			return !("" + item).Contains("/Magazines/");

		return !RK29_KitCompose.IsMagazine(item);
	}

	//------------------------------------------------------------------------------------------------
	//! Outermost first, to pick between competing mounts: what hangs on the rig is visible and
	//! reachable, what sits inside a jacket is neither.
	protected static int LayerRank(string kind)
	{
		if (kind == KIND_RIG)
			return 0;
		if (kind == KIND_PACK)
			return 1;
		if (kind == KIND_UNIFORM)
			return 2;
		if (kind == KIND_TROUSER)
			return 3;
		return 4;
	}

	//------------------------------------------------------------------------------------------------
	//! How well a container answers an authored preference list. A token matches a container kind, a
	//! named slot (FlashlightSlot, Etool), or any part of the container's own name. Lower is better;
	//! unlisted containers rank last but stay usable.
	protected static int PreferenceRank(array<string> wanted, string containerKey, string kind)
	{
		if (!wanted)
			return PREF_UNLISTED;

		for (int i = 0, n = wanted.Count(); i < n; i++)
		{
			string token = wanted[i];
			if (token == "")
				continue;
			if (token == kind)
				return i;

			// "owner/slot" pins both halves without depending on the ordinal in the key, so
			// "suspenders/FlashlightSlot" means that mount and not the jacket's
			array<string> parts = {};
			token.Split("/", parts, true);

			bool all = true;
			foreach (string part : parts)
			{
				if (part != "" && !containerKey.Contains(part))
				{
					all = false;
					break;
				}
			}
			if (all)
				return i;
		}
		return PREF_UNLISTED;
	}

	//------------------------------------------------------------------------------------------------
	//! Last resort before dropping: an item that fits nowhere may still fit once something smaller
	//! vacates a container it did not need. Only a container's own slots are candidates - GetAll
	//! recurses into pouches hung on a vest, and pulling an item out of one to "put it back" in the
	//! vest is a relocation nobody asked for.
	protected static int EvictAndPlace(SCR_InventoryStorageManagerComponent manager, notnull RK29_PlacementState st, int idx)
	{
		foreach (int c : st.m_aEligible[idx])
		{
			array<IEntity> occupants = {};
			st.m_aContainers[c].GetAll(occupants, false);
			foreach (IEntity occupant : occupants)
			{
				if (occupant && TryRelocate(manager, st, idx, c, occupant))
					return c;
			}
		}
		return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! Move one occupant out of container `c` and see whether that made room for item `idx`. True
	//! only when it did; a move that did not help is undone, or the kit lands with its bandages in
	//! the ruck for an item that was dropped anyway. The undo goes back to the occupant's own slot:
	//! a named-slot entry speaks for one slot of a storage GetAll enumerates whole.
	protected static bool TryRelocate(SCR_InventoryStorageManagerComponent manager, notnull RK29_PlacementState st, int idx, int c, notnull IEntity occupant)
	{
		int homeSlot = st.m_aSlotIds[c];
		if (homeSlot != -1)
		{
			InventoryItemComponent occupantItem = InventoryItemComponent.Cast(
				occupant.FindComponent(InventoryItemComponent));
			if (occupantItem && occupantItem.GetParentSlot())
				homeSlot = occupantItem.GetParentSlot().GetID();
		}

		for (int d = 0, n = st.m_aContainers.Count(); d < n; d++)
		{
			if (d == c || !manager.CanInsertItemInStorage(occupant, st.m_aContainers[d], st.m_aSlotIds[d]))
				continue;
			if (!manager.TryInsertItemInStorage(occupant, st.m_aContainers[d], st.m_aSlotIds[d]))
				continue;

			if (manager.CanInsertResourceInStorage(st.m_aItems[idx], st.m_aContainers[c], st.m_aSlotIds[c]))
			{
				int moved = st.m_aSpawned.Find(occupant);
				if (moved != -1)
					st.m_aHome[moved] = d;
				RK29_Log.Trace("[RK29] evicted " + FileNameOf(occupant) + " to make room for "
					+ FilePath.StripPath("" + st.m_aItems[idx]));
				return true;
			}

			if (!manager.TryInsertItemInStorage(occupant, st.m_aContainers[c], homeSlot))
			{
				Note(string.Format("[RK29] eviction undo failed - %1 stays where it was"
					+ " moved to", FileNameOf(occupant)), LogLevel.WARNING);
				int stranded = st.m_aSpawned.Find(occupant);
				if (stranded != -1)
					st.m_aHome[stranded] = d;
			}
			return false;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Largest authored side, in cm. Containers gate on this (MaxItemSize), not on volume, so it
	//! decides how scarce an item's eligible space is. The entity source is the fully merged view -
	//! one read, no ancestry walk. -1 is "no size authored", cached as such: an item without one is
	//! not an item of size zero, and caching it as one puts it level with the smallest real item in
	//! the size tie-break. A prefab that would not load is not cached at all - a cold cache can
	//! refuse the first load and answer the second, and remembering the refusal would make it
	//! permanent (see RK29_KitCompose.ReadZoomRange).
	protected static float ItemMaxDimension(ResourceName prefab)
	{
		float dim;
		if (s_mDimCache.Find(prefab, dim))
			return dim;

		dim = -1;
		bool loaded = false;
		Resource res = Resource.Load(prefab);
		if (res && res.IsValid())
		{
			IEntitySource src = res.GetResource().ToEntitySource();
			if (src)
			{
				loaded = true;
				for (int i = 0, n = src.GetComponentCount(); i < n; i++)
				{
					IEntityComponentSource comp = src.GetComponent(i);
					if (!comp || !comp.GetClassName().Contains("InventoryItemComponent"))
						continue;

					BaseContainer attr = comp.GetObject("Attributes");
					if (attr)
					{
						BaseContainer phys = attr.GetObject("ItemPhysAttributes");
						if (phys)
						{
							vector dims;
							if (phys.Get("ItemDimensions", dims))
								dim = Math.Max(dims[0], Math.Max(dims[1], dims[2]));
						}
					}
					break;
				}
			}
		}

		if (loaded)
			s_mDimCache.Set(prefab, dim);
		return dim;
	}

	//------------------------------------------------------------------------------------------------
	//! If the throwable slot is empty and the kit carries grenades, put one there (frag preferred).
	//! Returns the prefab primed so the caller can leave it out of the cargo pass. "Frag" is read
	//! off the file name (a `Grenade_` prefix), which is vanilla's naming convention and not a rule:
	//! a grenade named otherwise is still primed, just without the preference. It must not be
	//! removed from the batches: the kit struct handed in can be a shared, session-lived object (the
	//! boot-composed m_mKits entry), and editing it loses the kit a grenade on every later apply.
	protected static ResourceName PrimeGrenadeSlot(SCR_InventoryStorageManagerComponent manager, EquipedWeaponStorageComponent weaponStorage, IEntity character, RK29_KitStruct kit)
	{
		if (!weaponStorage)
			return ResourceName.Empty;

		CharacterGrenadeSlotComponent grenadeSlot = CharacterGrenadeSlotComponent.Cast(
			character.FindComponent(CharacterGrenadeSlotComponent));
		if (!grenadeSlot || grenadeSlot.GetWeaponEntity())
			return ResourceName.Empty; // no slot, or already occupied (kit-authored keep)

		RK29_KitItemBatch bestBatch;
		int bestIdx = -1;
		bool bestIsFrag = false;
		foreach (RK29_KitItemBatch batch : kit.m_aItems)
		{
			for (int i = 0, n = batch.m_aPrefabs.Count(); i < n; i++)
			{
				string path = "" + batch.m_aPrefabs[i];
				if (!path.Contains("/Grenades/"))
					continue;
				int slash = path.LastIndexOf("/");
				bool isFrag = path.IndexOfFrom(slash + 1, "Grenade_") == slash + 1;
				if (bestIdx == -1 || (isFrag && !bestIsFrag))
				{
					bestBatch = batch;
					bestIdx = i;
					bestIsFrag = isFrag;
				}
				if (bestIsFrag)
					break;
			}
			if (bestIsFrag)
				break;
		}
		if (bestIdx == -1)
			return ResourceName.Empty;

		ResourceName grenade = bestBatch.m_aPrefabs[bestIdx];
		if (!manager.TrySpawnPrefabToStorage(grenade, weaponStorage, -1))
			return ResourceName.Empty;

		RK29_Log.Trace("[RK29] primed grenade slot: " + FilePath.StripPath("" + grenade));
		return grenade;
	}

	//------------------------------------------------------------------------------------------------
	//! The prefab file name of whatever wears this storage - a container key and a log line, so an
	//! owner that cannot answer contributes nothing rather than a placeholder.
	protected static string OwnerFileName(BaseInventoryStorageComponent storage)
	{
		ResourceName prefab = PrefabOf(storage.GetOwner());
		if (prefab == ResourceName.Empty)
			return string.Empty;

		return FilePath.StripPath("" + prefab);
	}
}
