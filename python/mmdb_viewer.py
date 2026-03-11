#!/usr/bin/env python3
"""
MaxMind MMDB Viewer - Pure Python, no dependencies
Outputs ONLY valid JSON containing both metadata and lookup result (if IP provided)

Usage:
    python mmdb_viewer.py <database.mmdb>                # metadata only
    python mmdb_viewer.py <database.mmdb> 8.8.8.8        # metadata + lookup
"""

import json
import socket
import struct
import sys
from typing import Any, Dict, Tuple, Optional


class MMDBViewer:
    def __init__(self, filename: str):
        with open(filename, "rb") as f:
            self._db: bytes = f.read()

        self._metadata_start = self._find_metadata_marker()
        self._metadata, _ = self._decode(self._metadata_start, pointer_base=self._metadata_start)

        self._node_count: int = self._metadata["node_count"]
        self._record_size: int = self._metadata["record_size"]
        self._ip_version: int = self._metadata["ip_version"]

        self._node_byte_size = (self._record_size * 2) // 8
        self._search_tree_size = self._node_byte_size * self._node_count
        self._data_section_start = self._search_tree_size + 16

    def _find_metadata_marker(self) -> int:
        marker = b"\xab\xcd\xefMaxMind.com"
        pos = self._db.rfind(marker)
        if pos == -1:
            raise ValueError("Invalid MMDB file: metadata marker not found")
        return pos + len(marker)

    def _decode(self, offset: int, pointer_base: int) -> Tuple[Any, int]:
        control_byte = self._db[offset]
        offset += 1

        type_code = control_byte >> 5
        size_bits = control_byte & 0x1F

        if type_code == 0:  # extended
            ext_type = self._db[offset]
            offset += 1
            type_code = ext_type + 7

        if type_code == 1:  # pointer
            pointer_size = (control_byte >> 3) & 0b11
            pointer_value_bits = control_byte & 0b00000111

            if pointer_size == 0:
                next_byte = self._db[offset]
                pointer_value = (pointer_value_bits << 8) | next_byte
                offset += 1
            elif pointer_size == 1:
                extra = struct.unpack_from(">H", self._db, offset)[0]
                pointer_value = (pointer_value_bits << 16) | extra
                pointer_value += 2048
                offset += 2
            elif pointer_size == 2:
                extra = int.from_bytes(self._db[offset:offset+3], "big")
                pointer_value = (pointer_value_bits << 24) | extra
                pointer_value += 526336
                offset += 3
            else:
                pointer_value = struct.unpack_from(">I", self._db, offset)[0]
                offset += 4

            pointed_offset = pointer_base + pointer_value
            value, _ = self._decode(pointed_offset, pointer_base)
            return value, offset

        # Size calculation
        if size_bits < 29:
            payload_size = size_bits
        elif size_bits == 29:
            payload_size = 29 + self._db[offset]
            offset += 1
        elif size_bits == 30:
            payload_size = 285 + struct.unpack_from(">H", self._db, offset)[0]
            offset += 2
        else:
            payload_size = 65821 + int.from_bytes(self._db[offset:offset+3], "big")
            offset += 3

        if type_code == 2:   # UTF-8 string
            value = self._db[offset:offset + payload_size].decode("utf-8") if payload_size > 0 else ""
            offset += payload_size
            return value, offset

        if type_code == 3:   # double
            value = struct.unpack_from(">d", self._db, offset)[0]
            offset += 8
            return value, offset

        if type_code == 4:   # bytes
            value = self._db[offset:offset + payload_size]
            offset += payload_size
            return value, offset

        if type_code in (5, 6, 9, 10):  # uint16/32/64/128
            value = int.from_bytes(self._db[offset:offset + payload_size], "big") if payload_size > 0 else 0
            offset += payload_size
            return value, offset

        if type_code == 8:   # int32 (signed)
            b = self._db[offset:offset + payload_size]
            if payload_size == 0:
                value = 0
            elif payload_size < 4:
                value = int.from_bytes(b, "big")
            else:
                value = int.from_bytes(b, "big", signed=True)
            offset += payload_size
            return value, offset

        if type_code == 7:   # map
            value: Dict[str, Any] = {}
            count = payload_size
            for _ in range(count):
                key, offset = self._decode(offset, pointer_base)
                val, offset = self._decode(offset, pointer_base)
                if isinstance(key, str):
                    value[key] = val
            return value, offset

        if type_code == 11:  # array
            value = []
            for _ in range(payload_size):
                item, offset = self._decode(offset, pointer_base)
                value.append(item)
            return value, offset

        if type_code == 14:  # boolean
            return bool(size_bits), offset

        if type_code == 15:  # float
            value = struct.unpack_from(">f", self._db, offset)[0]
            offset += 4
            return value, offset

        if type_code in (12, 13):
            raise NotImplementedError(f"Deprecated type {type_code}")

        raise ValueError(f"Unknown type code: {type_code}")

    def _read_node(self, node_num: int) -> Tuple[int, int]:
        offset = node_num * self._node_byte_size
        rs = self._record_size

        if rs == 24:
            left = struct.unpack_from(">I", self._db, offset)[0] >> 8
            right = struct.unpack_from(">I", self._db, offset + 3)[0] >> 8
        elif rs == 32:
            left, right = struct.unpack_from(">II", self._db, offset)
        elif rs == 28:
            left24 = struct.unpack_from(">I", self._db, offset)[0] >> 8
            right24 = struct.unpack_from(">I", self._db, offset + 4)[0] >> 8
            middle = self._db[offset + 3]
            left = ((middle >> 4) << 24) | left24
            right = ((middle & 0x0F) << 24) | right24
        else:
            raise NotImplementedError(f"Unsupported record size: {rs}")

        return left, right

    def lookup(self, ip_address: str) -> Optional[Any]:
        if ":" in ip_address:
            if self._ip_version == 4:
                raise ValueError("IPv6 address given but database is IPv4-only")
            try:
                ip_bytes = socket.inet_pton(socket.AF_INET6, ip_address)
                bit_length = 128
            except OSError:
                raise ValueError(f"Invalid IPv6 address: {ip_address}")
        else:
            try:
                ip_bytes = socket.inet_pton(socket.AF_INET, ip_address)
                bit_length = 32
            except OSError:
                raise ValueError(f"Invalid IPv4 address: {ip_address}")

            if self._ip_version == 6:
                ip_bytes = b"\x00" * 12 + ip_bytes
                bit_length = 128

        node = 0
        for i in range(bit_length):
            byte_idx = i // 8
            bit_idx = 7 - (i % 8)
            bit = (ip_bytes[byte_idx] >> bit_idx) & 1

            left, right = self._read_node(node)
            record = left if bit == 0 else right

            if record < self._node_count:
                node = record
            elif record == self._node_count:
                return None
            else:
                data_offset = self._search_tree_size + (record - self._node_count)
                value, _ = self._decode(data_offset, pointer_base=self._data_section_start)
                return value

        return None


