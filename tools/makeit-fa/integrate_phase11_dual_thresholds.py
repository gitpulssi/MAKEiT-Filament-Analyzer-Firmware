#!/usr/bin/env python3
"""Add Phase-11 dual accuracy / hard-throughput campaign thresholds.

P remains the printing-accuracy threshold. R remains the rolling safety and
hard-throughput threshold. A full-length point that finishes below P but at or
above R is retained as an accuracy-limit observation and the speed ladder
continues. Early rolling stops or whole-point efficiency below R remain hard
LIMIT_FOUND results.
"""

from __future__ import annotations

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="surrogateescape")


def write(path: str, content: str) -> None:
    (ROOT / path).write_text(content, encoding="utf-8", errors="surrogateescape")
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


def patch_campaign_header() -> None:
    path = "Marlin/src/feature/makeit_fa_campaign.h"
    ensure(
        path,
        "last_accurate_feed_mm_min()",
        "  static float first_fail_feed_mm_min() { return first_fail_feed_mm_min_; }",
        "  static float first_fail_feed_mm_min() { return first_fail_feed_mm_min_; }\n"
        "  // Phase 11: P is print accuracy; R is the hard throughput threshold.\n"
        "  static float last_accurate_feed_mm_min() { return last_accurate_feed_mm_min_; }\n"
        "  static float first_accuracy_fail_feed_mm_min() { return first_accuracy_fail_feed_mm_min_; }\n"
        "  static float last_point_efficiency_pct() { return last_point_efficiency_pct_; }",
    )
    ensure(
        path,
        "static float last_accurate_feed_mm_min_;",
        "  static float first_fail_feed_mm_min_;",
        "  static float first_fail_feed_mm_min_;\n"
        "  static float last_accurate_feed_mm_min_;\n"
        "  static float first_accuracy_fail_feed_mm_min_;\n"
        "  static float last_point_efficiency_pct_;",
    )


