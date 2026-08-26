//------------------------------------------------------------------------------------------------
//! /kitvalidate - dry-runs every kit and reports what would not fit.
//!
//! Spawns each kit's own prefab locally, waits out the engine's async item-init, runs the REAL
//! apply against it, records the drops, then deletes the body. Reusing the live pipeline is the
//! point: a static capacity estimate would drift from what the game actually does, and the
//! engine's fit test is the only trustworthy oracle for whether an item has a home.
//------------------------------------------------------------------------------------------------
//! One kit + one weapon choice to test.
class RK29_ValidateJob
{
	string m_sKit;
	string m_sLabel;
	ResourceName m_sWeapon;   //!< empty = the class default
}

//------------------------------------------------------------------------------------------------
class RK29_KitValidate
{
	//! matches the apply path's own item-init guard - a body younger than this still has
	//! stock items landing on it, which would poison the result
	protected static const int SETTLE_MS = 750;

	//! Async storage spawns land over several frames, and a crammed kit queues more of them
	//! than a roomy one - so the audit polls until the body stops changing rather than betting
	//! on a fixed delay. A short fixed wait reports the slowest kits as failures for no reason.
	protected static const int AUDIT_POLL_MS = 150;

	//! consecutive unchanged polls that count as "the spawns are done"
	protected static const int AUDIT_STABLE = 3;

	//! ceiling so a genuinely stuck body cannot stall the queue (~3s)
	protected static const int AUDIT_MAX_POLLS = 20;

	protected static int s_iAuditPolls;
	protected static const string REPORT = "$profile:RK29_KitValidation.txt";

	protected static ref array<ref RK29_ValidateJob> s_aQueue = {};
	protected static ref array<string> s_aReport = {};
	protected static bool s_bRunning;
	protected static int s_iFailures;

	//--------------------------------------------------------------------------------------------
	static void Run()
	{
		if (!Replication.IsServer())
		{
			Print("[RK29] kitvalidate - server only", LogLevel.WARNING);
			return;
		}
		if (s_bRunning)
		{
			Print("[RK29] kitvalidate already running", LogLevel.NORMAL);
			return;
		}

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return;

		// Plans are cached for the session and keyed on kit+weapon, so anything already
		// spawned this session would be REPLAYED rather than solved - against whatever the
		// config looked like then. A validation run that replays is not a validation.
		RK29_KitApply.ClearPlans();

		s_aQueue.Clear();
		s_aReport.Clear();
		s_iFailures = 0;
		s_bRunning = true;

		// one job per WEAPON, not per kit: a kit that fits with the M249 can overflow with
		// the M60's extra belt and pack, and only the default has a prefab to be judged by
		foreach (string kitName, RK29_KitStruct kit : mgr.m_mKits)
		{
			if (!kit || kit.m_sSourcePrefab == ResourceName.Empty)
				continue;

			RK29_WeaponSlot primary = mgr.m_Setup.FindSlot(mgr.m_mKitOptions.Get(kitName), 0);
			int primaries = 0;
			if (primary && primary.m_aOptions)
			{
				foreach (RK29_WeaponOption option : primary.m_aOptions)
				{
					if (!option)
						continue;
					ResourceName prefab = mgr.m_Setup.WeaponPrefabOf(option, kit.m_sFactionKey);
					if (prefab == ResourceName.Empty)
						continue;

					RK29_ValidateJob job = new RK29_ValidateJob();
					job.m_sKit = kitName;
					job.m_sWeapon = prefab;
					job.m_sLabel = kitName + " [" + RK29_ItemNames.Get(prefab) + "]";
					s_aQueue.Insert(job);
					primaries++;
				}
			}
			if (primaries > 0)
				continue;

			RK29_ValidateJob plain = new RK29_ValidateJob();
			plain.m_sKit = kitName;
			plain.m_sLabel = kitName;
			s_aQueue.Insert(plain);
		}

		Print("[RK29] kitvalidate - checking " + s_aQueue.Count().ToString()
			+ " kit(s), settle " + SETTLE_MS.ToString() + "ms then poll until the spawns land",
			LogLevel.NORMAL);
		GetGame().GetCallqueue().CallLater(Step, 100, false);
	}

