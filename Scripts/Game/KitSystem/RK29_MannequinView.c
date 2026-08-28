//------------------------------------------------------------------------------------------------
//! The loadout menu's soldier: the body it spawns, the camera it is rendered through, the drag
//! and wheel that turn and zoom him, and the band he shares with the detail scroll. Which kit he
//! wears stays on the menu and arrives whole through DressLoaded. He is also weighed -
//! RK29_KitWeight reads the body - so the dress is a real apply pass. The body is respawned on
//! every dress and cannot be dressed twice (RK29_MannequinDress.ApplyLoaded), so no caller may
//! hold it and the player's viewing angle cannot live on the camera; m_fMannequinYaw is where it
//! lives instead.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
class RK29_MannequinView
{
	//! Vanilla's own limits for the very same call (SCR_InventoryCharacterWidgetHelper:139). The
	//! pitch half is inert - the drag is yaw-only. [1] must stay equal to MANNEQUIN_YAW_LIMIT below.
	protected static const vector MANNEQUIN_ROT_MIN = "-30 -180 0";
	protected static const vector MANNEQUIN_ROT_MAX = "0 180 0";
	//! MANNEQUIN_ROT_MAX's yaw as a scalar: the clamp wants a float and vanilla never indexes a
	//! static const vector, so the number is stated twice on purpose.
	protected static const float MANNEQUIN_YAW_LIMIT = 180.0;

	//! Vanilla's smoothing for the same rotation, lifted whole; the zoom figure is negative so a
	//! wheel forward moves the camera in. SCR_InventoryCharacterWidgetHelper lines 116, 85 and 86.
	protected static const float MANNEQUIN_SMOOTHING = 0.15;
	protected static const float MANNEQUIN_ROT_SPEED = 500.0;
	protected static const float MANNEQUIN_ZOOM_SPEED = -64.0;

	//! Cursor pixels into the units vanilla's pending accumulator is denominated in, and not
	//! optional: the accumulator spends MANNEQUIN_SMOOTHING of itself per tick and so settles at
	//! input-per-tick / 0.15, which pins raw pixels against the clamp below - one pixel a tick and
	//! twenty would both come out as a fixed 720 deg/s. At 0.0625 a fully-spent pixel is half a
	//! degree (a pending unit pays out as MANNEQUIN_TICK_SECONDS * MANNEQUIN_ROT_SPEED = 8), so ~720
	//! pixels turn him round.
	protected static const float MANNEQUIN_PAN_PER_PIXEL = 0.0625;

	//! One wheel notch as pending zoom - very nearly four degrees of FOV, inward. Vanilla's own
	//! figure (a hundredth per unit of its wheel action) is unusable: at that scale its
	//! inputResetEpsilon of 10 zeroes the accumulator before it is ever spent, so the
	//! inventory's mouse-wheel zoom cannot move at all; only its gamepad path clears that floor.
	//! Copying it would ship the same dead control.
	protected static const float MANNEQUIN_ZOOM_PER_NOTCH = 4.0;

	//! Ceiling on pending rotation - vanilla's own +-10, here 160 pixels of unspent drag: no hand
	//! movement produces that in one tick, and a cursor teleport (alt-tab, resolution change) does.
	protected static const float MANNEQUIN_PAN_CLAMP = 10.0;

	//! Below this, squared, a pending input is spent rather than chased to zero down ever-halving
	//! ticks. Vanilla's inputResetEpsilon is the same idea; the value is ours because the scale is.
	protected static const float MANNEQUIN_INPUT_EPSILON = 0.0001;

	//! How far the camera may be zoomed either way. Nothing sets a starting FOV: the body prefab's
	//! own attributes are what the real inventory paperdoll is framed by, so the mannequin opens
	//! framed.
	protected static const float MANNEQUIN_FOV_MIN = 22.0;
	protected static const float MANNEQUIN_FOV_MAX = 60.0;

	//! The pump behind all of it, roughly 60Hz, running only while there is input left to spend - see
	//! MannequinCameraFrame - so an untouched mannequin costs nothing. The seconds figure is derived
	//! from the interval rather than restated so the two cannot disagree, the same shape as
	//! SCR_GameModeStatistics:755; the float divisor is what keeps the division real.
	protected static const int MANNEQUIN_TICK_MS = 16;
	protected static const float MANNEQUIN_TICK_SECONDS = MANNEQUIN_TICK_MS / 1000.0;

