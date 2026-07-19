# Phase 8 — Temperature × speed envelope (`M870`)

Phase 8 adds an outer temperature ladder around the validated Phase-7 `M872`
speed campaign. It is the first layer that automatically produces rows of the
`Qmax(T)` map.

## Command model

The current hotend target is the first temperature. Set and stabilize it before
starting the envelope:

```gcode
M109 S230
M870 J1000 T240 E10 Y1.75 F100 U300 V100 O5 \
     L50 S0.35 B2 I250 C0.685 P95 D5 A1 W20 R85 K2 G4 H500 X2
```

Envelope-specific parameters:

```text
J  envelope ID and first point ID reserved by the envelope
T  maximum temperature in °C
E  temperature increment in °C
Y  filament diameter in mm for volumetric-flow reporting
Q  query current/latest envelope, optionally with J
Z  cancel current envelope, optionally with J
```

The remaining parameters have the same meaning as `M872`.

The example runs these rows:

```text
230 °C: F100, F200, F300
240 °C: F100, F200, F300
```

## Point-ID allocation

If there are `Ns` speed points per row and `Nt` temperature rows, the envelope
reserves `Ns × Nt` consecutive point IDs.

For the example above:

```text
row 0 / 230 °C: point IDs 1000..1002
row 1 / 240 °C: point IDs 1003..1005
```

The firmware rejects an envelope whose reserved range overlaps the retained
transaction or speed-campaign record.

## Row output

Each finished temperature row emits an `FA8ROW` record:

```text
FA8ROW: envelope_id=1000 row=0 temp_c=230.00 campaign_id=1000
        campaign_state=COMPLETE last_pass_feed=300.00 first_fail_feed=0.00
        q_pass_mm3_s=12.03 q_fail_mm3_s=0.00
        point_result=1 point_crc=...
```

Volumetric flow is calculated from the supplied filament diameter:

```text
Q = π × diameter² / 4 × feed_mm_min / 60
```

When every configured speed passes, `last_pass` / `q_pass` are measured lower
bounds and `first_fail` is zero. When the speed campaign finds a limit, the row
contains both the last passing speed and first failing speed.

## Safe Phase-8 limit policy

A real `LOW_FEED` point can leave melt pressure and feeder grip indeterminate.
Automatic recovery / re-prime is not implemented yet. Therefore Phase 8 stores
the row and terminates as:

```text
state=LIMIT_FOUND
tag=limit_found_recovery_required
```

It does **not** continue to a hotter row after a detected feed limit. This avoids
contaminating later temperatures with an unvalidated feed state. Phase 9 will
add recovery and allow the full envelope to continue after a row limit.

Rows that reach their configured maximum without feed loss advance normally to
the next temperature.

## Thermal ownership

`M870` records the starting hotend target, changes the target for each row, and
restores the original target when the envelope completes, is cancelled, or
terminates with an error.

Each nested speed campaign still requires the temperature to remain within `D`
for `O` seconds before every point.

## Query and replay

```gcode
M870 Q
M870 Q J1000
```

The query is side-effect-free and repeats the envelope summary plus every stored
row. Terminal output includes a stable `envelope_crc` built from the envelope
identity, terminal state, and retained row table.

Repeating the identical start command with the same `J` returns `tag=replay`
and never re-runs motion. Reusing `J` with changed parameters returns
`PARAM_CONFLICT`.

## Cancellation

```gcode
M870 Z J1000
```

While a nested row is waiting for temperature, cancellation ends it immediately.
During a point, cancellation uses the existing bounded graceful abort path.
Bare `M879` also aborts an active point; the envelope then terminates `ABORTED`.

## First validation run

Use two temperatures and two speeds:

```gcode
M109 S230
M870 J1000 T240 E10 Y1.75 F100 U200 V100 O5 \
     L30 S0.35 B2 I250 C0.685 P95 D5 A1 W20 R85 K2 G4 H500 X2
```

Expected point IDs:

```text
230 °C: 1000, 1001
240 °C: 1002, 1003
```

Acceptance criteria:

```text
both rows wait for five seconds of thermal stability
four points execute without manual commands
all retained point IDs are unique
one FA8ROW record is emitted per temperature
terminal state is COMPLETE when all configured speeds pass
last_pass=200 and first_fail=0 for both rows
the final hotend target is restored to 230 °C
repeated M870 Q J1000 returns the same rows and envelope_crc
no query or replay causes filament motion
```
