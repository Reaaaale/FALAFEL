#!/usr/bin/env python3
"""Prepare an OFF-REPOSITORY lab NVS image; never flash or change source stores."""
import argparse
import csv
import hashlib
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]


def outside_repo(path):
    resolved = Path(path).expanduser().resolve()
    if resolved == REPO or REPO in resolved.parents:
        raise ValueError("Credential inputs/outputs must be outside the repository")
    return resolved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--identity", required=True, help="C6's 64-byte identity, NOT simulator's identity")
    parser.add_argument("--own", type=int, required=True)
    parser.add_argument("--peer", type=int, required=True)
    parser.add_argument("--output-dir", required=True, help="NEW private directory outside repository")
    parser.add_argument("--idf", default=str(REPO / ".cache/sec002/esp-idf-v5.5.6"))
    args = parser.parse_args()
    if not (0 < args.own < 0xffffffff and 0 < args.peer < 0xffffffff and args.own != args.peer):
        parser.error("Own/peer IDs must be positive, distinct and below UINT32_MAX")
    os.umask(0o077)
    source = outside_repo(args.identity)
    metadata = source.stat()
    if not stat.S_ISREG(metadata.st_mode) or metadata.st_size != 64 or metadata.st_mode & 0o077 or metadata.st_uid != os.geteuid():
        raise ValueError("Identity must be an owner-owned private regular 64-byte file")
    output = outside_repo(args.output_dir)
    output.mkdir(mode=0o700, parents=False, exist_ok=False)
    # Only the generator/import process reads key bytes; never print them or
    # put them in arguments, defaults, C arrays, source trees or manifests.
    body = b"FCI1" + struct.pack(">II", args.own, args.peer) + source.read_bytes()
    record = output / "identity-record.bin"
    record.write_bytes(body + hashlib.sha256(body).digest())
    table = output / "identity.csv"
    with table.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["key", "type", "encoding", "value"])
        writer.writerow(["falafel_id", "namespace", "", ""])
        writer.writerow(["identity", "file", "binary", str(record)])
    generator = Path(args.idf) / "components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
    # No key material in captured diagnostics; suppress generator output too.
    result = subprocess.run([sys.executable, str(generator), "generate", str(table),
                             str(output / "falafel-data.bin"), "0x100000"], capture_output=True)
    if result.returncode:
        raise RuntimeError("NVS generator failed; check the selected isolated IDF Python environment")
    print("Prepared private lab image:", output / "falafel-data.bin")
    print("Initial provisioning ONLY: writing at 0x100000 replaces the entire falafel_data partition.")
    print("Do not provision again to retry/reboot; that would discard delivery/deduplication state.")
    print("Simulator identity, mailbox and history were not modified. No device was flashed.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, RuntimeError):
        print("Provisioning image not completed; verify private paths, permissions and IDF environment.", file=sys.stderr)
        sys.exit(1)
