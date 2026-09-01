"""
Resolve azra.sta's remaining undecoded fields (flags, unkA/unkB) by
cross-referencing against azra.ent and entities.txt -- pure data-parsing,
no RE (see docs/ZONE_FORMAT.md's "azra.sta" section and
tools/parse_zone_placement.py, which this extends).

Usage: python tools/analyze_sta_fields.py <path-to-6r51-dir>
(the directory containing azra.ent, azra.sta, entities.txt)
"""
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from parse_zone_placement import parse_ent, parse_sta


def load_entities_txt(path):
    """typeId -> (modelArchiveIndex, category, name)"""
    out = {}
    for line in Path(path).read_text(encoding="latin-1").splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split(None, 3)
        if len(parts) < 4:
            continue
        type_id, model_idx, category, name = parts
        try:
            out[int(type_id)] = (int(model_idx), int(category), name)
        except ValueError:
            continue
    return out


def main():
    root = Path(sys.argv[1])
    ent_records = parse_ent(root / "azra.ent")
    sta_records, trailer = parse_sta(root / "azra.sta")
    entities = load_entities_txt(root / "entities.txt")

    print(f".ent: {len(ent_records)} records, .sta: {len(sta_records)} records")

    ent_by_pos = {}
    for r in ent_records:
        ent_by_pos.setdefault((r["x"], r["y"], r["z"]), []).append(r)

    # ---- flags distribution ----
    flag_counts = Counter(s["flags"] for s in sta_records)
    print("\n.sta flags value distribution (raw, and /16):")
    for v, n in sorted(flag_counts.items()):
        print(f"  {v:4d} ({v//16:3d}, {'exact/16' if v % 16 == 0 else 'NOT /16!'})  x{n}")

    # ---- flags vs entities.txt category (via sta's own typeId f0) ----
    print("\nflags/16 vs entities.txt category (keyed by .sta's own f0/typeId):")
    match, total, mismatches = 0, 0, []
    for s in sta_records:
        info = entities.get(s["f0"])
        if not info:
            continue
        total += 1
        _, category, name = info
        if s["flags"] // 16 == category:
            match += 1
        else:
            mismatches.append((s["f0"], name, category, s["flags"]))
    print(f"  {match}/{total} records: flags/16 == entities.txt category exactly")
    if mismatches[:15]:
        print("  sample mismatches (typeId, name, category, flags):")
        for m in mismatches[:15]:
            print(f"    {m}")

    # ---- flags vs category via matched .ent record's typeId instead ----
    print("\nflags/16 vs entities.txt category (keyed by matched .ent record's typeId):")
    match2, total2 = 0, 0
    for s in sta_records:
        cands = ent_by_pos.get((s["x"], s["y"], s["z"]))
        if not cands:
            continue
        for c in cands:
            info = entities.get(c["type_id"])
            if not info:
                continue
            total2 += 1
            _, category, name = info
            if s["flags"] // 16 == category:
                match2 += 1
            break
    print(f"  {match2}/{total2} records: flags/16 == entities.txt category (via matched .ent typeId)")

    # ---- unkB: does .sta have it anywhere? cross-check unused byte ranges ----
    print(f"\n.sta record is 32 bytes; decoded so far: f0(0-3) x(4-7) y(8-11) z(12-15) "
          f"rot(16-23) unkA(24-27) flags(28-29) marker(30-31) == 32 bytes total, fully accounted for.")
    print("So .sta genuinely has no unkB-sized field left undecoded -- it's not hiding in padding.")

    # ---- unkA distribution (both .sta's copy and .ent's full set) ----
    print("\n.ent unkA distribution (all 282 records, hex, signed):")
    unk_a_vals = Counter(r["unk_a"] for r in ent_records)
    print(f"  {len(unk_a_vals)} distinct values across {len(ent_records)} records")
    for v, n in sorted(unk_a_vals.items(), key=lambda kv: -kv[1])[:10]:
        print(f"    {v:12d} (0x{v & 0xffffffff:08x})  x{n}")

    print("\n.ent unkB distribution (all 282 records):")
    unk_b_vals = Counter(r["unk_b"] for r in ent_records)
    print(f"  {len(unk_b_vals)} distinct values across {len(ent_records)} records")
    for v, n in sorted(unk_b_vals.items(), key=lambda kv: -kv[1])[:10]:
        print(f"    {v:12d} (0x{v & 0xffffffff:08x})  x{n}")

    # correlate unkA with typeId / category
    print("\nunkA vs typeId: is unkA a function of typeId (same typeId -> same unkA)?")
    typeid_to_unka = {}
    consistent = True
    for r in ent_records:
        prev = typeid_to_unka.setdefault(r["type_id"], r["unk_a"])
        if prev != r["unk_a"]:
            consistent = False
    print(f"  {'YES' if consistent else 'NO'} -- {len(typeid_to_unka)} distinct typeIds")
    if not consistent:
        # show a typeId with multiple unkA values
        by_type = {}
        for r in ent_records:
            by_type.setdefault(r["type_id"], set()).add(r["unk_a"])
        varying = {k: v for k, v in by_type.items() if len(v) > 1}
        print(f"  {len(varying)} typeIds have >1 distinct unkA value, e.g.: "
              f"{list(varying.items())[:5]}")

    print("\nunkB vs typeId: is unkB a function of typeId?")
    consistent_b = True
    by_type_b = {}
    for r in ent_records:
        by_type_b.setdefault(r["type_id"], set()).add(r["unk_b"])
    varying_b = {k: v for k, v in by_type_b.items() if len(v) > 1}
    print(f"  {'YES' if not varying_b else 'NO'} -- {len(by_type_b)} distinct typeIds, "
          f"{len(varying_b)} have >1 distinct unkB value")
    if varying_b:
        print(f"  sample: {list(varying_b.items())[:5]}")


if __name__ == "__main__":
    main()
