#!/usr/bin/env python3
"""Compute events/mm and beat numbers from physical-mark calibration runs."""

from __future__ import annotations

import argparse
import csv
import math
from statistics import mean, stdev


def flow_to_filament_speed(flow_mm3_s: float, diameter_mm: float = 1.75) -> float:
    area = math.pi * (diameter_mm ** 2) / 4.0
    return flow_mm3_s / area


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("csv", help="CSV with columns physical_length_mm, encoder_events")
    p.add_argument("--segment-mm", type=float, default=0.35)
    p.add_argument("--diameter-mm", type=float, default=1.75)
    args = p.parse_args()

    values = []
    with open(args.csv, newline="") as f:
        for row in csv.DictReader(f):
            length = float(row["physical_length_mm"])
            events = float(row["encoder_events"])
            if length <= 0:
                raise ValueError("physical_length_mm must be positive")
            values.append(events / length)

    if not values:
        raise SystemExit("No rows found")

    epm = mean(values)
    epm_sd = stdev(values) if len(values) > 1 else 0.0
    l_event = 1.0 / epm
    r = l_event / args.segment_mm
    nearest = round(r)
    delta = abs(r - nearest)
    n_beat = math.inf if delta == 0 else 1.0 / delta

    print(f"runs={len(values)}")
    print(f"events_per_mm_mean={epm:.6f}")
    print(f"events_per_mm_sd={epm_sd:.6f}")
    print(f"L_event_mm={l_event:.6f}")
    print(f"segment_mm={args.segment_mm:.6f}")
    print(f"R=L_event/segment={r:.6f}")
    print(f"nearest_integer={nearest}")
    print(f"delta={delta:.6f}")
    print(f"N_beat_events={n_beat:.3f}")

    for q in (1.5, 2.0, 3.0, 5.0):
        vf = flow_to_filament_speed(q, args.diameter_mm)
        t_beat = math.inf if math.isinf(n_beat) else (n_beat * l_event / vf)
        print(f"T_beat_s_at_{q:.1f}_mm3s={t_beat:.2f}")

    if math.isinf(n_beat) or n_beat > 25:
        print("recommendation=consider shrinking segment length to 0.33 mm and recompute")
    else:
        print("recommendation=0.35 mm likely acceptable for Phase-0 trace")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
