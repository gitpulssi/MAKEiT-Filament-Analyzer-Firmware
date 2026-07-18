/**
 * MAKEiT Filament Analyzer - Phase 0 / 1 / 2 / 3 bring-up
 *
 * Purpose:
 *   - Count filament encoder events.
 *   - Provide reset/report/stream control via M875.
 *   - Run an open-loop segmented E-only feed diagnostic via M874.
 *   - Run one evaluated extrusion point via M873.
 *   - Optionally monitor rolling feed efficiency and gracefully stop adding
 *     segments after confirmed mid-point feed loss.
 *   - Optionally stream raw telemetry on a dedicated one-way UART.
 *
 * This file intentionally does NOT implement:
 *   - automatic temperature sweep / Qmax campaign logic
 *   - recovery logic
 *   - TMC load classification
 *   - M877/M878/M879 production transaction layer
 */
#pragma once

#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include <stdint.h>

#ifndef MAKEIT_FA_TELEM_INTERVAL_MS
  #define MAKEIT_FA_TELEM_INTERVAL_MS 200UL
#endif

#ifndef MAKEIT_FA_ENCODER_TRIGGER_NAME
  #define MAKEIT_FA_ENCODER_TRIGGER_NAME "RISING"
#endif

class MakeItFilamentAnalyzerPhase0 {
public:
  enum TestPointResult : uint8_t {
    TP_RESULT_NONE = 0,
    TP_RESULT_PASS,
    TP_RESULT_LOW_FEED,
    TP_RESULT_INVALID_TEMP,
    TP_RESULT_ERROR
  };

  static void init();
  static void idle();

  static void reset_encoder();
  static void set_stream_enabled(const bool enabled);
  static void set_stream_interval_ms(const uint32_t interval_ms);

  static uint32_t encoder_events();
  static uint32_t last_edge_us();
  static uint8_t encoder_pin_state();

  static void poll_encoder();
  static void report_to_host();

  /**
   * Start a non-blocking, open-loop segmented E-only feed diagnostic.
   * Segments are enqueued from idle() so the watchdog and Marlin background
   * tasks remain serviced.
   */
  static bool run_segmented_feed_test(
    float total_mm,
    float feed_mm_min,
    float segment_mm,
    uint8_t max_inflight,
    uint16_t report_ms
  );

  /**
   * Start one evaluated extrusion point.
   *
   * The hotend must already be at a non-zero target and within temp_tolerance
   * of that target. The command resets the encoder, runs the validated
   * segmented-motion engine, samples temperature and heater power, and reports
   * PASS, LOW_FEED, INVALID_TEMP, or ERROR.
   *
   * When auto_stop_enabled is true, rolling encoder efficiency is checked in
   * monitor_window_mm windows. After monitor_confirm_windows consecutive
   * windows below monitor_efficiency_pct, the engine stops adding new segments
   * and gracefully drains the at-most-two already committed planner blocks.
   */
  static bool start_evaluated_test_point(
    float total_mm,
    float feed_mm_min,
    float segment_mm,
    uint8_t max_inflight,
    uint16_t report_ms,
    float encoder_events_per_mm,
    float pass_efficiency_pct,
    float temp_tolerance,
    bool auto_stop_enabled,
    float monitor_window_mm,
    float monitor_efficiency_pct,
    uint8_t monitor_confirm_windows
  );

  static void report_test_point();
  static bool test_point_active() { return tp_active_; }
  static bool segmented_feed_active() { return seg_active_; }

private:
  static volatile uint32_t encoder_events_;
  static volatile uint32_t last_edge_us_;

  static bool initialized_;
  static bool stream_enabled_;
  static bool poll_initialized_;
  static bool last_pin_state_;
  static uint32_t stream_interval_ms_;
  static uint32_t next_stream_ms_;
  static uint32_t seq_;

  static bool seg_active_;
  static bool seg_draining_;
  static float seg_total_mm_;
  static float seg_feed_mm_min_;
  static float seg_segment_mm_;
  static float seg_commanded_mm_;
  static uint8_t seg_max_inflight_;
  static uint16_t seg_report_ms_;
  static uint32_t seg_seq_;
  static uint32_t seg_enqueued_segments_;
  static uint32_t seg_next_report_ms_;
  static feedRate_t seg_old_feedrate_;

  static bool tp_active_;
  static bool tp_has_result_;
  static bool tp_temp_valid_;
  static TestPointResult tp_result_;
  static uint32_t tp_generation_;
  static uint32_t tp_started_ms_;
  static uint32_t tp_finished_ms_;
  static uint32_t tp_next_sample_ms_;
  static float tp_total_mm_;
  static float tp_tested_mm_;
  static float tp_feed_mm_min_;
  static float tp_encoder_events_per_mm_;
  static float tp_pass_efficiency_pct_;
  static float tp_temp_target_;
  static float tp_temp_tolerance_;
  static float tp_temp_sum_;
  static float tp_temp_min_;
  static float tp_temp_max_;
  static uint32_t tp_temp_samples_;
  static uint32_t tp_heater_sum_;
  static uint32_t tp_heater_samples_;
  static float tp_expected_events_;
  static uint32_t tp_actual_events_;
  static float tp_efficiency_pct_;

  static bool tp_auto_stop_enabled_;
  static bool tp_abort_triggered_;
  static float tp_monitor_window_mm_;
  static float tp_monitor_efficiency_pct_;
  static uint8_t tp_monitor_confirm_windows_;
  static uint8_t tp_monitor_failed_windows_;
  static float tp_monitor_last_completed_mm_;
  static uint32_t tp_monitor_last_events_;
  static float tp_monitor_last_efficiency_pct_;
  static float tp_abort_commanded_mm_;

  static void encoder_isr();
  static void count_encoder_event();
  static void service_segmented_feed();
  static void request_segmented_stop();
  static float estimated_completed_mm();
  static void service_test_point();
  static void monitor_test_point_feed();
  static void sample_test_point();
  static void finish_test_point();
  static const char* test_point_result_name(const TestPointResult result);
  static void telemetry_line();
  static void telemetry_print_line(const uint32_t seq, const uint32_t ms, const uint32_t enc, const uint32_t edge_us, const uint8_t pin_state);
  static void segmented_feed_telemetry(const char *tag, const uint32_t seq, const float commanded_mm, const float total_mm, const uint8_t planned_blocks, const uint8_t max_inflight);
};

extern MakeItFilamentAnalyzerPhase0 makeit_fa_phase0;

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
