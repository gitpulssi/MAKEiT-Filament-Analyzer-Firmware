#!/usr/bin/env python3
"""Wire Phase-9 recovery into M870 row flow, ID allocation, and cancel."""

from phase9_patch_common import ensure


def patch_source_flow() -> None:
    path = "Marlin/src/feature/makeit_fa_envelope.cpp"

    ensure(
        path,
        "r.recovery_required = recovery_required_;",
        """  r.last_point_crc = makeit_fa_campaign.last_point_result_crc();
  r.campaign_state = uint8_t(makeit_fa_campaign.state());
  r.last_point_result_code = makeit_fa_campaign.last_point_result_code();""",
        """  r.last_point_crc = makeit_fa_campaign.last_point_result_crc();
  r.recovery_point_id = recovery_point_id_;
  r.recovery_crc = recovery_result_crc_;
  r.campaign_state = uint8_t(makeit_fa_campaign.state());
  r.last_point_result_code = makeit_fa_campaign.last_point_result_code();
  r.recovery_result_code = recovery_result_code_;
  r.recovery_required = recovery_required_;""",
    )

    ensure(
        path,
        "complete_with_limit",
        """    case MakeItFASpeedCampaign::CAM_COMPLETE:
      if (uint8_t(row_index_ + 1) >= row_count_) {
        finish(ENV_COMPLETE, "complete");
        return;
      }
      ++row_index_;
      if (!start_current_row())
        finish(ENV_ERROR, "next_row_start_failed");
      break;

    case MakeItFASpeedCampaign::CAM_LIMIT_FOUND:
      // Continuing after a real feed-loss point requires the future recovery /
      // re-prime state machine. Stop here rather than contaminate later rows.
      finish(ENV_LIMIT_FOUND, "limit_found_recovery_required");
      break;""",
        """    case MakeItFASpeedCampaign::CAM_COMPLETE:
      if (uint8_t(row_index_ + 1) >= row_count_) {
        finish(ENV_COMPLETE, "complete");
        return;
      }
      ++row_index_;
      clear_row_recovery();
      if (!start_current_row())
        finish(ENV_ERROR, "next_row_start_failed");
      break;

    case MakeItFASpeedCampaign::CAM_LIMIT_FOUND:
      if (uint8_t(row_index_ + 1) >= row_count_) {
        finish(ENV_COMPLETE, "complete_with_limit");
        return;
      }
      ++row_index_;
      if (!prepare_recovery_for_current_row())
        finish(ENV_ERROR, "recovery_prepare_failed");
      break;""",
    )

    ensure(
        path,
        "const uint32_t recovery_slots =",
        """  const uint32_t total_points = uint32_t(rows) * uint32_t(speed_points);
  if (!total_points || envelope_id > 0xFFFFFFFFUL - (total_points - 1UL)) {""",
        """  const uint32_t recovery_slots = rows > 1 ? uint32_t(rows - 1) : 0UL;
  const uint32_t total_points = uint32_t(rows) * uint32_t(speed_points) + recovery_slots;
  if (!total_points || envelope_id > 0xFFFFFFFFUL - (total_points - 1UL)) {""",
    )

    ensure(
        path,
        "current_temp_c_ = normalized.start_temp_c;\n  clear_row_recovery();",
        """  original_target_temp_c_ = normalized.start_temp_c;
  current_temp_c_ = normalized.start_temp_c;
  for (uint8_t i = 0; i < MAX_ROWS; ++i) rows_[i] = RowResult{};""",
        """  original_target_temp_c_ = normalized.start_temp_c;
  current_temp_c_ = normalized.start_temp_c;
  clear_row_recovery();
  for (uint8_t i = 0; i < MAX_ROWS; ++i) rows_[i] = RowResult{};""",
    )

    ensure(
        path,
        "void MakeItFATemperatureEnvelope::idle() {\n  switch (state_) {",
        """void MakeItFATemperatureEnvelope::idle() {
  if (state_ != ENV_RUNNING_ROW) return;
  if (makeit_fa_campaign.active()) return;

  if (!makeit_fa_campaign.terminal()
      || makeit_fa_campaign.current_campaign_id() != current_row_campaign_id_) {
    finish(ENV_ERROR, "campaign_state_error");
    return;
  }

  handle_row_result();
}""",
        """void MakeItFATemperatureEnvelope::idle() {
  switch (state_) {
    case ENV_WAIT_RECOVERY_TEMP:
      service_recovery_wait();
      return;

    case ENV_RUNNING_RECOVERY:
      if (makeit_fa_transaction.running()) return;
      if (!makeit_fa_transaction.terminal()
          || makeit_fa_transaction.current_point_id() != recovery_point_id_) {
        finish(ENV_ERROR, "recovery_transaction_state_error");
        return;
      }
      handle_recovery_result();
      return;

    case ENV_RUNNING_ROW:
      if (makeit_fa_campaign.active()) return;
      if (!makeit_fa_campaign.terminal()
          || makeit_fa_campaign.current_campaign_id() != current_row_campaign_id_) {
        finish(ENV_ERROR, "campaign_state_error");
        return;
      }
      handle_row_result();
      return;

    default:
      return;
  }
}""",
    )