def patch_campaign_source() -> None:
    path = "Marlin/src/feature/makeit_fa_campaign.cpp"
    ensure(
        path,
        "float MakeItFASpeedCampaign::last_accurate_feed_mm_min_",
        "float MakeItFASpeedCampaign::first_fail_feed_mm_min_ = 0.0f;",
        "float MakeItFASpeedCampaign::first_fail_feed_mm_min_ = 0.0f;\n"
        "float MakeItFASpeedCampaign::last_accurate_feed_mm_min_ = 0.0f;\n"
        "float MakeItFASpeedCampaign::first_accuracy_fail_feed_mm_min_ = 0.0f;\n"
        "float MakeItFASpeedCampaign::last_point_efficiency_pct_ = 0.0f;",
    )
    ensure(
        path,
        'SERIAL_ECHOPGM(" accuracy_last_pass=");',
        '  SERIAL_ECHOPGM(" first_fail="); SERIAL_ECHO(first_fail_feed_mm_min_);',
        '  SERIAL_ECHOPGM(" first_fail="); SERIAL_ECHO(first_fail_feed_mm_min_);\n'
        '  SERIAL_ECHOPGM(" accuracy_last_pass="); SERIAL_ECHO(last_accurate_feed_mm_min_);\n'
        '  SERIAL_ECHOPGM(" accuracy_first_fail="); SERIAL_ECHO(first_accuracy_fail_feed_mm_min_);\n'
        '  SERIAL_ECHOPGM(" point_efficiency="); SERIAL_ECHO(last_point_efficiency_pct_);\n'
        '  SERIAL_ECHOPGM(" accuracy_threshold="); SERIAL_ECHO(params_.point.pass_efficiency_pct);\n'
        '  SERIAL_ECHOPGM(" throughput_threshold="); SERIAL_ECHO(params_.point.monitor_efficiency_pct);',
    )

    text = read(path)
    if 'report("accuracy_limit_continue")' not in text:
        pattern = re.compile(
            r"void MakeItFASpeedCampaign::handle_point_result\(\) \{.*?\n\}\n\nbool MakeItFASpeedCampaign::start",
            re.S,
        )
        replacement = r'''void MakeItFASpeedCampaign::handle_point_result() {
  last_point_result_code_ = makeit_fa_transaction.result_code();
  last_point_result_crc_ = makeit_fa_transaction.result_crc();
  last_point_efficiency_pct_ = makeit_fa_phase0.test_point_efficiency_pct();
  report("point_result");

  if (makeit_fa_transaction.state() == MakeItFATransaction::TX_ABORTED
      || last_point_result_code_ == MakeItFilamentAnalyzerPhase0::TP_RESULT_ABORTED) {
    finish(CAM_ABORTED, "aborted");
    return;
  }

  bool accepted_for_throughput = false;
  const bool full_length = makeit_fa_phase0.test_point_tested_mm()
    >= params_.point.total_mm - 0.001f;

  switch (last_point_result_code_) {
    case MakeItFilamentAnalyzerPhase0::TP_RESULT_PASS:
      if (first_accuracy_fail_feed_mm_min_ <= 0.0f)
        last_accurate_feed_mm_min_ = current_feed_mm_min_;
      accepted_for_throughput = true;
      break;

    case MakeItFilamentAnalyzerPhase0::TP_RESULT_LOW_FEED:
      // Phase 11 soft limit: the full point missed P, but still meets R.
      // Record the printing-accuracy boundary and continue toward the physical
      // throughput limit. Early rolling stops and values below R remain hard.
      if (full_length
          && params_.point.pass_efficiency_pct > params_.point.monitor_efficiency_pct + 0.001f
          && last_point_efficiency_pct_ >= params_.point.monitor_efficiency_pct) {
        if (first_accuracy_fail_feed_mm_min_ <= 0.0f)
          first_accuracy_fail_feed_mm_min_ = current_feed_mm_min_;
        accepted_for_throughput = true;
        report("accuracy_limit_continue");
      }
      else {
        first_fail_feed_mm_min_ = current_feed_mm_min_;
        finish(CAM_LIMIT_FOUND, "limit_found");
        return;
      }
      break;

    case MakeItFilamentAnalyzerPhase0::TP_RESULT_INVALID_TEMP:
      finish(CAM_INVALID_TEMP, "invalid_temp");
      return;

    default:
      finish(CAM_ERROR, "point_error");
      return;
  }

  if (!accepted_for_throughput) {
    finish(CAM_ERROR, "classification_error");
    return;
  }

  last_pass_feed_mm_min_ = current_feed_mm_min_;
  const uint16_t next_index = point_index_ + 1;
  const float next_feed = params_.start_feed_mm_min + float(next_index) * params_.step_feed_mm_min;

  if (next_index >= point_count_ || next_feed > params_.max_feed_mm_min + 0.0001f) {
    finish(CAM_COMPLETE, "max_reached");
    return;
  }

  point_index_ = next_index;
  current_feed_mm_min_ = next_feed;
  enter_wait_temp();
  report("next_speed");
}

bool MakeItFASpeedCampaign::start'''
        changed, count = pattern.subn(replacement, text, count=1)
        if count != 1:
            raise RuntimeError("failed to replace Phase-11 campaign result classifier")
        write(path, changed)
    else:
        print(f"ok {path}: Phase-11 result classifier already present")

    ensure(
        path,
        "  last_accurate_feed_mm_min_ = 0.0f;",
        "  first_fail_feed_mm_min_ = 0.0f;\n"
        "  conditioning_started_ms_ = 0;",
        "  first_fail_feed_mm_min_ = 0.0f;\n"
        "  last_accurate_feed_mm_min_ = 0.0f;\n"
        "  first_accuracy_fail_feed_mm_min_ = 0.0f;\n"
        "  last_point_efficiency_pct_ = 0.0f;\n"
        "  conditioning_started_ms_ = 0;",
    )


def patch_envelope_header() -> None:
    path = "Marlin/src/feature/makeit_fa_envelope.h"
    ensure(
        path,
        "float last_accurate_feed_mm_min;",
        "    float first_fail_feed_mm_min;",
        "    float first_fail_feed_mm_min;\n"
        "    float last_accurate_feed_mm_min;\n"
        "    float first_accuracy_fail_feed_mm_min;",
    )


