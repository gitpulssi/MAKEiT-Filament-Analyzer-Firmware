/**
 * M872 - MAKEiT Filament Analyzer Phase-7 fixed-temperature speed campaign
 *
 * The hotend target must already be set. M872 waits for thermal stability,
 * runs a speed ladder, and stops at the first non-PASS point.
 *
 * Start:
 *   M872 J100 F100 U500 V100 O5 L50 S0.35 B2 I250
 *        C0.685 P95 D5 A1 W20 R85 K2 G4 H500 X2
 *
 * Query:
 *   M872 Q
 *   M872 Q J100
 *
 * Cancel:
 *   M872 Z
 *   M872 Z J100
 *
 * J is both the campaign ID and the first point ID. The ladder uses J, J+1...
 */
#include "../../inc/MarlinConfig.h"

#if ENABLED(MAKEIT_FILAMENT_ANALYZER_PHASE0)

#include "../gcode.h"
#include "../../feature/makeit_fa_campaign.h"

void GcodeSuite::M872() {
  const bool has_id = parser.seenval('J');
  const uint32_t campaign_id = has_id ? parser.value_ulong() : 0;

  if (parser.seen('Q')) {
    makeit_fa_campaign.query(has_id, campaign_id);
    return;
  }

  if (parser.seen('Z')) {
    makeit_fa_campaign.cancel(has_id, campaign_id);
    return;
  }

  if (!has_id) {
    SERIAL_ECHOLNPGM("FA7: error=MISSING_CAMPAIGN_ID use_J");
    return;
  }

  MakeItFASpeedCampaignParams campaign;
  campaign.start_feed_mm_min = parser.seenval('F') ? parser.value_float() : 100.0f;
  campaign.max_feed_mm_min = parser.seenval('U') ? parser.value_float() : 500.0f;
  campaign.step_feed_mm_min = parser.seenval('V') ? parser.value_float() : 100.0f;
  campaign.settle_seconds = parser.seenval('O') ? (uint16_t)parser.value_int() : 5;

  MakeItFAPointParams &p = campaign.point;
  p.total_mm                     = parser.seenval('L') ? parser.value_float() : 50.0f;
  p.feed_mm_min                  = campaign.start_feed_mm_min;
  p.segment_mm                   = parser.seenval('S') ? parser.value_float() : 0.35f;
  p.max_inflight                 = parser.seenval('B') ? (uint8_t)parser.value_int() : 2;
  p.report_ms                    = parser.seenval('I') ? (uint16_t)parser.value_int() : 250;
  p.encoder_events_per_mm        = parser.seenval('C') ? parser.value_float() : 0.685f;
  p.pass_efficiency_pct          = parser.seenval('P') ? parser.value_float() : 95.0f;
  p.temp_tolerance               = parser.seenval('D') ? parser.value_float() : 5.0f;
  p.auto_stop_enabled            = parser.seenval('A') ? parser.value_bool() : true;
  p.monitor_window_mm            = parser.seenval('W') ? parser.value_float() : 20.0f;
  p.monitor_efficiency_pct       = parser.seenval('R') ? parser.value_float() : 85.0f;
  p.monitor_confirm_windows      = parser.seenval('K') ? (uint8_t)parser.value_int() : 2;
  p.pulse_gap_factor             = parser.seenval('G') ? parser.value_float() : 4.0f;
  p.pulse_gap_min_ms             = parser.seenval('H') ? (uint16_t)parser.value_int() : 500;
  p.pulse_gap_min_missing_events = parser.seenval('X') ? parser.value_float() : 2.0f;

  makeit_fa_campaign.start(campaign_id, campaign);
}

#endif // MAKEIT_FILAMENT_ANALYZER_PHASE0
