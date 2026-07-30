/**
 * MAKEiT Filament Analyzer - Phase 7 fixed-temperature speed campaign
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "makeit_fa_campaign.h"
#include "makeit_filament_analyzer_phase0.h"
#include "../core/serial.h"
#include "../module/temperature.h"
#include "../MarlinCore.h"

MakeItFASpeedCampaign::State MakeItFASpeedCampaign::state_ = MakeItFASpeedCampaign::CAM_EMPTY;
bool MakeItFASpeedCampaign::record_valid_ = false;
bool MakeItFASpeedCampaign::cancel_requested_ = false;
uint32_t MakeItFASpeedCampaign::campaign_id_ = 0;
uint32_t MakeItFASpeedCampaign::params_hash_ = 0;
uint16_t MakeItFASpeedCampaign::point_index_ = 0;
uint16_t MakeItFASpeedCampaign::point_count_ = 0;
uint32_t MakeItFASpeedCampaign::current_point_id_ = 0;
uint32_t MakeItFASpeedCampaign::started_ms_ = 0;
uint32_t MakeItFASpeedCampaign::finished_ms_ = 0;
uint32_t MakeItFASpeedCampaign::stable_since_ms_ = 0;
uint32_t MakeItFASpeedCampaign::next_report_ms_ = 0;
float MakeItFASpeedCampaign::target_temp_ = 0.0f;
float MakeItFASpeedCampaign::current_feed_mm_min_ = 0.0f;
float MakeItFASpeedCampaign::last_pass_feed_mm_min_ = 0.0f;
float MakeItFASpeedCampaign::first_fail_feed_mm_min_ = 0.0f;
uint8_t MakeItFASpeedCampaign::last_point_result_code_ = 0;
uint32_t MakeItFASpeedCampaign::last_point_result_crc_ = 0;
MakeItFASpeedCampaignParams MakeItFASpeedCampaign::params_ = {};

MakeItFASpeedCampaign makeit_fa_campaign;

uint32_t MakeItFASpeedCampaign::float_bits(const float value) {
  union FloatBits {
    float f;
    uint32_t u;
  } bits;
  bits.f = value;
  return bits.u;
}

uint32_t MakeItFASpeedCampaign::hash_word(uint32_t hash, const uint32_t word) {
  for (uint8_t i = 0; i < 4; ++i) {
    hash ^= uint8_t(word >> (i * 8));
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t MakeItFASpeedCampaign::hash_parameters(const MakeItFASpeedCampaignParams &p) {
  uint32_t hash = 2166136261UL;
  hash = hash_word(hash, float_bits(p.start_feed_mm_min));
  hash = hash_word(hash, float_bits(p.max_feed_mm_min));
  hash = hash_word(hash, float_bits(p.step_feed_mm_min));
  hash = hash_word(hash, p.settle_seconds);
  hash = hash_word(hash, float_bits(p.point.total_mm));
  hash = hash_word(hash, float_bits(p.point.segment_mm));
  hash = hash_word(hash, p.point.max_inflight);
  hash = hash_word(hash, p.point.report_ms);
  hash = hash_word(hash, float_bits(p.point.encoder_events_per_mm));
  hash = hash_word(hash, float_bits(p.point.pass_efficiency_pct));
  hash = hash_word(hash, float_bits(p.point.temp_tolerance));
  hash = hash_word(hash, p.point.auto_stop_enabled ? 1UL : 0UL);
  hash = hash_word(hash, float_bits(p.point.monitor_window_mm));
  hash = hash_word(hash, float_bits(p.point.monitor_efficiency_pct));
  hash = hash_word(hash, p.point.monitor_confirm_windows);
  hash = hash_word(hash, float_bits(p.point.pulse_gap_factor));
  hash = hash_word(hash, p.point.pulse_gap_min_ms);
  hash = hash_word(hash, float_bits(p.point.pulse_gap_min_missing_events));
  return hash;
}

uint16_t MakeItFASpeedCampaign::count_points(const float start_feed, const float max_feed, const float step_feed) {
  if (start_feed <= 0.0f || max_feed < start_feed || step_feed <= 0.0f)
    return 0;

  uint16_t count = 0;
  while (count < 101) {
    const float feed = start_feed + float(count) * step_feed;
    if (feed > max_feed + 0.0001f) break;
    ++count;
  }

  return count <= 100 ? count : 0;
}

const char* MakeItFASpeedCampaign::state_name(const State state) {
  switch (state) {
    case CAM_WAIT_TEMP:     return "WAIT_TEMP";
    case CAM_RUNNING_POINT: return "RUNNING_POINT";
    case CAM_COMPLETE:      return "COMPLETE";
    case CAM_LIMIT_FOUND:   return "LIMIT_FOUND";
    case CAM_INVALID_TEMP:  return "INVALID_TEMP";
    case CAM_ABORTED:       return "ABORTED";
    case CAM_ERROR:         return "ERROR";
    default:                return "EMPTY";
  }
}

void MakeItFASpeedCampaign::report(const char *tag) {
  const uint32_t now = millis();
  const uint32_t stable_ms = stable_since_ms_ ? now - stable_since_ms_ : 0;

  SERIAL_ECHOPGM("FA7: tag="); SERIAL_ECHO(tag);
  SERIAL_ECHOPGM(" campaign_id="); SERIAL_ECHO(campaign_id_);
  SERIAL_ECHOPGM(" params_hash="); SERIAL_ECHO(params_hash_);
  SERIAL_ECHOPGM(" state="); SERIAL_ECHO(state_name(state_));
  SERIAL_ECHOPGM(" index="); SERIAL_ECHO(point_index_);
  SERIAL_ECHOPGM(" points="); SERIAL_ECHO(point_count_);
  SERIAL_ECHOPGM(" point_id="); SERIAL_ECHO(current_point_id_);
  SERIAL_ECHOPGM(" feed_mm_min="); SERIAL_ECHO(current_feed_mm_min_);
  SERIAL_ECHOPGM(" last_pass="); SERIAL_ECHO(last_pass_feed_mm_min_);
  SERIAL_ECHOPGM(" first_fail="); SERIAL_ECHO(first_fail_feed_mm_min_);
  SERIAL_ECHOPGM(" point_result="); SERIAL_ECHO(last_point_result_code_);
  SERIAL_ECHOPGM(" point_crc="); SERIAL_ECHO(last_point_result_crc_);
  SERIAL_ECHOPGM(" target="); SERIAL_ECHO(target_temp_);
  SERIAL_ECHOPGM(" temp="); SERIAL_ECHO(thermalManager.degHotend(0));
  SERIAL_ECHOPGM(" stable_ms="); SERIAL_ECHO(stable_ms);
  SERIAL_ECHOPGM(" cancel="); SERIAL_ECHO(cancel_requested_ ? 1 : 0);
  SERIAL_ECHOPGM(" started_ms="); SERIAL_ECHO(started_ms_);
  SERIAL_ECHOPGM(" finished_ms="); SERIAL_ECHO(finished_ms_);
  SERIAL_ECHOLNPGM("");
}

void MakeItFASpeedCampaign::finish(const State terminal_state, const char *tag) {
  state_ = terminal_state;
  finished_ms_ = millis();
  stable_since_ms_ = 0;
  report(tag);
}

void MakeItFASpeedCampaign::enter_wait_temp() {
  state_ = CAM_WAIT_TEMP;
  stable_since_ms_ = 0;
  next_report_ms_ = millis();
}

bool MakeItFASpeedCampaign::start_current_point() {
  MakeItFAPointParams point = params_.point;
  point.feed_mm_min = current_feed_mm_min_;
  current_point_id_ = campaign_id_ + point_index_;

  if (!makeit_fa_transaction.execute(current_point_id_, point))
    return false;

  state_ = CAM_RUNNING_POINT;
  stable_since_ms_ = 0;
  report("point_started");
  return true;
}

void MakeItFASpeedCampaign::handle_point_result() {
  last_point_result_code_ = makeit_fa_transaction.result_code();
  last_point_result_crc_ = makeit_fa_transaction.result_crc();
  report("point_result");

  if (makeit_fa_transaction.state() == MakeItFATransaction::TX_ABORTED
      || last_point_result_code_ == MakeItFilamentAnalyzerPhase0::TP_RESULT_ABORTED) {
    finish(CAM_ABORTED, "aborted");
    return;
  }

  switch (last_point_result_code_) {
    case MakeItFilamentAnalyzerPhase0::TP_RESULT_PASS: {
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
    } break;

    case MakeItFilamentAnalyzerPhase0::TP_RESULT_LOW_FEED:
      first_fail_feed_mm_min_ = current_feed_mm_min_;
      finish(CAM_LIMIT_FOUND, "limit_found");
      break;

    case MakeItFilamentAnalyzerPhase0::TP_RESULT_INVALID_TEMP:
      finish(CAM_INVALID_TEMP, "invalid_temp");
      break;

    default:
      finish(CAM_ERROR, "point_error");
      break;
  }
}

bool MakeItFASpeedCampaign::start(const uint32_t campaign_id, const MakeItFASpeedCampaignParams &requested) {
  if (!campaign_id) {
    SERIAL_ECHOLNPGM("FA7: error=INVALID_CAMPAIGN_ID campaign_id=0");
    return false;
  }

  MakeItFASpeedCampaignParams normalized = requested;
  normalized.start_feed_mm_min = constrain(normalized.start_feed_mm_min, 1.0f, 2000.0f);
  normalized.max_feed_mm_min = constrain(normalized.max_feed_mm_min, 1.0f, 2000.0f);
  normalized.step_feed_mm_min = constrain(normalized.step_feed_mm_min, 1.0f, 1000.0f);
  normalized.settle_seconds = constrain(normalized.settle_seconds, uint16_t(0), uint16_t(120));

  normalized.point.total_mm = constrain(normalized.point.total_mm, 20.0f, 500.0f);
  normalized.point.feed_mm_min = normalized.start_feed_mm_min;
  normalized.point.segment_mm = constrain(normalized.point.segment_mm, 0.05f, 0.35f);
  normalized.point.max_inflight = constrain(normalized.point.max_inflight, uint8_t(1), uint8_t(2));
  normalized.point.report_ms = constrain(normalized.point.report_ms, uint16_t(50), uint16_t(5000));
  normalized.point.encoder_events_per_mm = constrain(normalized.point.encoder_events_per_mm, 0.01f, 100.0f);
  normalized.point.pass_efficiency_pct = constrain(normalized.point.pass_efficiency_pct, 50.0f, 105.0f);
  normalized.point.temp_tolerance = constrain(normalized.point.temp_tolerance, 0.5f, 15.0f);
  normalized.point.monitor_window_mm = constrain(normalized.point.monitor_window_mm, 5.0f, 100.0f);
  normalized.point.monitor_efficiency_pct = constrain(normalized.point.monitor_efficiency_pct, 50.0f, 105.0f);
  normalized.point.monitor_confirm_windows = constrain(normalized.point.monitor_confirm_windows, uint8_t(1), uint8_t(5));
  if (normalized.point.pulse_gap_factor > 0.0f)
    normalized.point.pulse_gap_factor = constrain(normalized.point.pulse_gap_factor, 1.5f, 20.0f);
  else
    normalized.point.pulse_gap_factor = 0.0f;
  normalized.point.pulse_gap_min_ms = constrain(normalized.point.pulse_gap_min_ms, uint16_t(50), uint16_t(30000));
  normalized.point.pulse_gap_min_missing_events = constrain(normalized.point.pulse_gap_min_missing_events, 0.5f, 20.0f);

  const uint16_t points = count_points(
    normalized.start_feed_mm_min,
    normalized.max_feed_mm_min,
    normalized.step_feed_mm_min
  );

  if (!points) {
    SERIAL_ECHOLNPGM("FA7: error=INVALID_SPEED_RANGE");
    return false;
  }

  if (campaign_id > 0xFFFFFFFFUL - uint32_t(points - 1)) {
    SERIAL_ECHOLNPGM("FA7: error=POINT_ID_OVERFLOW");
    return false;
  }

  const uint32_t hash = hash_parameters(normalized);

  if (record_valid_ && campaign_id == campaign_id_) {
    if (hash != params_hash_) {
      SERIAL_ECHOPGM("FA7: error=PARAM_CONFLICT campaign_id="); SERIAL_ECHO(campaign_id);
      SERIAL_ECHOPGM(" stored_hash="); SERIAL_ECHO(params_hash_);
      SERIAL_ECHOPGM(" requested_hash="); SERIAL_ECHO(hash);
      SERIAL_ECHOLNPGM("");
      return false;
    }

    report("replay");
    return true;
  }

  if (active() || makeit_fa_transaction.running()) {
    SERIAL_ECHOPGM("FA7: error=BUSY campaign_id="); SERIAL_ECHO(campaign_id_);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  if (makeit_fa_transaction.has_record()) {
    const uint32_t retained_point_id = makeit_fa_transaction.current_point_id();
    const uint32_t final_point_id = campaign_id + uint32_t(points - 1);
    if (retained_point_id >= campaign_id && retained_point_id <= final_point_id) {
      SERIAL_ECHOPGM("FA7: error=POINT_ID_RANGE_IN_USE retained_point_id="); SERIAL_ECHO(retained_point_id);
      SERIAL_ECHOPGM(" range_start="); SERIAL_ECHO(campaign_id);
      SERIAL_ECHOPGM(" range_end="); SERIAL_ECHO(final_point_id);
      SERIAL_ECHOLNPGM("");
      return false;
    }
  }

  target_temp_ = thermalManager.degTargetHotend(0);
  if (target_temp_ <= 0.0f) {
    SERIAL_ECHOLNPGM("FA7: error=NO_HOTEND_TARGET");
    return false;
  }

  params_ = normalized;
  state_ = CAM_WAIT_TEMP;
  record_valid_ = true;
  cancel_requested_ = false;
  campaign_id_ = campaign_id;
  params_hash_ = hash;
  point_index_ = 0;
  point_count_ = points;
  current_point_id_ = campaign_id;
  started_ms_ = millis();
  finished_ms_ = 0;
  stable_since_ms_ = 0;
  next_report_ms_ = started_ms_;
  current_feed_mm_min_ = params_.start_feed_mm_min;
  last_pass_feed_mm_min_ = 0.0f;
  first_fail_feed_mm_min_ = 0.0f;
  last_point_result_code_ = 0;
  last_point_result_crc_ = 0;

  report("started");
  return true;
}

void MakeItFASpeedCampaign::idle() {
  if (state_ == CAM_WAIT_TEMP) {
    const uint32_t now = millis();
    const float target_now = thermalManager.degTargetHotend(0);
    const float temp_now = thermalManager.degHotend(0);

    if (ABS(target_now - target_temp_) > 0.5f) {
      finish(CAM_ERROR, "target_changed");
      return;
    }

    if (ABS(temp_now - target_temp_) <= params_.point.temp_tolerance
        && thermalManager.hotEnoughToExtrude(0)) {
      if (!stable_since_ms_)
        stable_since_ms_ = now;

      const uint32_t required_ms = uint32_t(params_.settle_seconds) * 1000UL;
      if (uint32_t(now - stable_since_ms_) >= required_ms) {
        if (!start_current_point())
          finish(CAM_ERROR, "point_start_failed");
        return;
      }
    }
    else {
      stable_since_ms_ = 0;
    }

    if ((int32_t)(now - next_report_ms_) >= 0) {
      next_report_ms_ = now + 1000UL;
      report("wait_temp");
    }
    return;
  }

  if (state_ != CAM_RUNNING_POINT)
    return;

  if (makeit_fa_transaction.running())
    return;

  if (!makeit_fa_transaction.terminal()
      || makeit_fa_transaction.current_point_id() != current_point_id_) {
    finish(CAM_ERROR, "transaction_state_error");
    return;
  }

  handle_point_result();
}

void MakeItFASpeedCampaign::query(const bool has_campaign_id, const uint32_t requested_campaign_id) {
  idle();

  if (!record_valid_) {
    SERIAL_ECHOLNPGM("FA7: state=EMPTY result=NONE");
    return;
  }

  if (has_campaign_id && requested_campaign_id != campaign_id_) {
    SERIAL_ECHOPGM("FA7: error=NOT_FOUND requested_campaign_id="); SERIAL_ECHO(requested_campaign_id);
    SERIAL_ECHOPGM(" retained_campaign_id="); SERIAL_ECHO(campaign_id_);
    SERIAL_ECHOLNPGM("");
    return;
  }

  report("query");
}

bool MakeItFASpeedCampaign::cancel(const bool has_campaign_id, const uint32_t requested_campaign_id) {
  idle();

  if (!record_valid_) {
    SERIAL_ECHOLNPGM("FA7: error=NO_CAMPAIGN");
    return false;
  }

  if (has_campaign_id && requested_campaign_id != campaign_id_) {
    SERIAL_ECHOPGM("FA7: error=NOT_FOUND requested_campaign_id="); SERIAL_ECHO(requested_campaign_id);
    SERIAL_ECHOPGM(" retained_campaign_id="); SERIAL_ECHO(campaign_id_);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  if (state_ == CAM_ABORTED && cancel_requested_) {
    report("cancel_replay");
    return true;
  }

  if (state_ == CAM_WAIT_TEMP) {
    cancel_requested_ = true;
    finish(CAM_ABORTED, "cancelled_wait");
    return true;
  }

  if (state_ != CAM_RUNNING_POINT) {
    SERIAL_ECHOPGM("FA7: error=NOT_ACTIVE state="); SERIAL_ECHO(state_name(state_));
    SERIAL_ECHOLNPGM("");
    return false;
  }

  if (cancel_requested_) {
    report("cancel_replay");
    return true;
  }

  cancel_requested_ = true;
  if (!makeit_fa_transaction.request_abort(current_point_id_)) {
    cancel_requested_ = false;
    SERIAL_ECHOLNPGM("FA7: error=CANCEL_REJECTED");
    return false;
  }

  report("cancel_requested");
  return true;
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
