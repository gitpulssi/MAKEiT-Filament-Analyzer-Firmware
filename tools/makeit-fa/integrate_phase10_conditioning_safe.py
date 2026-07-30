#!/usr/bin/env python3
"""Run the Phase-10 conditioning installer with idempotent text matching.

The original stage writes aligned C++ such as::

    case CAM_CONDITIONING:  return "CONDITIONING";

Some of its rerun probes used a one-space spelling.  This wrapper keeps the
original patch logic but treats horizontal-whitespace-only differences and
CRLF/LF differences as equivalent, so a partially or fully integrated Marlin
tree can be processed repeatedly.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
ORIGINAL = HERE / "integrate_phase10_conditioning.py"

spec = importlib.util.spec_from_file_location("makeit_fa_phase10_conditioning", ORIGINAL)
if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load {ORIGINAL}")

stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


def normalized(text: str) -> str:
    """Normalize line endings and horizontal whitespace without joining lines."""
    return re.sub(r"[ \t]+", " ", text.replace("\r\n", "\n"))


def tolerant_ensure(path: str, needle: str, old: str, new: str) -> None:
    text = stage.read(path)
    if needle in text or normalized(needle) in normalized(text):
        print(f"ok {path}: already contains {needle!r} (format-tolerant)")
        return

    changed = text.replace(old, new, 1)
    if changed == text:
        raise RuntimeError(
            f"failed to patch {path}; missing pattern for {needle!r}"
        )
    stage.write(path, changed)


def verify_final_tree() -> None:
    required = {
        "Marlin/src/feature/makeit_fa_campaign.h": (
            "CAM_CONDITIONING",
            "float conditioning_mm;",
            "static bool start_conditioning();",
        ),
        "Marlin/src/feature/makeit_fa_campaign.cpp": (
            "case CAM_CONDITIONING:",
            "bool MakeItFASpeedCampaign::start_conditioning()",
            "float_bits(p.conditioning_mm)",
            "const bool started = params_.conditioning_mm",
        ),
        "Marlin/src/feature/makeit_fa_envelope.cpp": (
            "float_bits(p.speed.conditioning_mm)",
            "params_.speed.conditioning_mm",
        ),
        "Marlin/src/gcode/feature/M870.cpp": (
            "campaign.conditioning_mm = MakeItFASpeedCampaign::default_conditioning_mm();",
        ),
        "Marlin/src/gcode/feature/M872.cpp": (
            "campaign.conditioning_mm = MakeItFASpeedCampaign::default_conditioning_mm();",
        ),
    }

    for path, markers in required.items():
        text = stage.read(path)
        for marker in markers:
            if marker not in text:
                raise RuntimeError(f"Phase-10 verification failed: {path} missing {marker!r}")

    print("Phase-10 conditioning tree is complete and idempotent.")


def main() -> int:
    stage.ensure = tolerant_ensure
    result = stage.main()
    verify_final_tree()
    return int(result or 0)


if __name__ == "__main__":
    raise SystemExit(main())
