#!/usr/bin/env python3
'''Add Phase-10 same-speed flow conditioning before every measured campaign point.'''

from __future__ import annotations

from pathlib import Path

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
        "float conditioning_mm;",
        "  uint16_t settle_seconds;\n  MakeItFAPointParams point;",
        "  uint16_t settle_seconds;\n"
        "  float conditioning_mm;\n"
        "  MakeItFAPointParams point;",
    )
    ensure(
        path,
        "CAM_CONDITIONING",
        "    CAM_WAIT_TEMP,\n    CAM_RUNNING_POINT,",
        "    CAM_WAIT_TEMP,\n"
        "    CAM_CONDITIONING,\n"
        "    CAM_RUNNING_POINT,",
    )
    ensure(
        path,
        "static float default_conditioning_mm()",
        "  static void idle();",
        "  static void idle();\n\n"
        "  /** RAM-only default used by M870/M872. P0 disables conditioning. */\n"
        "  static float default_conditioning_mm() { return default_conditioning_mm_; }\n"
        "  static void set_default_conditioning_mm(float mm);\n"
        "  static void report_conditioning_config();",
    )
    ensure(
        path,
        "state_ == CAM_CONDITIONING",
        "    return state_ == CAM_WAIT_TEMP || state_ == CAM_RUNNING_POINT;",
        "    return state_ == CAM_WAIT_TEMP || state_ == CAM_CONDITIONING\n"
        "        || state_ == CAM_RUNNING_POINT;",
    )
    ensure(
        path,
        "static float default_conditioning_mm_;",
        "  static float first_fail_feed_mm_min_;\n"
        "  static uint8_t last_point_result_code_;",
        "  static float first_fail_feed_mm_min_;\n"
        "  static float default_conditioning_mm_;\n"
        "  static uint32_t conditioning_started_ms_;\n"
        "  static uint32_t conditioning_events_;\n"
        "  static float conditioning_efficiency_pct_;\n"
        "  static bool conditioning_stop_requested_;\n"
        "  static uint8_t last_point_result_code_;",
    )
    ensure(
        path,
        "static bool start_conditioning();",
        "  static void enter_wait_temp();\n"
        "  static bool start_current_point();",
        "  static void enter_wait_temp();\n"
        "  static bool start_conditioning();\n"
        "  static void handle_conditioning_result();\n"
        "  static bool start_current_point();",
    )


