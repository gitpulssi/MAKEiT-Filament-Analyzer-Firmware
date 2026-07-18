/**
 * M879 - MAKEiT Filament Analyzer host-requested graceful abort
 *
 * Usage:
 *   M879       abort whichever analyzer point is currently running
 *   M879 J42   abort only retained point ID 42 on the normal command path
 *
 * The emergency parser recognizes bare M879 directly in the receive stream.
 * Argument-bearing M879 lines remain on the normal parser so J validation
 * cannot be bypassed. This command performs a controlled bounded drain; use
 * M112 for a hard emergency stop.
 */
#include "../../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "../gcode.h"
#include "../../feature/makeit_fa_transaction.h"

#if ENABLED(EMERGENCY_PARSER)
  #include "../../feature/e_parser.h"
#endif

void GcodeSuite::M879() {
  // The point-qualified form is intentionally handled only here. The emergency
  // parser rejects argument-bearing M879 lines and therefore cannot bypass J.
  if (parser.seenval('J')) {
    makeit_fa_transaction.request_abort(parser.value_ulong());
    return;
  }

  #if ENABLED(EMERGENCY_PARSER)
    // A bare serial M879 has already been observed by the low-level parser.
    // Depending on main-loop timing, either this handler or transaction idle()
    // may get to the flag first. Exactly one of them must issue the request.
    if (EmergencyParser::abort_by_M879) {
      EmergencyParser::abort_by_M879 = false;
      makeit_fa_transaction.request_abort_current("gcode");
      return;
    }

    // If transaction idle already consumed this same line, suppress the normal
    // queued copy. This removes duplicate abort_replay / NO_RUNNING_POINT lines.
    if (makeit_fa_transaction.abort_requested())
      return;

    // Fallback for a non-serial command source where the emergency parser did
    // not see the line. Only act while a transaction is actually running.
    if (makeit_fa_transaction.running())
      makeit_fa_transaction.request_abort_current("gcode");
  #else
    makeit_fa_transaction.request_abort_current("gcode");
  #endif
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
