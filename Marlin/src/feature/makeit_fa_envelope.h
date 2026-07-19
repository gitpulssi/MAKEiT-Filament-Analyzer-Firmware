/**
 * MAKEiT Filament Analyzer - Phase 8/9 temperature / speed envelope
 *
 * Phase 9 adds a validated recovery / re-prime step so an envelope can continue
 * to hotter rows after a real low-temperature feed limit.
 */
#pragma once

#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include <stdint.h>
#include "makeit_fa_campaign.h"

struct MakeItFATemperatureEnvelopeParams {
  float start_temp_c;             // Filled from the current hotend target.
  float max_temp_c;
  float temp_step_c;
  float filament_diameter_mm;
  float recovery_temp_c;          // 0 disables recovery; otherwise normally max_temp_c.
  MakeItFASpeedCampaignParams speed;
};

class MakeItFATemperatureEnvelope {
public:
  static constexpr uint8_t MAX_ROWS = 24;

  enum State : uint8_t {
    ENV_EMPTY = 0,
    ENV_RUNNING_ROW,
    ENV_RECOVERING,
    ENV_COMPLETE,
    ENV_LIMIT_FOUND,
    ENV_RECOVERY_FAILED,
    ENV_INVALID_TEMP,
    ENV_ABORTED,
    ENV_ERROR
  };

  struct RowResult {
    float temp_c;
    float last_pass_feed_mm_min;
    float first_fail_feed_mm_min;
    uint32_t row_campaign_id;
    uint32_t last_point_crc;
    uint32_t recovery_id;
    uint32_t recovery_crc;
    uint8_t campaign_state;
    uint8_t last_point_result_code;
    bool recovery_attempted;
    bool recovery_passed;
  };

  static void idle();
  static bool start(uint32_t envelope_id, const MakeItFATemperatureEnvelopeParams &params);
  static void query(bool has_envelope_id, uint32_t envelope_id);
  static bool cancel(bool has_envelope_id, uint32_t envelope_id);

  static bool active() { return state_ == ENV_RUNNING_ROW || state_ == ENV_RECOVERING; }
  static bool has_record() { return record_valid_; }
  static State state() { return state_; }
  static uint32_t result_crc() { return result_crc_; }

private:
  static State state_;
  static bool record_valid_;
  static bool cancel_requested_;
  static uint32_t envelope_id_;
  static uint32_t params_hash_;
  static uint32_t started_ms_;
  static uint32_t finished_ms_;
  static uint32_t current_row_campaign_id_;
  static uint32_t current_recovery_id_;
  static uint32_t result_crc_;
  static uint16_t speed_points_per_row_;
  static uint16_t row_id_stride_;
  static uint8_t row_index_;
  static uint8_t row_count_;
  static uint8_t completed_rows_;
  static float original_target_temp_c_;
  static float current_temp_c_;
  static MakeItFATemperatureEnvelopeParams params_;
  static RowResult rows_[MAX_ROWS];

  static uint32_t float_bits(float value);
  static uint32_t hash_word(uint32_t hash, uint32_t word);
  static uint32_t crc32_word(uint32_t crc, uint32_t word);
  static uint32_t hash_parameters(const MakeItFATemperatureEnvelopeParams &params);
  static uint16_t count_speed_points(float start_feed, float max_feed, float step_feed);
  static uint8_t count_temperature_rows(float start_temp, float max_temp, float temp_step);
  static const char* state_name(State state);
  static const char* row_state_name(MakeItFASpeedCampaign::State state);
  static float feed_to_q_mm3_s(float feed_mm_min, float diameter_mm);
  static void normalize_speed_params(MakeItFASpeedCampaignParams &params);
  static bool start_current_row();
  static bool start_recovery_for_next_row();
  static void handle_row_result();
  static void handle_recovery_result();
  static void emit_row(uint8_t row);
  static void report(const char *tag, bool include_rows=false);
  static void finish(State terminal_state, const char *tag);
  static void restore_original_target();
  static void capture_result_crc();
};

extern MakeItFATemperatureEnvelope makeit_fa_envelope;

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
