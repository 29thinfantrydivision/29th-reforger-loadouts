//------------------------------------------------------------------------------------------------
//! In-game display names for item/weapon prefabs, read off the prefab container and cached.
//! Localization keys (#AR-...) are translated here so callers can concatenate freely.
//------------------------------------------------------------------------------------------------
class RK29_ItemNames
{
	protected static ref map<ResourceName, string> s_mCache = new map<ResourceName, string>();

	//--------------------------------------------------------------------------------------------
	static string Get(ResourceName prefab)
	{
		if (prefab == ResourceName.Empty)
			return "";

		string cached;
		if (s_mCache.Find(prefab, cached))
			return cached;

		string name = ReadName(prefab);
		if (name == "")
			name = Fallback(prefab);

		s_mCache.Set(prefab, name);
		return name;
	}

	//--------------------------------------------------------------------------------------------
	//! Thin variant prefabs (e.g. Rifle_SVD_PSO) may not declare the item component or its
	//! name at their own level - walk the prefab ancestry until a name shows up.
	protected static string ReadName(ResourceName prefab)
	{
		Resource res = Resource.Load(prefab);
		if (!res.IsValid())
			return "";
		IEntitySource src = res.GetResource().ToEntitySource();

		while (src)
		{
			string name = ReadNameFrom(src);
			if (name != "")
				return name;
			src = src.GetAncestor();
		}
		return "";
	}

	//--------------------------------------------------------------------------------------------
	//! Items carry the name on InventoryItemComponent, weapons on their attachments
	//! storage - any component with item Attributes counts.
	protected static string ReadNameFrom(IEntitySource src)
	{
		for (int i = 0, n = src.GetComponentCount(); i < n; i++)
		{
			IEntityComponentSource comp = src.GetComponent(i);
			if (!comp)
				continue;

			BaseContainer attributes = comp.GetObject("Attributes");
			if (!attributes)
				continue;
			BaseContainer uiInfo = attributes.GetObject("ItemDisplayName");
			if (!uiInfo)
				uiInfo = attributes.GetObject("UIInfo");
			if (!uiInfo)
				continue;

			string name;
			uiInfo.Get("Name", name);
			if (name == "")
				continue;
			if (name.StartsWith("#"))
				name = WidgetManager.Translate(name);
			return name;
		}
		return "";
	}

	//--------------------------------------------------------------------------------------------
	protected static string Fallback(ResourceName prefab)
	{
		string raw = "" + prefab;
		int lastSlash = raw.LastIndexOf("/");
		if (lastSlash >= 0)
			raw = raw.Substring(lastSlash + 1, raw.Length() - lastSlash - 1);
		int dot = raw.LastIndexOf(".");
		if (dot > 0)
			raw = raw.Substring(0, dot);
		raw.Replace("_", " ");
		return raw;
	}
}
