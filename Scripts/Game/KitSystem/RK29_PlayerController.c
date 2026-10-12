//------------------------------------------------------------------------------------------------
//! Client -> server kit request bridge.
//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController
{
	//! How long to keep trying once the draw is actionable. Later than this and the player has
	//! drawn it.
	protected static const float RK29_RESTORE_TIMEOUT_MS = 1000;

	//! Outer cap on a spawn draw waiting to become actionable, so an aborted spawn does not poll
	//! forever.
	protected static const float RK29_SPAWN_DRAW_CAP_MS = 30000;

	//! Gap between equip attempts. TryEquipRightHandItem can refuse and only says so in a return
	//! value, so every attempt is checked next tick against what is in the hand and re-issued.
	protected static const float RK29_EQUIP_RETRY_MS = 250;

	protected static const int RK29_POLL_MS = 50;

	//! Longest kit name a request may carry - the longest authored is ~30 characters. Over it the
	//! request is dropped unread, so a hostile name never reaches a log line.
	protected static const int RK29_KIT_NAME_MAX_CHARS = 128;

	//! Longest a request waits for the ADS blend-out before going anyway.
	protected static const float RK29_ADS_OUT_CAP_MS = 1500;

	//! Longest a request waits for the hands to come free. Past it the request is cancelled, never
	//! forced: a long reload outlasts it, and stripping a gun mid-reload is what this wait prevents.
	//! The server cancels the same way on its own HANDS_SETTLE_TIMEOUT_MS.
	protected static const float RK29_HANDS_FREE_CAP_MS = 3000;

	//! How long busy hands hold a request before the waiting notification is sent, so an item change
	//! that ends a tick later does not log one.
	protected static const float RK29_WAIT_NOTIFY_MS = 250;

	protected string m_RK29_SendKitName;
	protected string m_RK29_SendChoices;
	protected bool   m_RK29_SendIsSeed;
	protected float  m_RK29_SendAfter;
	protected float  m_RK29_SendAdsDeadline;
	protected float  m_RK29_SendDeadline;
	protected float  m_RK29_SendQueuedAt;
	protected bool   m_RK29_SendNotified;

	//! The body a waiting request is about. The server dresses whatever is controlled when it
	//! arrives, so a request that outlives its body - GM possession, place-for-player - is dropped.
	protected IEntity m_RK29_SendBody;

	protected int   m_RK29_DrawStance;
	protected float m_RK29_DrawDynStance;
	protected RplId m_RK29_DrawWeaponId;
	protected RplId m_RK29_DrawCharacterId;
	protected bool  m_RK29_DrawStanceDone;
	protected float m_RK29_DrawDeadline;
	protected float m_RK29_DrawNextTry;

	//! A spawn draw skips the stance restore and the stale-graph re-seat, and anchors at
	//! possession.
	protected bool  m_RK29_DrawIsSpawn;

	//! True from the start for re-kits; false for a spawn draw until the body is ours and the
	//! weapon has replicated in.
	protected bool  m_RK29_DrawAnchored;

	//! Absolute give-up for a spawn draw that never becomes actionable.
	protected float m_RK29_DrawHardDeadline;

	//! Only ever read to log a draw that needed more than the first try.
	protected int   m_RK29_DrawAttempts;
	protected float m_RK29_DrawArmedAt;

	//------------------------------------------------------------------------------------------------
	//! The kit request: the kit name and the picks as RK29_KitResolve's encoded string
	//! ("group=entry:count;..."). A held gadget is put away before asking: a gadget in the hand
	//! is engine-attached rather than stored, so the server's strip cannot see it and the player
	//! ends up with two compasses. Straight to the controller, not
	//! SCR_GadgetManagerComponent.RemoveHeldGadget() - the manager has no skip-animation entry
	//! point, so it would play a putaway after apply. Same pair vanilla uses in StartLoitering;
	//! the manager stays in sync through m_OnGadgetStateChangedInvoker.
	//! The request then waits on this client until the hands are free (RK29_KitApply.HandsBusy):
	//! the owner sees its own reload or item change the frame it starts, the server only later.
	//! Hands still busy at the cap cancel it, unsent.
	void RK29_RequestKit(string kitName, string choices)
	{
		RK29_QueueSend(kitName, choices, false);
	}

	//------------------------------------------------------------------------------------------------
	//! One pending send at a time, request or seed: a newer one replaces whatever is waiting, which
	//! is right both ways - a request outranks a seed, and a seed means the side changed under a
	//! waiting request that the server would now refuse.
	protected void RK29_QueueSend(string kitName, string choices, bool isSeed)
	{
		GetGame().GetCallqueue().Remove(RK29_PollSendKit);

		float now = GetGame().GetWorld().GetWorldTime();
		m_RK29_SendQueuedAt = now;
		m_RK29_SendNotified = false;
		m_RK29_SendKitName = kitName;
		m_RK29_SendChoices = choices;
		m_RK29_SendIsSeed  = isSeed;
		m_RK29_SendAfter   = 0;
		m_RK29_SendAdsDeadline = 0;
		m_RK29_SendDeadline = now + RK29_HANDS_FREE_CAP_MS;
		m_RK29_SendBody = GetControlledEntity();

		CharacterControllerComponent ctrl = RK29_ControlledCtrl();
		if (ctrl && ctrl.IsWeaponADS())
		{
			// the ADS camera is anchored to the weapon for the whole blend-out, so the strip must
			// not delete it until the blend has run: IsWeaponADS() off, then GetADSTime() more
			ctrl.SetWeaponADSInput(false);
			ctrl.SetWeaponADS(false);
			m_RK29_SendAdsDeadline = now + RK29_ADS_OUT_CAP_MS;
		}

		// before the wait rather than at the send: the wait must never be waiting on the gadget
		if (ctrl)
			RK29_PutAwayGadget(ctrl);

		if (RK29_SendWaiting(now))
		{
			GetGame().GetCallqueue().CallLater(RK29_PollSendKit, RK29_POLL_MS, true);
			return;
		}

		RK29_SendKit();
	}

	//------------------------------------------------------------------------------------------------
	protected void RK29_PollSendKit()
	{
		if (GetControlledEntity() != m_RK29_SendBody)
		{
			GetGame().GetCallqueue().Remove(RK29_PollSendKit);
			RK29_Log.Trace("[RK29] kit request dropped - the body it was for has changed");
			return;
		}

		float now = GetGame().GetWorld().GetWorldTime();
		bool waiting = RK29_SendWaiting(now);
		if (waiting && now < m_RK29_SendDeadline)
		{
			if (!m_RK29_SendNotified && now >= m_RK29_SendQueuedAt + RK29_WAIT_NOTIFY_MS)
				RK29_ShowApplyWaiting();
			return;
		}

		GetGame().GetCallqueue().Remove(RK29_PollSendKit);
		if (!waiting)
		{
			RK29_SendKit();
			return;
		}

		RK29_Log.Trace("[RK29] kit request cancelled - hands never came free on this client");
		RK29_ShowApplyCancelled();
	}

	//------------------------------------------------------------------------------------------------
	//! True while the request should hold: the ADS blend-out has not run yet, or the hands are busy.
	protected bool RK29_SendWaiting(float now)
	{
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(GetControlledEntity());
		if (!character)
			return false;
		CharacterControllerComponent ctrl = character.GetCharacterController();
		if (!ctrl)
			return false;

		if (now < m_RK29_SendAdsDeadline)
		{
			if (ctrl.IsWeaponADS())
				return true;

			if (m_RK29_SendAfter <= 0)
			{
				m_RK29_SendAfter = now + ctrl.GetADSTime() * 1000;
				return true;
			}

			if (now < m_RK29_SendAfter)
				return true;
		}

		return RK29_KitApply.HandsBusy(character);
	}

	//------------------------------------------------------------------------------------------------
	protected void RK29_PutAwayGadget(notnull CharacterControllerComponent ctrl)
	{
		if (!ctrl.IsGadgetInHands())
			return;

		ctrl.SetGadgetRaisedModeWanted(false);
		ctrl.RemoveGadgetFromHand(true);
	}

	//------------------------------------------------------------------------------------------------
	protected void RK29_SendKit()
	{
		CharacterControllerComponent ctrl = RK29_ControlledCtrl();
		if (ctrl)
		{
			// still (or again) aiming once the wait is over: drop it instantly and go
			if (ctrl.IsWeaponADS())
			{
				ctrl.SetWeaponADSInput(false);
				ctrl.SetWeaponADS(false);
			}

			RK29_PutAwayGadget(ctrl);
		}

		if (m_RK29_SendIsSeed)
			Rpc(RK29_RpcAsk_SeedKit, m_RK29_SendKitName, m_RK29_SendChoices);
		else
			Rpc(RK29_RpcAsk_Kit, m_RK29_SendKitName, m_RK29_SendChoices);
	}

	//------------------------------------------------------------------------------------------------
	protected CharacterControllerComponent RK29_ControlledCtrl()
	{
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(GetControlledEntity());
		if (!character)
			return null;
		return character.GetCharacterController();
	}

	//------------------------------------------------------------------------------------------------
	override void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		super.OnControlledEntityChanged(from, to);

		if (to && GetGame().GetPlayerController() == this)
			RK29_LoadoutMenu.RegisterListeners();
	}

	//------------------------------------------------------------------------------------------------
	//! Once per request, and only for busy hands: the ADS blend-out alone is over before anyone could
	//! read it, so this stays unarmed and the next poll asks again. A feed entry cannot be taken
	//! back, so it stays up for its own duration even after the kit lands.
	protected void RK29_ShowApplyWaiting()
	{
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(GetControlledEntity());
		if (!character)
			return;
		RK29_EHandsBusy reason = RK29_KitApply.HandsBusyReason(character);
		if (reason == RK29_EHandsBusy.NONE)
			return;

		m_RK29_SendNotified = true;
		SCR_NotificationsComponent.SendLocal(ENotification.RK29_KIT_WAITING_HANDS, reason);
	}

	//------------------------------------------------------------------------------------------------
	//! Both cancels say the same thing: whichever side gave up, nothing on the body changed.
	protected void RK29_ShowApplyCancelled()
	{
		SCR_UISoundEntity.SoundEvent(SCR_SoundEvent.ACTION_FAILED);
		SCR_NotificationsComponent.SendLocal(ENotification.RK29_KIT_NOT_APPLIED);
	}

	//------------------------------------------------------------------------------------------------
	void RK29_NotifyApplyCancelled_S()
	{
		Rpc(RK29_RpcDo_ApplyCancelled);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcDo_ApplyCancelled()
	{
		RK29_ShowApplyCancelled();
	}

	//------------------------------------------------------------------------------------------------
	void RK29_NotifyItemsDropped_S(int count, string itemList)
	{
		Rpc(RK29_RpcDo_ItemsDropped, count, itemList);
	}

	//------------------------------------------------------------------------------------------------
	//! The kit the server settled on and the picks it settled it with, so the deploy row resolves
	//! the same.
	void RK29_NotifyKitSaved_S(string kitName, string picksWire)
	{
		Rpc(RK29_RpcDo_KitSaved, kitName, picksWire);
	}

	//------------------------------------------------------------------------------------------------
	//! Ask the owning client to restore its state after a live re-kit. Re-kit only - once the
	//! client owns the body it holds authority over its own item commands. `characterId` is the
	//! body the request is about: for a few frames after a respawn the client still drives the
	//! previous body.
	void RK29_NotifyRestoreState_S(int stance, float dynStance, RplId weaponId, RplId characterId)
	{
		Rpc(RK29_RpcDo_RestoreState, stance, dynStance, weaponId, characterId);
	}

	//------------------------------------------------------------------------------------------------
	//! Ask the owning client to draw the primary of the body a spawn just dressed - the only
	//! machine whose draw sticks, see RK29_KitManager.RequestSpawnDraw_S. The give-up window is
	//! anchored at possession, not here.
	void RK29_NotifySpawnDraw_S(RplId weaponId, RplId characterId)
	{
		Rpc(RK29_RpcDo_SpawnDraw, weaponId, characterId);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcDo_RestoreState(int stance, float dynStance, RplId weaponId, RplId characterId)
	{
		m_RK29_DrawStance      = stance;
		m_RK29_DrawDynStance   = dynStance;
		m_RK29_DrawWeaponId    = weaponId;
		m_RK29_DrawCharacterId = characterId;
		m_RK29_DrawStanceDone  = false;
		m_RK29_DrawNextTry     = 0;
		m_RK29_DrawAttempts    = 0;
		m_RK29_DrawIsSpawn     = false;
		m_RK29_DrawAnchored    = true;

		m_RK29_DrawArmedAt      = GetGame().GetWorld().GetWorldTime();
		m_RK29_DrawDeadline     = m_RK29_DrawArmedAt + RK29_RESTORE_TIMEOUT_MS;
		m_RK29_DrawHardDeadline = m_RK29_DrawDeadline;

		// state lives on the controller, not in callqueue arguments, so a second request
		// supersedes the first
		GetGame().GetCallqueue().Remove(RK29_PollRestore);
		GetGame().GetCallqueue().CallLater(RK29_PollRestore, RK29_POLL_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcDo_SpawnDraw(RplId weaponId, RplId characterId)
	{
		m_RK29_DrawWeaponId    = weaponId;
		m_RK29_DrawCharacterId = characterId;
		m_RK29_DrawStanceDone  = true; // a fresh body spawns erect - nothing to restore
		m_RK29_DrawNextTry     = 0;
		m_RK29_DrawAttempts    = 0;
		m_RK29_DrawIsSpawn     = true;
		m_RK29_DrawAnchored    = false;

		m_RK29_DrawArmedAt      = GetGame().GetWorld().GetWorldTime();
		m_RK29_DrawHardDeadline = m_RK29_DrawArmedAt + RK29_SPAWN_DRAW_CAP_MS;
		m_RK29_DrawDeadline     = m_RK29_DrawHardDeadline;

		GetGame().GetCallqueue().Remove(RK29_PollRestore);
		GetGame().GetCallqueue().CallLater(RK29_PollRestore, RK29_POLL_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Wait for the named body, then keep asking until the weapon is in the hand. The stance is
	//! restored as soon as that body arrives, since it does not depend on the weapon.
	protected void RK29_PollRestore()
	{
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(GetControlledEntity());
		CharacterControllerComponent ctrl;
		if (character && RK29_IsRequestedBody(character))
			ctrl = character.GetCharacterController();

		if (ctrl)
		{
			// A spawn draw anchors its patience here, not at the request: the RPC can precede a
			// first spawn's world stream by many seconds, and until then the player is on the
			// deploy fade. Measuring from the request is what sank the original owner-side spawn
			// draw.
			if (!m_RK29_DrawAnchored)
			{
				bool weaponHere = Replication.FindItem(m_RK29_DrawWeaponId) != null;
				if (weaponHere || !m_RK29_DrawWeaponId.IsValid())
				{
					m_RK29_DrawAnchored = true;
					m_RK29_DrawDeadline = Math.Min(
						GetGame().GetWorld().GetWorldTime() + RK29_RESTORE_TIMEOUT_MS,
						m_RK29_DrawHardDeadline);
				}
			}

			// a seated body keeps the seat's pose and refuses stance commands; skipped rather
			// than deferred, since TryDraw ends the whole request on the same condition
			if (!m_RK29_DrawStanceDone && !character.IsInVehicle())
			{
				m_RK29_DrawStanceDone = true;
				RK29_RestoreStance(ctrl);
			}

			if (RK29_TryDraw(character, ctrl))
			{
				RK29_StopDraw();

				float took = GetGame().GetWorld().GetWorldTime() - m_RK29_DrawArmedAt;

				// a finished spawn draw always logs - this path shipped broken twice. 0 attempts
				// means the weapon was already in hand when the body became ours
				if (m_RK29_DrawIsSpawn)
					Print(string.Format(
						"[RK29] spawn draw finished - %1 attempt(s), %2ms after the request",
						m_RK29_DrawAttempts, took.ToString(-1, 0)), LogLevel.NORMAL);
				// re-kit: silent when it works first time; anything else means the retry carried
				// it
				else if (m_RK29_DrawAttempts > 1)
					Print(string.Format(
						"[RK29] weapon drawn after %1 attempts, %2ms after the request",
						m_RK29_DrawAttempts, took.ToString(-1, 0)), LogLevel.NORMAL);
				return;
			}
		}

		if (GetGame().GetWorld().GetWorldTime() < m_RK29_DrawDeadline)
			return;

		RK29_StopDraw();
		RK29_GiveUpDraw(ctrl);
	}

	//------------------------------------------------------------------------------------------------
	//! Ran out of patience. `ctrl` is null when the named body never became ours on this client -
	//! one of the three cases the log lines tell apart. A re-kit re-seats the hand empty because
	//! the strip deleted the weapon the graph still points at, but only while the hand really is
	//! empty, or this would strip a weapon the player drew themselves. A spawn draw skips it: a
	//! fresh body's graph never held anything.
	protected void RK29_GiveUpDraw(CharacterControllerComponent ctrl)
	{
		if (!m_RK29_DrawIsSpawn && ctrl && !RK29_HeldWeapon(ctrl))
			ctrl.TryEquipRightHandItem(null, EEquipItemType.EEquipTypeUnarmedDeliberate, true);

		string context = "re-kit";
		if (m_RK29_DrawIsSpawn)
			context = "spawn";

		if (!ctrl)
			Print(string.Format(
				"[RK29] gave up on the %1 draw - the body was never ours on this client",
				context), LogLevel.WARNING);
		else if (m_RK29_DrawAttempts == 0)
			Print(string.Format("[RK29] gave up on the %1 draw - the weapon never replicated in",
				context), LogLevel.WARNING);
		else
			Print(string.Format(
				"[RK29] gave up on the %1 draw - %2 attempts and it never reached the hand",
				context, m_RK29_DrawAttempts), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	protected void RK29_StopDraw()
	{
		GetGame().GetCallqueue().Remove(RK29_PollRestore);
	}

	//------------------------------------------------------------------------------------------------
	//! A poll still armed when this controller goes away would fire against a dead instance. The
	//! destructor, as vanilla's task entities do: entities have no OnDelete hook.
	void ~SCR_PlayerController()
	{
		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
		{
			queue.Remove(RK29_PollRestore);
			queue.Remove(RK29_PollSendKit);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The weapon entity in hands, or null. GetCurrentWeapon() returns the slot component for a
	//! slotted weapon (WeaponSlotComponent extends BaseWeaponComponent) - unwrap before
	//! comparing.
	protected static IEntity RK29_HeldWeapon(CharacterControllerComponent ctrl)
	{
		BaseWeaponManagerComponent wm = ctrl.GetWeaponManagerComponent();
		if (!wm)
			return null;

		BaseWeaponComponent current = wm.GetCurrentWeapon();
		if (!current)
			return null;

		WeaponSlotComponent slot = WeaponSlotComponent.Cast(current);
		if (slot)
			return slot.GetWeaponEntity();

		return current.GetOwner();
	}

	//------------------------------------------------------------------------------------------------
	//! Is this the body the request named? An invalid id means the server could not name one, in
	//! which case whatever we are controlling has to do.
	protected bool RK29_IsRequestedBody(IEntity character)
	{
		if (!m_RK29_DrawCharacterId.IsValid())
			return true;

		RplComponent rpl = RplComponent.Cast(character.FindComponent(RplComponent));
		return rpl && rpl.Id() == m_RK29_DrawCharacterId;
	}

	//------------------------------------------------------------------------------------------------
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

	//------------------------------------------------------------------------------------------------
	//! One tick of the draw. Returns true when the request is finished with - drawn, refused for
	//! a reason that will not change, or overtaken by the player.
	//! Unconfirmed fix: the animation-free snap (and the give-up re-seat) was written for "weapon
	//! stuck in a bad animation state after apply" and never watched succeed on a repro.
	protected bool RK29_TryDraw(SCR_ChimeraCharacter character, CharacterControllerComponent ctrl)
	{
		// dead and unconscious do not un-become true while the request is alive; a vehicle can be
		// dismounted, but an in-vehicle apply deliberately ends the draw
		if (ctrl.IsDead() || ctrl.IsUnconscious() || character.IsInVehicle())
			return true;

		if (!m_RK29_DrawWeaponId.IsValid())
		{
			// not a no-op: the strip may have deleted the throwable that was held, leaving the
			// graph on a stale item state - unless a weapon reached the hand meanwhile, when
			// re-seating would strip it
			if (!RK29_HeldWeapon(ctrl))
				ctrl.TryEquipRightHandItem(null, EEquipItemType.EEquipTypeUnarmedDeliberate, true);
			return true;
		}

		// the id the server named is the only acceptable target: the previous weapon may not have
		// been reaped here yet, and the same prefab handed back is indistinguishable by slot
		IEntity weapon;
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(m_RK29_DrawWeaponId));
		if (rpl)
			weapon = rpl.GetEntity();
		if (!weapon)
			return false; // still streaming in - keep waiting

		// the in-hand check must go through the weapon manager: GetRightHandItem() documents
		// itself null while the active item is a weapon, so comparing against it reads "not
		// drawn" forever
		if (RK29_HeldWeapon(ctrl) == weapon)
			return true;

		float now = GetGame().GetWorld().GetWorldTime();
		if (now < m_RK29_DrawNextTry)
			return false;

		// racing an item change already in flight is the competing-change class that jams the
		// graph
		if (ctrl.IsChangingItem())
			return false;

		m_RK29_DrawNextTry = now + RK29_EQUIP_RETRY_MS;
		m_RK29_DrawAttempts = m_RK29_DrawAttempts + 1;

		// The weapon in hands was deleted out from under the animation graph. SelectWeapon()
		// re-draws with the switching animation - a transition out of that stale state - and that
		// is the one that sticks. TryEquipRightHandItem with swap snaps it in and re-seats the
		// graph, the call vanilla uses in ApplyCharacterDataLoadoutString.
		ctrl.TryEquipRightHandItem(weapon, EEquipItemType.EEquipTypeWeapon, true);

		// always low ready: the weapon the player held no longer exists, so "restoring" a raised
		// one means drawing a fresh one already aimed
		ctrl.SetWeaponRaised(false);

		// not done: the return only says the equip was accepted - the next tick reads the hand
		return false;
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcDo_KitSaved(string kitName, string picksWire)
	{
		RK29_LocalStash.Mark(kitName, picksWire);

		// Remembered across sessions, where the stash above is a session static. What the SERVER
		// settled on, not what the menu asked for, and the same for an apply, a rejoin restore and
		// a spawn seed - all three are "this is your kit now".
		RK29_KitLastUsedStore store = RK29_KitLastUsedStore.GetInstance();
		if (store)
			store.Mark(kitName, picksWire);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RK29_RpcDo_ItemsDropped(int count, string itemList)
	{
		string body = count.ToString() + " item(s) did not fit in your inventory and were left out";
		if (itemList != string.Empty)
			body += ":\n" + itemList;
		else
			body += ".";
		SCR_HintManagerComponent.ShowCustomHint(body, "KIT APPLIED", 10);
	}

	//------------------------------------------------------------------------------------------------
	//! Offer the server what this client last wore on this side. Separate from RK29_RequestKit
	//! because the two carry different authority: a request is the player asking now and always
	//! wins, a seed is a profile file remembering and loses to anything the server already holds.
	//! Usually there is no body yet and it goes at once; a GM moving a living player to the other
	//! side is the exception, and the server re-dresses that body like a request - so it waits the
	//! same way.
	void RK29_SeedKit(string kitName, string choices)
	{
		RK29_QueueSend(kitName, choices, true);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RK29_RpcAsk_SeedKit(string kitName, string choices)
	{
		if (kitName.Length() > RK29_KIT_NAME_MAX_CHARS
			|| choices.Length() > RK29_KitResolve.WIRE_MAX_CHARS)
		{
			Print(string.Format("[RK29] kit seed dropped - oversized (player %1: name %2 chars,"
				+ " picks %3 chars)", GetPlayerId(), kitName.Length(), choices.Length()),
				LogLevel.WARNING);
			return;
		}

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
		{
			Print("[RK29] kit seed dropped - manager never booted", LogLevel.ERROR);
			return;
		}
		mgr.HandleKitSeed_S(GetPlayerId(), kitName, choices);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RK29_RpcAsk_Kit(string kitName, string choices)
	{
		if (kitName.Length() > RK29_KIT_NAME_MAX_CHARS
			|| choices.Length() > RK29_KitResolve.WIRE_MAX_CHARS)
		{
			Print(string.Format("[RK29] kit request dropped - oversized (player %1: name %2 chars,"
				+ " picks %3 chars)", GetPlayerId(), kitName.Length(), choices.Length()),
				LogLevel.WARNING);
			return;
		}

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
		{
			Print("[RK29] kit request dropped - manager never booted", LogLevel.ERROR);
			return;
		}
		mgr.HandleKitRequest_S(GetPlayerId(), kitName, choices);
	}
}
