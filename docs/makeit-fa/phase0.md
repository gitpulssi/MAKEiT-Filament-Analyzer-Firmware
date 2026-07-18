# MAKEiT Filament Analyzer — Phase 0 / Phase 1 / Phase 2

The firmware is now split into three usable layers:

- **Phase 0:** encoder counting and calibration with `M875`.
- **Phase 1:** open-loop segmented E-only motion with `M874`.
- **Phase 2:** one evaluated extrusion point with `M873`.

Phase 2 is the first command that converts raw encoder movement into an automatic terminal result.

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
FA0: enc=68 last_edge_us=123456 pin=0 stream=0 interval_ms=200 mode=RISING poll=1 seg=0 tp=0
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

Parameters:

```text
L  test filament length in mm. Default 100; clamped to 20..500.
F  filament feed rate in mm/min. Default 100.
S  segment length in mm. Default 0.35; clamped to 0.05..0.35.
B  maximum in-flight planner blocks. Default 2; clamped to 1..2.
I  FA1 telemetry interval in ms. Default 250.
C  calibrated encoder events per physical filament mm. Default 0.685.
P  minimum passing feed efficiency percent. Default 95.
D  maximum allowed deviation from the current hotend target in °C. Default 2.
Q  query the active point or the latest stored terminal result.
```

The command requires:

- a non-zero hotend target;
- a hotend warm enough for normal Marlin extrusion;
- current temperature within `D` degrees of the target.

It resets the encoder automatically and uses the validated `M874` segmented-motion engine. During the point it samples:

```text
encoder events
temperature
temperature minimum / maximum / average
heater power returned by Marlin
```

Expected encoder events are calculated as:

```text
expected_events = L × C
```

Feed efficiency is:

```text
efficiency_pct = actual_events / expected_events × 100
```

Terminal classifications:

```text
PASS          efficiency >= P and temperature remained within D
LOW_FEED      efficiency < P and temperature remained within D
INVALID_TEMP  temperature left the allowed band during the point
ERROR         no valid hotend target, hotend too cold, or motion could not start
```

Example start output:

```text
FA2: started gen=1 total_mm=100 feed_mm_min=500 expected_enc=68.50 pass_pct=95 temp_target=240 temp_tol=2
```

Example terminal output:

```text
FA2: result=PASS gen=1 duration_ms=12050 total_mm=100 feed_mm_min=500 expected_enc=68.50 actual_enc=68 efficiency_pct=99.27 pass_pct=95 temp_target=240 temp_avg=239.8 temp_min=239.2 temp_max=240.5 heater_avg_raw=118.4
```

Query while running or after completion:

```gcode
M873 Q
```

While active it reports `state=RUNNING`; after completion it repeats the stored terminal result without re-running extrusion.

## Dedicated telemetry

`FA1` records now also include the hotend temperature, target, and heater-power value:

```text
FA1,seq=4,ms=123456,tag=run,cmd_mm=4.550,total_mm=100.000,blocks=2,max_blocks=2,enc=3,pin=1,temp=239.80,target=240,heater=118
```

## Safety and current limits

Marlin thermal runaway, maximum-temperature protection, and cold-extrusion protection remain active.

Phase 2 evaluates a point only after it finishes. It does **not** yet stop midway on falling encoder efficiency. Use normal printer controls and `M112` as the hard emergency stop.

Not implemented yet:

- mid-point encoder-based abort;
- pulse-gap failure detection;
- automatic recovery;
- automatic temperature / speed campaign;
- TMC load classification;
- `M877` / `M878` / `M879` production transaction layer.

The next code layer is mid-point feed-loss detection with a controlled stop, using thresholds derived from completed Phase-2 points.
