from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="surrogateescape")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", errors="surrogateescape")
    print(f"updated {path}")


def ensure(path: str, needle: str, old: str, new: str) -> None:
    text = read(path)
    if needle in text:
        print(f"ok {path}: already contains {needle!r}")
        return
    changed = text.replace(old, new, 1)
    if changed == text:
        raise RuntimeError(f"failed to patch {path}; missing pattern for {needle!r}")
    write(path, changed)
