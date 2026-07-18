/**
 * MAKEiT Filament Analyzer - Phase 6 host-requested graceful abort
 *
 * M879 J<id> is point-ID-qualified on the normal command path. A bare M879 may
 * also be recognized by EMERGENCY_PARSER and consumed by the transaction idle
 * service before another segment is enqueued. Both forms request the same
 * controlled drain stop. M112 remains the hard emergency stop.
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "makeit_filament_analyzer_phase0.h"
#include "../core/serial.h"
#include "../module/planner.h"

bool MakeItFilamentAnalyzerPhase0::tp_host_abort_requested_ = false;

bool MakeItFilamentAnalyzerPhase0::request_host_abort() {
  if (!tp_active_ || !seg_active_) {
    SERIAL_ECHOLNPGM("FA6: error=NO_ACTIVE_POINT");
    return false;
  }

  if (tp_host_abort_requested_) {
    SERIAL_ECHOPGM("FA6: tag=abort_replay gen="); SERIAL_ECHO(tp_generation_);
    SERIAL_ECHOPGM(" cmd_mm="); SERIAL_ECHO(tp_abort_commanded_mm_);
    SERIAL_ECHOLNPGM("");
    return true;
  }

  if (seg_draining_) {
    SERIAL_ECHOPGM("FA6: error=TOO_LATE gen="); SERIAL_ECHO(tp_generation_);
    SERIAL_ECHOPGM(" cmd_mm="); SERIAL_ECHO(seg_commanded_mm_);
    SERIAL_ECHOLNPGM("");
    return false;
  }

  tp_host_abort_requested_ = true;
  tp_abort_triggered_ = true;
  tp_abort_commanded_mm_ = seg_commanded_mm_;
  request_segmented_stop();

  SERIAL_ECHOPGM("FA6: tag=abort_requested gen="); SERIAL_ECHO(tp_generation_);
  SERIAL_ECHOPGM(" cmd_mm="); SERIAL_ECHO(tp_abort_commanded_mm_);
  SERIAL_ECHOPGM(" completed_est_mm="); SERIAL_ECHO(estimated_completed_mm());
  SERIAL_ECHOPGM(" committed_blocks="); SERIAL_ECHO(uint8_t(planner.movesplanned()));
  SERIAL_ECHOLNPGM("");
  return true;
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
