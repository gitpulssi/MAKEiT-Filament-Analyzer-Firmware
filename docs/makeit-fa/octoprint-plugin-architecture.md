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
- Count filament encoder events and executed motion.
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
- Estimate test duration and maximum filament consumption.
- Run one temperature row at a time with `M872` so the host retains control.
- Refresh the active heater target periodically while a bench run is active, so
  unrelated host idle-heater timers cannot terminate a supervised test.
- Parse firmware telemetry without blocking OctoPrint's serial receive loop.
- Persist every completed row as JSON and tidy CSV.
- Render a temperature × speed heatmap and derived boundary curves.
- Browse, reopen, download, and delete saved runs.
- Mark active tests interrupted if OctoPrint disconnects or reports a connection error.

## User-adjustable experiment definition

Every run stores the following immutable definition:

```text
run name
material family and free-form material name
manufacturer / product / color / lot / notes
filament diameter
nozzle diameter and nozzle material
extruder / hotend identifier
printer / test-bench identifier
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
recovery temperature
recovery prime length and feed
recovery validation length, feed, and accuracy threshold
```

Material presets are convenience templates only. Every field remains editable.

## Grid validation

The plugin expands both inclusive ranges before sending any G-code.

```text
temperatures = inclusive_grid(temp_start, temp_end, temp_step)
speeds       = inclusive_grid(speed_start, speed_end, speed_step)
points       = len(temperatures) × len(speeds)
```

Reject a run when:

- a step is zero or points away from the end value;
- a range contains a non-finite value;
- a temperature is outside the configured machine safety limits;
- the speed exceeds the configured machine/extruder limit;
- conditioning is neither zero nor at least the firmware's 10 mm minimum;
- measured length cannot contain the requested rolling windows;
- the requested grid exceeds a configurable point-count ceiling;
- a generated M872 or M880 command exceeds Marlin's 191-character payload limit;
- OctoPrint is printing, paused, disconnected, or not operational.

The UI shows rows, columns, maximum points, estimated maximum filament use,
estimated minimum duration, and command lengths before enabling Start.

## Execution model

The plugin orchestrates one fixed-temperature `M872` row at a time instead of
relying on one long `M870` envelope. This avoids the firmware's 24-row retained
result array and supports up to the configurable plugin limit.

```text
SET_TEMP -> WAIT_TEMP -> RUN_ROW -> CHECKPOINT -> OPTIONAL_RECOVERY -> NEXT_TEMP
```

For each temperature:

1. Send `M881 P<conditioning_mm>` once for the run.
2. Send `M109 S<temperature>`.
3. Send one `M872` command using the selected speed range and resolution.
4. Parse all `FA2` point records and the terminal `FA7` campaign record.
5. Write JSON and CSV checkpoints.
6. On a hard limit, optionally run `M880` recovery before the next row.
7. Advance to the next user-selected temperature.

The plugin periodically resends `M104 S<active_target>` while a supervised run is
active. This is a same-target keepalive, not a temperature change. Keepalive timing
is reset at every row and recovery transition so it cannot overwrite the return
target used by M880.

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
  "temperature_droop_c": 2.6,
  "result": "LOW_FEED",
  "classification": "BELOW_ACCURACY_ABOVE_THROUGHPUT"
}
```

The original telemetry line is retained in JSON for auditability. CSV contains one
row per measured temperature/speed point.

## Visualization

The primary graph is a measured-cell heatmap:

```text
X axis: temperature (C)
Y axis: filament feed speed (mm/min)
cell color: selected metric
```

Selectable cell metrics:

- measured efficiency percent;
- delivered volumetric flow;
- commanded volumetric flow;
- minimum measured temperature;
- temperature droop.

Classification colors identify accurate, below-accuracy/above-throughput, hard
failure, invalid-temperature, aborted, and untested cells.

The secondary graph plots versus temperature:

- highest feed/flow that remained at or above P;
- highest feed/flow accepted before the hard R boundary.

No interpolation is required for raw-data mode. Optional contours or smoothing
must be clearly labeled and never replace measured cells.

## Persistence and exports

Every active run is checkpointed after each completed row and recovery. A terminal
run is checkpointed again with its final state.

```text
~/.octoprint/data/makeit_filament_analyzer/run-<id>.json
~/.octoprint/data/makeit_filament_analyzer/run-<id>.csv
```

The UI can:

- list saved runs;
- reopen a saved run and redraw its graphs;
- download JSON;
- download CSV;
- delete both files for a run.

## OctoPrint integration

Version 0.2.0 uses:

- `StartupPlugin`;
- `ShutdownPlugin`;
- `SettingsPlugin`;
- `TemplatePlugin`;
- `AssetPlugin`;
- `SimpleApiPlugin`;
- `EventHandlerPlugin`;
- `octoprint.comm.protocol.gcode.received`;
- `octoprint.comm.protocol.firmware.info`.

The receive hook only checks the prefix and queues matching telemetry. It returns
the original line immediately. A background worker parses telemetry, updates run
state, writes files, and sends plugin messages to the browser.

## Validation status

The v0.2.0 package has passed:

- Python syntax compilation;
- JavaScript syntax checking;
- wheel construction;
- 11 unit tests covering grid expansion, telemetry parsing, volumetric conversion,
  and dual-threshold point classification.

It still requires integration testing on the target OctoPrint instance, including
a 2 × 2 smoke test, recovery transition, cancellation in each active phase, and
connection-interruption checkpointing.