	//--------------------------------------------------------------------------------------------
	//! One kit per tick: spawn, settle, apply, record, delete. Sequential rather than parallel
	//! so a slow frame cannot overlap two test bodies.
	protected static void Step()
	{
		if (s_aQueue.IsEmpty())
		{
			Finish();
			return;
		}

		RK29_ValidateJob job = s_aQueue[0];
		s_aQueue.RemoveOrdered(0);
		string kitName = job.m_sKit;

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		RK29_KitStruct kit;
		if (mgr)
			kit = mgr.m_mKits.Get(kitName);
		if (!kit)
		{
			GetGame().GetCallqueue().CallLater(Step, 10, false);
			return;
		}

		Resource res = Resource.Load(kit.m_sSourcePrefab);
		if (!res.IsValid())
		{
			s_aReport.Insert(string.Format("%1  SKIP  prefab will not load", job.m_sLabel));
			GetGame().GetCallqueue().CallLater(Step, 10, false);
			return;
		}

		// far under the map and local-only: never replicated, never seen, never collided with
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[3] = Vector(0, -5000, 0);

		IEntity body = GetGame().SpawnEntityPrefabLocal(res, GetGame().GetWorld(), params);
		if (!body)
		{
			s_aReport.Insert(string.Format("%1  SKIP  spawn failed", job.m_sLabel));
			GetGame().GetCallqueue().CallLater(Step, 10, false);
			return;
		}

		GetGame().GetCallqueue().CallLater(Check, SETTLE_MS, false, job, body);
	}

	//--------------------------------------------------------------------------------------------
	protected static void Check(RK29_ValidateJob job, IEntity body)
	{
		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		RK29_KitStruct kit;
		if (mgr)
			kit = mgr.m_mKits.Get(job.m_sKit);

		// a non-default weapon is composed the same way the apply path composes it
		if (kit && mgr && job.m_sWeapon != ResourceName.Empty)
		{
			RK29_KitStruct base = mgr.m_mKitsBase.Get(job.m_sKit);
			if (base)
				kit = RK29_KitCompose.ApplyWeaponChoices(base, mgr.m_mKitOptions.Get(job.m_sKit), job.m_sWeapon, mgr.m_Setup);
		}

		if (!kit || !body)
		{
			if (body)
				SCR_EntityHelper.DeleteEntityAndChildren(body);
			GetGame().GetCallqueue().CallLater(Step, 10, false);
			return;
		}

		array<ResourceName> dropped;
		array<ResourceName> mounts;
		RK29_KitApply.Apply(body, kit, ResourceName.Empty, mounts, dropped);

		// TrySpawnPrefabToStorage is async - it reports that the request was accepted, not that
		// the item landed. Reading the body once the spawns have settled is the only honest
		// answer to "did it fit", so the verdict waits for the audit rather than trusting the
		// solver's own tally.
		s_iAuditPolls = 0;
		GetGame().GetCallqueue().CallLater(Audit, AUDIT_POLL_MS, false, job, body, kit, dropped, 0, 0);
	}

