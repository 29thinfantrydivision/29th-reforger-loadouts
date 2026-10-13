//------------------------------------------------------------------------------------------------
//! A saved kit's quick bar: what it puts on hotbar keys 6-0, saved from the body the player wears
//! and restored on the owner once a body wearing that kit has its gear. Client-only end to end -
//! SCR_CharacterInventoryStorageComponent.m_aQuickSlots is never replicated, every machine builds
//! its own bar, and the server never hears of this. Do not move any of it server-side.
//!
//! Quick-slot indices 5-9 are keys 6-0. 0-4 (keys 1-5) are never saved: StoreItemToQuickSlot files
//! whatever sits in a weapon slot under that slot's id, and the throwable grenade slot is
//! WeaponSlotIndex 4, so key 5 is taken back every time a grenade is primed - exactly like 1-4.
//! Vanilla's own SCR_CharacterQuickBarSerializer does save 4; do not follow it there.
//------------------------------------------------------------------------------------------------
class RK29_QuickBar
{
	static const int FIRST_SLOT = 5;
	static const int SLOT_COUNT = 5;

	//! The preset record's trailing field - see RK29_KitPresetStorage.QuickBarOf.
	static const string EXTRA_KEY = "qs=";

	//! "{0123456789ABCDEF}". A slot stores the GUID, not the path: no "|" or "," can reach the record.
	protected static const int GUID_LENGTH = 18;

	//------------------------------------------------------------------------------------------------
	//! Hotbar key for a quick-slot index: 5 -> 6, 9 -> 0.
	static int KeyOf(int index)
	{
		return (index + 1) % 10;
	}

	//------------------------------------------------------------------------------------------------
	//! The GUID part of a resource name, "" for anything not shaped like one.
	static string GuidOf(ResourceName prefab)
	{
		string s = prefab;
		if (s.Length() < GUID_LENGTH || s.Get(0) != "{" || s.Get(GUID_LENGTH - 1) != "}")
			return "";

		return s.Substring(0, GUID_LENGTH);
	}

	//------------------------------------------------------------------------------------------------
	static ResourceName PrefabOf(IEntity entity)
	{
		if (!entity)
			return ResourceName.Empty;

		EntityPrefabData data = entity.GetPrefabData();
		if (!data)
			return ResourceName.Empty;

		return data.GetPrefabName();
	}

	//------------------------------------------------------------------------------------------------
	//! SLOT_COUNT comma-separated fields, "" for an empty key: "{A},,{B},,".
	static string Encode(notnull array<ResourceName> bar)
	{
		string value;
		foreach (int i, ResourceName prefab : bar)
		{
			if (i > 0)
				value += ",";
			value += GuidOf(prefab);
		}

		return value;
	}

