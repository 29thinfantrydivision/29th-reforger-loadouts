# tools/

Authoring scripts. **Nothing in here is game content** — it is kept outside
`Prefabs/` so Workbench does not try to index it and so it does not travel with the
packed addon.

| Script | What it does |
| --- | --- |
| `audit_kits.py` | Static audit of the kit configs: resolves every `Common -> Roles -> <FACTION>` inheritance chain plus the blocks each pulls in, then checks **reachability** (every roster kit is offered by some squad, every squad name resolves to a kit), the **2-tracer mag doctrine**, and **US/USSR parity**. Exits with the finding count, so it works as a pre-release gate. Findings fail; notes are asymmetries that look deliberate. Note it does *not* flag `{}` vs `+{}` on an array - both accumulate, because the engine merges the whole ancestry before script sees the container. |
| `strip_variant_data.py` | Removes inherited `SCR_EditableEntityVariantData` blocks from 29th character prefabs. A prefab duplicated from a vanilla character carries the vanilla `m_VariantData` with it, which makes `SCR_BasePlayerLoadout.GetLoadoutResource()` resolve to the *vanilla* variant prefab instead of the 29th one — the deploy menu then spawns stock gear. Dry-runs by default; pass `--apply` to write. Leaves a `.bak` beside each file it changes, so clean those up before committing. |

Run from the repo root:

```bash
python tools/audit_kits.py
```

```bash
python tools/strip_variant_data.py Prefabs/Characters/Factions
```
