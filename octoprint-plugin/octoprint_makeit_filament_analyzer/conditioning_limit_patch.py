from __future__ import annotations

from pathlib import Path
import threading
from typing import Any, Dict, List, Optional

from . import (
    PLUGIN_ID,
    MakeItFilamentAnalyzerPlugin,
    _float_field,
    _flow_mm3_s,
    _int_field,
)

PLUGIN_VERSION = "0.2.2"
PACKAGE_DIR = Path(__file__).resolve().parent


def _conditioning_limit_point(
    fields: Dict[str, Any],
    definition: Dict[str, Any],
    temperature_c: float,
) -> Dict[str, Any]:
    feed = _float_field(fields, "feed_mm_min") or 0.0
    conditioning_mm = _float_field(fields, "conditioning_mm") or 0.0
    efficiency = _float_field(fields, "conditioning_eff") or 0.0
    filament_diameter = float(definition.get("filament_diameter_mm", 1.75))
    commanded_flow = _flow_mm3_s(feed, filament_diameter)
    encoder_events_per_mm = float(definition.get("encoder_events_per_mm", 0.0))
    current_temp = _float_field(fields, "temp")

    return {
        "temperature_c": temperature_c,
        "feed_mm_min": feed,
        "commanded_flow_mm3_s": round(commanded_flow, 5),
        "tested_mm": conditioning_mm,
        "requested_mm": conditioning_mm,
        "expected_encoder_events": round(conditioning_mm * encoder_events_per_mm, 5),
        "actual_encoder_events": _int_field(fields, "conditioning_events"),
        "efficiency_pct": efficiency,
        "delivered_flow_mm3_s": round(commanded_flow * efficiency / 100.0, 5),
        "temperature_avg_c": current_temp,
        "temperature_min_c": current_temp,
        "temperature_max_c": current_temp,
        "temperature_droop_c": (
            round(temperature_c - current_temp, 5)
            if current_temp is not None
            else None
        ),
        "heater_average_raw": None,
        "result": "CONDITIONING_LIMIT",
        "classification": "HARD_THROUGHPUT_FAIL",
        "result_generation": None,
        "failure_stage": "CONDITIONING",
        "measurement_started": False,
        "conditioning_mm": conditioning_mm,
        "conditioning_efficiency_pct": efficiency,
        "source_fields": dict(fields),
        "raw": fields.get("raw"),
    }


def _append_conditioning_limit_points(state: Dict[str, Any]) -> int:
    points = state.setdefault("points", [])
    definition = state.get("definition") or {}
    added = 0

    for row in state.get("rows") or []:
        if row.get("tag") != "conditioning_limit":
            continue

        feed = _float_field(row, "feed_mm_min") or 0.0
        target = _float_field(row, "target")
        if target is None:
            target = _float_field(row, "temperature_c") or 0.0

        duplicate = any(
            abs(float(point.get("temperature_c", -1.0)) - target) < 0.0001
            and abs(float(point.get("feed_mm_min", -1.0)) - feed) < 0.0001
            for point in points
        )
        if duplicate:
            continue

        points.append(_conditioning_limit_point(row, definition, target))
        added += 1

    return added


class MakeItFilamentAnalyzerPluginV022(MakeItFilamentAnalyzerPlugin):
    """OctoPrint controller with deterministic UI resource locations."""

    def get_asset_folder(self) -> str:
        return str(PACKAGE_DIR / "static")

    def get_template_folder(self) -> str:
        return str(PACKAGE_DIR / "templates")

    def get_template_configs(self) -> List[Dict[str, Any]]:
        return [
            dict(
                type="tab",
                name="Filament Analyzer",
                template="makeit_filament_analyzer_tab.jinja2",
                custom_bindings=True,
            )
        ]

    def on_after_startup(self) -> None:
        self._stop_event.clear()
        self._worker = threading.Thread(
            target=self._worker_loop,
            name="makeit-fa-worker",
            daemon=True,
        )
        self._worker.start()
        self._logger.info(
            "MAKEiT Filament Analyzer controller %s started; template=%s; assets=%s",
            PLUGIN_VERSION,
            self.get_template_folder(),
            self.get_asset_folder(),
        )

    def _load_saved_run(self, run_id: int) -> Dict[str, Any]:
        state = super()._load_saved_run(run_id)
        _append_conditioning_limit_points(state)
        return state

    def _handle_row_terminal(self, state: str, fields: Dict[str, str]) -> None:
        synthetic_point: Optional[Dict[str, Any]] = None

        if state == "LIMIT_FOUND" and fields.get("tag") == "conditioning_limit":
            with self._lock:
                feed = _float_field(fields, "feed_mm_min") or 0.0
                target = _float_field(fields, "target")
                if target is None:
                    temperatures = self._state.get("temperatures") or []
                    index = int(self._state.get("temperature_index") or 0)
                    target = float(temperatures[index]) if index < len(temperatures) else 0.0

                duplicate = any(
                    abs(float(point.get("temperature_c", -1.0)) - target) < 0.0001
                    and abs(float(point.get("feed_mm_min", -1.0)) - feed) < 0.0001
                    for point in self._state.get("points", [])
                )

                if not duplicate:
                    definition = self._state.get("definition") or {}
                    synthetic_point = _conditioning_limit_point(fields, definition, target)
                    self._state["points"].append(synthetic_point)

            if synthetic_point is not None:
                self._plugin_manager.send_plugin_message(
                    PLUGIN_ID,
                    {
                        "type": "FA7",
                        "fields": dict(fields),
                        "point": synthetic_point,
                        "synthetic": True,
                    },
                )

        super()._handle_row_terminal(state, fields)


__plugin_name__ = "MAKEiT Filament Analyzer"
__plugin_version__ = PLUGIN_VERSION
__plugin_description__ = (
    "Adjustable material-agnostic temperature/speed mapping and visualization"
)
__plugin_pythoncompat__ = ">=3.9,<4"


def __plugin_load__() -> None:
    global __plugin_implementation__
    __plugin_implementation__ = MakeItFilamentAnalyzerPluginV022()

    global __plugin_hooks__
    __plugin_hooks__ = {
        "octoprint.comm.protocol.gcode.received": (
            __plugin_implementation__.received_gcode
        ),
        "octoprint.comm.protocol.firmware.info": (
            __plugin_implementation__.firmware_info
        ),
    }
