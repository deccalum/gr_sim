#!/usr/bin/env python3
"""
Fetch SDSS DR17 star photometry via SciServer CasJobs and write
compressed binary (.zst) in one pass.

Record layout (64 bytes per star): '<ddffffffffffQ'
- ra_deg (float64)
- dec_deg (float64)
- u,g,r,i,z (float32)
- err_u,err_g,err_r,err_i,err_z (float32)
- objID (uint64)

Header:
- magic: b'SDSS' (4 bytes)
- version: uint32 (1)
- record_count: uint32 (filled at finalize)

Notes:
- Requires SciScript-Python cloned (py3 path) and zstandard, pandas.
- Authentication uses SciServer account. Supply via --user/--password or env vars
  SCI_USERNAME / SCI_PASSWORD.
"""

from __future__ import annotations

import argparse
import getpass
import importlib
import os
import sys
import struct
from typing import Optional
import json
from pathlib import Path
import time

import pandas as pd
import zstandard as zstd

RECORD_FMT = '<ddffffffffffQ'
RECORD_SIZE = struct.calcsize(RECORD_FMT)


def safe_name(path: str) -> str:
    """Return a non-sensitive display name for logs."""
    name = os.path.basename(path.rstrip('/\\'))
    return name if name else '<path>'


def load_sciserver(sciserver_path: Optional[str]):
    """Load SciServer Authentication, CasJobs, and Config modules."""
    if sciserver_path:
        p = Path(sciserver_path)
        if not p.exists():
            print('Error: Provided --sciserver-path does not exist.')
            sys.exit(2)
        sys.path.insert(0, str(p))

    try:
        module = importlib.import_module('SciServer')
        authentication = module.Authentication
        casjobs = module.CasJobs
        config = module.Config
    except Exception:
        print('Error: SciServer module is not available.')
        print('Install SciServer in the active environment or pass --sciserver-path.')
        sys.exit(2)

    if sciserver_path:
        print('[info] Using custom SciServer module path.')

    return authentication, casjobs, config


def login(authentication, user: Optional[str], password: Optional[str]) -> str:
    """Authenticate against SciServer and return a token string."""
    user = user or os.environ.get('SCI_USERNAME')
    password = password or os.environ.get('SCI_PASSWORD')

    if not user:
        user = input('SciServer email/username: ').strip()
    if not password:
        password = getpass.getpass('SciServer password: ')

    if not user or not password:
        print('Error: SciServer credentials required.')
        sys.exit(2)

    try:
        token = authentication.login(user, password)
        print('Successfully authenticated to SciServer.')
        return token
    except Exception as e:
        print(f"Error: Authentication failed: {e}")
        print('Make sure credentials are correct and network access is available.')
        sys.exit(2)


def fetch_chunk(casjobs, sql: str, dr: str, use_batch: bool = False) -> pd.DataFrame:
    """Execute SQL directly or submit batch job and return a DataFrame handle."""
    try:
        if use_batch:
            print('[info] Submitting batch job to CasJobs (saving to MyDB)...')
            table_name = 'sdss_temp_' + str(os.getpid())
            sql_into = sql.replace('SELECT', f'SELECT INTO MyDB..{table_name}', 1)
            job_id = casjobs.submitJob(sql_into, context=dr)
            print(f'[info] Job submitted (ID: {job_id}). Waiting for completion...')

            while True:
                status = casjobs.getJobStatus(job_id)
                status_code = status if isinstance(status, int) else status.get('Status', -1)
                if status_code in [3, 4]:
                    print('Job completed successfully.')
                    break
                elif status_code == 5:
                    print('Error: Job failed or was cancelled.')
                    if isinstance(status, dict) and status.get('Message'):
                        print(f"Details: {status.get('Message')}")
                    sys.exit(1)
                else:
                    print(f'[info] Job status: {status}. Waiting 30s...')
                    time.sleep(30)

            return pd.DataFrame({"__table__": [table_name]})

        df = casjobs.executeQuery(sql, context=dr, format='pandas')
        return df
    except Exception as e:
        print(f"Error: CasJobs query failed: {e}")
        msg = str(e).lower()
        if "queue time" in msg or "timeout" in msg or "internalservererror" in msg:
            print('Tip: For large queries, use --batch mode.')
        sys.exit(1)


