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
	//! The kit a Current Kit mannequin should wear, resolved the way the server will, with the
	//! attachment orders and seated rounds the dress needs. Null picks means the kit at its defaults.
	//! The picked sight travels in `outOrders`, not separately. False - and no kit - for every row
	//! that is not a Current Kit one.
	static bool ResolvePreviewLoadout(SCR_BasePlayerLoadout loadout, out RK29_KitStruct outKit,
		out array<ref RK29_AttachmentOrder> outOrders,
		out map<int, ref array<ref RK29_LoadedPick>> outLoadedMags)
	{
		outKit = null;
		outOrders = null;
		outLoadedMags = null;

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
		ResourceName unusedOptic;
		if (!mgr.RK29_ResolvePreviewKit(kit.m_sKitName, picks, edited, unusedOptic, outOrders,
			outLoadedMags) || !edited)
			edited = kit;

		outKit = edited;
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
