/**
 * MAKEiT Filament Analyzer - Phase 5/6 transaction wrapper
 *
 * Provides a single-record, RAM-only, idempotent execute/query layer around
 * the non-blocking evaluated test point, plus a point-ID-qualified graceful
 * host abort. Results intentionally do not survive a controller reset because
 * reset also invalidates the physical feed context.
 */
#pragma once

#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include <stdint.h>

struct MakeItFAPointParams {
  float total_mm;
  float feed_mm_min;
  float segment_mm;
  uint8_t max_inflight;
  uint16_t report_ms;
  float encoder_events_per_mm;
  float pass_efficiency_pct;
  float temp_tolerance;
  bool auto_stop_enabled;
  float monitor_window_mm;
  float monitor_efficiency_pct;
  uint8_t monitor_confirm_windows;
  float pulse_gap_factor;
  uint16_t pulse_gap_min_ms;
  float pulse_gap_min_missing_events;
};

class MakeItFATransaction {
public:
  enum State : uint8_t {
    TX_EMPTY = 0,
    TX_RUNNING,
    TX_TERMINAL
  };

  static void idle();

  /** Execute one point exactly once for the retained point ID/hash pair. */
  static bool execute(const uint32_t point_id, const MakeItFAPointParams &params);

  /** Query the active/latest retained point without side effects. */
  static void query(const bool has_point_id, const uint32_t point_id);

  /**
   * Request a controlled drain stop for the active point.
   * Repeating the request is idempotent. A mismatched point ID is rejected.
   */
  static bool request_abort(const uint32_t point_id);

  static bool running() { return state_ == TX_RUNNING; }
  static uint32_t current_point_id() { return point_id_; }
  static uint32_t current_params_hash() { return params_hash_; }
  static bool abort_requested() { return abort_requested_; }

private:
  static State state_;
  static bool record_valid_;
  static bool abort_requested_;
  static uint32_t point_id_;
  static uint32_t params_hash_;
  static uint32_t started_ms_;
  static uint32_t finished_ms_;
  static uint32_t abort_requested_ms_;

  static uint32_t hash_parameters(const MakeItFAPointParams &params);
  static uint32_t hash_word(uint32_t hash, uint32_t word);
  static uint32_t float_bits(float value);
  static const char* state_name(State state);
  static void report_transaction(const char *tag);
};

extern MakeItFATransaction makeit_fa_transaction;

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
