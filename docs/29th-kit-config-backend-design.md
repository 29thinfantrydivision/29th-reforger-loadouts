# 29th Kit System — Config Backend

Where kit *content* comes from. Kits are hand-editable config compositions built from
named blocks; nothing about what a soldier carries lives in a character prefab.

Companion to [`29th-kit-system-design.md`](29th-kit-system-design.md), which covers the
runtime that consumes the result. Both docs meet at `RK29_KitStruct`.

---

## 1. Layout

```
Configs/KitSystem/
  RK29_KitSetup.conf                    entry point - everything below is loaded from here
  Rosters/    RK29_Roster_US · RK29_Roster_USSR       per-faction class lists
  Catalogs/   RK29_Aliases · RK29_Magazines · RK29_Optics · RK29_Weapons
  Kits/       Common/infantry · Roles/<role> · US|USSR/<faction>_<role>
  Blocks/     Common/<bundle> · USSR/gear_pkm · USSR/gear_rpk
```

Three naming rules: the **folder supplies the qualifier and the filename never repeats
it**; **`Common` means faction-neutral** in both trees; **`RK29_` marks an entry point** —
a file the setup loads by name — while content addressed only by GUID reference stays
bare.

The `Kits/` path **is** the inheritance chain, read top to bottom:

```
Kits/Common/infantry.conf     the bundles every foot soldier carries: grenades, medical,
                              utility. No weapons live in Common.
Kits/Roles/<role>.conf        what the role is, on both sides: identity (m_UIInfo),
                              traits, the cross-faction common items, and the sidearm
                              slot where both factions carry one.
Kits/<FAC>/<fac>_<role>.conf  the primary weapon, the faction's vest and pack, and the
                              faction remainder.
```

Editing `Roles/role_medic.conf` changes the medic on both sides. Editing
`US/us_medic.conf` changes only the American one.

---

## 2. Authoring rules

1. **Blocks add up.** Items are additive; clothing and weapon slots are later-wins per
   slot.
2. **Arrays accumulate down the chain.** Both `+{}` and `{}` contribute — the engine
   merges the whole ancestry before script sees a container, so a faction kit writing a
   bare `m_aClothing {}` adds its vest to what the role file declared rather than
   replacing it.
3. **Shipped configs are additive-only.** The use-site override mechanism exists but is a
   last resort. A kit that carries less references a leaner block; a kit that carries more
   adds inline. Reading any kit file top to bottom is pure addition — nothing you read
   gets taken away further down.
4. **If a reference would remove more than a couple of things, make a smaller base block
   instead.** "Minus hat plus helmet" needs no override at all — clothing is later-wins
   per slot, so a later helmet entry replaces the hat.
5. **Author weapons as base prefabs, except kits whose stock version ships glass.** Stock
   deploy spawns are never mutated — they are the backup path when this addon
   misbehaves — so a kit that must spawn scoped authors the pre-composed variant and sets
   `m_sDefaultOptic` to match. `m_sWeaponVariantPrefab` remains the escape hatch for
   truly integral optics.
6. **Never author kit gear inside garment prefabs.** Config items are character-level by
   construction, and the clothing keep-optimisation depends on it: garment-internal gear
   does not survive a same-garment keep.
7. **No placement paths.** Items are `{prefab, count}` and the solver routes them.
   `m_sTargetHint` is the rare manual override; `m_aPreferredContainers` is the softer
   and usual one.
8. **Faction-specific gear goes through aliases.** Consumables differ by side
   (`FieldDressing_US_01` vs `_USSR_01`), so a shared block uses the alias `bandage` and
   stays one file. Direct prefab entries bind a block to one faction — right for rigs and
   weapons, wrong for shared bases.

---

## 3. Schema

