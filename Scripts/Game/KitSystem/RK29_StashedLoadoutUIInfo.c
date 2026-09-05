//------------------------------------------------------------------------------------------------
//! What a "Current Kit" deploy row is offering - identity, name and preview - resolved through
//! the kit manager rather than off the row's own resource, which is only the side's shared body.
//! Every method answers null/""/false for any other row. Client-side, and asked per player.
//------------------------------------------------------------------------------------------------
class RK29_StashedLoadoutUIInfo
{
	//------------------------------------------------------------------------------------------------
	static SCR_EditableEntityUIInfo Resolve(SCR_BasePlayerLoadout loadout)
	{
		RK29_KitStruct kit = ResolveKit(loadout);
		if (!kit)
			return null;

		SCR_EditableEntityUIInfo info = SCR_EditableEntityUIInfo.Cast(kit.m_UIInfo);
		if (!info)
			return null;

		// SCR_LoadoutButton.SetLoadout() does entityUIInfo.GetFaction().GetFactionColor() unguarded, so a
		// UIInfo with an unset or unknown m_sFaction takes the deploy menu down the moment a row is
		// built. Refusing to hand ours over falls back to the prefab icon.
		if (!info.GetFaction())
		{
			Print(string.Format("[RK29] kit '%1' has a UIInfo with no usable m_sFaction -"
				+ " falling back to the prefab icon. Set m_sFaction in its composition",
				kit.m_sKitName), LogLevel.WARNING);
			return null;
		}

		return info;
	}

	//------------------------------------------------------------------------------------------------
	//! The kit behind a Current Kit row: the stash, or the side default. Resolved through the
	//! manager rather than off the local stash, so an untouched row shows Rifleman.
	protected static RK29_KitStruct ResolveKit(SCR_BasePlayerLoadout loadout)
	{
		RK29_CurrentKitLoadout currentKit = RK29_CurrentKitLoadout.Cast(loadout);
		if (!currentKit)
			return null;

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		PlayerController pc = GetGame().GetPlayerController();
		if (!mgr || !pc)
			return null;

		// what the server last applied wins, but only while it belongs to this row's side and is still
		// offered: the mirror is not cleared on a side change, and the server re-seeds at spawn on this
		// same test
		string kitName = RK29_LocalStash.Kit();
		if (kitName != "")
		{
			RK29_KitStruct stashed = mgr.KitByName(kitName);
			if (stashed && stashed.m_sFactionKey == currentKit.GetFactionKey()
				&& mgr.IsKitOffered(kitName))
				return stashed;
		}

		kitName = mgr.EffectiveKitFor(pc.GetPlayerId(), currentKit.GetFactionKey());
		if (kitName == "")
			return null;

		return mgr.KitByName(kitName);
	}

	//------------------------------------------------------------------------------------------------
	//! Dress and weapons for a Current Kit mannequin, resolved the way the server will. Null picks
	//! means the kit at its defaults. Hands back three plain maps rather than a resolved kit: the
	//! deploy mannequin is vanilla's preview entity, built for rendering, with nowhere to put cargo -
	//! the full apply drops every item and leaves a naked body. The F4 mannequin is a different body
	//! that does take the full apply.
	static bool ResolvePreviewLoadout(SCR_BasePlayerLoadout loadout,
		notnull map<string, ResourceName> outDress, notnull map<int, ResourceName> outWeapons,
		out ResourceName outOptic)
	{
		outDress.Clear();
		outWeapons.Clear();
		outOptic = ResourceName.Empty;

		if (!RK29_CurrentKitLoadout.Cast(loadout))
			return false;

		RK29_KitStruct kit = ResolveKit(loadout);
		if (!kit)
			return false;

		RK29_KitManager mgr = RK29_KitManager.GetInstance();
		if (!mgr)
			return false;

		// the resolver answers with the kit as picked - the weapon option and the sight it carries -
		// where the catalog copy only knows the class default
		array<ref RK29_ChoicePick> picks = {};
		if (RK29_LocalStash.Kit() == kit.m_sKitName)
			RK29_KitResolve.ParsePicks(RK29_LocalStash.Picks(), picks);
		RK29_KitStruct edited;
		array<ref RK29_AttachmentOrder> unusedOrders;
		map<int, ref array<ref RK29_LoadedPick>> unusedMags;
		if (!mgr.RK29_ResolvePreviewKit(kit.m_sKitName, picks, edited, outOptic, unusedOrders,
			unusedMags) || !edited)
			edited = kit;

		foreach (string slot, ResourceName garment : edited.m_mClothing)
		{
			if (garment != ResourceName.Empty)
				outDress.Set(slot, garment);
		}

		foreach (int idx, ResourceName weapon : edited.m_mWeapons)
		{
			if (weapon != ResourceName.Empty)
				outWeapons.Set(idx, weapon);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! The kit's own short name ("Automatic Rifleman"), not the literal loadout name. Empty for
	//! every other row, which keeps its own name.
	static string ResolveName(SCR_BasePlayerLoadout loadout)
	{
		RK29_KitStruct kit = ResolveKit(loadout);
		if (!kit)
			return "";

		return RK29_KitHud.ShortKitName(kit.m_sKitName);
	}
}
