//------------------------------------------------------------------------------------------------
//! Deploy-menu preview for "Current Kit" rows: vanilla spawns the side's base body, this dresses
//! it from the kit the row resolves to (RK29_MannequinDress.Apply). Other rows never run apply,
//! so their body as authored is what the player wears. The preview entity is cached per prefab
//! and shared by every row, so each pass accounts for every slot: one the kit does not name is
//! emptied.
//------------------------------------------------------------------------------------------------
modded class SCR_LoadoutPreviewComponent
{
	//------------------------------------------------------------------------------------------------
	override IEntity SetPreviewedLoadout(notnull SCR_BasePlayerLoadout loadout, PreviewRenderAttributes attributes = null)
	{
		IEntity ent = super.SetPreviewedLoadout(loadout, attributes);
		if (!ent)
			return ent;

		// Apply, not ApplyLoaded: this is vanilla's preview entity and has no cargo storage, so the full
		// apply drops all 38 items and seats nothing - the naked mannequin. A non-Current-Kit row
		// resolves nothing and keeps vanilla's own body.
		map<string, ResourceName> dress = new map<string, ResourceName>();
		map<int, ResourceName> weapons = new map<int, ResourceName>();
		ResourceName optic;
		if (!RK29_StashedLoadoutUIInfo.ResolvePreviewLoadout(loadout, dress, weapons, optic))
			return ent;

		RK29_MannequinDress.Apply(ent, dress, weapons, optic,
			"preview '" + RK29_StashedLoadoutUIInfo.ResolveName(loadout) + "'");

		// super() already presented the undressed body; present it again now that it is dressed, or what
		// the player sees depends on when the widget last sampled the entity.
		if (m_PreviewManager && m_wPreview)
			m_PreviewManager.SetPreviewItem(m_wPreview, ent, attributes, true);

		return ent;
	}
}
