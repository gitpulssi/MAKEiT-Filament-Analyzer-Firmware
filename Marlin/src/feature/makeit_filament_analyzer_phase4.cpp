/**
 * MAKEiT Filament Analyzer - Phase 4 pulse-gap monitor
 *
 * This file adds a fast, distance-and-time-qualified encoder pulse-gap path
 * without changing the validated Phase-3 rolling-window monitor. When a gap is
 * confirmed, it requests the same graceful segmented stop: no new E segments
 * are added and the bounded in-flight planner horizon is allowed to drain.
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "makeit_filament_analyzer_phase0.h"
#include "../core/serial.h"

namespace {

  bool pg_enabled = false;
  bool pg_triggered = false;
  bool pg_announced = false;
  bool pg_terminal_reported = false;

  float pg_gap_factor = 4.0f;
  float pg_min_missing_events = 2.0f;
  uint32_t pg_min_gap_us = 500000UL;
  uint32_t pg_threshold_us = 0;

  uint32_t pg_last_event_count = 0;
  float pg_last_event_cmd_mm = 0.0f;

  uint32_t pg_last_gap_us = 0;
  float pg_last_expected_missing = 0.0f;

  uint32_t pg_trigger_gap_us = 0;
  float pg_trigger_cmd_mm = 0.0f;
  float pg_trigger_expected_missing = 0.0f;

}

void MakeItFilamentAnalyzerPhase0::configure_pulse_gap_monitor(
  bool enabled,
  float gap_factor,
  uint16_t min_gap_ms,
  float min_missing_events
) {
  pg_enabled = enabled;
  pg_gap_factor = constrain(gap_factor, 1.5f, 20.0f);
  pg_min_gap_us = uint32_t(constrain(min_gap_ms, uint16_t(50), uint16_t(30000))) * 1000UL;
  pg_min_missing_events = constrain(min_missing_events, 0.5f, 20.0f);

  pg_triggered = false;
  pg_announced = false;
  pg_terminal_reported = false;
  pg_threshold_us = 0;
  pg_last_event_count = 0;
  pg_last_event_cmd_mm = 0.0f;
  pg_last_gap_us = 0;
  pg_last_expected_missing = 0.0f;
  pg_trigger_gap_us = 0;
  pg_trigger_cmd_mm = 0.0f;
  pg_trigger_expected_missing = 0.0f;
}

void MakeItFilamentAnalyzerPhase0::service_pulse_gap_monitor() {
  if (!pg_enabled) return;

  if (!tp_active_) {
    if (pg_triggered && !pg_terminal_reported) {
      SERIAL_ECHOPGM("FA4: result=TRIGGERED gen="); SERIAL_ECHO(tp_generation_);
      SERIAL_ECHOPGM(" trigger_cmd_mm="); SERIAL_ECHO(pg_trigger_cmd_mm);
      SERIAL_ECHOPGM(" gap_ms="); SERIAL_ECHO(pg_trigger_gap_us / 1000UL);
      SERIAL_ECHOPGM(" threshold_ms="); SERIAL_ECHO(pg_threshold_us / 1000UL);
      SERIAL_ECHOPGM(" expected_missing="); SERIAL_ECHO(pg_trigger_expected_missing);
      SERIAL_ECHOLNPGM("");
      pg_terminal_reported = true;
    }
    return;
  }

  if (pg_threshold_us == 0) {
    const float event_rate_hz = (tp_feed_mm_min_ / 60.0f) * tp_encoder_events_per_mm_;
    if (event_rate_hz <= 0.0f) return;

    const float expected_interval_us = 1000000.0f / event_rate_hz;
    const float scaled_threshold_us = expected_interval_us * pg_gap_factor;
    pg_threshold_us = uint32_t(scaled_threshold_us + 0.5f);
    if (pg_threshold_us < pg_min_gap_us)
      pg_threshold_us = pg_min_gap_us;
  }

  if (!pg_announced) {
    SERIAL_ECHOPGM("FA4: armed gen="); SERIAL_ECHO(tp_generation_);
    SERIAL_ECHOPGM(" factor="); SERIAL_ECHO(pg_gap_factor);
    SERIAL_ECHOPGM(" threshold_ms="); SERIAL_ECHO(pg_threshold_us / 1000UL);
    SERIAL_ECHOPGM(" min_gap_ms="); SERIAL_ECHO(pg_min_gap_us / 1000UL);
    SERIAL_ECHOPGM(" min_missing_events="); SERIAL_ECHO(pg_min_missing_events);
    SERIAL_ECHOLNPGM("");
    pg_announced = true;
  }

  // Do not evaluate once the segmented engine is already draining, or after
  // another monitor path has requested the graceful stop.
  if (!seg_active_ || seg_draining_ || tp_abort_triggered_) return;

  const uint32_t events_now = encoder_events();
  if (events_now != pg_last_event_count) {
    pg_last_event_count = events_now;
    pg_last_event_cmd_mm = seg_commanded_mm_;
    pg_last_gap_us = 0;
    pg_last_expected_missing = 0.0f;
    return;
  }

  float commanded_without_edge = seg_commanded_mm_ - pg_last_event_cmd_mm;
  if (commanded_without_edge < 0.0f) commanded_without_edge = 0.0f;

  pg_last_expected_missing = commanded_without_edge * tp_encoder_events_per_mm_;
  pg_last_gap_us = uint32_t(micros() - last_edge_us());

  // Qualify the stop in both domains. The timer alone can false-trigger at
  // low flow; commanded distance alone can false-trigger during startup.
  if (pg_last_expected_missing < pg_min_missing_events || pg_last_gap_us < pg_threshold_us)
    return;

  pg_triggered = true;
  pg_trigger_gap_us = pg_last_gap_us;
  pg_trigger_cmd_mm = seg_commanded_mm_;
  pg_trigger_expected_missing = pg_last_expected_missing;

  tp_abort_triggered_ = true;
  tp_abort_commanded_mm_ = seg_commanded_mm_;
  request_segmented_stop();

  SERIAL_ECHOPGM("FA4: stop_requested gen="); SERIAL_ECHO(tp_generation_);
  SERIAL_ECHOPGM(" reason=pulse_gap cmd_mm="); SERIAL_ECHO(pg_trigger_cmd_mm);
  SERIAL_ECHOPGM(" gap_ms="); SERIAL_ECHO(pg_trigger_gap_us / 1000UL);
  SERIAL_ECHOPGM(" threshold_ms="); SERIAL_ECHO(pg_threshold_us / 1000UL);
  SERIAL_ECHOPGM(" expected_missing="); SERIAL_ECHO(pg_trigger_expected_missing);
  SERIAL_ECHOPGM(" enc="); SERIAL_ECHO(events_now);
  SERIAL_ECHOLNPGM("");
}

void MakeItFilamentAnalyzerPhase0::report_pulse_gap_monitor() {
  SERIAL_ECHOPGM("FA4: enabled="); SERIAL_ECHO(pg_enabled ? 1 : 0);
  SERIAL_ECHOPGM(" active="); SERIAL_ECHO((pg_enabled && tp_active_) ? 1 : 0);
  SERIAL_ECHOPGM(" triggered="); SERIAL_ECHO(pg_triggered ? 1 : 0);
  SERIAL_ECHOPGM(" factor="); SERIAL_ECHO(pg_gap_factor);
  SERIAL_ECHOPGM(" threshold_ms="); SERIAL_ECHO(pg_threshold_us / 1000UL);
  SERIAL_ECHOPGM(" min_gap_ms="); SERIAL_ECHO(pg_min_gap_us / 1000UL);
  SERIAL_ECHOPGM(" min_missing_events="); SERIAL_ECHO(pg_min_missing_events);
  SERIAL_ECHOPGM(" last_gap_ms="); SERIAL_ECHO(pg_last_gap_us / 1000UL);
  SERIAL_ECHOPGM(" last_expected_missing="); SERIAL_ECHO(pg_last_expected_missing);
  SERIAL_ECHOPGM(" trigger_cmd_mm="); SERIAL_ECHO(pg_trigger_cmd_mm);
  SERIAL_ECHOLNPGM("");
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
