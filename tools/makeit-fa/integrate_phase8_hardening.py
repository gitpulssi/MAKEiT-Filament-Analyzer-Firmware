#!/usr/bin/env python3
"""Fix Phase-8 command truncation and premature thermal settling."""

from __future__ import annotations

from pathlib import Path
import re

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


def patch_command_buffer() -> None:
    path = "Marlin/Configuration_adv.h"
    text = read(path)
    match = re.search(r"(?m)^(\s*#define\s+MAX_CMD_SIZE\s+)(\d+)([^\r\n]*)$", text)
    if not match:
        raise RuntimeError("MAX_CMD_SIZE definition not found")
    if int(match.group(2)) >= 192:
        print(f"ok {path}: MAX_CMD_SIZE={match.group(2)}")
        return
    line = match.group(1) + "192  // MAKEiT analyzer campaign commands may exceed 96 bytes"
    write(path, text[:match.start()] + line + text[match.end():])


def patch_campaign_header() -> None:
    path = "Marlin/src/feature/makeit_fa_campaign.h"
    ensure(
        path,
        "static float settle_band_c_;",
        "  static uint32_t stable_since_ms_;\n"
        "  static uint32_t next_report_ms_;\n"
        "  static float target_temp_;",
        "  static uint32_t stable_since_ms_;\n"
        "  static uint32_t next_report_ms_;\n"
        "  static float target_temp_;\n"
        "  static float settle_band_c_;\n"
        "  static float stable_temp_min_c_;\n"
        "  static float stable_temp_max_c_;",
    )


def patch_campaign_source() -> None:
    path = "Marlin/src/feature/makeit_fa_campaign.cpp"
    ensure(
        path,
        "float MakeItFASpeedCampaign::settle_band_c_",
        "uint32_t MakeItFASpeedCampaign::next_report_ms_ = 0;\n"
        "float MakeItFASpeedCampaign::target_temp_ = 0.0f;",
        "uint32_t MakeItFASpeedCampaign::next_report_ms_ = 0;\n"
        "float MakeItFASpeedCampaign::target_temp_ = 0.0f;\n"
        "float MakeItFASpeedCampaign::settle_band_c_ = 1.0f;\n"
        "float MakeItFASpeedCampaign::stable_temp_min_c_ = 0.0f;\n"
        "float MakeItFASpeedCampaign::stable_temp_max_c_ = 0.0f;",
    )
    ensure(
        path,
        "const float stable_span_c =",
        "  const uint32_t stable_ms = stable_since_ms_ ? now - stable_since_ms_ : 0;",
        "  const uint32_t stable_ms = stable_since_ms_ ? now - stable_since_ms_ : 0;\n"
        "  const float stable_span_c = stable_since_ms_\n"
        "    ? stable_temp_max_c_ - stable_temp_min_c_ : 0.0f;",
    )
    ensure(
        path,
        'SERIAL_ECHOPGM(" settle_band_c=");',
        '  SERIAL_ECHOPGM(" stable_ms="); SERIAL_ECHO(stable_ms);',
        '  SERIAL_ECHOPGM(" stable_ms="); SERIAL_ECHO(stable_ms);\n'
        '  SERIAL_ECHOPGM(" settle_band_c="); SERIAL_ECHO(settle_band_c_);\n'
        '  SERIAL_ECHOPGM(" stable_span_c="); SERIAL_ECHO(stable_span_c);',
    )
    ensure(
        path,
        "const float temp_now = thermalManager.degHotend(0);\n  state_ = CAM_WAIT_TEMP;",
        "void MakeItFASpeedCampaign::enter_wait_temp() {\n"
        "  state_ = CAM_WAIT_TEMP;\n"
        "  stable_since_ms_ = 0;\n"
        "  next_report_ms_ = millis();\n"
        "}",
        "void MakeItFASpeedCampaign::enter_wait_temp() {\n"
        "  const float temp_now = thermalManager.degHotend(0);\n"
        "  state_ = CAM_WAIT_TEMP;\n"
        "  stable_since_ms_ = 0;\n"
        "  stable_temp_min_c_ = temp_now;\n"
        "  stable_temp_max_c_ = temp_now;\n"
        "  next_report_ms_ = millis();\n"
        "}",
    )
    ensure(
        path,
        "settle_band_c_ = _MIN(params_.point.temp_tolerance, 1.0f);",
        "  stable_since_ms_ = 0;\n"
        "  next_report_ms_ = started_ms_;\n"
        "  current_feed_mm_min_ = params_.start_feed_mm_min;",
        "  stable_since_ms_ = 0;\n"
        "  next_report_ms_ = started_ms_;\n"
        "  settle_band_c_ = _MIN(params_.point.temp_tolerance, 1.0f);\n"
        "  stable_temp_min_c_ = thermalManager.degHotend(0);\n"
        "  stable_temp_max_c_ = stable_temp_min_c_;\n"
        "  current_feed_mm_min_ = params_.start_feed_mm_min;",
    )

    old = """    if (ABS(temp_now - target_temp_) <= params_.point.temp_tolerance
        && thermalManager.hotEnoughToExtrude(0)) {
      if (!stable_since_ms_)
        stable_since_ms_ = now;

      const uint32_t required_ms = uint32_t(params_.settle_seconds) * 1000UL;
      if (uint32_t(now - stable_since_ms_) >= required_ms) {
        if (!start_current_point())
          finish(CAM_ERROR, \"point_start_failed\");
        return;
      }
    }
    else {
      stable_since_ms_ = 0;
    }
"""
    new = """    const bool inside_settle_band = ABS(temp_now - target_temp_) <= settle_band_c_;
    if (inside_settle_band && thermalManager.hotEnoughToExtrude(0)) {
      if (!stable_since_ms_) {
        stable_since_ms_ = now;
        stable_temp_min_c_ = stable_temp_max_c_ = temp_now;
      }
      else {
        if (temp_now < stable_temp_min_c_) stable_temp_min_c_ = temp_now;
        if (temp_now > stable_temp_max_c_) stable_temp_max_c_ = temp_now;
        if (stable_temp_max_c_ - stable_temp_min_c_ > settle_band_c_ + 0.001f) {
          stable_since_ms_ = now;
          stable_temp_min_c_ = stable_temp_max_c_ = temp_now;
        }
      }

      const uint32_t required_ms = uint32_t(params_.settle_seconds) * 1000UL;
      const float stable_span_c = stable_temp_max_c_ - stable_temp_min_c_;
      if (uint32_t(now - stable_since_ms_) >= required_ms
          && stable_span_c <= settle_band_c_ + 0.001f) {
        if (!start_current_point())
          finish(CAM_ERROR, \"point_start_failed\");
        return;
      }
    }
    else {
      stable_since_ms_ = 0;
      stable_temp_min_c_ = stable_temp_max_c_ = temp_now;
    }
"""
    ensure(path, "const bool inside_settle_band =", old, new)


def main() -> int:
    patch_command_buffer()
    patch_campaign_header()
    patch_campaign_source()
    print("Phase-8 command-buffer and thermal-settle hardening is in place.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
