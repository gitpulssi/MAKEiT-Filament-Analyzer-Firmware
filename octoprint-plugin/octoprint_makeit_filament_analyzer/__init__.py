from __future__ import annotations

import copy
import csv
import json
import math
import os
import queue
import re
import threading
import time
from typing import Any, Dict, List, Optional, Tuple

import flask
import octoprint.plugin


PLUGIN_ID = "makeit_filament_analyzer"
PLUGIN_VERSION = "0.2.0"
KV_RE = re.compile(r"([A-Za-z0-9_]+)=([^\s]+)")
TELEMETRY_PREFIXES = (
    "FA1:", "FA2:", "FA3:", "FA4:", "FA7:", "FA8:",
    "FA8ROW:", "FA9:", "FA10:", "FATX:",
)
ROW_TERMINAL_STATES = {"COMPLETE", "LIMIT_FOUND", "INVALID_TEMP", "ABORTED", "ERROR"}
RECOVERY_TERMINAL_STATES = {"COMPLETE", "FAILED", "ABORTED", "ERROR"}
CSV_FIELDS = [
    "temperature_c", "feed_mm_min", "commanded_flow_mm3_s",
    "delivered_flow_mm3_s", "efficiency_pct", "classification",
    "result", "tested_mm", "requested_mm", "expected_encoder_events",
    "actual_encoder_events", "temperature_avg_c", "temperature_min_c",
    "temperature_max_c", "temperature_droop_c", "heater_average_raw",
    "result_generation",
]


class ValidationError(ValueError):
    pass


def _number(
    data: Dict[str, Any],
    key: str,
    *,
    minimum: Optional[float] = None,
    maximum: Optional[float] = None,
) -> float:
    try:
        value = float(data[key])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValidationError(f"{key} must be a number") from exc
    if not math.isfinite(value):
        raise ValidationError(f"{key} must be finite")
    if minimum is not None and value < minimum:
        raise ValidationError(f"{key} must be at least {minimum}")
    if maximum is not None and value > maximum:
        raise ValidationError(f"{key} must be no more than {maximum}")
    return value


def _integer(data: Dict[str, Any], key: str, *, minimum: int, maximum: int) -> int:
    value = _number(data, key, minimum=float(minimum), maximum=float(maximum))
    result = int(round(value))
    if abs(value - result) > 1e-9:
        raise ValidationError(f"{key} must be an integer")
    return result


def _inclusive_grid(start: float, end: float, step: float, limit: int) -> List[float]:
    if step <= 0:
        raise ValidationError("grid step must be greater than zero")
    if end < start:
        raise ValidationError("grid end must be greater than or equal to grid start")

    count = int(math.floor((end - start) / step + 1e-9)) + 1
    values = [start + index * step for index in range(count)]
    if not values:
        values = [start]
    tolerance = max(1e-6, step * 1e-6)
    if values[-1] < end - tolerance:
        values.append(end)
    if len(values) > limit:
        raise ValidationError(f"grid contains {len(values)} values; limit is {limit}")
    return [round(value, 6) for value in values]


def _fmt(value: float) -> str:
    return f"{value:.6f}".rstrip("0").rstrip(".")


def _flow_mm3_s(feed_mm_min: float, filament_diameter_mm: float) -> float:
    return feed_mm_min * math.pi * filament_diameter_mm * filament_diameter_mm / 240.0


def _parse_fields(line: str) -> Tuple[str, Dict[str, str]]:
    prefix, _, remainder = line.partition(":")
    return prefix, {
        match.group(1): match.group(2)
        for match in KV_RE.finditer(remainder)
    }


def _float_field(fields: Dict[str, str], key: str) -> Optional[float]:
    try:
        return float(fields[key])
    except (KeyError, TypeError, ValueError):
        return None


def _int_field(fields: Dict[str, str], key: str) -> Optional[int]:
    try:
        return int(fields[key], 10)
    except (KeyError, TypeError, ValueError):
        return None


def _classify_point(
    *,
    result: str,
    tested_mm: float,
    requested_mm: float,
    efficiency_pct: float,
    accuracy_threshold_pct: float,
    throughput_threshold_pct: float,
) -> str:
    if result == "INVALID_TEMP":
        return "INVALID_TEMP"
    if result == "ABORTED":
        return "ABORTED"
    if tested_mm + 0.001 < requested_mm or efficiency_pct < throughput_threshold_pct:
        return "HARD_THROUGHPUT_FAIL"
    if efficiency_pct >= accuracy_threshold_pct:
        return "ACCURATE"
    return "BELOW_ACCURACY_ABOVE_THROUGHPUT"