def patch_campaign_source() -> None:
    path = "Marlin/src/feature/makeit_fa_campaign.cpp"

    ensure(
        path,
        "float MakeItFASpeedCampaign::default_conditioning_mm_",
        "float MakeItFASpeedCampaign::last_pass_feed_mm_min_ = 0.0f;\n"
        "float MakeItFASpeedCampaign::first_fail_feed_mm_min_ = 0.0f;",
        "float MakeItFASpeedCampaign::last_pass_feed_mm_min_ = 0.0f;\n"
        "float MakeItFASpeedCampaign::first_fail_feed_mm_min_ = 0.0f;\n"
        "float MakeItFASpeedCampaign::default_conditioning_mm_ = 20.0f;\n"
        "uint32_t MakeItFASpeedCampaign::conditioning_started_ms_ = 0;\n"
        "uint32_t MakeItFASpeedCampaign::conditioning_events_ = 0;\n"
        "float MakeItFASpeedCampaign::conditioning_efficiency_pct_ = 0.0f;\n"
        "bool MakeItFASpeedCampaign::conditioning_stop_requested_ = false;",
    )
    ensure(
        path,
        "float_bits(p.conditioning_mm)",
        "  hash = hash_word(hash, p.settle_seconds);\n"
        "  hash = hash_word(hash, float_bits(p.point.total_mm));",
        "  hash = hash_word(hash, p.settle_seconds);\n"
        "  hash = hash_word(hash, float_bits(p.conditioning_mm));\n"
        "  hash = hash_word(hash, float_bits(p.point.total_mm));",
    )
    ensure(
        path,
        'case CAM_CONDITIONING: return "CONDITIONING";',
        '    case CAM_WAIT_TEMP:     return "WAIT_TEMP";\n'
        '    case CAM_RUNNING_POINT: return "RUNNING_POINT";',
        '    case CAM_WAIT_TEMP:     return "WAIT_TEMP";\n'
        '    case CAM_CONDITIONING:  return "CONDITIONING";\n'
        '    case CAM_RUNNING_POINT: return "RUNNING_POINT";',
    )
    ensure(
        path,
        'SERIAL_ECHOPGM(" conditioning_mm=");',
        '  SERIAL_ECHOPGM(" feed_mm_min="); SERIAL_ECHO(current_feed_mm_min_);\n'
        '  SERIAL_ECHOPGM(" last_pass=");',
        '  SERIAL_ECHOPGM(" feed_mm_min="); SERIAL_ECHO(current_feed_mm_min_);\n'
        '  SERIAL_ECHOPGM(" conditioning_mm="); SERIAL_ECHO(params_.conditioning_mm);\n'
        '  SERIAL_ECHOPGM(" conditioning_events="); SERIAL_ECHO(conditioning_events_);\n'
        '  SERIAL_ECHOPGM(" conditioning_eff="); SERIAL_ECHO(conditioning_efficiency_pct_);\n'
        '  SERIAL_ECHOPGM(" conditioning_started_ms="); SERIAL_ECHO(conditioning_started_ms_);\n'
        '  SERIAL_ECHOPGM(" last_pass=");',
    )

    config_block = r'''
void MakeItFASpeedCampaign::set_default_conditioning_mm(const float requested_mm) {
  default_conditioning_mm_ = requested_mm <= 0.0f
    ? 0.0f
    : constrain(requested_mm, 10.0f, 100.0f);
  report_conditioning_config();
}

void MakeItFASpeedCampaign::report_conditioning_config() {
  SERIAL_ECHOPGM("FA10: conditioning_mm="); SERIAL_ECHO(default_conditioning_mm_);
  SERIAL_ECHOPGM(" speed_mode=same_as_point");
  SERIAL_ECHOPGM(" counter_reset_before_measure=1");
  SERIAL_ECHOPGM(" thermal_dwell_after_conditioning=0");
  SERIAL_ECHOLNPGM("");
}

'''
    ensure(
        path,
        "void MakeItFASpeedCampaign::set_default_conditioning_mm",
        "void MakeItFASpeedCampaign::finish(const State terminal_state, const char *tag) {",
        config_block + "void MakeItFASpeedCampaign::finish(const State terminal_state, const char *tag) {",
    )

    conditioning_block = r'''
bool MakeItFASpeedCampaign::start_conditioning() {
  if (params_.conditioning_mm <= 0.0f)
    return start_current_point();

  if (makeit_fa_phase0.segmented_feed_active() || makeit_fa_phase0.test_point_active())
    return false;

  makeit_fa_phase0.reset_encoder();
  conditioning_started_ms_ = millis();
  conditioning_events_ = 0;
  conditioning_efficiency_pct_ = 0.0f;
  conditioning_stop_requested_ = false;

  if (!makeit_fa_phase0.run_segmented_feed_test(
        params_.conditioning_mm,
        current_feed_mm_min_,
        params_.point.segment_mm,
        params_.point.max_inflight,
        params_.point.report_ms
      ))
    return false;

  state_ = CAM_CONDITIONING;
  stable_since_ms_ = 0;
  report("conditioning_started");
  return true;
}

void MakeItFASpeedCampaign::handle_conditioning_result() {
  conditioning_events_ = makeit_fa_phase0.encoder_events();
  const float expected_events = params_.conditioning_mm * params_.point.encoder_events_per_mm;
  conditioning_efficiency_pct_ = expected_events > 0.0f
    ? float(conditioning_events_) * 100.0f / expected_events
    : 0.0f;

  report("conditioning_done");

  if (cancel_requested_) {
    finish(CAM_ABORTED, "cancelled_conditioning");
    return;
  }

  const float temp_now = thermalManager.degHotend(0);
  if (ABS(temp_now - target_temp_) > params_.point.temp_tolerance) {
    last_point_result_code_ = MakeItFilamentAnalyzerPhase0::TP_RESULT_INVALID_TEMP;
    last_point_result_crc_ = 0;
    finish(CAM_INVALID_TEMP, "conditioning_invalid_temp");
    return;
  }

  if (params_.point.auto_stop_enabled
      && conditioning_efficiency_pct_ < params_.point.monitor_efficiency_pct) {
    first_fail_feed_mm_min_ = current_feed_mm_min_;
    last_point_result_code_ = MakeItFilamentAnalyzerPhase0::TP_RESULT_LOW_FEED;
    last_point_result_crc_ = 0;
    finish(CAM_LIMIT_FOUND, "conditioning_limit");
    return;
  }

  // Analyzer idle runs before campaign idle. When the bounded conditioning feed
  // drains, this starts the measured point in the same main-loop iteration.
  // The encoder and all point statistics are reset by start_evaluated_test_point.
  if (!start_current_point())
    finish(CAM_ERROR, "point_start_after_conditioning_failed");
}

'''
    ensure(
        path,
        "bool MakeItFASpeedCampaign::start_conditioning()",
        "bool MakeItFASpeedCampaign::start_current_point() {",
        conditioning_block + "bool MakeItFASpeedCampaign::start_current_point() {",
    )

    ensure(
        path,
        "conditioning_events_ = 0;\n  conditioning_efficiency_pct_ = 0.0f;\n  conditioning_stop_requested_ = false;\n  next_report_ms_ = millis();",
        "  stable_temp_max_c_ = temp_now;\n"
        "  next_report_ms_ = millis();",
        "  stable_temp_max_c_ = temp_now;\n"
        "  conditioning_started_ms_ = 0;\n"
        "  conditioning_events_ = 0;\n"
        "  conditioning_efficiency_pct_ = 0.0f;\n"
        "  conditioning_stop_requested_ = false;\n"
        "  next_report_ms_ = millis();",
    )
    ensure(
        path,
        "normalized.conditioning_mm",
        "  normalized.settle_seconds = constrain(normalized.settle_seconds, uint16_t(0), uint16_t(120));\n\n"
        "  normalized.point.total_mm",
        "  normalized.settle_seconds = constrain(normalized.settle_seconds, uint16_t(0), uint16_t(120));\n"
        "  normalized.conditioning_mm = normalized.conditioning_mm <= 0.0f\n"
        "    ? 0.0f : constrain(normalized.conditioning_mm, 10.0f, 100.0f);\n\n"
        "  normalized.point.total_mm",
    )
    ensure(
        path,
        "  first_fail_feed_mm_min_ = 0.0f;\n  conditioning_started_ms_ = 0;",
        "  last_pass_feed_mm_min_ = 0.0f;\n"
        "  first_fail_feed_mm_min_ = 0.0f;\n"
        "  last_point_result_code_ = 0;",
        "  last_pass_feed_mm_min_ = 0.0f;\n"
        "  first_fail_feed_mm_min_ = 0.0f;\n"
        "  conditioning_started_ms_ = 0;\n"
        "  conditioning_events_ = 0;\n"
        "  conditioning_efficiency_pct_ = 0.0f;\n"
        "  conditioning_stop_requested_ = false;\n"
        "  last_point_result_code_ = 0;",
    )
    ensure(
        path,
        "const bool started = params_.conditioning_mm",
        '        if (!start_current_point())\n'
        '          finish(CAM_ERROR, "point_start_failed");\n'
        '        return;',
        '        const bool started = params_.conditioning_mm > 0.0f\n'
        '          ? start_conditioning()\n'
        '          : start_current_point();\n'
        '        if (!started)\n'
        '          finish(CAM_ERROR, "point_start_failed");\n'
        '        return;',
    )

    conditioning_idle = r'''
  if (state_ == CAM_CONDITIONING) {
    if (cancel_requested_ && !conditioning_stop_requested_) {
      conditioning_stop_requested_ = makeit_fa_phase0.request_segmented_feed_stop();
      report("conditioning_stop_requested");
    }

    if (makeit_fa_phase0.segmented_feed_active())
      return;

    handle_conditioning_result();
    return;
  }

'''
    ensure(
        path,
        "if (state_ == CAM_CONDITIONING) {",
        "  if (state_ != CAM_RUNNING_POINT)\n"
        "    return;",
        conditioning_idle + "  if (state_ != CAM_RUNNING_POINT)\n    return;",
    )

    conditioning_cancel = r'''
  if (state_ == CAM_CONDITIONING) {
    cancel_requested_ = true;
    conditioning_stop_requested_ = makeit_fa_phase0.request_segmented_feed_stop();
    report("cancel_requested");
    return true;
  }

'''
    ensure(
        path,
        "if (state_ == CAM_CONDITIONING) {\n    cancel_requested_ = true;",
        "  if (state_ != CAM_RUNNING_POINT) {\n"
        '    SERIAL_ECHOPGM("FA7: error=NOT_ACTIVE state="); SERIAL_ECHO(state_name(state_));',
        conditioning_cancel
        + "  if (state_ != CAM_RUNNING_POINT) {\n"
        + '    SERIAL_ECHOPGM("FA7: error=NOT_ACTIVE state="); SERIAL_ECHO(state_name(state_));',
    )


