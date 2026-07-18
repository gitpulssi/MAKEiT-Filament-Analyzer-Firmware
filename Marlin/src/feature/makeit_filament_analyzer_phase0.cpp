/**
 * MAKEiT Filament Analyzer - Phase 0 / Phase 1 / Phase 2 bring-up
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "makeit_filament_analyzer_phase0.h"
#include "../core/serial.h"
#include "../module/motion.h"
#include "../module/planner.h"
#include "../module/temperature.h"
#include "../MarlinCore.h"

#if !PIN_EXISTS(MAKEIT_FA_ENCODER)
  #error "MAKEIT_FILAMENT_ANALYZER_PHASE0 requires MAKEIT_FA_ENCODER_PIN."
#endif

#ifndef MAKEIT_FA_ENCODER_INTERRUPT_MODE
  #define MAKEIT_FA_ENCODER_INTERRUPT_MODE RISING
#endif

#ifndef MAKEIT_FA_ENCODER_USE_POLLING
  #define MAKEIT_FA_ENCODER_USE_POLLING 1
#endif

#ifndef MAKEIT_FA_TELEM_BAUD
  #define MAKEIT_FA_TELEM_BAUD 250000
#endif

#if !defined(MAKEIT_FA_TELEM_SERIAL)
  #define MAKEIT_FA_TELEM_AVAILABLE 0
#else
  #define MAKEIT_FA_TELEM_AVAILABLE 1
#endif

#ifndef CRITICAL_SECTION_START
  #define CRITICAL_SECTION_START() noInterrupts()
  #define CRITICAL_SECTION_END()   interrupts()
#endif

volatile uint32_t MakeItFilamentAnalyzerPhase0::encoder_events_ = 0;
volatile uint32_t MakeItFilamentAnalyzerPhase0::last_edge_us_ = 0;

bool MakeItFilamentAnalyzerPhase0::initialized_ = false;
bool MakeItFilamentAnalyzerPhase0::stream_enabled_ = false;
bool MakeItFilamentAnalyzerPhase0::poll_initialized_ = false;
bool MakeItFilamentAnalyzerPhase0::last_pin_state_ = false;
uint32_t MakeItFilamentAnalyzerPhase0::stream_interval_ms_ = MAKEIT_FA_TELEM_INTERVAL_MS;
uint32_t MakeItFilamentAnalyzerPhase0::next_stream_ms_ = 0;
uint32_t MakeItFilamentAnalyzerPhase0::seq_ = 0;

bool MakeItFilamentAnalyzerPhase0::seg_active_ = false;
bool MakeItFilamentAnalyzerPhase0::seg_draining_ = false;
float MakeItFilamentAnalyzerPhase0::seg_total_mm_ = 0.0f;
float MakeItFilamentAnalyzerPhase0::seg_feed_mm_min_ = 0.0f;
float MakeItFilamentAnalyzerPhase0::seg_segment_mm_ = 0.35f;
float MakeItFilamentAnalyzerPhase0::seg_commanded_mm_ = 0.0f;
uint8_t MakeItFilamentAnalyzerPhase0::seg_max_inflight_ = 2;
uint16_t MakeItFilamentAnalyzerPhase0::seg_report_ms_ = 250;
uint32_t MakeItFilamentAnalyzerPhase0::seg_seq_ = 0;
uint32_t MakeItFilamentAnalyzerPhase0::seg_enqueued_segments_ = 0;
uint32_t MakeItFilamentAnalyzerPhase0::seg_next_report_ms_ = 0;
feedRate_t MakeItFilamentAnalyzerPhase0::seg_old_feedrate_ = 0.0f;

bool MakeItFilamentAnalyzerPhase0::tp_active_ = false;
bool MakeItFilamentAnalyzerPhase0::tp_has_result_ = false;
bool MakeItFilamentAnalyzerPhase0::tp_temp_valid_ = false;
MakeItFilamentAnalyzerPhase0::TestPointResult MakeItFilamentAnalyzerPhase0::tp_result_ = MakeItFilamentAnalyzerPhase0::TP_RESULT_NONE;
uint32_t MakeItFilamentAnalyzerPhase0::tp_generation_ = 0;
uint32_t MakeItFilamentAnalyzerPhase0::tp_started_ms_ = 0;
uint32_t MakeItFilamentAnalyzerPhase0::tp_finished_ms_ = 0;
uint32_t MakeItFilamentAnalyzerPhase0::tp_next_sample_ms_ = 0;
float MakeItFilamentAnalyzerPhase0::tp_total_mm_ = 0.0f;
float MakeItFilamentAnalyzerPhase0::tp_feed_mm_min_ = 0.0f;
float MakeItFilamentAnalyzerPhase0::tp_encoder_events_per_mm_ = 0.685f;
float MakeItFilamentAnalyzerPhase0::tp_pass_efficiency_pct_ = 95.0f;
float MakeItFilamentAnalyzerPhase0::tp_temp_target_ = 0.0f;
float MakeItFilamentAnalyzerPhase0::tp_temp_tolerance_ = 2.0f;
float MakeItFilamentAnalyzerPhase0::tp_temp_sum_ = 0.0f;
float MakeItFilamentAnalyzerPhase0::tp_temp_min_ = 0.0f;
float MakeItFilamentAnalyzerPhase0::tp_temp_max_ = 0.0f;
uint32_t MakeItFilamentAnalyzerPhase0::tp_temp_samples_ = 0;
uint32_t MakeItFilamentAnalyzerPhase0::tp_heater_sum_ = 0;
uint32_t MakeItFilamentAnalyzerPhase0::tp_heater_samples_ = 0;
float MakeItFilamentAnalyzerPhase0::tp_expected_events_ = 0.0f;
uint32_t MakeItFilamentAnalyzerPhase0::tp_actual_events_ = 0;
float MakeItFilamentAnalyzerPhase0::tp_efficiency_pct_ = 0.0f;

MakeItFilamentAnalyzerPhase0 makeit_fa_phase0;

void MakeItFilamentAnalyzerPhase0::count_encoder_event() {
  ++encoder_events_;
  last_edge_us_ = micros();

  #if PIN_EXISTS(MAKEIT_FA_MARKER_ENCODER)
    WRITE(MAKEIT_FA_MARKER_ENCODER_PIN, HIGH);
    WRITE(MAKEIT_FA_MARKER_ENCODER_PIN, LOW);
  #endif
}

void MakeItFilamentAnalyzerPhase0::encoder_isr() {
  // ISR rule: count/timestamp only. No serial, no TMC UART, no allocation.
  count_encoder_event();
}

uint32_t MakeItFilamentAnalyzerPhase0::encoder_events() {
  uint32_t v;
  CRITICAL_SECTION_START();
  v = encoder_events_;
  CRITICAL_SECTION_END();
  return v;
}

uint32_t MakeItFilamentAnalyzerPhase0::last_edge_us() {
  uint32_t v;
  CRITICAL_SECTION_START();
  v = last_edge_us_;
  CRITICAL_SECTION_END();
  return v;
}

uint8_t MakeItFilamentAnalyzerPhase0::encoder_pin_state() {
  return READ(MAKEIT_FA_ENCODER_PIN) ? 1 : 0;
}

void MakeItFilamentAnalyzerPhase0::poll_encoder() {
  const bool current = !!encoder_pin_state();

  if (!poll_initialized_) {
    last_pin_state_ = current;
    poll_initialized_ = true;
    return;
  }

  if (current == last_pin_state_) return;

  bool should_count = false;

  #if MAKEIT_FA_ENCODER_INTERRUPT_MODE == CHANGE
    should_count = true;
  #elif MAKEIT_FA_ENCODER_INTERRUPT_MODE == RISING
    should_count = (!last_pin_state_ && current);
  #elif MAKEIT_FA_ENCODER_INTERRUPT_MODE == FALLING
    should_count = (last_pin_state_ && !current);
  #else
    // Unknown mode: count every transition for diagnostics.
    should_count = true;
  #endif

  last_pin_state_ = current;

  if (should_count) {
    CRITICAL_SECTION_START();
    count_encoder_event();
    CRITICAL_SECTION_END();
  }
}

void MakeItFilamentAnalyzerPhase0::reset_encoder() {
  CRITICAL_SECTION_START();
  encoder_events_ = 0;
  last_edge_us_ = micros();
  CRITICAL_SECTION_END();

  // Reset the software edge detector to the current physical pin state so
  // the next count represents a real new edge, not the state at reset time.
  last_pin_state_ = !!encoder_pin_state();
  poll_initialized_ = true;
  seq_ = 0;
}

void MakeItFilamentAnalyzerPhase0::set_stream_enabled(const bool enabled) {
  stream_enabled_ = enabled;
  next_stream_ms_ = millis();
}

void MakeItFilamentAnalyzerPhase0::set_stream_interval_ms(const uint32_t interval_ms) {
  stream_interval_ms_ = constrain(interval_ms, 20UL, 5000UL);
}

void MakeItFilamentAnalyzerPhase0::init() {
  if (initialized_) return;

  #if ENABLED(MAKEIT_FA_ENCODER_PULLUP)
    SET_INPUT_PULLUP(MAKEIT_FA_ENCODER_PIN);
  #else
    SET_INPUT(MAKEIT_FA_ENCODER_PIN);
  #endif

  #if PIN_EXISTS(MAKEIT_FA_MARKER_ENCODER)
    OUT_WRITE(MAKEIT_FA_MARKER_ENCODER_PIN, LOW);
  #endif

  reset_encoder();

  #if !MAKEIT_FA_ENCODER_USE_POLLING
    attachInterrupt(MAKEIT_FA_ENCODER_PIN, encoder_isr, MAKEIT_FA_ENCODER_INTERRUPT_MODE);
  #endif

  #if MAKEIT_FA_TELEM_AVAILABLE
    MAKEIT_FA_TELEM_SERIAL.begin(MAKEIT_FA_TELEM_BAUD);
  #endif

  initialized_ = true;
  report_to_host();
}

void MakeItFilamentAnalyzerPhase0::idle() {
  if (!initialized_) return;

  #if MAKEIT_FA_ENCODER_USE_POLLING
    poll_encoder();
  #endif

  service_segmented_feed();
  service_test_point();

  if (!stream_enabled_) return;

  const uint32_t now = millis();
  if ((int32_t)(now - next_stream_ms_) >= 0) {
    next_stream_ms_ = now + stream_interval_ms_;
    telemetry_line();
  }
}

void MakeItFilamentAnalyzerPhase0::telemetry_line() {
  #if MAKEIT_FA_ENCODER_USE_POLLING
    poll_encoder();
  #endif

  const uint32_t enc = encoder_events();
  const uint32_t edge_us = last_edge_us();
  const uint8_t pin_state = encoder_pin_state();
  telemetry_print_line(++seq_, millis(), enc, edge_us, pin_state);
}

void MakeItFilamentAnalyzerPhase0::telemetry_print_line(const uint32_t seq, const uint32_t ms, const uint32_t enc, const uint32_t edge_us, const uint8_t pin_state) {
  #if MAKEIT_FA_TELEM_AVAILABLE
    MAKEIT_FA_TELEM_SERIAL.print(F("FA0,"));
    MAKEIT_FA_TELEM_SERIAL.print(F("seq=")); MAKEIT_FA_TELEM_SERIAL.print(seq);
    MAKEIT_FA_TELEM_SERIAL.print(F(",ms=")); MAKEIT_FA_TELEM_SERIAL.print(ms);
    MAKEIT_FA_TELEM_SERIAL.print(F(",enc=")); MAKEIT_FA_TELEM_SERIAL.print(enc);
    MAKEIT_FA_TELEM_SERIAL.print(F(",last_edge_us=")); MAKEIT_FA_TELEM_SERIAL.print(edge_us);
    MAKEIT_FA_TELEM_SERIAL.print(F(",pin=")); MAKEIT_FA_TELEM_SERIAL.print(pin_state);
    MAKEIT_FA_TELEM_SERIAL.print(F(",mode=")); MAKEIT_FA_TELEM_SERIAL.print(F(MAKEIT_FA_ENCODER_TRIGGER_NAME));
    MAKEIT_FA_TELEM_SERIAL.println();
  #endif
}

void MakeItFilamentAnalyzerPhase0::segmented_feed_telemetry(const char *tag, const uint32_t seq, const float commanded_mm, const float total_mm, const uint8_t planned_blocks, const uint8_t max_inflight) {
  #if MAKEIT_FA_TELEM_AVAILABLE
    MAKEIT_FA_TELEM_SERIAL.print(F("FA1,"));
    MAKEIT_FA_TELEM_SERIAL.print(F("seq=")); MAKEIT_FA_TELEM_SERIAL.print(seq);
    MAKEIT_FA_TELEM_SERIAL.print(F(",ms=")); MAKEIT_FA_TELEM_SERIAL.print(millis());
    MAKEIT_FA_TELEM_SERIAL.print(F(",tag=")); MAKEIT_FA_TELEM_SERIAL.print(tag);
    MAKEIT_FA_TELEM_SERIAL.print(F(",cmd_mm=")); MAKEIT_FA_TELEM_SERIAL.print(commanded_mm, 3);
    MAKEIT_FA_TELEM_SERIAL.print(F(",total_mm=")); MAKEIT_FA_TELEM_SERIAL.print(total_mm, 3);
    MAKEIT_FA_TELEM_SERIAL.print(F(",blocks=")); MAKEIT_FA_TELEM_SERIAL.print(planned_blocks);
    MAKEIT_FA_TELEM_SERIAL.print(F(",max_blocks=")); MAKEIT_FA_TELEM_SERIAL.print(max_inflight);
    MAKEIT_FA_TELEM_SERIAL.print(F(",enc=")); MAKEIT_FA_TELEM_SERIAL.print(encoder_events());
    MAKEIT_FA_TELEM_SERIAL.print(F(",pin=")); MAKEIT_FA_TELEM_SERIAL.print(encoder_pin_state());
    MAKEIT_FA_TELEM_SERIAL.print(F(",temp=")); MAKEIT_FA_TELEM_SERIAL.print(thermalManager.degHotend(0), 2);
    MAKEIT_FA_TELEM_SERIAL.print(F(",target=")); MAKEIT_FA_TELEM_SERIAL.print(thermalManager.degTargetHotend(0));
    MAKEIT_FA_TELEM_SERIAL.print(F(",heater=")); MAKEIT_FA_TELEM_SERIAL.print(thermalManager.getHeaterPower(H_E0));
    MAKEIT_FA_TELEM_SERIAL.println();
  #endif
}

bool MakeItFilamentAnalyzerPhase0::run_segmented_feed_test(float total_mm, float feed_mm_min, float segment_mm, uint8_t max_inflight, uint16_t report_ms) {
  if (!initialized_) init();

  if (seg_active_) {
    SERIAL_ECHOLNPGM("FA1: busy");
    return false;
  }

  if (total_mm <= 0.0f || feed_mm_min <= 0.0f || segment_mm <= 0.0f) {
    SERIAL_ECHOLNPGM("FA1: invalid parameters");
    return false;
  }

  seg_total_mm_ = constrain(total_mm, 0.01f, 500.0f);
  seg_feed_mm_min_ = constrain(feed_mm_min, 1.0f, 2000.0f);
  seg_segment_mm_ = constrain(segment_mm, 0.05f, 0.35f);       // Preserve the 0.70mm cap with two blocks.
  seg_max_inflight_ = constrain(max_inflight, uint8_t(1), uint8_t(2));
  seg_report_ms_ = constrain(report_ms, uint16_t(50), uint16_t(5000));
  seg_commanded_mm_ = 0.0f;
  seg_seq_ = 0;
  seg_enqueued_segments_ = 0;
  seg_next_report_ms_ = millis();
  seg_old_feedrate_ = feedrate_mm_s;
  seg_draining_ = false;
  seg_active_ = true;

  sync_plan_position_e();

  segmented_feed_telemetry("start", ++seg_seq_, seg_commanded_mm_, seg_total_mm_, planner.movesplanned(), seg_max_inflight_);

  SERIAL_ECHOPGM("FA1: started total_mm="); SERIAL_ECHO(seg_total_mm_);
  SERIAL_ECHOPGM(" segment_mm="); SERIAL_ECHO(seg_segment_mm_);
  SERIAL_ECHOPGM(" feed_mm_min="); SERIAL_ECHO(seg_feed_mm_min_);
  SERIAL_ECHOPGM(" max_blocks="); SERIAL_ECHO(seg_max_inflight_);
  SERIAL_ECHOLNPGM("");
  return true;
}

void MakeItFilamentAnalyzerPhase0::service_segmented_feed() {
  if (!seg_active_) return;

  #if MAKEIT_FA_ENCODER_USE_POLLING
    poll_encoder();
  #endif

  if (!seg_draining_ && seg_commanded_mm_ < seg_total_mm_) {
    const uint8_t planned_blocks = planner.movesplanned();

    if (planned_blocks < seg_max_inflight_ && !planner.is_full()) {
      const float remaining = seg_total_mm_ - seg_commanded_mm_;
      const float this_segment = remaining < seg_segment_mm_ ? remaining : seg_segment_mm_;

      destination = current_position;
      destination.e += this_segment;
      feedrate_mm_s = seg_feed_mm_min_ / 60.0f;
      prepare_line_to_destination();

      seg_commanded_mm_ += this_segment;
      ++seg_enqueued_segments_;
    }
  }

  if (seg_commanded_mm_ >= seg_total_mm_)
    seg_draining_ = true;

  const uint32_t now = millis();
  if ((int32_t)(now - seg_next_report_ms_) >= 0) {
    seg_next_report_ms_ = now + seg_report_ms_;
    segmented_feed_telemetry(seg_draining_ ? "drain" : "run", ++seg_seq_, seg_commanded_mm_, seg_total_mm_, planner.movesplanned(), seg_max_inflight_);
  }

  if (seg_draining_ && planner.movesplanned() == 0) {
    feedrate_mm_s = seg_old_feedrate_;
    segmented_feed_telemetry("done", ++seg_seq_, seg_commanded_mm_, seg_total_mm_, planner.movesplanned(), seg_max_inflight_);

    SERIAL_ECHOPGM("FA1: done total_mm="); SERIAL_ECHO(seg_total_mm_);
    SERIAL_ECHOPGM(" segment_mm="); SERIAL_ECHO(seg_segment_mm_);
    SERIAL_ECHOPGM(" feed_mm_min="); SERIAL_ECHO(seg_feed_mm_min_);
    SERIAL_ECHOPGM(" max_blocks="); SERIAL_ECHO(seg_max_inflight_);
    SERIAL_ECHOPGM(" enqueued="); SERIAL_ECHO(seg_enqueued_segments_);
    SERIAL_ECHOPGM(" enc="); SERIAL_ECHO(encoder_events());
    SERIAL_ECHOLNPGM("");

    seg_active_ = false;
    seg_draining_ = false;
  }
}

bool MakeItFilamentAnalyzerPhase0::start_evaluated_test_point(
  float total_mm,
  float feed_mm_min,
  float segment_mm,
  uint8_t max_inflight,
  uint16_t report_ms,
  float encoder_events_per_mm,
  float pass_efficiency_pct,
  float temp_tolerance
) {
  if (!initialized_) init();

  if (tp_active_ || seg_active_) {
    SERIAL_ECHOLNPGM("FA2: busy");
    return false;
  }

  total_mm = constrain(total_mm, 20.0f, 500.0f);
  encoder_events_per_mm = constrain(encoder_events_per_mm, 0.01f, 100.0f);
  pass_efficiency_pct = constrain(pass_efficiency_pct, 50.0f, 105.0f);
  temp_tolerance = constrain(temp_tolerance, 0.5f, 15.0f);

  const float current_temp = thermalManager.degHotend(0);
  const float target_temp = thermalManager.degTargetHotend(0);

  ++tp_generation_;
  tp_has_result_ = false;
  tp_result_ = TP_RESULT_NONE;
  tp_total_mm_ = total_mm;
  tp_feed_mm_min_ = feed_mm_min;
  tp_encoder_events_per_mm_ = encoder_events_per_mm;
  tp_pass_efficiency_pct_ = pass_efficiency_pct;
  tp_temp_target_ = target_temp;
  tp_temp_tolerance_ = temp_tolerance;
  tp_expected_events_ = total_mm * encoder_events_per_mm;
  tp_actual_events_ = 0;
  tp_efficiency_pct_ = 0.0f;
  tp_started_ms_ = millis();
  tp_finished_ms_ = 0;
  tp_temp_sum_ = 0.0f;
  tp_temp_min_ = current_temp;
  tp_temp_max_ = current_temp;
  tp_temp_samples_ = 0;
  tp_heater_sum_ = 0;
  tp_heater_samples_ = 0;
  tp_temp_valid_ = true;

  if (target_temp <= 0.0f || !thermalManager.hotEnoughToExtrude(0)) {
    tp_result_ = TP_RESULT_ERROR;
    tp_has_result_ = true;
    tp_finished_ms_ = millis();
    SERIAL_ECHOPGM("FA2: result=ERROR gen="); SERIAL_ECHO(tp_generation_);
    SERIAL_ECHOPGM(" reason=cold_or_no_target temp="); SERIAL_ECHO(current_temp);
    SERIAL_ECHOPGM(" target="); SERIAL_ECHO(target_temp);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  if (ABS(current_temp - target_temp) > temp_tolerance) {
    tp_result_ = TP_RESULT_INVALID_TEMP;
    tp_has_result_ = true;
    tp_temp_valid_ = false;
    tp_finished_ms_ = millis();
    SERIAL_ECHOPGM("FA2: result=INVALID_TEMP gen="); SERIAL_ECHO(tp_generation_);
    SERIAL_ECHOPGM(" reason=not_stable temp="); SERIAL_ECHO(current_temp);
    SERIAL_ECHOPGM(" target="); SERIAL_ECHO(target_temp);
    SERIAL_ECHOPGM(" tolerance="); SERIAL_ECHO(temp_tolerance);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  reset_encoder();
  tp_active_ = true;
  tp_next_sample_ms_ = millis();
  sample_test_point();

  if (!run_segmented_feed_test(total_mm, feed_mm_min, segment_mm, max_inflight, report_ms)) {
    tp_active_ = false;
    tp_result_ = TP_RESULT_ERROR;
    tp_has_result_ = true;
    tp_finished_ms_ = millis();
    SERIAL_ECHOPGM("FA2: result=ERROR gen="); SERIAL_ECHO(tp_generation_);
    SERIAL_ECHOLNPGM(" reason=motion_start_failed");
    return false;
  }

  SERIAL_ECHOPGM("FA2: started gen="); SERIAL_ECHO(tp_generation_);
  SERIAL_ECHOPGM(" total_mm="); SERIAL_ECHO(tp_total_mm_);
  SERIAL_ECHOPGM(" feed_mm_min="); SERIAL_ECHO(tp_feed_mm_min_);
  SERIAL_ECHOPGM(" expected_enc="); SERIAL_ECHO(tp_expected_events_);
  SERIAL_ECHOPGM(" pass_pct="); SERIAL_ECHO(tp_pass_efficiency_pct_);
  SERIAL_ECHOPGM(" temp_target="); SERIAL_ECHO(tp_temp_target_);
  SERIAL_ECHOPGM(" temp_tol="); SERIAL_ECHO(tp_temp_tolerance_);
  SERIAL_ECHOLNPGM("");
  return true;
}

void MakeItFilamentAnalyzerPhase0::sample_test_point() {
  const float temp = thermalManager.degHotend(0);
  int16_t heater = thermalManager.getHeaterPower(H_E0);
  if (heater < 0) heater = 0;

  if (tp_temp_samples_ == 0) {
    tp_temp_min_ = temp;
    tp_temp_max_ = temp;
  }
  else {
    if (temp < tp_temp_min_) tp_temp_min_ = temp;
    if (temp > tp_temp_max_) tp_temp_max_ = temp;
  }

  tp_temp_sum_ += temp;
  ++tp_temp_samples_;
  tp_heater_sum_ += uint16_t(heater);
  ++tp_heater_samples_;

  if (ABS(temp - tp_temp_target_) > tp_temp_tolerance_)
    tp_temp_valid_ = false;
}

void MakeItFilamentAnalyzerPhase0::service_test_point() {
  if (!tp_active_) return;

  const uint32_t now = millis();
  if ((int32_t)(now - tp_next_sample_ms_) >= 0) {
    tp_next_sample_ms_ = now + 100UL;
    sample_test_point();
  }

  if (!seg_active_)
    finish_test_point();
}

const char* MakeItFilamentAnalyzerPhase0::test_point_result_name(const TestPointResult result) {
  switch (result) {
    case TP_RESULT_PASS:         return "PASS";
    case TP_RESULT_LOW_FEED:     return "LOW_FEED";
    case TP_RESULT_INVALID_TEMP: return "INVALID_TEMP";
    case TP_RESULT_ERROR:        return "ERROR";
    default:                     return "NONE";
  }
}

void MakeItFilamentAnalyzerPhase0::finish_test_point() {
  sample_test_point();

  tp_actual_events_ = encoder_events();
  tp_efficiency_pct_ = tp_expected_events_ > 0.0f
    ? (float(tp_actual_events_) * 100.0f / tp_expected_events_)
    : 0.0f;

  if (!tp_temp_valid_ || tp_temp_samples_ == 0)
    tp_result_ = TP_RESULT_INVALID_TEMP;
  else if (tp_efficiency_pct_ >= tp_pass_efficiency_pct_)
    tp_result_ = TP_RESULT_PASS;
  else
    tp_result_ = TP_RESULT_LOW_FEED;

  tp_finished_ms_ = millis();
  tp_active_ = false;
  tp_has_result_ = true;
  report_test_point();
}

void MakeItFilamentAnalyzerPhase0::report_test_point() {
  if (tp_active_) {
    SERIAL_ECHOPGM("FA2: state=RUNNING gen="); SERIAL_ECHO(tp_generation_);
    SERIAL_ECHOPGM(" cmd_mm="); SERIAL_ECHO(seg_commanded_mm_);
    SERIAL_ECHOPGM(" total_mm="); SERIAL_ECHO(tp_total_mm_);
    SERIAL_ECHOPGM(" enc="); SERIAL_ECHO(encoder_events());
    SERIAL_ECHOPGM(" expected_enc="); SERIAL_ECHO(tp_expected_events_);
    SERIAL_ECHOPGM(" temp="); SERIAL_ECHO(thermalManager.degHotend(0));
    SERIAL_ECHOPGM(" target="); SERIAL_ECHO(tp_temp_target_);
    SERIAL_ECHOPGM(" heater="); SERIAL_ECHO(thermalManager.getHeaterPower(H_E0));
    SERIAL_ECHOLNPGM("");
    return;
  }

  if (!tp_has_result_) {
    SERIAL_ECHOLNPGM("FA2: state=IDLE result=NONE");
    return;
  }

  const float temp_avg = tp_temp_samples_ ? tp_temp_sum_ / float(tp_temp_samples_) : 0.0f;
  const float heater_avg = tp_heater_samples_ ? float(tp_heater_sum_) / float(tp_heater_samples_) : 0.0f;

  SERIAL_ECHOPGM("FA2: result="); SERIAL_ECHOPGM(test_point_result_name(tp_result_));
  SERIAL_ECHOPGM(" gen="); SERIAL_ECHO(tp_generation_);
  SERIAL_ECHOPGM(" duration_ms="); SERIAL_ECHO(tp_finished_ms_ - tp_started_ms_);
  SERIAL_ECHOPGM(" total_mm="); SERIAL_ECHO(tp_total_mm_);
  SERIAL_ECHOPGM(" feed_mm_min="); SERIAL_ECHO(tp_feed_mm_min_);
  SERIAL_ECHOPGM(" expected_enc="); SERIAL_ECHO(tp_expected_events_);
  SERIAL_ECHOPGM(" actual_enc="); SERIAL_ECHO(tp_actual_events_);
  SERIAL_ECHOPGM(" efficiency_pct="); SERIAL_ECHO(tp_efficiency_pct_);
  SERIAL_ECHOPGM(" pass_pct="); SERIAL_ECHO(tp_pass_efficiency_pct_);
  SERIAL_ECHOPGM(" temp_target="); SERIAL_ECHO(tp_temp_target_);
  SERIAL_ECHOPGM(" temp_avg="); SERIAL_ECHO(temp_avg);
  SERIAL_ECHOPGM(" temp_min="); SERIAL_ECHO(tp_temp_min_);
  SERIAL_ECHOPGM(" temp_max="); SERIAL_ECHO(tp_temp_max_);
  SERIAL_ECHOPGM(" heater_avg_raw="); SERIAL_ECHO(heater_avg);
  SERIAL_ECHOLNPGM("");
}

void MakeItFilamentAnalyzerPhase0::report_to_host() {
  #if MAKEIT_FA_ENCODER_USE_POLLING
    poll_encoder();
  #endif

  const uint32_t enc = encoder_events();
  const uint32_t edge_us = last_edge_us();
  const uint8_t pin_state = encoder_pin_state();

  SERIAL_ECHOPGM("FA0: ");
  SERIAL_ECHOPGM("enc="); SERIAL_ECHO(enc);
  SERIAL_ECHOPGM(" last_edge_us="); SERIAL_ECHO(edge_us);
  SERIAL_ECHOPGM(" pin="); SERIAL_ECHO(pin_state);
  SERIAL_ECHOPGM(" stream="); SERIAL_ECHO(stream_enabled_ ? 1 : 0);
  SERIAL_ECHOPGM(" interval_ms="); SERIAL_ECHO(stream_interval_ms_);
  SERIAL_ECHOPGM(" mode="); SERIAL_ECHOPGM(MAKEIT_FA_ENCODER_TRIGGER_NAME);
  SERIAL_ECHOPGM(" poll="); SERIAL_ECHO(MAKEIT_FA_ENCODER_USE_POLLING ? 1 : 0);
  SERIAL_ECHOPGM(" seg="); SERIAL_ECHO(seg_active_ ? 1 : 0);
  SERIAL_ECHOPGM(" tp="); SERIAL_ECHO(tp_active_ ? 1 : 0);
  SERIAL_ECHOLNPGM("");
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
