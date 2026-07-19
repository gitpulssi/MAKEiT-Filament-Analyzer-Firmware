#!/usr/bin/env python3
"""Run all MAKEiT filament-analyzer integration stages in order."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent

for script in (
    "integrate_phase0.py",
    "integrate_phase7.py",
    "integrate_phase8.py",
    "integrate_phase8_hardening.py",
    "integrate_phase9.py",
    "integrate_phase10_conditioning.py",
):
    subprocess.check_call([sys.executable, str(HERE / script)])

print("All MAKEiT filament-analyzer integration stages are in place.")
