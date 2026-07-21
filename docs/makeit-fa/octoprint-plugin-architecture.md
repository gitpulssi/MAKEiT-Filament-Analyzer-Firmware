# OctoPrint plugin architecture for the MAKEiT filament test bench

## Product boundary

The test bench is material-agnostic. Marlin is the deterministic measurement and
safety engine. OctoPrint is the experiment controller, persistent data store, and
visualization layer.

The firmware must not encode PLA-, PETG-, TPU-, nylon-, CF-, or GF-specific test
windows. Temperature, filament feed speed, grid resolution, point length,
conditioning length, tolerances, and thresholds are runtime inputs.

## Responsibilities

### Marlin

- Execute bounded segmented E motion.
- Count encoder events and executed motion.
- Measure whole-point and rolling efficiency.
- Track temperature statistics and pulse gaps.
- Enforce graceful abort and bounded committed distance.
- Report parseable `FA1`, `FA2`, `FA3`, `FA4`, `FA7`, `FA8`, `FA8ROW`, `FA9`,
  and `FA10` telemetry.
- Keep `P` as the printing-accuracy threshold and `R` as the hard throughput /
  rolling-safety threshold.

### OctoPrint plugin

- Let the user choose all temperature and speed grid limits and steps.
- Validate the requested grid before starting.
- Estimate test duration and filament consumption.
- Run one temperature row at a time with `M872` so the host retains control.
- Refresh the active heater target periodically while a bench run is active, so
  unrelated host idle-heater timers cannot terminate a supervised test.
- Parse firmware telemetry without blocking OctoPrint's serial receive loop.
- Persist every point and run as JSON and CSV.
- Resume or safely terminate interrupted runs.
- Render a temperature x speed heatmap and derived flow curves.

## User-adjustable experiment definition

Every run stores the following immutable definition:

```text
run name
material family and free-form material name
manufacturer / product / color / lot / notes
filament diameter
nozzle diameter and nozzle material
extruder / hotend identifier
encoder calibration

temperature start / end / step
filament-feed start / end / step
conditioning length
measured length
thermal settle time
printing-accuracy threshold P
hard throughput threshold R
temperature tolerance
rolling window length and confirmation count
pulse-gap settings
recovery temperature and recovery point settings
```

Material presets are convenience templates only. Every field remains editable.

## Grid validation

The plugin expands both inclusive ranges before sending any G-code.

```text
temperatures = inclusive_grid(temp_start, temp_end, temp_step)
speeds       = inclusive_grid(speed_start, speed_end, speed_step)
points       = len(temperatures) x len(speeds)
```

Reject a run when:

- a step is zero or points away from the end value;
- a range contains a non-finite value;
- a temperature is outside the configured machine safety limits;
- the speed exceeds the configured machine/extruder limit;
- a point length or conditioning length is invalid;
- the requested grid exceeds a configurable point-count ceiling;
- the estimated filament consumption exceeds the user-confirmation threshold;
- OctoPrint is printing, paused, disconnected, or not operational.

The UI shows rows, columns, total points, estimated filament use, and estimated
minimum duration before enabling Start.

## Execution model

The plugin should orchestrate one fixed-temperature `M872` row at a time instead
of relying exclusively on a long `M870` envelope.

```text
SET_TEMP -> WAIT_TEMP -> RUN_ROW -> OPTIONAL_RECOVERY -> NEXT_TEMP
```

For each temperature:

1. Send `M881 P<conditioning_mm>`.
2. Send `M109 S<temperature>`.
3. Send one `M872` command using the selected speed range and resolution.
4. Parse all `FA2` point records and `FA7` campaign records.
5. On a hard limit, optionally run `M880` recovery before the next row.
6. Persist the completed row before continuing.

The plugin periodically resends `M104 S<active_target>` while a supervised run is
active. This is a same-target keepalive, not a temperature change.

## Data model

Each measured point is stored independently:

```json
{
  "temperature_c": 220.0,
  "feed_mm_min": 625.0,
  "commanded_flow_mm3_s": 25.06,
  "tested_mm": 200.0,
  "expected_encoder_events": 137.0,
  "actual_encoder_events": 122,
  "efficiency_pct": 89.05,
  "delivered_flow_mm3_s": 22.31,
  "temperature_avg_c": 219.1,
  "temperature_min_c": 217.4,
  "temperature_max_c": 220.2,
  "result": "LOW_FEED",
  "accuracy_class": "BELOW_ACCURACY_ABOVE_THROUGHPUT",
  "point_crc": 0
}
```

The original telemetry line is retained for auditability.

## Visualization

The primary graph is a heatmap:

```text
X axis: temperature (C)
Y axis: filament feed speed (mm/min) or commanded flow (mm3/s)
cell color: selected metric
```

Selectable cell metrics:

- measured efficiency percent;
- delivered volumetric flow;
- temperature droop;
- pass class;
- rolling-window minimum efficiency.

Overlays:

- 97% printing-accuracy boundary;
- hard throughput boundary;
- untested cells;
- invalid-temperature cells;
- interrupted points.

Secondary charts:

- delivered flow versus commanded flow at each temperature;
- maximum accurate flow versus temperature;
- maximum hard throughput versus temperature;
- temperature droop versus flow.

No interpolation is required for raw-data mode. Optional contour/smoothing must be
clearly labeled and never replace the measured cells.

## Exports

Every run can export:

- complete JSON including definition, firmware version, machine metadata, raw
  telemetry, points, rows, CRCs, and timestamps;
- tidy CSV with one row per temperature/speed point;
- PNG/SVG graph export from the browser;
- a compact material profile containing conservative slicer flow limits.

## Initial OctoPrint plugin mixins and hooks

Use:

- `StartupPlugin`
- `SettingsPlugin`
- `TemplatePlugin`
- `AssetPlugin`
- `SimpleApiPlugin`
- `EventHandlerPlugin`
- `octoprint.comm.protocol.gcode.received`

The receive hook only parses and queues matching telemetry. It must return the
original line immediately. A background worker updates run state, writes files,
and sends plugin messages to the browser.

## Development phases

1. Installable plugin shell, adjustable grid form, validation, and G-code preview.
2. Telemetry parser and live point table.
3. Row-by-row automatic runner with cancel and heater keepalive.
4. Persistent JSON/CSV datasets and run browser.
5. Heatmap and derived boundary plots.
6. Recovery orchestration, interrupted-run handling, and resume support.
7. Material preset templates and slicer-profile export.
