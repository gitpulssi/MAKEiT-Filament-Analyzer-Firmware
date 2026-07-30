from __future__ import annotations

import threading
from typing import Any, Dict, List, Tuple

from . import _number
from .invalid_temp_continue_patch import MakeItFilamentAnalyzerPluginV023

PLUGIN_VERSION = "0.2.4"


def _value_or_default(raw: Dict[str, Any], key: str, default: float) -> Any:
    value = raw.get(key)
    if value is None or value == "":
        return default
    return value


def _normalize_print_geometry(
    raw: Dict[str, Any],
    definition: Dict[str, Any],
) -> Dict[str, float]:
    nozzle = float(definition.get("nozzle_diameter_mm", 0.6))
    line_width = _number(
        {"value": _value_or_default(raw, "print_line_width_mm", nozzle)},
        "value",
        minimum=0.05,
        maximum=5.0,
    )
    layer_height = _number(
        {"value": _value_or_default(raw, "print_layer_height_mm", nozzle * 0.5)},
        "value",
        minimum=0.01,
        maximum=3.0,
    )
    safety_pct = _number(
        {"value": _value_or_default(raw, "print_speed_safety_pct", 90.0)},
        "value",
        minimum=1.0,
        maximum=100.0,
    )
    return {
        "print_line_width_mm": line_width,
        "print_layer_height_mm": layer_height,
        "print_speed_safety_pct": safety_pct,
    }


def _recommended_print_speed_mm_s(
    delivered_flow_mm3_s: float,
    line_width_mm: float,
    layer_height_mm: float,
    safety_pct: float,
) -> float:
    area_mm2 = line_width_mm * layer_height_mm
    if area_mm2 <= 0.0:
        return 0.0
    return delivered_flow_mm3_s * safety_pct / 100.0 / area_mm2


def _add_print_geometry_defaults(state: Dict[str, Any]) -> None:
    definition = state.setdefault("definition", {})
    nozzle = float(definition.get("nozzle_diameter_mm", 0.6))
    definition.setdefault("print_line_width_mm", nozzle)
    definition.setdefault("print_layer_height_mm", nozzle * 0.5)
    definition.setdefault("print_speed_safety_pct", 90.0)


class MakeItFilamentAnalyzerPluginV024(MakeItFilamentAnalyzerPluginV023):
    """Add user-adjustable print-speed visualization geometry."""

    def get_assets(self) -> Dict[str, List[str]]:
        assets = super().get_assets()
        assets["js"] = list(assets.get("js", [])) + [
            "js/makeit_filament_analyzer_v024.js"
        ]
        assets["css"] = list(assets.get("css", [])) + [
            "css/makeit_filament_analyzer_v024.css"
        ]
        return assets

    def get_template_configs(self) -> List[Dict[str, Any]]:
        return [
            dict(
                type="tab",
                name="Filament Analyzer",
                template="makeit_filament_analyzer_tab_v024.jinja2",
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

    def _validate_definition(
        self,
        raw: Any,
    ) -> Tuple[Dict[str, Any], Dict[str, Any]]:
        definition, preview = super()._validate_definition(raw)
        geometry = _normalize_print_geometry(raw, definition)
        definition.update(geometry)
        preview["print_geometry"] = dict(geometry)
        preview["print_speed_formula"] = (
            "delivered_flow_mm3_s * safety_pct / 100 / "
            "(line_width_mm * layer_height_mm)"
        )
        return definition, preview

    def _load_saved_run(self, run_id: int) -> Dict[str, Any]:
        state = super()._load_saved_run(run_id)
        _add_print_geometry_defaults(state)
        return state


__plugin_name__ = "MAKEiT Filament Analyzer"
__plugin_version__ = PLUGIN_VERSION
__plugin_description__ = (
    "Adjustable material-agnostic temperature/speed mapping and visualization"
)
__plugin_pythoncompat__ = ">=3.9,<4"


def __plugin_load__() -> None:
    global __plugin_implementation__
    __plugin_implementation__ = MakeItFilamentAnalyzerPluginV024()

    global __plugin_hooks__
    __plugin_hooks__ = {
        "octoprint.comm.protocol.gcode.received": (
            __plugin_implementation__.received_gcode
        ),
        "octoprint.comm.protocol.firmware.info": (
            __plugin_implementation__.firmware_info
        ),
    }
