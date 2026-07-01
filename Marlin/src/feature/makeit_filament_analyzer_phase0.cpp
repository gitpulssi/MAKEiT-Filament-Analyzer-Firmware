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
uint32_t MakeItFilamentAnalyzerPhase0::stream_interval_ms_ = MAKEIT_FA_TELEM_INTERVAL_MS;
uint32_t MakeItFilamentAnalyzerPhase0::next_stream_ms_ = 0;
uint32_t MakeItFilamentAnalyzerPhase0::seq_ = 0;

MakeItFilamentAnalyzerPhase0 makeit_fa_phase0;

void MakeItFilamentAnalyzerPhase0::encoder_isr() {
  // ISR rule: count/timestamp only. No serial, no TMC UART, no allocation.
  ++encoder_events_;
  last_edge_us_ = micros();

  #if PIN_EXISTS(MAKEIT_FA_MARKER_ENCODER)
    WRITE(MAKEIT_FA_MARKER_ENCODER_PIN, HIGH);
    WRITE(MAKEIT_FA_MARKER_ENCODER_PIN, LOW);
  #endif
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

void MakeItFilamentAnalyzerPhase0::reset_encoder() {
  CRITICAL_SECTION_START();
  encoder_events_ = 0;
  last_edge_us_ = micros();
  CRITICAL_SECTION_END();
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
  attachInterrupt(digitalPinToInterrupt(MAKEIT_FA_ENCODER_PIN), encoder_isr, MAKEIT_FA_ENCODER_INTERRUPT_MODE);

  #if MAKEIT_FA_TELEM_AVAILABLE
    MAKEIT_FA_TELEM_SERIAL.begin(MAKEIT_FA_TELEM_BAUD);
  #endif

  initialized_ = true;
  report_to_host();
}

void MakeItFilamentAnalyzerPhase0::idle() {
  if (!initialized_ || !stream_enabled_) return;

  const uint32_t now = millis();
  if ((int32_t)(now - next_stream_ms_) >= 0) {
    next_stream_ms_ = now + stream_interval_ms_;
    telemetry_line();
  }
}

void MakeItFilamentAnalyzerPhase0::telemetry_line() {
  const uint32_t enc = encoder_events();
  const uint32_t edge_us = last_edge_us();
  telemetry_print_line(++seq_, millis(), enc, edge_us);
}

void MakeItFilamentAnalyzerPhase0::telemetry_print_line(const uint32_t seq, const uint32_t ms, const uint32_t enc, const uint32_t edge_us) {
  #if MAKEIT_FA_TELEM_AVAILABLE
    MAKEIT_FA_TELEM_SERIAL.print(F("FA0,"));
    MAKEIT_FA_TELEM_SERIAL.print(F("seq=")); MAKEIT_FA_TELEM_SERIAL.print(seq);
    MAKEIT_FA_TELEM_SERIAL.print(F(",ms=")); MAKEIT_FA_TELEM_SERIAL.print(ms);
    MAKEIT_FA_TELEM_SERIAL.print(F(",enc=")); MAKEIT_FA_TELEM_SERIAL.print(enc);
    MAKEIT_FA_TELEM_SERIAL.print(F(",last_edge_us=")); MAKEIT_FA_TELEM_SERIAL.print(edge_us);
    MAKEIT_FA_TELEM_SERIAL.print(F(",mode=")); MAKEIT_FA_TELEM_SERIAL.print(F(MAKEIT_FA_ENCODER_TRIGGER_NAME));
    MAKEIT_FA_TELEM_SERIAL.println();
  #endif
}

void MakeItFilamentAnalyzerPhase0::report_to_host() {
  const uint32_t enc = encoder_events();
  const uint32_t edge_us = last_edge_us();

  SERIAL_ECHOPGM("FA0: ");
  SERIAL_ECHOPGM("enc="); SERIAL_ECHO(enc);
  SERIAL_ECHOPGM(" last_edge_us="); SERIAL_ECHO(edge_us);
  SERIAL_ECHOPGM(" stream="); SERIAL_ECHO(stream_enabled_ ? 1 : 0);
  SERIAL_ECHOPGM(" interval_ms="); SERIAL_ECHO(stream_interval_ms_);
  SERIAL_ECHOPGM(" mode="); SERIAL_ECHOPGM(MAKEIT_FA_ENCODER_TRIGGER_NAME);
  SERIAL_ECHOLNPGM("");
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
