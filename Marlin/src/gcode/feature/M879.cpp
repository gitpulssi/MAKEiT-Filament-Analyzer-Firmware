/**
 * M879 - MAKEiT Filament Analyzer host-requested graceful abort
 *
 * Required:
 *   J  active host-assigned point ID
 *
 * Example:
 *   M879 J42
 *
 * The current M877 implementation is non-blocking, so M879 can be parsed on
 * the normal command channel while a point is running. M879 stops new segment
 * enqueueing and drains only the bounded in-flight planner horizon. Use M112
 * for a hard emergency stop.
 */
#include "../../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "../gcode.h"
#include "../../feature/makeit_fa_transaction.h"

void GcodeSuite::M879() {
  if (!parser.seenval('J')) {
    SERIAL_ECHOLNPGM("FATX: error=MISSING_POINT_ID use_J");
    return;
  }

  makeit_fa_transaction.request_abort(parser.value_ulong());
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
