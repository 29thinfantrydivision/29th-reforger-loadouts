# 29th Kit System — Config Backend Design

Successor to the prefab-capture pipeline: kits become hand-editable config compositions
("blocks") instead of being read off character prefabs at load. Companion to
`29th-kit-system-design.md`; that doc covers the runtime (picker, HUD, apply, counting),
this one covers where kit *content* comes from.

## 1. Goals

- Kits defined as compositions of named building blocks:
  `US Rifleman = base_medical + base_utility + us_standard_dress + us_m16_rifleman`.
- Standardize once: edit `base_medical.conf`, every kit referencing it follows.
- Numbers are numbers: "give the Grenadier 12 shells" is one integer edit, never a
  sub-slot path edit.
- Weapon choice = variant choice (formalizing the `m_sSourceKitName` routing): picking
  M60 selects the M60 variant with its own rig/ammo blocks.
- Zero rework downstream: config builds the same `RK29_KitStruct` the capture builds
  today. Apply, skips, optics, counting, HUD, Current Kit, spectator stamping are all
  consumers of the struct and do not change.
- No flag day: per-kit hybrid — a kit with config blocks uses them, a kit without falls
  back to prefab capture.

## 2. Non-goals (unchanged from main doc §15)

- Personal save/load of kits, hard kit limits, mid-round HUD, ammo-count picker UI.
- Replacing GM29_Kits.conf: vanilla loadout entries remain the identity spine
  (deploy menu, squad restriction via GetPlayerLoadoutsByGroup, counting index,
  Current Kit pseudo-entries, spectator icons).

## 3. Authoring rules (the short version an editor needs)

1. **Blocks add up.** Items are additive; clothing and weapon slots are later-wins.
2. **Variants that add stay linked; variants that remove go their own way.**
   Block inheritance is native Enfusion conf inheritance: a child conf that *appends*
   (`+{}`) keeps tracking its base; a child that *replaces* an array spells out the full
   list and stops tracking. Workbench shows the resolved result either way.
3. **If a variant would remove more than ~2 things, make a smaller base block instead.**
4. **Author weapons as base prefabs** (`Rifle_SVD`, not `Rifle_SVD_PSO`). Glass comes
   from the optic system: class default optic + optic categories. The pre-composed
   variant prefabs are exactly "base + optic in a slot" — our optic pass does the same
   composition at runtime, and one source of truth kills the 4x20-on-M21 class of bug.
   `m_sWeaponVariantPrefab` stays as the escape hatch for truly integral optics only.
5. **Never author kit gear inside garment prefabs.** Config items are character-level by
   construction; the clothing/weapon keep-optimizations and the item swap logic depend
   on this invariant (garment-internal gear does not survive a same-garment keep).
6. **No placement paths.** Items are `{prefab, count}`; the engine routes them
   (priority-aware deposit), largest-first ordering prevents packing failures.
   `m_sTargetHint` exists per entry as the rare manual override ("radio in buttpack").

## 4. Schema

New folder: `Configs/KitSystem/Blocks/` (suggested layout `Blocks/Base/`, `Blocks/US/`,
`Blocks/USSR/`). All attributes `category: "29th"`, desc on every field, `params: "et"` /
`"conf"` so Workbench gives pickers.

```c
[BaseContainerProps()]
class RK29_BlockItemEntry
{
    ResourceName m_sPrefab;      // et
    int          m_iCount;       // default 1
    string       m_sTargetHint;  // optional container filename; empty = engine routes
}

[BaseContainerProps()]
class RK29_BlockWeaponEntry
{
    int          m_iSlotIndex;   // 0 primary, 1 secondary, 2 sidearm; 100 = grenade slot
    ResourceName m_sPrefab;      // BASE weapon prefab (rule 4)
}

//! One building block - Configs/KitSystem/Blocks/**.conf. Inherit conf-from-conf for
//! variants (crew_medical : base_medical).
[BaseContainerProps(configRoot: true)]
class RK29_KitBlock
{
    ref array<ResourceName>              m_aClothing;  // garment prefabs, slot-typed at apply
    ref array<ref RK29_BlockWeaponEntry> m_aWeapons;
    ref array<ref RK29_BlockItemEntry>   m_aItems;
}
```

Class setup grows block references; `RK29_WeaponOption` grows variant blocks:

