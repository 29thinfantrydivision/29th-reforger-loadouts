#!/usr/bin/env python3
"""
Static audit of the RK29 kit configs. Catches the class of mistake that costs a
briefing: config that resolves to something other than what it looks like.

Note on `+{}` vs `{}`: both accumulate. The engine merges the whole ancestry before
script ever sees a container, so a bare `m_aArray {}` in a faction kit adds to what the
role file contributed rather than replacing it. An earlier version of this script
checked for "replace mode" and was simply wrong about the engine.

Checks
  1. reachability   - every kit a roster declares is offered by some squad, and every
                      name a squad offers resolves to a real kit. A squad whose names
                      all miss falls through GetOfferedKits() to EVERY faction kit,
                      which is the opposite of a restriction and logs nothing.
  2. mag doctrine   - a rifle/carbine primary spends exactly 2 spares on tracer.
  3. parity         - per role, US vs USSR: sidearm, backpack, weapon options, rounds.

Exit code is the number of findings, so this works as a pre-release gate.

    python tools/audit_kits.py            # full report
    python tools/audit_kits.py --quiet    # findings only
"""

import io
import os
import re
import sys
import collections

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
KITS = os.path.join(ROOT, 'Configs', 'KitSystem', 'Kits')
CATALOGS = os.path.join(ROOT, 'Configs', 'KitSystem', 'Catalogs')
ROSTERS = os.path.join(ROOT, 'Configs', 'KitSystem', 'Rosters')

ARRAYS = ['m_aBlocks', 'm_aItems', 'm_aClothing', 'm_aWeaponSlots', 'm_aTraits']

#! weapons the 2-tracer doctrine does not cover: sidearms, launchers, belt-feds, marksman
NOT_RIFLE = ('sidearm', 'm72', 'rpg', 'mine', 'rocket', 'pkm', 'rpk', 'm60',
             'm249', 'svd', 'm21', 'm40')

#! ammo labels that mean "the weapon's ordinary load", not a special round. "mag" is the
#! M40's alias: its default magazine lives in MagazinePosition rather than
#! MagazineTemplate, so the catalog has to name it explicitly - see RK29_Weapons.conf.
ORDINARY_ROUNDS = ('ball', 'tracer', 'mag')

findings = []   #! likely wrong - these set the exit code
notes = []      #! asymmetries worth seeing but not worth failing a release on


def report(kind, msg):
    findings.append((kind, msg))


def note(kind, msg):
    notes.append((kind, msg))


def read(path):
    return io.open(path, encoding='utf-8', newline='').read()


def guid_index():
    """GUID -> file path, built from every .meta in the addon."""
    idx = {}
    for dirpath, dirnames, names in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d != '.git']
        for n in names:
            if n.endswith('.meta'):
                m = re.search(r'\{([0-9A-F]{16})\}', read(os.path.join(dirpath, n)))
                if m:
                    idx[m.group(1)] = os.path.join(dirpath, n[:-5])
    return idx


IDX = guid_index()


def parent_of(text):
    m = re.match(r'^\s*RK29_KitComposition\s*:\s*"\{([0-9A-F]{16})\}', text)
    return IDX.get(m.group(1)) if m else None


def top_arrays(text):
    """{array name: (appends?, [body lines])} for arrays declared at depth 1."""
    out, lines, i = {}, text.splitlines(), 0
    while i < len(lines):
        m = re.match(r'^ (m_a[A-Za-z]+) (\+?)\{\s*$', lines[i])
        if not m:
            i += 1
            continue
        depth, body, i = 1, [], i + 1
        while i < len(lines) and depth > 0:
            depth += lines[i].count('{') - lines[i].count('}')
            if depth > 0:
                body.append(lines[i])
            i += 1
        out[m.group(1)] = (m.group(2) == '+', body)
    return out


def chain(path):
    c = []
    while path and os.path.exists(path):
        c.append(path)
        path = parent_of(read(path))
    return list(reversed(c))


