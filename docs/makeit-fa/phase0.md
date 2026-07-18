# MAKEiT Filament Analyzer — Phase 0 / Phase 1

Phase 0 proves the encoder and telemetry path. Phase 1 adds an open-loop segmented E-only feed diagnostic before any automatic flow-test logic exists.

## Hardware locked for Phase 0 / 1

```text
Controller:
  BTT SKR Pro V1.2

Encoder input:
  PG5

Telemetry:
  SKR Pro TFT TX3 -> Raspberry Pi RX2
  SKR Pro GND     -> Raspberry Pi GND
  SKR Pro TFT RX3 disconnected for Phase 0 / 1
  Pi TX2          disconnected for Phase 0 / 1
```

The telemetry link is one-way. OctoPrint remains the only commander on the normal printer serial connection.

## Firmware commands

Segmented-feed diagnostic:

```text
M874 L20 F100 S0.35 B2 I250
```

Parameters:

```text
L  total forward filament length in mm. Default 10.0.
F  filament feed rate in mm/min. Default 100.0.
S  segment length in mm. Default 0.35. Clamped to 0.05..0.35.
B  maximum in-flight planner blocks. Default 2. Clamped to 1..2.
I  FA1 telemetry interval in ms while the blocking test runs. Default 250.
```

`M874` is open-loop. It does not stop based on encoder efficiency. It only commands firmware-generated E-only segments while keeping the planner occupancy capped.

Encoder diagnostics:

```text
M875       report encoder count
M875 R     reset encoder count
M875 S1    enable FA0 telemetry stream on Serial3
M875 S0    disable FA0 telemetry stream
M875 I200  set telemetry interval to 200 ms
```

`M876` is intentionally not used because Marlin's `HOST_PROMPT_SUPPORT` uses that command number.

Example `M875` host response:

```text
FA0: enc=0 last_edge_us=123456 pin=0 stream=0 interval_ms=200 mode=RISING poll=1
```

Example `FA0` telemetry record:

```text
FA0,seq=12,ms=45820,enc=381,last_edge_us=45810122,pin=1,mode=RISING
```

Example `FA1` telemetry record during `M874`:

```text
FA1,seq=4,ms=123456,tag=run,cmd_mm=4.550,total_mm=20.000,blocks=2,max_blocks=2,enc=12,pin=1
```

## Integration

The analyzer files are present in the firmware tree. Run this once from the repository root to patch the Marlin hooks and config:

```bash
python tools/makeit-fa/integrate_phase0.py
```

The script:

- disables `SERIAL_PORT_3 3` so analyzer telemetry can own `Serial3` directly;
- appends the analyzer config block to `Configuration_adv.h`;
- adds `makeit_fa_phase0.init()` to `setup()`;
- adds `makeit_fa_phase0.idle()` to `idle()`;
- declares `M874` and `M875` in `gcode.h`;
- dispatches `M874` and `M875` in `gcode.cpp`;
- removes obsolete analyzer `M876` hooks from earlier local test runs.

## Encoder calibration result

Current measured result:

```text
physical length:       400 mm
encoder events:        274
count mode:            RISING
count method:          polling fallback
speed check:           100 mm/min and 500 mm/min both gave 274 events
encoder events/mm:     0.685
encoder mm/event:      1.460
selected segment:      0.35 mm
```

## Encoder calibration procedure

Use physical filament marks, not commanded E distance.

1. Mark the filament with two marks 300–500 mm apart.
2. Measure the mark spacing independently of the printer.
3. Feed slowly and stop mark 1 exactly at a fixed hard reference edge.
4. Send `M875 R`.
5. Feed slowly and stop mark 2 at the same reference edge.
6. Send `M875`.
7. Record encoder count, physical length, feed speed, and trigger mode.
8. Repeat 3–5 times at low speed and once at a second speed.

Create a calibration CSV:

```csv
physical_length_mm,encoder_events
400.0,274
400.0,274
400.0,274
```

Calculate encoder geometry and segment beat values:

```bash
python tools/makeit-fa/encoder_calculation.py calibration_runs.csv --segment-mm 0.35
```

## First segmented-feed test

Heat the hotend to a safe extrusion temperature before running `M874`; the diagnostic uses normal Marlin extrusion motion and does not bypass cold-extrusion protection.

Start conservative:

```gcode
M875 R
M874 L20 F100 S0.35 B2 I250
M875
```

Then try a faster open-loop run:

```gcode
M875 R
M874 L20 F500 S0.35 B2 I250
M875
```

Expected host completion line:

```text
FA1: done total_mm=20 segment_mm=0.35 feed_mm_min=100 max_blocks=2 enqueued=58 enc=...
```

For the real timing test, capture E STEP with a logic analyzer and compare continuous G1 feed against `M874` segmented feed.

## Phase-0 / 1 safety

Keep Marlin thermal runaway and max-temperature protection enabled. These blocks have no encoder-based automatic abort. Use normal printer controls and `M112` as the hard emergency stop.

## What is intentionally not included yet

- no encoder-based stop;
- no feed-efficiency threshold;
- no recovery classifier;
- no Qmax campaign;
- no `M877` / `M878` / `M879` transaction layer.

The next build block after `M874` validation adds lower-level executed E-step timing and more detailed raw timing telemetry if the segmented feed looks clean.