```c
class RK29_ClassSetup     // existing, extended
{
    ...existing fields (kitName, displayName, opticCategories, defaultOptic, legacyHidden)...

    [Attribute(desc: "Content blocks, applied in order. Empty = capture the kit prefab (hybrid fallback)", params: "conf")]
    ref array<ResourceName> m_aBlocks;
}

class RK29_WeaponOption   // existing, extended
{
    ...existing fields (weapon, mag, magCount, displayName, sourceKitName)...

    [Attribute(desc: "Variant blocks appended after the class blocks when this weapon is chosen", params: "conf")]
    ref array<ResourceName> m_aBlocks;
}
```

Semantics:
- Class `m_aBlocks` build the base kit. A chosen weapon option with `m_aBlocks` appends
  them (later-wins on clothing/weapon slots -> the variant's rig and gun replace the
  base ones; its ammo adds).
- Weapon option precedence during migration: `m_aBlocks` (config era) >
  `m_sSourceKitName` (capture era) > plain weapon+mag swap. Once all variants have
  blocks, `m_sSourceKitName` and `m_iMagazineCount` retire — ammo counts live in the
  variant's ammo block, which is the "edit one number" requirement.

Example (target state):

```
us_rifleman.conf class blocks:  [base_medical, base_utility, us_standard_dress, us_m16_rifleman]
us_mg class blocks:             [base_medical, base_utility, us_standard_dress]
  M249 option blocks:           [us_ar_rig, us_m249_ammo]     // rig block carries ALICE_AR vest
  M60  option blocks:           [us_mg_rig, us_m60_ammo]      // rig block carries ALICE_MG vest + M5 backpack + M9
```

## 5. Struct build (the seam)

`RK29_KitManager.Boot` per kit:

1. `cls.m_aBlocks` empty -> `RK29_KitCapture.Capture(prefab)` exactly as today.
2. Otherwise `RK29_KitCompose.Build(cls)`:
   - Load each block conf via `BaseContainerTools.CreateInstanceFromContainer` — the
     engine resolves conf inheritance *before* we see values, so none of the prefab
     ancestry-walk pitfalls apply here (that enumeration quirk is .et-specific).
   - Fold in order into a fresh `RK29_KitStruct`:
     - clothing: garment prefab -> slot resolved by type at apply (as today); within the
       struct keyed by garment prefab type-slot; later block same-slot garment replaces.
     - weapons: `m_mWeapons[slotIndex] = prefab` (later-wins). Slot 100 = grenade.
     - items: expand `{prefab, count}` to count entries in one `RK29_KitItemBatch`
       (hint from the entry; usually empty).
   - UIInfo: still instanced from the kit's loadout prefab at boot (identity shell keeps
     `SCR_EditableCharacterComponent`), so spectator icons are untouched.
3. Log the source per kit: `[RK29] kit 'X' from CONFIG (blocks: a+b+c)` vs `from PREFAB`.

Variant resolution moves into `ResolveSelection`: chosen weapon option with blocks ->
rebuild struct as class blocks + variant blocks; else existing paths (source kit /
mag swap) unchanged.

## 6. Spawn flow

Decision: **keep clothing authored on the loadout prefabs; move weapons + items to
config.** Rationale:
- Deploy-menu 3D preview renders the prefab; keeping dress on the prefab keeps previews
  honest (the Current Kit placeholder is already the accepted exception).
- Clothing is the least-repeated, least-edited part of a kit; weapons/ammo/med counts
  are what the 29th actually tunes.
- The prefab's `InitialInventoryItems` get emptied as kits convert. That removes the
  vanilla async item spawn — the 500ms defer on spawn mutation can drop once a kit's
  prefab carries no items (keep the defer while any hybrid kit remains).

End-state spawn: vanilla spawns the dressed-but-empty prefab; `OnPlayerSpawned_S`
applies the config struct (weapons + items; clothing already correct -> the slot-by-slot
keep logic makes it a no-op wearing-wise). Current Kit spawns keep their full re-dress.

## 7. Placement (already built, config inherits it)

- Largest-first ordering by `ItemVolume` (prefab-ancestry aware).
- Per item: exact hint container -> hint path -> engine auto-deposit
  (`TrySpawnPrefabToStorage(null, PURPOSE_DEPOSIT)`, priority-aware).
- Failures: server log names each dropped prefab; owner-RPC hint tells the player
  ("N item(s) did not fit..."). Contract: heuristic + FFD + validation + loud failure,
  no mathematical fit guarantee.

