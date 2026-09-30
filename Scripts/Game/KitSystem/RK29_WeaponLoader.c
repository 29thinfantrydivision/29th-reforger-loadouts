//------------------------------------------------------------------------------------------------
//! Per-gun takeover of loading a chamber (RK29_WeaponDef.m_Loader); none = the standard steps.
//! One stateless instance per weapon def, shared by menu, preview and server. Vanilla API only.
//! DO NOT change a hook's parameters - add a field to its context instead.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! What PlanLoad is asked with, and where it answers. Read-only apart from the answer fields.
class RK29_LoadContext
{
	RK29_KitSetup m_Setup;
	string m_sWeaponId;
	ResourceName m_sWeapon;
	RK29_ELoadedSeat m_eSeat;
	RK29_ResolvedGroup m_Group; // the ammo group feeding m_eSeat
	array<int> m_aCounts;       // per entry of m_Group: the player's totals

	// the answer: m_iRounds of entry m_iEntry go into the gun. The core clamps to what was picked
	int m_iEntry = -1;
	int m_iRounds;
}

//------------------------------------------------------------------------------------------------
//! What Seat is asked with. m_Seated is what the gun spawned in the chamber's well, or null.
class RK29_SeatContext
{
	IEntity m_Weapon;
	IEntity m_Seated;
	RK29_LoadedPick m_Order;
}

//------------------------------------------------------------------------------------------------
//! The defaults take over nothing.
[BaseContainerProps()]
class RK29_WeaponLoader
{
	//------------------------------------------------------------------------------------------------
	//! The one decision, per chamber. Where it says yes: no loaded-magazine picker is offered, the
	//! rounds come from PlanLoad, Seat seats them, and a gun the kit feeds nothing is seated with
	//! zero rounds instead of emptied. Where it says no, the other hooks are never called.
	bool TakesOver(RK29_ELoadedSeat seat)
	{
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Fill ctx.m_iEntry / ctx.m_iRounds. False = leave the gun as it spawned, every round carried.
	bool PlanLoad(notnull RK29_LoadContext ctx)
	{
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Put ctx.m_Order.m_iRounds into the gun, in place of the delete-and-spawn. False = nothing seated.
	bool Seat(notnull RK29_SeatContext ctx)
	{
		return false;
	}
}

//------------------------------------------------------------------------------------------------
//! A magazine built into the gun and fed a round at a time - a shotgun tube, a double barrel. The
//! rounds present the magazine's own well, so the standard steps would swap the tube for one shell
//! and the mod's reload would have nothing to fill. Assumes the chamber spawns empty (owner call,
//! unverified - IsCurrentBarrelChambered after an apply settles it).
[BaseContainerProps()]
class RK29_TubeLoader : RK29_WeaponLoader
{
	//------------------------------------------------------------------------------------------------
	override bool TakesOver(RK29_ELoadedSeat seat)
	{
		return seat == RK29_ELoadedSeat.OWN_MUZZLE;
	}

	//------------------------------------------------------------------------------------------------
	//! The default-flagged entry only (else the first): SetAmmoCount sets a count and the tube keeps
	//! its own round type, so rounds of another entry would change type. Those stay carried.
	override bool PlanLoad(notnull RK29_LoadContext ctx)
	{
		// unreadable: the gun keeps the full tube it spawned with rather than be emptied
		int room = RK29_KitCompose.MagazineCapacityOf(RK29_KitCompose.DefaultMagOf(ctx.m_sWeapon));
		if (room <= 0)
			return false;

		ctx.m_iEntry = 0;
		foreach (int i, RK29_ResolvedEntry e : ctx.m_Group.m_aEntries)
		{
			if (e && e.m_bDefault)
			{
				ctx.m_iEntry = i;
				break;
			}
		}
		ctx.m_iRounds = room;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool Seat(notnull RK29_SeatContext ctx)
	{
		if (!ctx.m_Seated)
			return false;

		// SetAmmoCount is master-only; the server's body and a client's local preview body both are
		RplComponent rpl = RplComponent.Cast(ctx.m_Seated.FindComponent(RplComponent));
		if (rpl && rpl.IsProxy())
			return false;

		BaseMagazineComponent tube = BaseMagazineComponent.Cast(ctx.m_Seated.FindComponent(BaseMagazineComponent));
		if (!tube)
			return false;

		tube.SetAmmoCount(Math.ClampInt(ctx.m_Order.m_iRounds, 0, tube.GetMaxAmmoCount()));
		return true;
	}
}
