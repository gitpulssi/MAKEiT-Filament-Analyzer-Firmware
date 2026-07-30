#!/usr/bin/env python3
"""Add Phase-8 M870 temperature-envelope hooks to an integrated Marlin tree."""

from __future__ import annotations

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]

ANALYZER_IDLE_SERVICES = (
    "transaction",
    "phase0",
    "recovery",
    "campaign",
    "envelope",
)


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


def normalize_analyzer_idle_order(
    path: str,
    required_services: tuple[str, ...],
    phase_name: str,
) -> None:
    """Normalize installed analyzer services while retaining later-phase services."""
    text = read(path)
    names = "|".join(re.escape(name) for name in ANALYZER_IDLE_SERVICES)
    block_pattern = re.compile(
        rf"(?m)(?:^(?P<indent>[ \t]*)makeit_fa_(?:{names})\.idle\(\);[ \t]*(?:\r?\n|$))+"
    )

    for match in block_pattern.finditer(text):
        block = match.group(0)
        if not all(f"makeit_fa_{name}.idle();" in block for name in required_services):
            continue

        first_line = block.splitlines()[0]
        indent_match = re.match(r"([ \t]*)", first_line)
        indent = indent_match.group(1) if indent_match else "    "
        newline = "\r\n" if "\r\n" in block else "\n"
        present = [
            name
            for name in ANALYZER_IDLE_SERVICES
            if f"makeit_fa_{name}.idle();" in block
        ]
        canonical = "".join(
            f"{indent}makeit_fa_{name}.idle();{newline}" for name in present
        )
        new_text = text[: match.start()] + canonical + text[match.end() :]

        if new_text != text:
            write(path, new_text)
        else:
            print(
                f"ok {path}: {phase_name} idle order is canonical "
                f"with services {', '.join(present)}"
            )
        return

    positions = [text.find(f"makeit_fa_{name}.idle();") for name in required_services]
    if all(position >= 0 for position in positions) and positions == sorted(positions):
        print(f"ok {path}: {phase_name} required idle order is preserved in expanded tree")
        return

    raise RuntimeError(f"failed to normalize {phase_name} idle service order")


def patch_marlin_core() -> None:
    path = "Marlin/src/MarlinCore.cpp"

    ensure_contains(
        path,
        'feature/makeit_fa_envelope.h',
        lambda text: text.replace(
            '  #include "feature/makeit_fa_campaign.h"',
            '  #include "feature/makeit_fa_campaign.h"\n'
            '  #include "feature/makeit_fa_envelope.h"',
            1,
        ),
    )

    ensure_contains(
        path,
        "makeit_fa_envelope.idle();",
        lambda text: text.replace(
            "    makeit_fa_campaign.idle();",
            "    makeit_fa_campaign.idle();\n"
            "    makeit_fa_envelope.idle();",
            1,
        ),
    )

    normalize_analyzer_idle_order(
        path,
        ("transaction", "phase0", "campaign", "envelope"),
        "Phase-8",
    )


def patch_gcode_h() -> None:
    path = "Marlin/src/gcode/gcode.h"
    ensure_contains(
        path,
        "static void M870();",
        lambda text: text.replace(
            "    static void M872();",
            "    static void M870();\n"
            "    static void M872();",
            1,
        ),
    )


def patch_gcode_cpp() -> None:
    path = "Marlin/src/gcode/gcode.cpp"
    ensure_contains(
        path,
        "case 870: M870(); break;",
        lambda text: text.replace(
            "        case 872: M872(); break;",
            "        case 870: M870(); break;                                  // M870: MAKEiT temperature / speed envelope\n"
            "        case 872: M872(); break;",
            1,
        ),
    )


def main() -> int:
    patch_marlin_core()
    patch_gcode_h()
    patch_gcode_cpp()
    print("Phase-8 M870 temperature-envelope hooks are in place.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
