# 29th Infantry Division — Loadouts

Kit system for the 29th ID's Arma Reforger server. Players pick a class, a weapon and an
optic from an in-game menu; the mod strips whatever they are wearing and re-dresses them
from config. Kits are authored as **config compositions**, not character prefabs — nothing
here requires opening a body prefab to change what a rifleman carries.

- **Addon GUID:** `69730206FA071FE2` · **ID:** `29thInfantryDivisionLoadouts`
- **Picker keybind:** `F4` (rebindable, category *Kits - 29th ID*)
- **Factions:** US, USSR · **Classes:** 12 US, 11 USSR

## Dependencies

| GUID | Addon |
| --- | --- |
| `58D0FB3206B6F859` | Arma Reforger (base data) |
| `595F2BF2F44836FB` | RHS: Status Quo |
| `1337C0DE5DABBEEF` | RHS: Status Quo — Content Pack 01 |
| `BADC0DEDABBEDA5E` | RHS: Status Quo — Content Pack 02 |

## Repo layout

```
Configs/
  KitSystem/
    RK29_KitSetup.conf        entry point - everything below is loaded from here
    Rosters/                  RK29_Roster_US · RK29_Roster_USSR   per-faction class lists
    Catalogs/                 RK29_Aliases · RK29_Magazines · RK29_Optics · RK29_Squads · RK29_Weapons
    Kits/                     Common/infantry -> Roles/<role> -> US|USSR/<role>   (inheritance chain)
    Blocks/                   Common/<bundle> · USSR/gear_pkm · USSR/gear_rpk
  Loadouts/GM29_Kits.conf     the deploy-menu loadout entries the kit system hangs off
  Groups/GM29_Groups.conf     squad presets + which kits each squad may offer
  UI/RK29_Dialogs.conf        picker dialog preset
Prefabs/                      weapons, vests, character bodies
Scripts/Game/                 GM29_* injection + KitSystem/RK29_* runtime
UI/KitSystem/                 picker + HUD layouts
docs/                         design docs (see below)
tools/                        authoring scripts - not game content
```

The `Kits/` path **is** the inheritance chain, read top to bottom: `Common` → `Roles` →
`<FACTION>`. Editing `Roles/role_medic.conf` changes the medic on both sides; editing
`US/us_medic.conf` changes only the American one.

## Docs

| File | Covers |
| --- | --- |
| `docs/29th-kit-system-design.md` | Runtime: picker, HUD, apply, counting, replication |
| `docs/29th-kit-config-backend-design.md` | Where kit *content* comes from: blocks, catalogs, aliases, magazine resolution |

## Diagnostics

Chat commands, **server/listen-host only** (a dedicated client is refused):

| Command | Does |
| --- | --- |
| `kitvalidate` | Spawns each kit and reports anything that did not fit or did not equip |
| `kitdigest <kit name>` | Prints one kit's fully-resolved contents |

`m_bVerboseLogging` in `RK29_KitSetup.conf` turns on the per-item apply trace (~100 lines
per kit). **Leave it off on a live server** — a briefing applies kits for the whole roster
at once. It is off in the committed config.

## Known issues / gotchas

Read this before "fixing" anything in `Prefabs/`.

**Several 29th prefabs deliberately carry vanilla GUIDs and register at vanilla paths.**
They are prefab *overrides*: the file on disk is named `29th_*`, but its `.meta` `Name`
still points at the vanilla path, which is what makes the override bind. This is
intentional and load-bearing — do not "correct" a `.meta` to match its filename, and do
not rename these files.

| On-disk file | Registers as (vanilla) |
| --- | --- |
| `Vests/Vest_ALICE/Variants/29th_Vest_ALICE_rifleman.et` | `Vest_ALICE_rifleman.et` |
| `Vests/Vest_ALICE/Variants/29th_Vest_ALICE_AR.et` | `Vest_ALICE_AR.et` |
| `Vests/Vest_ALICE/Variants/29th_Vest_ALICE_sniper.et` | `Vest_ALICE_sniper.et` |
| `Vests/Vest_ALICE/Variants/29th_Vest_ALICE_commander.et` | `Vest_ALICE_commander.et` |
| `Vests/Vest_Lifchik/29th_Vest_Lifchik.et` | `Vest_Lifchik.et` |
| `Vests/Vest_Lifchik/29th_Vest_Lifchik_GL.et` | `Vest_Lifchik_GL.et` |
| `Vests/Vest_SovietHarness/Variants/29th_Vest_SovietHarness_sharpshooter.et` | `Vest_SovietHarness_sharpshooter.et` |

The vests disable their Etool and Canteen slots to free space for the kits. Because the
override is global, **every** unit on the server gets the modified vest, not just 29th
kits. That is the current accepted behaviour.

This is also why `GM29_KitLoadouts` matches loadout ownership on the **resource GUID only,
never the full ResourceName string** — the path text is not trustworthy here.

**`Character_29th_Parade_Loadout.et` squats on the vanilla Green Beret GUID**
(`894628A45793E7A0` = `US_Army/GreenBerets/Character_US_SF_BaseLoadout.et`). Unlike the
vests this looks accidental — a Workbench duplicate from `cdeb312` (2026-06-18) that never
got a fresh GUID. Consequences:

- the mod replaces the vanilla SF base loadout server-wide
- `Character_29th_Recruit_Loadout.et` and `Character_29th_Training_Staff_Loadout.et`
  declare that GUID as their parent, so they inherit **from the parade loadout**
- all three files are near-identical: same `ID`, same UIInfo name `"29th Parade"`, same
  M21 + M9. The deploy menu's *29th TP - Recruit* and *29th TP - Training Staff* both
  spawn a parade soldier.

Fixing it means a new GUID for parade plus repointing the two BCT parents and
`GM29_Kits.conf`. Not done yet.

**Unreferenced prefabs.** Nothing in this repo points at these. They predate the kit
system, which dresses a single body per faction from config. They are kept because an
external scenario or mission may still reference them — check before deleting.

```
Prefabs/Characters/Factions/BLUFOR/29th_US/    29th_Character_US_AR · _GL · _LAT · _MG · _SL · _Sniper
Prefabs/Characters/Factions/OPFOR/29th_USSR/   29th_Character_USSR_AR · _GL · _MG · _SL
                                               29TH_Character_USSR_LAT · 29TH_Character_USSR_Sharpshooter
Prefabs/Weapons/Rifles/M16/                    29th_M16A2_carbine_M203.et · 29th_M16A3_RedDot.et
```

The two M16 prefabs are simply absent from `Catalogs/RK29_Weapons.conf` — add an entry
there if you want them offered in the picker.

**`resourceDatabase.rdb` is gitignored** and rebuilt by Workbench on open, from the
`.meta` files. If you add a resource, **commit its `.meta` with it** or a fresh clone will
not bind to the same identity.
