#!/usr/bin/env python3
"""Integrate MAKEiT Filament Analyzer Phase-0/1/2/3/4/5/6 hooks into Marlin.

Run from the repository root:

    python tools/makeit-fa/integrate_phase0.py

The script is idempotent. It inserts the analyzer hooks into the active Marlin
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

    disabled_line = "//#define SERIAL_PORT_3 3  // Disabled: MAKEiT filament analyzer owns Serial3 telemetry"
    if disabled_line in text:
        print(f"ok {path}: SERIAL_PORT_3 already disabled for analyzer")
    elif re.search(r'^\s*#define\s+SERIAL_PORT_3\s+3\b', text, re.MULTILINE):
        text = re.sub(
            r'^\s*#define\s+SERIAL_PORT_3\s+3\b.*$',
            disabled_line,
            text,
            count=1,
            flags=re.MULTILINE,
        )
        write(path, text)
    else:
        print(f"note {path}: no active '#define SERIAL_PORT_3 3' line found")


def patch_configuration_adv_h() -> None:
    path = "Marlin/Configuration_adv.h"
    marker = "// MAKEiT Filament Analyzer Phase 0"
    block = r'''

// --------------------------------------------------------------------------
// MAKEiT Filament Analyzer Phase 0
// --------------------------------------------------------------------------
// Encoder calibration, segmented motion, evaluated points, transactions, and
// M879 emergency-parser graceful abort.
#define MAKEIT_FILAMENT_ANALYZER_PHASE0

// Encoder signal connected to SKR Pro filament runout / E2 DIAG area.
#define MAKEIT_FA_ENCODER_PIN            PG5
#define MAKEIT_FA_ENCODER_PULLUP

#define MAKEIT_FA_ENCODER_INTERRUPT_MODE RISING
#define MAKEIT_FA_ENCODER_TRIGGER_NAME   "RISING"
#define MAKEIT_FA_ENCODER_USE_POLLING    1

// Dedicated one-way telemetry on the free TFT UART3 path.
#define MAKEIT_FA_TELEM_SERIAL           Serial3
#define MAKEIT_FA_TELEM_BAUD             250000
#define MAKEIT_FA_TELEM_INTERVAL_MS      200

//#define MAKEIT_FA_MARKER_ENCODER_PIN     P1_01  // OPTIONAL PLACEHOLDER
'''

    ensure_contains(path, marker, lambda text: text.rstrip() + block + "\n")

    # The low-level parser is what makes M879 out-of-band.
    text = read(path)
    if re.search(r'^\s*#define\s+EMERGENCY_PARSER\b', text, re.MULTILINE):
        print(f"ok {path}: EMERGENCY_PARSER enabled")
    elif re.search(r'^\s*//\s*#define\s+EMERGENCY_PARSER\b', text, re.MULTILINE):
        text = re.sub(
            r'^\s*//\s*#define\s+EMERGENCY_PARSER\b.*$',
            '#define EMERGENCY_PARSER',
            text,
            count=1,
            flags=re.MULTILINE,
        )
        write(path, text)
    else:
        raise RuntimeError("EMERGENCY_PARSER is required for out-of-band M879")


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
        'feature/makeit_fa_transaction.h',
        lambda text: text.replace(
            '  #include "feature/makeit_filament_analyzer_phase0.h"',
            '  #include "feature/makeit_filament_analyzer_phase0.h"\n  #include "feature/makeit_fa_transaction.h"',
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

    ensure_contains(
        path,
        'makeit_fa_transaction.idle();',
        lambda text: text.replace(
            '    makeit_fa_phase0.idle();',
            '    makeit_fa_transaction.idle();\n    makeit_fa_phase0.idle();',
            1,
        ),
    )

    # Consume an emergency M879 before the analyzer can enqueue another segment.
    text = read(path)
    old_order = '    makeit_fa_phase0.idle();\n    makeit_fa_transaction.idle();'
    new_order = '    makeit_fa_transaction.idle();\n    makeit_fa_phase0.idle();'
    if old_order in text:
        write(path, text.replace(old_order, new_order, 1))
    elif new_order in text:
        print(f"ok {path}: transaction idle precedes analyzer idle")


def patch_feature_cpp() -> None:
    path = "Marlin/src/feature/makeit_filament_analyzer_phase0.cpp"

    ensure_contains(
        path,
        'service_pulse_gap_monitor();',
        lambda text: text.replace(
            '  service_segmented_feed();\n  service_test_point();',
            '  service_pulse_gap_monitor();\n  service_segmented_feed();\n  service_test_point();',
            1,
        ),
    )

    ensure_contains(
        path,
        'tp_host_abort_requested_ = false;',
        lambda text: text.replace(
            '  tp_abort_triggered_ = false;\n',
            '  tp_abort_triggered_ = false;\n  tp_host_abort_requested_ = false;\n',
            1,
        ),
    )

    ensure_contains(
        path,
        'case TP_RESULT_ABORTED:',
        lambda text: text.replace(
            '    case TP_RESULT_LOW_FEED:     return "LOW_FEED";\n',
            '    case TP_RESULT_LOW_FEED:     return "LOW_FEED";\n    case TP_RESULT_ABORTED:      return "ABORTED";\n',
            1,
        ),
    )

    ensure_contains(
        path,
        'else if (tp_host_abort_requested_)',
        lambda text: text.replace(
            '  if (!tp_temp_valid_ || tp_temp_samples_ == 0)\n    tp_result_ = TP_RESULT_INVALID_TEMP;\n  else if (tp_abort_triggered_)\n    tp_result_ = TP_RESULT_LOW_FEED;',
            '  if (!tp_temp_valid_ || tp_temp_samples_ == 0)\n    tp_result_ = TP_RESULT_INVALID_TEMP;\n  else if (tp_host_abort_requested_)\n    tp_result_ = TP_RESULT_ABORTED;\n  else if (tp_abort_triggered_)\n    tp_result_ = TP_RESULT_LOW_FEED;',
            1,
        ),
    )

    ensure_contains(
        path,
        'SERIAL_ECHOPGM(" host_abort=");',
        lambda text: text.replace(
            '  SERIAL_ECHOPGM(" aborted="); SERIAL_ECHO(tp_abort_triggered_ ? 1 : 0);\n  SERIAL_ECHOPGM(" abort_cmd_mm=");',
            '  SERIAL_ECHOPGM(" aborted="); SERIAL_ECHO(tp_abort_triggered_ ? 1 : 0);\n  SERIAL_ECHOPGM(" host_abort="); SERIAL_ECHO(tp_host_abort_requested_ ? 1 : 0);\n  SERIAL_ECHOPGM(" abort_cmd_mm=");',
            1,
        ),
    )

    text = read(path)
    compact = '  SERIAL_ECHOPGM(" abort="); SERIAL_ECHO(tp_abort_triggered_ ? 1 : 0);\n  SERIAL_ECHOLNPGM("");'
    if compact in text:
        write(path, text.replace(
            compact,
            '  SERIAL_ECHOPGM(" abort="); SERIAL_ECHO(tp_abort_triggered_ ? 1 : 0);\n  SERIAL_ECHOPGM(" host_abort="); SERIAL_ECHO(tp_host_abort_requested_ ? 1 : 0);\n  SERIAL_ECHOLNPGM("");',
            1,
        ))


def patch_gcode_h() -> None:
    path = "Marlin/src/gcode/gcode.h"

    def add_analyzer_commands(text: str) -> str:
        old = '  #if HAS_PTC\n    static void M871();\n  #endif'
        new = (
            '  #if HAS_PTC\n    static void M871();\n  #endif\n\n'
            '  #if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)\n'
            '    static void M873();\n'
            '    static void M874();\n'
            '    static void M875();\n'
            '    static void M877();\n'
            '    static void M878();\n'
            '    static void M879();\n'
            '  #endif'
        )
        return text.replace(old, new, 1)

    ensure_contains(path, 'static void M875();', add_analyzer_commands)
    ensure_contains(path, 'static void M874();', lambda text: text.replace('    static void M875();', '    static void M874();\n    static void M875();', 1))
    ensure_contains(path, 'static void M873();', lambda text: text.replace('    static void M874();', '    static void M873();\n    static void M874();', 1))
    ensure_contains(path, 'static void M877();', lambda text: text.replace('    static void M875();', '    static void M875();\n    static void M877();', 1))
    ensure_contains(path, 'static void M878();', lambda text: text.replace('    static void M877();', '    static void M877();\n    static void M878();', 1))
    ensure_contains(path, 'static void M879();', lambda text: text.replace('    static void M878();', '    static void M878();\n    static void M879();', 1))

    text = read(path)
    if 'static void M876();' in text:
        write(path, text.replace('    static void M876();\n', ''))


def patch_gcode_cpp() -> None:
    path = "Marlin/src/gcode/gcode.cpp"

    def add_analyzer_commands(text: str) -> str:
        old = '      #if HAS_PTC\n        case 871: M871(); break;                                  // M871: Print/reset/clear first layer temperature offset values\n      #endif'
        new = (
            '      #if HAS_PTC\n        case 871: M871(); break;                                  // M871: Print/reset/clear first layer temperature offset values\n      #endif\n\n'
            '      #if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)\n'
            '        case 873: M873(); break;                                  // M873: MAKEiT evaluated extrusion test point\n'
            '        case 874: M874(); break;                                  // M874: MAKEiT segmented feed diagnostic\n'
            '        case 875: M875(); break;                                  // M875: MAKEiT encoder diagnostics\n'
            '        case 877: M877(); break;                                  // M877: MAKEiT idempotent point execute\n'
            '        case 878: M878(); break;                                  // M878: MAKEiT repeatable result query\n'
            '        case 879: M879(); break;                                  // M879: MAKEiT graceful point abort\n'
            '      #endif'
        )
        return text.replace(old, new, 1)

    ensure_contains(path, 'case 875: M875(); break;', add_analyzer_commands)
    ensure_contains(path, 'case 874: M874(); break;', lambda text: text.replace('        case 875: M875(); break;                                  // M875: MAKEiT filament analyzer Phase-0 diagnostics', '        case 874: M874(); break;                                  // M874: MAKEiT segmented feed diagnostic\n        case 875: M875(); break;                                  // M875: MAKEiT encoder diagnostics', 1))
    ensure_contains(path, 'case 873: M873(); break;', lambda text: text.replace('        case 874: M874(); break;', '        case 873: M873(); break;                                  // M873: MAKEiT evaluated extrusion test point\n        case 874: M874(); break;', 1))
    ensure_contains(path, 'case 877: M877(); break;', lambda text: text.replace('        case 875: M875(); break;', '        case 875: M875(); break;\n        case 877: M877(); break;                                  // M877: MAKEiT idempotent point execute', 1))
    ensure_contains(path, 'case 878: M878(); break;', lambda text: text.replace('        case 877: M877(); break;', '        case 877: M877(); break;\n        case 878: M878(); break;                                  // M878: MAKEiT repeatable result query', 1))
    ensure_contains(path, 'case 879: M879(); break;', lambda text: text.replace('        case 878: M878(); break;', '        case 878: M878(); break;\n        case 879: M879(); break;                                  // M879: MAKEiT graceful point abort', 1))

    text = read(path)
    obsolete = '        case 876: M876(); break;                                  // M876: MAKEiT filament analyzer segmented feed diagnostic\n'
    if obsolete in text:
        write(path, text.replace(obsolete, ''))


def main() -> int:
    patch_configuration_h()
    patch_configuration_adv_h()
    patch_marlin_core()
    patch_feature_cpp()
    patch_gcode_h()
    patch_gcode_cpp()
    print("Phase-0/1/2/3/4/5/6 analyzer integration hooks are in place.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
