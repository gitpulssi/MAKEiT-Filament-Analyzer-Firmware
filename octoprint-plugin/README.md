# OctoPrint MAKEiT Filament Analyzer

This plugin is the experiment-control, storage, and visualization layer for the
MAKEiT filament analyzer Marlin firmware.

It is material-agnostic. PLA, TPU, PET, PETG, ABS, ASA, nylon, nylon-CF/GF,
PC, PEEK, and custom materials all use the same measurement engine. Material
presets are only convenience starting points; every test window remains editable.

## Version 0.2.2

The usable bench-controller release includes:

- editable material, spool, printer, extruder, filament, and nozzle metadata;
- adjustable temperature start, end, and step;
- adjustable filament-feed start, end, and step;
- adjustable conditioning and measured lengths;
- independent printing-accuracy (`P`) and hard-throughput (`R`) thresholds;
- adjustable recovery temperature, purge, and validation settings;
- preflight grid expansion and validation;
- point-count, maximum filament-use, minimum-duration, and command-length estimates;
- one-temperature-row-at-a-time `M872` orchestration;
- automatic `M880` recovery after a hard row limit;
- same-target heater keepalive during a supervised run;
- non-blocking `FA*` telemetry queuing from OctoPrint's serial receive hook;
- live measured-point table;
- live temperature x speed heatmap;
- accurate-flow and hard-throughput boundary graph versus temperature;
- JSON and tidy CSV checkpoints after every completed row;
- saved-run browser with open, CSV download, JSON download, and delete;
- interruption checkpointing on OctoPrint disconnect/error events;
- pure-Python unit tests for grid expansion, telemetry parsing, flow conversion,
  point classification, and UI resource discovery.

Version 0.2.1 converts an `FA7 tag=conditioning_limit` terminal record into a
synthetic hard-failure point. The heatmap, JSON, and CSV therefore show the
failed temperature/speed cell even when firmware correctly stops during the
conditioning feed before an `FA2` measured point exists. The record includes:

```text
result=CONDITIONING_LIMIT
classification=HARD_THROUGHPUT_FAIL
failure_stage=CONDITIONING
measurement_started=false
```

Version 0.2.2 hardens OctoPrint UI loading:

- the template and asset folders are resolved from the installed package path;
- the tab template filename is supplied directly to OctoPrint;
- the template no longer duplicates OctoPrint's generated tab wrapper ID;
- startup logging prints the resolved template and asset folders;
- the template contains `data-makeit-fa-ui-version="0.2.2"` for browser checks.

The row-by-row runner avoids the firmware `M870` limit of 24 temperature rows.
The plugin defaults allow up to 100 temperature values, 100 speed values, and
2,000 total measured points. These are configurable safety ceilings, not
material constants.

## Install for development

Use the Python environment that runs the OctoPrint service. On this project's
OctoPi installation, systemd runs:

```text
/opt/octopi/oprint/bin/octoprint
```

Install with:

```bash
cd ~/MAKEiT-Filament-Analyzer-Firmware
git pull --ff-only
/opt/octopi/oprint/bin/python -m pip install -e ./octoprint-plugin
sudo systemctl restart octoprint
```

Confirm the installed package:

```bash
/opt/octopi/oprint/bin/python -m pip show OctoPrint-MAKEiT-Filament-Analyzer
```

The plugin appears as the **Filament Analyzer** tab.

## Upgrade an editable installation

```bash
cd ~/MAKEiT-Filament-Analyzer-Firmware
git pull --ff-only
/opt/octopi/oprint/bin/python -m pip install -e ./octoprint-plugin
sudo systemctl restart octoprint
```

A browser hard refresh may be needed after JavaScript or CSS changes.

## Blank-tab diagnostics

Check startup and resolved resource paths:

```bash
sudo journalctl -u octoprint --since "2 minutes ago" --no-pager | \
  grep -iE "makeit|filament analyzer|traceback|failed to load|error"
```

Expected startup text includes:

```text
MAKEiT Filament Analyzer controller 0.2.2 started
```

Check that OctoPrint serves the static JavaScript:

```bash
curl -s -o /dev/null -w "%{http_code}\n" \
  http://127.0.0.1:5000/plugin/makeit_filament_analyzer/static/js/makeit_filament_analyzer.js
```

Expected HTTP status:

```text
200
```

Check whether the rendered OctoPrint page contains the tab and UI marker:

```bash
curl -s http://127.0.0.1:5000/ | \
  grep -E "tab_plugin_makeit_filament_analyzer|makeit-fa-ui-version"
```

A logged-in browser may still be required to inspect the final rendered page if
OctoPrint redirects unauthenticated requests to its login view.

## Test definition

A run can use any user-selected window, for example:

```text
Material: TPU 95A
Temperature: 205 to 245 C, step 2 C
Feed speed: 50 to 500 mm/min, step 25 mm/min
Conditioning: 50 mm
Measurement: 200 mm
Accuracy P: 97%
Hard throughput R: 85%
Recovery: 245 C, 50 mm prime at F100, 100 mm validation at F100
```

The plugin converts filament feed to volumetric flow using the entered filament
diameter. Nozzle diameter and nozzle material are stored because they affect
backpressure and measured capability, but they are not part of the filament
volume conversion.

## Execution model

The plugin owns the outer temperature loop:

```text
M881 conditioning configuration
  -> M109 selected row temperature
  -> M872 selected speed grid
  -> save JSON and CSV checkpoint
  -> optional M880 recovery after a hard limit
  -> next selected temperature
```

Marlin remains responsible for bounded segmented extrusion, encoder counting,
temperature validity, rolling and pulse-gap monitoring, exactly-once point
execution, and graceful abort.

## Graphs

The primary graph is a measured-cell heatmap:

```text
X = temperature
Y = filament feed speed
Color = selected metric
```

Metrics include:

- efficiency percent;
- delivered volumetric flow;
- commanded volumetric flow;
- minimum measured temperature;
- temperature droop.

The secondary boundary graph plots:

- highest feed/flow that remained at or above `P`;
- highest feed/flow accepted before the hard `R` boundary.

No interpolation is applied to the raw measured cells.

## Data files

Runs are checkpointed under OctoPrint's plugin data directory, usually:

```text
~/.octoprint/data/makeit_filament_analyzer/
```

Each run produces:

```text
run-<id>.json
run-<id>.csv
```

The JSON preserves the immutable test definition, firmware information,
telemetry-derived points, completed row summaries, recovery summaries, raw
telemetry, and terminal state. The CSV contains one row per measured or
conditioning-limit temperature/speed point.

## Unit tests

The tests do not require a running OctoPrint server:

```bash
cd octoprint-plugin
python -m unittest discover -s tests -v
```

## Production-validation checklist

Before unattended use:

1. Install the plugin and confirm the tab and saved-run list load.
2. Validate a 2 x 2 grid without starting.
3. Run a two-temperature, two-speed smoke test.
4. Confirm heatmap cells and both saved files.
5. Force a hard row limit and confirm `M880` recovery.
6. Test Stop during `WAIT_TEMP`, `CONDITIONING`, `RUNNING_POINT`, and `RECOVERING`.
7. Disconnect OctoPrint during a short run and confirm an `INTERRUPTED` checkpoint.