```c
enum RK29_EItemSource { PREFAB, ALIAS }

[BaseContainerProps()]
class RK29_BlockItemEntry
{
    RK29_EItemSource     m_eSource;              // explicit - no magic-string namespaces
    ResourceName         m_sPrefab;              // PREFAB
    string               m_sAlias;               // ALIAS: catalog name, e.g. "bandage"
    string               m_sVariant;             // magazine variant, where applicable
    int                  m_iCount;               // default 1
    string               m_sTargetHint;          // hard placement override
    bool                 m_bOnlyIfPrimaryTakesIt;// bayonets: skip when the gun has no lug
    ref array<string>    m_aPreferredContainers; // soft placement preference
}

[BaseContainerProps()]
class RK29_BlockClothingEntry
{
    string       m_sSlot;    // Hat, Jacket, Vest, Back, ... or an equipment slot name
    ResourceName m_sPrefab;  // empty clears the slot
    string       m_sAlias;
}

//! One building block - Configs/KitSystem/Blocks/**.conf. Leaf-only; blocks never
//! reference other blocks.
[BaseContainerProps(configRoot: true)]
class RK29_KitBlock
{
    ref array<ref RK29_BlockClothingEntry> m_aClothing;
    ref array<ref RK29_BlockClothingEntry> m_aEquipment;   // wristwatch, binocular slots
    ref array<ref RK29_BlockItemEntry>     m_aItems;
}

//! How a kit references a block. Item overrides SET the final count of that identity
//! from this block; 0 removes it entirely.
[BaseContainerProps()]
class RK29_BlockRef
{
    ResourceName                       m_sBlock;
    ref array<ref RK29_BlockItemEntry> m_aItemOverrides;
}

//! One kit's content story. Compositions inherit each other conf-from-conf; blocks do
//! not. Reading this file is reading the kit.
[BaseContainerProps(configRoot: true)]
class RK29_KitComposition
{
    ref array<ref RK29_BlockRef>           m_aBlocks;
    ref array<ref RK29_WeaponSlot>         m_aWeaponSlots;
    ref array<ref RK29_BlockItemEntry>     m_aItems;
    ref array<ref RK29_BlockClothingEntry> m_aClothing;
    ref array<ref RK29_BlockClothingEntry> m_aEquipment;
    ref SCR_EditableEntityUIInfo           m_UIInfo;   // what this role IS
    ref array<RK29_ETrait>                 m_aTraits;  // what it is QUALIFIED at
}
```

The roster (`RK29_SideSetup` → `RK29_ClassSetup`) names the class, points at its
composition, and carries presentation and optic policy: `m_sDisplayName`,
`m_aOpticCategories`, `m_sDefaultOptic`, `m_aOpticExclude` / `m_aOpticInclude`, and
`m_sBodyPrefab` where the class overrides its side's body.

Every attribute uses `category: "29th"` and carries a `desc`. Block references are
class-constrained (`params: "conf class=RK29_KitBlock"`) so a wrong-class pick is
impossible in the editor, and every array element class has a custom title decoration so
entries read as content — `3x bandage`, not `RK29_BlockItemEntry`.

---

## 4. Weapons and ammo

**Ammo is declared beside the weapon it feeds, wherever that weapon is declared.** Ammo
counts cannot live in a shared block: a shared AR role saying "3 magazines" is right for
an M249 and wrong for an RPK, and no amount of appending in the faction file lets either
file state the truth alone.

```
Catalogs/RK29_Weapons.conf     a weapon: id, prefab (or per-faction prefabs), display
                               name override, its ammo ALIAS table, and where its
                               magazines prefer to live. "belt" is one magazine on an
                               M249 and another on a PKM.

weapon option in a kit         m_sWeapon "pkmn"
                               m_aAmmo   { x4 }
                               m_aBlocks { gear_pkm }
```

An ammo entry names a catalog alias, a magazine variant from `RK29_Magazines.conf`, a
literal prefab, or nothing at all — in which case the weapon's authored default magazine
is used. Re-declaring a slot replaces its weapon *and* its ammo together, so the two can
never drift apart.

**Weapon options live on the class** and carry what *this* class does with that weapon:
its ammo, and blocks for any gear the choice brings. Gear sits on the option rather than
on the weapon definition because rifles are shared between classes and would otherwise
fight over dress. One option for a slot is a fixed weapon; several make a picker column.
`m_bDefault` marks the slot's default regardless of list position; unset on every option
means the first one wins.

**Magazine doctrine.** A rifle or carbine kit keeps its total spare count but spends
exactly two of those spares on tracer; the gun itself is loaded with ball.
`tools/audit_kits.py` checks this.

