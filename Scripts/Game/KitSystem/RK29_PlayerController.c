//------------------------------------------------------------------------------------------------
//! Client -> server kit request bridge.
//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController
{
	//--------------------------------------------------------------------------------------------
	//! Empty weapon = authored weapon; empty optic = irons.
	void RK29_RequestKit(string kitName, ResourceName weapon, ResourceName optic)
	{
		// Put a held gadget away BEFORE asking, rather than after the re-dress. A gadget in the
		// hand is engine-attached instead of stored, so it is invisible to the server's strip
		// and survives the re-kit on top of the fresh one the dress pass spawns - two compasses,
		// since utility.conf gives every kit map/compass/flashlight/radio. Stowed here it is an
		// ordinary stored item by the time the request lands and the strip treats it like any
		// other.
		//
		// Straight to the controller, not SCR_GadgetManagerComponent.RemoveHeldGadget(): the
		// manager has no skip-animation entry point at all - SetGadgetMode hardcodes
		// RemoveGadgetFromHand(false) for anything that is not going to the ground - so going
		// through it means watching a putaway play out after you already pressed apply. Same
		// pair vanilla uses to clear the hand before a loiter command
		// (SCR_CharacterControllerComponent.StartLoitering), which is likewise owner-side.
		// The manager stays in sync either way: it learns the gadget left the hand from the
		// controller's own m_OnGadgetStateChangedInvoker, which fires whichever route took it
		// out. The one thing bypassing it costs is closing a raised/ADS'd gadget, and
		// SetGadgetRaisedModeWanted covers that half.
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(GetControlledEntity());
		if (character)
		{
			CharacterControllerComponent ctrl = character.GetCharacterController();
			if (ctrl && ctrl.IsGadgetInHands())
			{
				ctrl.SetGadgetRaisedModeWanted(false);
				ctrl.RemoveGadgetFromHand(true);
			}
		}

		Rpc(RK29_RpcAskKit, kitName, weapon, optic);
	}

	//--------------------------------------------------------------------------------------------
	override void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		super.OnControlledEntityChanged(from, to);

		if (to && GetGame().GetPlayerController() == this)
			RK29_KitPicker.RegisterListeners();
	}

	//--------------------------------------------------------------------------------------------
	void RK29_NotifyItemsDropped_S(int count, string itemList)
	{
		Rpc(RK29_RpcItemsDropped, count, itemList);
	}

	//--------------------------------------------------------------------------------------------
	void RK29_NotifyKitSaved_S(string kitName, ResourceName optic, string loadoutWire)
	{
		Rpc(RK29_RpcKitSaved, kitName, optic, loadoutWire);
	}

	//--------------------------------------------------------------------------------------------
	//! Ask the owning client to restore its state after a live re-kit: weapon back in hand,
	//! stance back where it was. Re-kit ONLY - once the client owns the body it holds authority
	//! over its own item commands, so this restore has to happen at this end. A spawn never
	//! sends this: the primary is equipped server-side in the same pass that dressed the body
	//! (vanilla-style, see RK29_KitManager.EquipPrimary_S) and arrives already in hand, because
	//! a draw the client only sees seconds after spawning is worthless to the player.
	//!
	//! `characterId` is the body the request is ABOUT. The client checks it before touching
	//! anything: for a few frames after a respawn the client is still driving the previous body,
	//! and a draw aimed at the wrong body is a draw thrown away.
	void RK29_NotifyRestoreState_S(int stance, float dynStance, RplId weaponId, RplId characterId)
	{
		Rpc(RK29_RpcRestoreState, stance, dynStance, weaponId, characterId);
	}

	//! How long to keep trying. The body is already here and the player is standing in the
	//! staging area, so this only has to outlast the re-dressed weapon's trip over the wire.
	protected static const float RK29_RESTORE_TIMEOUT_MS = 3000;

	//! Gap between equip attempts once the weapon is here. TryEquipRightHandItem can refuse -
	//! an item change already running, a graph still settling - and it says so in a return value
	//! the old code threw away. Every attempt is checked on the next tick against what is
	//! actually in the hand, and re-issued if it is not there.
	protected static const float RK29_EQUIP_RETRY_MS = 250;

	//! Poll interval. Fine-grained enough that the draw is not visibly late.
	protected static const int RK29_POLL_MS = 50;

	protected int   m_RK29_DrawStance;
	protected float m_RK29_DrawDynStance;
	protected RplId m_RK29_DrawWeaponId;
	protected RplId m_RK29_DrawCharacterId;
	protected bool  m_RK29_DrawStanceDone;
	protected float m_RK29_DrawDeadline;
	protected float m_RK29_DrawNextTry;

	//! Equip attempts made for the live request, and when it was armed. Only ever read to log a
	//! draw that needed more than the first try - which is the evidence to look at if this comes
	//! back, rather than another theory about why it did not happen.
	protected int   m_RK29_DrawAttempts;
	protected float m_RK29_DrawArmedAt;

	//--------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcRestoreState(int stance, float dynStance, RplId weaponId, RplId characterId)
	{
		m_RK29_DrawStance      = stance;
		m_RK29_DrawDynStance   = dynStance;
		m_RK29_DrawWeaponId    = weaponId;
		m_RK29_DrawCharacterId = characterId;
		m_RK29_DrawStanceDone  = false;
		m_RK29_DrawNextTry     = 0;
		m_RK29_DrawAttempts    = 0;

		m_RK29_DrawArmedAt  = GetGame().GetWorld().GetWorldTime();
		m_RK29_DrawDeadline = m_RK29_DrawArmedAt + RK29_RESTORE_TIMEOUT_MS;

		// State lives on the controller rather than in the callqueue arguments, so a second
		// request supersedes the first instead of leaving two polls racing each other.
		GetGame().GetCallqueue().Remove(RK29_PollRestore);
		GetGame().GetCallqueue().CallLater(RK29_PollRestore, RK29_POLL_MS, true);
	}

	//--------------------------------------------------------------------------------------------
	//! Wait for the body, then keep asking until the weapon is actually in the hand.
	//!
	//! The old version waited on the WEAPON only, for half a second, then called the restore once
	//! and never looked at the result. Both halves of that failed on a dedicated server: the wait
	//! ran out long before the client had a character at all, and the single equip attempt was
	//! unverified. This waits on the named body, restores the stance the moment that body arrives
	//! (the stance does not depend on the weapon, so a slow weapon must not hold it up), and
	//! treats the equip as a goal to be re-attempted rather than a call to be made once.
	protected void RK29_PollRestore()
	{
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(GetControlledEntity());
		CharacterControllerComponent ctrl;
		if (character && RK29_IsRequestedBody(character))
			ctrl = character.GetCharacterController();

		if (ctrl)
		{
			if (!m_RK29_DrawStanceDone)
			{
				m_RK29_DrawStanceDone = true;
				RK29_RestoreStance(ctrl);
			}

			if (RK29_TryDraw(character, ctrl))
			{
				RK29_StopDraw();

				// Silent when it works first time, which is the normal case. Anything else is
				// worth a line: it says the draw only landed because the retry was there.
				if (m_RK29_DrawAttempts > 1)
				{
					float took = GetGame().GetWorld().GetWorldTime() - m_RK29_DrawArmedAt;
					Print("[RK29] weapon drawn after " + m_RK29_DrawAttempts.ToString() + " attempts, "
						+ took.ToString(-1, 0) + "ms after the request", LogLevel.NORMAL);
				}
				return;
			}
		}

		if (GetGame().GetWorld().GetWorldTime() < m_RK29_DrawDeadline)
			return;

		RK29_StopDraw();

		// Ran out of patience: the weapon the player was holding was deleted by the strip, so
		// the animation graph is still pointing at a destroyed entity. Re-seat the hand empty
		// rather than leaving it there - it is the difference between "empty-handed" and
		// "empty-handed and stuck".
		if (ctrl)
			ctrl.TryEquipRightHandItem(null, EEquipItemType.EEquipTypeUnarmedDeliberate, true);

		// Giving up is not a failure to recover from: re-drawing is a convenience, re-kits are
		// preround-only, and the player can pull the weapon out themselves. Say WHICH half ran
		// out though - the three of them mean very different things, and guessing between them
		// from "it did not work" is what made this expensive to chase the first time.
		if (!ctrl)
			Print("[RK29] gave up restoring the weapon - the body was never ours on this client", LogLevel.WARNING);
		else if (m_RK29_DrawAttempts == 0)
			Print("[RK29] gave up restoring the weapon - it never replicated in", LogLevel.WARNING);
		else
			Print("[RK29] gave up restoring the weapon - " + m_RK29_DrawAttempts.ToString()
				+ " attempts and it never reached the hand", LogLevel.WARNING);
	}

	//--------------------------------------------------------------------------------------------
	protected void RK29_StopDraw()
	{
		GetGame().GetCallqueue().Remove(RK29_PollRestore);
	}

	//--------------------------------------------------------------------------------------------
	//! Is this the body the request named? An invalid id means the server could not name one, in
	//! which case whatever we are controlling has to do.
	protected bool RK29_IsRequestedBody(IEntity character)
	{
		if (!m_RK29_DrawCharacterId.IsValid())
			return true;

		RplComponent rpl = RplComponent.Cast(character.FindComponent(RplComponent));
		return rpl && rpl.Id() == m_RK29_DrawCharacterId;
	}

	//--------------------------------------------------------------------------------------------
	protected void RK29_RestoreStance(CharacterControllerComponent ctrl)
	{
		if (ctrl.GetStance() != m_RK29_DrawStance)
		{
			int change = ECharacterStanceChange.STANCECHANGE_TOERECTED;
			if (m_RK29_DrawStance == ECharacterStance.CROUCH)
				change = ECharacterStanceChange.STANCECHANGE_TOCROUCH;
			else if (m_RK29_DrawStance == ECharacterStance.PRONE)
				change = ECharacterStanceChange.STANCECHANGE_TOPRONE;
			if (ctrl.CanChangeStance(change))
				ctrl.SetStanceChange(change);
		}

		if (ctrl.CanSetDynamicStance(m_RK29_DrawDynStance))
			ctrl.SetDynamicStance(m_RK29_DrawDynStance);
	}

	//--------------------------------------------------------------------------------------------
	//! One tick of the draw. Returns true when the request is finished with - drawn, refused for
	//! a reason that will not change, or overtaken by the player.
	protected bool RK29_TryDraw(SCR_ChimeraCharacter character, CharacterControllerComponent ctrl)
	{
		// Nothing to draw for, and none of these un-become true while the request is alive.
		if (ctrl.IsDead() || ctrl.IsUnconscious() || character.IsInVehicle())
			return true;

		if (!m_RK29_DrawWeaponId.IsValid())
		{
			// Nothing was in hands to restore. Still not a no-op: the strip may have deleted the
			// throwable the player was holding, so the graph is left on a stale item state that
			// has to be re-seated empty.
			ctrl.TryEquipRightHandItem(null, EEquipItemType.EEquipTypeUnarmedDeliberate, true);
			return true;
		}

		// The id the server named is the only acceptable target. The body's previous weapon may
		// not have been reaped here yet, and if the re-kit handed back the same prefab the two
		// are indistinguishable by slot - guessing from the slots would sooner or later hand the
		// player the doomed one.
		IEntity weapon;
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(m_RK29_DrawWeaponId));
		if (rpl)
			weapon = rpl.GetEntity();
		if (!weapon)
			return false; // still streaming in - keep waiting

		if (ctrl.GetRightHandItem() == weapon)
			return true;

		float now = GetGame().GetWorld().GetWorldTime();
		if (now < m_RK29_DrawNextTry)
			return false;

		// Racing an item change already in flight is the competing-change class that jams the
		// graph in the first place. Let it finish; we are polling anyway.
		if (ctrl.IsChangingItem())
			return false;

		m_RK29_DrawNextTry = now + RK29_EQUIP_RETRY_MS;
		m_RK29_DrawAttempts = m_RK29_DrawAttempts + 1;

		// The weapon that was in hands got deleted out from under the animation graph, so the
		// item state here is stale whatever we do next. SelectWeapon() re-draws WITH the
		// switching animation, which is a transition out of that stale state - and it is the
		// one that sticks (weapon in hands, wrong pose, no ADS, until you swap manually).
		// TryEquipRightHandItem with swap = no animation at all: the item is snapped into
		// hands and the graph is re-seated instead of transitioned. Same call vanilla's own
		// live re-dress uses after it deletes and respawns a character's whole loadout
		// (SCR_PlayerArsenalLoadout.ApplyCharacterDataLoadoutString).
		ctrl.TryEquipRightHandItem(weapon, EEquipItemType.EEquipTypeWeapon, true);

		// Always low ready, whatever the player was doing before. The weapon they were holding
		// no longer exists - re-dressing deletes and respawns it - so "restoring" a raised
		// weapon means drawing a fresh one already aimed, which is how a re-kit ends with a
		// round in a teammate.
		ctrl.SetWeaponRaised(false);

		// Not done: the return of TryEquipRightHandItem only says the equip was accepted. The
		// next tick reads the hand and re-issues if it did not take.
		return false;
	}

	//--------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcKitSaved(string kitName, ResourceName optic, string loadoutWire)
	{
		RK29_KitPicker.MarkLocalStash(kitName, optic, loadoutWire);
	}

	//--------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcItemsDropped(int count, string itemList)
	{
		string body = count.ToString() + " item(s) did not fit in your inventory and were left out";
		if (itemList != string.Empty)
			body += ":\n" + itemList;
		else
			body += ".";
		SCR_HintManagerComponent.ShowCustomHint(body, "KIT APPLIED", 10);
	}

	//--------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RK29_RpcAskKit(string kitName, ResourceName weapon, ResourceName optic)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
		{
			Print("[RK29] kit request dropped - manager never booted", LogLevel.ERROR);
			return;
		}
		mgr.HandleKitRequest_S(GetPlayerId(), kitName, weapon, optic);
	}
}
