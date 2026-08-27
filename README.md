# 29th Infantry Division — Loadouts

Kit system for the 29th ID's Arma Reforger server. Players pick a class, a weapon and an
optic from an in-game menu; the mod strips whatever they are wearing and re-dresses them
from config. Kits are authored as **config compositions**, not character prefabs — nothing
here requires opening a body prefab to change what a rifleman carries.

- **Addon GUID:** `69730206FA071FE2` · **ID:** `29thInfantryDivisionLoadouts`
- **Picker keybind:** `F4` · **Apply kit:** `Space` (both rebindable, category *Kits - 29th ID*;
  rebinds are backed up to `$profile:RK29_Keybinds.json` and restored if a session without the
  mod wipes them from the engine's input settings)
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

## Known issues

**`Character_29th_Parade_Loadout.et` squats on the vanilla Green Beret GUID**
(`894628A45793E7A0` = `US_Army/GreenBerets/Character_US_SF_BaseLoadout.et`). Unlike the
deliberate vest overrides this looks accidental — a Workbench duplicate from `cdeb312`
(2026-06-18) that never got a fresh GUID. Consequences:

- the mod replaces the vanilla SF base loadout server-wide
- `Character_29th_Recruit_Loadout.et` and `Character_29th_Training_Staff_Loadout.et`
  declare that GUID as their parent, so they inherit **from the parade loadout**
- all three files are near-identical: same `ID`, same UIInfo name `"29th Parade"`, same
  M21 + M9. The deploy menu's *29th TP - Recruit* and *29th TP - Training Staff* both
  spawn a parade soldier.

Fixing it means a new GUID for parade plus repointing the two BCT parents and
`GM29_Kits.conf`. Not done yet.
