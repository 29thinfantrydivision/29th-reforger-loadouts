# 29th Infantry Division — Loadouts

Kit system for the 29th ID's Arma Reforger server. Players pick a class, then weapons,
ammunition, attachments and clothing from an in-game menu; the mod strips whatever they are
wearing and re-dresses them from config. Kits are authored as **config compositions**, not
character prefabs — nothing here requires opening a body prefab to change what a rifleman
carries.

- **Addon GUID:** `69730206FA071FE2` · **ID:** `29thInfantryDivisionLoadouts`
- **Kit menu keybind:** `F4` · **Apply kit:** `Space` (both rebindable, category *Kits - 29th ID*;
  rebinds are backed up to `$profile:RK29_Keybinds.json` and restored if a session without the
  mod wipes them from the engine's input settings)
- **Factions:** US, USSR · **Classes:** 11 US (incl. Parade), 10 USSR
- **Default kit** for a player who has not picked one: US *29th Parade*, USSR *Rifleman*

## Dependencies

| GUID | Addon |
| --- | --- |
| `58D0FB3206B6F859` | Arma Reforger (base data) |
| `595F2BF2F44836FB` | RHS: Status Quo |
| `1337C0DE5DABBEEF` | RHS: Status Quo — Content Pack 01 |
| `BADC0DEDABBEDA5E` | RHS: Status Quo — Content Pack 02 |
| `62A711001B8FDEEA` | Big Chungus Rifles |

RHS is a hard dependency: the scripts mod and extend RHS classes, and the mod's own prefabs
inherit from RHS prefabs. Big Chungus Rifles is one because the mod overrides its M1 Garand
anim graph (`Assets/Weapons/Rifles/workspaces/bc_m1_garand.agf`), and an override only wins when
it loads after the file it replaces. Nothing else is.

## Content from other mods

The kit config may name prefabs from **any** mod, dependency or not — ACE Medical's epinephrine
is one (`Catalogs/RK29_Aliases.conf`). Whatever does not load in a session is left out of every
kit as if it were parked: the row never appears, overrides and exclusions naming it do nothing,
and a group left with nothing (a kit's own hat group whose every helmet is missing) is not
offered and leaves the group it was written to replace in place. A default that is missing hands
its flag to the first row left, and a budget floor comes down to what the remaining defaults spend.

- **Add a mod's gear:** put the mod on the server's mod list and name its prefabs in the
  catalogs or kits. Do **not** add it to `addon.gproj`.
- **Drop a mod:** take it off the server's mod list. Its rows drop out on their own; saved kits
  that used them are flagged as changed until it returns.
- **Authoring in Workbench:** add the mod to your local project to browse its prefabs, and leave
  that `addon.gproj` change uncommitted.
- **Rules:** only config may name optional content. A 29th prefab must never inherit from, and a
  script must never name a class from, a mod that is not a dependency above.

On boot the server log names every prefab the config references that did not load, one line
each, then one warning with the count. A mistyped GUID looks the same as a mod that is not
running, so check that list after changing the config.

## Repo layout

```
Configs/
  KitSystem/
    RK29_KitSetup.conf        entry point - everything below is loaded from here
    Rosters/                  RK29_Roster_US · RK29_Roster_USSR   per-faction class lists
    Catalogs/                 RK29_Aliases · RK29_Attachments · RK29_Choices · RK29_Magazines
                              RK29_Overrides · RK29_Weapons
    Kits/                     Common/infantry -> Roles/<role> -> US|USSR/<role>   (inheritance chain)
  Loadouts/GM29_Kits.conf     deploy-menu rows: one Current Kit row per side + the two TP bodies
  Groups/GM29_Groups.conf     squad presets (every squad offers every kit on its side; TP offers
                              the two BCT bodies)
  Notifications/              mid-round re-kit line for the notification feed (merges into vanilla's)
  System/chimeraInputCommon.conf      default keybinds + the action contexts the kit menu lives in
  Systems/ChimeraSystemsConfig.conf   registers the client-side RK29_KitInputSystem
  UI/RK29_Dialogs.conf        the loadout menu's dialog preset (Apply Kit / Close, action context)
Prefabs/                      weapons, vests, empty scabbards, character bodies (see Known issues)
Scripts/Game/                 GM29_* injection (loadouts, groups) + KitSystem/RK29_* runtime
UI/KitSystem/                 loadout menu and HUD layouts
```

The `Kits/` path **is** the inheritance chain, read top to bottom: `Common` → `Roles` →
`<FACTION>`. Editing `Roles/role_medic.conf` changes the medic on both sides; editing
`US/us_medic.conf` changes only the American one.

Everything a kit wears or carries is a **choice group**, including fixed dress: a one-entry
clothing group is a garment the player cannot change, and a kit's own group on a worn slot
replaces the shared group on that slot. There are no fixed clothing or item lists.

Only the two *Current Kit* rows go through the kit system. The *29th TP* rows are plain
vanilla loadouts spawned from their body prefab and never appear in the kit menu.

## Saved kits

Players can save their picks as named kits (up to 12 per class) and load them from the kit
menu. They live client-side in the player's profile, never on the server:

| File | Holds |
| --- | --- |
| `$profile:RK29_KitPresets_v2.json` | every saved kit, every class |
| `$profile:RK29_KitLastUsed.json` | per class, the name of the saved kit last applied |

A saved kit stores the full pick list, so a later config change cannot silently alter it; a kit
the config has moved under since it was saved is flagged in the menu. On each session the last
applied saved kit is seeded for that class, unless it was deleted, renamed or changed by the
config, in which case the class starts at its Standard defaults. Kits built but never saved are
not remembered. The first run imports the old `RK29_KitPresets.json` once and leaves it untouched.

## Round phase

The mod reads the round phase from the 29th Round Timer when it is loaded: during the
preround the briefing HUD is shown and a re-kit heals the player without announcement; once
the round is live a re-kit skips the heal and is announced to the whole server. With no
Round Timer present the phase falls back to `m_bNoTimerOpen` in `RK29_KitSetup.conf`
(`1` = treat as preround, `0` = treat as live). It is `0` in the committed config.

## Diagnostics

Chat commands:

| Command | Does |
| --- | --- |
| `kitmenu` | Toggles the loadout menu (local, ungated, same as F4) |

`m_bVerboseLogging` in `RK29_KitSetup.conf` turns on the per-item apply trace (~100 lines
per kit). **Leave it off on a live server** — a briefing applies kits for the whole roster
at once. It is off in the committed config, and the mod prints a warning at startup when it
is on.

## Known issues

**`Character_29th_Parade_Loadout.et` squats on the vanilla Green Beret GUID**
(`894628A45793E7A0` = `US_Army/GreenBerets/Character_US_SF_BaseLoadout.et`). Unlike the
deliberate vest overrides this looks accidental — a Workbench duplicate from `cdeb312`
(2026-06-18) that never got a fresh GUID. Consequences:

- the mod replaces the vanilla SF base loadout server-wide
- `Character_29th_Recruit_Loadout.et` and `Character_29th_Training_Staff_Loadout.et`
  declare that GUID as their parent, so they inherit **from the parade loadout**
- all three files are identical apart from the parent line: same `ID`, same UIInfo name
  `"29th Parade"`, same M21 + M9. The deploy menu's *29th TP - Recruit* and *29th TP -
  Training Staff* both spawn a parade soldier.

The parade *kit* itself no longer depends on this prefab — it is a normal picker class
(`Kits/US/us_parade.conf`) and nothing in `Configs/` references the prefab any more. Fixing
it means reparenting the two BCT bodies to the vanilla US base and either giving the parade
prefab a fresh GUID or deleting it. Not done yet.
