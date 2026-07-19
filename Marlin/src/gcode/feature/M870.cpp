/**
 * M870 - MAKEiT Filament Analyzer Phase-8/9 temperature / speed envelope
 *
 * The current hotend target is the starting temperature. The command advances
 * the target in E-degree steps through T and runs the validated M872 speed
 * ladder at every row. M selects the recovery / re-prime temperature; M0 keeps
 * the conservative Phase-8 behavior and stops at the first feed limit.
 *
 * PLA / 0.6 mm nozzle example, 190..230 C in 5 C increments:
 *   M109 S190
 *   M870 J2000 T230 E5 M230 Y1.75 F50 U300 V50 O10
 *        L40 S0.35 B2 I250 C0.685 P95 D5 A1 W20 R85 K2 G4 H500 X2
 *
 * Y is filament diameter, not nozzle diameter. Nozzle diameter affects the
 * physical flow ceiling but does not enter the filament-volume conversion.
 *
 * Query:  M870 Q [J2000]
 * Cancel: M870 Z [J2000]
 */
#include "../../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "../gcode.h"
#include "../../feature/makeit_fa_envelope.h"
#include "../../module/temperature.h"

void GcodeSuite::M870() {
  const bool has_id = parser.seenval('J');
  const uint32_t envelope_id = has_id ? parser.value_ulong() : 0;

  if (parser.seen('Q')) {
    makeit_fa_envelope.query(has_id, envelope_id);
    return;
  }

  if (parser.seen('Z')) {
    makeit_fa_envelope.cancel(has_id, envelope_id);
    return;
  }

  if (!has_id) {
    SERIAL_ECHOLNPGM("FA8: error=MISSING_ENVELOPE_ID use_J");
    return;
  }

  MakeItFATemperatureEnvelopeParams envelope;
  envelope.start_temp_c = float(thermalManager.degTargetHotend(0));
  envelope.max_temp_c = parser.seenval('T') ? parser.value_float() : envelope.start_temp_c;
  envelope.temp_step_c = parser.seenval('E') ? parser.value_float() : 10.0f;
  envelope.filament_diameter_mm = parser.seenval('Y') ? parser.value_float() : 1.75f;
  envelope.recovery_temp_c = parser.seenval('M') ? parser.value_float() : envelope.max_temp_c;

  MakeItFASpeedCampaignParams &campaign = envelope.speed;
  campaign.start_feed_mm_min = parser.seenval('F') ? parser.value_float() : 100.0f;
  campaign.max_feed_mm_min = parser.seenval('U') ? parser.value_float() : 500.0f;
  campaign.step_feed_mm_min = parser.seenval('V') ? parser.value_float() : 100.0f;
  campaign.settle_seconds = parser.seenval('O') ? (uint16_t)parser.value_int() : 5;

  MakeItFAPointParams &p = campaign.point;
  p.total_mm                     = parser.seenval('L') ? parser.value_float() : 50.0f;
  p.feed_mm_min                  = campaign.start_feed_mm_min;
  p.segment_mm                   = parser.seenval('S') ? parser.value_float() : 0.35f;
  p.max_inflight                 = parser.seenval('B') ? (uint8_t)parser.value_int() : 2;
  p.report_ms                    = parser.seenval('I') ? (uint16_t)parser.value_int() : 250;
  p.encoder_events_per_mm        = parser.seenval('C') ? parser.value_float() : 0.685f;
  p.pass_efficiency_pct          = parser.seenval('P') ? parser.value_float() : 95.0f;
  p.temp_tolerance               = parser.seenval('D') ? parser.value_float() : 5.0f;
  p.auto_stop_enabled            = parser.seenval('A') ? parser.value_bool() : true;
  p.monitor_window_mm            = parser.seenval('W') ? parser.value_float() : 20.0f;
  p.monitor_efficiency_pct       = parser.seenval('R') ? parser.value_float() : 85.0f;
  p.monitor_confirm_windows      = parser.seenval('K') ? (uint8_t)parser.value_int() : 2;
  p.pulse_gap_factor             = parser.seenval('G') ? parser.value_float() : 4.0f;
  p.pulse_gap_min_ms             = parser.seenval('H') ? (uint16_t)parser.value_int() : 500;
  p.pulse_gap_min_missing_events = parser.seenval('X') ? parser.value_float() : 2.0f;

  makeit_fa_envelope.start(envelope_id, envelope);
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
