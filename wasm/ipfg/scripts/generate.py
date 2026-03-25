#!/usr/bin/env python3
# scripts/generate_lookup.py
# --------------------------------------------------------------
# Generates ip_ranges.bin and ip_strings.bin from DB‑IP MMDB.
# --------------------------------------------------------------
import os
import sys
import gzip
import struct
import maxminddb  # pip install maxminddb

MMDB_URL = (
    "https://download.db-ip.com/free/dbip-city-lite-2026-03.mmdb.gz"
)
MMDB_GZ = "dbip-city-lite-2026-03.mmdb.gz"
MMDB_PATH = "dbip-city-lite-2026-03.mmdb"
RANGES_OUT = "../data/ip_ranges.bin"
STRINGS_OUT = "../data/ip_strings.bin"


def download_if_needed():
    if not os.path.exists(MMDB_PATH):
        if not os.path.exists(MMDB_GZ):
            print(f"Downloading {MMDB_URL} ...", file=sys.stderr)
            import urllib.request
            urllib.request.urlretrieve(MMDB_URL, MMDB_GZ)
        print("Decompressing ...", file=sys.stderr)
        with gzip.open(MMDB_GZ, "rb") as f_in, open(MMDB_PATH, "wb") as f_out:
            f_out.write(f_in.read())
    else:
        print("MMDB already present.", file=sys.stderr)


def ip_to_int(ip_str):
    parts = list(map(int, ip_str.split(".")))
    return (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]


def int_to_ip(num):
    return f"{(num >> 24) & 0xFF}.{(num >> 16) & 0xFF}.{(num >> 8) & 0xFF}.{num & 0xFF}"


def main():
    download_if_needed()

    reader = maxminddb.open_database(MMDB_PATH)

    # Collect raw entries: (start_int, end_int, country, city)
    raw = []
    for network, record in reader:
        # network is an ipaddress.IPv4Network object
        if not network.version == 4:
            continue
        start = int(network.network_address)
        # broadcast address = last address in the subnet
        end = start + (1 << (32 - network.prefixlen)) - 1

        country = record.get("country", {}).get("iso_code", "")
        city = record.get("city", {}).get("names", {}).get("en", "")
        if not country:
            country = "ZZ"
        if not city:
            city = "Unknown"
        raw.append((start, end, country, city))

    reader.close()

    # -----------------------------------------------------------------
    # Merge overlapping/adjacent ranges and sort by start.
    # -----------------------------------------------------------------
    raw.sort(key=lambda x: x[0])
    merged = []
    for s, e, c, ct in raw:
        if not merged:
            merged.append([s, e, c, ct])
            continue
        last = merged[-1]

        if s <= last[1] + 1 and last[2] == c and last[3] == ct:
            # same country/city and contiguous/overlapping → extend
            if e > last[1]:
                last[1] = e
        else:
            merged.append([s, e, c, ct])

    # -----------------------------------------------------------------
    # Build string table (deduplicated)
    # -----------------------------------------------------------------
    string_set = set()
    for _, _, c, ct in merged:
        string_set.add(c)
        string_set.add(ct)
    string_set.add("ZZ")      # ensure fallback present
    string_set.add("Unknown")
    strings = sorted(string_set)   # deterministic order    str_to_idx = {s: i for i, s in enumerate(strings)}

    # -----------------------------------------------------------------
    # Write binary blobs
    # ---------------------------------------------------------------
    # 1) ip_ranges.bin : array of Range {start,end,country_offset,city_offset}
    # 2) ip_strings.bin: concatenated NUL‑terminated strings    # -----------------------------------------------------------------
    os.makedirs(os.path.dirname(RANGES_OUT), exist_ok=True)
    os.makedirs(os.path.dirname(STRINGS_OUT), exist_ok=True)

    with open(RANGES_OUT, "wb") as f_range, open(STRINGS_OUT, "wb") as f_str:
        # Write strings first so we can compute offsets
        offsets = {}
        for s in strings:
            offsets[s] = f_str.tell()
            f_str.write(s.encode("utf-8") + b"\x00")

        # Write ranges
        for s, e, c, ct in merged:
            rec = struct.pack(
                "<IIII",  # little‑endian uint32 ×4
                s,
                e,
                offsets[c],
                offsets[ct],
            )
            f_range.write(rec)

    print(
        f"Generated {len(merged)} ranges and {len(strings)} strings.",
        file=sys.stderr,
    )
    print(f"Ranges → {RANGES_OUT}", file=sys.stderr)
    print(f"Strings → {STRINGS_OUT}", file=sys.stderr)


if __name__ == "__main__":
    main()
