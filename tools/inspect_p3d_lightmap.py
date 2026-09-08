#!/usr/bin/env python3
"""Report Pure3D mesh UV channels and lightmap shader references."""

import argparse
import struct
from pathlib import Path

ROOT = 0xFF443350
PRIMGROUP = 0x00010002
UVLIST = 0x00010007
MEMORY_VERTEX_LIST = 0x00010012
SHADER = 0x00011000
SHADER_TEXTURE_PARAM = 0x00011002
LIGHTMAP = int.from_bytes(b"LMAP", "little")


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def pstring(data: bytes, offset: int, end: int) -> tuple[str, int]:
    if offset >= end:
        return "", offset
    length = data[offset]
    begin = offset + 1
    finish = min(begin + length, end)
    return data[begin:finish].decode("latin-1", "replace").rstrip("\0"), finish


def children(data: bytes, begin: int, end: int):
    cursor = begin
    while cursor + 12 <= end:
        chunk_id, data_length, total_length = struct.unpack_from("<III", data, cursor)
        if data_length < 12 or total_length < data_length or cursor + total_length > end:
            break
        yield cursor, chunk_id, data_length, total_length
        cursor += total_length


def inspect(path: Path) -> dict[str, int]:
    data = path.read_bytes()
    result = {"primgroups": 0, "uv0": 0, "uv1": 0, "uv2plus": 0,
              "memory_vertices": 0, "lightmap_params": 0}

    def walk(begin: int, end: int, parent: int = 0):
        for pos, chunk_id, data_length, total_length in children(data, begin, end):
            payload = pos + 12
            data_end = pos + data_length
            if chunk_id == PRIMGROUP:
                result["primgroups"] += 1
            elif chunk_id == MEMORY_VERTEX_LIST:
                result["memory_vertices"] += 1
            elif chunk_id == UVLIST and data_end >= payload + 8:
                channel = u32(data, payload + 4)
                if channel == 0:
                    result["uv0"] += 1
                elif channel == 1:
                    result["uv1"] += 1
                else:
                    result["uv2plus"] += 1
            elif chunk_id == SHADER_TEXTURE_PARAM and data_end >= payload + 4:
                if u32(data, payload) == LIGHTMAP:
                    result["lightmap_params"] += 1
            if data_end < pos + total_length:
                walk(data_end, pos + total_length, chunk_id)

    walk(0, len(data))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()
    paths = []
    for path in args.paths:
        paths.extend(sorted(path.rglob("*.p3d")) if path.is_dir() else [path])
    if not paths:
        return 1
    totals = {key: 0 for key in inspect(paths[0])}
    for path in paths:
        values = inspect(path)
        for key, value in values.items():
            totals[key] += value
        print(path.name, " ".join(f"{key}={value}" for key, value in values.items()))
    print("TOTAL", " ".join(f"{key}={value}" for key, value in totals.items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
