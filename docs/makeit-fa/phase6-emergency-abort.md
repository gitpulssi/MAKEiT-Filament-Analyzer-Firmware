# Phase 6 — Out-of-band M879 graceful abort and result CRC

Phase 5 proved the execute/query transaction layer:

- `M877 J42 ...` executed one point.
- `M878 J42` returned the retained terminal result.
- Repeating the identical `M877 J42 ...` returned `tag=replay` and did not move filament.
- Querying `M878 J41` returned `NOT_FOUND` while point 42 remained retained.

Phase 6 adds a host-requested controlled stop and a stable integrity value for the retained terminal record.

## Stop semantics

`M879` is a graceful analyzer stop, not a hard quickstop:

```text
stop adding new E-only segments
allow only the bounded in-flight blocks to drain
preserve the known E position
retain and query the terminal transaction result
```

With the normal analyzer settings:

```text
S0.35 B2
```

the maximum already-committed filament distance is approximately:

```text
0.35 mm × 2 blocks = 0.70 mm
```

Use `M112` for a hard emergency stop. A hard stop forfeits trusted feed position and is not the same operation as `M879`.

## Emergency-parser path

Marlin's low-level emergency parser now recognizes `M879` directly in the serial receive stream. The parser sets only an abort flag. The normal analyzer/transaction idle service consumes that flag and requests the controlled drain stop.

No serial output, TMC UART access, allocation, or motion operation is performed inside the receive parser.

`EMERGENCY_PARSER` must remain enabled in `Configuration_adv.h`.

For OctoPrint, configure `M879` as an emergency command so it is transmitted immediately rather than waiting behind ordinary queued commands.

## Commands

Abort the currently running analyzer point:

```gcode
M879
```

Point-ID-qualified normal-path form:

```gcode
M879 J42
```

The low-level emergency parser intentionally treats bare `M879` as "abort the currently active analyzer point." The optional `J` check is applied by the normal G-code handler.

## Transaction states

The retained transaction now distinguishes:

```text
EMPTY
RUNNING
ABORTING
TERMINAL
ABORTED
```

An accepted abort first produces `ABORTING`. After the bounded planner horizon drains, the retained transaction becomes `ABORTED`.

The evaluated point result is classified `ABORTED`, with:

```text
aborted=1
host_abort=1
requested_mm=<original request>
tested_mm=<distance reached before controlled drain completed>
```

## Result integrity

Every retained transaction reports:

```text
result_generation
result_code
result_crc
```

`result_crc` is a CRC-32 over the stable transaction and terminal-summary fields:

```text
point_id
params_hash
transaction state
start / finish timestamps
abort state and abort timestamp
result generation and result code
actual encoder events
tested filament distance
terminal efficiency
```

The CRC protects the retained result payload, not a single serial frame. Repeated `M878` queries and idempotent `M877` replays must return the same `result_crc` for the same terminal record.

## Pull, integrate, and build

```powershell
cd E:\MAKEiT_steel\MAKEiT-Filament-Analyzer-Firmware
git pull --ff-only
python tools\makeit-fa\integrate_phase0.py
py -3.12 -m platformio run -e BIGTREE_SKR_PRO
```

Flash:

```text
.pio\build\BIGTREE_SKR_PRO\firmware.bin
```

## First controlled-abort validation

Use a long enough point to give time to send the abort:

```gcode
M109 S240
M877 J43 L200 F500 S0.35 B2 I250 C0.685 P95 D3 A1 W20 R85 K2 G4 H500 X2
```

After at least one healthy `FA3: window` record, send:

```gcode
M879
```

Expected sequence:

```text
FA6: tag=abort_requested ...
FATX: tag=abort_requested ... state=ABORTING ...
short bounded planner drain
FA2: result=ABORTED ... aborted=1 host_abort=1 ...
FATX: tag=aborted ... state=ABORTED ... result_crc=<nonzero>
```

Query the retained record:

```gcode
M878 J43
```

Expected:

```text
same point_id
state=ABORTED
same result_generation
same result_crc
same FA2 terminal result
```

Sending `M879` again after terminal completion should not start motion. Sending the same `M877 J43 ...` again remains an idempotent replay and also must not start motion.

## Remaining layers

Not yet implemented:

- multi-entry result ring;
- telemetry mirror carrying the same result CRC;
- automatic recovery and re-prime;
- automatic temperature/speed campaign;
- TMC load classification.
