/**
 * MAKEiT Filament Analyzer - Phase 8 temperature / speed envelope
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "makeit_fa_envelope.h"
#include "makeit_fa_transaction.h"
#include "../core/serial.h"
#include "../module/temperature.h"
#include "../MarlinCore.h"

MakeItFATemperatureEnvelope::State MakeItFATemperatureEnvelope::state_ = MakeItFATemperatureEnvelope::ENV_EMPTY;
bool MakeItFATemperatureEnvelope::record_valid_ = false;
bool MakeItFATemperatureEnvelope::cancel_requested_ = false;
uint32_t MakeItFATemperatureEnvelope::envelope_id_ = 0;
uint32_t MakeItFATemperatureEnvelope::params_hash_ = 0;
uint32_t MakeItFATemperatureEnvelope::started_ms_ = 0;
uint32_t MakeItFATemperatureEnvelope::finished_ms_ = 0;
uint32_t MakeItFATemperatureEnvelope::current_row_campaign_id_ = 0;
uint32_t MakeItFATemperatureEnvelope::result_crc_ = 0;
uint16_t MakeItFATemperatureEnvelope::speed_points_per_row_ = 0;
uint8_t MakeItFATemperatureEnvelope::row_index_ = 0;
uint8_t MakeItFATemperatureEnvelope::row_count_ = 0;
uint8_t MakeItFATemperatureEnvelope::completed_rows_ = 0;
float MakeItFATemperatureEnvelope::original_target_temp_c_ = 0.0f;
float MakeItFATemperatureEnvelope::current_temp_c_ = 0.0f;
MakeItFATemperatureEnvelopeParams MakeItFATemperatureEnvelope::params_ = {};
MakeItFATemperatureEnvelope::RowResult MakeItFATemperatureEnvelope::rows_[MakeItFATemperatureEnvelope::MAX_ROWS] = {};

MakeItFATemperatureEnvelope makeit_fa_envelope;

uint32_t MakeItFATemperatureEnvelope::float_bits(const float value) {
  union FloatBits {
    float f;
    uint32_t u;
  } bits;
  bits.f = value;
  return bits.u;
}

uint32_t MakeItFATemperatureEnvelope::hash_word(uint32_t hash, const uint32_t word) {
  for (uint8_t i = 0; i < 4; ++i) {
    hash ^= uint8_t(word >> (i * 8));
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t MakeItFATemperatureEnvelope::crc32_word(uint32_t crc, const uint32_t word) {
  for (uint8_t byte_index = 0; byte_index < 4; ++byte_index) {
    crc ^= uint8_t(word >> (byte_index * 8));
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320UL & uint32_t(-int32_t(crc & 1UL)));
  }
  return crc;
}

uint32_t MakeItFATemperatureEnvelope::hash_parameters(const MakeItFATemperatureEnvelopeParams &p) {
  uint32_t hash = 2166136261UL;
  hash = hash_word(hash, float_bits(p.start_temp_c));
  hash = hash_word(hash, float_bits(p.max_temp_c));
  hash = hash_word(hash, float_bits(p.temp_step_c));
  hash = hash_word(hash, float_bits(p.filament_diameter_mm));
  hash = hash_word(hash, float_bits(p.speed.start_feed_mm_min));
  hash = hash_word(hash, float_bits(p.speed.max_feed_mm_min));
  hash = hash_word(hash, float_bits(p.speed.step_feed_mm_min));
  hash = hash_word(hash, p.speed.settle_seconds);
  hash = hash_word(hash, float_bits(p.speed.point.total_mm));
  hash = hash_word(hash, float_bits(p.speed.point.segment_mm));
  hash = hash_word(hash, p.speed.point.max_inflight);
  hash = hash_word(hash, p.speed.point.report_ms);
  hash = hash_word(hash, float_bits(p.speed.point.encoder_events_per_mm));
  hash = hash_word(hash, float_bits(p.speed.point.pass_efficiency_pct));
  hash = hash_word(hash, float_bits(p.speed.point.temp_tolerance));
  hash = hash_word(hash, p.speed.point.auto_stop_enabled ? 1UL : 0UL);
  hash = hash_word(hash, float_bits(p.speed.point.monitor_window_mm));
  hash = hash_word(hash, float_bits(p.speed.point.monitor_efficiency_pct));
  hash = hash_word(hash, p.speed.point.monitor_confirm_windows);
  hash = hash_word(hash, float_bits(p.speed.point.pulse_gap_factor));
  hash = hash_word(hash, p.speed.point.pulse_gap_min_ms);
  hash = hash_word(hash, float_bits(p.speed.point.pulse_gap_min_missing_events));
  return hash;
}

uint16_t MakeItFATemperatureEnvelope::count_speed_points(const float start_feed, const float max_feed, const float step_feed) {
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

uint8_t MakeItFATemperatureEnvelope::count_temperature_rows(const float start_temp, const float max_temp, const float temp_step) {
  if (start_temp <= 0.0f || max_temp < start_temp || temp_step <= 0.0f)
    return 0;

  uint8_t count = 0;
  while (count <= MAX_ROWS) {
    const float temp = start_temp + float(count) * temp_step;
    if (temp > max_temp + 0.0001f) break;
    ++count;
  }
  return count <= MAX_ROWS ? count : 0;
}

const char* MakeItFATemperatureEnvelope::state_name(const State state) {
  switch (state) {
    case ENV_RUNNING_ROW:  return "RUNNING_ROW";
    case ENV_COMPLETE:     return "COMPLETE";
    case ENV_LIMIT_FOUND:  return "LIMIT_FOUND";
    case ENV_INVALID_TEMP: return "INVALID_TEMP";
    case ENV_ABORTED:      return "ABORTED";
    case ENV_ERROR:        return "ERROR";
    default:               return "EMPTY";
  }
}

const char* MakeItFATemperatureEnvelope::row_state_name(const MakeItFASpeedCampaign::State state) {
  switch (state) {
    case MakeItFASpeedCampaign::CAM_COMPLETE:      return "COMPLETE";
    case MakeItFASpeedCampaign::CAM_LIMIT_FOUND:   return "LIMIT_FOUND";
    case MakeItFASpeedCampaign::CAM_INVALID_TEMP:  return "INVALID_TEMP";
    case MakeItFASpeedCampaign::CAM_ABORTED:       return "ABORTED";
    case MakeItFASpeedCampaign::CAM_ERROR:         return "ERROR";
    case MakeItFASpeedCampaign::CAM_WAIT_TEMP:     return "WAIT_TEMP";
    case MakeItFASpeedCampaign::CAM_RUNNING_POINT: return "RUNNING_POINT";
    default:                                       return "EMPTY";
  }
}

float MakeItFATemperatureEnvelope::feed_to_q_mm3_s(const float feed_mm_min, const float diameter_mm) {
  if (feed_mm_min <= 0.0f || diameter_mm <= 0.0f) return 0.0f;
  const float area_mm2 = 0.78539816339f * diameter_mm * diameter_mm;
  return area_mm2 * feed_mm_min / 60.0f;
}

void MakeItFATemperatureEnvelope::normalize_speed_params(MakeItFASpeedCampaignParams &p) {
  p.start_feed_mm_min = constrain(p.start_feed_mm_min, 1.0f, 2000.0f);
  p.max_feed_mm_min = constrain(p.max_feed_mm_min, 1.0f, 2000.0f);
  p.step_feed_mm_min = constrain(p.step_feed_mm_min, 1.0f, 1000.0f);
  p.settle_seconds = constrain(p.settle_seconds, uint16_t(0), uint16_t(120));

  p.point.total_mm = constrain(p.point.total_mm, 20.0f, 500.0f);
  p.point.feed_mm_min = p.start_feed_mm_min;
  p.point.segment_mm = constrain(p.point.segment_mm, 0.05f, 0.35f);
  p.point.max_inflight = constrain(p.point.max_inflight, uint8_t(1), uint8_t(2));
  p.point.report_ms = constrain(p.point.report_ms, uint16_t(50), uint16_t(5000));
  p.point.encoder_events_per_mm = constrain(p.point.encoder_events_per_mm, 0.01f, 100.0f);
  p.point.pass_efficiency_pct = constrain(p.point.pass_efficiency_pct, 50.0f, 105.0f);
  p.point.temp_tolerance = constrain(p.point.temp_tolerance, 0.5f, 15.0f);
  p.point.monitor_window_mm = constrain(p.point.monitor_window_mm, 5.0f, 100.0f);
  p.point.monitor_efficiency_pct = constrain(p.point.monitor_efficiency_pct, 50.0f, 105.0f);
  p.point.monitor_confirm_windows = constrain(p.point.monitor_confirm_windows, uint8_t(1), uint8_t(5));
  if (p.point.pulse_gap_factor > 0.0f)
    p.point.pulse_gap_factor = constrain(p.point.pulse_gap_factor, 1.5f, 20.0f);
  else
    p.point.pulse_gap_factor = 0.0f;
  p.point.pulse_gap_min_ms = constrain(p.point.pulse_gap_min_ms, uint16_t(50), uint16_t(30000));
  p.point.pulse_gap_min_missing_events = constrain(p.point.pulse_gap_min_missing_events, 0.5f, 20.0f);
}

void MakeItFATemperatureEnvelope::restore_original_target() {
  if (original_target_temp_c_ > 0.0f)
    thermalManager.setTargetHotend(celsius_t(original_target_temp_c_ + 0.5f), 0);
}

void MakeItFATemperatureEnvelope::emit_row(const uint8_t row) {
  if (row >= completed_rows_) return;
  const RowResult &r = rows_[row];
  const float q_pass = feed_to_q_mm3_s(r.last_pass_feed_mm_min, params_.filament_diameter_mm);
  const float q_fail = feed_to_q_mm3_s(r.first_fail_feed_mm_min, params_.filament_diameter_mm);

  SERIAL_ECHOPGM("FA8ROW: envelope_id="); SERIAL_ECHO(envelope_id_);
  SERIAL_ECHOPGM(" row="); SERIAL_ECHO(row);
  SERIAL_ECHOPGM(" temp_c="); SERIAL_ECHO(r.temp_c);
  SERIAL_ECHOPGM(" campaign_id="); SERIAL_ECHO(r.row_campaign_id);
  SERIAL_ECHOPGM(" campaign_state="); SERIAL_ECHO(row_state_name(MakeItFASpeedCampaign::State(r.campaign_state)));
  SERIAL_ECHOPGM(" last_pass_feed="); SERIAL_ECHO(r.last_pass_feed_mm_min);
  SERIAL_ECHOPGM(" first_fail_feed="); SERIAL_ECHO(r.first_fail_feed_mm_min);
  SERIAL_ECHOPGM(" q_pass_mm3_s="); SERIAL_ECHO(q_pass);
  SERIAL_ECHOPGM(" q_fail_mm3_s="); SERIAL_ECHO(q_fail);
  SERIAL_ECHOPGM(" point_result="); SERIAL_ECHO(r.last_point_result_code);
  SERIAL_ECHOPGM(" point_crc="); SERIAL_ECHO(r.last_point_crc);
  SERIAL_ECHOLNPGM("");
}

void MakeItFATemperatureEnvelope::report(const char *tag, const bool include_rows) {
  SERIAL_ECHOPGM("FA8: tag="); SERIAL_ECHO(tag);
  SERIAL_ECHOPGM(" envelope_id="); SERIAL_ECHO(envelope_id_);
  SERIAL_ECHOPGM(" params_hash="); SERIAL_ECHO(params_hash_);
  SERIAL_ECHOPGM(" state="); SERIAL_ECHO(state_name(state_));
  SERIAL_ECHOPGM(" row="); SERIAL_ECHO(row_index_);
  SERIAL_ECHOPGM(" rows="); SERIAL_ECHO(row_count_);
  SERIAL_ECHOPGM(" completed_rows="); SERIAL_ECHO(completed_rows_);
  SERIAL_ECHOPGM(" row_campaign_id="); SERIAL_ECHO(current_row_campaign_id_);
  SERIAL_ECHOPGM(" temp_c="); SERIAL_ECHO(current_temp_c_);
  SERIAL_ECHOPGM(" start_temp_c="); SERIAL_ECHO(params_.start_temp_c);
  SERIAL_ECHOPGM(" max_temp_c="); SERIAL_ECHO(params_.max_temp_c);
  SERIAL_ECHOPGM(" temp_step_c="); SERIAL_ECHO(params_.temp_step_c);
  SERIAL_ECHOPGM(" filament_diameter_mm="); SERIAL_ECHO(params_.filament_diameter_mm);
  SERIAL_ECHOPGM(" speed_points="); SERIAL_ECHO(speed_points_per_row_);
  SERIAL_ECHOPGM(" cancel="); SERIAL_ECHO(cancel_requested_ ? 1 : 0);
  SERIAL_ECHOPGM(" started_ms="); SERIAL_ECHO(started_ms_);
  SERIAL_ECHOPGM(" finished_ms="); SERIAL_ECHO(finished_ms_);
  SERIAL_ECHOPGM(" envelope_crc="); SERIAL_ECHO(result_crc_);
  SERIAL_ECHOLNPGM("");

  if (include_rows)
    for (uint8_t i = 0; i < completed_rows_; ++i) emit_row(i);
}

void MakeItFATemperatureEnvelope::capture_result_crc() {
  uint32_t crc = 0xFFFFFFFFUL;
  crc = crc32_word(crc, envelope_id_);
  crc = crc32_word(crc, params_hash_);
  crc = crc32_word(crc, uint32_t(state_));
  crc = crc32_word(crc, started_ms_);
  crc = crc32_word(crc, finished_ms_);
  crc = crc32_word(crc, row_count_);
  crc = crc32_word(crc, completed_rows_);
  crc = crc32_word(crc, float_bits(params_.filament_diameter_mm));

  for (uint8_t i = 0; i < completed_rows_; ++i) {
    const RowResult &r = rows_[i];
    crc = crc32_word(crc, float_bits(r.temp_c));
    crc = crc32_word(crc, float_bits(r.last_pass_feed_mm_min));
    crc = crc32_word(crc, float_bits(r.first_fail_feed_mm_min));
    crc = crc32_word(crc, r.row_campaign_id);
    crc = crc32_word(crc, r.last_point_crc);
    crc = crc32_word(crc, r.campaign_state);
    crc = crc32_word(crc, r.last_point_result_code);
  }
  result_crc_ = crc ^ 0xFFFFFFFFUL;
}

void MakeItFATemperatureEnvelope::finish(const State terminal_state, const char *tag) {
  state_ = terminal_state;
  finished_ms_ = millis();
  restore_original_target();
  capture_result_crc();
  report(tag, true);
}

bool MakeItFATemperatureEnvelope::start_current_row() {
  if (row_index_ >= row_count_) return false;
  if (makeit_fa_campaign.active() || makeit_fa_transaction.running()) return false;

  current_temp_c_ = params_.start_temp_c + float(row_index_) * params_.temp_step_c;
  current_row_campaign_id_ = envelope_id_ + uint32_t(row_index_) * uint32_t(speed_points_per_row_);
  thermalManager.setTargetHotend(celsius_t(current_temp_c_ + 0.5f), 0);

  if (!makeit_fa_campaign.start(current_row_campaign_id_, params_.speed))
    return false;

  state_ = ENV_RUNNING_ROW;
  report("row_started");
  return true;
}

void MakeItFATemperatureEnvelope::handle_row_result() {
  if (completed_rows_ >= row_count_ || completed_rows_ >= MAX_ROWS) {
    finish(ENV_ERROR, "row_storage_error");
    return;
  }

  RowResult &r = rows_[completed_rows_];
  r.temp_c = current_temp_c_;
  r.last_pass_feed_mm_min = makeit_fa_campaign.last_pass_feed_mm_min();
  r.first_fail_feed_mm_min = makeit_fa_campaign.first_fail_feed_mm_min();
  r.row_campaign_id = current_row_campaign_id_;
  r.last_point_crc = makeit_fa_campaign.last_point_result_crc();
  r.campaign_state = uint8_t(makeit_fa_campaign.state());
  r.last_point_result_code = makeit_fa_campaign.last_point_result_code();
  ++completed_rows_;
  emit_row(completed_rows_ - 1);

  switch (makeit_fa_campaign.state()) {
    case MakeItFASpeedCampaign::CAM_COMPLETE:
      if (uint8_t(row_index_ + 1) >= row_count_) {
        finish(ENV_COMPLETE, "complete");
        return;
      }
      ++row_index_;
      if (!start_current_row())
        finish(ENV_ERROR, "next_row_start_failed");
      break;

    case MakeItFASpeedCampaign::CAM_LIMIT_FOUND:
      // Continuing after a real feed-loss point requires the future recovery /
      // re-prime state machine. Stop here rather than contaminate later rows.
      finish(ENV_LIMIT_FOUND, "limit_found_recovery_required");
      break;

    case MakeItFASpeedCampaign::CAM_INVALID_TEMP:
      finish(ENV_INVALID_TEMP, "invalid_temp");
      break;

    case MakeItFASpeedCampaign::CAM_ABORTED:
      finish(ENV_ABORTED, "aborted");
      break;

    default:
      finish(ENV_ERROR, "row_error");
      break;
  }
}

bool MakeItFATemperatureEnvelope::start(const uint32_t envelope_id, const MakeItFATemperatureEnvelopeParams &requested) {
  if (!envelope_id) {
    SERIAL_ECHOLNPGM("FA8: error=INVALID_ENVELOPE_ID envelope_id=0");
    return false;
  }

  MakeItFATemperatureEnvelopeParams normalized = requested;
  normalized.start_temp_c = float(thermalManager.degTargetHotend(0));
  normalized.max_temp_c = float(int32_t(normalized.max_temp_c + 0.5f));
  normalized.temp_step_c = float(int32_t(normalized.temp_step_c + 0.5f));
  normalized.filament_diameter_mm = constrain(normalized.filament_diameter_mm, 1.0f, 3.5f);
  normalize_speed_params(normalized.speed);

  if (normalized.start_temp_c <= 0.0f || !thermalManager.targetHotEnoughToExtrude(0)) {
    SERIAL_ECHOPGM("FA8: error=INVALID_START_TARGET target="); SERIAL_ECHO(normalized.start_temp_c);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  const float max_allowed = float(thermalManager.hotend_max_target(0));
  if (normalized.max_temp_c < normalized.start_temp_c || normalized.max_temp_c > max_allowed) {
    SERIAL_ECHOPGM("FA8: error=INVALID_TEMP_RANGE start="); SERIAL_ECHO(normalized.start_temp_c);
    SERIAL_ECHOPGM(" max="); SERIAL_ECHO(normalized.max_temp_c);
    SERIAL_ECHOPGM(" allowed_max="); SERIAL_ECHO(max_allowed);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  if (normalized.temp_step_c <= 0.0f) {
    SERIAL_ECHOLNPGM("FA8: error=INVALID_TEMP_STEP");
    return false;
  }

  const uint16_t speed_points = count_speed_points(
    normalized.speed.start_feed_mm_min,
    normalized.speed.max_feed_mm_min,
    normalized.speed.step_feed_mm_min
  );
  const uint8_t rows = count_temperature_rows(
    normalized.start_temp_c,
    normalized.max_temp_c,
    normalized.temp_step_c
  );

  if (!speed_points) {
    SERIAL_ECHOLNPGM("FA8: error=INVALID_SPEED_RANGE");
    return false;
  }
  if (!rows) {
    SERIAL_ECHOPGM("FA8: error=TOO_MANY_OR_INVALID_TEMP_ROWS max_rows="); SERIAL_ECHO(MAX_ROWS);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  const uint32_t total_points = uint32_t(rows) * uint32_t(speed_points);
  if (!total_points || envelope_id > 0xFFFFFFFFUL - (total_points - 1UL)) {
    SERIAL_ECHOLNPGM("FA8: error=POINT_ID_OVERFLOW");
    return false;
  }
  const uint32_t final_point_id = envelope_id + total_points - 1UL;
  const uint32_t hash = hash_parameters(normalized);

  if (record_valid_ && envelope_id == envelope_id_) {
    if (hash != params_hash_) {
      SERIAL_ECHOPGM("FA8: error=PARAM_CONFLICT envelope_id="); SERIAL_ECHO(envelope_id);
      SERIAL_ECHOPGM(" stored_hash="); SERIAL_ECHO(params_hash_);
      SERIAL_ECHOPGM(" requested_hash="); SERIAL_ECHO(hash);
      SERIAL_ECHOLNPGM("");
      return false;
    }
    report("replay", true);
    return true;
  }

  if (active() || makeit_fa_campaign.active() || makeit_fa_transaction.running()) {
    SERIAL_ECHOPGM("FA8: error=BUSY envelope_id="); SERIAL_ECHO(envelope_id_);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  if (makeit_fa_transaction.has_record()) {
    const uint32_t retained_point_id = makeit_fa_transaction.current_point_id();
    if (retained_point_id >= envelope_id && retained_point_id <= final_point_id) {
      SERIAL_ECHOPGM("FA8: error=POINT_ID_RANGE_IN_USE retained_point_id="); SERIAL_ECHO(retained_point_id);
      SERIAL_ECHOPGM(" range_start="); SERIAL_ECHO(envelope_id);
      SERIAL_ECHOPGM(" range_end="); SERIAL_ECHO(final_point_id);
      SERIAL_ECHOLNPGM("");
      return false;
    }
  }

  if (makeit_fa_campaign.has_record()) {
    const uint32_t retained_campaign_id = makeit_fa_campaign.current_campaign_id();
    if (retained_campaign_id >= envelope_id && retained_campaign_id <= final_point_id) {
      SERIAL_ECHOPGM("FA8: error=CAMPAIGN_ID_RANGE_IN_USE retained_campaign_id="); SERIAL_ECHO(retained_campaign_id);
      SERIAL_ECHOLNPGM("");
      return false;
    }
  }

  params_ = normalized;
  state_ = ENV_RUNNING_ROW;
  record_valid_ = true;
  cancel_requested_ = false;
  envelope_id_ = envelope_id;
  params_hash_ = hash;
  started_ms_ = millis();
  finished_ms_ = 0;
  current_row_campaign_id_ = envelope_id;
  result_crc_ = 0;
  speed_points_per_row_ = speed_points;
  row_index_ = 0;
  row_count_ = rows;
  completed_rows_ = 0;
  original_target_temp_c_ = normalized.start_temp_c;
  current_temp_c_ = normalized.start_temp_c;
  for (uint8_t i = 0; i < MAX_ROWS; ++i) rows_[i] = RowResult{};

  report("started");
  if (!start_current_row()) {
    finish(ENV_ERROR, "first_row_start_failed");
    return false;
  }
  return true;
}

void MakeItFATemperatureEnvelope::idle() {
  if (state_ != ENV_RUNNING_ROW) return;
  if (makeit_fa_campaign.active()) return;

  if (!makeit_fa_campaign.terminal()
      || makeit_fa_campaign.current_campaign_id() != current_row_campaign_id_) {
    finish(ENV_ERROR, "campaign_state_error");
    return;
  }

  handle_row_result();
}

void MakeItFATemperatureEnvelope::query(const bool has_envelope_id, const uint32_t requested_envelope_id) {
  idle();

  if (!record_valid_) {
    SERIAL_ECHOLNPGM("FA8: state=EMPTY result=NONE");
    return;
  }
  if (has_envelope_id && requested_envelope_id != envelope_id_) {
    SERIAL_ECHOPGM("FA8: error=NOT_FOUND requested_envelope_id="); SERIAL_ECHO(requested_envelope_id);
    SERIAL_ECHOPGM(" retained_envelope_id="); SERIAL_ECHO(envelope_id_);
    SERIAL_ECHOLNPGM("");
    return;
  }
  report("query", true);
}

bool MakeItFATemperatureEnvelope::cancel(const bool has_envelope_id, const uint32_t requested_envelope_id) {
  idle();

  if (!record_valid_) {
    SERIAL_ECHOLNPGM("FA8: error=NO_ENVELOPE");
    return false;
  }
  if (has_envelope_id && requested_envelope_id != envelope_id_) {
    SERIAL_ECHOPGM("FA8: error=NOT_FOUND requested_envelope_id="); SERIAL_ECHO(requested_envelope_id);
    SERIAL_ECHOPGM(" retained_envelope_id="); SERIAL_ECHO(envelope_id_);
    SERIAL_ECHOLNPGM("");
    return false;
  }
  if (state_ == ENV_ABORTED && cancel_requested_) {
    report("cancel_replay", true);
    return true;
  }
  if (state_ != ENV_RUNNING_ROW) {
    SERIAL_ECHOPGM("FA8: error=NOT_ACTIVE state="); SERIAL_ECHO(state_name(state_));
    SERIAL_ECHOLNPGM("");
    return false;
  }
  if (cancel_requested_) {
    report("cancel_replay");
    return true;
  }

  cancel_requested_ = true;
  if (!makeit_fa_campaign.cancel(true, current_row_campaign_id_)) {
    cancel_requested_ = false;
    SERIAL_ECHOLNPGM("FA8: error=CANCEL_REJECTED");
    return false;
  }
  report("cancel_requested");
  return true;
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
