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
- ~~Replacing GM29_Kits.conf: vanilla loadout entries remain the identity spine~~
  **(revised 2026-08-24 - see §17. A loadout entry is now how a kit is SPAWNED, not what
  makes it exist.)** (deploy menu, squad restriction via GetPlayerLoadoutsByGroup, counting index,
  Current Kit pseudo-entries, spectator icons).

## 3. Authoring rules (the short version an editor needs)

1. **Blocks add up.** Items are additive; clothing and weapon slots are later-wins.
2. **Variants that add stay linked; variants that remove go their own way.**
   Block inheritance is native Enfusion conf inheritance: a child conf that *appends*
   (`+{}`) keeps tracking its base; a child that *replaces* an array spells out the full
   list and stops tracking. Workbench shows the resolved result either way.
3. **Shipped configs are ADDITIVE-ONLY — model deviation with smaller blocks plus
   inline additions, never with overrides.** The set-count override mechanism exists
   (use-site, SET semantics, literal identity match) but is a last resort, not
   practice: a kit that carries less references a leaner block rather than
   subtracting from a fuller one; a kit that carries more adds inline
   ("smoke x1" on top of the smokes block). Reading any kit file top to bottom is
   pure addition — nothing you read gets taken away later.
   Note: "minus hat plus helmet" needs no override either — clothing is later-wins
   per slot, so a later helmet entry replaces the hat.
4. **If a reference would remove more than ~2 things, make a smaller base block instead.**
5. **Author weapons as base prefabs — EXCEPT kits whose stock version ships glass.**
   Stock deploy-menu spawns are never mutated (they are the backup path when this
   addon misbehaves), so a kit that must spawn scoped authors the pre-composed variant
   (`Rifle_SVD_PSO`, `Rifle_M21_ARTII`) and sets `m_sDefaultOptic` to match; the optic
   pass swaps glass fine on picker-driven applies. Base prefabs remain the rule
   everywhere a scoped stock spawn is not required, and `m_sWeaponVariantPrefab` stays
   the escape hatch for truly integral optics.
6. **Never author kit gear inside garment prefabs.** Config items are character-level by
   construction; the clothing/weapon keep-optimizations and the item swap logic depend
   on this invariant (garment-internal gear does not survive a same-garment keep).
7. **No placement paths.** Items are `{prefab, count}`; the engine routes them
   (priority-aware deposit), largest-first ordering prevents packing failures.
   `m_sTargetHint` exists per entry as the rare manual override ("radio in buttpack").
8. **Faction-specific gear goes through aliases.** Consumables differ by faction
   (FieldDressing_US_01 vs _USSR_01); a block that should serve both sides uses alias
   entries ("bandage") resolved through the alias catalog, keeping `base_medical` one
   file. Direct prefab entries bind a block to one faction — fine for rigs and
   weapons, wrong for shared bases. `MAG_PRIMARY` likewise keeps ammo blocks
   weapon-neutral.

## 4. Schema

Layout (settled 2026-08-21). Three rules: the **folder supplies the qualifier and the
filename never repeats it**; **`Common` always means faction-neutral**, in both trees;
**`RK29_` marks an entry point** - a file the setup loads by name - while content
addressed only by GUID reference stays bare.

```
Configs/KitSystem/
  RK29_KitSetup.conf                      entry point
  Catalogs/   RK29_Aliases · RK29_Magazines · RK29_Optics · RK29_Squads
  Rosters/    RK29_Roster_US · RK29_Roster_USSR      per-faction class lists
  Kits/       Common/infantry · Roles/<role> · US/<role> · USSR/<role>
  Blocks/     Common/<bundle> · US/dress · USSR/dress
```

The `Kits/` path IS the inheritance chain, read top to bottom: `Common` -> `Roles` ->
`<FACTION>`. All attributes `category: "29th"`, desc on every field.

Workbench affordances (verified against the vanilla corpus):
- Conf-to-conf inheritance with `+{}` array append is shipped vanilla practice
  (support-station action configs) — the block-variant mechanism is proven, not novel.