**Attachment fitment is derived, not declared.** `WeaponRejectsAttachment` intersects the
mount types the weapon and the attachment each declare. The picker filters its optic
column against the selected weapon and the server re-checks the request, dropping the
optic rather than the kit. Options declaring `m_sWeaponVariantPrefab` or
`m_aRequiredAttachments` are exempt — they swap the weapon or bring their own adapter.

The same helper gates bayonets, so the two can never disagree. Fitment also honours
obstruction, which attachments declare themselves:
`SCR_WeaponAttachmentObstructionAttributes.m_aObstructedAttachmentTypes` on `Bayonet_M9`
names the M203 mounts, `Bayonet_6Kh4` names the GP-25 one. The test reads what is
**seated** on the weapon, never the slots it merely offers — an AK-74N advertises an empty
GP-25 slot and keeps its bayonet, while a grenadier's rifle has a launcher in that slot
and loses it. No hand-maintained exception list.

---

## 5. Identity, dress and bodies

**Identity is config-owned and lives with the role.** `m_UIInfo` — a
`SCR_EditableEntityUIInfo`, the same type prefabs use — supplies icon, preview image, name
and browser labels. It sits on `RK29_KitComposition`, so a role states what it looks like
beside what it is qualified at. `Compose` resolves nearest-first: captured body →
composition chain → `RK29_ClassSetup.m_UIInfo`, the last for entries with no composition
at all.

The role/faction split falls out of conf inheritance. `role_medic.conf` states name, icon
and the faction-invariant labels; `us_medic.conf` inherits it and adds only `m_Image`,
`m_sFaction` and `m_aAuthoredLabels +{ FACTION_US }`. Both declare the sub-object under
the **same GUID**, which is what makes Enfusion merge them field by field rather than
replace. The family GUIDs are `{AB29C0FFEE29CA01..CA09}`, one per role, each appearing in
its own role file plus that role's two faction kits.

The short picker and HUD label stays on the roster as `m_sDisplayName` — presentation, not
identity.

**Dress is config-owned, and not seeded from the body.** `ReplaceClothing` deletes every
garment before re-dressing and `DressEquipment` clears every unwanted occupant, so apply
is scorched earth either way. Seeding only meant an undeclared slot silently inherited
whatever body the kit happened to spawn on — which is how a combat vest ends up on a
parade uniform the moment bodies are shared. The composition is the whole truth and an
undeclared slot is empty.

Equipment slots (`WristwatchSlot`, `BinocularSlot`) are a third channel beside dress slots
and items, merged slot-keyed later-wins and delta-swapped at apply. Without them the apply
pass silently deletes the watch and binoculars, because the loose-item strip catches them
and nothing re-adds them.

**Bodies are per-side by default.** `RK29_SideSetup.m_sBodyPrefab` names the faction body
and `RK29_ClassSetup.m_sBodyPrefab` overrides it. What a prefab still supplies:

| From the prefab | Still needed? |
| --- | --- |
| Weapon and grenade slot components | shared — all from `Character_Base`, identical on every body |
| Equipment slots | shared from `Character_Base`; contents replaced by `m_aEquipment` |
| Clothing, weapons, items | replaced at apply, and not even seeded |
| Garment structural content (pouches = capacity) | yes — like a weapon's magazine well |
| Faction affiliation, voices, identity | **the one real reason a body exists** |

**A kit only wants its own body if it is deploy-selectable.** A stock deploy spawn never
runs apply, so its body is what the player wears and what the preview mannequin renders. A
picker-only kit is always applied before anyone sees it and can take the side body freely.

A loadout entry in `GM29_Kits.conf` is therefore how a kit is *spawned*, not what makes it
exist. What a deploy row buys is reachability on a cold spawn.

---

## 6. Traits

A kit says what a soldier *carries*; traits say what they are *qualified at*. Vanilla
already has the mechanism — `SCR_EditableCharacterComponent` merges prefab labels with a
per-instance list, and a scatter of user actions and consumables read the merged set for a
qualified-personnel speed bonus. The mod grants labels; the base game owns the numbers.

```
Kits/Roles/<role>.conf   m_aTraits { MEDIC }
      |  Compose         copied onto the struct, NONE rows warned about
      v
RK29_KitStruct.m_aTraits survives the weapon-choice clone - a gun cannot add or drop a
      |                  qualification
      v
ApplyTraits              written on every apply, empty list included
```

