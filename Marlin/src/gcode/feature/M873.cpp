/**
 * M873 - MAKEiT Filament Analyzer Phase-2 evaluated extrusion point
 *
 * The hotend must already be at a stable extrusion temperature. Use M109 first.
 *
 * Start one point:
 *   M873 L100 F500 S0.35 B2 I250 C0.685 P95 D2
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
 *   P  minimum passing feed efficiency percent. Default 95.
 *   D  maximum temperature deviation from current target in C. Default 2.
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

  makeit_fa_phase0.start_evaluated_test_point(
    total_mm,
    feed_mm_min,
    segment_mm,
    blocks,
    report_ms,
    events_per_mm,
    pass_pct,
    temp_tolerance
  );
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
