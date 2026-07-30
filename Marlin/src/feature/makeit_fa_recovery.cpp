/**
 * MAKEiT Filament Analyzer - Phase 9 recovery / re-prime controller
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "makeit_fa_recovery.h"
#include "makeit_filament_analyzer_phase0.h"
#include "../core/serial.h"
#include "../module/temperature.h"
#include "../MarlinCore.h"

MakeItFARecovery::State MakeItFARecovery::state_ = MakeItFARecovery::REC_EMPTY;
bool MakeItFARecovery::record_valid_ = false;
bool MakeItFARecovery::cancel_requested_ = false;
bool MakeItFARecovery::prime_stop_requested_ = false;
uint32_t MakeItFARecovery::recovery_id_ = 0;
uint32_t MakeItFARecovery::params_hash_ = 0;
uint32_t MakeItFARecovery::started_ms_ = 0;
uint32_t MakeItFARecovery::finished_ms_ = 0;
uint32_t MakeItFARecovery::stable_since_ms_ = 0;
uint32_t MakeItFARecovery::next_report_ms_ = 0;
float MakeItFARecovery::settle_band_c_ = 1.0f;
float MakeItFARecovery::stable_temp_min_c_ = 0.0f;
float MakeItFARecovery::stable_temp_max_c_ = 0.0f;
uint8_t MakeItFARecovery::validation_result_code_ = 0;
uint32_t MakeItFARecovery::validation_result_crc_ = 0;
uint32_t MakeItFARecovery::result_crc_ = 0;
MakeItFARecoveryParams MakeItFARecovery::params_ = {};

MakeItFARecovery makeit_fa_recovery;

uint32_t MakeItFARecovery::float_bits(const float value) {
  union FloatBits { float f; uint32_t u; } bits;
  bits.f = value;
  return bits.u;
}

uint32_t MakeItFARecovery::hash_word(uint32_t hash, const uint32_t word) {
  for (uint8_t i = 0; i < 4; ++i) {
    hash ^= uint8_t(word >> (i * 8));
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t MakeItFARecovery::crc32_word(uint32_t crc, const uint32_t word) {
  for (uint8_t byte_index = 0; byte_index < 4; ++byte_index) {
    crc ^= uint8_t(word >> (byte_index * 8));
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320UL & uint32_t(-int32_t(crc & 1UL)));
  }
  return crc;
}

uint32_t MakeItFARecovery::hash_parameters(const MakeItFARecoveryParams &p) {
  uint32_t hash = 2166136261UL;
  hash = hash_word(hash, float_bits(p.recovery_temp_c));
  hash = hash_word(hash, float_bits(p.return_temp_c));
  hash = hash_word(hash, p.settle_seconds);
  hash = hash_word(hash, float_bits(p.prime_mm));
  hash = hash_word(hash, float_bits(p.prime_feed_mm_min));
  hash = hash_word(hash, float_bits(p.validation.total_mm));
  hash = hash_word(hash, float_bits(p.validation.feed_mm_min));
  hash = hash_word(hash, float_bits(p.validation.segment_mm));
  hash = hash_word(hash, p.validation.max_inflight);
  hash = hash_word(hash, p.validation.report_ms);
  hash = hash_word(hash, float_bits(p.validation.encoder_events_per_mm));
  hash = hash_word(hash, float_bits(p.validation.pass_efficiency_pct));
  hash = hash_word(hash, float_bits(p.validation.temp_tolerance));
  hash = hash_word(hash, p.validation.auto_stop_enabled ? 1UL : 0UL);
  hash = hash_word(hash, float_bits(p.validation.monitor_window_mm));
  hash = hash_word(hash, float_bits(p.validation.monitor_efficiency_pct));
  hash = hash_word(hash, p.validation.monitor_confirm_windows);
  hash = hash_word(hash, float_bits(p.validation.pulse_gap_factor));
  hash = hash_word(hash, p.validation.pulse_gap_min_ms);
  hash = hash_word(hash, float_bits(p.validation.pulse_gap_min_missing_events));
  return hash;
}

const char* MakeItFARecovery::state_name(const State state) {
  switch (state) {
    case REC_WAIT_TEMP:  return "WAIT_TEMP";
    case REC_PRIMING:    return "PRIMING";
    case REC_VALIDATING: return "VALIDATING";
    case REC_COMPLETE:   return "COMPLETE";
    case REC_FAILED:     return "FAILED";
    case REC_ABORTED:    return "ABORTED";
    case REC_ERROR:      return "ERROR";
    default:             return "EMPTY";
  }
}

void MakeItFARecovery::report(const char *tag) {
  const uint32_t now = millis();
  const uint32_t stable_ms = stable_since_ms_ ? now - stable_since_ms_ : 0;
  const float stable_span_c = stable_since_ms_ ? stable_temp_max_c_ - stable_temp_min_c_ : 0.0f;

  SERIAL_ECHOPGM("FA9: tag="); SERIAL_ECHO(tag);
  SERIAL_ECHOPGM(" recovery_id="); SERIAL_ECHO(recovery_id_);
  SERIAL_ECHOPGM(" params_hash="); SERIAL_ECHO(params_hash_);
  SERIAL_ECHOPGM(" state="); SERIAL_ECHO(state_name(state_));
  SERIAL_ECHOPGM(" recovery_temp_c="); SERIAL_ECHO(params_.recovery_temp_c);
  SERIAL_ECHOPGM(" return_temp_c="); SERIAL_ECHO(params_.return_temp_c);
  SERIAL_ECHOPGM(" temp="); SERIAL_ECHO(thermalManager.degHotend(0));
  SERIAL_ECHOPGM(" stable_ms="); SERIAL_ECHO(stable_ms);
  SERIAL_ECHOPGM(" settle_band_c="); SERIAL_ECHO(settle_band_c_);
  SERIAL_ECHOPGM(" stable_span_c="); SERIAL_ECHO(stable_span_c);
  SERIAL_ECHOPGM(" prime_mm="); SERIAL_ECHO(params_.prime_mm);
  SERIAL_ECHOPGM(" prime_feed_mm_min="); SERIAL_ECHO(params_.prime_feed_mm_min);
  SERIAL_ECHOPGM(" validate_mm="); SERIAL_ECHO(params_.validation.total_mm);
  SERIAL_ECHOPGM(" validate_feed_mm_min="); SERIAL_ECHO(params_.validation.feed_mm_min);
  SERIAL_ECHOPGM(" validation_result="); SERIAL_ECHO(validation_result_code_);
  SERIAL_ECHOPGM(" validation_crc="); SERIAL_ECHO(validation_result_crc_);
  SERIAL_ECHOPGM(" cancel="); SERIAL_ECHO(cancel_requested_ ? 1 : 0);
  SERIAL_ECHOPGM(" started_ms="); SERIAL_ECHO(started_ms_);
  SERIAL_ECHOPGM(" finished_ms="); SERIAL_ECHO(finished_ms_);
  SERIAL_ECHOPGM(" recovery_crc="); SERIAL_ECHO(result_crc_);
  SERIAL_ECHOLNPGM("");
}

void MakeItFARecovery::restore_return_target() {
  if (params_.return_temp_c > 0.0f)
    thermalManager.setTargetHotend(celsius_t(params_.return_temp_c + 0.5f), 0);
}

void MakeItFARecovery::capture_result_crc() {
  uint32_t crc = 0xFFFFFFFFUL;
  crc = crc32_word(crc, recovery_id_);
  crc = crc32_word(crc, params_hash_);
  crc = crc32_word(crc, uint32_t(state_));
  crc = crc32_word(crc, started_ms_);
  crc = crc32_word(crc, finished_ms_);
  crc = crc32_word(crc, cancel_requested_ ? 1UL : 0UL);
  crc = crc32_word(crc, validation_result_code_);
  crc = crc32_word(crc, validation_result_crc_);
  result_crc_ = crc ^ 0xFFFFFFFFUL;
}

void MakeItFARecovery::finish(const State terminal_state, const char *tag) {
  state_ = terminal_state;
  finished_ms_ = millis();
  stable_since_ms_ = 0;
  restore_return_target();
  capture_result_crc();
  report(tag);
}

bool MakeItFARecovery::start_prime() {
  if (!makeit_fa_phase0.run_segmented_feed_test(
        params_.prime_mm,
        params_.prime_feed_mm_min,
        params_.validation.segment_mm,
        params_.validation.max_inflight,
        params_.validation.report_ms
      ))
    return false;

  state_ = REC_PRIMING;
  prime_stop_requested_ = false;
  report("prime_started");
  return true;
}

bool MakeItFARecovery::start_validation() {
  if (!makeit_fa_transaction.execute(recovery_id_, params_.validation))
    return false;

  state_ = REC_VALIDATING;
  report("validation_started");
  return true;
}

bool MakeItFARecovery::start(const uint32_t recovery_id, const MakeItFARecoveryParams &requested) {
  if (!recovery_id) {
    SERIAL_ECHOLNPGM("FA9: error=INVALID_RECOVERY_ID recovery_id=0");
    return false;
  }

  MakeItFARecoveryParams normalized = requested;
  normalized.recovery_temp_c = float(int32_t(normalized.recovery_temp_c + 0.5f));
  normalized.return_temp_c = float(int32_t(normalized.return_temp_c + 0.5f));
  normalized.settle_seconds = constrain(normalized.settle_seconds, uint16_t(0), uint16_t(120));
  normalized.prime_mm = constrain(normalized.prime_mm, 5.0f, 100.0f);
  normalized.prime_feed_mm_min = constrain(normalized.prime_feed_mm_min, 1.0f, 500.0f);

  MakeItFAPointParams &p = normalized.validation;
  p.total_mm = constrain(p.total_mm, 20.0f, 100.0f);
  p.feed_mm_min = constrain(p.feed_mm_min, 1.0f, 500.0f);
  p.segment_mm = constrain(p.segment_mm, 0.05f, 0.35f);
  p.max_inflight = constrain(p.max_inflight, uint8_t(1), uint8_t(2));
  p.report_ms = constrain(p.report_ms, uint16_t(50), uint16_t(5000));
  p.encoder_events_per_mm = constrain(p.encoder_events_per_mm, 0.01f, 100.0f);
  p.pass_efficiency_pct = constrain(p.pass_efficiency_pct, 50.0f, 105.0f);
  p.temp_tolerance = constrain(p.temp_tolerance, 0.5f, 15.0f);
  p.monitor_window_mm = constrain(p.monitor_window_mm, 5.0f, 100.0f);
  p.monitor_efficiency_pct = constrain(p.monitor_efficiency_pct, 50.0f, 105.0f);
  p.monitor_confirm_windows = constrain(p.monitor_confirm_windows, uint8_t(1), uint8_t(5));
  if (p.pulse_gap_factor > 0.0f)
    p.pulse_gap_factor = constrain(p.pulse_gap_factor, 1.5f, 20.0f);
  p.pulse_gap_min_ms = constrain(p.pulse_gap_min_ms, uint16_t(50), uint16_t(30000));
  p.pulse_gap_min_missing_events = constrain(p.pulse_gap_min_missing_events, 0.5f, 20.0f);

  const float max_allowed = float(thermalManager.hotend_max_target(0));
  if (normalized.recovery_temp_c <= 0.0f || normalized.recovery_temp_c > max_allowed) {
    SERIAL_ECHOPGM("FA9: error=INVALID_RECOVERY_TEMP requested="); SERIAL_ECHO(normalized.recovery_temp_c);
    SERIAL_ECHOPGM(" allowed_max="); SERIAL_ECHO(max_allowed);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  if (normalized.return_temp_c <= 0.0f)
    normalized.return_temp_c = float(thermalManager.degTargetHotend(0));

  const uint32_t hash = hash_parameters(normalized);
  if (record_valid_ && recovery_id == recovery_id_) {
    if (hash != params_hash_) {
      SERIAL_ECHOPGM("FA9: error=PARAM_CONFLICT recovery_id="); SERIAL_ECHO(recovery_id);
      SERIAL_ECHOPGM(" stored_hash="); SERIAL_ECHO(params_hash_);
      SERIAL_ECHOPGM(" requested_hash="); SERIAL_ECHO(hash);
      SERIAL_ECHOLNPGM("");
      return false;
    }
    report("replay");
    return true;
  }

  if (active() || makeit_fa_transaction.running()
      || makeit_fa_phase0.test_point_active() || makeit_fa_phase0.segmented_feed_active()) {
    SERIAL_ECHOPGM("FA9: error=BUSY recovery_id="); SERIAL_ECHO(recovery_id_);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  params_ = normalized;
  state_ = REC_WAIT_TEMP;
  record_valid_ = true;
  cancel_requested_ = false;
  prime_stop_requested_ = false;
  recovery_id_ = recovery_id;
  params_hash_ = hash_parameters(params_);
  started_ms_ = millis();
  finished_ms_ = 0;
  stable_since_ms_ = 0;
  next_report_ms_ = started_ms_;
  settle_band_c_ = _MIN(params_.validation.temp_tolerance, 1.0f);
  stable_temp_min_c_ = thermalManager.degHotend(0);
  stable_temp_max_c_ = stable_temp_min_c_;
  validation_result_code_ = 0;
  validation_result_crc_ = 0;
  result_crc_ = 0;

  thermalManager.setTargetHotend(celsius_t(params_.recovery_temp_c + 0.5f), 0);
  if (!thermalManager.targetHotEnoughToExtrude(0)) {
    restore_return_target();
    state_ = REC_ERROR;
    finished_ms_ = millis();
    capture_result_crc();
    report("target_too_cold");
    return false;
  }

  report("started");
  return true;
}

void MakeItFARecovery::idle() {
  if (state_ == REC_WAIT_TEMP) {
    if (cancel_requested_) {
      finish(REC_ABORTED, "cancelled_wait");
      return;
    }

    const uint32_t now = millis();
    const float target_now = thermalManager.degTargetHotend(0);
    const float temp_now = thermalManager.degHotend(0);

    if (ABS(target_now - params_.recovery_temp_c) > 0.5f) {
      finish(REC_ERROR, "target_changed");
      return;
    }

    if (ABS(temp_now - params_.recovery_temp_c) <= settle_band_c_
        && thermalManager.hotEnoughToExtrude(0)) {
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
      if (uint32_t(now - stable_since_ms_) >= required_ms
          && stable_temp_max_c_ - stable_temp_min_c_ <= settle_band_c_ + 0.001f) {
        if (!start_prime())
          finish(REC_ERROR, "prime_start_failed");
        return;
      }
    }
    else {
      stable_since_ms_ = 0;
      stable_temp_min_c_ = stable_temp_max_c_ = temp_now;
    }

    if ((int32_t)(now - next_report_ms_) >= 0) {
      next_report_ms_ = now + 1000UL;
      report("wait_temp");
    }
    return;
  }

  if (state_ == REC_PRIMING) {
    if (cancel_requested_ && !prime_stop_requested_) {
      prime_stop_requested_ = makeit_fa_phase0.request_segmented_feed_stop();
      report("prime_stop_requested");
    }

    if (makeit_fa_phase0.segmented_feed_active()) return;

    if (cancel_requested_) {
      finish(REC_ABORTED, "cancelled_prime");
      return;
    }

    if (!start_validation())
      finish(REC_ERROR, "validation_start_failed");
    return;
  }

  if (state_ != REC_VALIDATING) return;

  if (cancel_requested_ && makeit_fa_transaction.running())
    makeit_fa_transaction.request_abort(recovery_id_);

  if (makeit_fa_transaction.running()) return;

  if (!makeit_fa_transaction.terminal()
      || makeit_fa_transaction.current_point_id() != recovery_id_) {
    finish(REC_ERROR, "transaction_state_error");
    return;
  }

  validation_result_code_ = makeit_fa_transaction.result_code();
  validation_result_crc_ = makeit_fa_transaction.result_crc();

  if (cancel_requested_ || makeit_fa_transaction.state() == MakeItFATransaction::TX_ABORTED) {
    finish(REC_ABORTED, "aborted");
    return;
  }

  if (validation_result_code_ == MakeItFilamentAnalyzerPhase0::TP_RESULT_PASS)
    finish(REC_COMPLETE, "complete");
  else
    finish(REC_FAILED, "validation_failed");
}

void MakeItFARecovery::query(const bool has_recovery_id, const uint32_t requested_recovery_id) {
  idle();
  if (!record_valid_) {
    SERIAL_ECHOLNPGM("FA9: state=EMPTY result=NONE");
    return;
  }
  if (has_recovery_id && requested_recovery_id != recovery_id_) {
    SERIAL_ECHOPGM("FA9: error=NOT_FOUND requested_recovery_id="); SERIAL_ECHO(requested_recovery_id);
    SERIAL_ECHOPGM(" retained_recovery_id="); SERIAL_ECHO(recovery_id_);
    SERIAL_ECHOLNPGM("");
    return;
  }
  report("query");
}

bool MakeItFARecovery::cancel(const bool has_recovery_id, const uint32_t requested_recovery_id) {
  idle();
  if (!record_valid_) {
    SERIAL_ECHOLNPGM("FA9: error=NO_RECOVERY");
    return false;
  }
  if (has_recovery_id && requested_recovery_id != recovery_id_) {
    SERIAL_ECHOPGM("FA9: error=NOT_FOUND requested_recovery_id="); SERIAL_ECHO(requested_recovery_id);
    SERIAL_ECHOPGM(" retained_recovery_id="); SERIAL_ECHO(recovery_id_);
    SERIAL_ECHOLNPGM("");
    return false;
  }
  if (terminal()) {
    SERIAL_ECHOPGM("FA9: error=NOT_ACTIVE state="); SERIAL_ECHO(state_name(state_));
    SERIAL_ECHOLNPGM("");
    return false;
  }
  if (cancel_requested_) {
    report("cancel_replay");
    return true;
  }

  cancel_requested_ = true;
  if (state_ == REC_PRIMING)
    prime_stop_requested_ = makeit_fa_phase0.request_segmented_feed_stop();
  else if (state_ == REC_VALIDATING && makeit_fa_transaction.running())
    makeit_fa_transaction.request_abort(recovery_id_);

  report("cancel_requested");
  return true;
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