`RK29_ETrait` is a curated vocabulary rather than the raw 150-entry `EEditableEntityLabel`,
so the config names a job and the engine enum never reaches the kit author.

| Trait | Label | What vanilla does with it |
| --- | --- | --- |
| `MEDIC` | `ROLE_MEDIC` | field dressing 1.5x, tourniquet 1.2x, heal-other by that item's factor, casualty inspect 4x, casualty load 2x, station heal 2x |
| `SAPPER` | `ROLE_SAPPER` | building 2x, multi-part assembly 1.5x, vehicle repair 1.5x |
| `VEHICLE_CREW` | `TRAIT_VEHICLE_CREW` | vehicle repair, refuel, rearm 1.5x, supply load/unload 2x |
| `HELI_CREW` | `TRAIT_HELI_CREW` | every vehicle-crew station above, repair included |
| `LOGISTICS` | `TRAIT_LOGISTICS` | loading a resource container into a vehicle 2x |

Vehicle repair is one action, `SCR_RepairAtSupportStationAction` on `Vehicle_Base` —
"support station" names the *provider*, which is either a static station or someone
carrying a repair kit. The bonus applies to field wrench repair, not only to a depot.

Only field dressing and tourniquet prefabs configure `m_aCharacterLabels` and
`m_fRoleSpeedBonus`, so morphine, saline and gauze get no medic bonus at all. A medic's
advantage with those is carrying more of them.

Two rules follow from where traits live:

- **Traits belong on the shared role file.** US character prefabs carry hand-authored
  labels and the Soviet ones carry none, so declaring the trait in `Kits/Roles/` is what
  makes both sides of a role equal.
- **Prefab labels cannot be removed at runtime** through the vanilla path — the instance
  list only ever adds. This is why the kit system takes ownership of the merged answer
  instead (runtime doc §7).

Stock deploy spawns never run the apply pass — their gear is authored, by design — but
qualifications are config-owned, so `OnPlayerSpawned_S` calls `ApplyTraits` alone for
them, keyed on the loadout the player actually spawned with. Without it a medic would
bandage at rifleman speed until they opened the picker, because the first spawn of every
session is a stock spawn. A loadout resolving to no kit is skipped so its prefab labels
stand.

---

## 7. Aliases and magazine variants

`RK29_Aliases.conf` maps a name to a per-faction prefab array, so new factions are config
additions rather than schema surgery. An alias entry can carry its own
`m_aPreferredContainers`, which the item inherits when it does not state its own.

`RK29_Magazines.conf` keys magazine variants by magazine-well class. A kit asking for the
`tracer` variant resolves it through whichever weapon holds the slot, so one ammo line
serves every weapon sharing a well.

Both are entry points loaded by `RK29_KitSetup.conf`.

---

## 8. Placement

Items are `{prefab, count}` with no authored path. The solver runs per kit and is cached
by kit signature.

1. **Collect containers** — every cargo storage on the body, plus each named slot of an
   `SCR_EquipmentStorageComponent` expanded as its own candidate.
2. **Solve** most-constrained-item-first, least-constraining-container-first, with the
   engine as the only fit oracle (`CanInsertResourceInStorage`).
3. **Rank** candidates, strongest authority first:

   | Key | Meaning |
   | --- | --- |
   | `penalty` | never strand a scarce item |
   | `tier` | a matching special slot beats cargo; among those the visible one wins (vest over uniform) |
   | `listed` | the item's `m_aPreferredContainers`, from the entry or its alias |
   | `cohesion` | join the rest of your stack, bounded at `COHESION_MAX_STACK = 3` so it cannot outvote a preference — magazines never |
   | `whole` | start a stack where all of it fits, which is what stops two tourniquets splitting when the trousers could hold both |
   | `detail` | the fine order: uniform before trouser, outer mounts |

   Everything below that is a tie, and a tie goes to the earliest container in the list,
   which is ordered by storage priority.
4. **Grenades** are primed into the throwable slot *before* cargo placement and removed
   from the batch, so counts stay exact and the slot never sits empty while grenades fill
   uniform space.

Item size comes from `ItemMaxDimension`, not `ItemVolume` — most items never author a
volume (the M249 belt box declares `ItemDimensions 15 15 15` and no volume at all), so
sorting on it reads 0, puts the biggest items last, and lets pistol magazines claim the
pouches.