def main():
    if len(sys.argv) < 2:
        print(json.dumps({"error": "Usage: mmdb_viewer.py <database.mmdb> [IP]"}, indent=2))
        sys.exit(1)

    db_path = sys.argv[1]
    ip = sys.argv[2] if len(sys.argv) > 2 else None

    try:
        viewer = MMDBViewer(db_path)
    except Exception as e:
        print(json.dumps({"error": f"Failed to load database: {str(e)}"}, indent=2))
        sys.exit(1)

    result = {
        "metadata": {
            "database_type": viewer._metadata.get("database_type"),
            "ip_version": viewer._metadata.get("ip_version"),
            "node_count": viewer._metadata.get("node_count"),
            "record_size": viewer._metadata.get("record_size"),
            "binary_format": f"{viewer._metadata.get('binary_format_major_version', 2)}.{viewer._metadata.get('binary_format_minor_version', 0)}",
            "build_epoch": viewer._metadata.get("build_epoch"),
            "languages": viewer._metadata.get("languages", []),
            "description": viewer._metadata.get("description", {}),
        },
        "lookup": None,
        "lookup_ip": ip
    }

    if ip:
        try:
            result["lookup"] = viewer.lookup(ip)
        except Exception as e:
            result["lookup"] = {"error": str(e)}

    # Output pure JSON — nothing else
    print(json.dumps(result, indent=2, ensure_ascii=False, default=str))


if __name__ == "__main__":
    main()
