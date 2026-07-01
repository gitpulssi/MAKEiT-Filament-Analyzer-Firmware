# MAKEiT Filament Analyzer — Phase 0

Phase 0 proves the encoder and telemetry path before any automatic flow-test logic exists.

## Hardware locked for Phase 0

```text
Encoder input:
  FIL_RUNOUT_PIN / PI11

Telemetry:
  GTR TFT TX3 -> Raspberry Pi RX2
  GTR GND     -> Raspberry Pi GND
  GTR TFT RX3 disconnected for Phase 0
  Pi TX2      disconnected for Phase 0
```

The telemetry link is one-way. OctoPrint remains the only commander on the normal printer serial connection.

## Firmware command

Temporary diagnostic command:

```text
M875       report encoder count
M875 R     reset encoder count
M875 S1    enable FA0 telemetry stream on Serial3
M875 S0    disable FA0 telemetry stream
M875 I200  set telemetry interval to 200 ms
```

Example host response:

```text
FA0: enc=0 last_edge_us=123456 stream=0 interval_ms=200 mode=RISING
```

Example telemetry record:

```text
FA0,seq=12,ms=45820,enc=381,last_edge_us=45810122,mode=RISING
```

## Integration

The analyzer files are present in the firmware tree. Run this once from the repository root to patch the Marlin hooks and Phase-0 config:

```bash
python tools/makeit-fa/integrate_phase0.py
```

The script:

- disables `SERIAL_PORT_3 3` so analyzer telemetry can own `Serial3` directly;
- appends the Phase-0 config block to `Configuration_adv.h`;
- adds `makeit_fa_phase0.init()` to `setup()`;
- adds `makeit_fa_phase0.idle()` to `idle()`;
- declares `M875` in `gcode.h`;
- dispatches `M875` in `gcode.cpp`.

## Encoder calibration procedure

Use physical filament marks, not commanded E distance.

1. Mark the filament with two marks 300–500 mm apart.
2. Measure the mark spacing independently of the printer.
3. Feed slowly and stop mark 1 exactly at a fixed hard reference edge.
4. Send `M875 R`.
5. Feed slowly and stop mark 2 at the same reference edge.
6. Send `M875`.
7. Record encoder count, physical length, feed speed, and ISR trigger mode.
8. Repeat 3–5 times at low speed and once at a second speed.

Create a calibration CSV:

```csv
physical_length_mm,encoder_events
400.0,286
400.0,287
400.0,286
```

Calculate encoder geometry and segment beat values:

```bash
python tools/makeit-fa/encoder_calculation.py calibration_runs.csv --segment-mm 0.35
```

## Phase-0 safety

Keep Marlin thermal runaway and max-temperature protection enabled. This first block has no encoder-based automatic abort. Use normal printer controls and `M112` as the hard emergency stop.

## What is intentionally not included yet

- no segmented E-only feed engine;
- no encoder-based stop;
- no feed-efficiency threshold;
- no recovery classifier;
- no Qmax campaign;
- no `M877` / `M878` / `M879` transaction layer.

The next build block adds the raw timing skeleton and segmented-feed test after encoder events/mm is measured.
