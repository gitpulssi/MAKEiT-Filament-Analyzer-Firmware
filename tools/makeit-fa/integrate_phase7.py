#!/usr/bin/env python3
"""Add Phase-7 M872 speed-campaign hooks to an already integrated Marlin tree."""

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
    new_text = edit(text)
    if new_text == text:
        raise RuntimeError(f"failed to patch {path}; pattern not found for {needle!r}")
    write(path, new_text)


def patch_marlin_core() -> None:
    path = "Marlin/src/MarlinCore.cpp"

    ensure_contains(
        path,
        'feature/makeit_fa_campaign.h',
        lambda text: text.replace(
            '  #include "feature/makeit_fa_transaction.h"',
            '  #include "feature/makeit_fa_transaction.h"\n'
            '  #include "feature/makeit_fa_campaign.h"',
            1,
        ),
    )

    ensure_contains(
        path,
        "makeit_fa_campaign.idle();",
        lambda text: text.replace(
            "    makeit_fa_phase0.idle();",
            "    makeit_fa_phase0.idle();\n"
            "    makeit_fa_campaign.idle();",
            1,
        ),
    )

    # Preserve emergency-abort ordering and service campaign orchestration last.
    text = read(path)
    pattern = re.compile(
        r"(?:    makeit_fa_(?:transaction|phase0|campaign)\.idle\(\);\n){3}"
    )
    canonical = (
        "    makeit_fa_transaction.idle();\n"
        "    makeit_fa_phase0.idle();\n"
        "    makeit_fa_campaign.idle();\n"
    )
    new_text, count = pattern.subn(canonical, text, count=1)
    if count and new_text != text:
        write(path, new_text)
    elif canonical in text:
        print(f"ok {path}: Phase-7 idle order is canonical")
    else:
        raise RuntimeError("failed to normalize Phase-7 idle service order")


def patch_gcode_h() -> None:
    path = "Marlin/src/gcode/gcode.h"
    ensure_contains(
        path,
        "static void M872();",
        lambda text: text.replace(
            "    static void M873();",
            "    static void M872();\n"
            "    static void M873();",
            1,
        ),
    )


def patch_gcode_cpp() -> None:
    path = "Marlin/src/gcode/gcode.cpp"
    ensure_contains(
        path,
        "case 872: M872(); break;",
        lambda text: text.replace(
            "        case 873: M873(); break;",
            "        case 872: M872(); break;                                  // M872: MAKEiT fixed-temperature speed campaign\n"
            "        case 873: M873(); break;",
            1,
        ),
    )


def main() -> int:
    patch_marlin_core()
    patch_gcode_h()
    patch_gcode_cpp()
    print("Phase-7 M872 campaign hooks are in place.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
