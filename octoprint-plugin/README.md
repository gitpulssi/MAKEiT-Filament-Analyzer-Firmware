# OctoPrint MAKEiT Filament Analyzer

This plugin is the experiment-control and visualization layer for the MAKEiT
filament analyzer Marlin firmware.

It is deliberately material-agnostic. PLA, TPU, PET, PETG, ABS, ASA, nylon,
nylon-CF/GF, PC, and custom materials use the same measurement engine. Material
presets are convenience starting points only; every test window remains editable.

## Current scaffold

Version `0.1.0` includes:

- editable material and hardware metadata;
- adjustable temperature start, end, and step;
- adjustable filament-feed start, end, and step;
- adjustable conditioning and measured lengths;
- independent printing-accuracy (`P`) and hard-throughput (`R`) thresholds;
- preflight grid expansion and validation;
- point-count, filament-use, minimum-duration, and G-code-length estimates;
- one-temperature-row-at-a-time `M872` orchestration;
- automatic `M880` recovery after a hard row limit;
- same-target heater keepalive during a supervised run;
- non-blocking `FA*` telemetry queuing from OctoPrint's serial receive hook;
- live point table and temperature x speed heatmap;
- JSON persistence in the plugin data directory.

The row-by-row runner avoids the firmware `M870` limit of 24 temperature rows.
The plugin default allows up to 100 temperature values, 100 speed values, and
1000 total measured points. These ceilings are plugin settings, not material
constants.

## Install for development

On the Raspberry Pi running OctoPrint:

```bash
cd ~/MAKEiT-Filament-Analyzer-Firmware/octoprint-plugin
~/oprint/bin/pip install -e .
sudo systemctl restart octoprint
```

If OctoPrint is not installed in `~/oprint`, use the `pip` executable belonging
to the Python environment that runs OctoPrint.

The plugin appears as the **Filament Analyzer** tab.

## Test definition

A run is defined by user-selected values such as:

```text
Temperature: 180 to 260 C, step 2 C
Feed speed: 100 to 900 mm/min, step 25 mm/min
Conditioning: 50 mm
Measurement: 200 mm
Accuracy P: 97%
Hard throughput R: 85%
```

The plugin converts filament feed to volumetric flow using the entered filament
diameter. Nozzle diameter is stored as test metadata because it changes the
physical result, but it is not part of that geometric conversion.

## Graph

The primary graph is a measured-cell heatmap:

```text
X = temperature
Y = filament feed speed
Color = selected metric
```

Available metrics in the first scaffold:

- efficiency percent;
- delivered volumetric flow;
- commanded volumetric flow;
- minimum measured temperature.

Pass classes are shown as accurate, below-accuracy/above-throughput, hard
throughput failure, invalid temperature, or aborted.

## Safety model

The plugin does not replace firmware safety. Marlin remains responsible for:

- bounded segmented extrusion;
- maximum committed distance;
- encoder and pulse-gap monitoring;
- temperature validity;
- graceful abort;
- exactly-once point execution.

The plugin refuses to start while OctoPrint is disconnected, printing, or
paused. Stop sends the active row/recovery cancellation commands, `M879`, and
`M104 S0`.

## Data files

Completed runs are written as JSON under OctoPrint's plugin data directory,
usually beneath:

```text
~/.octoprint/data/makeit_filament_analyzer/
```

CSV export, run browsing, graph image export, and resumable interrupted runs are
planned next.

## Development status

This is the initial installable scaffold. Before production use it still needs:

1. plugin installation on the target OctoPrint instance;
2. UI/API smoke testing;
3. one short two-temperature test;
4. recovery transition testing;
5. cancellation testing in WAIT_TEMP, CONDITIONING, RUNNING_POINT, and RECOVERING;
6. CSV export and saved-run browser;
7. automated parser/grid tests.
