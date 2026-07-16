#!/usr/bin/env python3
"""Integrate MAKEiT Filament Analyzer Phase-0 hooks into this Marlin tree.

Run from the repository root:

    python tools/makeit-fa/integrate_phase0.py

The script is idempotent. It inserts the Phase-0 hooks into the active Marlin
files so the firmware can build directly from this dedicated repository.
"""

from __future__ import annotations

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="surrogateescape")


def write(path: str, content: str) -> None:
    p = ROOT / path
    p.write_text(content, encoding="utf-8", errors="surrogateescape")
    print(f"updated {path}")


def ensure_contains(path: str, needle: str, edit) -> None:
    text = read(path)
    if needle in text:
        print(f"ok {path}: already contains {needle!r}")
        return
    new_text = edit(text)
    if new_text == text:
        raise RuntimeError(f"failed to patch {path}; pattern not found for {needle!r}")
    write(path, new_text)


def patch_configuration_h() -> None:
    path = "Marlin/Configuration.h"
    text = read(path)

    # Analyzer telemetry owns Serial3 directly. Disable only an active Marlin host SERIAL_PORT_3 assignment.
    # Match at the beginning of a line so an already-commented //#define line is not rewritten repeatedly.
    pattern = re.compile(r'^(\s*)#define\s+SERIAL_PORT_3\s+3\b.*$', re.MULTILINE)
    replacement = r'\1//#define SERIAL_PORT_3 3  // Disabled: MAKEiT filament analyzer owns Serial3 telemetry'
    new_text, count = pattern.subn(replacement, text, count=1)

    if count:
        write(path, new_text)
    elif "MAKEiT filament analyzer owns Serial3 telemetry" in text:
        print(f"ok {path}: SERIAL_PORT_3 already disabled for analyzer")
    else:
        print(f"note {path}: no active '#define SERIAL_PORT_3 3' line found")


def patch_configuration_adv_h() -> None:
    path = "Marlin/Configuration_adv.h"
    marker = "// MAKEiT Filament Analyzer Phase 0"
    block = r'''

// --------------------------------------------------------------------------
// MAKEiT Filament Analyzer Phase 0
// --------------------------------------------------------------------------
// First bench-test block: encoder calibration and raw one-way telemetry only.
#define MAKEIT_FILAMENT_ANALYZER_PHASE0

// Encoder signal connected to the existing filament runout input.
// On BTT SKR Pro V1.2, FIL_RUNOUT_PIN is PG5 in the MAKEiT pin config.
#define MAKEIT_FA_ENCODER_PIN            FIL_RUNOUT_PIN
#define MAKEIT_FA_ENCODER_PULLUP

// Record this exact trigger mode with calibration data.
#define MAKEIT_FA_ENCODER_INTERRUPT_MODE RISING
#define MAKEIT_FA_ENCODER_TRIGGER_NAME   "RISING"

// Dedicated one-way telemetry on the free TFT UART3 path.
// Wire SKR Pro TFT TX3 -> Raspberry Pi RX2, board GND -> Pi GND.
// Leave SKR Pro TFT RX3 / Pi TX2 disconnected for Phase 0.
#define MAKEIT_FA_TELEM_SERIAL           Serial3
#define MAKEIT_FA_TELEM_BAUD             250000
#define MAKEIT_FA_TELEM_INTERVAL_MS      200

// Optional encoder ISR marker pin for logic analyzer. Leave undefined if unused.
//#define MAKEIT_FA_MARKER_ENCODER_PIN     P1_01  // PLACEHOLDER - CHANGE THIS IF USED
'''

    ensure_contains(path, marker, lambda text: text.rstrip() + block + "\n")


def patch_marlin_core() -> None:
    path = "Marlin/src/MarlinCore.cpp"

    ensure_contains(
        path,
        'feature/makeit_filament_analyzer_phase0.h',
        lambda text: text.replace(
            '#include "MarlinCore.h"',
            '#include "MarlinCore.h"\n\n#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)\n  #include "feature/makeit_filament_analyzer_phase0.h"\n#endif',
            1,
        ),
    )

    ensure_contains(
        path,
        'makeit_fa_phase0.init();',
        lambda text: re.sub(
            r'(void\s+setup\s*\([^)]*\)\s*\{)',
            r'\1\n  #if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)\n    makeit_fa_phase0.init();\n  #endif',
            text,
            count=1,
        ),
    )

    ensure_contains(
        path,
        'makeit_fa_phase0.idle();',
        lambda text: re.sub(
            r'(void\s+idle\s*\([^)]*\)\s*\{)',
            r'\1\n  #if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)\n    makeit_fa_phase0.idle();\n  #endif',
            text,
            count=1,
        ),
    )


def patch_gcode_h() -> None:
    path = "Marlin/src/gcode/gcode.h"
    ensure_contains(
        path,
        'static void M875();',
        lambda text: text.replace(
            '  #if HAS_PTC\n    static void M871();\n  #endif',
            '  #if HAS_PTC\n    static void M871();\n  #endif\n\n  #if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)\n    static void M875();\n  #endif',
            1,
        ),
    )


def patch_gcode_cpp() -> None:
    path = "Marlin/src/gcode/gcode.cpp"
    ensure_contains(
        path,
        'case 875: M875(); break;',
        lambda text: text.replace(
            '      #if HAS_PTC\n        case 871: M871(); break;                                  // M871: Print/reset/clear first layer temperature offset values\n      #endif',
            '      #if HAS_PTC\n        case 871: M871(); break;                                  // M871: Print/reset/clear first layer temperature offset values\n      #endif\n\n      #if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)\n        case 875: M875(); break;                                  // M875: MAKEiT filament analyzer Phase-0 diagnostics\n      #endif',
            1,
        ),
    )


def main() -> int:
    patch_configuration_h()
    patch_configuration_adv_h()
    patch_marlin_core()
    patch_gcode_h()
    patch_gcode_cpp()
    print("Phase-0 analyzer integration hooks are in place.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
