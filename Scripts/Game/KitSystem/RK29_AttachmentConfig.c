//------------------------------------------------------------------------------------------------
//! Attachment catalog - Configs/KitSystem/Catalogs/*.conf. Attachments are defined once and
//! referenced by id; slot compatibility is read from the prefab, never authored.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), BaseContainerCustomTitleField("m_sId")]
class RK29_AttachmentDef
{
	[Attribute(desc: "Id attachment groups refer to", category: "29th")]
	string m_sId;

	[Attribute(desc: "The attachment prefab", params: "et", category: "29th")]
	ResourceName m_sPrefab;

	[Attribute(desc: "Picker label override. Empty = the prefab's in-game display name", category: "29th")]
	string m_sDisplayName;

	[Attribute("0", desc: "This optic magnifies - drives the picker's magnified badge and the HUD magnified tally. Whether an OFFER of it counts is the attachment group's m_bMagnifiedExempt, not a property of the glass", category: "29th")]
	bool m_bMagnified;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(configRoot: true)]
class RK29_AttachmentCatalog
{
	[Attribute(desc: "Attachment definitions", category: "29th")]
	ref array<ref RK29_AttachmentDef> m_aAttachments;
}