def write_zst(output_path: str, df: pd.DataFrame) -> int:
    """Write a DataFrame to SDSS binary zstd format and return record count."""
    if zstd is None:
        print('Error: zstandard is required (pip install zstandard).')
        sys.exit(2)
    count = 0
    with open(output_path, 'wb') as f:
        f.write(b'SDSS')
        f.write(struct.pack('<I', 1))
        f.write(struct.pack('<I', 0))
        cctx = zstd.ZstdCompressor(level=3)
        for _, row in df.iterrows():
            try:
                ra = float(row['ra'])
                dec = float(row['dec'])
                u = float(row['u']) if pd.notna(row['u']) else float('nan')
                g = float(row['g']) if pd.notna(row['g']) else float('nan')
                r = float(row['r']) if pd.notna(row['r']) else float('nan')
                i = float(row['i']) if pd.notna(row['i']) else float('nan')
                z = float(row['z']) if pd.notna(row['z']) else float('nan')
                eu = float(row['err_u']) if pd.notna(row['err_u']) else float('nan')
                eg = float(row['err_g']) if pd.notna(row['err_g']) else float('nan')
                er = float(row['err_r']) if pd.notna(row['err_r']) else float('nan')
                ei = float(row['err_i']) if pd.notna(row['err_i']) else float('nan')
                ez = float(row['err_z']) if pd.notna(row['err_z']) else float('nan')
                objid = int(row['objID'])
                rec = struct.pack(RECORD_FMT, ra, dec, u, g, r, i, z, eu, eg, er, ei, ez, objid)
                comp = cctx.compress(rec)
                f.write(comp)
                count += 1
            except Exception:
                continue
        f.seek(8)
        f.write(struct.pack('<I', count))
    return count


def read_table_chunk(casjobs, table: str, last_objid: Optional[int], chunk_size: int, max_retries: int = 5) -> pd.DataFrame:
    """Read a chunk from MyDB table with retry logic for transient network errors."""
    where = "" if last_objid is None else f"WHERE objID > {int(last_objid)}"
    sql = f"SELECT TOP {int(chunk_size)} objID, ra, dec, u, g, r, i, z, err_u, err_g, err_r, err_i, err_z FROM {table} {where} ORDER BY objID"

    for attempt in range(max_retries + 1):
        try:
            return casjobs.executeQuery(sql, context='MyDB', format='pandas')
        except Exception as e:
            if any(x in str(e).lower() for x in ["prematurely", "chunked", "connection", "timeout"]):
                if attempt < max_retries:
                    wait_time = 2 ** attempt
                    print(f"[warn] Network error reading chunk (attempt {attempt + 1}/{max_retries + 1}): {type(e).__name__}")
                    print(f"[info] Retrying in {wait_time} seconds...")
                    time.sleep(wait_time)
                    continue
            raise


def append_records(output_path: str, df: pd.DataFrame, initial: bool, current_count: int) -> int:
    """Append DataFrame rows to output zstd stream and patch running count."""
    if zstd is None:
        print('Error: zstandard is required (pip install zstandard).')
        sys.exit(2)
    mode = 'r+b' if Path(output_path).exists() else 'wb'
    with open(output_path, mode) as f:
        if mode == 'wb' and initial:
            f.write(b'SDSS')
            f.write(struct.pack('<I', 1))
            f.write(struct.pack('<I', 0))
        else:
            f.seek(0, os.SEEK_END)
        cctx = zstd.ZstdCompressor(level=3)
        written = 0
        for _, row in df.iterrows():
            try:
                ra = float(row['ra'])
                dec = float(row['dec'])
                u = float(row['u']) if pd.notna(row['u']) else float('nan')
                g = float(row['g']) if pd.notna(row['g']) else float('nan')
                r = float(row['r']) if pd.notna(row['r']) else float('nan')
                i = float(row['i']) if pd.notna(row['i']) else float('nan')
                z = float(row['z']) if pd.notna(row['z']) else float('nan')
                eu = float(row['err_u']) if pd.notna(row['err_u']) else float('nan')
                eg = float(row['err_g']) if pd.notna(row['err_g']) else float('nan')
                er = float(row['err_r']) if pd.notna(row['err_r']) else float('nan')
                ei = float(row['err_i']) if pd.notna(row['err_i']) else float('nan')
                ez = float(row['err_z']) if pd.notna(row['err_z']) else float('nan')
                objid = int(row['objID'])
                rec = struct.pack(RECORD_FMT, ra, dec, u, g, r, i, z, eu, eg, er, ei, ez, objid)
                comp = cctx.compress(rec)
                f.write(comp)
                written += 1
            except Exception:
                continue
        f.seek(8)
        f.write(struct.pack('<I', current_count + written))
        return written


def build_sql(limit: Optional[int], where: Optional[str]) -> str:
    """Build the SDSS PhotoObj query with optional TOP and extra filter."""
    base_where = "p.type = 6"
    if where:
        full_where = base_where + " AND (" + where + ")"
    else:
        full_where = base_where

    top_clause = f"TOP {limit}" if limit and limit > 0 else ""

    sql = f"""
    SELECT {top_clause}
      p.objID, p.ra, p.dec,
      p.u, p.g, p.r, p.i, p.z,
      p.err_u, p.err_g, p.err_r, p.err_i, p.err_z,
      p.type
    FROM PhotoObj p
    WHERE {full_where}
    """
    return sql


