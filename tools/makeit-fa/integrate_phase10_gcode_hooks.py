#!/usr/bin/env python3
"""Normalize and verify the Phase-10 M880/M881 Marlin G-code hooks.

This stage is intentionally separate from the larger conditioning patch. It
locates the existing MAKEiT analyzer preprocessor block structurally, removes
all existing M880/M881 declarations and dispatcher lines from that block, and
then inserts exactly one canonical pair. This repairs partial or repeated local
integrations, including malformed lines with duplicated trailing comments.
"""

from __future__ import annotations

from pathlib import Path
import re
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


def line_ending(line: str) -> str:
    return "\r\n" if line.endswith("\r\n") else "\n"


def normalize_header_block() -> None:
    path = "Marlin/src/gcode/gcode.h"
    lines = read_lines(path)
    start, end = find_makeit_block(
        lines, lambda body: "static void M879();" in body
    )

    targets = {"static void M880();", "static void M881();"}
    kept = []
    removed = 0
    for line in lines[start : end + 1]:
        if line.strip() in targets:
            removed += 1
            continue
        kept.append(line)

    anchor = next(
        (index for index, line in enumerate(kept) if line.strip() == "static void M879();"),
        None,
    )
    if anchor is None:
        raise RuntimeError("gcode.h analyzer block is missing static void M879();")

    newline = line_ending(kept[anchor])
    indent = re.match(r"[ \t]*", kept[anchor]).group(0)
    kept[anchor + 1 : anchor + 1] = [
        f"{indent}static void M880();{newline}",
        f"{indent}static void M881();{newline}",
    ]

    new_lines = lines[:start] + kept + lines[end + 1 :]
    body = "".join(kept)
    if body.count("static void M880();") != 1:
        raise RuntimeError("gcode.h M880 declaration normalization failed")
    if body.count("static void M881();") != 1:
        raise RuntimeError("gcode.h M881 declaration normalization failed")

    if new_lines != lines:
        write_lines(path, new_lines)
        print(f"normalized {path}: removed {removed} old M880/M881 declaration line(s)")
    else:
        print(f"ok {path}: exactly one M880 and one M881 declaration")


def normalize_dispatch_block() -> None:
    path = "Marlin/src/gcode/gcode.cpp"
    lines = read_lines(path)
    start, end = find_makeit_block(
        lines, lambda body: "case 879: M879(); break;" in body
    )

    dispatcher_pattern = re.compile(
        r"^\s*case\s+(880|881)\s*:\s*M\1\(\)\s*;\s*break\s*;"
    )
    kept = []
    removed = 0
    for line in lines[start : end + 1]:
        if dispatcher_pattern.match(line):
            removed += 1
            continue
        kept.append(line)

    anchor = next(
        (
            index
            for index, line in enumerate(kept)
            if "case 879: M879(); break;" in line
        ),
        None,
    )
    if anchor is None:
        raise RuntimeError("gcode.cpp analyzer block is missing case 879")

    newline = line_ending(kept[anchor])
    indent = re.match(r"[ \t]*", kept[anchor]).group(0)
    kept[anchor + 1 : anchor + 1] = [
        f"{indent}case 880: M880(); break;                                  // M880: MAKEiT recovery / re-prime{newline}",
        f"{indent}case 881: M881(); break;                                  // M881: MAKEiT point-conditioning configuration{newline}",
    ]

    new_lines = lines[:start] + kept + lines[end + 1 :]
    body = "".join(kept)
    if len(re.findall(r"case\s+880\s*:\s*M880\(\)\s*;\s*break\s*;", body)) != 1:
        raise RuntimeError("gcode.cpp M880 dispatcher normalization failed")
    if len(re.findall(r"case\s+881\s*:\s*M881\(\)\s*;\s*break\s*;", body)) != 1:
        raise RuntimeError("gcode.cpp M881 dispatcher normalization failed")

    if new_lines != lines:
        write_lines(path, new_lines)
        print(f"normalized {path}: removed {removed} old M880/M881 dispatcher line(s)")
    else:
        print(f"ok {path}: exactly one M880 and one M881 dispatcher")


def main() -> int:
    normalize_header_block()
    normalize_dispatch_block()
    print("Phase-10 M880/M881 G-code hooks are normalized and verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