- **Class-constrained pickers**: `params: "conf class=RK29_KitBlock"` on block refs
  (and the setup's side/optic/alias refs) — wrong-class picks impossible in the editor.
- **Custom title decorations on every array element class** so entries read as content,
  not class names: `SCR_BaseContainerCustomTitleResourceName("m_sPrefab", true)` for
  prefab entries, `BaseContainerCustomTitleField("m_sAlias")` for aliases, an RK29
  scripted title (they are plain script classes) rendering "3x bandage" for item
  entries, `m_sDisplayName` titles for classes/weapon options.
- `visible: false, insertable: false` on any shared base classes not meant to be
  picked directly.

```c
enum RK29_EItemSource { PREFAB, ALIAS, MAG_PRIMARY, MAG_LAUNCHER, MAG_SIDEARM }

[BaseContainerProps()]
class RK29_BlockItemEntry
{
    RK29_EItemSource m_eSource;  // explicit source - no magic-string namespaces
    ResourceName m_sPrefab;      // PREFAB
    string       m_sAlias;       // ALIAS: catalog name, e.g. "bandage"
    string       m_sVariant;     // MAG_*: variant name (tracer/ap/...); empty = weapon default
    int          m_iCount;       // default 1
    string       m_sTargetHint;  // optional container filename; empty = engine routes
}

//! RK29_ItemAliases.conf - faction-flavored consumables; per-faction entry ARRAY so
//! new factions are config additions, not schema surgery.
class RK29_ItemAlias { string m_sAlias; ref array<ref RK29_ItemAliasEntry> m_aPerFaction; }

//! RK29_MagazineSets.conf - mag variants keyed by MagazineWell class; MAG_* entries
//! with a variant resolve through the slot weapon's wells. Plain MAG_PRIMARY = weapon
//! option's mag, else the weapon's authored MagazineTemplate; other slots = template.
// Overrides on RK29_BlockRef match entry identity LITERALLY (same source +
// alias/variant/prefab) and apply before resolution - "set 0" is always safe.

[BaseContainerProps()]
class RK29_BlockWeaponEntry
{
    int          m_iSlotIndex;   // 0 primary, 1 secondary, 2 sidearm; 100 = grenade slot
    ResourceName m_sPrefab;      // BASE weapon prefab (rule 4)
}

//! One building block - Configs/KitSystem/Blocks/**.conf. Inherit conf-from-conf for
//! variants (crew_medical : base_medical). Blocks are purely additive (rule 3).
[BaseContainerProps(configRoot: true)]
class RK29_KitBlock
{
    ref array<ResourceName>              m_aClothing;  // garment prefabs, slot-typed at apply
    ref array<ref RK29_BlockWeaponEntry> m_aWeapons;
    ref array<ref RK29_BlockItemEntry>   m_aItems;
}

//! How kits reference blocks: block + optional use-site overrides, applied against this
//! block's own contribution before merging. Item overrides SET the final count of that
//! prefab from this block (0 = remove entirely).
[BaseContainerProps()]
class RK29_BlockRef
{
    ResourceName                       m_sBlock;          // conf
    ref array<ref RK29_BlockItemEntry> m_aItemOverrides;  // identity + absolute count
}
```

Each kit's content story lives in ONE file - `Configs/KitSystem/Kits/<faction>/<kit>.conf`:

```c
//! Shared bases by reference (with use-site overrides), the kit's own weapons and
//! items INLINE. Reading this file = reading the kit. Compositions may inherit each
//! other conf-from-conf (e.g. a base_infantry composition); blocks stay leaf-only.
[BaseContainerProps(configRoot: true)]
class RK29_KitComposition
{
    ref array<ref RK29_BlockRef>         m_aBlocks;   // base_medical, base_utility, ...
    ref array<ref RK29_BlockWeaponEntry> m_aWeapons;  // this kit's guns, literal
    ref array<ref RK29_BlockItemEntry>   m_aItems;    // this kit's own items, literal
    ref array<ResourceName>              m_aClothing; // rare dress overrides
}

class RK29_ClassSetup     // existing, extended
{
    ...existing fields (kitName, displayName, opticCategories, defaultOptic, legacyHidden)...

    [Attribute(desc: "This kit's composition. Empty = capture the kit prefab (hybrid fallback)", params: "conf class=RK29_KitComposition")]
    ResourceName m_sComposition;
}

`RK29_WeaponOption` is unchanged: **weapon choice = kit choice, permanently.**
`m_sSourceKitName` routes M60 to the Machine Gunner *kit*, whose own class entry has
its own blocks and whose own prefab carries the MG dress. Variants never carry blocks;
there is exactly one way a weapon choice changes the body, and deploy previews stay
honest for every kit. `m_iMagazineCount` retires once ammo lives in blocks
(`MAG_PRIMARY` + count).

Semantics:
- Class `m_aBlocks` build that kit's contents (items + weapon slots + dress). Dress is
  declared in per-faction dress blocks (§6), slot-keyed later-wins over the prefab
  capture; the kit prefab remains the backup path's dress truth.
- Aliases resolve at compose time: faction aliases through the kit's faction,
  MAG_* entries through the slot weapon (option mag / MagazineTemplate / well variants).

Shipped layout (landed 2026-08-20) — a ROLE layer sits between the bases and the
faction kits, because US/USSR pairs of the same class share almost everything once
faction flavor goes through aliases and MAG tokens:

```
Blocks/<FAC>/dress.conf           // ONE per faction: the lines every kit repeats
                                  //   (helmet, uniform, armour, boots, binos, watch).
                                  //   Vest and Back are class identity - the kit states
                                  //   those inline, so they are visible where they are
                                  //   tuned. Crewman shares nothing and is fully inline.
Kits/Common/infantry.conf         // standalone: [grenades, medical, utility].
                                  //   No weapons live in Common.
Kits/Roles/<role>.conf            // : infantry when both factions run the standard
                                  //   bundle, else parentless (deviants declare blocks
                                  //   per faction). Holds the cross-faction common
                                  //   items (min of both sides) in identity space:
                                  //   MAG tokens, @aliases, literal prefabs - and the
                                  //   slot-2 handgun alias when both factions carry
                                  //   their standard sidearm, so a class can drop its
                                  //   handgun later by editing that one role file
                                  //   (asymmetric today: LAT - USSR-only, in its
                                  //   faction file).
Kits/<faction>/<faction>_<kit>.conf  // : base_<role>. Carries the slot-0 primary
                                  //   (+ slot-1/2 deviations) and the faction
                                  //   remainder items. Rifleman = one weapon line.
```

Roles: rifleman, automatic_rifleman, machine_gunner, grenadier, light_antitank,
sniper (US Sniper + USSR Sharpshooter pair), crewman, leadership. Bayonets were
normalized in the role bases (either side had one -> both get one; previously only
USSR kits + US Crewman carried them — authoring oversight flagged 2026-08-20; veto =
delete the bayonet entry in the role base), so /kitcompare will report the US kits'
added bayonet as an intentional diff.

## 5. Struct build (the seam)

`RK29_KitManager.Boot` per kit:

1. `cls.m_aBlocks` empty -> `RK29_KitCapture.Capture(prefab)` exactly as today.
2. Otherwise `RK29_KitCompose.Build(cls)`:
   - Load each block conf via `BaseContainerTools.CreateInstanceFromContainer` — the
     engine resolves conf inheritance *before* we see values. (Prefab entity sources
     are ALSO fully merged — component enumeration includes inherited components and
     value reads resolve; verified empirically, no ancestry walking needed anywhere.)
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

Decision (REVISED 2026-08-20): **dress is config-declared like everything else.** Each
faction kit references its faction's single `Blocks/<FAC>/dress.conf`
(RK29_BlockClothingEntry: slot name + garment prefab) and inlines only its deltas.
The per-kit dress files this replaced duplicated the same six or seven lines fifteen
times; measured across both factions, only vest, pack and the odd hat ever varied. Merge is slot-keyed later-wins over the prefab-captured dress; an entry with
an empty prefab clears the slot; slots no config mentions fall back to the prefab, so
hybrid/capture kits (TP, Parade) are unaffected. The original "prefab must stay
dress-truth for the preview" rationale died when the Current Kit preview proved
resource+optic preview swapping works; the prefab remains the backup path's truth and
must keep matching the dress blocks until the deploy preview is config-driven too.
- Weapon variants are whole kits (source-kit routing), so the MG rig lives on the MG
  kit's dress block.
- Dress blocks also carry `m_aEquipment` — the SCR_EquipmentStorageComponent slots
  (WristwatchSlot, BinocularSlot) that vanilla authors on the base character prefabs,
  a third channel beside dress slots and item-init. Capture reads it, compose merges
  it slot-keyed later-wins, apply delta-swaps it (DressEquipment), /kitcompare and
  /kitdump cover it. Before this, apply silently deleted the watch and binoculars
  (StripLooseItems caught them, nothing re-added them).
- Remaining prefab reliance after this: garment prefabs' structural content (pouches =
  capacity, like a weapon's mag well), the deploy-menu 3D preview, the stock backup
  spawns, and kit identity (UIInfo name/icon). Garment gadget slots are neutralized at
  apply (StripGarmentSlotGadgets).
- The prefab's `InitialInventoryItems` get emptied as kits convert. That removes the
  vanilla async item spawn — the 500ms defer on spawn mutation can drop once a kit's
  prefab carries no items (keep the defer while any hybrid kit remains).

End-state spawn: vanilla spawns the dressed-but-empty prefab; `OnPlayerSpawned_S`
applies the config struct (weapons + items; clothing already correct -> the slot-by-slot
keep logic makes it a no-op wearing-wise). Current Kit spawns keep their full re-dress.

## 7. Placement

Rewritten during the config rollout; the old "largest-first by `ItemVolume`" note was
wrong twice over. `ItemVolume` is a field most items never author (the M249 belt box
declares `ItemDimensions 15 15 15` and no volume at all), so the sort read 0 and put the
biggest items last, letting pistol mags claim the pouches. Size now comes from
`ItemMaxDimension`. Container reads are plain merged reads — no ancestry walking; see the
Enfusion container model note.

Current algorithm, per kit, cached by kit signature:

1. **Collect containers** — every cargo storage on the body, plus each named slot of an
   `SCR_EquipmentStorageComponent` expanded as its own candidate
   (`Vest_ALICE_suspenders_2.et#0/FlashlightSlot`).
2. **Solve** most-constrained-item-first, least-constraining-container-first, with the
   engine as the only fit oracle (`CanInsertResourceInStorage`).
3. **Rank** candidates by `penalty -> tier -> listed -> cohesion -> whole -> detail`:
   - *tier*: a matching special slot outranks cargo. If several exist, the visible one
     wins (vest over uniform) - free capacity that also looks right.
   - *listed*: the item's `m_aPreferredContainers`, from the entry or its alias.
   - *cohesion*: stacks of the same item stay together, bounded at
     `COHESION_MAX_STACK = 3` so cohesion cannot outvote a preference.
4. **Grenades** are primed into the throwable slot BEFORE cargo placement, and removed
   from the batch, so counts stay exact and the slot never sits empty while grenades
   fill uniform space.

Failure contract is unchanged: every dropped prefab is logged at WARNING, and an
owner-RPC names them for the player. No mathematical fit guarantee - use `/kitvalidate`
(§14) to prove a kit fits before shipping it.

### Verbose tracing

The per-item trace (every strip, every container weighed, every placement) is what made
these bugs findable, and it is ~100 lines per apply. It is gated behind
`m_bVerboseLogging` in `RK29_KitSetup.conf`, off by default, routed through
`RK29_Log.Trace`. Warnings and the one-per-apply begin/done lines always print.

## 8. Boot validation (server, one pass, ERROR/WARNING log lines naming file+field)

Existing behavior plus, in rough priority order:

1. Every `m_sKitName` matches a GM29_Kits.conf loadout name, **or the class resolves a body
   prefab of its own** (§17). A class with neither is an ERROR at boot.
2. Every block ResourceName loads and is an `RK29_KitBlock`.
3. Optic category names referenced by classes resolve; `m_sDefaultOptic` lies inside an
   allowed category.
3b. **Dead overrides**: a `RK29_BlockRef` item override naming an identity
   its block does not contain warns (renames in a base must not silently orphan
   use-site overrides).
3c. **Aliases**: every alias used by a block resolves in the catalog; the resolved
   prefab for the kit's faction is non-empty; MAG_* entries only appear in kits whose
   weapon options declare a magazine. Entries with both prefab and alias set = ERROR.
3d. **Faction sanity via vanilla entity catalogs**: warn when a composed item is absent
   from the kit faction's InventoryItems catalog
   (`faction.GetFactionEntityCatalogOfType`) — a ready-made cross-faction-gear
   detector. Catalog item types (HEAL etc.) also tag items during the factoring pass.
4. **Capacity**: sum of item volumes/weights vs the kit's container capacity
   (containers = clothing blocks' garments + their authored pouches, read via the same
   ancestry-aware readers). Warn with numbers: `Grenadier: 4180/4000 volume`.
5. ~~**Optic fitment**~~ - IMPLEMENTED, and at runtime rather than boot, which is
   strictly better: `RK29_KitCompose.WeaponRejectsAttachment` intersects the mount types
   declared by weapon and attachment. The picker filters its optic column against the
   selected weapon, and the server re-checks the request (dropping the optic, not the
   kit). Options declaring `m_sWeaponVariantPrefab` or `m_aRequiredAttachments` are
   exempt - they swap the weapon or bring their own adapter. Same helper backs the
   bayonet gate, so the two can never disagree.
   Fitment also honours obstruction, which the attachments themselves declare:
   `SCR_WeaponAttachmentObstructionAttributes.m_aObstructedAttachmentTypes` on Bayonet_M9
   names AttachmentUnderBarrelM203 / M203Carbine, and Bayonet_6Kh4 names
   AttachmentUnderBarrelGP25. The test reads what is SEATED on the weapon (a slot with an
   authored Prefab), never the slots it merely offers - an AK-74N advertises an empty GP-25
   slot and must keep its bayonet, while a grenadier's rifle has a launcher in that slot
   and loses it. No hand-maintained exception list.
6. Weapon entries resolve to prefabs with `WeaponComponent`; item entries to prefabs
   with `InventoryItemComponent`; counts >= 1.
7. Kit resolved-totals digest per kit for eyeballing after block surgery:
   `Rifleman: 34 items (base_medical 6, base_utility 8, us_m16_rifleman 20)`,
   with use-site overrides called out (`base_medical: bandages 20 -> 15`).

### Workbench viewing expectations

- Block conf inheritance is native: "show only modified" = the child's delta,
  full view = resolved values. No math needed.
- `RK29_BlockRef` overrides and block lists are plain data — Workbench shows them as
  authored but cannot render the composed kit (composition happens in our loader).
  The boot digest above is the resolved view; if it proves insufficient, a small
  Workbench plugin running the composer as a "preview resolved kit" context action is
  the upgrade path (reuses the runtime composer verbatim).

Also runtime (deferred from earlier, belongs with this work): **restore-on-failure in
`ApplyOptic`** — remember the old optic prefab; if the new one fails to seat, re-insert
the old instead of leaving irons. Turns config mistakes cosmetic.

## 9. Display names & ordering (already landed, config backend must preserve)

- Class display standard: Rifleman / Machine Gunner / Anti-Tank (LAT) / Grenadier
  / Sniper (incl. Sharpshooter) / Leadership / Crewman.
- Side-config class order drives both HUD and picker order; label aggregation sums kits
  sharing a display name (AR + legacy MG under "Machine Gunner").
- Weapon/optic labels: config `m_sDisplayName` override first, else in-game name
  (`RK29_ItemNames`: ItemDisplayName, ancestry walk, script-side translate).
- Optic restriction granularity: classes reference categories, then refine per class
  with `m_aOpticExclude` / `m_aOpticInclude` (specific prefabs; includes pull their
  badge/mount metadata from whichever library category defines them). The same
  include/exclude-at-use-site pattern is the template for restricting magazine
  variants per class if/when a magazine picker exists.

## 10. Migration

0. **Digest command**: `/kitdigest <kit>` — on-demand resolved-contents print (same
   output as the boot digest, without a restart). ~10 lines once the composer exists;
   this is the chosen "no-math check" (Workbench preview plugin deliberately deferred
   until the edit loop proves to need it).
1. **Dump tool**: chat command `/kitdump` (server console): serializes every captured
   `RK29_KitStruct` to `Configs/KitSystem/Blocks/Generated/<kit>.conf` (one monolithic
   block per kit) + a suggested class `m_aBlocks` line. Guarantees the config era starts
   from byte-accurate current content, no transcription.
2. **Factor by hand**: humans pull the shared sets (medical, utility, dress) out of the
   generated blocks into `Blocks/Common/`; the dump can list identical item-subsets across
   kits to hint candidates. Naming is a human job.
2b. **Compare tool**: `/kitcompare <kit>` — compose from config AND capture from
   prefab, print the struct-level diff (missing/extra items with counts, slot
   mismatches). Migration acceptance = EQUAL, not eyeballing inventories.
   With no argument it sweeps every configured kit, prints a one-line verdict per
   kit and writes `$profile:RK29_KitCompare.txt`; `/kitvalidate` is its companion
   for the capacity question (does everything the config asks for actually fit).
3. **Hybrid rollout**: convert one kit (suggest US Rifleman), `/kitcompare` until
   EQUAL, spot-check in-game, then roll the rest kit by kit. Source-kit routing is
   permanent; only `m_iMagazineCount` retires (once AR/MG kits are on blocks with
   `MAG_PRIMARY` ammo), keeping the transitional overlap to one mechanism.
   `m_aBlocks` empty keeps any kit on capture indefinitely.
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
2. RESOLVED: grenades are **items only** in config kits — no slot-100 entries. The
   SCR layer equips/cycles throwables from inventory (SCR_EquipNextGrenadeCB /
   FindNextWeaponOfType), so the authored slot only ever decided the spawn-primed
   grenade, which we deliberately dropped. The generator folds authored slot grenades
   into item counts, and /kitcompare normalizes captured kits the same way. Stock
   (prefab) kits still spawn primed — accepted divergence of the backup path.
   **Corollary, learned the hard way**: `Compose` must NOT copy the captured
   `GRENADE_SLOT` into a composed kit. It did, and since the config already counted that
   grenade as an item, every kit on both factions fielded one extra grenade or smoke -
   invisible until /kitcompare swept all 17 kits at once and every single one came back
   with the same `captured N vs composed N+1` line.
3. RESOLVED: the dump tool stays dumb (per-kit, byte-accurate). Factoring is an
   offline analysis pass over the dump output (commonness matrix -> proposed blocks +
   alias catalog + draft confs + per-kit block lists), reviewed by a human for names
   and groupings.

## 13. Known risks

- **Capacity validator effort**: container discovery = walking garment prefabs'
  attached pouches recursively, ancestry-aware. This is where the implementation time
  hides; budget for it, don't ship validation without it.
- **Per-reference override scope**: overrides cap one block's contribution; two blocks
  both contributing a prefab sum past the override. The digest shows it; it is a
  documented surprise, not a prevented one.
- **Preview drift is now limited to Current Kit** (placeholder body); every real kit,
  variants included, previews its own prefab honestly.

## 14. Kit-author tooling

Four chat commands, all server-side only (`Replication.IsServer()`): they run in Workbench
or on a listen host and refuse for remote clients. Vanilla applies no permission model of
its own - `GetCommandInvoker` mints an invoker for any name and dispatches on the typing
client - so the gate is ours. The picker is not a chat command: it is bound to F4, refuses
outside preround, and its outcome goes through the validated RPC.

| Command | Answers | Output |
|---|---|---|
| `/kitdigest [kit]` | what does the config compose to | log |
| `/kitdump` | what does the prefab actually contain | `$profile:RK29_KitDump/*.conf` |
| `/kitcompare [kit]` | does config still match prefab | log + `$profile:RK29_KitCompare.txt` |
| `/kitvalidate` | does everything the config asks for FIT | log + `$profile:RK29_KitValidation.txt` |

`/kitcompare` with no argument sweeps every configured kit and writes a one-line verdict
each - that sweep is what surfaced the grenade double-count, because a bug present in all
17 kits reads as noise one kit at a time. It compares against the boot capture
(`m_mCaptured`), never a re-resolve, and against a *copy* of it, since the grenade fold
rewrites the struct it is handed and `/kitdump` shares that data.

`/kitvalidate` spawns each kit's own prefab locally at `Vector(0, -5000, 0)`, waits out
the async item-init (`SETTLE_MS = 750`), runs the real apply, records the drops and
deletes the body. Reusing the live pipeline is the point: a static capacity estimate
would drift from what the game does, and the engine's fit test is the only trustworthy
oracle. Release gate is zero FAIL lines.

**Expected `/kitcompare` diffs** (everything else is a bug):
bayonet added on US Rifleman / Grenadier / LAT and USSR Crewman; bayonet removed on USSR
AR and MG (RPK/PKM have no lug); USSR Grenadier primary mags 7 -> 8; USSR Crewman radios
2 -> 1; and the two sniper rifles, where config carries the bare rifle and the scope
arrives from `m_sDefaultOptic` (`Rifle_M21` vs captured `Rifle_M21_ARTII`, `Rifle_SVD` vs
`Rifle_SVD_PSO`).

---

## 15. Weapons and ammo (settled 2026-08-21, supersedes the MAG_* model above)

Sections 4-10 describe ammo as an item source (`MAG_PRIMARY` / `MAG_LAUNCHER` /
`MAG_SIDEARM`) resolved off whatever weapon holds the slot. That model is gone. It put a
count in a file that could not know which weapon it fed - the shared AR role said
`MAG_PRIMARY 3`, correct for a US M249, and the Soviet kit appended 14 more to reach the
RPK's 17. Two files, neither able to state the truth alone.

**Ammo is now declared beside the weapon it feeds, wherever that weapon is declared.**

```
Catalogs/RK29_Weapons.conf     a weapon: prefab, display name, its ammo ALIAS table
                               ("belt" is one magazine on an M249, another on a PKM)

composition weapon option      m_sWeapon "pkmn"
                               m_aAmmo   { x4 }
                               m_aBlocks { gear_pkm }

composition weapon entry       m_iSlotIndex 2 · m_sAlias "handgun"
                               m_aAmmo   { x2 }
```

Both paths emit through one function (`EmitAmmo`), so there is a single way to say "this
is what feeds that". An ammo entry names a catalog alias, a magazine variant from
`RK29_Magazines.conf`, or neither (the weapon's authored default). Re-declaring a slot
replaces its ammo along with its weapon - they cannot drift apart.

**Weapon options** live on the class (`RK29_ClassSetup.m_aWeapons`) and carry what THIS
class does with that weapon: its ammo, and blocks for any gear or items the choice brings.
Gear stays on the option rather than the weapon definition because rifles are shared
between classes and would otherwise fight over dress. One option for a slot is a fixed
weapon; several make a picker column. This replaced `m_sSourceKitName` routing, which
needed a whole duplicate kit to express "the M60 also wants a different vest".

**Boot applies each class's default option** (the first for each slot) so `m_mKits` still
holds a fieldable kit; the un-optioned composition is kept beside it in `m_mKitsBase`, and
every choice is laid over that base - never over an already-optioned kit, or a weapon's
blocks would stack a second time.

`/kitcompare` was deleted with this change. Diffing config against prefabs stopped being
meaningful once weapons, ammo and dress became config-owned: a non-default weapon has no
prefab to be judged against. `/kitvalidate` now queues one job per weapon option and is
the release gate.

## 16. Role traits (settled 2026-08-23)

A kit says what a soldier *carries*; traits say what they are *qualified at*. Vanilla already
has the mechanism: `SCR_EditableCharacterComponent` merges labels baked into the character
prefab with a per-instance list, and a scatter of user actions and consumables read the merged
set for a "qualified personnel" speed bonus. We grant labels; the base game owns the numbers.

```
composition (Kits/Roles/<role>.conf)   m_aTraits { MEDIC }
       |  RK29_KitCompose.Compose      copied onto the struct, NONE rows warned about
       v
RK29_KitStruct.m_aTraits               survives CloneWithChoices - a weapon pick cannot
       |                               add or drop a qualification
       v
RK29_KitApply.ApplyTraits              SetCustomCharacterLabels_S on every apply, empty
                                       list included (RplProp - replicates to clients)
```

Stock deploy-menu spawns never run the apply pass - their GEAR is authored, by design - but
qualifications are config-owned, so `OnPlayerSpawned_S` calls `ApplyTraits` alone for them,
keyed on the loadout the player actually spawned with. This is not optional polish: selections
live in server memory only (§9), so the first spawn of every session is a stock spawn, and
without it a medic would bandage at rifleman speed until they opened the picker. Labels only,
no inventory touched, and no `SPAWN_SETTLE_MS` defer - labels do not race the async item-init
that the apply pass waits out. A loadout that resolves to no kit (vanilla, foreign) is skipped
so its prefab labels stand.

`RK29_ETrait` is a curated vocabulary, not the raw 150-entry `EEditableEntityLabel`:

| Trait | Label | What vanilla does with it (factor = default, per-instance overridable) |
|---|---|---|
| `MEDIC` | `ROLE_MEDIC` | field dressing 1.5x, tourniquet 1.2x, heal-other action (any health item) by that item's factor, casualty inspect 4x, casualty load 2x, station heal 2x |
| `SAPPER` | `ROLE_SAPPER` | Conflict building 2x, multi-part assembly (mortars, tripods, NSV) 1.5x, vehicle repair 1.5x |
| `VEHICLE_CREW` | `TRAIT_VEHICLE_CREW` | vehicle repair 1.5x, refuel 1.5x, rearm 1.5x, supply load/unload 2x |
| `HELI_CREW` | `TRAIT_HELI_CREW` | every vehicle-crew station above, repair included |
| `LOGISTICS` | `TRAIT_LOGISTICS` | loading a resource container into a vehicle 2x |

Vehicle repair is one action, `SCR_RepairAtSupportStationAction` on `Vehicle_Base` - "support
station" names the PROVIDER, which is either a static station or someone carrying a repair kit
(the kit itself hosts `SCR_RepairSupportStationComponent`). So the bonus applies to field wrench
repair, not just to repairing at a depot.

Only **field dressing** and **tourniquet** prefabs configure `m_aCharacterLabels` +
`m_fRoleSpeedBonus`, so morphine, saline and gauze get no medic bonus - `GetUsageSpeedFactor`
returns 1 for them. A medic's advantage with those items is carrying more of them, nothing more.

Two rules follow from where traits live:

- **Traits belong on the shared role file**, not the faction kit. US character prefabs carry
  hand-authored labels (`TRAIT_SUPPRESSIVE` on the AR, `TRAIT_VEHICLE_CREW` on the crewman)
  and the Soviet ones carry none — declaring the trait in `Kits/Roles/` is what makes both
  sides of a role equal. Faction kits still may restate the list (replaces) or `+` append.
- **Prefab labels cannot be removed at runtime.** `GetAllCharacterLabels` unions prefab and
  instance labels; the instance list only adds. Taking a trait away from a role whose prefab
  bakes it in means editing the prefab.

The write is unconditional so re-kitting mid-round is symmetric: pick the medic kit, get
`ROLE_MEDIC`; pick rifleman next, the list is overwritten with the rifleman's (usually empty)
one. `/kitdigest <kit>` prints the composed traits beside the clothing lines.


## 17. Kits without prefabs, and without deploy entries (settled 2026-08-24)

Three couplings went, in dependency order. Together they mean a new kit is a config edit and
nothing else - no prefab, no deploy-menu row.

**Identity is config-owned, and lives with the role.** `m_UIInfo` (a `SCR_EditableEntityUIInfo`,
the same type prefabs use) supplies icon, preview image, name and browser labels. It sits on
`RK29_KitComposition`, so a role states what it looks like beside what it is qualified at, and
`Compose` resolves nearest-first: captured body -> composition chain -> `RK29_ClassSetup.m_UIInfo`.
The class-level field remains for entries with no composition at all (the two Current Kit
pseudo-entries and the legacy Machine Gunner rows).

The role/faction split falls out of conf inheritance. `role_medic.conf` states name, icon and the
faction-invariant labels; `us_medic.conf` inherits it and adds only `m_Image`, `m_sFaction` and
`m_aAuthoredLabels +{ FACTION_US }`. Both declare the sub-object under the SAME GUID, which is
what makes Enfusion merge them field-by-field rather than replace - the family GUIDs are
`{AB29C0FFEE29CA01..CA09}`, one per role, and each appears only in its own role plus that role's
two faction kits.

The short picker/HUD label stays on the roster as `m_sDisplayName` - it is presentation, not
identity, and it is what the class list and the count rows read.

All 23 classes were migrated from their prefabs' effective UIInfo, labels included, so nothing
moved on screen. Two pre-existing errors were corrected on the way: the medic carried the
rifleman body's `ROLE_RIFLEMAN` (now `ROLE_MEDIC`) and the Soviet crewman had no name or icon at
all, having resolved to a KLMK sniper prefab up its chain (now matches the US crewman).

**Dress is config-owned.** `Compose` no longer seeds `m_mClothing` / `m_mEquipment` from the
captured body. It never needed to: `ReplaceClothing` deletes every garment before re-dressing
and `DressEquipment` clears every unwanted occupant, so apply was always scorched earth. Seeding
only meant an undeclared slot silently inherited whatever body the kit happened to spawn on -
which is how a combat vest ended up on the parade uniform the moment bodies were shared. The
composition is now the whole truth; an undeclared slot is empty.

**Bodies are per-side by default.** `RK29_SideSetup.m_sBodyPrefab` names the faction body and
`RK29_ClassSetup.m_sBodyPrefab` overrides it. The prefab only ever supplied five things and
config now owns three:

| From the prefab | Still needed? |
|---|---|
| Weapon / grenade slot components | shared - all from `Character_Base`, identical on every body |
| Equipment slots | shared from `Character_Base`; contents replaced by `m_aEquipment` |
| Clothing, weapons, items | replaced at apply, and no longer even seeded |
| UIInfo | **config** (`m_UIInfo`) |
| Faction affiliation, voices, identity | **per side** - the one real reason a body exists |

**The rule for when a kit still wants its own body:** only if it is deploy-selectable. A stock
deploy spawn never runs apply, so its body is what the player wears and what the preview
mannequin renders. A picker-only kit is always applied before anyone sees it, so it can take the
side body freely. The 19 kits with deploy rows keep their own prefabs for exactly this reason;
Medic and Pilot, being picker-only, do not have one.

**A loadout entry is now optional.** Boot runs two passes: the loadout walk as before, then every
`RK29_ClassSetup` not already built, captured from its resolved body. `m_aIndexToKit` grows past
the loadout count - every consumer iterates it by position and never maps back to the loadout
manager, and it is built identically on server and client. What a deploy row still buys is
reachability on a cold spawn: selections live in server memory only (§9), so a picker-only kit is
reached by spawning as something else and re-kitting.

**Traits are authoritative on a kitted body.** Vanilla unions instance labels with the prefab's,
so a body could only gain labels, never shed them - wrong when the body is an implementation
detail and a re-kit must actually stop the player being a medic. `RK29_CharacterLabels.c` mods
`SCR_EditableCharacterComponent`: `RK29_SetTraits_S` writes the labels and a replicated
`m_bRK29_TraitsAuthoritative` in one bump, and `GetAllCharacterLabels` returns the kit's list
alone when the flag is set. Anything the kit system never dressed - vanilla characters,
GM-placed AI wearing 29th prefabs - keeps vanilla behaviour untouched. Reach is small by
construction: that method feeds only `HasLabel` and `SCR_PlayerArsenalLoadout`'s save.

Across all 23 bodies exactly one prefab label had a gameplay consumer, the US crew prefab's
`TRAIT_VEHICLE_CREW`; it is now `VEHICLE_CREW` on `role_crewman.conf`, which also gives the
Soviet crewman the parity it never had. The other labels - `ROLE_RIFLEMAN`, `SIZE_M`,
`TRAIT_OPTICS` and the rest - are browser categorisation with no code path, and ride along in
`m_UIInfo`.

**Known gap:** the deploy preview renders the loadout's prefab as-is. Vanilla dresses a mannequin
from data only for `SCR_PlayerArsenalLoadout`; every other loadout takes the
`SetPreviewItemFromPrefab` branch. So a preview shows prefab dress, not the composed kit - a
latent inaccuracy for any kit whose config differs from its body, and the reason a shared body
would show every kit as a rifleman. Fixable by dressing the mannequin from `RK29_KitStruct` in
the `SCR_LoadoutPreviewComponent` mod that already exists for the optic swap.