def patch_envelope_source() -> None:
    path = "Marlin/src/feature/makeit_fa_envelope.cpp"

    ensure(
        path,
        "float_bits(p.speed.conditioning_mm)",
        "  hash = hash_word(hash, p.speed.settle_seconds);\n"
        "  hash = hash_word(hash, float_bits(p.speed.point.total_mm));",
        "  hash = hash_word(hash, p.speed.settle_seconds);\n"
        "  hash = hash_word(hash, float_bits(p.speed.conditioning_mm));\n"
        "  hash = hash_word(hash, float_bits(p.speed.point.total_mm));",
    )
    ensure(
        path,
        "p.conditioning_mm = p.conditioning_mm <= 0.0f",
        "  p.settle_seconds = constrain(p.settle_seconds, uint16_t(0), uint16_t(120));\n"
        "  p.point.total_mm",
        "  p.settle_seconds = constrain(p.settle_seconds, uint16_t(0), uint16_t(120));\n"
        "  p.conditioning_mm = p.conditioning_mm <= 0.0f\n"
        "    ? 0.0f : constrain(p.conditioning_mm, 10.0f, 100.0f);\n"
        "  p.point.total_mm",
    )
    ensure(
        path,
        'SERIAL_ECHOPGM(" conditioning_mm="); SERIAL_ECHO(params_.speed.conditioning_mm);',
        '  SERIAL_ECHOPGM(" speed_points="); SERIAL_ECHO(speed_points_per_row_);',
        '  SERIAL_ECHOPGM(" speed_points="); SERIAL_ECHO(speed_points_per_row_);\n'
        '  SERIAL_ECHOPGM(" conditioning_mm="); SERIAL_ECHO(params_.speed.conditioning_mm);',
    )


