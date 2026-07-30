/**
 * MAKEiT Filament Analyzer - Phase 7 fixed-temperature speed campaign
 *
 * Runs a non-blocking feed-speed ladder at the already configured hotend
 * target. Each speed is executed through the Phase-5/6 transaction wrapper,
 * so point execution remains idempotent and queryable.
 */
#pragma once

#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include <stdint.h>
#include "makeit_fa_transaction.h"

struct MakeItFASpeedCampaignParams {
  float start_feed_mm_min;
  float max_feed_mm_min;
  float step_feed_mm_min;
  uint16_t settle_seconds;
  MakeItFAPointParams point;
};

class MakeItFASpeedCampaign {
public:
  enum State : uint8_t {
    CAM_EMPTY = 0,
    CAM_WAIT_TEMP,
    CAM_RUNNING_POINT,
    CAM_COMPLETE,
    CAM_LIMIT_FOUND,
    CAM_INVALID_TEMP,
    CAM_ABORTED,
    CAM_ERROR
  };

  static void idle();

  /**
   * Start a fixed-temperature speed ladder.
   *
   * campaign_id is also the first point ID. Later point IDs are
   * campaign_id + point_index, so the host must reserve that ID range.
   */
  static bool start(const uint32_t campaign_id, const MakeItFASpeedCampaignParams &params);

  /** Side-effect-free current/latest campaign query. */
  static void query(const bool has_campaign_id, const uint32_t campaign_id);

  /** Cancel while waiting, or gracefully abort the active campaign point. */
  static bool cancel(const bool has_campaign_id, const uint32_t campaign_id);

  static bool active() {
    return state_ == CAM_WAIT_TEMP || state_ == CAM_RUNNING_POINT;
  }

  // Read-only summary access for the Phase-8 temperature-envelope controller.
  static bool has_record() { return record_valid_; }
  static bool terminal() {
    return state_ == CAM_COMPLETE || state_ == CAM_LIMIT_FOUND
        || state_ == CAM_INVALID_TEMP || state_ == CAM_ABORTED
        || state_ == CAM_ERROR;
  }
  static State state() { return state_; }
  static uint32_t current_campaign_id() { return campaign_id_; }
  static uint32_t current_params_hash() { return params_hash_; }
  static uint16_t point_count() { return point_count_; }
  static uint32_t current_point_id() { return current_point_id_; }
  static float target_temp() { return target_temp_; }
  static float current_feed_mm_min() { return current_feed_mm_min_; }
  static float last_pass_feed_mm_min() { return last_pass_feed_mm_min_; }
  static float first_fail_feed_mm_min() { return first_fail_feed_mm_min_; }
  static uint8_t last_point_result_code() { return last_point_result_code_; }
  static uint32_t last_point_result_crc() { return last_point_result_crc_; }

private:
  static State state_;
  static bool record_valid_;
  static bool cancel_requested_;
  static uint32_t campaign_id_;
  static uint32_t params_hash_;
  static uint16_t point_index_;
  static uint16_t point_count_;
  static uint32_t current_point_id_;
  static uint32_t started_ms_;
  static uint32_t finished_ms_;
  static uint32_t stable_since_ms_;
  static uint32_t next_report_ms_;
  static float target_temp_;
  static float current_feed_mm_min_;
  static float last_pass_feed_mm_min_;
  static float first_fail_feed_mm_min_;
  static uint8_t last_point_result_code_;
  static uint32_t last_point_result_crc_;
  static MakeItFASpeedCampaignParams params_;

  static uint32_t float_bits(float value);
  static uint32_t hash_word(uint32_t hash, uint32_t word);
  static uint32_t hash_parameters(const MakeItFASpeedCampaignParams &params);
  static uint16_t count_points(float start_feed, float max_feed, float step_feed);
  static const char* state_name(State state);
  static void report(const char *tag);
  static void finish(State terminal_state, const char *tag);
  static void enter_wait_temp();
  static bool start_current_point();
  static void handle_point_result();
};

extern MakeItFASpeedCampaign makeit_fa_campaign;

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
