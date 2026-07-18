# MAKEiT Filament Analyzer — Phase 0 / 1 / 2 / 3

The firmware is split into four separately testable layers:

- **Phase 0:** encoder counting and calibration with `M875`.
- **Phase 1:** open-loop segmented E-only motion with `M874`.
- **Phase 2:** one evaluated extrusion point with `M873`.
- **Phase 3:** optional rolling feed-loss detection and graceful segmented stop, also through `M873`.

## Hardware

```text
Controller:
  BTT SKR Pro V1.2

Encoder input:
  PG5

Telemetry:
  SKR Pro TFT TX3 -> Raspberry Pi RX2
  SKR Pro GND     -> Raspberry Pi GND
  SKR Pro TFT RX3 disconnected during bring-up
  Pi TX2          disconnected during bring-up
```

The telemetry link is one-way. OctoPrint remains the only commander on the normal printer serial connection.

## Integration

Run this from the repository root after pulling the analyzer branch:

```bash
python tools/makeit-fa/integrate_phase0.py
```

The script:

- disables `SERIAL_PORT_3 3` so analyzer telemetry owns `Serial3`;
- appends the analyzer configuration block to `Configuration_adv.h`;
- adds `makeit_fa_phase0.init()` to `setup()`;
- adds `makeit_fa_phase0.idle()` to `idle()`;
- declares and dispatches `M873`, `M874`, and `M875`;
- removes obsolete analyzer `M876` hooks, because Marlin already uses `M876` for host prompts.

## Phase 0 — M875 encoder diagnostics

```text
M875       report encoder count and analyzer state
M875 R     reset encoder count
M875 S1    enable FA0 telemetry stream on Serial3
M875 S0    disable FA0 telemetry stream
M875 I200  set telemetry interval to 200 ms
```

Example response:

```text
FA0: enc=68 last_edge_us=123456 pin=0 stream=0 interval_ms=200 mode=RISING poll=1 seg=0 tp=0 abort=0
```

### Locked encoder calibration

```text
physical filament movement: 400 mm
encoder events:             274
count mode:                 RISING
count method:               polling fallback
encoder events/mm:          0.685
encoder mm/event:           1.460
selected segment length:    0.35 mm
```

The feeder E-steps were calibrated afterward. With calibrated E-steps, both normal `G1 E100` and segmented `M874 L100` produced 68–69 encoder events at `F100` and `F500`.

## Phase 1 — M874 segmented-feed diagnostic

```gcode
M874 L100 F500 S0.35 B2 I250
```

Parameters:

```text
L  total commanded filament length in mm. Default 10.
F  filament feed rate in mm/min. Default 100.
S  segment length in mm. Default 0.35; clamped to 0.05..0.35.
B  maximum in-flight planner blocks. Default 2; clamped to 1..2.
I  FA1 telemetry interval in ms. Default 250.
```

`M874` is non-blocking and open-loop. It returns immediately, enqueues segments from Marlin `idle()`, and later emits:

```text
FA1: done total_mm=100 segment_mm=0.35 feed_mm_min=500 max_blocks=2 enqueued=286 enc=68
```

It does not decide pass or fail.

## Phase 2 — M873 evaluated extrusion point

Heat and stabilize the hotend first:

```gcode
M109 S240
```

Then start one evaluated point:

```gcode
M873 L100 F500 S0.35 B2 I250 C0.685 P95 D2
```

Core parameters:

```text
L  test filament length in mm. Default 100; clamped to 20..500.
F  filament feed rate in mm/min. Default 100.
S  segment length in mm. Default 0.35; clamped to 0.05..0.35.
B  maximum in-flight planner blocks. Default 2; clamped to 1..2.
I  FA1 telemetry interval in ms. Default 250.
C  calibrated encoder events per physical filament mm. Default 0.685.
P  minimum terminal passing feed efficiency percent. Default 95.
D  maximum allowed deviation from the current hotend target in °C. Default 2.
Q  query the active point or latest stored terminal result.
```

The command requires a non-zero hotend target, a hotend warm enough for normal Marlin extrusion, and current temperature within `D` degrees of the target.

It resets the encoder automatically and samples:

