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

		Print("[RK29] kitvalidate - checking " + s_aQueue.Count().ToString() + " kit(s), one every "
			+ SETTLE_MS.ToString() + "ms", LogLevel.NORMAL);
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

		if (kit && body)
		{
			array<ResourceName> dropped;
			array<ResourceName> mounts;
			RK29_KitApply.Apply(body, kit, ResourceName.Empty, mounts, dropped);

			if (!dropped || dropped.IsEmpty())
			{
				s_aReport.Insert(string.Format("%1  OK    %2 item(s) placed", job.m_sLabel, kit.CountItems()));
			}
			else
			{
				s_iFailures++;
				map<string, int> counts = new map<string, int>();
				array<string> order = {};
				foreach (ResourceName prefab : dropped)
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
				s_aReport.Insert(string.Format("%1  FAIL  %2 dropped: %3", job.m_sLabel, dropped.Count(), list));
			}
		}

		if (body)
			SCR_EntityHelper.DeleteEntityAndChildren(body);

		GetGame().GetCallqueue().CallLater(Step, 10, false);
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
