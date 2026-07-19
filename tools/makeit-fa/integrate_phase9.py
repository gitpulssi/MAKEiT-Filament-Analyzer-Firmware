#!/usr/bin/env python3
"""Add Phase-9 recovery/re-prime hooks to an integrated Marlin tree."""

from __future__ import annotations

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="surrogateescape")


def write(path: str, content: str) -> None:
    (ROOT / path).write_text(content, encoding="utf-8", errors="surrogateescape")
    print(f"updated {path}")


def ensure_contains(path: str, needle: str, edit) -> None:
    text = read(path)
    if needle in text:
        print(f"ok {path}: already contains {needle!r}")
        return
    changed = edit(text)
    if changed == text:
        raise RuntimeError(f"failed to patch {path}; pattern not found for {needle!r}")
    write(path, changed)


def patch_analyzer_header() -> None:
    path = "Marlin/src/feature/makeit_filament_analyzer_phase0.h"
    ensure_contains(
        path,
        "static bool request_segmented_feed_stop();",
        lambda text: text.replace(
            "  static bool request_host_abort();",
            "  static bool request_host_abort();\n\n"
            "  /** Stop an open-loop segmented prime and drain the bounded horizon. */\n"
            "  static bool request_segmented_feed_stop();",
            1,
        ),
    )


def patch_marlin_core() -> None:
    path = "Marlin/src/MarlinCore.cpp"
    ensure_contains(
        path,
        'feature/makeit_fa_recovery.h',
        lambda text: text.replace(
            '  #include "feature/makeit_fa_transaction.h"',
            '  #include "feature/makeit_fa_transaction.h"\n'
            '  #include "feature/makeit_fa_recovery.h"',
            1,
        ),
    )
    ensure_contains(
        path,
        "makeit_fa_recovery.idle();",
        lambda text: text.replace(
            "    makeit_fa_phase0.idle();",
            "    makeit_fa_phase0.idle();\n"
            "    makeit_fa_recovery.idle();",
            1,
        ),
    )

    text = read(path)
    pattern = re.compile(
        r"(?:    makeit_fa_(?:transaction|phase0|recovery|campaign|envelope)\.idle\(\);\n){5}"
    )
    canonical = (
        "    makeit_fa_transaction.idle();\n"
        "    makeit_fa_phase0.idle();\n"
        "    makeit_fa_recovery.idle();\n"
        "    makeit_fa_campaign.idle();\n"
        "    makeit_fa_envelope.idle();\n"
    )
    changed, count = pattern.subn(canonical, text, count=1)
    if count and changed != text:
        write(path, changed)
    elif canonical in text:
        print(f"ok {path}: Phase-9 idle order is canonical")
    else:
        raise RuntimeError("failed to normalize Phase-9 idle service order")


def patch_gcode_h() -> None:
    path = "Marlin/src/gcode/gcode.h"
    text = read(path)
    obsolete = "    static void M869();\n    static void M870();"
    if obsolete in text:
        text = text.replace(obsolete, "    static void M870();", 1)
        write(path, text)

    ensure_contains(
        path,
        "static void M880();",
        lambda current: current.replace(
            "    static void M879();",
            "    static void M879();\n"
            "    static void M880();",
            1,
        ),
    )


def patch_gcode_cpp() -> None:
    path = "Marlin/src/gcode/gcode.cpp"
    text = read(path)
    obsolete = "        case 869: M869(); break;                                  // M869: MAKEiT recovery / re-prime\n"
    if obsolete in text:
        write(path, text.replace(obsolete, "", 1))

    ensure_contains(
        path,
        "case 880: M880(); break;",
        lambda current: current.replace(
            "        case 879: M879(); break;",
            "        case 879: M879(); break;\n"
            "        case 880: M880(); break;                                  // M880: MAKEiT recovery / re-prime",
            1,
        ),
    )


def verify_phase9_sources() -> None:
    required = {
        "Marlin/src/gcode/feature/M870.cpp": "recovery_temp_c",
        "Marlin/src/gcode/feature/M880.cpp": "GcodeSuite::M880",
        "Marlin/src/feature/makeit_fa_envelope.cpp": "ENV_RECOVERING",
        "Marlin/src/feature/makeit_fa_recovery.cpp": "FA9: tag=",
    }
    for path, needle in required.items():
        if needle not in read(path):
            raise RuntimeError(f"{path} is not the Phase-9 source; missing {needle!r}")
        print(f"ok {path}: contains {needle!r}")


def main() -> int:
    verify_phase9_sources()
    patch_analyzer_header()
    patch_marlin_core()
    patch_gcode_h()
    patch_gcode_cpp()
    print("Phase-9 recovery / re-prime hooks are in place.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