def resolve(path):
    """Walk base -> leaf accumulating array contributions. Returns (arrays, chain).

    Both `+{}` and `{}` accumulate. The engine hands script a fully merged container -
    a child writing a bare `m_aArray {}` adds its elements, it does NOT discard what the
    base contributed. Verified in game: us_rifleman.conf writes `m_aClothing {` with only
    a Vest entry, and riflemen still spawn with the Back backpack that role_rifleman.conf
    is the sole source of. Do not "optimise" this back into replace semantics.
    """
    acc = collections.OrderedDict((a, []) for a in ARRAYS)
    ch = chain(path)
    for step in ch:
        arrs = top_arrays(read(step))
        for a in ARRAYS:
            if a in arrs:
                acc[a].extend(arrs[a][1])
    return acc, ch


def weapon_options(body):
    """[(slot, weapon, [(round, count)], is_default)] for a resolved m_aWeaponSlots."""
    txt, out = '\n'.join(body), []
    for slot_blk in re.split(r'\n(?=  RK29_WeaponSlot)', txt):
        sm = re.search(r'm_iSlot (\d+)', slot_blk)
        slot = int(sm.group(1)) if sm else 0
        for opt in re.split(r'\n(?=\s{4,}RK29_WeaponOption)', slot_blk):
            wm = re.search(r'm_sWeapon "([^"]+)"', opt)
            if not wm:
                continue
            ammo = []
            for a in re.split(r'\n(?=\s{6,}RK29_WeaponAmmo)', opt):
                if 'RK29_WeaponAmmo' not in a:
                    continue
                variant = re.search(r'm_sVariant "([^"]+)"', a)
                alias = re.search(r'm_sAlias "([^"]+)"', a)
                prefab = re.search(r'm_sPrefab "\{[0-9A-F]{16}\}([^"]+)"', a)
                count = re.search(r'm_iCount (\d+)', a)
                if variant:
                    label = variant.group(1)
                elif alias:
                    label = alias.group(1)
                elif prefab:
                    label = os.path.basename(prefab.group(1)).replace('.et', '')
                else:
                    label = 'ball'
                ammo.append((label, int(count.group(1)) if count else 1))
            out.append((slot, wm.group(1), ammo, 'm_bDefault 1' in opt))
    return out


def kit_files():
    for faction in ('US', 'USSR'):
        d = os.path.join(KITS, faction)
        for f in sorted(os.listdir(d)):
            if f.endswith('.conf'):
                yield faction, f, os.path.join(d, f)


# ---------------------------------------------------------------------------- checks

def check_reachability(quiet):
    if not quiet:
        print('\n== 1. reachability ==')
    text = read(os.path.join(CATALOGS, 'RK29_Squads.conf'))
    offered = {}
    for m in re.finditer(r'm_sGroupName "([^"]+)"\s*\n\s*m_aKitNames \{(.*?)\n\s*\}', text, re.S):
        offered[m.group(1)] = re.findall(r'"([^"]+)"', m.group(2))

    declared = set()
    for f in os.listdir(ROSTERS):
        if f.endswith('.conf'):
            declared |= set(re.findall(r'm_sKitName "([^"]+)"', read(os.path.join(ROSTERS, f))))

    all_offered = set(k for v in offered.values() for k in v)
    for k in sorted(declared - all_offered):
        report('unreachable', 'kit "%s" is declared by a roster but no squad offers it' % k)

    for group in sorted(offered):
        names = offered[group]
        dangling = [k for k in names if k not in declared]
        if dangling and len(dangling) == len(names):
            report('squad-inert',
                   'squad "%s" offers %d name(s), none of which resolve to a kit (%s). '
                   'GetOfferedKits() then falls through to EVERY faction kit for this '
                   'squad - the opposite of a restriction, and it logs nothing.'
                   % (group, len(names), ', '.join(dangling)))
        elif dangling:
            report('squad-dangling',
                   'squad "%s" names %s, which no roster declares'
                   % (group, ', '.join(dangling)))
        if not quiet:
            tail = '   dangling: ' + ', '.join(dangling) if dangling else ''
            print('   %-24s %2d kit(s)%s' % (group, len(names), tail))


