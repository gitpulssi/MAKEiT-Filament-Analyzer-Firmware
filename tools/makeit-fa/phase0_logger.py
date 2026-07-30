#!/usr/bin/env python3
"""MAKEiT Filament Analyzer Phase-0 telemetry logger.

Reads FA0 telemetry lines from the dedicated one-way UART and writes CSV.

Example:
    python3 tools/makeit-fa/phase0_logger.py --port /dev/ttyAMA2 --baud 250000 --csv encoder_cal.csv --echo
"""

from __future__ import annotations

import argparse
import csv
import sys
import time
from dataclasses import dataclass
from typing import Dict, Optional

try:
    import serial  # type: ignore
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial is required: pip install pyserial") from exc


@dataclass
class FA0Record:
    host_time_s: float
    seq: Optional[int]
    ms: Optional[int]
    enc: Optional[int]
    last_edge_us: Optional[int]
    mode: str
    raw: str


def parse_fa0(line: str) -> Optional[FA0Record]:
    line = line.strip()
    if not line.startswith("FA0,"):
        return None

    fields: Dict[str, str] = {}
    for item in line.split(",")[1:]:
        if "=" in item:
            key, value = item.split("=", 1)
            fields[key.strip()] = value.strip()

    def parse_int(name: str) -> Optional[int]:
        value = fields.get(name)
        if value is None or value == "":
            return None
        try:
            return int(value, 0)
        except ValueError:
            return None

    return FA0Record(
        host_time_s=time.time(),
        seq=parse_int("seq"),
        ms=parse_int("ms"),
        enc=parse_int("enc"),
        last_edge_us=parse_int("last_edge_us"),
        mode=fields.get("mode", ""),
        raw=line,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="MAKEiT FA0 telemetry logger")
    parser.add_argument("--port", required=True, help="Telemetry serial port, e.g. /dev/ttyAMA2")
    parser.add_argument("--baud", type=int, default=250000)
    parser.add_argument("--csv", required=True, help="Output CSV path")
    parser.add_argument("--echo", action="store_true", help="Echo parsed records to stdout")
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=1.0) as ser, open(args.csv, "w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["host_time_s", "seq", "ms", "enc", "last_edge_us", "mode", "raw"],
        )
        writer.writeheader()
        f.flush()

        while True:
            try:
                raw_bytes = ser.readline()
            except serial.SerialException as exc:
                print(f"serial error: {exc}", file=sys.stderr)
                return 2

            if not raw_bytes:
                continue

            line = raw_bytes.decode("ascii", errors="replace").strip()
            rec = parse_fa0(line)
            if rec is None:
                continue

            writer.writerow({
                "host_time_s": f"{rec.host_time_s:.6f}",
                "seq": rec.seq,
                "ms": rec.ms,
                "enc": rec.enc,
                "last_edge_us": rec.last_edge_us,
                "mode": rec.mode,
                "raw": rec.raw,
            })
            f.flush()

            if args.echo:
                print(rec.raw)


if __name__ == "__main__":
    raise SystemExit(main())