	//------------------------------------------------------------------------------------------------
	//! False for anything Encode did not write, which restores nothing rather than a guess.
	//! skipEmptyEntries is FALSE: an empty key is a field, and skipping it shifts every key after it.
	//! A short list is padded with empty keys: nothing proves Split keeps TRAILING empty fields, and
	//! a dropped tail can only ever have been empty keys.
	static bool Decode(string value, notnull array<string> outGuids)
	{
		outGuids.Clear();

		array<string> fields = {};
		value.Split(",", fields, false);
		if (fields.IsEmpty() || fields.Count() > SLOT_COUNT)
			return false;

		while (fields.Count() < SLOT_COUNT)
			fields.Insert("");

		foreach (string field : fields)
		{
			if (field != "" && GuidOf(field) != field)
			{
				outGuids.Clear();
				return false;
			}

			outGuids.Insert(field);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! The body a bar may be saved from: this client's own, alive and not possessed. Null otherwise,
	//! which is what hides the menu item and what the save itself refuses on.
	static IEntity SaveableBody()
	{
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc || pc.IsPossessing())
			return null;

		ChimeraCharacter character = ChimeraCharacter.Cast(pc.GetControlledEntity());
		if (!character)
			return null;

		CharacterControllerComponent ctrl = character.GetCharacterController();
		if (!ctrl || ctrl.IsDead())
			return null;

		return character;
	}

	//------------------------------------------------------------------------------------------------
	//! Keys 6-0 of the body as prefabs, Empty for an empty key or one holding anything but an item: a
	//! squad command bound from the commanding radial is not gear, and is saved as an empty key.
	static bool Capture(notnull IEntity character, notnull array<ResourceName> outBar)
	{
		outBar.Clear();

		SCR_CharacterInventoryStorageComponent storage = SCR_CharacterInventoryStorageComponent.Cast(
			character.FindComponent(SCR_CharacterInventoryStorageComponent));
		if (!storage)
			return false;

		for (int i = FIRST_SLOT; i < FIRST_SLOT + SLOT_COUNT; i++)
		{
			outBar.Insert(PrefabOf(storage.GetItemFromQuickSlot(i)));
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! What the bar names that the kit would not carry, one prefab per missing copy - smoke on two
	//! keys needs two smokes. Weighed against today's config, resolved the way the deploy preview
	//! resolves it. False when the kit cannot be resolved at all.
	static bool MissingFrom(string kitName, string picksWire, notnull array<ResourceName> bar,
		notnull array<ResourceName> outMissing)
	{
		outMissing.Clear();

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return false;

		array<ref RK29_ChoicePick> picks = {};
		RK29_KitResolve.ParsePicks(picksWire, picks);

		RK29_KitStruct edited;
		ResourceName optic;
		array<ref RK29_AttachmentOrder> orders;
		map<int, ref array<ref RK29_LoadedPick>> loadedMags;
		if (!mgr.RK29_ResolvePreviewKit(kitName, picks, edited, optic, orders, loadedMags) || !edited)
			return false;

		map<string, int> carried = new map<string, int>();
		CountKit(edited, carried);

		foreach (ResourceName prefab : bar)
		{
			string guid = GuidOf(prefab);
			if (guid == "")
				continue;

			int left;
			if (carried.Find(guid, left) && left > 0)
				carried.Set(guid, left - 1);
			else
				outMissing.Insert(prefab);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! What the kit puts on the body that a key may hold, by GUID - exactly what Eligible can pick, or a
	//! save is accepted that never restores. Left out: clothing (no garment goes on a key), loaded
	//! magazines (in the gun), and everything in a weapon slot - the guns and the one grenade
	//! RK29_KitApply primes into the grenade slot.
	protected static void CountKit(notnull RK29_KitStruct kit, notnull map<string, int> outCounts)
	{
		foreach (RK29_KitItemBatch batch : kit.m_aItems)
		{
			foreach (ResourceName prefab : batch.m_aPrefabs)
				AddCount(outCounts, prefab);
		}

		foreach (string slot, ResourceName prefab : kit.m_mEquipment)
			AddCount(outCounts, prefab);

		foreach (string key, ResourceName prefab : kit.m_mGarmentAttachments)
			AddCount(outCounts, prefab);

		string primed = GuidOf(RK29_KitApply.GrenadeToPrime(kit));
		int n;
		if (primed != "" && outCounts.Find(primed, n))
			outCounts.Set(primed, n - 1);
	}

	//------------------------------------------------------------------------------------------------
	protected static void AddCount(notnull map<string, int> counts, ResourceName prefab)
	{
		string guid = GuidOf(prefab);
		if (guid == "")
			return;

		int n;
		counts.Find(guid, n);
		counts.Set(guid, n + 1);
	}
}

//------------------------------------------------------------------------------------------------
//! The one pending restore on this client. Armed on a body that is ours and wears a saved kit with a
//! bar; fires once every key the bar fills has a NEW copy of its item on that body, plus SETTLE_MS.
//! The rest of a kit reaches the owner in one burst, and the settle lets that burst land before the
//! five keys are written - vanilla only ever auto-places an arriving item into an EMPTY key, so once
//! nothing is still arriving it cannot fill a key the bar keeps empty.
//!
//! New copies only: on a re-kit the old gear is still on this client until its deletion replicates,
//! and counting it would fire on the outgoing kit. The request snapshots what the body held
//! (NoteRequest); a fresh body has no snapshot and everything on it is new.
//!
//! Past GIVE_UP_MS without every item, nothing is touched - vanilla's own bar stands - and the
//! player is told.
//------------------------------------------------------------------------------------------------
class RK29_QuickBarRestore
{
	protected static const float SETTLE_MS = 1000;
	protected static const float GIVE_UP_MS = 30000;

	protected static ref RK29_QuickBarRestore s_Instance;

	//! The body the bar is for. A restore never follows the player to another one.
	protected IEntity m_Body;

	//! One GUID per key 6-0, "" for a key the bar keeps empty.
	protected ref array<string> m_aWanted = {};

	//! Items on the body when the request left; null for a fresh body.
	protected ref map<EntityID, bool> m_mOld;

	protected string m_sKitName;
	protected string m_sPresetName;
	protected bool m_bSettling;

	//! What NoteRequest saw, held until the next arm on that body consumes it.
	protected IEntity m_SnapshotBody;
	protected ref map<EntityID, bool> m_mSnapshot;

	//! The saved kit the player applied from the menu, which outranks a twin holding the same picks.
	protected string m_sIntentKit;
	protected string m_sIntentPreset;

	//! The last body that became ours while not possessing. Control coming back to it - a possessed
	//! AI dying returns the player to their own body - is not a fresh body, whatever order the
	//! possession flag and the entity change replicate in.
	protected IEntity m_OwnBody;

	//------------------------------------------------------------------------------------------------
	protected static RK29_QuickBarRestore Instance()
	{
		if (!s_Instance)
			s_Instance = new RK29_QuickBarRestore();

		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	//! A request is about to leave for this body: remember what it holds now. Every send, request or
	//! seed, because a seed re-dresses a living body exactly like a request. The watch and binocular
	//! slots stay out: DressEquipment KEEPS a same-prefab occupant, so binoculars on a key would
	//! never get a new copy; one it replaces arrives under a new ID anyway.
	static void NoteRequest(IEntity body)
	{
		RK29_QuickBarRestore self = Instance();
		self.m_SnapshotBody = null;
		self.m_mSnapshot = null;

		SCR_InventoryStorageManagerComponent manager = ManagerOf(body);
		if (!manager)
			return;

		array<IEntity> items = {};
		manager.GetItems(items);

		self.m_mSnapshot = new map<EntityID, bool>();
		foreach (IEntity item : items)
		{
			if (item && !InManagedEquipment(item, body))
				self.m_mSnapshot.Set(item.GetID(), true);
		}

		self.m_SnapshotBody = body;
	}

	//------------------------------------------------------------------------------------------------
	//! The request was cancelled: nothing on the body is about to change.
	static void ForgetRequest()
	{
		if (!s_Instance)
			return;

		s_Instance.m_SnapshotBody = null;
		s_Instance.m_mSnapshot = null;
	}

	//------------------------------------------------------------------------------------------------
	//! Which saved kit the player just applied, for the twin tie-break in PresetFor. "" forgets.
	static void NoteIntent(string kitName, string presetName)
	{
		RK29_QuickBarRestore self = Instance();
		self.m_sIntentKit = kitName;
		self.m_sIntentPreset = presetName;
	}

	//------------------------------------------------------------------------------------------------
	//! The server has settled this client's kit (RK29_RpcDo_KitSaved, after the stash is marked). A
	//! living body got it in the same frame - the live re-dress commits as it runs - so that body is
	//! armed now. With no living body the next possession arms instead.
	static void OnKitSaved(notnull SCR_PlayerController pc)
	{
		if (pc.IsPossessing())
			return;

		Instance().Arm(pc.GetControlledEntity());
	}

	//------------------------------------------------------------------------------------------------
	//! A body became ours, or stopped being. A restore never outlives its body, and only a fresh one is
	//! armed: `from` gone or dead, and not the body control is returning to after a possession. A stock
	//! row body (Recruit, Training Staff) is armed with the stashed kit's bar too - owner ruling
	//! 2026-10-13, not worth a gate.
	static void OnPossessed(notnull SCR_PlayerController pc, IEntity from, IEntity to)
	{
		if (s_Instance && s_Instance.m_Body && s_Instance.m_Body != to)
			s_Instance.Disarm();

		if (!to || pc.IsPossessing() || IsAlive(from))
			return;

		RK29_QuickBarRestore self = Instance();
		if (to == self.m_OwnBody)
			return;

		self.m_OwnBody = to;
		self.Arm(to);
	}

	//------------------------------------------------------------------------------------------------
	//! Session statics survive a world rebuild inside one process; the callqueue does not.
	static void Clear()
	{
		if (!s_Instance)
			return;

		s_Instance.Disarm();
		s_Instance = null;
	}

	//------------------------------------------------------------------------------------------------
	protected void Arm(IEntity body)
	{
		Disarm();

		// consumed by whichever arm comes next: a snapshot is only ever about the request just sent
		map<EntityID, bool> old;
		if (body && body == m_SnapshotBody)
			old = m_mSnapshot;
		m_SnapshotBody = null;
		m_mSnapshot = null;

		if (!IsAlive(body))
			return;

		SCR_InventoryStorageManagerComponent manager = ManagerOf(body);
		if (!manager)
			return;

		string kitName = RK29_LocalStash.Kit();
		string presetName = PresetFor(kitName, RK29_LocalStash.Picks());
		if (presetName == "")
			return;

		RK29_KitPresetStorage store = RK29_KitPresetStorage.GetInstance();
		if (!store)
			return;

		string value = store.QuickBarOf(kitName, presetName);
		if (value == "")
			return;

		if (!RK29_QuickBar.Decode(value, m_aWanted))
		{
			Print(string.Format("[RK29] quick bar: saved kit '%1' holds a quick bar this build cannot"
				+ " read - left alone", presetName), LogLevel.WARNING);
			return;
		}

		m_Body = body;
		m_mOld = old;
		m_sKitName = kitName;
		m_sPresetName = presetName;

		manager.m_OnItemAddedInvoker.Insert(OnItemAdded);
		GetGame().GetCallqueue().CallLater(GiveUp, GIVE_UP_MS, false);

		// a fresh body can already carry everything by the time it is ours
		Check();
	}

	//------------------------------------------------------------------------------------------------
	//! The manager is looked up from m_Body every time, never kept: a component is not Managed, so a
	//! kept pointer dangles once a world teardown deletes the body, and Boot's Clear lands here then.
	//! An IEntity is nulled on delete, and a deleted body's invoker went with it.
	protected void Disarm()
	{
		SCR_InventoryStorageManagerComponent manager = ManagerOf(m_Body);
		if (manager)
			manager.m_OnItemAddedInvoker.Remove(OnItemAdded);

		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
		{
			queue.Remove(GiveUp);
			queue.Remove(Apply);
		}

		m_Body = null;
		m_mOld = null;
		m_aWanted.Clear();
		m_bSettling = false;
	}

	//------------------------------------------------------------------------------------------------
	//! The saved kit these picks are: the one the player applied when it is a twin of the answer,
	//! otherwise the store's own answer - the same first-match NameForPicks the last-used record takes.
	protected string PresetFor(string kitName, string picksWire)
	{
		RK29_KitPresetStorage store = RK29_KitPresetStorage.GetInstance();
		if (!store)
			return "";

		string name = store.NameForPicks(kitName, picksWire);
		if (name == "" || m_sIntentKit != kitName || m_sIntentPreset == "" || m_sIntentPreset == name)
			return name;

		RK29_KitPreset answer = store.Find(kitName, name);
		RK29_KitPreset intent = store.Find(kitName, m_sIntentPreset);
		if (answer && intent && intent.m_sPicks == answer.m_sPicks)
			return m_sIntentPreset;

		return name;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnItemAdded(IEntity item, BaseInventoryStorageComponent storage)
	{
		Check();
	}

	//------------------------------------------------------------------------------------------------
	protected void Check()
	{
		if (m_bSettling || !m_Body)
			return;

		array<IEntity> chosen = {};
		int unmet;
		if (!Assign(chosen, unmet))
			return;

		m_bSettling = true;
		SCR_InventoryStorageManagerComponent manager = ManagerOf(m_Body);
		if (manager)
			manager.m_OnItemAddedInvoker.Remove(OnItemAdded);

		GetGame().GetCallqueue().Remove(GiveUp);
		GetGame().GetCallqueue().CallLater(Apply, SETTLE_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Writes all five keys: the chosen item, or empty. Assigned again rather than reused from Check -
	//! a second has passed, and an item can have been used or dropped in it.
	protected void Apply()
	{
		m_bSettling = false;

		if (!m_Body || m_Body != SCR_PlayerController.GetLocalControlledEntity() || !IsAlive(m_Body)
			|| Possessing())
		{
			Disarm();
			return;
		}

		array<IEntity> chosen = {};
		int unmet;
		if (!Assign(chosen, unmet))
		{
			GiveUp();
			return;
		}

		SCR_CharacterInventoryStorageComponent storage = SCR_CharacterInventoryStorageComponent.Cast(
			m_Body.FindComponent(SCR_CharacterInventoryStorageComponent));
		if (!storage)
		{
			Disarm();
			return;
		}

		foreach (int i, IEntity item : chosen)
		{
			int index = RK29_QuickBar.FIRST_SLOT + i;
			if (item)
				storage.StoreItemToQuickSlot(item, index, true);
			else
				storage.RemoveItemFromQuickSlotAtIndex(index);
		}

		SCR_WeaponSwitchingBaseUI.RefreshQuickSlots();

		RK29_Log.Trace(string.Format("[RK29] quick bar: restored '%1' of %2", m_sPresetName,
			m_sKitName));
		Disarm();
	}

	//------------------------------------------------------------------------------------------------
	//! Not every item arrived: vanilla's bar stands untouched. A body that died waiting is not news,
	//! nor is an AI a GM possesses.
	protected void GiveUp()
	{
		if (!IsAlive(m_Body) || Possessing())
		{
			Disarm();
			return;
		}

		array<IEntity> chosen = {};
		int unmet = -1;
		Assign(chosen, unmet);

		string missing;
		if (unmet >= 0)
			missing = string.Format(" - key %1 never got a new %2",
				RK29_QuickBar.KeyOf(RK29_QuickBar.FIRST_SLOT + unmet), m_aWanted[unmet]);

		Print(string.Format("[RK29] quick bar: '%1' of %2 not restored%3", m_sPresetName, m_sKitName,
			missing), LogLevel.WARNING);
		SCR_NotificationsComponent.SendLocal(ENotification.RK29_QUICKBAR_NOT_RESTORED);
		Disarm();
	}

	//------------------------------------------------------------------------------------------------
	//! One distinct item per filled key, or false with `outUnmet` the first key position (0-5) that
	//! has none. An item already on its key keeps it, so nothing is shuffled between equal copies.
	protected bool Assign(notnull array<IEntity> outChosen, out int outUnmet)
	{
		outChosen.Clear();
		outUnmet = -1;

		SCR_InventoryStorageManagerComponent manager = ManagerOf(m_Body);
		if (!manager)
			return false;

		SCR_CharacterInventoryStorageComponent storage = SCR_CharacterInventoryStorageComponent.Cast(
			m_Body.FindComponent(SCR_CharacterInventoryStorageComponent));
		if (!storage)
			return false;

		array<IEntity> items = {};
		manager.GetItems(items);

		array<string> guids = {};
		foreach (IEntity item : items)
		{
			guids.Insert(RK29_QuickBar.GuidOf(RK29_QuickBar.PrefabOf(item)));
		}

		set<IEntity> taken = new set<IEntity>();
		foreach (int i, string wanted : m_aWanted)
		{
			if (wanted == "")
			{
				outChosen.Insert(null);
				continue;
			}

			int index = RK29_QuickBar.FIRST_SLOT + i;
			IEntity pick;

			IEntity current = storage.GetItemFromQuickSlot(index);
			if (current && !taken.Contains(current))
			{
				int at = items.Find(current);
				if (at >= 0 && guids[at] == wanted && Eligible(current))
					pick = current;
			}

			// a copy on no key, or on one of ours, before one vanilla filed on keys 1-5: taking that
			// one would blank its key, which this bar never writes
			for (int pass = 0; pass < 2 && !pick; pass++)
			{
				foreach (int j, IEntity candidate : items)
				{
					if (!candidate || guids[j] != wanted || taken.Contains(candidate)
						|| !Eligible(candidate))
						continue;

					int on = storage.GetEntityIndexInQuickslots(candidate);
					if (pass == 0 && on >= 0 && on < RK29_QuickBar.FIRST_SLOT)
						continue;

					pick = candidate;
					break;
				}
			}

			if (!pick)
			{
				outUnmet = i;
				return false;
			}

			taken.Insert(pick);
			outChosen.Insert(pick);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether this copy may go on a key. Never one from before the request. Never one in a weapon
	//! slot: it owns its slot's key, and moving it would blank that key. Never one inside a weapon - a
	//! magazine in the gun, a grenade in a launcher. The attachment-storage test catches the UGL
	//! chamber, whose holder is the launcher attachment and carries no BaseWeaponComponent; the
	//! holder test catches the rest, with the body exempt, as its weapon slots are
	//! BaseWeaponComponents too. Cargo never lives in an attachment storage.
	protected bool Eligible(notnull IEntity item)
	{
		if (m_mOld && m_mOld.Contains(item.GetID()))
			return false;

		InventoryItemComponent iic = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
		if (!iic)
			return false;

		InventoryStorageSlot slot = iic.GetParentSlot();
		if (!slot)
			return false;

		BaseInventoryStorageComponent storage = slot.GetStorage();
		if (!storage)
			return false;

		if (EquipedWeaponStorageComponent.Cast(storage)
			|| WeaponAttachmentsStorageComponent.Cast(storage))
			return false;

		IEntity holder = storage.GetOwner();
		if (holder && holder != m_Body && holder.FindComponent(BaseWeaponComponent))
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Read again when a timer fires: OnPossessed can see a possessed AI become ours before
	//! m_bIsPossessing replicates (a GM with no living body of their own), and the RplProp has landed
	//! by the time the settle or the give-up runs.
	protected static bool Possessing()
	{
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		return pc && pc.IsPossessing();
	}

	//------------------------------------------------------------------------------------------------
	protected static bool InManagedEquipment(notnull IEntity item, IEntity body)
	{
		InventoryItemComponent iic = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
		if (!iic)
			return false;

		InventoryStorageSlot slot = iic.GetParentSlot();
		if (!slot || !slot.GetStorage())
			return false;

		return RK29_KitApply.IsManagedEquipmentStorage(slot.GetStorage(), body);
	}

	//------------------------------------------------------------------------------------------------
	protected static SCR_InventoryStorageManagerComponent ManagerOf(IEntity body)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(body);
		if (!character)
			return null;

		CharacterControllerComponent ctrl = character.GetCharacterController();
		if (!ctrl)
			return null;

		return SCR_InventoryStorageManagerComponent.Cast(ctrl.GetInventoryStorageManager());
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsAlive(IEntity body)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(body);
		if (!character)
			return false;

		CharacterControllerComponent ctrl = character.GetCharacterController();
		return ctrl && !ctrl.IsDead();
	}
}
