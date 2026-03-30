#!/usr/bin/env python3
"""
Milky Way top-down minimap renderer for binary star catalogs.

The input format is a framed zstd stream with a 12-byte header:
- 4-byte magic token
- uint32 version
- uint32 record count

Each record is expected to match '<14fQ' with Cartesian coordinates in
light-seconds and photometric metadata in a common star-catalog layout.
"""

from __future__ import annotations

import argparse
import math
import os
import random
import struct
from typing import Optional, Iterator

import numpy as np
from PIL import Image, ImageDraw
import zstandard as zstd

STAR_STRUCT_FMT = '<14fQ'
STAR_STRUCT_SIZE = struct.calcsize(STAR_STRUCT_FMT)


def read_data_location(config_path: str, label: str) -> Optional[str]:
    """Read a path value from a key/value config file."""
    if not os.path.exists(config_path):
        return None

    with open(config_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if '=' in line:
                key, value = line.split('=', 1)
                key = key.strip()
                value = value.strip().strip('"\'')
                if key == label:
                    return value
    return None


def safe_name(path: str) -> str:
    """Return a non-sensitive display name for logs."""
    name = os.path.basename(path.rstrip('/\\'))
    return name if name else '<path>'


def iter_catalog_zst(
    path: str,
    subsample: float = 1.0,
    expected_magic: Optional[str] = None,
    strict_magic: bool = False,
) -> Iterator[dict]:
    """
    Stream-read stars from a zstd-compressed binary catalog file.

    The parser expects records in '<14fQ' order with keys matching:
    x, y, z, gmag, bp_rp, parallax_mas, parallax_error_mas, pmra,
    pmdec, ruwe, gal_l_deg, gal_b_deg, ra_deg, dec_deg, source_id.
    """
    expected_magic_bytes = expected_magic.encode('ascii') if expected_magic else None

    with open(path, 'rb') as f:
        magic = f.read(4)
        if len(magic) != 4:
            raise ValueError('Invalid file header: missing magic token.')
        if strict_magic and expected_magic_bytes and magic != expected_magic_bytes:
            raise ValueError('Catalog magic token does not match --expected-magic.')
        if expected_magic_bytes and (not strict_magic) and magic != expected_magic_bytes:
            print('[warn] Catalog magic differs from --expected-magic; continuing.')

        version = struct.unpack('<I', f.read(4))[0]
        star_count = struct.unpack('<I', f.read(4))[0]

        print(
            f"[info] Header: magic={magic.decode('ascii', errors='replace')} "
            f"version={version} records={star_count:,}"
        )

        dctx = zstd.ZstdDecompressor()
        with dctx.stream_reader(f) as reader:
            count = 0
            while True:
                buf = reader.read(STAR_STRUCT_SIZE)
                if len(buf) < STAR_STRUCT_SIZE:
                    break

                count += 1

                if subsample < 1.0 and random.random() > subsample:
                    continue

                data = struct.unpack(STAR_STRUCT_FMT, buf)

                yield {
                    'x': data[0],
                    'y': data[1],
                    'z': data[2],
                    'gmag': data[3],
                    'bp_rp': data[4],
                    'parallax_mas': data[5],
                    'parallax_error_mas': data[6],
                    'pmra': data[7],
                    'pmdec': data[8],
                    'ruwe': data[9],
                    'gal_l_deg': data[10],
                    'gal_b_deg': data[11],
                    'ra_deg': data[12],
                    'dec_deg': data[13],
                    'source_id': data[14],
                }

                print(f"[info] Read {count:,} records from {safe_name(path)}")


def main(argv: Optional[list[str]] = None) -> int:
    """Parse arguments, render a top-down density image, and write PNG output."""
    parser = argparse.ArgumentParser(
        description='Render a top-down Milky Way minimap from binary catalog data.'
    )

    script_dir = os.path.dirname(__file__)
    config_path = os.path.join(script_dir, 'data_locations.txt')
    default_data = (
        read_data_location(config_path, 'CATALOG_BINARY')
        or read_data_location(config_path, 'GAIA_BINARY')
    )
    default_out = os.path.join(script_dir, 'data', 'catalog_minimap.png')

    parser.add_argument(
        '--input', '-i', default=default_data,
        help='Input .zst binary file or directory containing .zst files.'
    )
    parser.add_argument('--output', '-o', default=default_out, help='Output image path (PNG).')
    parser.add_argument('--width', type=int, default=8192,
                        help='Output image width in pixels.')
    parser.add_argument('--height', type=int, default=8192,
                        help='Output image height in pixels.')
    parser.add_argument('--extent-kpc', type=float, default=20.0,
                        help='Half-extent of the view in kpc (covers [-E,+E] in X and Y).')
    parser.add_argument('--r0-kpc', type=float, default=8.2,
                        help='Sun–Galactic Center distance (kpc).')
    parser.add_argument('--max-dist-kpc', type=float, default=0.0,
                        help='Optional max heliocentric distance to include (kpc). Set <=0 to disable.')
    parser.add_argument('--subsample', type=float, default=1.0,
                        help='Random fraction of rows to include (0<frac<=1). Use <1 to speed up.')
    parser.add_argument(
        '--brightness-mode', choices=['count', 'mag'], default='count',
        help='Pixel weight: count=1 per star, mag=weight by 10^(-0.4*(mag-10)).'
    )
    parser.add_argument('--gamma', type=float, default=0.5,
                        help='Gamma for tone mapping the histogram (after log scale).')
    parser.add_argument('--arm-radius-kpc', type=float, default=15.0,
                        help='Estimated spiral arm end radius. Draw markers at ±R along X (kpc).')
    parser.add_argument('--show-sun', action='store_true',
                        help='Draw a marker at the Sun position (−R0, 0).')
    parser.add_argument('--show-maxdist', action='store_true',
                        help='Draw a circle of radius max-dist (kpc) centered at the Sun.')
    parser.add_argument('--scale-bar-kpc', type=float, default=5.0,
                        help='Optional scale bar length (kpc). Set ≤0 to hide.')
    parser.add_argument('--progress-every', type=int, default=500_000,
                        help='Print a progress message every N parsed rows.')
    parser.add_argument(
        '--expected-magic',
        default='',
        help='Optional 4-character expected magic token (for example SDSS or STAR).'
    )
    parser.add_argument(
        '--strict-magic',
        action='store_true',
        help='Fail if header magic differs from --expected-magic.'
    )
    parser.add_argument(
        '--ls-per-pc',
        type=float,
        default=400.0,
        help='Conversion scale from light-seconds to parsec.'
    )

    args = parser.parse_args(argv)

    if not args.input:
        print('Error: --input is required when no default catalog path is configured.')
        return 2

    w, h = int(args.width), int(args.height)
    extent = float(args.extent_kpc)
    r0 = float(args.r0_kpc)
    max_dist_kpc = float(args.max_dist_kpc)
    subsample = float(args.subsample)

    if not (0.0 < subsample <= 1.0):
        print('Error: --subsample must be in (0,1].')
        return 2
    if len(args.expected_magic) not in (0, 4):
        print('Error: --expected-magic must be empty or exactly 4 ASCII characters.')
        return 2

    ls_per_pc = float(args.ls_per_pc)
    grid = np.zeros((h, w), dtype=np.float32)
    total = 0
    used = 0
    skipped = 0
    out_of_bounds = 0

    print(f"[info] Reading from: {safe_name(args.input)}")
    print(f"[info] Rendering {w}x{h} image, extent=±{extent} kpc, R0={r0} kpc")

    input_files = []
    if os.path.isfile(args.input):
        input_files = [args.input]
    elif os.path.isdir(args.input):
        for fname in sorted(os.listdir(args.input)):
            if fname.endswith('.zst'):
                input_files.append(os.path.join(args.input, fname))
        if not input_files:
            print('Error: No .zst files found in the input directory.')
            return 1
        print(f"[info] Found {len(input_files)} .zst file(s): {[safe_name(f) for f in input_files]}")
    else:
        print('Error: Input path does not exist.')
        return 1

    try:
        for file_idx, zst_file in enumerate(input_files, 1):
            print(f"[{file_idx}/{len(input_files)}] Processing {safe_name(zst_file)}...")

            for star in iter_catalog_zst(
                zst_file,
                subsample=subsample,
                expected_magic=args.expected_magic or None,
                strict_magic=args.strict_magic,
            ):
                total += 1
                x_ls = star['x']
                y_ls = star['y']
                z_ls = star['z']

                dist_ls = math.sqrt(x_ls**2 + y_ls**2 + z_ls**2)
                dist_kpc = dist_ls / ls_per_pc / 1000.0

                if max_dist_kpc > 0.0 and dist_kpc > max_dist_kpc:
                    skipped += 1
                    continue

                xh_kpc = x_ls / ls_per_pc / 1000.0
                yh_kpc = y_ls / ls_per_pc / 1000.0

                X = xh_kpc - r0
                Y = yh_kpc

                if X < -extent or X > extent or Y < -extent or Y > extent:
                    out_of_bounds += 1
                    continue

                u = (X + extent) / (2.0 * extent)
                v = (Y + extent) / (2.0 * extent)
                px = int(u * (w - 1))
                py = int((1.0 - v) * (h - 1))

                if px < 0 or px >= w or py < 0 or py >= h:
                    out_of_bounds += 1
                    continue

                if args.brightness_mode == 'mag':
                    gmag = star.get('gmag', 0.0)
                    if gmag > 0:
                        weight = 10.0 ** (-0.4 * (gmag - 10.0))
                    else:
                        weight = 1.0
                else:
                    weight = 1.0

                grid[py, px] += float(weight)
                used += 1

                if args.progress_every and (total % args.progress_every == 0):
                    print(f"[progress] read={total:,} used={used:,} skipped={skipped:,} oob={out_of_bounds:,}")

            print(f"[{file_idx}/{len(input_files)}] Completed {safe_name(zst_file)}")

    except FileNotFoundError:
        print('Error: Input file not found.')
        print('Check config defaults or specify --input explicitly.')
        return 1
    except Exception as e:
        print(f"Error reading data: {e}")
        return 1

    if used == 0:
        print('No records were plotted. Check input and filters.')
        return 1

    print(f"[render] Processing {used:,} records into image...")

    grid_log = np.log1p(grid)
    m = float(grid_log.max())
    if m <= 0:
        print('Histogram is empty after log transform.')
        return 1

    img_lin = (grid_log / m) ** max(1e-3, float(args.gamma))
    img_u8 = np.clip((img_lin * 255 + 0.5), 0, 255).astype(np.uint8)

    img = Image.fromarray(img_u8, mode='L')

    if ImageDraw is not None:
        draw = ImageDraw.Draw(img)
        cx, cy = (w // 2), (h // 2)

        cross = 8
        draw.line((cx - cross, cy, cx + cross, cy), fill=255)
        draw.line((cx, cy - cross, cx, cy + cross), fill=255)

        def to_px(x_kpc: float, y_kpc: float) -> tuple[int, int] | None:
            """Map kpc coordinates to image pixels."""
            if x_kpc < -extent or x_kpc > extent or y_kpc < -extent or y_kpc > extent:
                return None
            u = (x_kpc + extent) / (2.0 * extent)
            v = (y_kpc + extent) / (2.0 * extent)
            px = int(u * (w - 1))
            py = int((1.0 - v) * (h - 1))
            if px < 0 or px >= w or py < 0 or py >= h:
                return None
            return px, py

        r_arm = float(args.arm_radius_kpc)
        r_pix = 5
        for xk in (-r_arm, +r_arm):
            pt = to_px(xk, 0.0)
            if pt is not None:
                px, py = pt
                draw.ellipse((px - r_pix, py - r_pix, px + r_pix, py + r_pix), outline=255, width=2)

        if args.show_sun:
            sun_pt = to_px(-r0, 0.0)
            if sun_pt is not None:
                sx, sy = sun_pt
                draw.rectangle((sx - 3, sy - 3, sx + 3, sy + 3), outline=255, width=1)
                draw.line((sx - 5, sy, sx + 5, sy), fill=200)
                draw.line((sx, sy - 5, sx, sy + 5), fill=200)

        if args.show_maxdist and max_dist_kpc > 0.0:
            sun_pt = to_px(-r0, 0.0)
            if sun_pt is not None:
                sx, sy = sun_pt
                px_per_kpc = (w - 1) / (2.0 * extent)
                rp = max(1.0, float(max_dist_kpc) * px_per_kpc)
                draw.ellipse((sx - rp, sy - rp, sx + rp, sy + rp), outline=180, width=1)

        if args.scale_bar_kpc and args.scale_bar_kpc > 0:
            px_per_kpc = (w - 1) / (2.0 * extent)
            L = int(float(args.scale_bar_kpc) * px_per_kpc)
            if L >= 4:
                margin = 16
                x0 = margin
                y0 = h - margin
                draw.line((x0, y0, x0 + L, y0), fill=220, width=2)
                draw.line((x0, y0 - 4, x0, y0 + 4), fill=220, width=2)
                draw.line((x0 + L, y0 - 4, x0 + L, y0 + 4), fill=220, width=2)

    os.makedirs(os.path.dirname(args.output) or '.', exist_ok=True)
    img.save(args.output)

    print(f"Saved minimap: {safe_name(args.output)}")
    print(f"  size={w}x{h} | records plotted={used:,} of {total:,}")
    print(f"    skipped={skipped:,}, out_of_bounds={out_of_bounds:,}")
    print(f"    markers: GC, ±{args.arm_radius_kpc} kpc, "
          f"sun={'on' if args.show_sun else 'off'}, "
          f"maxdist={'on' if (args.show_maxdist and max_dist_kpc>0) else 'off'}")
    
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