def read_data_location() -> Optional[str]:
    """Read a default output data directory from local config file."""
    config_path = os.path.join(os.path.dirname(__file__), '.zst data.txt')
    if not os.path.exists(config_path):
        return None
    try:
        with open(config_path, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                if '=' not in line and '"' in line:
                    path = line.strip('"\'')
                    if os.path.exists(path):
                        return path
    except Exception:
        pass
    return None


def main(argv: Optional[list[str]] = None) -> int:
    """CLI entry point for SDSS CasJobs fetch and zstd export."""
    ap = argparse.ArgumentParser(description='Fetch SDSS DR17 stars via CasJobs and write .zst binary.')
    ap.add_argument('--user', help='SciServer email (optional; will prompt if not provided)')
    ap.add_argument('--password', help='SciServer password (optional; will prompt if not provided)')
    ap.add_argument('--limit', type=int, default=0, help='Max rows to fetch (default: 0 = all rows, WARNING: can be slow for entire catalog).')
    ap.add_argument('--where', help='Optional SQL WHERE clause (without WHERE keyword).')
    ap.add_argument('--dr', default='dr17', help='SDSS data release (default: dr17).')
    ap.add_argument('--batch', action='store_true', help='Use batch job mode for large queries (required for >100k rows).')
    ap.add_argument('--resume', action='store_true', help='Resume from previous batch state (uses state file).')
    ap.add_argument('--chunk-size', type=int, default=200000, help='Rows per chunk when reading from MyDB in batch mode.')
    ap.add_argument('--max-retries', type=int, default=5, help='Max retries on transient network errors (default: 5).')
    ap.add_argument('--sciserver-path', help='Optional path to SciScript-Python py3 folder if not installed.')
    ap.add_argument('--show-types', action='store_true', help='Query and display PhotoObj type distribution, then exit.')

    data_dir = read_data_location()
    default_out = os.path.join(data_dir, 'sdss_stars.zst') if data_dir else 'sdss_stars.zst'

    ap.add_argument('--output', '-o', default=default_out, help='Output .zst file path.')
    args = ap.parse_args(argv)
    authentication, casjobs, _ = load_sciserver(args.sciserver_path)

    if args.show_types:
        try:
            sql = "SELECT type, COUNT(*) as cnt FROM PhotoObj GROUP BY type ORDER BY type"
            df = casjobs.executeQuery(sql, context=args.dr, format='pandas')
            print("\n=== SDSS PhotoObj Type Distribution (DR17) ===")
            print(df.to_string(index=False))
            print("\nType Legend:")
            print("  0 = Unknown")
            print("  1 = Cosmic ray")
            print("  2 = Defect")
            print("  3 = Galaxy")
            print("  4 = Ghost")
            print("  5 = Knot")
            print("  6 = Star")
            print("  7 = Trail")
            print("  8 = Spurious")
            print("\nPoint sources for Milky Way: types 2–6 (star-like)")
            print("Safe conservative filter: type = 6 (classified stars only)")
            return 0
        except Exception as e:
            print(f"Error querying types: {e}")
            sys.exit(1)

    _token = login(authentication, args.user, args.password)

    state_path = os.path.join(os.path.dirname(args.output) or '.', 'sdss_fetch_state.json')

    if args.batch:
        os.makedirs(os.path.dirname(args.output) or '.', exist_ok=True)
        state = {}
        if args.resume and os.path.exists(state_path):
            with open(state_path, 'r', encoding='utf-8') as sf:
                state = json.load(sf)
            table_name = state.get('table_name')
            last_objid = state.get('last_objid')
            current_count = int(state.get('count_written', 0))
            print(f"[info] Resuming from state: table={table_name}, last_objid={last_objid}, count={current_count}")
        else:
            sql = build_sql(args.limit, args.where)
            print('[info] Executing CasJobs batch query...')
            df_table = fetch_chunk(casjobs, sql, args.dr, use_batch=True)
            table_name = df_table['__table__'].iloc[0]
            last_objid = None
            current_count = 0
            state = {'table_name': table_name, 'last_objid': None, 'count_written': 0}
            with open(state_path, 'w', encoding='utf-8') as sf:
                json.dump(state, sf)

        initial = current_count == 0 and not Path(args.output).exists()
        while True:
            chunk = read_table_chunk(casjobs, table_name, last_objid, args.chunk_size, max_retries=args.max_retries)
            if chunk is None or len(chunk) == 0:
                print('[info] No more rows to read. Finalizing...')
                break
            written = append_records(args.output, chunk, initial=initial, current_count=current_count)
            current_count += written
            last_objid = int(chunk['objID'].iloc[-1])
            initial = False
            state.update({'table_name': table_name, 'last_objid': last_objid, 'count_written': current_count})
            with open(state_path, 'w', encoding='utf-8') as sf:
                json.dump(state, sf)
            print(f"[info] Wrote chunk of {written:,} rows (total {current_count:,}).")

        try:
            casjobs.executeQuery(f"DROP TABLE {table_name}", context='MyDB')
        except Exception:
            pass
        try:
            os.remove(state_path)
        except Exception:
            pass
        print(f"[ok] Wrote {current_count:,} SDSS records to {safe_name(args.output)}")
        return 0

    else:
        sql = build_sql(args.limit, args.where)
        print('[info] Executing CasJobs query...')
        df = fetch_chunk(casjobs, sql, args.dr, use_batch=False)
        print(f"[info] Retrieved {len(df):,} rows.")
        os.makedirs(os.path.dirname(args.output) or '.', exist_ok=True)
        count = write_zst(args.output, df)
        print(f"[ok] Wrote {count:,} SDSS records to {safe_name(args.output)}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
