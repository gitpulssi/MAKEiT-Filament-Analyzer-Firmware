/**
 * MAKEiT Filament Analyzer - Phase 5 transaction wrapper
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "makeit_fa_transaction.h"
#include "makeit_filament_analyzer_phase0.h"
#include "../core/serial.h"
#include "../MarlinCore.h"

MakeItFATransaction::State MakeItFATransaction::state_ = MakeItFATransaction::TX_EMPTY;
bool MakeItFATransaction::record_valid_ = false;
uint32_t MakeItFATransaction::point_id_ = 0;
uint32_t MakeItFATransaction::params_hash_ = 0;
uint32_t MakeItFATransaction::started_ms_ = 0;
uint32_t MakeItFATransaction::finished_ms_ = 0;

MakeItFATransaction makeit_fa_transaction;

uint32_t MakeItFATransaction::float_bits(const float value) {
  union FloatBits {
    float f;
    uint32_t u;
  } bits;
  bits.f = value;
  return bits.u;
}

uint32_t MakeItFATransaction::hash_word(uint32_t hash, const uint32_t word) {
  // FNV-1a over the four bytes of a normalized 32-bit parameter word.
  for (uint8_t i = 0; i < 4; ++i) {
    hash ^= uint8_t(word >> (i * 8));
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t MakeItFATransaction::hash_parameters(const MakeItFAPointParams &p) {
  uint32_t hash = 2166136261UL;
  hash = hash_word(hash, float_bits(p.total_mm));
  hash = hash_word(hash, float_bits(p.feed_mm_min));
  hash = hash_word(hash, float_bits(p.segment_mm));
  hash = hash_word(hash, p.max_inflight);
  hash = hash_word(hash, p.report_ms);
  hash = hash_word(hash, float_bits(p.encoder_events_per_mm));
  hash = hash_word(hash, float_bits(p.pass_efficiency_pct));
  hash = hash_word(hash, float_bits(p.temp_tolerance));
  hash = hash_word(hash, p.auto_stop_enabled ? 1UL : 0UL);
  hash = hash_word(hash, float_bits(p.monitor_window_mm));
  hash = hash_word(hash, float_bits(p.monitor_efficiency_pct));
  hash = hash_word(hash, p.monitor_confirm_windows);
  hash = hash_word(hash, float_bits(p.pulse_gap_factor));
  hash = hash_word(hash, p.pulse_gap_min_ms);
  hash = hash_word(hash, float_bits(p.pulse_gap_min_missing_events));
  return hash;
}

const char* MakeItFATransaction::state_name(const State state) {
  switch (state) {
    case TX_RUNNING:  return "RUNNING";
    case TX_TERMINAL: return "TERMINAL";
    default:          return "EMPTY";
  }
}

void MakeItFATransaction::report_transaction(const char *tag) {
  SERIAL_ECHOPGM("FATX: tag="); SERIAL_ECHO(tag);
  SERIAL_ECHOPGM(" point_id="); SERIAL_ECHO(point_id_);
  SERIAL_ECHOPGM(" params_hash="); SERIAL_ECHO(params_hash_);
  SERIAL_ECHOPGM(" state="); SERIAL_ECHO(state_name(state_));
  SERIAL_ECHOPGM(" started_ms="); SERIAL_ECHO(started_ms_);
  SERIAL_ECHOPGM(" finished_ms="); SERIAL_ECHO(finished_ms_);
  SERIAL_ECHOLNPGM("");
}

void MakeItFATransaction::idle() {
  if (state_ != TX_RUNNING) return;

  if (!makeit_fa_phase0.test_point_active() && !makeit_fa_phase0.segmented_feed_active()) {
    state_ = TX_TERMINAL;
    finished_ms_ = millis();
    report_transaction("completed");
  }
}

bool MakeItFATransaction::execute(const uint32_t point_id, const MakeItFAPointParams &p) {
  if (!point_id) {
    SERIAL_ECHOLNPGM("FATX: error=INVALID_POINT_ID point_id=0");
    return false;
  }

  idle();
  const uint32_t hash = hash_parameters(p);

  if (record_valid_ && point_id == point_id_) {
    if (hash != params_hash_) {
      SERIAL_ECHOPGM("FATX: error=PARAM_CONFLICT point_id="); SERIAL_ECHO(point_id);
      SERIAL_ECHOPGM(" stored_hash="); SERIAL_ECHO(params_hash_);
      SERIAL_ECHOPGM(" requested_hash="); SERIAL_ECHO(hash);
      SERIAL_ECHOLNPGM("");
      return false;
    }

    // Idempotent replay. Never start motion again for the same ID/hash pair.
    report_transaction("replay");
    makeit_fa_phase0.report_test_point();
    makeit_fa_phase0.report_pulse_gap_monitor();
    return true;
  }

  if (state_ == TX_RUNNING || makeit_fa_phase0.test_point_active() || makeit_fa_phase0.segmented_feed_active()) {
    SERIAL_ECHOPGM("FATX: error=BUSY active_point_id="); SERIAL_ECHO(point_id_);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  record_valid_ = true;
  point_id_ = point_id;
  params_hash_ = hash;
  started_ms_ = millis();
  finished_ms_ = 0;
  state_ = TX_RUNNING;

  makeit_fa_phase0.configure_pulse_gap_monitor(
    p.pulse_gap_factor > 0.0f,
    p.pulse_gap_factor > 0.0f ? p.pulse_gap_factor : 4.0f,
    p.pulse_gap_min_ms,
    p.pulse_gap_min_missing_events
  );

  const bool started = makeit_fa_phase0.start_evaluated_test_point(
    p.total_mm,
    p.feed_mm_min,
    p.segment_mm,
    p.max_inflight,
    p.report_ms,
    p.encoder_events_per_mm,
    p.pass_efficiency_pct,
    p.temp_tolerance,
    p.auto_stop_enabled,
    p.monitor_window_mm,
    p.monitor_efficiency_pct,
    p.monitor_confirm_windows
  );

  if (!started) {
    state_ = TX_TERMINAL;
    finished_ms_ = millis();
    report_transaction("rejected");
    return false;
  }

  report_transaction("started");
  return true;
}

void MakeItFATransaction::query(const bool has_point_id, const uint32_t requested_point_id) {
  idle();

  if (!record_valid_) {
    SERIAL_ECHOLNPGM("FATX: state=EMPTY result=NONE");
    return;
  }

  if (has_point_id && requested_point_id != point_id_) {
    SERIAL_ECHOPGM("FATX: error=NOT_FOUND requested_point_id="); SERIAL_ECHO(requested_point_id);
    SERIAL_ECHOPGM(" retained_point_id="); SERIAL_ECHO(point_id_);
    SERIAL_ECHOLNPGM("");
    return;
  }

  report_transaction("query");
  makeit_fa_phase0.report_test_point();
  makeit_fa_phase0.report_pulse_gap_monitor();
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
