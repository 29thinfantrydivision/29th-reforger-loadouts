#!/usr/bin/env python3
"""
Strip inherited SCR_EditableEntityVariantData blocks from 29th character prefabs.

Why: a prefab duplicated from a vanilla character carries the vanilla
m_VariantData block with it. The respawn system resolves a loadout through that
variant data, so SCR_BasePlayerLoadout.GetLoadoutResource() returns the VANILLA
variant prefab instead of the 29th prefab. Symptoms are a deploy-menu kit that
spawns stock gear, plus a resource GUID that is not stable between calls.

Usage:
    python strip_variant_data.py <prefab_root>            # dry run, reports only
    python strip_variant_data.py <prefab_root> --apply    # writes changes

Writes a .bak alongside each modified file. CRLF line endings are preserved
byte-for-byte; the file is only ever rewritten if a block was actually removed.
"""

import sys
import os
import shutil

MARKER = "m_VariantData SCR_EditableEntityVariantData"


def find_block(lines, start_index):
    """
    Brace-match forward from the marker line. Returns the index of the line
    holding the block's closing brace, or None if braces never balance.
    """
    depth = 0
    seen_open = False

    for i in range(start_index, len(lines)):
        line = lines[i]
        depth += line.count("{")
        depth -= line.count("}")

        if "{" in line:
            seen_open = True

        if seen_open and depth <= 0:
            return i

    return None


def process(path, apply_changes):
    with open(path, "rb") as handle:
        raw = handle.read()

    text = raw.decode("utf-8")

    # keepends=True so CRLF rides along with each line untouched
    lines = text.splitlines(keepends=True)

    targets = [i for i, line in enumerate(lines) if MARKER in line]
    if not targets:
        return None

    removed_total = 0
    variant_refs = []

    # Work back-to-front so earlier indices stay valid
    for start in reversed(targets):
        end = find_block(lines, start)
        if end is None:
            print("  !! braces never balanced at line %d - SKIPPED" % (start + 1))
            continue

        for line in lines[start:end + 1]:
            if "m_sVariantPrefab" in line:
                variant_refs.append(line.strip())

        del lines[start:end + 1]
        removed_total += (end - start + 1)

    if removed_total == 0:
        return None

    if apply_changes:
        shutil.copy2(path, path + ".bak")
        with open(path, "wb") as handle:
            handle.write("".join(lines).encode("utf-8"))

    return removed_total, variant_refs


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    root = sys.argv[1]
    apply_changes = "--apply" in sys.argv

    if not os.path.isdir(root):
        print("not a directory: %s" % root)
        return 1

    touched = 0

    for dirpath, _dirnames, filenames in os.walk(root):
        for name in sorted(filenames):
            if not name.endswith(".et"):
                continue

            path = os.path.join(dirpath, name)
            result = process(path, apply_changes)
            if not result:
                continue

            removed, refs = result
            touched += 1
            print("\n%s" % path)
            print("  removed %d line(s)" % removed)
            for ref in refs:
                print("  was pointing at: %s" % ref)

    print("\n%d file(s) %s" % (touched, "modified" if apply_changes else "would be modified"))

    if touched and not apply_changes:
        print("Dry run only. Re-run with --apply to write changes.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