def patch_cancel() -> None:
    path = "Marlin/src/feature/makeit_fa_envelope.cpp"

    old = r'''bool MakeItFATemperatureEnvelope::cancel(const bool has_envelope_id, const uint32_t requested_envelope_id) {
  idle();

  if (!record_valid_) {
    SERIAL_ECHOLNPGM("FA8: error=NO_ENVELOPE");
    return false;
  }
  if (has_envelope_id && requested_envelope_id != envelope_id_) {
    SERIAL_ECHOPGM("FA8: error=NOT_FOUND requested_envelope_id="); SERIAL_ECHO(requested_envelope_id);
    SERIAL_ECHOPGM(" retained_envelope_id="); SERIAL_ECHO(envelope_id_);
    SERIAL_ECHOLNPGM("");
    return false;
  }
  if (state_ == ENV_ABORTED && cancel_requested_) {
    report("cancel_replay", true);
    return true;
  }
  if (state_ != ENV_RUNNING_ROW) {
    SERIAL_ECHOPGM("FA8: error=NOT_ACTIVE state="); SERIAL_ECHO(state_name(state_));
    SERIAL_ECHOLNPGM("");
    return false;
  }
  if (cancel_requested_) {
    report("cancel_replay");
    return true;
  }

  cancel_requested_ = true;
  if (!makeit_fa_campaign.cancel(true, current_row_campaign_id_)) {
    cancel_requested_ = false;
    SERIAL_ECHOLNPGM("FA8: error=CANCEL_REJECTED");
    return false;
  }
  report("cancel_requested");
  return true;
}'''

    new = r'''bool MakeItFATemperatureEnvelope::cancel(const bool has_envelope_id, const uint32_t requested_envelope_id) {
  idle();

  if (!record_valid_) {
    SERIAL_ECHOLNPGM("FA8: error=NO_ENVELOPE");
    return false;
  }
  if (has_envelope_id && requested_envelope_id != envelope_id_) {
    SERIAL_ECHOPGM("FA8: error=NOT_FOUND requested_envelope_id="); SERIAL_ECHO(requested_envelope_id);
    SERIAL_ECHOPGM(" retained_envelope_id="); SERIAL_ECHO(envelope_id_);
    SERIAL_ECHOLNPGM("");
    return false;
  }
  if (state_ == ENV_ABORTED && cancel_requested_) {
    report("cancel_replay", true);
    return true;
  }
  if (!active()) {
    SERIAL_ECHOPGM("FA8: error=NOT_ACTIVE state="); SERIAL_ECHO(state_name(state_));
    SERIAL_ECHOLNPGM("");
    return false;
  }
  if (cancel_requested_) {
    report("cancel_replay");
    return true;
  }

  cancel_requested_ = true;

  if (state_ == ENV_WAIT_RECOVERY_TEMP) {
    finish(ENV_ABORTED, "cancelled_recovery_wait");
    return true;
  }

  if (state_ == ENV_RUNNING_RECOVERY) {
    if (!makeit_fa_transaction.request_abort(recovery_point_id_)) {
      cancel_requested_ = false;
      SERIAL_ECHOLNPGM("FA8: error=RECOVERY_CANCEL_REJECTED");
      return false;
    }
    report("cancel_requested");
    return true;
  }

  if (!makeit_fa_campaign.cancel(true, current_row_campaign_id_)) {
    cancel_requested_ = false;
    SERIAL_ECHOLNPGM("FA8: error=CANCEL_REJECTED");
    return false;
  }
  report("cancel_requested");
  return true;
}'''

    ensure(path, "RECOVERY_CANCEL_REJECTED", old, new)


def main() -> int:
    patch_source_flow()
    patch_cancel()
    print("Phase-9 recovery flow is in place.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