def patch_envelope_source() -> None:
    path = "Marlin/src/feature/makeit_fa_envelope.cpp"
    ensure(
        path,
        "const float q_accurate =",
        "  const float q_fail = feed_to_q_mm3_s(r.first_fail_feed_mm_min, params_.filament_diameter_mm);",
        "  const float q_fail = feed_to_q_mm3_s(r.first_fail_feed_mm_min, params_.filament_diameter_mm);\n"
        "  const float q_accurate = feed_to_q_mm3_s(r.last_accurate_feed_mm_min, params_.filament_diameter_mm);\n"
        "  const float q_accuracy_fail = feed_to_q_mm3_s(r.first_accuracy_fail_feed_mm_min, params_.filament_diameter_mm);",
    )
    ensure(
        path,
        'SERIAL_ECHOPGM(" accuracy_last_pass_feed=");',
        '  SERIAL_ECHOPGM(" q_fail_mm3_s="); SERIAL_ECHO(q_fail);',
        '  SERIAL_ECHOPGM(" q_fail_mm3_s="); SERIAL_ECHO(q_fail);\n'
        '  SERIAL_ECHOPGM(" accuracy_last_pass_feed="); SERIAL_ECHO(r.last_accurate_feed_mm_min);\n'
        '  SERIAL_ECHOPGM(" accuracy_first_fail_feed="); SERIAL_ECHO(r.first_accuracy_fail_feed_mm_min);\n'
        '  SERIAL_ECHOPGM(" q_accurate_mm3_s="); SERIAL_ECHO(q_accurate);\n'
        '  SERIAL_ECHOPGM(" q_accuracy_fail_mm3_s="); SERIAL_ECHO(q_accuracy_fail);',
    )
    ensure(
        path,
        "crc = crc32_word(crc, float_bits(r.last_accurate_feed_mm_min));",
        "    crc = crc32_word(crc, float_bits(r.first_fail_feed_mm_min));",
        "    crc = crc32_word(crc, float_bits(r.first_fail_feed_mm_min));\n"
        "    crc = crc32_word(crc, float_bits(r.last_accurate_feed_mm_min));\n"
        "    crc = crc32_word(crc, float_bits(r.first_accuracy_fail_feed_mm_min));",
    )
    ensure(
        path,
        "r.last_accurate_feed_mm_min = makeit_fa_campaign.last_accurate_feed_mm_min();",
        "  r.first_fail_feed_mm_min = makeit_fa_campaign.first_fail_feed_mm_min();",
        "  r.first_fail_feed_mm_min = makeit_fa_campaign.first_fail_feed_mm_min();\n"
        "  r.last_accurate_feed_mm_min = makeit_fa_campaign.last_accurate_feed_mm_min();\n"
        "  r.first_accuracy_fail_feed_mm_min = makeit_fa_campaign.first_accuracy_fail_feed_mm_min();",
    )


def verify() -> None:
    required = {
        "Marlin/src/feature/makeit_fa_campaign.h": (
            "last_accurate_feed_mm_min()",
            "first_accuracy_fail_feed_mm_min()",
        ),
        "Marlin/src/feature/makeit_fa_campaign.cpp": (
            "accuracy_limit_continue",
            "throughput_threshold=",
        ),
        "Marlin/src/feature/makeit_fa_envelope.h": (
            "last_accurate_feed_mm_min",
            "first_accuracy_fail_feed_mm_min",
        ),
        "Marlin/src/feature/makeit_fa_envelope.cpp": (
            "q_accurate_mm3_s=",
            "accuracy_first_fail_feed=",
        ),
    }
    for path, markers in required.items():
        text = read(path)
        for marker in markers:
            if marker not in text:
                raise RuntimeError(f"Phase-11 verification failed: {path} missing {marker!r}")
    print("Phase-11 dual accuracy / throughput thresholds are present and verified.")


def main() -> int:
    patch_campaign_header()
    patch_campaign_source()
    patch_envelope_header()
    patch_envelope_source()
    verify()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