```text
encoder events
temperature minimum / maximum / average
Marlin heater-power output
```

Calculations:

```text
expected_events = tested_mm × C
efficiency_pct  = actual_events / expected_events × 100
```

Terminal classifications:

```text
PASS          efficiency >= P and temperature remained within D
LOW_FEED      efficiency < P, or Phase-3 monitor requested a stop
INVALID_TEMP  temperature left the allowed band during the point
ERROR         no valid target, hotend too cold, or motion could not start
```

### Bench-validated Phase-2 result

```text
FA2: result=PASS gen=3 duration_ms=14581 requested_mm=100 tested_mm=100
     feed_mm_min=500 expected_enc=68.50 actual_enc=67
     efficiency_pct=97.81 pass_pct=95.00
     temp_target=240.00 temp_avg=239.57 temp_min=238.36 temp_max=240.21
     heater_avg_raw=61.36 auto_stop=0 aborted=0
```

The repeatable result query was also validated:

```gcode
M873 Q
```

It returned the same stored generation and result without re-extruding.

## Phase 3 — rolling feed-loss monitor and graceful stop

Phase 3 is optional and disabled unless `A1` is supplied.

Example normal monitored point:

```gcode
M109 S240
M873 L120 F500 S0.35 B2 I250 C0.685 P95 D2 A1 W20 R85 K2
```

Additional parameters:

```text
A  enable rolling monitor and graceful auto-stop. Default 0.
W  rolling monitor window length in commanded mm. Default 20; range 5..100.
R  minimum rolling-window efficiency percent. Default 85.
K  consecutive failing windows required before stopping. Default 2; range 1..5.
```

The monitor estimates completed filament distance by subtracting the bounded planner horizon from the enqueued distance. With `S0.35 B2`, the estimate can lag actual execution by no more than the approximately 0.70 mm committed-distance cap.

At each completed window it reports:

```text
FA3: window gen=4 from_mm=20.00 to_mm=40.10 actual_enc=14 expected_enc=13.77
     efficiency_pct=101.67 threshold_pct=85.00 failed_windows=0 confirm=2
```

After `K` consecutive windows below `R`, it stops adding new segments and lets only the already committed blocks drain:

```text
FA3: stop_requested gen=4 cmd_mm=61.25 last_efficiency_pct=12.40 failed_windows=2
```

The terminal `FA2` result then includes:

```text
requested_mm
tested_mm
auto_stop
aborted
abort_cmd_mm
monitor_window_mm
monitor_pct
monitor_last_eff
failed_windows
```

A monitor-triggered stop is classified `LOW_FEED` unless the temperature validity check takes precedence and classifies the point `INVALID_TEMP`.

### First Phase-3 validation sequence

First prove there are no false stops under clean feed:

```gcode
M109 S240
M873 L120 F500 S0.35 B2 I250 C0.685 P95 D2 A1 W20 R85 K2
M873 Q
```

Expected:

```text
multiple FA3 window records
failed_windows stays 0
FA2 result=PASS
aborted=0
```

Only after that clean run should a controlled feed-loss test be induced. Use a supervised, low-risk setup and keep `M112` available. The expected behavior is two low-efficiency window reports, `FA3: stop_requested`, a short drain of at most the committed horizon, and a stored `LOW_FEED` result with `aborted=1`.

## Dedicated telemetry

`FA1` records include hotend temperature, target, and heater-power value:

```text
FA1,seq=4,ms=123456,tag=run,cmd_mm=4.550,total_mm=100.000,blocks=2,max_blocks=2,enc=3,pin=1,temp=239.80,target=240,heater=118
```

## Safety and current limits

Marlin thermal runaway, maximum-temperature protection, and cold-extrusion protection remain active.

Phase 3 uses a graceful segmented stop: it stops adding segments and drains only the bounded in-flight planner horizon. It is not yet the final emergency-parser abort path.

Not implemented yet:

- pulse-gap immediate failure detection;
- out-of-band `M879` graceful abort;
- automatic recovery;
- automatic temperature / speed campaign;
- TMC load classification;
- `M877` / `M878` production transaction layer.

The next code layer after Phase-3 validation is pulse-gap detection plus an out-of-band graceful abort path.
