/**
 * M878 - MAKEiT Filament Analyzer repeatable result query
 *
 * Usage:
 *   M878       query the retained active/latest transaction
 *   M878 J42   query only if point ID 42 is retained
 *
 * This command is side-effect-free and never starts extrusion.
 */
#include "../../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "../gcode.h"
#include "../../feature/makeit_fa_transaction.h"

void GcodeSuite::M878() {
  const bool has_point_id = parser.seenval('J');
  const uint32_t point_id = has_point_id ? parser.value_ulong() : 0;
  makeit_fa_transaction.query(has_point_id, point_id);
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