def check_doctrine(quiet):
    if not quiet:
        print('\n== 2. mag doctrine (2 tracer spares on rifle/carbine primaries) ==')
    before = len(findings)
    for faction, name, path in kit_files():
        acc, _ = resolve(path)
        for slot, wpn, ammo, _ in weapon_options(acc['m_aWeaponSlots']):
            if slot != 0 or any(k in wpn for k in NOT_RIFLE):
                continue
            tracer = sum(c for v, c in ammo if v == 'tracer')
            if tracer != 2:
                report('doctrine', '%s / %s carries %d tracer spare(s); doctrine is 2'
                       % (name, wpn, tracer))
    if not quiet and len(findings) == before:
        print('   every rifle/carbine primary carries exactly 2 tracer spares')


def check_parity(quiet):
    if not quiet:
        print('\n== 3. US vs USSR parity ==')
    roles = collections.defaultdict(dict)
    for faction, name, path in kit_files():
        role = name.split('_', 1)[1].replace('.conf', '')
        acc, _ = resolve(path)
        opts = weapon_options(acc['m_aWeaponSlots'])
        roles[role][faction] = {
            'backpack': bool(re.search(r'm_sSlot "Back"', '\n'.join(acc['m_aClothing']))),
            'sidearm': any(w == 'sidearm' for _, w, _, _ in opts),
            'primaries': [w for s, w, _, _ in opts if s == 0],
            'special': set(v for s, w, am, _ in opts if s == 0 for v, c in am
                           if v not in ORDINARY_ROUNDS),
        }
    for role in sorted(roles):
        sides = roles[role]
        if len(sides) < 2:
            if not quiet:
                print('   %-18s %s only' % (role, list(sides)[0]))
            continue
        us, su = sides['US'], sides['USSR']
        hard, soft = [], []
        if us['backpack'] != su['backpack']:
            hard.append('backpack US=%s USSR=%s' % (us['backpack'], su['backpack']))
        if us['sidearm'] != su['sidearm']:
            hard.append('sidearm US=%s USSR=%s' % (us['sidearm'], su['sidearm']))
        if len(us['primaries']) != len(su['primaries']):
            # inherent to the source content - RHS gives the US two M16 variants where
            # the Soviets have one AK. Worth seeing, not worth failing a release on.
            soft.append('weapon options US=%d USSR=%d'
                        % (len(us['primaries']), len(su['primaries'])))
        # compare the SETS, not just emptiness - a side can carry special rounds and
        # still be missing a category the other side has (40mm smoke, for one)
        if us['special'] != su['special']:
            only_us = sorted(us['special'] - su['special'])
            only_su = sorted(su['special'] - us['special'])
            gap = []
            if only_us:
                gap.append('US-only ' + ', '.join(only_us))
            if only_su:
                gap.append('USSR-only ' + ', '.join(only_su))
            hard.append('special rounds: ' + '; '.join(gap))
        if hard:
            report('parity', '%s: %s' % (role, '; '.join(hard)))
        if soft:
            note('parity', '%s: %s' % (role, '; '.join(soft)))
        if not quiet:
            print('   %-18s %s' % (role, '; '.join(hard + soft) or 'matched'))


def main():
    quiet = '--quiet' in sys.argv
    check_reachability(quiet)
    check_doctrine(quiet)
    check_parity(quiet)

    print('\n' + '=' * 78)
    if notes:
        print('%d note(s) - asymmetries that look deliberate:' % len(notes))
        for kind, msg in notes:
            print('  [%s] %s' % (kind, msg))
        print()
    if not findings:
        print('no findings')
        return 0
    print('%d finding(s):' % len(findings))
    for kind, msg in findings:
        print('  [%s] %s' % (kind, msg))
    return len(findings)


if __name__ == '__main__':
    sys.exit(main())
