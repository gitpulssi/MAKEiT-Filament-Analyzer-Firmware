#!/usr/bin/env python3
"""Treat an explicit host heater-off command as a clean analyzer abort.

M104 S0 is commonly used by OctoPrint or an operator to end a supervised test.
The campaign should preserve completed results, stop without reporting a
firmware error, and the outer envelope must not turn the hotend back on by
restoring its original target.
"""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="surrogateescape")


def write(path: str, content: str) -> None:
    (ROOT / path).write_text(content, encoding="utf-8", errors="surrogateescape")
    print(f"updated {path}")


def patch_campaign_heater_off() -> None:
    path = "Marlin/src/feature/makeit_fa_campaign.cpp"
    text = read(path)

    if 'finish(CAM_ABORTED, "heater_disabled")' in text:
        print(f"ok {path}: heater-off abort handling already present")
        return

    old = '''    if (ABS(target_now - target_temp_) > 0.5f) {
      finish(CAM_ERROR, "target_changed");
      return;
    }'''
    new = '''    if (ABS(target_now - target_temp_) > 0.5f) {
      if (target_now <= 0.0f) {
        cancel_requested_ = true;
        finish(CAM_ABORTED, "heater_disabled");
      }
      else
        finish(CAM_ERROR, "target_changed");
      return;
    }'''

    changed = text.replace(old, new, 1)
    if changed == text:
        raise RuntimeError(
            "failed to patch campaign heater-off handling; target-change block not found"
        )
    write(path, changed)


def patch_envelope_target_restore() -> None:
    path = "Marlin/src/feature/makeit_fa_envelope.cpp"
    text = read(path)

    if "const float current_target_c = float(thermalManager.degTargetHotend(0));" in text:
        print(f"ok {path}: explicit heater-off target is already preserved")
        return

    old = '''void MakeItFATemperatureEnvelope::restore_original_target() {
  if (original_target_temp_c_ > 0.0f)
    thermalManager.setTargetHotend(celsius_t(original_target_temp_c_ + 0.5f), 0);
}'''
    new = '''void MakeItFATemperatureEnvelope::restore_original_target() {
  // An explicit M104 S0 is an operator / host request to keep the heater off.
  // Never defeat that request by restoring the pre-envelope temperature.
  const float current_target_c = float(thermalManager.degTargetHotend(0));
  if (current_target_c <= 0.0f) return;

  if (original_target_temp_c_ > 0.0f)
    thermalManager.setTargetHotend(celsius_t(original_target_temp_c_ + 0.5f), 0);
}'''

    changed = text.replace(old, new, 1)
    if changed == text:
        raise RuntimeError(
            "failed to patch envelope target restore; original function not found"
        )
    write(path, changed)


def verify() -> None:
    campaign = read("Marlin/src/feature/makeit_fa_campaign.cpp")
    envelope = read("Marlin/src/feature/makeit_fa_envelope.cpp")

    if 'finish(CAM_ABORTED, "heater_disabled")' not in campaign:
        raise RuntimeError("campaign heater-off verification failed")
    if "current_target_c <= 0.0f" not in envelope:
        raise RuntimeError("envelope heater-off preservation verification failed")


def main() -> int:
    patch_campaign_heater_off()
    patch_envelope_target_restore()
    verify()
    print("Phase-10 host heater-off handling is present and verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