	//! Layout-authored, like every ItemPreviewWidget here - one built procedurally does not render.
	protected Widget m_wMannequinBox;
	protected ItemPreviewWidget m_wMannequinPreview;

	//! The scrolling half of the detail column. Held so it can be taken away while the mannequin is
	//! up: the two share the band and only ever one of them is answering.
	protected Widget m_wDetailScroll;

	//! The camera is the spawned body's own inventory-preview attributes, fetched off it in
	//! SpawnMannequinBody the way SCR_InventoryMenuUI:933-936 fetches the player's: the character's
	//! storage manager carries an attribute collection holding
	//! SCR_CharacterInventoryPreviewAttributes. Character_Base.et:171-175 authors the full-body
	//! paperdoll framing. not a ref - the object belongs to the prefab's collection, and vanilla's
	//! m_PlayerRenderAttributes is not one either (SCR_InventoryMenuUI:377). Null for a body without
	//! it: SetPreviewItem takes a null override and falls back to the prefab's camera, so the
	//! mannequin still renders - it simply cannot be turned.
	protected PreviewRenderAttributes m_MannequinCamera;
	protected ref RK29_MannequinCameraHandler m_MannequinCameraHandler;

	//! The drag, and how far across the screen the cursor had got when the pump
	//! last looked (screen pixels, straight off WidgetManager). Only horizontal:
	//! the drag turns the soldier, never tips him.
	protected bool m_bMannequinDragging;
	protected int m_iMannequinCursorX;

	//! Input handed to the pump and not yet spent, in vanilla's two accumulators: pending
	//! rotation as degrees-worth, pending zoom as FOV-worth. Kept as a bare yaw because vanilla
	//! only ever writes [1] of its rotation vector from the mouse; the vector RotateItemCamera
	//! takes is built at the one call that spends it.
	protected float m_fMannequinYawInput;
	protected float m_fMannequinZoomInput;

	//! How far round the player has turned him, in degrees of yaw, kept for the life of the
	//! menu and not of a body: the body is respawned on every dress and the camera is the
	//! body's, so without a total kept out here, stepping a magazine count would spin the
	//! soldier back to face front. PreviewRenderAttributes offers no way to ask what the
	//! rotation is (RotateItemCamera, ResetDeltaRotation and ZoomCamera are its whole surface),
	//! so a total not accumulated as it is spent cannot be recovered. ApplyMannequinYaw pays it
	//! back; Teardown is the only thing to zero it.
	protected float m_fMannequinYaw;

	//! Started by input, and stops itself once both accumulators are spent.
	protected bool m_bMannequinPumpRunning;

	//! The bare body, spawned and thrown away on every dress - never cached and re-dressed in place;
	//! see SpawnMannequinBody. It is the menu's only preview entity: every weapon tile pictures the
	//! gun standing in the matching slot of this body (RK29_MenuTileColumn.StampWeaponTile) rather
	//! than a second copy, which is why a tile can show the launcher's own sight and mount. Nobody
	//! may hold this pointer - every reader asks again through Body() or WeaponAt() after the dress
	//! that made it. Local-only and never replicated; ClearMannequin has to take it back out of the
	//! world.
	protected IEntity m_MannequinBody;

	//! What the last dress could not place on the body - the weight row turns red over it.
	protected ref array<ResourceName> m_aDropped = {};

