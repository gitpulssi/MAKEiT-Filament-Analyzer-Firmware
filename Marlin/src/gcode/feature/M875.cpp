/**
 * M875 - MAKEiT Filament Analyzer Phase-0 diagnostics
 *
 * Usage:
 *   M875          ; report current encoder count
 *   M875 R        ; reset encoder counter
 *   M875 S1       ; enable raw FA0 telemetry stream
 *   M875 S0       ; disable raw FA0 telemetry stream
 *   M875 I200     ; set stream interval in ms, 20..5000
 */
#include "../../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "../gcode.h"
#include "../../feature/makeit_filament_analyzer_phase0.h"

void GcodeSuite::M875() {
  if (parser.seen('R'))
    makeit_fa_phase0.reset_encoder();

  if (parser.seenval('I'))
    makeit_fa_phase0.set_stream_interval_ms(parser.value_ulong());

  if (parser.seenval('S'))
    makeit_fa_phase0.set_stream_enabled(parser.value_bool());

  makeit_fa_phase0.report_to_host();
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
