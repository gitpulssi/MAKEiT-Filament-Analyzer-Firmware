from __future__ import annotations

import threading
from typing import Any, Dict, List, Optional, Tuple

from . import PLUGIN_ID
from .conditioning_limit_patch import MakeItFilamentAnalyzerPluginV022

PLUGIN_VERSION = "0.2.3"


def _setting_bool(value: Any, default: bool) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    text = str(value).strip().lower()
    if text in {"1", "true", "yes", "on"}:
        return True
    if text in {"0", "false", "no", "off"}:
        return False
    return default


def _invalid_temp_action(
    *,
    next_index: int,
    temperature_count: int,
    continue_enabled: bool,
    recovery_enabled: bool,
    recover_after_invalid: bool,
) -> Tuple[str, Optional[int]]:
    """Return the controller action after an INVALID_TEMP row.

    INVALID_TEMP means the measured point left the user-selected D band. It is a
    valid graph cell and row boundary, not a Marlin thermal-protection fault.
    """
    if not continue_enabled:
        return "stop", None
    if next_index >= temperature_count:
        return "complete", None
    if recovery_enabled and recover_after_invalid:
        return "recover", next_index
    return "row", next_index


class MakeItFilamentAnalyzerPluginV023(MakeItFilamentAnalyzerPluginV022):
    """Continue a material map after a row ends with INVALID_TEMP."""

    def get_settings_defaults(self) -> Dict[str, Any]:
        defaults = super().get_settings_defaults()
        defaults.update(
            continue_after_invalid_temp=True,
            recover_after_invalid_temp=True,
        )
        return defaults

    def get_assets(self) -> Dict[str, List[str]]:
        assets = super().get_assets()
        assets["css"] = list(assets.get("css", [])) + [
            "css/makeit_filament_analyzer_v023.css"
        ]
        return assets

    def on_after_startup(self) -> None:
        self._stop_event.clear()
        self._worker = threading.Thread(
            target=self._worker_loop,
            name="makeit-fa-worker",
            daemon=True,
        )
        self._worker.start()
        self._logger.info(
            "MAKEiT Filament Analyzer controller %s started; template=%s; assets=%s; "
            "continue_after_invalid_temp=%s; recover_after_invalid_temp=%s",
            PLUGIN_VERSION,
            self.get_template_folder(),
            self.get_asset_folder(),
            _setting_bool(
                self._settings.get(["continue_after_invalid_temp"]),
                True,
            ),
            _setting_bool(
                self._settings.get(["recover_after_invalid_temp"]),
                True,
            ),
        )

    def _handle_row_terminal(self, state: str, fields: Dict[str, str]) -> None:
        if state != "INVALID_TEMP":
            super()._handle_row_terminal(state, fields)
            return

        with self._lock:
            index = int(self._state.get("temperature_index") or 0)
            temperatures = list(self._state.get("temperatures") or [])
            temperature_c = temperatures[index] if 0 <= index < len(temperatures) else None

            self._state["had_invalid_temperature"] = True
            invalid_rows = self._state.setdefault("invalid_temperature_rows", [])
            row_record = {
                "temperature_index": index,
                "temperature_c": temperature_c,
                "campaign_id": self._state.get("current_campaign_id"),
                "fields": dict(fields),
            }
            if not any(
                item.get("campaign_id") == row_record["campaign_id"]
                for item in invalid_rows
            ):
                invalid_rows.append(row_record)

            self._checkpoint_locked()

            if self._state["status"] == "CANCELLING":
                self._finish_run_locked(
                    "ABORTED",
                    {
                        "source": "FA7",
                        "state": state,
                        "tag": fields.get("tag", ""),
                        "fields": fields,
                    },
                )
                action = "stop"
                action_index: Optional[int] = None
            else:
                next_index = index + 1
                definition = self._state.get("definition") or {}
                recovery_enabled = float(definition.get("recovery_temp_c", 0.0)) > 0.0
                action, action_index = _invalid_temp_action(
                    next_index=next_index,
                    temperature_count=len(temperatures),
                    continue_enabled=_setting_bool(
                        self._settings.get(["continue_after_invalid_temp"]),
                        True,
                    ),
                    recovery_enabled=recovery_enabled,
                    recover_after_invalid=_setting_bool(
                        self._settings.get(["recover_after_invalid_temp"]),
                        True,
                    ),
                )

                if action == "stop":
                    self._finish_run_locked(
                        "INVALID_TEMP",
                        {
                            "source": "FA7",
                            "state": state,
                            "tag": fields.get("tag", ""),
                            "fields": fields,
                        },
                    )
                elif action == "complete":
                    self._finish_run_locked(
                        "COMPLETE_WITH_INVALID_TEMP",
                        {
                            "source": "FA7",
                            "state": state,
                            "tag": fields.get("tag", ""),
                            "fields": fields,
                        },
                    )

            self._logger.warning(
                "Analyzer row ended INVALID_TEMP at temperature=%s campaign_id=%s; action=%s",
                temperature_c,
                row_record["campaign_id"],
                action,
            )

        if action in {"stop", "complete"}:
            self._printer.commands(
                ["M104 S0"],
                tags={"source:plugin", f"plugin:{PLUGIN_ID}"},
            )
            self._publish()
        elif action == "recover" and action_index is not None:
            self._start_recovery(action_index)
        elif action == "row" and action_index is not None:
            self._start_row(action_index)


__plugin_name__ = "MAKEiT Filament Analyzer"
__plugin_version__ = PLUGIN_VERSION
__plugin_description__ = (
    "Adjustable material-agnostic temperature/speed mapping and visualization"
)
__plugin_pythoncompat__ = ">=3.9,<4"


def __plugin_load__() -> None:
    global __plugin_implementation__
    __plugin_implementation__ = MakeItFilamentAnalyzerPluginV023()

    global __plugin_hooks__
    __plugin_hooks__ = {
        "octoprint.comm.protocol.gcode.received": (
            __plugin_implementation__.received_gcode
        ),
        "octoprint.comm.protocol.firmware.info": (
            __plugin_implementation__.firmware_info
        ),
    }