def patch_gcode_sources() -> None:
    for path in ("Marlin/src/gcode/feature/M872.cpp", "Marlin/src/gcode/feature/M870.cpp"):
        ensure(
            path,
            "campaign.conditioning_mm = MakeItFASpeedCampaign::default_conditioning_mm();",
            "  campaign.settle_seconds = parser.seenval('O') ? (uint16_t)parser.value_int() : 5;",
            "  campaign.settle_seconds = parser.seenval('O') ? (uint16_t)parser.value_int() : 5;\n"
            "  campaign.conditioning_mm = MakeItFASpeedCampaign::default_conditioning_mm();",
        )


def patch_gcode_hooks() -> None:
    path = "Marlin/src/gcode/gcode.h"
    ensure(
        path,
        "static void M881();",
        "    static void M880();",
        "    static void M880();\n"
        "    static void M881();",
    )

    path = "Marlin/src/gcode/gcode.cpp"
    ensure(
        path,
        "case 881: M881(); break;",
        "        case 880: M880(); break;                                  // M880: MAKEiT recovery / re-prime",
        "        case 880: M880(); break;                                  // M880: MAKEiT recovery / re-prime\n"
        "        case 881: M881(); break;                                  // M881: MAKEiT point-conditioning configuration",
    )


def verify_sources() -> None:
    required = {
        "Marlin/src/gcode/feature/M881.cpp": "GcodeSuite::M881",
        "Marlin/src/feature/makeit_fa_campaign.cpp": "settle_band_c_",
        "Marlin/src/feature/makeit_fa_envelope.cpp": "recovery_temp_c",
    }
    for path, needle in required.items():
        if needle not in read(path):
            raise RuntimeError(f"{path} is not ready for Phase 10; missing {needle!r}")
        print(f"ok {path}: contains {needle!r}")


def main() -> int:
    verify_sources()
    patch_campaign_header()
    patch_campaign_source()
    patch_envelope_source()
    patch_gcode_sources()
    patch_gcode_hooks()
    print("Phase-10 same-speed flow conditioning is in place.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