## 8. Boot validation (server, one pass, ERROR/WARNING log lines naming file+field)

Existing behavior plus, in rough priority order:

1. Every `m_sKitName` matches a GM29_Kits.conf loadout name (join-key check).
2. Every block ResourceName loads and is an `RK29_KitBlock`.
3. Optic category names referenced by classes resolve; `m_sDefaultOptic` lies inside an
   allowed category.
4. **Capacity**: sum of item volumes/weights vs the kit's container capacity
   (containers = clothing blocks' garments + their authored pouches, read via the same
   ancestry-aware readers). Warn with numbers: `Grenadier: 4180/4000 volume`.
5. **Optic fitment**: each offered optic's attachment type vs the class weapons' slot
   types (readable from prefab containers). Catches the next 4x20-on-M21 at load.
6. Weapon entries resolve to prefabs with `WeaponComponent`; item entries to prefabs
   with `InventoryItemComponent`; counts >= 1.
7. Kit resolved-totals digest per kit for eyeballing after block surgery:
   `Rifleman: 34 items (base_medical 6, base_utility 8, us_m16_rifleman 20)`.

Also runtime (deferred from earlier, belongs with this work): **restore-on-failure in
`ApplyOptic`** — remember the old optic prefab; if the new one fails to seat, re-insert
the old instead of leaving irons. Turns config mistakes cosmetic.

## 9. Display names & ordering (already landed, config backend must preserve)

- Class display standard: Rifleman / Machine Gunner / Combat Engineer (LAT) / Grenadier
  / Sniper (incl. Sharpshooter) / Squad Leader / Crewman.
- Side-config class order drives both HUD and picker order; label aggregation sums kits
  sharing a display name (AR + legacy MG under "Machine Gunner").
- Weapon/optic labels: config `m_sDisplayName` override first, else in-game name
  (`RK29_ItemNames`: ItemDisplayName, ancestry walk, script-side translate).

## 10. Migration

1. **Dump tool**: chat command `/kitdump` (server console): serializes every captured
   `RK29_KitStruct` to `Configs/KitSystem/Blocks/Generated/<kit>.conf` (one monolithic
   block per kit) + a suggested class `m_aBlocks` line. Guarantees the config era starts
   from byte-accurate current content, no transcription.
2. **Factor by hand**: humans pull the shared sets (medical, utility, dress) out of the
   generated blocks into `Blocks/Base/`; the dump can list identical item-subsets across
   kits to hint candidates. Naming is a human job.
3. **Hybrid rollout**: convert one kit (suggest US Rifleman), verify against its
   captured twin in-game (item-for-item, same counts, same drop-free apply), then roll
   the rest kit by kit. `m_aBlocks` empty keeps any kit on capture indefinitely.
4. **Retire**: when all kits are config, strip `InitialInventoryItems` from the 29th
   prefabs (clothing stays per §6), drop `m_sSourceKitName` + `m_iMagazineCount`, and
   the capture path remains only as the fallback for unconverted/new kits.

## 11. Compatibility ledger (what must not change and why it won't)

| Consumer | Depends on | Impact |
|---|---|---|
| Apply / skips / disguise fix | `RK29_KitStruct` shape | none — same struct, new producer |
| Optic system | struct primary + optic pass | none; base-prefab rule makes it simpler |
| Counting / HUD / labels | loadout index + EffectiveKitName + class order | none |
| Current Kit | stash + pseudo-loadout + placeholder body | none; full re-dress path already config-agnostic |
| Squad restriction | GetPlayerLoadoutsByGroup on GM29 loadouts | none — loadout entries persist |
| Spectator icons | UIInfo instanced at boot from prefab | none — prefab keeps SCR_EditableCharacterComponent |
| Keybind/picker UI | unchanged surfaces | none |

## 12. Open questions (decide before implementation)

1. Block granularity taste: per-faction dress as one block (`us_standard_dress`) vs
   split (`us_uniform` + `us_webbing`)? Recommendation: one dress block per rig variant,
   since rigs are what differ (AR vs MG proved it).
2. Should grenades live in weapon slots (slot 100, one primed) or as items? Capture
   keeps slot-100; recommendation: items in blocks (`{RGD5, 2}`), slot 100 only if the
   primed-grenade slot matters in practice.
3. Does the dump tool need auto-factoring, or is per-kit dump + manual factoring enough?
   Recommendation: manual — 19 kits is a one-afternoon factoring job and names need a
   human anyway.
