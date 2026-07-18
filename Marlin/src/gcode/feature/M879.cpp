/**
 * M879 - MAKEiT Filament Analyzer host-requested graceful abort
 *
 * Usage:
 *   M879       abort whichever analyzer point is currently running
 *   M879 J42   abort only retained point ID 42 on the normal command path
 *
 * The emergency parser recognizes bare M879 directly in the receive stream.
 * This command stops new segment enqueueing and drains only the bounded
 * in-flight planner horizon. Use M112 for a hard emergency stop.
 */
#include "../../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "../gcode.h"
#include "../../feature/makeit_fa_transaction.h"

#if ENABLED(EMERGENCY_PARSER)
  #include "../../feature/e_parser.h"
#endif

void GcodeSuite::M879() {
  #if ENABLED(EMERGENCY_PARSER)
    // The normal command handler is now acting on this same line, so consume
    // any still-pending low-level flag to avoid a duplicate request in idle().
    EmergencyParser::abort_by_M879 = false;
  #endif

  if (parser.seenval('J'))
    makeit_fa_transaction.request_abort(parser.value_ulong());
  else
    makeit_fa_transaction.request_abort_current("gcode");
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
