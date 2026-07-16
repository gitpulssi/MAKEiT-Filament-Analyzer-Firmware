/**
 * MAKEiT Filament Analyzer - Phase 0 / Phase 1 bring-up
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "makeit_filament_analyzer_phase0.h"
#include "../core/serial.h"

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
