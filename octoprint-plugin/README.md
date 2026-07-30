# OctoPrint MAKEiT Filament Analyzer

This plugin is the experiment-control, storage, and visualization layer for the
MAKEiT filament analyzer Marlin firmware.

It is material-agnostic. PLA, TPU, PET, PETG, ABS, ASA, nylon, nylon-CF/GF,
PC, PEEK, and custom materials all use the same measurement engine. Material
presets are convenience starting points only. Every test window remains editable.

## Version 0.2.4

The bench controller includes:

- editable material, spool, printer, extruder, filament, and nozzle metadata;
- adjustable temperature start, end, and step;
- adjustable filament-feed start, end, and step;
- adjustable conditioning and measured lengths;
- independent printing-accuracy `P` and hard-throughput `R` thresholds;
- adjustable recovery temperature, purge, and validation settings;
- preflight grid expansion and validation;
- point-count, maximum filament-use, minimum-duration, and command-length estimates;
- one-temperature-row-at-a-time `M872` orchestration;
- automatic `M880` recovery after a hard or invalid-temperature row boundary;
- same-target heater keepalive during a supervised run;
- non-blocking `FA*` telemetry processing;
- live point table and temperature x speed heatmap;
- accurate-flow and hard-throughput boundary graph versus temperature;
- recommended printing-speed heatmap;
- JSON and tidy CSV checkpoints;
- saved-run browser;
- interrupted-run checkpointing.

### Version history

0.2.1 converts `FA7 tag=conditioning_limit` into a synthetic hard-failure point
so the failed temperature/speed cell appears even when measurement never starts.

0.2.2 resolves template and asset folders from the installed package, names the
tab template directly, and removes a duplicate tab wrapper ID.

0.2.3 treats the user-selected `D`-band `INVALID_TEMP` result as a measured graph
boundary. The controller can recover and continue at the next temperature.

0.2.4 adds a recommended printing-speed heatmap based on measured delivered flow,
user-entered line width, layer height, and slicer safety factor.

## Install or upgrade on this OctoPi system

The OctoPrint service uses:

```text
/opt/octopi/oprint/bin/octoprint
```

Install into that same Python environment:

```bash
cd ~/MAKEiT-Filament-Analyzer-Firmware
git pull --ff-only
/opt/octopi/oprint/bin/python -m pip install --force-reinstall -e ./octoprint-plugin
sudo systemctl restart octoprint
```

Confirm the installed package:

```bash
/opt/octopi/oprint/bin/python -m pip show OctoPrint-MAKEiT-Filament-Analyzer
```

Expected version:

```text
0.2.4
```

A browser hard refresh may be needed after JavaScript or CSS changes.

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

The plugin converts filament feed to volumetric flow using filament diameter.
Nozzle diameter and nozzle material are stored because they affect the physical
result, but nozzle diameter is not part of the filament-volume conversion.

## Recommended printing-speed heatmap

Printing speed cannot be calculated from nozzle diameter alone. It also depends
on the intended line width and layer height.

The graph uses:

```text
recommended print speed, mm/s
  = delivered flow, mm3/s
  x safety factor / 100
  / (line width, mm x layer height, mm)
```

The `Use nozzle defaults` button sets:

```text
line width = nozzle diameter
layer height = nozzle diameter / 2
```

The default safety factor is 90 percent. Set it to 100 percent to display the
direct measured-flow equivalent.

Graph colors retain the measurement classification:

```text
Green  = met P
Amber  = below P but above R
Red    = hard throughput failure
Purple = invalid temperature
Gray   = untested
```

For a 0.6 mm line width, 0.3 mm layer height, 90 percent safety factor, and
20 mm3/s delivered flow:

```text
20 x 0.90 / (0.6 x 0.3) = 100 mm/s
```

## Execution model

```text
M881 conditioning configuration
  -> M109 selected temperature
  -> M872 selected speed grid
  -> save JSON and CSV checkpoint
  -> optional M880 recovery
  -> next selected temperature
```

Marlin remains responsible for bounded segmented extrusion, encoder counting,
temperature validity, rolling and pulse-gap monitoring, exactly-once point
execution, and graceful abort.

## Data files

Runs are checkpointed under:

```text
~/.octoprint/data/makeit_filament_analyzer/
```

Each run produces:

```text
run-<id>.json
run-<id>.csv
```

The JSON preserves the test definition, firmware information, points, row
summaries, recovery summaries, telemetry, and terminal state. The CSV contains
one row per measured or conditioning-limit temperature/speed point.

## Unit tests

```bash
cd octoprint-plugin
python -m unittest discover -s tests -v
```

## Current production checklist

1. Install 0.2.4 and confirm both heatmaps render.
2. Load a saved run and confirm nozzle-based print geometry is populated.
3. Change line width, layer height, and safety factor and confirm cell speeds update.
4. Validate a 2 x 2 grid.
5. Test Stop during thermal waiting, conditioning, measurement, and recovery.
6. Test disconnect interruption checkpointing.
7. Validate cooling-gated automatic PSU power-off before unattended overnight use.
