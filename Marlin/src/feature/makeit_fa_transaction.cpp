/**
 * MAKEiT Filament Analyzer - Phase 5/6 transaction wrapper
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "makeit_fa_transaction.h"
#include "makeit_filament_analyzer_phase0.h"
#include "../core/serial.h"
#include "../MarlinCore.h"

#if ENABLED(EMERGENCY_PARSER)
  #include "e_parser.h"
#endif

MakeItFATransaction::State MakeItFATransaction::state_ = MakeItFATransaction::TX_EMPTY;
bool MakeItFATransaction::record_valid_ = false;
bool MakeItFATransaction::abort_requested_ = false;
uint32_t MakeItFATransaction::point_id_ = 0;
uint32_t MakeItFATransaction::params_hash_ = 0;
uint32_t MakeItFATransaction::started_ms_ = 0;
uint32_t MakeItFATransaction::finished_ms_ = 0;
uint32_t MakeItFATransaction::abort_requested_ms_ = 0;
uint32_t MakeItFATransaction::result_generation_ = 0;
uint8_t MakeItFATransaction::result_code_ = 0;
uint32_t MakeItFATransaction::result_crc_ = 0;

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

uint32_t MakeItFATransaction::crc32_word(uint32_t crc, const uint32_t word) {
  for (uint8_t byte_index = 0; byte_index < 4; ++byte_index) {
    crc ^= uint8_t(word >> (byte_index * 8));
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320UL & uint32_t(-int32_t(crc & 1UL)));
  }
  return crc;
}

void MakeItFATransaction::capture_terminal_result() {
  result_generation_ = makeit_fa_phase0.test_point_generation();
  result_code_ = makeit_fa_phase0.test_point_result_code();

  uint32_t crc = 0xFFFFFFFFUL;
  crc = crc32_word(crc, point_id_);
  crc = crc32_word(crc, params_hash_);
  crc = crc32_word(crc, uint32_t(state_));
  crc = crc32_word(crc, started_ms_);
  crc = crc32_word(crc, finished_ms_);
  crc = crc32_word(crc, abort_requested_ ? 1UL : 0UL);
  crc = crc32_word(crc, abort_requested_ms_);
  crc = crc32_word(crc, result_generation_);
  crc = crc32_word(crc, result_code_);
  crc = crc32_word(crc, makeit_fa_phase0.test_point_actual_events());
  crc = crc32_word(crc, float_bits(makeit_fa_phase0.test_point_tested_mm()));
  crc = crc32_word(crc, float_bits(makeit_fa_phase0.test_point_efficiency_pct()));
  result_crc_ = crc ^ 0xFFFFFFFFUL;
}

const char* MakeItFATransaction::state_name(const State state) {
  switch (state) {
    case TX_RUNNING:  return "RUNNING";
    case TX_ABORTING: return "ABORTING";
    case TX_TERMINAL: return "TERMINAL";
    case TX_ABORTED:  return "ABORTED";
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
  SERIAL_ECHOPGM(" abort_requested="); SERIAL_ECHO(abort_requested_ ? 1 : 0);
  SERIAL_ECHOPGM(" abort_requested_ms="); SERIAL_ECHO(abort_requested_ms_);
  SERIAL_ECHOPGM(" result_generation="); SERIAL_ECHO(result_generation_);
  SERIAL_ECHOPGM(" result_code="); SERIAL_ECHO(result_code_);
  SERIAL_ECHOPGM(" result_crc="); SERIAL_ECHO(result_crc_);
  SERIAL_ECHOLNPGM("");
}

void MakeItFATransaction::idle() {
  #if ENABLED(EMERGENCY_PARSER)
    if (EmergencyParser::abort_by_M879) {
      EmergencyParser::abort_by_M879 = false;
      request_abort_current("emergency");
    }
  #endif

  if (state_ != TX_RUNNING && state_ != TX_ABORTING) return;

  if (!makeit_fa_phase0.test_point_active() && !makeit_fa_phase0.segmented_feed_active()) {
    state_ = abort_requested_ ? TX_ABORTED : TX_TERMINAL;
    finished_ms_ = millis();
    capture_terminal_result();
    report_transaction(abort_requested_ ? "aborted" : "completed");
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

  if (state_ == TX_RUNNING || state_ == TX_ABORTING || makeit_fa_phase0.test_point_active() || makeit_fa_phase0.segmented_feed_active()) {
    SERIAL_ECHOPGM("FATX: error=BUSY active_point_id="); SERIAL_ECHO(point_id_);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  record_valid_ = true;
  abort_requested_ = false;
  point_id_ = point_id;
  params_hash_ = hash;
  started_ms_ = millis();
  finished_ms_ = 0;
  abort_requested_ms_ = 0;
  result_generation_ = 0;
  result_code_ = 0;
  result_crc_ = 0;
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
    capture_terminal_result();
    report_transaction("rejected");
    return false;
  }

  report_transaction("started");
  return true;
}

bool MakeItFATransaction::request_abort_current(const char *source) {
  if (abort_requested_) {
    report_transaction("abort_replay");
    return true;
  }

  if (state_ != TX_RUNNING || !makeit_fa_phase0.test_point_active()) {
    SERIAL_ECHOPGM("FATX: error=NO_RUNNING_POINT source="); SERIAL_ECHO(source);
    SERIAL_ECHOPGM(" point_id="); SERIAL_ECHO(point_id_);
    SERIAL_ECHOPGM(" state="); SERIAL_ECHO(state_name(state_));
    SERIAL_ECHOLNPGM("");
    return false;
  }

  // Flag first, then request the controlled drain stop.
  abort_requested_ = true;
  abort_requested_ms_ = millis();
  state_ = TX_ABORTING;

  if (!makeit_fa_phase0.request_host_abort()) {
    abort_requested_ = false;
    abort_requested_ms_ = 0;
    state_ = TX_RUNNING;
    SERIAL_ECHOPGM("FATX: error=ABORT_REJECTED source="); SERIAL_ECHO(source);
    SERIAL_ECHOPGM(" point_id="); SERIAL_ECHO(point_id_);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  SERIAL_ECHOPGM("FATX: abort_source="); SERIAL_ECHO(source);
  SERIAL_ECHOPGM(" point_id="); SERIAL_ECHO(point_id_);
  SERIAL_ECHOLNPGM("");
  report_transaction("abort_requested");
  return true;
}

bool MakeItFATransaction::request_abort(const uint32_t requested_point_id) {
  idle();

  if (!record_valid_) {
    SERIAL_ECHOLNPGM("FATX: error=NO_ACTIVE_TRANSACTION");
    return false;
  }

  if (requested_point_id != point_id_) {
    SERIAL_ECHOPGM("FATX: error=NOT_FOUND requested_point_id="); SERIAL_ECHO(requested_point_id);
    SERIAL_ECHOPGM(" retained_point_id="); SERIAL_ECHO(point_id_);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  return request_abort_current("gcode");
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
