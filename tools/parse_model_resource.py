"""
Parser/verifier for Shadowkey's 3D model resource archive
(system/apps/6r51/models.idx + models.huge in the game install tree).

See docs/MODEL_FORMAT.md for the full format writeup and how this was
reverse-engineered from Actor3D_TransformAndSubmitModel and
RoomGeometry_TransformAndSort in 6r51.app.

    python tools/parse_model_resource.py <models.idx> <models.huge> [--verify] [--dump N]
"""
import argparse
import struct
import sys


def read_index(idx_path):
    data = open(idx_path, "rb").read()
    (count,) = struct.unpack_from("<I", data, 0)
    entries = []
    off = 4
    for _ in range(count):
        start, size = struct.unpack_from("<II", data, off)
        off += 8
        entries.append((start, size))
    return entries


def parse_model(blob):
    """Decode one model resource slice. Returns a dict of parsed fields,
    or raises if the header looks inconsistent."""
    h0, h1, h2, h3, h4, h5, h6 = struct.unpack_from("<7h", blob, 0)
    uv_base = h0 + h1 * h5          # halfwords
    face_base = uv_base + h3 * 2    # halfwords
    tex_hdr_base = face_base + h4 * 6  # halfwords
    tex_hdr_byte = tex_hdr_base * 2
    skin_count, width, height = struct.unpack_from("<3H", blob, tex_hdr_byte)
    pix_start = tex_hdr_byte + 8
    pix_total = skin_count * width * height * 2
    trailer = len(blob) - pix_start - pix_total

    vertices = [
        struct.unpack_from("<3h", blob, h0 * 2 + v * 6)
        for v in range(h1 * h2)
    ]
    uvs = [
        struct.unpack_from("<2H", blob, uv_base * 2 + u * 4)
        for u in range(h3)
    ]
    faces = [
        struct.unpack_from("<6h", blob, face_base * 2 + f * 12)
        for f in range(h4)
    ]

    return {
        "frameCount": h1, "vertsPerFrame": h2, "uvCount": h3,
        "faceCount": h4, "halfwordsPerFrame": h5, "versionTag": h6,
        "skinCount": skin_count, "width": width, "height": height,
        "pixStart": pix_start, "pixTotal": pix_total, "trailer": trailer,
        "vertices": vertices, "uvs": uvs, "faces": faces,
    }


def verify(entries, huge):
    ok, bad = 0, 0
    for i, (start, size) in enumerate(entries):
        if size < 14:
            continue
        blob = huge[start:start + size]
        try:
            m = parse_model(blob)
            assert m["halfwordsPerFrame"] == m["vertsPerFrame"] * 3
            for vA, vB, vC, uA, uB, uC in m["faces"]:
                assert 0 <= vA < m["vertsPerFrame"] and 0 <= vB < m["vertsPerFrame"] and 0 <= vC < m["vertsPerFrame"]
                assert 0 <= uA < m["uvCount"] and 0 <= uB < m["uvCount"] and 0 <= uC < m["uvCount"]
            assert m["trailer"] >= 0 and m["trailer"] % 6 == 0
            ok += 1
        except Exception as e:
            print(f"entry {i}: FAILED ({e})")
            bad += 1
    print(f"verified {ok} entries, {bad} failures, {len(entries) - ok - bad} skipped (too small)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("idx_path")
    ap.add_argument("huge_path")
    ap.add_argument("--verify", action="store_true", help="decode every entry and check invariants")
    ap.add_argument("--dump", type=int, metavar="N", help="print entry N's parsed header")
    args = ap.parse_args()

    entries = read_index(args.idx_path)
    huge = open(args.huge_path, "rb").read()
    print(f"{len(entries)} entries, models.huge is {len(huge)} bytes")

    if args.verify:
        verify(entries, huge)

    if args.dump is not None:
        start, size = entries[args.dump]
        blob = huge[start:start + size]
        m = parse_model(blob)
        for k, v in m.items():
            if k in ("vertices", "uvs", "faces"):
                print(f"{k}: {len(v)} entries, first 3 = {v[:3]}")
            else:
                print(f"{k}: {v}")


if __name__ == "__main__":
    sys.exit(main())
