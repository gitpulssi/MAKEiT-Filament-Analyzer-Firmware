/**
 * MAKEiT Filament Analyzer - Phase 9 recovery / re-prime controller
 */
#pragma once

#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include <stdint.h>
#include "makeit_fa_transaction.h"

struct MakeItFARecoveryParams {
  float recovery_temp_c;
  float return_temp_c;
  uint16_t settle_seconds;
  float prime_mm;
  float prime_feed_mm_min;
  MakeItFAPointParams validation;
};

class MakeItFARecovery {
public:
  enum State : uint8_t {
    REC_EMPTY = 0,
    REC_WAIT_TEMP,
    REC_PRIMING,
    REC_VALIDATING,
    REC_COMPLETE,
    REC_FAILED,
    REC_ABORTED,
    REC_ERROR
  };

  static void idle();
  static bool start(uint32_t recovery_id, const MakeItFARecoveryParams &params);
  static void query(bool has_recovery_id, uint32_t recovery_id);
  static bool cancel(bool has_recovery_id, uint32_t recovery_id);

  static bool active() {
    return state_ == REC_WAIT_TEMP || state_ == REC_PRIMING || state_ == REC_VALIDATING;
  }
  static bool terminal() {
    return state_ == REC_COMPLETE || state_ == REC_FAILED
        || state_ == REC_ABORTED || state_ == REC_ERROR;
  }
  static bool has_record() { return record_valid_; }
  static State state() { return state_; }
  static uint32_t current_recovery_id() { return recovery_id_; }
  static uint8_t validation_result_code() { return validation_result_code_; }
  static uint32_t validation_result_crc() { return validation_result_crc_; }
  static uint32_t result_crc() { return result_crc_; }

private:
  static State state_;
  static bool record_valid_;
  static bool cancel_requested_;
  static bool prime_stop_requested_;
  static uint32_t recovery_id_;
  static uint32_t params_hash_;
  static uint32_t started_ms_;
  static uint32_t finished_ms_;
  static uint32_t stable_since_ms_;
  static uint32_t next_report_ms_;
  static float settle_band_c_;
  static float stable_temp_min_c_;
  static float stable_temp_max_c_;
  static uint8_t validation_result_code_;
  static uint32_t validation_result_crc_;
  static uint32_t result_crc_;
  static MakeItFARecoveryParams params_;

  static uint32_t float_bits(float value);
  static uint32_t hash_word(uint32_t hash, uint32_t word);
  static uint32_t crc32_word(uint32_t crc, uint32_t word);
  static uint32_t hash_parameters(const MakeItFARecoveryParams &params);
  static const char* state_name(State state);
  static void report(const char *tag);
  static void finish(State terminal_state, const char *tag);
  static void restore_return_target();
  static void capture_result_crc();
  static bool start_prime();
  static bool start_validation();
};

extern MakeItFARecovery makeit_fa_recovery;

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
