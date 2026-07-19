#!/usr/bin/env python3
"""Add Phase-9 recovery state, telemetry, and helper methods to M870."""

from phase9_patch_common import ensure


def patch_header() -> None:
    path = "Marlin/src/feature/makeit_fa_envelope.h"

    ensure(
        path,
        "ENV_WAIT_RECOVERY_TEMP",
        """  enum State : uint8_t {
    ENV_EMPTY = 0,
    ENV_RUNNING_ROW,
    ENV_COMPLETE,
    ENV_LIMIT_FOUND,
    ENV_INVALID_TEMP,
    ENV_ABORTED,
    ENV_ERROR
  };""",
        """  enum State : uint8_t {
    ENV_EMPTY = 0,
    ENV_RUNNING_ROW,
    ENV_WAIT_RECOVERY_TEMP,
    ENV_RUNNING_RECOVERY,
    ENV_COMPLETE,
    ENV_LIMIT_FOUND,
    ENV_RECOVERY_FAILED,
    ENV_INVALID_TEMP,
    ENV_ABORTED,
    ENV_ERROR
  };""",
    )

    ensure(
        path,
        "uint32_t recovery_point_id;",
        """    uint32_t row_campaign_id;
    uint32_t last_point_crc;
    uint8_t campaign_state;
    uint8_t last_point_result_code;
  };""",
        """    uint32_t row_campaign_id;
    uint32_t last_point_crc;
    uint32_t recovery_point_id;
    uint32_t recovery_crc;
    uint8_t campaign_state;
    uint8_t last_point_result_code;
    uint8_t recovery_result_code;
    bool recovery_required;
  };""",
    )

    ensure(
        path,
        "state_ == ENV_WAIT_RECOVERY_TEMP",
        "  static bool active() { return state_ == ENV_RUNNING_ROW; }",
        """  static bool active() {
    return state_ == ENV_RUNNING_ROW
        || state_ == ENV_WAIT_RECOVERY_TEMP
        || state_ == ENV_RUNNING_RECOVERY;
  }""",
    )

    ensure(
        path,
        "static bool recovery_required_;",
        """  static float original_target_temp_c_;
  static float current_temp_c_;
  static MakeItFATemperatureEnvelopeParams params_;""",
        """  static float original_target_temp_c_;
  static float current_temp_c_;
  static bool recovery_required_;
  static uint32_t recovery_point_id_;
  static uint8_t recovery_result_code_;
  static uint32_t recovery_result_crc_;
  static uint32_t recovery_stable_since_ms_;
  static uint32_t recovery_next_report_ms_;
  static float recovery_settle_band_c_;
  static float recovery_stable_temp_min_c_;
  static float recovery_stable_temp_max_c_;
  static MakeItFATemperatureEnvelopeParams params_;""",
    )

    ensure(
        path,
        "static uint32_t row_campaign_id(uint8_t row);",
        """  static void normalize_speed_params(MakeItFASpeedCampaignParams &params);
  static bool start_current_row();
  static void handle_row_result();""",
        """  static void normalize_speed_params(MakeItFASpeedCampaignParams &params);
  static uint32_t row_campaign_id(uint8_t row);
  static uint32_t recovery_point_id_before_row(uint8_t row);
  static void clear_row_recovery();
  static bool prepare_recovery_for_current_row();
  static void service_recovery_wait();
  static bool start_recovery_point();
  static void handle_recovery_result();
  static void report_recovery(const char *tag);
  static bool start_current_row();
  static void handle_row_result();""",
    )