Grenades are **items only** in config kits. The SCR layer equips and cycles throwables
from inventory, so an authored slot only ever decided the spawn-primed grenade. `Compose`
must not copy a captured grenade slot into a composed kit: the config already counts that
grenade as an item, and doing so fields one extra grenade on every kit on both sides.

Container reads are plain merged reads. Enfusion resolves the whole ancestry before script
sees a container, so no ancestry walking or name-keyed dedup belongs anywhere in this
path.

---

## 9. Boot validation

One server pass at `RK29_KitManager.Boot`, naming the offending file and field.

**ERROR — the kit is wrong and someone must fix it:**

| Check | Message |
| --- | --- |
| Class resolves no body prefab | `no body prefab (set m_sBodyPrefab, or m_sBodyPrefab on its side config)` |
| Composition missing, or not an `RK29_KitComposition` | `composition not found` / `not an RK29_KitComposition` |
| Block missing, or not an `RK29_KitBlock` | `block not found` / `not an RK29_KitBlock` |
| `PREFAB` entry with no prefab | `PREFAB entry with no prefab` |
| Alias unresolved for this kit's faction | `alias 'x' unresolved for USSR` |
| Item entry with neither prefab nor alias | `item entry with no prefab or alias` |
| Clothing slot alias unresolved | `slot 'Vest' alias 'x'` |
| Weapon option resolving to no prefab | `weapon option 'x' resolves to no prefab` |
| Ammo alias the weapon's catalog entry does not declare | `AMMO "belt" is not declared by pkmn` |
| Magazine variant not defined for the weapon's well | `ammo variant 'tracer' not defined for ...` |

**WARNING — it will run, but read the line:**

- Any referenced side, optic, alias, magazine or weapon config missing or
  unreadable. `RK29_KitSetup.conf` itself missing disables customization entirely while
  still counting kits.
- More than one option on a slot setting `m_bDefault`.
- Class-level weapon options left over from before options moved to the composition.
- Composed item count disagreeing with the captured body's.
- `m_bVerboseLogging` left on.

Capacity is deliberately **not** validated here. A volume sum drifts from what the game
actually does, and the engine's fit test is the only trustworthy oracle — which is what
`kitvalidate` uses.

Two checks worth having that do not exist yet: a **dead-override** warning, so renaming an
identity in a base cannot silently orphan a use-site override, and a **faction sanity**
pass warning when a composed item is absent from the kit faction's `InventoryItems`
catalog (`faction.GetFactionEntityCatalogOfType` is a ready-made cross-faction-gear
detector).

### Verbose tracing

The per-item trace — every strip, every container weighed, every placement — is roughly
100 lines per apply and is what makes placement bugs findable. It is gated behind
`m_bVerboseLogging` in `RK29_KitSetup.conf`, off by default, routed through
`RK29_Log.Trace`. Warnings and the one-per-apply begin and done lines always print.

Leave it off on a live server: a briefing applies kits for the whole roster at once.

---

## 10. Tooling

Three chat commands, all server or listen-host only — they refuse for a remote client.

| Command | Answers | Output |
| --- | --- | --- |
| `kitdigest [kit]` | what does the config compose to | log |
| `kitvalidate` | does everything the config asks for actually FIT | log |

`kitvalidate` spawns each kit's body locally well below the map, runs the real apply,
records the drops and deletes the body. Reusing the live pipeline is the point — a static
estimate would drift from what the game does. It queues one job per weapon option. **The
release gate is zero FAIL lines.**

`tools/audit_kits.py` is the static companion: it resolves the inheritance chains without
running the game and checks the magazine doctrine and US/USSR parity.

### Workbench expectations

Conf inheritance is native, so "show only modified" gives the child's delta and the full
view gives resolved values. Block references and overrides are plain data — Workbench
shows them as authored but cannot render the composed kit, because composition happens in
the loader. `kitdigest` is the resolved view.

---

## 11. Known surprises

- **Override scope is per reference.** An override caps one block's contribution; two
  blocks both contributing the same prefab sum past it. The digest shows it. Documented,
  not prevented.
- **Preview drift.** The deploy preview renders the loadout's prefab, not the composed
  kit. See the runtime doc §11.
