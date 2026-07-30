/**
 * MAKEiT Filament Analyzer - Phase 9 segmented-prime stop access
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "makeit_filament_analyzer_phase0.h"

bool MakeItFilamentAnalyzerPhase0::request_segmented_feed_stop() {
  if (!seg_active_ || seg_draining_) return false;
  request_segmented_stop();
  return true;
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