def patch_source_structure() -> None:
    path = "Marlin/src/feature/makeit_fa_envelope.cpp"

    ensure(
        path,
        '#include "makeit_filament_analyzer_phase0.h"',
        '''#include "makeit_fa_envelope.h"
#include "makeit_fa_transaction.h"''',
        '''#include "makeit_fa_envelope.h"
#include "makeit_fa_transaction.h"
#include "makeit_filament_analyzer_phase0.h"''',
    )

    ensure(
        path,
        "bool MakeItFATemperatureEnvelope::recovery_required_",
        """float MakeItFATemperatureEnvelope::original_target_temp_c_ = 0.0f;
float MakeItFATemperatureEnvelope::current_temp_c_ = 0.0f;
MakeItFATemperatureEnvelopeParams MakeItFATemperatureEnvelope::params_ = {};""",
        """float MakeItFATemperatureEnvelope::original_target_temp_c_ = 0.0f;
float MakeItFATemperatureEnvelope::current_temp_c_ = 0.0f;
bool MakeItFATemperatureEnvelope::recovery_required_ = false;
uint32_t MakeItFATemperatureEnvelope::recovery_point_id_ = 0;
uint8_t MakeItFATemperatureEnvelope::recovery_result_code_ = 0;
uint32_t MakeItFATemperatureEnvelope::recovery_result_crc_ = 0;
uint32_t MakeItFATemperatureEnvelope::recovery_stable_since_ms_ = 0;
uint32_t MakeItFATemperatureEnvelope::recovery_next_report_ms_ = 0;
float MakeItFATemperatureEnvelope::recovery_settle_band_c_ = 1.0f;
float MakeItFATemperatureEnvelope::recovery_stable_temp_min_c_ = 0.0f;
float MakeItFATemperatureEnvelope::recovery_stable_temp_max_c_ = 0.0f;
MakeItFATemperatureEnvelopeParams MakeItFATemperatureEnvelope::params_ = {};""",
    )

    ensure(
        path,
        'case ENV_WAIT_RECOVERY_TEMP: return "WAIT_RECOVERY_TEMP";',
        """    case ENV_RUNNING_ROW:  return "RUNNING_ROW";
    case ENV_COMPLETE:     return "COMPLETE";
    case ENV_LIMIT_FOUND:  return "LIMIT_FOUND";
    case ENV_INVALID_TEMP: return "INVALID_TEMP";""",
        """    case ENV_RUNNING_ROW:       return "RUNNING_ROW";
    case ENV_WAIT_RECOVERY_TEMP: return "WAIT_RECOVERY_TEMP";
    case ENV_RUNNING_RECOVERY:  return "RUNNING_RECOVERY";
    case ENV_COMPLETE:          return "COMPLETE";
    case ENV_LIMIT_FOUND:       return "LIMIT_FOUND";
    case ENV_RECOVERY_FAILED:   return "RECOVERY_FAILED";
    case ENV_INVALID_TEMP:      return "INVALID_TEMP";""",
    )

    ensure(
        path,
        'SERIAL_ECHOPGM(" recovery_required=");',
        """  SERIAL_ECHOPGM(" point_result="); SERIAL_ECHO(r.last_point_result_code);
  SERIAL_ECHOPGM(" point_crc="); SERIAL_ECHO(r.last_point_crc);
  SERIAL_ECHOLNPGM("");""",
        """  SERIAL_ECHOPGM(" point_result="); SERIAL_ECHO(r.last_point_result_code);
  SERIAL_ECHOPGM(" point_crc="); SERIAL_ECHO(r.last_point_crc);
  SERIAL_ECHOPGM(" recovery_required="); SERIAL_ECHO(r.recovery_required ? 1 : 0);
  SERIAL_ECHOPGM(" recovery_point_id="); SERIAL_ECHO(r.recovery_point_id);
  SERIAL_ECHOPGM(" recovery_result="); SERIAL_ECHO(r.recovery_result_code);
  SERIAL_ECHOPGM(" recovery_crc="); SERIAL_ECHO(r.recovery_crc);
  SERIAL_ECHOLNPGM("");""",
    )

    ensure(
        path,
        'SERIAL_ECHOPGM(" recovery_point_id="); SERIAL_ECHO(recovery_point_id_);',
        """  SERIAL_ECHOPGM(" speed_points="); SERIAL_ECHO(speed_points_per_row_);
  SERIAL_ECHOPGM(" cancel="); SERIAL_ECHO(cancel_requested_ ? 1 : 0);""",
        """  SERIAL_ECHOPGM(" speed_points="); SERIAL_ECHO(speed_points_per_row_);
  SERIAL_ECHOPGM(" recovery_required="); SERIAL_ECHO(recovery_required_ ? 1 : 0);
  SERIAL_ECHOPGM(" recovery_point_id="); SERIAL_ECHO(recovery_point_id_);
  SERIAL_ECHOPGM(" recovery_result="); SERIAL_ECHO(recovery_result_code_);
  SERIAL_ECHOPGM(" recovery_crc="); SERIAL_ECHO(recovery_result_crc_);
  SERIAL_ECHOPGM(" cancel="); SERIAL_ECHO(cancel_requested_ ? 1 : 0);""",
    )

    ensure(
        path,
        "crc = crc32_word(crc, r.recovery_point_id);",
        """    crc = crc32_word(crc, r.last_point_crc);
    crc = crc32_word(crc, r.campaign_state);
    crc = crc32_word(crc, r.last_point_result_code);""",
        """    crc = crc32_word(crc, r.last_point_crc);
    crc = crc32_word(crc, r.recovery_point_id);
    crc = crc32_word(crc, r.recovery_crc);
    crc = crc32_word(crc, r.campaign_state);
    crc = crc32_word(crc, r.last_point_result_code);
    crc = crc32_word(crc, r.recovery_result_code);
    crc = crc32_word(crc, r.recovery_required ? 1UL : 0UL);""",
    )


