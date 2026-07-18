/**
 * M873 - MAKEiT Filament Analyzer evaluated extrusion point
 *
 * The hotend must already be at a stable extrusion temperature. Use M109 first.
 *
 * Basic evaluated point:
 *   M873 L100 F500 S0.35 B2 I250 C0.685 P95 D2
 *
 * Phase-3 rolling monitor with graceful auto-stop:
 *   M873 L150 F500 S0.35 B2 I250 C0.685 P95 D2 A1 W20 R85 K2
 *
 * Query current / latest result:
 *   M873 Q
 *
 * Parameters:
 *   L  commanded filament length in mm. Default 100. Range 20..500.
 *   F  filament feed rate in mm/min. Default 100.
 *   S  segment length in mm. Default 0.35. Clamped to 0.05..0.35.
 *   B  maximum in-flight planner blocks. Default 2. Clamped to 1..2.
 *   I  FA1 telemetry interval in ms. Default 250.
 *   C  calibrated encoder events per physical filament mm. Default 0.685.
 *   P  minimum terminal passing feed efficiency percent. Default 95.
 *   D  maximum temperature deviation from current target in C. Default 2.
 *   A  enable rolling feed-loss auto-stop. Default 0.
 *   W  rolling monitor window length in commanded mm. Default 20.
 *   R  minimum rolling window efficiency percent. Default 85.
 *   K  consecutive failing windows required before graceful stop. Default 2.
 *   Q  report current state or latest terminal result without starting a point.
 */
#include "../../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "../gcode.h"
#include "../../feature/makeit_filament_analyzer_phase0.h"

void GcodeSuite::M873() {
  if (parser.seen('Q')) {
    makeit_fa_phase0.report_test_point();
    return;
  }

  const float total_mm       = parser.seenval('L') ? parser.value_float() : 100.0f;
  const float feed_mm_min    = parser.seenval('F') ? parser.value_float() : 100.0f;
  const float segment_mm     = parser.seenval('S') ? parser.value_float() : 0.35f;
  const uint8_t blocks       = parser.seenval('B') ? (uint8_t)parser.value_int() : 2;
  const uint16_t report_ms   = parser.seenval('I') ? (uint16_t)parser.value_int() : 250;
  const float events_per_mm  = parser.seenval('C') ? parser.value_float() : 0.685f;
  const float pass_pct       = parser.seenval('P') ? parser.value_float() : 95.0f;
  const float temp_tolerance = parser.seenval('D') ? parser.value_float() : 2.0f;
  const bool auto_stop       = parser.seenval('A') ? parser.value_bool() : false;
  const float window_mm      = parser.seenval('W') ? parser.value_float() : 20.0f;
  const float monitor_pct    = parser.seenval('R') ? parser.value_float() : 85.0f;
  const uint8_t confirm      = parser.seenval('K') ? (uint8_t)parser.value_int() : 2;

  makeit_fa_phase0.start_evaluated_test_point(
    total_mm,
    feed_mm_min,
    segment_mm,
    blocks,
    report_ms,
    events_per_mm,
    pass_pct,
    temp_tolerance,
    auto_stop,
    window_mm,
    monitor_pct,
    confirm
  );
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
