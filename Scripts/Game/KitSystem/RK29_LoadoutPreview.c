//------------------------------------------------------------------------------------------------
//! Deploy-menu preview for "Current Kit" rows: stands up its own body and runs the same dress the
//! F4 mannequin runs (RK29_MannequinDress.ApplyLoaded), then hands that body to vanilla's preview
//! widget. Every other row keeps vanilla's own cached entity, untouched.
//------------------------------------------------------------------------------------------------
modded class SCR_LoadoutPreviewComponent
{
	//! Our own dressed body, or null while a non-Current-Kit row is shown. A fresh one per dress is
	//! required, not an optimisation to undo - RK29_MannequinDress.ApplyLoaded has the reason.
	protected IEntity m_RK29PreviewBody;

	//! What m_RK29PreviewBody is already dressed from, empty whenever no body of ours is standing.
	//! Opening the deploy menu asks for the same preview four times - vanilla's HandlerAttached
	//! (SCR_LoadoutRequestUIComponent.c:158), the next-frame refresh ShowAvailableLoadouts queues
	//! (:316), RefreshLoadoutPreview (:618) and our own keyboard top-up - and vanilla dedups none of
	//! them because its preview is a cached body whose clothes are swapped. Ours stands a new soldier
	//! up and dresses him from nothing, so those four requests were four full dresses.
	protected string m_sRK29PreviewSig;

	//------------------------------------------------------------------------------------------------
	override IEntity SetPreviewedLoadout(notnull SCR_BasePlayerLoadout loadout, PreviewRenderAttributes attributes = null)
	{
		IEntity ent = super.SetPreviewedLoadout(loadout, attributes);
		// vanilla declined this call (m_bReloadLoadout) - leave standing whatever is on screen
		if (!ent)
			return ent;

		RK29_KitStruct kit;
		array<ref RK29_AttachmentOrder> orders;
		map<int, ref array<ref RK29_LoadedPick>> loadedMags;
		if (!RK29_StashedLoadoutUIInfo.ResolvePreviewLoadout(loadout, kit, orders, loadedMags)
			|| !kit)
		{
			// not ours: vanilla's body is already presented, so drop the one we were holding
			RK29_ClearPreviewBody();
			return ent;
		}

		if (!m_PreviewManager || !m_wPreview)
			return ent;

		// Already wearing this. super's else-branch has just put an undressed prefab body in the
		// widget (SCR_LoadoutPreviewComponent.c:179), so ours still has to be handed back - it is the
		// spawn and the dress that are skipped, never the handover.
		string sig = RK29_PreviewSignature(loadout, kit);
		if (sig == m_sRK29PreviewSig && m_RK29PreviewBody && !m_RK29PreviewBody.IsDeleted())
		{
			m_PreviewManager.SetPreviewItem(m_wPreview, m_RK29PreviewBody, attributes, true);
			return m_RK29PreviewBody;
		}

		IEntity body = RK29_SpawnPreviewBody(loadout.GetLoadoutResource());
		if (!body)
			return ent;

		RK29_MannequinDress.ApplyLoaded(body, kit, loadedMags, orders,
			"deploy preview '" + RK29_StashedLoadoutUIInfo.ResolveName(loadout) + "'");

		// present the new body before dropping the old one, so the render manager is never left
		// sampling an entity that has just been deleted
		m_PreviewManager.SetPreviewItem(m_wPreview, body, attributes, true);

		RK29_ClearPreviewBody();
		m_RK29PreviewBody = body;
		m_sRK29PreviewSig = sig;
		return body;
	}

	//------------------------------------------------------------------------------------------------
	//! Everything ResolvePreviewLoadout reads to build a dress: the body prefab, the kit it settled
	//! on, and the stash the picks come from. Deliberately coarser than the resolver itself, which
	//! consults the stash only while it names this kit - over-invalidating costs one dress, which is
	//! what every call cost before this guard, where under-invalidating would leave the wrong soldier
	//! standing. The orders and seated rounds need no place here: they are a pure function of these.
	protected string RK29_PreviewSignature(notnull SCR_BasePlayerLoadout loadout,
		notnull RK29_KitStruct kit)
	{
		string prefab = loadout.GetLoadoutResource();
		return prefab + "|" + kit.m_sKitName + "|" + RK29_LocalStash.Kit() + "|"
			+ RK29_LocalStash.Picks();
	}

	//------------------------------------------------------------------------------------------------
	override void HandlerAttached(Widget w)
	{
		super.HandlerAttached(w);
		RK29_ClearPreviewBody();
	}

	//------------------------------------------------------------------------------------------------
	//! The menu closing must not leave a dressed body standing in the world. No super call: the
	//! parent does not define this event and vanilla's own overrides of it do not chain either.
	override void HandlerDeattached(Widget w)
	{
		RK29_ClearPreviewBody();
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity RK29_SpawnPreviewBody(ResourceName prefab)
	{
		if (prefab == ResourceName.Empty)
			return null;

		Resource res = Resource.Load(prefab);
		if (!res.IsValid())
		{
			Print(string.Format("[RK29] deploy preview: body prefab did not load - %1", prefab),
				LogLevel.WARNING);
			return null;
		}

		return GetGame().SpawnEntityPrefabLocal(res, GetGame().GetWorld());
	}

	//------------------------------------------------------------------------------------------------
	//! Takes our body back out of the world, whole - the gun and everything seated on it hangs off
	//! its slots. Safe twice, safe on no body, and safe on one something else already took: the
	//! pointer outlives the entity, so IsDeleted is the only honest test (vanilla's own idiom in
	//! RK29_KitApply.DrainDoomed).
	protected void RK29_ClearPreviewBody()
	{
		// before the early return: the signature names a body, so it cannot outlive one
		m_sRK29PreviewSig = "";

		if (!m_RK29PreviewBody)
			return;

		if (!m_RK29PreviewBody.IsDeleted())
			SCR_EntityHelper.DeleteEntityAndChildren(m_RK29PreviewBody);

		m_RK29PreviewBody = null;
	}
}
