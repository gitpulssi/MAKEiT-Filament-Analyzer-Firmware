/**
 * M880 - MAKEiT Filament Analyzer recovery / re-prime controller
 *
 * Start example:
 *   M880 J9000 T230 O5 L20 F100 V30 U100 S0.35 B2 I250
 *        C0.685 P95 D5 A1 W20 R85 K2 G4 H500 X2
 *
 * Query:  M880 Q [J9000]
 * Cancel: M880 Z [J9000]
 */
#include "../../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "../gcode.h"
#include "../../feature/makeit_fa_recovery.h"
#include "../../module/temperature.h"

void GcodeSuite::M880() {
  const bool has_id = parser.seenval('J');
  const uint32_t recovery_id = has_id ? parser.value_ulong() : 0;

  if (parser.seen('Q')) {
    makeit_fa_recovery.query(has_id, recovery_id);
    return;
  }

  if (parser.seen('Z')) {
    makeit_fa_recovery.cancel(has_id, recovery_id);
    return;
  }

  if (!has_id) {
    SERIAL_ECHOLNPGM("FA9: error=MISSING_RECOVERY_ID use_J");
    return;
  }

  MakeItFARecoveryParams recovery;
  recovery.recovery_temp_c = parser.seenval('T') ? parser.value_float() : float(thermalManager.degTargetHotend(0));
  recovery.return_temp_c = float(thermalManager.degTargetHotend(0));
  recovery.settle_seconds = parser.seenval('O') ? (uint16_t)parser.value_int() : 5;
  recovery.prime_mm = parser.seenval('L') ? parser.value_float() : 20.0f;
  recovery.prime_feed_mm_min = parser.seenval('F') ? parser.value_float() : 100.0f;

  MakeItFAPointParams &p = recovery.validation;
  p.total_mm                     = parser.seenval('V') ? parser.value_float() : 30.0f;
  p.feed_mm_min                  = parser.seenval('U') ? parser.value_float() : 100.0f;
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

  makeit_fa_recovery.start(recovery_id, recovery);
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
