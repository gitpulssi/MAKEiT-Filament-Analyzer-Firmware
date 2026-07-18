/**
 * MAKEiT Filament Analyzer - Phase 0 / Phase 1 bring-up
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "makeit_filament_analyzer_phase0.h"
#include "../core/serial.h"
#include "../module/motion.h"
#include "../module/planner.h"
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
    MAKEIT_FA_TELEM_SERIAL.println();
  #endif
}

void MakeItFilamentAnalyzerPhase0::run_segmented_feed_test(float total_mm, float feed_mm_min, float segment_mm, uint8_t max_inflight, uint16_t report_ms) {
  if (!initialized_) init();

  if (total_mm <= 0.0f || feed_mm_min <= 0.0f || segment_mm <= 0.0f) {
    SERIAL_ECHOLNPGM("FA1: invalid parameters");
    return;
  }

  total_mm = constrain(total_mm, 0.01f, 500.0f);
  feed_mm_min = constrain(feed_mm_min, 1.0f, 2000.0f);
  segment_mm = constrain(segment_mm, 0.05f, 0.35f);       // Preserve the 0.70mm cap with two blocks.
  max_inflight = constrain(max_inflight, uint8_t(1), uint8_t(2));
  report_ms = constrain(report_ms, uint16_t(50), uint16_t(5000));

  const feedRate_t old_feedrate = feedrate_mm_s;
  const feedRate_t test_feedrate = feed_mm_min / 60.0f;

  float commanded_mm = 0.0f;
  uint32_t local_seq = 0;
  uint32_t enqueued_segments = 0;
  millis_t next_report_ms = millis();

  sync_plan_position_e();

  segmented_feed_telemetry("start", ++local_seq, commanded_mm, total_mm, planner.movesplanned(), max_inflight);

  while (commanded_mm < total_mm) {
    idle();

    #if MAKEIT_FA_ENCODER_USE_POLLING
      poll_encoder();
    #endif

    const uint8_t planned_blocks = planner.movesplanned();

    if (planned_blocks < max_inflight && !planner.is_full()) {
      const float remaining = total_mm - commanded_mm;
      const float this_segment = remaining < segment_mm ? remaining : segment_mm;

      destination = current_position;
      destination.e += this_segment;
      feedrate_mm_s = test_feedrate;
      prepare_line_to_destination();

      commanded_mm += this_segment;
      ++enqueued_segments;
    }

    const millis_t now = millis();
    if ((int32_t)(now - next_report_ms) >= 0) {
      next_report_ms = now + report_ms;
      segmented_feed_telemetry("run", ++local_seq, commanded_mm, total_mm, planner.movesplanned(), max_inflight);
    }
  }

  while (planner.movesplanned()) {
    idle();

    #if MAKEIT_FA_ENCODER_USE_POLLING
      poll_encoder();
    #endif

    const millis_t now = millis();
    if ((int32_t)(now - next_report_ms) >= 0) {
      next_report_ms = now + report_ms;
      segmented_feed_telemetry("drain", ++local_seq, commanded_mm, total_mm, planner.movesplanned(), max_inflight);
    }
  }

  feedrate_mm_s = old_feedrate;

  segmented_feed_telemetry("done", ++local_seq, commanded_mm, total_mm, planner.movesplanned(), max_inflight);

  SERIAL_ECHOPGM("FA1: done total_mm="); SERIAL_ECHO(total_mm);
  SERIAL_ECHOPGM(" segment_mm="); SERIAL_ECHO(segment_mm);
  SERIAL_ECHOPGM(" feed_mm_min="); SERIAL_ECHO(feed_mm_min);
  SERIAL_ECHOPGM(" max_blocks="); SERIAL_ECHO(max_inflight);
  SERIAL_ECHOPGM(" enqueued="); SERIAL_ECHO(enqueued_segments);
  SERIAL_ECHOPGM(" enc="); SERIAL_ECHO(encoder_events());
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
  SERIAL_ECHOLNPGM("");
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