	//------------------------------------------------------------------------------------------------
	//! Called once as the menu opens; nothing here spawns a body, which waits for the first dress.
	void Init(Widget root)
	{
		if (!root)
			return;

		m_wMannequinBox = RK29_WidgetUtil.Require(root, "MannequinBox");
		m_wMannequinPreview = ItemPreviewWidget.Cast(
			RK29_WidgetUtil.Require(root, "MannequinPreview"));
		m_wDetailScroll = RK29_WidgetUtil.Require(root, "DetailScroll");

		// the handler goes on the workspace, exactly where vanilla's puts itself
		// (SCR_InventoryCharacterWidgetHelper:173), and gates on the box's screen bounds rather than on
		// the widget the event arrived at - an ItemPreviewWidget is not somewhere a drag can be caught.
		// Attached once for the life of the dialog and removed in Teardown.
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (workspace && m_wMannequinBox)
		{
			m_MannequinCameraHandler = new RK29_MannequinCameraHandler();
			m_MannequinCameraHandler.m_View = this;
			workspace.AddHandler(m_MannequinCameraHandler);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The two things this view owns outside the dialog's widget tree, given back - both teardown
	//! paths open with it. The pump is a repeating call queued against this object, and the handler
	//! is on the workspace, which outlives the dialog, so neither goes away with the widgets; the
	//! same pair the vanilla helper's Destroy() makes (SCR_InventoryCharacterWidgetHelper:155-162).
	//! Once only - the pointer is dropped in the same breath, so a second close cannot remove it
	//! again.
	protected void ReleaseExternalHooks()
	{
		StopMannequinPump();
		m_bMannequinDragging = false;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (workspace && m_MannequinCameraHandler)
			workspace.RemoveHandler(m_MannequinCameraHandler);

		m_MannequinCameraHandler = null;
	}

	//------------------------------------------------------------------------------------------------
	//! A world loss: the hooks, then every handle dropped - the body and the widgets died with the
	//! world, and a kept handle is a pointer into a dead one. Teardown is the dialog-close path and
	//! is the one with real work left to do on the body.
	void AbandonWithWorld()
	{
		ReleaseExternalHooks();

		m_MannequinCamera = null;
		m_MannequinBody = null;
		m_wMannequinPreview = null;
		m_wMannequinBox = null;
		m_wDetailScroll = null;
	}

	//------------------------------------------------------------------------------------------------
	//! In the order it has to come apart: queued pump, workspace handler, body, then the widgets.
	void Teardown()
	{
		ReleaseExternalHooks();

		// the preview pointer is dropped before the body, and only on this path: vanilla destroys the
		// widget tree and only then invokes m_OnClose (SCR_ConfigurableDialogUI.Internal_Close:613-614),
		// so ClearMannequin's release through this pointer would be a call into a destroyed widget
		m_wMannequinPreview = null;

		ClearMannequin();

		// the player's yaw is forgotten only here - ClearMannequin runs on every dress and must keep it
		m_fMannequinYaw = 0;

		m_wMannequinBox = null;
		m_wDetailScroll = null;
	}

	//------------------------------------------------------------------------------------------------
	//! Stands a new body up - always a new one - and puts the whole kit on it: garments, guns, seated
	//! rounds, mounted attachments and cargo. Fresh every time because the dress ends by selecting
	//! the primary, which binds that gun to the body's weapon manager, and a body dressed twice
	//! stands there missing its slot-0 gun (RK29_MannequinDress.ApplyLoaded has the chain and the
	//! failed attempts to clear it). It is weighed rather than only pictured, which is why it takes a
	//! whole resolved kit; a body that could not be built takes the previous one with it and answers
	//! false.
	bool DressLoaded(ResourceName bodyPrefab, notnull RK29_KitStruct kit,
		map<int, ref array<ref RK29_LoadedPick>> loadedMags,
		array<ref RK29_AttachmentOrder> orders, string subject)
	{
		if (!SpawnMannequinBody(bodyPrefab))
		{
			ClearMannequin();
			return false;
		}

		array<ResourceName> dropped;
		RK29_MannequinDress.ApplyLoaded(m_MannequinBody, kit, loadedMags, orders, subject, dropped);
		m_aDropped.Clear();
		if (dropped)
			m_aDropped.InsertAll(dropped);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Items the last dress could not place; empty when everything fit or nothing is dressed.
	array<ResourceName> Dropped()
	{
		return m_aDropped;
	}

	//------------------------------------------------------------------------------------------------
	//! Null before the first dress and after one that failed - which is the weight row's
	//! honest-failure case: it stamps nothing rather than a number it cannot stand behind.
	IEntity Body()
	{
		return m_MannequinBody;
	}

	//------------------------------------------------------------------------------------------------
	//! Asked off the body every time rather than cached: a dress can delete and respawn any of these
	//! weapons. Null before the body exists, after a failed dress, for the menu's NO_SLOT (an index
	//! outside the body's weapon storage) and for a slot this kit seats nothing in -
	//! RK29_MenuTileColumn.StampWeaponTile reads all of them as "picture the catalog prefab instead".
	IEntity WeaponAt(int slot)
	{
		if (!m_MannequinBody)
			return null;

		return RK29_MannequinDress.WeaponAt(m_MannequinBody, slot);
	}

	//------------------------------------------------------------------------------------------------
	void ShowMannequin(bool show)
	{
		if (m_wMannequinBox)
			m_wMannequinBox.SetVisible(show);

		if (m_wDetailScroll)
			m_wDetailScroll.SetVisible(!show);

		// a hidden render raises no mouse-up, so a drag live when the band
		// switched would still be live when it comes back
		if (!show)
		{
			m_bMannequinDragging = false;
			StopMannequinPump();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Bounds test in physical pixels - mouse coordinates and GetScreenPos share them, so nothing is
	//! unscaled here (vanilla's CanProcessEvent, SCR_InventoryCharacterWidgetHelper:12-23).
	//! Visibility is asked as well, which vanilla need not: the same rectangle is a list of magazines
	//! half the time, and a wheel spun there must not zoom a soldier nobody can see.
	//! IsVisibleInHierarchy, so a hidden dialog above the box answers for it too.
	bool CursorOverMannequin(int x, int y)
	{
		if (!m_wMannequinBox || !m_wMannequinBox.IsVisibleInHierarchy())
			return false;

		float posX, posY, sizeX, sizeY;
		m_wMannequinBox.GetScreenPos(posX, posY);
		m_wMannequinBox.GetScreenSize(sizeX, sizeY);

		return x >= posX && x <= posX + sizeX && y >= posY && y <= posY + sizeY;
	}

	//------------------------------------------------------------------------------------------------
	//! Takes the camera off the body just spawned: the character storage's attribute collection
	//! carries SCR_CharacterInventoryPreviewAttributes. Shared data, and the method is shaped
	//! around it - what comes back is the instance the prefab owns, which the in-game paperdoll
	//! of any character on that prefab reads too, so this resets on the way in and
	//! ClearMannequin resets on the way out. The reset is followed by ApplyMannequinYaw
	//! replaying the player's angle onto a known zero, because this runs on every dress. A body
	//! with no such attribute leaves the camera null and cannot turn.
	protected void FetchMannequinCamera(notnull IEntity body)
	{
		m_MannequinCamera = null;

		SCR_CharacterInventoryStorageComponent storage = SCR_CharacterInventoryStorageComponent.Cast(
			body.FindComponent(SCR_CharacterInventoryStorageComponent));
		if (!storage)
		{
			Print("[RK29] loadout menu: mannequin body has no character"
				+ " inventory storage - it renders through the prefab's own camera and cannot be"
				+ " turned", LogLevel.WARNING);
			return;
		}

		ItemAttributeCollection collection = storage.GetAttributes();
		if (!collection)
		{
			Print("[RK29] loadout menu: mannequin body's storage carries no"
				+ " attribute collection - it renders through the prefab's own camera and cannot"
				+ " be turned", LogLevel.WARNING);
			return;
		}

		m_MannequinCamera = PreviewRenderAttributes.Cast(
			collection.FindAttribute(SCR_CharacterInventoryPreviewAttributes));

		if (!m_MannequinCamera)
		{
			Print("[RK29] loadout menu: mannequin body authors no"
				+ " SCR_CharacterInventoryPreviewAttributes - it renders through the prefab's own"
				+ " camera and cannot be turned", LogLevel.WARNING);
			return;
		}

		// a known zero first, whatever the last borrower left these shared attributes
		// turned to - the same reset the inventory makes as it closes
		// (SCR_InventoryMenuUI:3034) - then the player's own yaw
		m_MannequinCamera.ResetDeltaRotation();
		ApplyMannequinYaw();
	}

	//------------------------------------------------------------------------------------------------
	//! Puts the camera back where the player had turned it to, called only from the fetch above
	//! and only right after it reset to zero. One call, from a known zero, with a yaw the
	//! accumulator has already clamped to the same band - which is why it does not matter that the
	//! corpus never settles whether RotateItemCamera clamps the delta or the total
	//! (SCR_InventoryMenuUI:2778 reads as the total, :2802 works either way): both readings land
	//! on m_fMannequinYaw exactly. Yaw only, in [1].
	protected void ApplyMannequinYaw()
	{
		if (!m_MannequinCamera || m_fMannequinYaw == 0)
			return;

		vector replay = vector.Zero;
		replay[1] = m_fMannequinYaw;

		m_MannequinCamera.RotateItemCamera(replay, MANNEQUIN_ROT_MIN, MANNEQUIN_ROT_MAX);
	}

	//------------------------------------------------------------------------------------------------
	//! Puts the borrowed attributes back upright and lets go of them: they are the prefab's
	//! own and the real inventory screen reads them, so a menu closed mid-drag would leave
	//! every soldier of that prefab tilted. Rotation only, because it is all the engine
	//! offers a reset for - there is nothing of the kind for the FOV ZoomCamera moves, so a
	//! hard zoom follows the player into his inventory.
	protected void ResetMannequinCamera()
	{
		if (!m_MannequinCamera)
			return;

		m_MannequinCamera.ResetDeltaRotation();
		m_MannequinCamera = null;
	}

	//------------------------------------------------------------------------------------------------
	//! Hands the dressed body back to the preview widget through the camera fetched off that body, as
	//! vanilla does after a rotation moves (SCR_InventoryMenuUI.UpdateCharacterPreview, from :848). A
	//! null camera reads as "use the prefab's own". forceRefresh is the caller's: a rebuild changed
	//! what the body is and must force, a drag only moved the camera and must not, or every tick of
	//! the drag throws the cached preview away.
	protected void PresentMannequin(bool forceRefresh)
	{
		if (!m_wMannequinPreview || !m_MannequinBody)
			return;

		ItemPreviewManagerEntity previewMgr = GetItemPreviewManager();
		if (!previewMgr)
			return;

		previewMgr.SetPreviewItem(m_wMannequinPreview, m_MannequinBody, m_MannequinCamera,
			forceRefresh);
	}

	//------------------------------------------------------------------------------------------------
	//! The press was already bounds-tested by the handler. Only the horizontal cursor position is
	//! kept: GetMousePos answers both axes and the vertical is no measurement this drag has a use
	//! for.
	void BeginMannequinDrag()
	{
		if (!m_MannequinCamera)
			return;

		int unusedY;
		WidgetManager.GetMousePos(m_iMannequinCursorX, unusedY);
		m_bMannequinDragging = true;
		StartMannequinPump();
	}

	//------------------------------------------------------------------------------------------------
	//! The pump is not stopped here: the rotation still pending is what makes a flicked drag coast to
	//! a stop rather than freeze mid-turn, and the pump retires itself once that is spent.
	void EndMannequinDrag()
	{
		m_bMannequinDragging = false;
	}

	//------------------------------------------------------------------------------------------------
	//! A wheel notch banked as pending zoom, read as a direction rather than a magnitude: what the
	//! engine puts in this parameter for one detent is not stated anywhere in the corpus - every
	//! vanilla OnMouseWheel treats it as a flag or ignores it - so a scale built on a guess of it
	//! would be either inert or violent on hardware reporting the other convention.
	void ZoomMannequin(int wheel)
	{
		if (wheel == 0 || !m_MannequinCamera)
			return;

		float notch = MANNEQUIN_ZOOM_PER_NOTCH;
		if (wheel < 0)
			notch = -notch;

		m_fMannequinZoomInput += notch;
		StartMannequinPump();
	}

	//------------------------------------------------------------------------------------------------
	//! Once only: every notch and every press asks, and CallLater would queue another timer each.
	protected void StartMannequinPump()
	{
		if (m_bMannequinPumpRunning)
			return;

		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (!queue)
			return;

		m_bMannequinPumpRunning = true;
		queue.CallLater(MannequinCameraFrame, MANNEQUIN_TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	//! A repeating call outliving the object it is queued against is the one way this could crash.
	protected void StopMannequinPump()
	{
		if (!m_bMannequinPumpRunning)
			return;

		m_bMannequinPumpRunning = false;
		m_fMannequinYawInput = 0;
		m_fMannequinZoomInput = 0;

		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
			queue.Remove(MannequinCameraFrame);
	}

	//------------------------------------------------------------------------------------------------
	//! One tick, shaped exactly like SCR_InventoryCharacterWidgetHelper.Update. The one deviation is
	//! where the input comes from: vanilla reads "Inventory_InspectPanX" and
	//! "Inventory_InspectZoom", both declared inside chimeraInputCommon's InventoryMenuContext,
	//! which this dialog never activates - so both would answer a silent zero. Pan is therefore the
	//! cursor's own travel since the last tick and zoom arrives from the wheel event; everything
	//! downstream of that is vanilla's. Yaw-only is vanilla's shape too: its helper writes nothing
	//! into [0] from the mouse either, so the vertical is not read, not accumulated and not zeroed -
	//! an accumulator nothing fills cannot drift.
	protected void MannequinCameraFrame()
	{
		PreviewRenderAttributes camera = m_MannequinCamera;
		if (!camera || !m_wMannequinPreview)
		{
			StopMannequinPump();
			return;
		}

		if (m_bMannequinDragging)
		{
			int cursorX, unusedY;
			WidgetManager.GetMousePos(cursorX, unusedY);

			// converted out of pixels first - see MANNEQUIN_PAN_PER_PIXEL for why raw
			// pixels make every drag the same speed - and clamped after, at vanilla's own
			// bound, which is what a cursor teleport hits
			m_fMannequinYawInput = Math.Clamp(
				m_fMannequinYawInput + (cursorX - m_iMannequinCursorX) * MANNEQUIN_PAN_PER_PIXEL,
				-MANNEQUIN_PAN_CLAMP, MANNEQUIN_PAN_CLAMP);

			m_iMannequinCursorX = cursorX;
		}

		// residue below the epsilon is spent rather than chased: an ever-halving remainder would keep the
		// pump alive forever, re-presenting the same picture every tick
		if (m_fMannequinYawInput * m_fMannequinYawInput < MANNEQUIN_INPUT_EPSILON)
			m_fMannequinYawInput = 0;
		if (m_fMannequinZoomInput * m_fMannequinZoomInput < MANNEQUIN_INPUT_EPSILON)
			m_fMannequinZoomInput = 0;

		float spentYaw = m_fMannequinYawInput * MANNEQUIN_SMOOTHING;
		float zoom = m_fMannequinZoomInput * MANNEQUIN_SMOOTHING;

		m_fMannequinYawInput = m_fMannequinYawInput - spentYaw;
		m_fMannequinZoomInput -= zoom;

		spentYaw = spentYaw * MANNEQUIN_TICK_SECONDS * MANNEQUIN_ROT_SPEED;
		zoom = zoom * MANNEQUIN_TICK_SECONDS * MANNEQUIN_ZOOM_SPEED;

		bool moved = false;

		if (spentYaw != 0)
		{
			vector spent = vector.Zero;
			spent[1] = spentYaw;
			camera.RotateItemCamera(spent, MANNEQUIN_ROT_MIN, MANNEQUIN_ROT_MAX);

			// the same degrees, banked where they outlive the camera they were just spent on - the body is
			// respawned on every dress. Clamped as the camera clamps it; see m_fMannequinYaw.
			m_fMannequinYaw = Math.Clamp(m_fMannequinYaw + spentYaw, -MANNEQUIN_YAW_LIMIT,
				MANNEQUIN_YAW_LIMIT);

			moved = true;
		}

		if (zoom != 0)
		{
			camera.ZoomCamera(zoom, MANNEQUIN_FOV_MIN, MANNEQUIN_FOV_MAX);
			moved = true;
		}

		if (moved)
		{
			PresentMannequin(false);
			return;
		}

		if (!m_bMannequinDragging)
			StopMannequinPump();
	}

	//------------------------------------------------------------------------------------------------
	//! The presenting half only - the dress has already run for whatever changed, and
	//! this stays on the band's own empty-detail branch so a hidden widget is never
	//! presented. A body that is not there (never built, or taken away by a failed dress)
	//! hides the box rather than faking a soldier.
	void RevealMannequin()
	{
		if (!m_wMannequinPreview || !m_MannequinBody || !GetItemPreviewManager())
		{
			ShowMannequin(false);
			return;
		}

		ShowMannequin(true);

		// the widget samples the entity at the moment it is handed one, and that entity is a different
		// one on every dress - so it is handed one on every reveal, forced, ClearMannequin having handed
		// it null on the way past. Whatever the player has turned it to survives the respawn; only the
		// dialog closing puts it straight - see ApplyMannequinYaw, FetchMannequinCamera and Teardown.
		PresentMannequin(true);
		m_wMannequinPreview.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! A bare body, new every time: whatever stands there is taken away first, on every single dress
	//! - there is no cache and no prefab key. Do not reintroduce a cache without reading
	//! RK29_MannequinDress.ApplyLoaded: the dress ends by selecting the primary, the selection binds
	//! that gun to the body's weapon manager, and nothing reachable from script unbinds it on a body
	//! no player controls - so the next dress's strip is refused and the soldier stands there with no
	//! primary and a weight row short by exactly one rifle. It is cheap: spawn-plus-dress measures
	//! 8-11 ms in the field log against 10-13 for the cached re-dress it replaced. The only thing
	//! that survives the respawn is the player's yaw (m_fMannequinYaw, replayed by
	//! ApplyMannequinYaw).
	protected bool SpawnMannequinBody(ResourceName prefab)
	{
		if (prefab == ResourceName.Empty)
		{
			Print("[RK29] loadout menu: mannequin skipped - no body prefab for"
				+ " this class (set m_sBodyPrefab on its side config)", LogLevel.WARNING);
			return false;
		}

		// takes the previous body out of the world, its borrowed attributes put straight first. The
		// player's yaw is not dropped with it - Teardown is the one place that forgets it.
		ClearMannequin();

		Resource res = Resource.Load(prefab);
		if (!res.IsValid())
		{
			Print(string.Format("[RK29] loadout menu: mannequin body prefab did not load - %1",
				prefab), LogLevel.WARNING);
			return false;
		}

		m_MannequinBody = GetGame().SpawnEntityPrefabLocal(res, GetGame().GetWorld());
		if (!m_MannequinBody)
		{
			Print(string.Format("[RK29] loadout menu: mannequin body spawn failed for %1",
				prefab), LogLevel.WARNING);
			return false;
		}

		// the camera comes off the body itself and is the body prefab's own framing, so it is fetched
		// here, on the one call that ever produces a body. It is also where the player's yaw is replayed.
		FetchMannequinCamera(m_MannequinBody);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Takes the mannequin back out of the world - the whole tree, gun and seated attachments
	//! included, since all of it hangs off the body's slots. Runs from teardown, from a dress
	//! that could not resolve, and from SpawnMannequinBody before every respawn, so it is a hot
	//! path; safe twice and safe on no body. The preview widget is told first - the render
	//! manager samples whatever entity it was last handed, so it is given null before the delete
	//! rather than left pointing into the hole. It is also the one place the borrowed preview
	//! attributes are put upright and released. It does not forget the player's yaw, and must
	//! not: it runs on every dress. Teardown drops it.
	void ClearMannequin()
	{
		m_aDropped.Clear();
		ResetMannequinCamera();

		if (m_MannequinBody)
		{
			ReleaseMannequinPreview();
			SCR_EntityHelper.DeleteEntityAndChildren(m_MannequinBody);
			m_MannequinBody = null;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Hands the preview widget nothing, so the render manager is not left sampling an entity about
	//! to be deleted - vanilla's own move at SCR_InventorySlotUI:991 and SCR_InventoryMenuUI:4954. A
	//! null pointer is the answer, not an oversight: Teardown drops that pointer before the
	//! ClearMannequin that would otherwise reach through it.
	protected void ReleaseMannequinPreview()
	{
		if (!m_wMannequinPreview)
			return;

		ItemPreviewManagerEntity previewMgr = GetItemPreviewManager();
		if (previewMgr)
			previewMgr.SetPreviewItem(m_wMannequinPreview, null);
	}

	//------------------------------------------------------------------------------------------------
	//! Spawned on first use if the world carries none. Shared with the attachment view and the
	//! loadout menu, which render previews of their own.
	static ItemPreviewManagerEntity GetItemPreviewManager()
	{
		ChimeraWorld world = GetGame().GetWorld();
		if (!world)
			return null;

		ItemPreviewManagerEntity manager = world.GetItemPreviewManager();
		if (!manager)
		{
			Resource rsc = Resource.Load("{9F18C476AB860F3B}Prefabs/World/Game/ItemPreviewManager.et");
			if (rsc.IsValid())
				GetGame().SpawnEntityPrefabLocal(rsc, world);
			manager = world.GetItemPreviewManager();
		}
		return manager;
	}
}
