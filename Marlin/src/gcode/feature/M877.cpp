/**
 * M877 - MAKEiT Filament Analyzer idempotent point execution
 *
 * Required:
 *   J  positive host-assigned point ID
 *
 * Point parameters mirror M873:
 *   L F S B I C P D A W R K G H X
 *
 * Example:
 *   M877 J42 L100 F500 S0.35 B2 I250 C0.685 P95 D3 A1 W20 R85 K2 G4 H500 X2
 *
 * Repeating the identical J/parameter set never extrudes twice. Reusing J with
 * different parameters returns PARAM_CONFLICT.
 */
#include "../../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "../gcode.h"
#include "../../feature/makeit_fa_transaction.h"

void GcodeSuite::M877() {
  if (!parser.seenval('J')) {
    SERIAL_ECHOLNPGM("FATX: error=MISSING_POINT_ID use_J");
    return;
  }

  const uint32_t point_id = parser.value_ulong();

  MakeItFAPointParams p;
  p.total_mm                    = parser.seenval('L') ? parser.value_float() : 100.0f;
  p.feed_mm_min                 = parser.seenval('F') ? parser.value_float() : 100.0f;
  p.segment_mm                  = parser.seenval('S') ? parser.value_float() : 0.35f;
  p.max_inflight                = parser.seenval('B') ? (uint8_t)parser.value_int() : 2;
  p.report_ms                   = parser.seenval('I') ? (uint16_t)parser.value_int() : 250;
  p.encoder_events_per_mm       = parser.seenval('C') ? parser.value_float() : 0.685f;
  p.pass_efficiency_pct         = parser.seenval('P') ? parser.value_float() : 95.0f;
  p.temp_tolerance              = parser.seenval('D') ? parser.value_float() : 3.0f;
  p.auto_stop_enabled           = parser.seenval('A') ? parser.value_bool() : true;
  p.monitor_window_mm           = parser.seenval('W') ? parser.value_float() : 20.0f;
  p.monitor_efficiency_pct      = parser.seenval('R') ? parser.value_float() : 85.0f;
  p.monitor_confirm_windows     = parser.seenval('K') ? (uint8_t)parser.value_int() : 2;
  p.pulse_gap_factor            = parser.seenval('G') ? parser.value_float() : 4.0f;
  p.pulse_gap_min_ms            = parser.seenval('H') ? (uint16_t)parser.value_int() : 500;
  p.pulse_gap_min_missing_events = parser.seenval('X') ? parser.value_float() : 2.0f;

  makeit_fa_transaction.execute(point_id, p);
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
