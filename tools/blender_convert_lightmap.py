#!/usr/bin/env python3
"""Convert a linear EXR lightmap to an 8-bit linear PNG with a fixed range."""

import argparse
import sys
from pathlib import Path

import OpenImageIO as oiio


def main() -> int:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--range", type=float, default=2.0, dest="value_range")
    args = parser.parse_args(argv)
    if args.value_range <= 0:
        parser.error("--range must be positive")
    source = oiio.ImageBuf(str(args.input.resolve()))
    if source.has_error:
        raise RuntimeError(source.geterror())
    rgb = oiio.ImageBufAlgo.channels(source, (0, 1, 2), ("R", "G", "B"))
    encoded = oiio.ImageBufAlgo.mul(rgb, (1.0 / args.value_range,) * 3)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not encoded.write(str(args.output.resolve()), oiio.UINT8):
        raise RuntimeError(encoded.geterror())
    print(f"Lightmap PNG: {source.spec().width}x{source.spec().height}, "
          f"linear range 0..{args.value_range} -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