def patch_source_helpers() -> None:
    path = "Marlin/src/feature/makeit_fa_envelope.cpp"

    helper_block = r'''
uint32_t MakeItFATemperatureEnvelope::row_campaign_id(const uint8_t row) {
  return envelope_id_ + uint32_t(row) * uint32_t(speed_points_per_row_ + 1U);
}

uint32_t MakeItFATemperatureEnvelope::recovery_point_id_before_row(const uint8_t row) {
  return row ? row_campaign_id(row) - 1UL : 0UL;
}

void MakeItFATemperatureEnvelope::clear_row_recovery() {
  recovery_required_ = false;
  recovery_point_id_ = 0;
  recovery_result_code_ = 0;
  recovery_result_crc_ = 0;
  recovery_stable_since_ms_ = 0;
  recovery_next_report_ms_ = 0;
  recovery_settle_band_c_ = 1.0f;
  recovery_stable_temp_min_c_ = 0.0f;
  recovery_stable_temp_max_c_ = 0.0f;
}

void MakeItFATemperatureEnvelope::report_recovery(const char *tag) {
  const uint32_t now = millis();
  const uint32_t stable_ms = recovery_stable_since_ms_ ? now - recovery_stable_since_ms_ : 0;
  const float stable_span_c = recovery_stable_since_ms_
    ? recovery_stable_temp_max_c_ - recovery_stable_temp_min_c_ : 0.0f;

  SERIAL_ECHOPGM("FA9: tag="); SERIAL_ECHO(tag);
  SERIAL_ECHOPGM(" envelope_id="); SERIAL_ECHO(envelope_id_);
  SERIAL_ECHOPGM(" row="); SERIAL_ECHO(row_index_);
  SERIAL_ECHOPGM(" temp_c="); SERIAL_ECHO(current_temp_c_);
  SERIAL_ECHOPGM(" recovery_point_id="); SERIAL_ECHO(recovery_point_id_);
  SERIAL_ECHOPGM(" state="); SERIAL_ECHO(state_name(state_));
  SERIAL_ECHOPGM(" temp="); SERIAL_ECHO(thermalManager.degHotend(0));
  SERIAL_ECHOPGM(" target="); SERIAL_ECHO(thermalManager.degTargetHotend(0));
  SERIAL_ECHOPGM(" stable_ms="); SERIAL_ECHO(stable_ms);
  SERIAL_ECHOPGM(" settle_band_c="); SERIAL_ECHO(recovery_settle_band_c_);
  SERIAL_ECHOPGM(" stable_span_c="); SERIAL_ECHO(stable_span_c);
  SERIAL_ECHOPGM(" result_code="); SERIAL_ECHO(recovery_result_code_);
  SERIAL_ECHOPGM(" result_crc="); SERIAL_ECHO(recovery_result_crc_);
  SERIAL_ECHOLNPGM("");
}

bool MakeItFATemperatureEnvelope::prepare_recovery_for_current_row() {
  if (!row_index_ || row_index_ >= row_count_) return false;
  if (makeit_fa_campaign.active() || makeit_fa_transaction.running()) return false;

  recovery_required_ = true;
  recovery_point_id_ = recovery_point_id_before_row(row_index_);
  recovery_result_code_ = 0;
  recovery_result_crc_ = 0;
  recovery_stable_since_ms_ = 0;
  recovery_next_report_ms_ = millis();
  recovery_settle_band_c_ = _MIN(params_.speed.point.temp_tolerance, 1.0f);

  current_temp_c_ = params_.start_temp_c + float(row_index_) * params_.temp_step_c;
  current_row_campaign_id_ = row_campaign_id(row_index_);
  thermalManager.setTargetHotend(celsius_t(current_temp_c_ + 0.5f), 0);

  const float temp_now = thermalManager.degHotend(0);
  recovery_stable_temp_min_c_ = temp_now;
  recovery_stable_temp_max_c_ = temp_now;
  state_ = ENV_WAIT_RECOVERY_TEMP;
  report_recovery("wait_started");
  report("recovery_wait");
  return true;
}

bool MakeItFATemperatureEnvelope::start_recovery_point() {
  MakeItFAPointParams recovery = params_.speed.point;
  recovery.total_mm = 30.0f;
  recovery.feed_mm_min = _MIN(params_.speed.start_feed_mm_min, 100.0f);
  recovery.pass_efficiency_pct = _MIN(params_.speed.point.pass_efficiency_pct, 90.0f);
  recovery.auto_stop_enabled = true;
  recovery.monitor_window_mm = 10.0f;
  recovery.monitor_efficiency_pct = _MIN(params_.speed.point.monitor_efficiency_pct, 85.0f);
  recovery.monitor_confirm_windows = 2;

  if (!makeit_fa_transaction.execute(recovery_point_id_, recovery))
    return false;

  state_ = ENV_RUNNING_RECOVERY;
  report_recovery("started");
  report("recovery_started");
  return true;
}

void MakeItFATemperatureEnvelope::service_recovery_wait() {
  const uint32_t now = millis();
  const float target_now = thermalManager.degTargetHotend(0);
  const float temp_now = thermalManager.degHotend(0);

  if (ABS(target_now - current_temp_c_) > 0.5f) {
    finish(ENV_ERROR, "recovery_target_changed");
    return;
  }

  const bool inside_band = ABS(temp_now - current_temp_c_) <= recovery_settle_band_c_;
  if (inside_band && thermalManager.hotEnoughToExtrude(0)) {
    if (!recovery_stable_since_ms_) {
      recovery_stable_since_ms_ = now;
      recovery_stable_temp_min_c_ = recovery_stable_temp_max_c_ = temp_now;
    }
    else {
      if (temp_now < recovery_stable_temp_min_c_) recovery_stable_temp_min_c_ = temp_now;
      if (temp_now > recovery_stable_temp_max_c_) recovery_stable_temp_max_c_ = temp_now;
      if (recovery_stable_temp_max_c_ - recovery_stable_temp_min_c_ > recovery_settle_band_c_ + 0.001f) {
        recovery_stable_since_ms_ = now;
        recovery_stable_temp_min_c_ = recovery_stable_temp_max_c_ = temp_now;
      }
    }

    const uint32_t required_ms = uint32_t(params_.speed.settle_seconds) * 1000UL;
    const float stable_span_c = recovery_stable_temp_max_c_ - recovery_stable_temp_min_c_;
    if (uint32_t(now - recovery_stable_since_ms_) >= required_ms
        && stable_span_c <= recovery_settle_band_c_ + 0.001f) {
      if (!start_recovery_point())
        finish(ENV_ERROR, "recovery_start_failed");
      return;
    }
  }
  else {
    recovery_stable_since_ms_ = 0;
    recovery_stable_temp_min_c_ = recovery_stable_temp_max_c_ = temp_now;
  }

  if ((int32_t)(now - recovery_next_report_ms_) >= 0) {
    recovery_next_report_ms_ = now + 1000UL;
    report_recovery("wait_temp");
  }
}

void MakeItFATemperatureEnvelope::handle_recovery_result() {
  recovery_result_code_ = makeit_fa_transaction.result_code();
  recovery_result_crc_ = makeit_fa_transaction.result_crc();
  report_recovery("result");

  if (makeit_fa_transaction.state() == MakeItFATransaction::TX_ABORTED
      || recovery_result_code_ == MakeItFilamentAnalyzerPhase0::TP_RESULT_ABORTED) {
    finish(ENV_ABORTED, "recovery_aborted");
    return;
  }

  switch (recovery_result_code_) {
    case MakeItFilamentAnalyzerPhase0::TP_RESULT_PASS:
      report_recovery("passed");
      if (!start_current_row())
        finish(ENV_ERROR, "row_start_after_recovery_failed");
      break;

    case MakeItFilamentAnalyzerPhase0::TP_RESULT_LOW_FEED:
      finish(ENV_RECOVERY_FAILED, "recovery_low_feed");
      break;

    case MakeItFilamentAnalyzerPhase0::TP_RESULT_INVALID_TEMP:
      finish(ENV_INVALID_TEMP, "recovery_invalid_temp");
      break;

    default:
      finish(ENV_ERROR, "recovery_error");
      break;
  }
}

'''

    ensure(
        path,
        "uint32_t MakeItFATemperatureEnvelope::row_campaign_id",
        "bool MakeItFATemperatureEnvelope::start_current_row() {",
        helper_block + "bool MakeItFATemperatureEnvelope::start_current_row() {",
    )

    ensure(
        path,
        "bool MakeItFATemperatureEnvelope::start_current_row() {\n  if (row_index_ >= row_count_) return false;\n  if (makeit_fa_campaign.active() || makeit_fa_transaction.running()) return false;\n\n  current_temp_c_ = params_.start_temp_c + float(row_index_) * params_.temp_step_c;\n  current_row_campaign_id_ = row_campaign_id(row_index_);",
        """bool MakeItFATemperatureEnvelope::start_current_row() {
  if (row_index_ >= row_count_) return false;
  if (makeit_fa_campaign.active() || makeit_fa_transaction.running()) return false;

  current_temp_c_ = params_.start_temp_c + float(row_index_) * params_.temp_step_c;
  current_row_campaign_id_ = envelope_id_ + uint32_t(row_index_) * uint32_t(speed_points_per_row_);""",
        """bool MakeItFATemperatureEnvelope::start_current_row() {
  if (row_index_ >= row_count_) return false;
  if (makeit_fa_campaign.active() || makeit_fa_transaction.running()) return false;

  current_temp_c_ = params_.start_temp_c + float(row_index_) * params_.temp_step_c;
  current_row_campaign_id_ = row_campaign_id(row_index_);""",
    )


def main() -> int:
    patch_header()
    patch_source_structure()
    patch_source_helpers()
    print("Phase-9 recovery structure is in place.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