	//--------------------------------------------------------------------------------------------
	//! Counts what is actually carried and diffs it against what the kit asked for. An item the
	//! solver believed it placed but which is not on the body is an overflow the old check missed.
	protected static void Audit(RK29_ValidateJob job, IEntity body, RK29_KitStruct kit, array<ResourceName> dropped, int prevCount, int stable)
	{
		if (!body || !kit)
		{
			if (body)
				SCR_EntityHelper.DeleteEntityAndChildren(body);
			GetGame().GetCallqueue().CallLater(Step, 10, false);
			return;
		}

		array<ResourceName> intended = {};
		foreach (RK29_KitItemBatch batch : kit.m_aItems)
		{
			if (!batch)
				continue;
			foreach (ResourceName prefab : batch.m_aPrefabs)
				intended.Insert(prefab);
		}

		array<BaseInventoryStorageComponent> storages = {};
		array<IEntity> carried = {};
		CollectCarried(body, storages, carried);

		map<ResourceName, int> have = new map<ResourceName, int>();
		foreach (IEntity item : carried)
		{
			EntityPrefabData epd = item.GetPrefabData();
			if (!epd)
				continue;
			ResourceName rn = epd.GetPrefabName();
			int seen;
			have.Find(rn, seen);
			have.Set(rn, seen + 1);
		}

		// consume one per intended copy, so 7 magazines asked for and 5 present reports 2 missing
		array<ResourceName> missing = {};
		foreach (ResourceName want : intended)
		{
			int left;
			if (have.Find(want, left) && left > 0)
			{
				have.Set(want, left - 1);
				continue;
			}
			missing.Insert(want);
		}

		// Nothing outstanding, or the body has stopped gaining items - either way the picture
		// is final. Until then this is just a mid-flight snapshot and must not be judged.
		s_iAuditPolls++;
		if (!missing.IsEmpty() && stable < AUDIT_STABLE && s_iAuditPolls < AUDIT_MAX_POLLS)
		{
			int nextStable;
			if (carried.Count() == prevCount)
				nextStable = stable + 1;
			GetGame().GetCallqueue().CallLater(Audit, AUDIT_POLL_MS, false, job, body, kit, dropped, carried.Count(), nextStable);
			return;
		}

		s_aReport.Insert(string.Format("        fill: %1", FillLine(storages)));

		array<ResourceName> crammed = RK29_KitApply.LastCrammed();
		int tight;
		if (crammed)
			tight = crammed.Count();

		if (dropped && !dropped.IsEmpty())
		{
			s_iFailures++;
			s_aReport.Insert(string.Format("%1  FAIL  %2 dropped: %3", job.m_sLabel, dropped.Count(), NameList(dropped)));
		}
		else if (!missing.IsEmpty())
		{
			// raw prefab basenames, because two different prefabs can share a friendly name -
			// this line says whether the item is absent or merely a different resource
			s_aReport.Insert(string.Format("        want: %1", FileList(intended)));
			s_aReport.Insert(string.Format("        have: %1", CarriedList(carried)));
			WhyMissing(body, storages, missing);
			array<string> sent = RK29_KitApply.LastSent();
			foreach (ResourceName gone : missing)
			{
				string file = BaseName("" + gone);
				foreach (string entry : sent)
				{
					if (entry.StartsWith(file))
						s_aReport.Insert(string.Format("        solver sent %1", entry));
				}
			}
			s_iFailures++;
			s_aReport.Insert(string.Format("%1  FAIL  %2 of %3 item(s) never landed: %4",
				job.m_sLabel, missing.Count(), intended.Count(), NameList(missing)));
		}
		else if (tight > 0)
		{
			// TIGHT is not a failure: everything the kit carries is on the character. It is a
			// note that the fit was forced, which is sometimes exactly right - a scarce item
			// claiming a dedicated slot displaces whatever was squatting in it.
			s_aReport.Insert(string.Format("%1  TIGHT %2 item(s) carried, %3 only fit by displacing something or falling back: %4",
				job.m_sLabel, intended.Count(), tight, NameList(crammed)));
		}
		else
		{
			s_aReport.Insert(string.Format("%1  OK    %2 item(s) carried", job.m_sLabel, intended.Count()));
		}

		SCR_EntityHelper.DeleteEntityAndChildren(body);
		GetGame().GetCallqueue().CallLater(Step, 10, false);
	}

	//--------------------------------------------------------------------------------------------
	//! Breadth-first over every storage the body owns, including the ones nested inside pouches
	//! and the weapon/throwable slots, so nothing carried is missed and nothing is counted twice.
	protected static void CollectCarried(notnull IEntity body, notnull array<BaseInventoryStorageComponent> outStorages, notnull array<IEntity> outItems)
	{
		array<Managed> roots = {};
		body.FindComponents(BaseInventoryStorageComponent, roots);
		foreach (Managed root : roots)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(root);
			if (storage)
				outStorages.Insert(storage);
		}

		for (int q = 0; q < outStorages.Count(); q++)
		{
			array<IEntity> here = {};
			outStorages[q].GetAll(here, true);
			foreach (IEntity item : here)
			{
				if (!item || outItems.Contains(item))
					continue;
				outItems.Insert(item);

				array<Managed> nested = {};
				item.FindComponents(BaseInventoryStorageComponent, nested);
				foreach (Managed n : nested)
				{
					BaseInventoryStorageComponent child = BaseInventoryStorageComponent.Cast(n);
					if (child && !outStorages.Contains(child))
						outStorages.Insert(child);
				}
			}
		}

