/**
 * MAKEiT Filament Analyzer - Phase 0 / Phase 1 bring-up
 *
 * Purpose:
 *   - Count filament encoder events.
 *   - Provide reset/report/stream control via M875.
 *   - Run an open-loop segmented E-only feed diagnostic via M874.
 *   - Optionally stream raw telemetry on a dedicated one-way UART.
 *
 * This file intentionally does NOT implement:
 *   - feed-efficiency pass/fail
 *   - automatic extrusion stop from encoder data
 *   - Qmax campaign logic
 *   - recovery logic
 *   - M877/M878/M879 transaction layer
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
   * Open-loop segmented E-only feed diagnostic.
   *
   * total_mm:     total forward filament length to command.
   * feed_mm_min:  filament feed rate in mm/min.
   * segment_mm:   commanded length of each E-only segment.
   * max_inflight: maximum planner blocks allowed to be queued by this test.
   * report_ms:    telemetry/report interval while the blocking test runs.
   */
  static void run_segmented_feed_test(float total_mm, float feed_mm_min, float segment_mm, uint8_t max_inflight, uint16_t report_ms);

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

  static void encoder_isr();
  static void count_encoder_event();
  static void telemetry_line();
  static void telemetry_print_line(const uint32_t seq, const uint32_t ms, const uint32_t enc, const uint32_t edge_us, const uint8_t pin_state);
  static void segmented_feed_telemetry(const char *tag, const uint32_t seq, const float commanded_mm, const float total_mm, const uint8_t planned_blocks, const uint8_t max_inflight);
};

extern MakeItFilamentAnalyzerPhase0 makeit_fa_phase0;

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
