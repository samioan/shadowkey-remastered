"""
Parser/verifier for two real per-zone placement files, read directly from
the install image (NOT committed to git -- see docs/ZONE_FORMAT.md and the
project's scope rules): <zone>.ent (entity placement, loaded by the game)
and azra.sta (a leftover level-editor "staging" file the shipped game
never loads -- no code anywhere in 6r51.app opens "*.sta").

Confirms azra.sta's records are a near-total (188/201, ~93.5%) subset of
azra.ent's own entity placements -- same position, same rotation quad,
same "unkA" field -- for every matched record, i.e. .sta is very likely
an earlier/intermediate export of the same entity data now shipped as
azra.ent, abandoned mid-development rather than a distinct file format.

Also corrects a wrong byte-offset claim in an earlier pass of
docs/ZONE_FORMAT.md's `.ent` record struct: typeId is at offset 0x1c
(not 0x14) and name starts at 0x20 (not 0x1e) -- there are two extra
undecoded int32 fields at 0x14/0x18 this project hadn't previously
accounted for. See docs/ZONE_FORMAT.md's "azra.sta" section for the
full writeup.

Usage: python tools/parse_zone_placement.py <path-to-azra.ent> <path-to-azra.sta>
"""
import struct
import sys


def parse_ent(path):
    with open(path, "rb") as f:
        data = f.read()
    (count,) = struct.unpack_from("<I", data, 0)
    records = []
    off = 4
    for _ in range(count):
        x, y, z = struct.unpack_from("<iii", data, off)
        rot = struct.unpack_from("<HHHH", data, off + 0x0C)
        unk_a = struct.unpack_from("<i", data, off + 0x14)[0]
        unk_b = struct.unpack_from("<i", data, off + 0x18)[0]
        type_id = struct.unpack_from("<i", data, off + 0x1C)[0]
        name = data[off + 0x20 : off + 0x20 + 40].split(b"\x00")[0]
        records.append(
            dict(x=x, y=y, z=z, rot=rot, unk_a=unk_a, unk_b=unk_b, type_id=type_id, name=name)
        )
        off += 0x48
    assert off == len(data), f".ent size mismatch: consumed {off}, file is {len(data)}"
    return records


def parse_sta(path):
    with open(path, "rb") as f:
        data = f.read()
    count = len(data) // 32
    records = []
    off = 0
    for i in range(count):
        f0 = struct.unpack_from("<i", data, off)[0]
        x, y, z = struct.unpack_from("<iii", data, off + 4)
        rot = struct.unpack_from("<HHHH", data, off + 0x10)
        unk_a = struct.unpack_from("<i", data, off + 0x18)[0]
        flags = struct.unpack_from("<H", data, off + 0x1C)[0]
        marker = struct.unpack_from("<H", data, off + 0x1E)[0]
        assert marker == 0xCCCC, f"record {i}: expected 0xCCCC marker, got {marker:#x}"
        records.append(dict(f0=f0, x=x, y=y, z=z, rot=rot, unk_a=unk_a, flags=flags))
        off += 32
    trailer = data[off:]
    return records, trailer


def main():
    ent_path, sta_path = sys.argv[1], sys.argv[2]
    ent_records = parse_ent(ent_path)
    sta_records, trailer = parse_sta(sta_path)
    print(f".ent: {len(ent_records)} records")
    print(f".sta: {len(sta_records)} records, trailer bytes: {trailer.hex()}")

    ent_by_pos = {}
    for r in ent_records:
        ent_by_pos.setdefault((r["x"], r["y"], r["z"]), []).append(r)

    matched = 0
    rot_match = 0
    unk_a_match = 0
    type_id_match = 0
    for s in sta_records:
        cands = ent_by_pos.get((s["x"], s["y"], s["z"]))
        if not cands:
            continue
        matched += 1
        if any(c["rot"] == s["rot"] for c in cands):
            rot_match += 1
        if any(c["unk_a"] == s["unk_a"] for c in cands):
            unk_a_match += 1
        if any(c["type_id"] == s["f0"] for c in cands):
            type_id_match += 1

    n = len(sta_records)
    print(f"positions found in .ent: {matched}/{n}")
    print(f"  of those: rot matches {rot_match}/{matched}, unk_a matches {unk_a_match}/{matched}, "
          f"f0==typeId matches {type_id_match}/{matched}")


if __name__ == "__main__":
    main()
