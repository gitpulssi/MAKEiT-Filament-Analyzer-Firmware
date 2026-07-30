/**
 * M881 - MAKEiT Filament Analyzer point-conditioning configuration
 *
 * The speed campaign feeds this much filament at the upcoming point speed after
 * thermal settling and before measurement. This flushes filament that sat in
 * the melt zone during heating or dwell. The evaluated point then resets the
 * encoder and begins without another thermal wait.
 *
 *   M881        report the current RAM-only setting
 *   M881 Q      report the current RAM-only setting
 *   M881 P20    use a 20 mm conditioning feed before every point
 *   M881 P0     disable conditioning (legacy wait-then-measure behavior)
 */
#include "../../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "../gcode.h"
#include "../../feature/makeit_fa_campaign.h"

void GcodeSuite::M881() {
  if (parser.seenval('P'))
    MakeItFASpeedCampaign::set_default_conditioning_mm(parser.value_float());
  else
    MakeItFASpeedCampaign::report_conditioning_config();
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
