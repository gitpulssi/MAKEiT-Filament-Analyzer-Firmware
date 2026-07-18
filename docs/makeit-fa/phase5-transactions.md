# Phase 5 — Idempotent point execution and repeatable query

Phase 5 adds the first host-facing transaction wrapper around the validated
`M873` evaluated point.

It uses two new commands:

```text
M877  execute a host-assigned point ID
M878  query the retained active/latest point without side effects
```

`M873`, `M874`, and `M875` remain available as bring-up and diagnostic commands.

## Execute one point

Heat and stabilize first:

```gcode
M109 S240
```

Then execute a point with a positive host-assigned ID in `J`:

```gcode
M877 J42 L100 F500 S0.35 B2 I250 C0.685 P95 D3 A1 W20 R85 K2 G4 H500 X2
```

Parameters after `J` match the evaluated-point controls already used by `M873`:

```text
J  required positive point ID
L  requested filament length, mm
F  filament feed rate, mm/min
S  segment length, mm
B  maximum in-flight planner blocks
I  telemetry/report interval, ms
C  calibrated encoder events/mm
P  terminal pass efficiency, percent
D  allowed temperature deviation, °C
A  rolling monitor enable
W  rolling window length, mm
R  rolling window minimum efficiency, percent
K  consecutive failed rolling windows required
G  pulse-gap factor; zero disables pulse-gap monitoring
H  minimum pulse-gap timeout, ms
X  minimum expected missing encoder events
```

The transaction layer computes and stores a deterministic 32-bit parameter hash.

## Idempotent replay

Repeating the exact same point ID and parameter set does **not** start motion a
second time:

```gcode
M877 J42 L100 F500 S0.35 B2 I250 C0.685 P95 D3 A1 W20 R85 K2 G4 H500 X2
```

Expected transaction output includes:

```text
FATX: tag=replay point_id=42 params_hash=... state=RUNNING|TERMINAL ...
```

The firmware then reports the current or stored analyzer result.

Reusing the same point ID with different parameters is rejected:

```text
FATX: error=PARAM_CONFLICT point_id=42 stored_hash=... requested_hash=...
```

## Repeatable query

Query the retained transaction:

```gcode
M878
```

Or require a specific retained point ID:

```gcode
M878 J42
```

`M878` is side-effect-free and never starts extrusion. It reports:

```text
FATX: tag=query point_id=42 params_hash=... state=RUNNING|TERMINAL ...
FA2: ...
FA4: ...
```

A query for an ID other than the retained point returns:

```text
FATX: error=NOT_FOUND requested_point_id=41 retained_point_id=42
```

## Current retention and reset semantics

This bring-up implementation retains one transaction in RAM:

- the active point, or
- the latest terminal point.

Starting a new point ID after the previous point is terminal replaces the retained
record. Reusing the retained point ID remains protected against double execution.

The record intentionally does not survive controller reset. A reset invalidates
the physical feed and melt-pressure context, so a pre-reset result must not be
used to satisfy a post-reset query.

This phase does not yet add:

- a multi-entry result ring;
- result-payload CRC;
- persistent forensic storage;
- `M879` host-requested graceful abort.

## Local integration

After pulling the branch, run:

```bash
python tools/makeit-fa/integrate_phase0.py
```

The script adds:

```text
M877 and M878 declarations and dispatcher cases
the transaction header include in MarlinCore.cpp
the transaction idle service after the analyzer idle service
```

Then build the SKR Pro target:

```bash
py -3.12 -m platformio run -e BIGTREE_SKR_PRO
```

## First validation sequence

```gcode
M109 S240
M877 J42 L100 F500 S0.35 B2 I250 C0.685 P95 D3 A1 W20 R85 K2 G4 H500 X2
M878 J42
```

After completion, repeat the same execute command. The second `M877 J42 ...`
must report `tag=replay` and must not move filament again.

Then deliberately change one parameter while keeping `J42`, for example `F400`.
It must return `PARAM_CONFLICT` and must not start motion.