		// A gadget taken into the left hand is ATTACHED to the character, not stored, so it is
		// invisible to every storage query. It is still carried, and the audit must say so.
		ChimeraCharacter chimera = ChimeraCharacter.Cast(body);
		if (chimera)
		{
			CharacterControllerComponent controller = chimera.GetCharacterController();
			if (controller)
			{
				IEntity inHand = controller.GetAttachedGadgetAtLeftHandSlot();
				if (inHand && !outItems.Contains(inHand))
					outItems.Insert(inHand);
			}
		}

		// The engine's own "everything this character carries" call, folded in afterwards so the
		// walk above still harvests every nested storage first. Used alongside rather than
		// instead of it: whichever one has a blind spot, the union does not.
		SCR_InventoryStorageManagerComponent manager = SCR_InventoryStorageManagerComponent.Cast(
			body.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!manager)
			return;

		array<IEntity> managed = {};
		manager.GetItems(managed, EStoragePurpose.PURPOSE_ANY);
		foreach (IEntity item : managed)
		{
			if (item && !outItems.Contains(item))
				outItems.Insert(item);
		}
	}

	//--------------------------------------------------------------------------------------------
	//! "Vest_ALICE(2870/3300) Pants(1500/1600)" - occupancy after the fact, which is what says
	//! whether a kit is riding the ceiling. Token slots are skipped; they carry no volume.
	protected static string FillLine(notnull array<BaseInventoryStorageComponent> storages)
	{
		string line;
		foreach (BaseInventoryStorageComponent storage : storages)
		{
			float occupied, max;
			StorageVolume(storage, occupied, max);
			if (max <= 1)
				continue;

			IEntity owner = storage.GetOwner();
			string name = "?";
			if (owner)
			{
				EntityPrefabData epd = owner.GetPrefabData();
				if (epd)
				{
					string raw = "" + epd.GetPrefabName();
					int lastSlash = raw.LastIndexOf("/");
					if (lastSlash >= 0)
						raw = raw.Substring(lastSlash + 1, raw.Length() - lastSlash - 1);
					name = raw;
				}
			}

			if (line != string.Empty)
				line += " ";
			line += string.Format("%1(%2/%3)", name, Math.Round(occupied), Math.Round(max));
		}
		return line;
	}

	//--------------------------------------------------------------------------------------------
	//! Volume the way the vanilla inventory UI measures it. A ClothNodeStorageComponent - every
	//! vest and belt rig - is not itself one pocket: its own occupied figure counts the ITEM
	//! volume of whatever is strapped to it (suspenders, pouches, buttpack), while its capacity
	//! governs only the internal compartment. Reading the two directly is apples to oranges and
	//! reported an ALICE vest as 19055/3300 on a kit the validator had just passed. Vanilla sums
	//! BOTH sides across the owned universal sub-storages instead - see
	//! SCR_InventoryStorageBaseUI.GetOccupiedVolume() / GetMaxVolumeCapacity() - so mirror that.
	protected static void StorageVolume(notnull BaseInventoryStorageComponent storage, out float occupied, out float max)
	{
		occupied = 0;
		max = 0;

		if (!ClothNodeStorageComponent.Cast(storage))
		{
			occupied = storage.GetOccupiedSpace();
			max = storage.GetMaxVolumeCapacity();
			return;
		}

		array<BaseInventoryStorageComponent> subStorages = {};
		storage.GetOwnedStorages(subStorages, 1, false);
		foreach (BaseInventoryStorageComponent sub : subStorages)
		{
			if (!SCR_UniversalInventoryStorageComponent.Cast(sub))
				continue;
			occupied += sub.GetOccupiedSpace();
			max += sub.GetMaxVolumeCapacity();
		}
	}

	//--------------------------------------------------------------------------------------------
	//! For each item that never landed, ask the engine which containers would take it AS THE BODY
	//! STANDS. "nothing accepts it" means the kit is genuinely over capacity; anything listed
	//! means the placement solver had somewhere to put it and did not.
	protected static void WhyMissing(notnull IEntity body, notnull array<BaseInventoryStorageComponent> storages, notnull array<ResourceName> missing)
	{
		SCR_InventoryStorageManagerComponent manager = SCR_InventoryStorageManagerComponent.Cast(
			body.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!manager)
			return;

		array<ResourceName> reported = {};
		foreach (ResourceName prefab : missing)
		{
			if (reported.Contains(prefab))
				continue;
			reported.Insert(prefab);

			string takers;
			foreach (BaseInventoryStorageComponent storage : storages)
			{
				if (!manager.CanInsertResourceInStorage(prefab, storage, -1))
					continue;

				string owner = "?";
				IEntity holder = storage.GetOwner();
				if (holder)
				{
					EntityPrefabData epd = holder.GetPrefabData();
					if (epd)
						owner = BaseName("" + epd.GetPrefabName());
				}
				if (takers != string.Empty)
					takers += " ";
				takers += owner;
			}

			if (takers == string.Empty)
				takers = "<nothing accepts it - genuinely full>";
			s_aReport.Insert(string.Format("        why %1: %2", BaseName("" + prefab), takers));
		}
	}

	//--------------------------------------------------------------------------------------------
	//! Prefab basenames with duplicates folded, e.g. "7x Magazine_30rnd, PaperMap_01_folded_US".
	protected static string FileList(notnull array<ResourceName> prefabs)
	{
		array<string> names = {};
		foreach (ResourceName prefab : prefabs)
			names.Insert(BaseName("" + prefab));
		return Fold(names);
	}

	//--------------------------------------------------------------------------------------------
	//! The same, for entities actually on the body.
	protected static string CarriedList(notnull array<IEntity> carried)
	{
		array<string> names = {};
		foreach (IEntity item : carried)
		{
			EntityPrefabData epd = item.GetPrefabData();
			if (epd)
				names.Insert(BaseName("" + epd.GetPrefabName()));
			else
				names.Insert("<no prefab data>");
		}
		return Fold(names);
	}

	//--------------------------------------------------------------------------------------------
	protected static string BaseName(string path)
	{
		int lastSlash = path.LastIndexOf("/");
		if (lastSlash >= 0)
			path = path.Substring(lastSlash + 1, path.Length() - lastSlash - 1);
		return path;
	}

	//--------------------------------------------------------------------------------------------
	protected static string Fold(notnull array<string> names)
	{
		map<string, int> counts = new map<string, int>();
		array<string> order = {};
		foreach (string name : names)
		{
			int seen;
			if (!counts.Find(name, seen))
				order.Insert(name);
			counts.Set(name, seen + 1);
		}

		string line;
		foreach (string name : order)
		{
			if (line != string.Empty)
				line += ", ";
			int n = counts.Get(name);
			if (n > 1)
				line += n.ToString() + "x " + name;
			else
				line += name;
		}
		return line;
	}

	//--------------------------------------------------------------------------------------------
	//! "2x Field Dressing, Compass" - duplicates folded so one line stays readable.
	protected static string NameList(array<ResourceName> prefabs)
	{
		map<string, int> counts = new map<string, int>();
		array<string> order = {};
		foreach (ResourceName prefab : prefabs)
		{
			string name = RK29_ItemNames.Get(prefab);
			int seen;
			if (!counts.Find(name, seen))
				order.Insert(name);
			counts.Set(name, seen + 1);
		}

		string list;
		foreach (string name : order)
		{
			if (list != string.Empty)
				list += ", ";
			int n = counts.Get(name);
			if (n > 1)
				list += n.ToString() + "x " + name;
			else
				list += name;
		}
		return list;
	}
	//--------------------------------------------------------------------------------------------
	protected static void Finish()
	{
		s_bRunning = false;

		Print("[RK29] ===== kit validation =====", LogLevel.NORMAL);
		foreach (string line : s_aReport)
		{
			if (line.Contains("FAIL"))
				Print("[RK29] " + line, LogLevel.WARNING);
			else
				Print("[RK29] " + line, LogLevel.NORMAL);
		}

		string summary;
		if (s_iFailures == 0)
			summary = "all kits fit";
		else
			summary = s_iFailures.ToString() + " kit(s) overflow";
		Print("[RK29] ===== " + summary + " =====", LogLevel.NORMAL);

		FileHandle fh = FileIO.OpenFile(REPORT, FileMode.WRITE);
		if (!fh)
			return;
		fh.WriteLine("29th kit validation - " + summary);
		fh.WriteLine("");
		foreach (string line : s_aReport)
			fh.WriteLine(line);
		fh.Close();
		Print("[RK29] report written to " + REPORT, LogLevel.NORMAL);
	}
}