class MakeItFilamentAnalyzerPlugin(
    octoprint.plugin.StartupPlugin,
    octoprint.plugin.ShutdownPlugin,
    octoprint.plugin.SettingsPlugin,
    octoprint.plugin.AssetPlugin,
    octoprint.plugin.TemplatePlugin,
    octoprint.plugin.SimpleApiPlugin,
    octoprint.plugin.EventHandlerPlugin,
):
    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._telemetry_queue: "queue.Queue[str]" = queue.Queue(maxsize=8192)
        self._stop_event = threading.Event()
        self._worker: Optional[threading.Thread] = None
        self._last_keepalive_monotonic = 0.0
        self._firmware_info: Dict[str, Any] = {}
        self._state: Dict[str, Any] = self._empty_state()

    @staticmethod
    def _empty_state() -> Dict[str, Any]:
        return {
            "status": "IDLE",
            "phase": "IDLE",
            "run_id": None,
            "definition": None,
            "preview": None,
            "temperatures": [],
            "speeds_mm_min": [],
            "temperature_index": 0,
            "current_campaign_id": None,
            "current_recovery_id": None,
            "points": [],
            "rows": [],
            "recoveries": [],
            "raw": [],
            "active_target_c": None,
            "started_at": None,
            "finished_at": None,
            "terminal": None,
            "saved_json": None,
            "saved_csv": None,
            "error": None,
            "firmware": {},
        }

    def get_settings_defaults(self) -> Dict[str, Any]:
        return {
            "machine_min_temp_c": 170.0,
            "machine_max_temp_c": 450.0,
            "machine_max_feed_mm_min": 2000.0,
            "max_temperature_points": 100,
            "max_speed_points": 100,
            "max_total_points": 2000,
            "filament_confirmation_mm": 10000.0,
            "heater_keepalive_seconds": 180,
        }

    def get_assets(self) -> Dict[str, List[str]]:
        return {
            "js": ["js/makeit_filament_analyzer.js"],
            "css": ["css/makeit_filament_analyzer.css"],
        }

    def get_template_configs(self) -> List[Dict[str, Any]]:
        return [dict(type="tab", name="Filament Analyzer", custom_bindings=True)]

    def on_after_startup(self) -> None:
        self._stop_event.clear()
        self._worker = threading.Thread(
            target=self._worker_loop,
            name="makeit-fa-worker",
            daemon=True,
        )
        self._worker.start()
        self._logger.info("MAKEiT Filament Analyzer controller %s started", PLUGIN_VERSION)

    def on_shutdown(self) -> None:
        self._stop_event.set()
        if self._worker and self._worker.is_alive():
            self._worker.join(timeout=3.0)

    def on_event(self, event: str, payload: Dict[str, Any]) -> None:
        disconnected = event in {"Disconnected", "Error"}
        if not disconnected:
            return
        with self._lock:
            if not self._is_active_locked():
                return
            self._state["error"] = f"OctoPrint event {event} interrupted the analyzer run"
            safe_payload: Any
            if isinstance(payload, dict):
                safe_payload = {
                    str(key): value
                    if isinstance(value, (str, int, float, bool, type(None)))
                    else str(value)
                    for key, value in payload.items()
                }
            else:
                safe_payload = str(payload)
            self._finish_run_locked(
                "INTERRUPTED",
                {"source": "octoprint_event", "event": event, "payload": safe_payload},
            )
        self._publish()

    def firmware_info(
        self,
        comm_instance: Any,
        firmware_name: str,
        firmware_data: Dict[str, Any],
        *args: Any,
        **kwargs: Any,
    ) -> None:
        # This hook executes on the serial receive path: only copy small data.
        with self._lock:
            self._firmware_info = {
                "firmware_name": firmware_name,
                "firmware_data": dict(firmware_data or {}),
                "received_at": time.time(),
            }
            if self._is_active_locked():
                self._state["firmware"] = copy.deepcopy(self._firmware_info)

    def get_api_commands(self) -> Dict[str, List[str]]:
        return {
            "validate": ["definition"],
            "start": ["definition"],
            "cancel": [],
            "clear": [],
            "delete_run": ["run_id"],
        }

    def is_api_protected(self) -> bool:
        return True

    def on_api_get(self, request: Any) -> flask.Response:
        action = str(request.args.get("action") or "state")
        if action == "runs":
            return flask.jsonify(runs=self._list_saved_runs())
        if action == "load":
            run_id = self._validated_run_id(request.args.get("run_id"))
            return flask.jsonify(self._load_saved_run(run_id))
        if action == "csv":
            run_id = self._validated_run_id(request.args.get("run_id"))
            path = self._saved_path(run_id, "csv")
            if not os.path.isfile(path):
                flask.abort(404)
            return flask.send_file(
                path,
                mimetype="text/csv",
                as_attachment=True,
                download_name=f"makeit-filament-run-{run_id}.csv",
            )
        if action == "json":
            run_id = self._validated_run_id(request.args.get("run_id"))
            path = self._saved_path(run_id, "json")
            if not os.path.isfile(path):
                flask.abort(404)
            return flask.send_file(
                path,
                mimetype="application/json",
                as_attachment=True,
                download_name=f"makeit-filament-run-{run_id}.json",
            )
        with self._lock:
            return flask.jsonify(self._public_state_locked())

    def on_api_command(self, command: str, data: Dict[str, Any]) -> flask.Response:
        try:
            if command == "validate":
                normalized, preview = self._validate_definition(data["definition"])
                return flask.jsonify(valid=True, definition=normalized, preview=preview)
            if command == "start":
                return self._api_start(data["definition"])
            if command == "cancel":
                return self._api_cancel()
            if command == "clear":
                with self._lock:
                    if self._is_active_locked():
                        raise ValidationError("cannot clear an active run")
                    self._state = self._empty_state()
                self._publish()
                return flask.jsonify(ok=True)
            if command == "delete_run":
                run_id = self._validated_run_id(data.get("run_id"))
                self._delete_saved_run(run_id)
                return flask.jsonify(ok=True, runs=self._list_saved_runs())
        except ValidationError as exc:
            return flask.jsonify(valid=False, error=str(exc)), 400
        return flask.jsonify(error=f"unsupported command {command}"), 400

    def _is_active_locked(self) -> bool:
        return self._state["status"] in {
            "STARTING", "RUNNING", "RECOVERING", "CANCELLING",
        }

    def _validate_definition(self, raw: Any) -> Tuple[Dict[str, Any], Dict[str, Any]]:
        if not isinstance(raw, dict):
            raise ValidationError("definition must be an object")

        machine_min_temp = float(self._settings.get(["machine_min_temp_c"]))
        machine_max_temp = float(self._settings.get(["machine_max_temp_c"]))
        machine_max_feed = float(self._settings.get(["machine_max_feed_mm_min"]))
        max_temp_points = int(self._settings.get(["max_temperature_points"]))
        max_speed_points = int(self._settings.get(["max_speed_points"]))
        max_total_points = int(self._settings.get(["max_total_points"]))

        d: Dict[str, Any] = {
            "run_name": str(raw.get("run_name") or "Filament test").strip()[:120],
            "material_family": str(raw.get("material_family") or "Custom").strip()[:80],
            "material_name": str(raw.get("material_name") or "").strip()[:160],
            "manufacturer": str(raw.get("manufacturer") or "").strip()[:120],
            "color": str(raw.get("color") or "").strip()[:80],
            "lot": str(raw.get("lot") or "").strip()[:120],
            "notes": str(raw.get("notes") or "").strip()[:4000],
            "extruder_name": str(raw.get("extruder_name") or "").strip()[:160],
            "printer_name": str(raw.get("printer_name") or "").strip()[:160],
            "nozzle_material": str(raw.get("nozzle_material") or "").strip()[:80],
            "filament_diameter_mm": _number(
                raw, "filament_diameter_mm", minimum=0.5, maximum=4.0
            ),
            "nozzle_diameter_mm": _number(
                raw, "nozzle_diameter_mm", minimum=0.05, maximum=5.0
            ),
            "temperature_start_c": _number(
                raw, "temperature_start_c",
                minimum=machine_min_temp, maximum=machine_max_temp,
            ),
            "temperature_end_c": _number(
                raw, "temperature_end_c",
                minimum=machine_min_temp, maximum=machine_max_temp,
            ),
            "temperature_step_c": _number(
                raw, "temperature_step_c", minimum=1.0, maximum=200.0
            ),
            "speed_start_mm_min": _number(
                raw, "speed_start_mm_min", minimum=1.0, maximum=machine_max_feed
            ),
            "speed_end_mm_min": _number(
                raw, "speed_end_mm_min", minimum=1.0, maximum=machine_max_feed
            ),
            "speed_step_mm_min": _number(
                raw, "speed_step_mm_min", minimum=1.0, maximum=1000.0
            ),
            "conditioning_mm": _number(
                raw, "conditioning_mm", minimum=0.0, maximum=100.0
            ),
            "measurement_mm": _number(
                raw, "measurement_mm", minimum=20.0, maximum=500.0
            ),
            "settle_seconds": _integer(
                raw, "settle_seconds", minimum=0, maximum=120
            ),
            "accuracy_threshold_pct": _number(
                raw, "accuracy_threshold_pct", minimum=50.0, maximum=105.0
            ),
            "throughput_threshold_pct": _number(
                raw, "throughput_threshold_pct", minimum=50.0, maximum=105.0
            ),
            "temperature_tolerance_c": _number(
                raw, "temperature_tolerance_c", minimum=0.5, maximum=15.0
            ),
            "rolling_window_mm": _number(
                raw, "rolling_window_mm", minimum=5.0, maximum=100.0
            ),
            "rolling_confirm_windows": _integer(
                raw, "rolling_confirm_windows", minimum=1, maximum=5
            ),
            "encoder_events_per_mm": _number(
                raw, "encoder_events_per_mm", minimum=0.01, maximum=100.0
            ),
            "segment_mm": _number(
                raw, "segment_mm", minimum=0.05, maximum=0.35
            ),
            "max_inflight": _integer(raw, "max_inflight", minimum=1, maximum=2),
            "report_ms": _integer(raw, "report_ms", minimum=50, maximum=5000),
            "pulse_gap_factor": _number(
                raw, "pulse_gap_factor", minimum=0.0, maximum=20.0
            ),
            "pulse_gap_min_ms": _integer(
                raw, "pulse_gap_min_ms", minimum=50, maximum=30000
            ),
            "pulse_gap_missing_events": _number(
                raw, "pulse_gap_missing_events", minimum=0.5, maximum=20.0
            ),
            "recovery_temp_c": _number(
                raw, "recovery_temp_c", minimum=0.0, maximum=machine_max_temp
            ),
            "recovery_prime_mm": _number(
                raw, "recovery_prime_mm", minimum=5.0, maximum=100.0
            ),
            "recovery_prime_feed_mm_min": _number(
                raw, "recovery_prime_feed_mm_min", minimum=1.0, maximum=500.0
            ),
            "recovery_validation_mm": _number(
                raw, "recovery_validation_mm", minimum=20.0, maximum=100.0
            ),
            "recovery_validation_feed_mm_min": _number(
                raw, "recovery_validation_feed_mm_min", minimum=1.0, maximum=500.0
            ),
            "recovery_accuracy_threshold_pct": _number(
                raw, "recovery_accuracy_threshold_pct", minimum=50.0, maximum=105.0
            ),
        }

        if d["throughput_threshold_pct"] > d["accuracy_threshold_pct"]:
            raise ValidationError("throughput threshold R cannot exceed accuracy threshold P")
        if 0.0 < d["conditioning_mm"] < 10.0:
            raise ValidationError("conditioning must be 0 (disabled) or at least 10 mm")
        required_monitor_length = (
            d["rolling_window_mm"] * d["rolling_confirm_windows"]
        )
        if d["measurement_mm"] + 1e-9 < required_monitor_length:
            raise ValidationError(
                "measurement length must cover rolling window × confirmation count "
                f"({required_monitor_length:g} mm)"
            )
        if 0.0 < d["recovery_temp_c"] < machine_min_temp:
            raise ValidationError(
                "recovery temperature must be 0 (disabled) or at least the machine minimum"
            )

        temperatures = _inclusive_grid(
            d["temperature_start_c"],
            d["temperature_end_c"],
            d["temperature_step_c"],
            max_temp_points,
        )
        speeds = _inclusive_grid(
            d["speed_start_mm_min"],
            d["speed_end_mm_min"],
            d["speed_step_mm_min"],
            max_speed_points,
        )
        total_points = len(temperatures) * len(speeds)
        if total_points > max_total_points:
            raise ValidationError(
                f"grid contains {total_points} points; limit is {max_total_points}"
            )

        filament_mm = total_points * (
            d["conditioning_mm"] + d["measurement_mm"]
        )
        motion_seconds = sum(
            d["settle_seconds"]
            + sum(
                60.0 * (d["conditioning_mm"] + d["measurement_mm"]) / speed
                for speed in speeds
            )
            for _temperature in temperatures
        )

        row_command = self._build_row_command(d, campaign_id=4000000000)
        recovery_command = self._build_recovery_command(d, recovery_id=4000000101)
        if len(row_command) > 191:
            raise ValidationError(
                f"generated M872 command is {len(row_command)} characters; firmware limit is 191"
            )
        if d["recovery_temp_c"] > 0.0 and len(recovery_command) > 191:
            raise ValidationError(
                f"generated M880 command is {len(recovery_command)} characters; firmware limit is 191"
            )

        preview = {
            "temperatures": temperatures,
            "speeds_mm_min": speeds,
            "speeds_mm3_s": [
                round(_flow_mm3_s(speed, d["filament_diameter_mm"]), 4)
                for speed in speeds
            ],
            "rows": len(temperatures),
            "columns": len(speeds),
            "total_points": total_points,
            "estimated_filament_mm": round(filament_mm, 1),
            "estimated_minimum_minutes": round(motion_seconds / 60.0, 1),
            "requires_filament_confirmation": (
                filament_mm
                >= float(self._settings.get(["filament_confirmation_mm"]))
            ),
            "row_gcode_length": len(row_command),
            "recovery_gcode_length": len(recovery_command),
            "gcode": (
                f"M881 P{_fmt(d['conditioning_mm'])}\n"
                f"M109 S{_fmt(temperatures[0])}\n"
                f"{row_command}\n"
                f"... repeated for {len(temperatures)} temperature rows"
            ),
        }
        return d, preview

    def _build_row_command(self, d: Dict[str, Any], campaign_id: int) -> str:
        return (
            f"M872 J{campaign_id} F{_fmt(d['speed_start_mm_min'])} "
            f"U{_fmt(d['speed_end_mm_min'])} V{_fmt(d['speed_step_mm_min'])} "
            f"O{d['settle_seconds']} L{_fmt(d['measurement_mm'])} "
            f"S{_fmt(d['segment_mm'])} B{d['max_inflight']} I{d['report_ms']} "
            f"C{_fmt(d['encoder_events_per_mm'])} "
            f"P{_fmt(d['accuracy_threshold_pct'])} "
            f"D{_fmt(d['temperature_tolerance_c'])} A1 "
            f"W{_fmt(d['rolling_window_mm'])} "
            f"R{_fmt(d['throughput_threshold_pct'])} "
            f"K{d['rolling_confirm_windows']} G{_fmt(d['pulse_gap_factor'])} "
            f"H{d['pulse_gap_min_ms']} X{_fmt(d['pulse_gap_missing_events'])}"
        )

    def _build_recovery_command(self, d: Dict[str, Any], recovery_id: int) -> str:
        return (
            f"M880 J{recovery_id} T{_fmt(d['recovery_temp_c'])} "
            f"O{d['settle_seconds']} L{_fmt(d['recovery_prime_mm'])} "
            f"F{_fmt(d['recovery_prime_feed_mm_min'])} "
            f"V{_fmt(d['recovery_validation_mm'])} "
            f"U{_fmt(d['recovery_validation_feed_mm_min'])} "
            f"S{_fmt(d['segment_mm'])} B{d['max_inflight']} I{d['report_ms']} "
            f"C{_fmt(d['encoder_events_per_mm'])} "
            f"P{_fmt(d['recovery_accuracy_threshold_pct'])} "
            f"D{_fmt(d['temperature_tolerance_c'])} A1 "
            f"W{_fmt(min(d['recovery_validation_mm'], d['rolling_window_mm']))} "
            f"R{_fmt(d['throughput_threshold_pct'])} "
            f"K{d['rolling_confirm_windows']} G{_fmt(d['pulse_gap_factor'])} "
            f"H{d['pulse_gap_min_ms']} X{_fmt(d['pulse_gap_missing_events'])}"
        )

    def _campaign_id(self, temperature_index: int) -> int:
        with self._lock:
            run_id = int(self._state["run_id"])
            speed_count = len(self._state["speeds_mm_min"])
        return run_id + temperature_index * (speed_count + 2)

    def _recovery_id(self, temperature_index: int) -> int:
        with self._lock:
            speed_count = len(self._state["speeds_mm_min"])
        return self._campaign_id(temperature_index) + speed_count + 1

    def _api_start(self, raw_definition: Any) -> flask.Response:
        if not self._printer.is_operational():
            raise ValidationError("printer is not connected and operational")
        if self._printer.is_printing() or self._printer.is_paused():
            raise ValidationError("a print job is active")
        with self._lock:
            if self._is_active_locked():
                raise ValidationError("an analyzer run is already active")

        definition, preview = self._validate_definition(raw_definition)
        run_id = int(time.time() * 1000.0) % 3_000_000_000
        with self._lock:
            self._state = self._empty_state()
            self._state.update(
                status="STARTING",
                phase="STARTING",
                run_id=run_id,
                definition=definition,
                preview=preview,
                temperatures=list(preview["temperatures"]),
                speeds_mm_min=list(preview["speeds_mm_min"]),
                started_at=time.time(),
                firmware=copy.deepcopy(self._firmware_info),
            )
        self._printer.commands(
            [
                "M115",
                f"M881 P{_fmt(definition['conditioning_mm'])}",
            ],
            tags={"source:plugin", f"plugin:{PLUGIN_ID}"},
        )
        self._start_row(0)
        return flask.jsonify(ok=True, run_id=run_id, preview=preview)

    def _start_row(self, temperature_index: int) -> None:
        with self._lock:
            if self._state["status"] == "CANCELLING":
                return
            d = copy.deepcopy(self._state["definition"])
            temperature = float(self._state["temperatures"][temperature_index])
            campaign_id = self._campaign_id(temperature_index)
            self._state.update(
                status="RUNNING",
                phase="RUN_ROW",
                temperature_index=temperature_index,
                current_campaign_id=campaign_id,
                current_recovery_id=None,
                active_target_c=temperature,
            )
            self._last_keepalive_monotonic = time.monotonic()
        self._printer.commands(
            [
                f"M109 S{_fmt(temperature)}",
                self._build_row_command(d, campaign_id),
            ],
            tags={"source:plugin", f"plugin:{PLUGIN_ID}"},
        )
        self._publish()

    def _start_recovery(self, next_temperature_index: int) -> None:
        with self._lock:
            if self._state["status"] == "CANCELLING":
                return
            d = copy.deepcopy(self._state["definition"])
            next_temperature = float(
                self._state["temperatures"][next_temperature_index]
            )
            recovery_id = self._recovery_id(next_temperature_index - 1)
            self._state.update(
                status="RECOVERING",
                phase="RECOVERING",
                current_campaign_id=None,
                current_recovery_id=recovery_id,
                active_target_c=d["recovery_temp_c"],
            )
            self._last_keepalive_monotonic = time.monotonic()
        self._printer.commands(
            [
                # M880 reads this target as its return temperature, then raises
                # the hotend to recovery_temp_c itself.
                f"M104 S{_fmt(next_temperature)}",
                self._build_recovery_command(d, recovery_id),
            ],
            tags={"source:plugin", f"plugin:{PLUGIN_ID}"},
        )
        self._publish()

    def _api_cancel(self) -> flask.Response:
        with self._lock:
            if not self._is_active_locked():
                raise ValidationError("no analyzer run is active")
            campaign_id = self._state.get("current_campaign_id")
            recovery_id = self._state.get("current_recovery_id")
            self._state.update(status="CANCELLING", phase="CANCELLING")

        commands: List[str] = []
        if campaign_id is not None:
            commands.append(f"M872 Z J{campaign_id}")
        if recovery_id is not None:
            commands.append(f"M880 Z J{recovery_id}")
        commands.extend(["M879", "M104 S0"])
        self._printer.commands(
            commands,
            tags={"source:plugin", f"plugin:{PLUGIN_ID}"},
        )
        self._publish()
        return flask.jsonify(ok=True)

    def received_gcode(
        self,
        comm_instance: Any,
        line: str,
        *args: Any,
        **kwargs: Any,
    ) -> str:
        if line.startswith(TELEMETRY_PREFIXES):
            try:
                self._telemetry_queue.put_nowait(line)
            except queue.Full:
                self._logger.warning("Telemetry queue full; dropping analyzer line")
        return line

    def _worker_loop(self) -> None:
        while not self._stop_event.is_set():
            try:
                line = self._telemetry_queue.get(timeout=0.5)
            except queue.Empty:
                self._service_keepalive()
                continue
            try:
                self._process_telemetry(line)
            except Exception:
                self._logger.exception("Failed to process analyzer telemetry")
            finally:
                self._telemetry_queue.task_done()
            self._service_keepalive()

    def _service_keepalive(self) -> None:
        with self._lock:
            if self._state["status"] not in {"RUNNING", "RECOVERING"}:
                return
            target = self._state.get("active_target_c")
            interval = max(
                30,
                int(self._settings.get(["heater_keepalive_seconds"])),
            )
            due = (
                time.monotonic() - self._last_keepalive_monotonic >= interval
            )
        if target is not None and due and self._printer.is_operational():
            self._printer.commands(
                [f"M104 S{_fmt(float(target))}"],
                tags={
                    "source:plugin",
                    f"plugin:{PLUGIN_ID}",
                    "makeit-fa:keepalive",
                },
            )
            with self._lock:
                self._last_keepalive_monotonic = time.monotonic()

    def _process_telemetry(self, line: str) -> None:
        prefix, fields = _parse_fields(line)
        message: Dict[str, Any] = {
            "type": prefix,
            "fields": fields,
            "raw": line,
        }
        row_action: Optional[Tuple[str, Dict[str, str]]] = None
        recovery_action: Optional[Tuple[str, Dict[str, str]]] = None

        with self._lock:
            raw_lines: List[str] = self._state["raw"]
            raw_lines.append(line)
            if len(raw_lines) > 20000:
                del raw_lines[:5000]

            target = _float_field(fields, "target")
            if target is None:
                target = _float_field(fields, "temp_target")
            if target is not None and target > 0:
                self._state["active_target_c"] = target

            if (
                prefix == "FA2"
                and "result" in fields
                and self._state["phase"] == "RUN_ROW"
            ):
                point = self._point_from_fa2(fields, line)
                self._state["points"].append(point)
                message["point"] = point

            elif prefix == "FA7":
                campaign_id = _int_field(fields, "campaign_id")
                state = fields.get("state", "")
                if (
                    campaign_id == self._state.get("current_campaign_id")
                    and state in ROW_TERMINAL_STATES
                ):
                    row = dict(fields)
                    row["temperature_c"] = self._state["temperatures"][
                        self._state["temperature_index"]
                    ]
                    row["raw"] = line
                    self._state["rows"].append(row)
                    message["row"] = row
                    row_action = (state, dict(fields))

            elif prefix == "FA9":
                recovery_id = _int_field(fields, "recovery_id")
                state = fields.get("state", "")
                if (
                    recovery_id == self._state.get("current_recovery_id")
                    and state in RECOVERY_TERMINAL_STATES
                ):
                    recovery = dict(fields)
                    recovery["temperature_index"] = self._state["temperature_index"]
                    recovery["raw"] = line
                    self._state["recoveries"].append(recovery)
                    message["recovery"] = recovery
                    recovery_action = (state, dict(fields))

        self._plugin_manager.send_plugin_message(PLUGIN_ID, message)
        if row_action is not None:
            self._handle_row_terminal(*row_action)
        if recovery_action is not None:
            self._handle_recovery_terminal(*recovery_action)

    def _handle_row_terminal(
        self,
        state: str,
        fields: Dict[str, str],
    ) -> None:
        with self._lock:
            tag = fields.get("tag", "")
            self._checkpoint_locked()
            if self._state["status"] == "CANCELLING":
                self._finish_run_locked(
                    "ABORTED",
                    {"source": "FA7", "state": state, "tag": tag, "fields": fields},
                )
                send_off = True
                action: Optional[Tuple[str, int]] = None
            elif state in {"INVALID_TEMP", "ABORTED", "ERROR"}:
                status = state
                if tag == "heater_disabled":
                    status = "HEATER_DISABLED"
                self._finish_run_locked(
                    status,
                    {"source": "FA7", "state": state, "tag": tag, "fields": fields},
                )
                send_off = True
                action = None
            else:
                index = int(self._state["temperature_index"])
                next_index = index + 1
                if next_index >= len(self._state["temperatures"]):
                    self._finish_run_locked(
                        "COMPLETE",
                        {"source": "FA7", "state": state, "tag": tag, "fields": fields},
                    )
                    send_off = True
                    action = None
                else:
                    send_off = False
                    recovery_enabled = (
                        float(self._state["definition"]["recovery_temp_c"]) > 0.0
                    )
                    if state == "LIMIT_FOUND" and recovery_enabled:
                        action = ("recover", next_index)
                    else:
                        action = ("row", next_index)

        if send_off:
            self._printer.commands(
                ["M104 S0"],
                tags={"source:plugin", f"plugin:{PLUGIN_ID}"},
            )
            self._publish()
        elif action and action[0] == "recover":
            self._start_recovery(action[1])
        elif action:
            self._start_row(action[1])

    def _handle_recovery_terminal(
        self,
        state: str,
        fields: Dict[str, str],
    ) -> None:
        with self._lock:
            self._checkpoint_locked()
            if self._state["status"] == "CANCELLING":
                self._finish_run_locked(
                    "ABORTED",
                    {"source": "FA9", "state": state, "fields": fields},
                )
                next_index = None
            elif state == "COMPLETE":
                next_index = int(self._state["temperature_index"]) + 1
            else:
                self._finish_run_locked(
                    "RECOVERY_FAILED",
                    {"source": "FA9", "state": state, "fields": fields},
                )
                next_index = None

        if next_index is None:
            self._printer.commands(
                ["M104 S0"],
                tags={"source:plugin", f"plugin:{PLUGIN_ID}"},
            )
            self._publish()
        else:
            self._start_row(next_index)

    def _finish_run_locked(
        self,
        status: str,
        terminal: Dict[str, Any],
    ) -> None:
        self._state.update(
            status=status,
            phase="TERMINAL",
            terminal=terminal,
            finished_at=time.time(),
            active_target_c=None,
            current_campaign_id=None,
            current_recovery_id=None,
        )
        self._checkpoint_locked()

    def _point_from_fa2(
        self,
        fields: Dict[str, str],
        raw_line: str,
    ) -> Dict[str, Any]:
        feed = _float_field(fields, "feed_mm_min") or 0.0
        efficiency = _float_field(fields, "efficiency_pct") or 0.0
        temperature = _float_field(fields, "temp_target") or 0.0
        d = self._state.get("definition") or {}
        commanded_flow = _flow_mm3_s(
            feed,
            float(d.get("filament_diameter_mm", 1.75)),
        )
        result = fields.get("result", "UNKNOWN")
        tested_mm = _float_field(fields, "tested_mm") or 0.0
        requested_mm = _float_field(fields, "requested_mm") or 0.0
        classification = _classify_point(
            result=result,
            tested_mm=tested_mm,
            requested_mm=requested_mm,
            efficiency_pct=efficiency,
            accuracy_threshold_pct=float(
                d.get("accuracy_threshold_pct", 97.0)
            ),
            throughput_threshold_pct=float(
                d.get("throughput_threshold_pct", 85.0)
            ),
        )
        temp_min = _float_field(fields, "temp_min")
        return {
            "temperature_c": temperature,
            "feed_mm_min": feed,
            "commanded_flow_mm3_s": round(commanded_flow, 5),
            "tested_mm": tested_mm,
            "requested_mm": requested_mm,
            "expected_encoder_events": _float_field(fields, "expected_enc"),
            "actual_encoder_events": _int_field(fields, "actual_enc"),
            "efficiency_pct": efficiency,
            "delivered_flow_mm3_s": round(
                commanded_flow * efficiency / 100.0,
                5,
            ),
            "temperature_avg_c": _float_field(fields, "temp_avg"),
            "temperature_min_c": temp_min,
            "temperature_max_c": _float_field(fields, "temp_max"),
            "temperature_droop_c": (
                round(temperature - temp_min, 5)
                if temp_min is not None
                else None
            ),
            "heater_average_raw": _float_field(fields, "heater_avg_raw"),
            "result": result,
            "classification": classification,
            "result_generation": _int_field(fields, "gen"),
            "raw": raw_line,
        }

    def _checkpoint_locked(self) -> None:
        try:
            folder = self.get_plugin_data_folder()
            os.makedirs(folder, exist_ok=True)
            run_id = int(self._state.get("run_id") or int(time.time()))
            json_name = f"run-{run_id}.json"
            csv_name = f"run-{run_id}.csv"
            json_path = os.path.join(folder, json_name)
            csv_path = os.path.join(folder, csv_name)

            snapshot = copy.deepcopy(self._state)
            snapshot["saved_json"] = json_name
            snapshot["saved_csv"] = csv_name
            with open(json_path, "w", encoding="utf-8") as handle:
                json.dump(snapshot, handle, indent=2, sort_keys=True)

            with open(csv_path, "w", encoding="utf-8", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
                writer.writeheader()
                for point in snapshot.get("points", []):
                    writer.writerow({
                        field: point.get(field)
                        for field in CSV_FIELDS
                    })

            self._state["saved_json"] = json_name
            self._state["saved_csv"] = csv_name
        except Exception:
            self._logger.exception("Failed to save analyzer run checkpoint")

    def _public_state_locked(self) -> Dict[str, Any]:
        state = copy.deepcopy(self._state)
        state.pop("raw", None)
        return state

    def _publish(self) -> None:
        with self._lock:
            snapshot = self._public_state_locked()
        self._plugin_manager.send_plugin_message(
            PLUGIN_ID,
            {"type": "state", "state": snapshot},
        )

    def _validated_run_id(self, value: Any) -> int:
        try:
            run_id = int(str(value), 10)
        except (TypeError, ValueError) as exc:
            raise ValidationError("run_id must be an integer") from exc
        if run_id <= 0 or run_id > 0xFFFFFFFF:
            raise ValidationError("run_id is outside the supported range")
        return run_id

    def _saved_path(self, run_id: int, extension: str) -> str:
        return os.path.join(
            self.get_plugin_data_folder(),
            f"run-{run_id}.{extension}",
        )

    def _load_saved_run(self, run_id: int) -> Dict[str, Any]:
        path = self._saved_path(run_id, "json")
        if not os.path.isfile(path):
            flask.abort(404)
        with open(path, "r", encoding="utf-8") as handle:
            state = json.load(handle)
        state.pop("raw", None)
        return state

    def _list_saved_runs(self) -> List[Dict[str, Any]]:
        folder = self.get_plugin_data_folder()
        if not os.path.isdir(folder):
            return []
        runs: List[Dict[str, Any]] = []
        for name in os.listdir(folder):
            match = re.fullmatch(r"run-(\d+)\.json", name)
            if not match:
                continue
            run_id = int(match.group(1))
            path = os.path.join(folder, name)
            try:
                with open(path, "r", encoding="utf-8") as handle:
                    state = json.load(handle)
                definition = state.get("definition") or {}
                preview = state.get("preview") or {}
                runs.append({
                    "run_id": run_id,
                    "run_name": definition.get("run_name") or f"Run {run_id}",
                    "material_family": definition.get("material_family") or "Custom",
                    "material_name": definition.get("material_name") or "",
                    "status": state.get("status") or "UNKNOWN",
                    "started_at": state.get("started_at"),
                    "finished_at": state.get("finished_at"),
                    "point_count": len(state.get("points") or []),
                    "rows": preview.get("rows"),
                    "columns": preview.get("columns"),
                    "temperature_start_c": definition.get("temperature_start_c"),
                    "temperature_end_c": definition.get("temperature_end_c"),
                    "speed_start_mm_min": definition.get("speed_start_mm_min"),
                    "speed_end_mm_min": definition.get("speed_end_mm_min"),
                    "json_url": (
                        f"{flask.request.script_root}/api/plugin/{PLUGIN_ID}"
                        f"?action=json&run_id={run_id}"
                    ),
                    "csv_url": (
                        f"{flask.request.script_root}/api/plugin/{PLUGIN_ID}"
                        f"?action=csv&run_id={run_id}"
                    ),
                })
            except Exception:
                self._logger.exception("Failed to read saved analyzer run %s", path)
        runs.sort(key=lambda item: item.get("started_at") or 0, reverse=True)
        return runs

    def _delete_saved_run(self, run_id: int) -> None:
        found = False
        for extension in ("json", "csv"):
            path = self._saved_path(run_id, extension)
            if os.path.isfile(path):
                os.remove(path)
                found = True
        if not found:
            raise ValidationError(f"saved run {run_id} was not found")


__plugin_name__ = "MAKEiT Filament Analyzer"
__plugin_version__ = PLUGIN_VERSION
__plugin_description__ = (
    "Adjustable material-agnostic temperature/speed mapping and visualization"
)
__plugin_pythoncompat__ = ">=3.9,<4"


def __plugin_load__() -> None:
    global __plugin_implementation__
    __plugin_implementation__ = MakeItFilamentAnalyzerPlugin()

    global __plugin_hooks__
    __plugin_hooks__ = {
        "octoprint.comm.protocol.gcode.received": (
            __plugin_implementation__.received_gcode
        ),
        "octoprint.comm.protocol.firmware.info": (
            __plugin_implementation__.firmware_info
        ),
    }
