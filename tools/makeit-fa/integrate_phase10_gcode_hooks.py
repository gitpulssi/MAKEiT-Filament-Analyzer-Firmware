#!/usr/bin/env python3
"""Repair and verify the Phase-10 M880/M881 Marlin G-code hooks.

This stage is intentionally separate from the larger conditioning patch. It
locates the existing MAKEiT analyzer preprocessor block structurally, inserts
M880/M881 inside that block, and verifies the result before PlatformIO builds.
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable, List, Tuple

ROOT = Path(__file__).resolve().parents[2]


def read_lines(path: str) -> List[str]:
    return (ROOT / path).read_text(
        encoding="utf-8", errors="surrogateescape"
    ).splitlines(keepends=True)


def write_lines(path: str, lines: List[str]) -> None:
    (ROOT / path).write_text(
        "".join(lines), encoding="utf-8", errors="surrogateescape"
    )
    print(f"updated {path}")


def is_if_directive(stripped: str) -> bool:
    return (
        stripped.startswith("#if ")
        or stripped.startswith("#ifdef ")
        or stripped.startswith("#ifndef ")
    )


def find_makeit_block(
    lines: List[str], required: Callable[[str], bool]
) -> Tuple[int, int]:
    """Return [start, end] for the analyzer #if block containing required text."""
    marker = "#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)"

    for start, line in enumerate(lines):
        if line.strip() != marker:
            continue

        depth = 1
        for end in range(start + 1, len(lines)):
            stripped = lines[end].strip()
            if is_if_directive(stripped):
                depth += 1
            elif stripped.startswith("#endif"):
                depth -= 1
                if depth == 0:
                    body = "".join(lines[start : end + 1])
                    if required(body):
                        return start, end
                    break

    raise RuntimeError(
        "MAKEiT analyzer G-code block was not found in the expected Marlin file"
    )


def ensure_exact_line_after(
    lines: List[str],
    start: int,
    end: int,
    target: str,
    anchors: Tuple[str, ...],
) -> Tuple[List[str], int, bool]:
    """Ensure target exists in [start,end], inserting after the first anchor found."""
    block = lines[start : end + 1]
    if any(line.rstrip("\r\n") == target for line in block):
        return lines, end, False

    for anchor in anchors:
        for index in range(start, end + 1):
            if lines[index].rstrip("\r\n") == anchor:
                newline = "\r\n" if lines[index].endswith("\r\n") else "\n"
                lines.insert(index + 1, target + newline)
                return lines, end + 1, True

    raise RuntimeError(
        f"Unable to insert {target!r}; none of the anchors were found: {anchors!r}"
    )


def patch_gcode_h() -> None:
    path = "Marlin/src/gcode/gcode.h"
    lines = read_lines(path)
    start, end = find_makeit_block(
        lines, lambda body: "static void M879();" in body
    )

    lines, end, changed_880 = ensure_exact_line_after(
        lines,
        start,
        end,
        "    static void M880();",
        ("    static void M879();",),
    )
    lines, end, changed_881 = ensure_exact_line_after(
        lines,
        start,
        end,
        "    static void M881();",
        ("    static void M880();", "    static void M879();"),
    )

    block = "".join(lines[start : end + 1])
    for declaration in ("static void M880();", "static void M881();"):
        if declaration not in block:
            raise RuntimeError(f"gcode.h verification failed for {declaration}")

    if changed_880 or changed_881:
        write_lines(path, lines)
    else:
        print("ok Marlin/src/gcode/gcode.h: M880/M881 declarations are in analyzer block")


def patch_gcode_cpp() -> None:
    path = "Marlin/src/gcode/gcode.cpp"
    lines = read_lines(path)
    start, end = find_makeit_block(
        lines, lambda body: "case 879: M879(); break;" in body
    )

    lines, end, changed_880 = ensure_exact_line_after(
        lines,
        start,
        end,
        "        case 880: M880(); break;                                  // M880: MAKEiT recovery / re-prime",
        (
            "        case 879: M879(); break;                                  // M879: MAKEiT graceful point abort",
            "        case 879: M879(); break;",
        ),
    )
    lines, end, changed_881 = ensure_exact_line_after(
        lines,
        start,
        end,
        "        case 881: M881(); break;                                  // M881: MAKEiT point-conditioning configuration",
        (
            "        case 880: M880(); break;                                  // M880: MAKEiT recovery / re-prime",
            "        case 880: M880(); break;",
        ),
    )

    block = "".join(lines[start : end + 1])
    for dispatcher in ("case 880: M880(); break;", "case 881: M881(); break;"):
        if dispatcher not in block:
            raise RuntimeError(f"gcode.cpp verification failed for {dispatcher}")

    if changed_880 or changed_881:
        write_lines(path, lines)
    else:
        print("ok Marlin/src/gcode/gcode.cpp: M880/M881 dispatchers are in analyzer block")


def main() -> int:
    patch_gcode_h()
    patch_gcode_cpp()
    print("Phase-10 M880/M881 G-code hooks are present and verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
