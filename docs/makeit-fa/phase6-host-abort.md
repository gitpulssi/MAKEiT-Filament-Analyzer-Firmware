# Phase 6 — Host-requested graceful abort (`M879`)

Phase 6 adds a point-ID-qualified, controlled abort for a running `M877` transaction.

## Why normal command parsing is sufficient here

`M877` starts a non-blocking test point and returns immediately. The segmented feed engine continues from Marlin `idle()`, so the normal G-code queue remains available while the point runs. Therefore `M879` can be delivered on the normal command channel without blocking behind `M877`.

This is not the hard emergency path. `M112` remains the hard emergency stop.

## Command

```gcode
M879 J42
```

`J` is required and must match the active transaction point ID.

## Stop behavior

On a valid request, firmware:

1. marks the point as host-aborted;
2. stops adding new E-only segments;
3. lets only the already committed planner horizon drain;
4. preserves known E position;
5. stores the point as `ABORTED`;
6. completes the transaction as terminal.

With the validated settings:

```text
S0.35 B2
```

the maximum already committed distance is approximately:

```text
0.35 mm × 2 blocks = 0.70 mm
```

## Expected output

Start a longer point:

```gcode
M877 J43 L200 F500 S0.35 B2 I250 C0.685 P95 D3 A1 W20 R85 K2 G4 H500 X2
```

While it is running:

```gcode
M879 J43
```

Expected immediate records:

```text
FA6: tag=abort_requested gen=... cmd_mm=... completed_est_mm=... committed_blocks=...
FATX: tag=abort_requested point_id=43 ... state=RUNNING abort_requested=1 ...
```

After the bounded drain:

```text
FA1: done ...
FA2: result=ABORTED ... aborted=1 host_abort=1 ...
FATX: tag=completed point_id=43 ... state=TERMINAL abort_requested=1 ...
```

Query the retained result without motion:

```gcode
M878 J43
```

## Idempotency and rejection rules

Repeating the same abort request is safe:

```gcode
M879 J43
```

Expected while the abort is already pending:

```text
FATX: tag=abort_replay ...
```

Wrong point ID:

```gcode
M879 J44
```

Expected:

```text
FATX: error=NOT_FOUND requested_point_id=44 retained_point_id=43
```

Abort after the point is already terminal:

```text
FATX: error=NOT_RUNNING ...
```

A request that arrives after the segmented engine has entered its natural terminal drain is rejected as too late, because it can no longer reduce committed motion.

## Validation sequence

Use a supervised point long enough to issue the abort comfortably:

```gcode
M109 S240
M877 J43 L200 F100 S0.35 B2 I250 C0.685 P95 D3 A1 W20 R85 K2 G4 H500 X2
```

After the first healthy rolling-window record:

```gcode
M879 J43
```

Then:

```gcode
M878 J43
```

Acceptance criteria:

```text
no reset or watchdog event
no new segments added after the request
only the bounded in-flight horizon drains
FA2 terminal result is ABORTED
FATX terminal record retains abort_requested=1
repeated M878 is side-effect-free
```
