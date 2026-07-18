/**
 * M876 - MAKEiT Filament Analyzer Phase-1 segmented feed diagnostic
 *
 * Usage:
 *   M876 L20 F100 S0.35 B2 I250
 *
 * Parameters:
 *   L  total forward filament length in mm. Default 10.0.
 *   F  filament feed rate in mm/min. Default 100.0.
 *   S  segment length in mm. Default 0.35. Clamped to 0.05..0.35.
 *   B  max in-flight planner blocks. Default 2. Clamped to 1..2.
 *   I  telemetry/report interval in ms. Default 250.
 *
 * This diagnostic is open-loop. It does not stop based on encoder efficiency.
 */
#include "../../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "../gcode.h"
#include "../../feature/makeit_filament_analyzer_phase0.h"

void GcodeSuite::M876() {
  const float total_mm    = parser.seenval('L') ? parser.value_float() : 10.0f;
  const float feed_mm_min = parser.seenval('F') ? parser.value_float() : 100.0f;
  const float segment_mm  = parser.seenval('S') ? parser.value_float() : 0.35f;
  const uint8_t blocks    = parser.seenval('B') ? (uint8_t)parser.value_int() : 2;
  const uint16_t report   = parser.seenval('I') ? (uint16_t)parser.value_int() : 250;

  makeit_fa_phase0.run_segmented_feed_test(total_mm, feed_mm_min, segment_mm, blocks, report);
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
