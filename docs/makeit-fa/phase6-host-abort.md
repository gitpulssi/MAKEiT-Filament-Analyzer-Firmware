# Phase 6 — Host-requested graceful abort (`M879`)

Phase 6 adds a controlled drain-stop for a running `M877` transaction.

## Two command forms

Point-ID-qualified normal command:

```gcode
M879 J42
```

This form validates `J` against the retained transaction. A wrong ID is rejected.

Bare out-of-band command:

```gcode
M879
```

With `EMERGENCY_PARSER` enabled, a bare `M879` is recognized directly in the receive stream and aborts whichever analyzer point is currently running. The emergency parser deliberately ignores `M879` lines containing arguments, so `M879 J42` remains point-ID-qualified on the normal parser.

The serial line still reaches Marlin's normal G-code queue after the emergency parser observes it. The normal `M879` handler therefore suppresses its queued copy when transaction idle has already consumed the same bare command. One bare command must produce one abort action, not duplicate `abort_replay` or `NO_RUNNING_POINT` records.

`M112` remains the hard emergency stop. `M879` is a controlled graceful stop.

## Stop behavior

On a valid request, firmware:

1. marks the point as host-aborted;
2. changes the transaction to `ABORTING`;
3. stops adding E-only segments;
4. drains only the already committed planner horizon;
5. preserves known E position;
6. stores the analyzer result as `ABORTED`;
7. stores the transaction as `ABORTED` with result generation and CRC.

With:

```text
S0.35 B2
```

the maximum already committed distance is approximately:

```text
0.35 mm × 2 blocks = 0.70 mm
```

## Validation run

Use a point long enough that the abort cannot accidentally be sent after completion:

```gcode
M109 S240
M877 J44 L500 F100 S0.35 B2 I250 C0.685 P95 D5 A1 W20 R85 K2 G4 H500 X2
```

At `F100`, a 500 mm point takes about five minutes. The first healthy `FA3: window` arrives after roughly 12 seconds. Send the abort immediately after that first window; do not wait for another manual checkpoint:

```gcode
M879
```

This validates the bare emergency-parser path. A later run may separately validate the point-qualified normal path with:

```gcode
M879 J44
```

Expected immediate records from one bare command:

```text
FA6: tag=abort_requested gen=... cmd_mm=... completed_est_mm=... committed_blocks=...
FATX: abort_source=emergency point_id=44
FATX: tag=abort_requested point_id=44 ... state=ABORTING abort_requested=1 ...
```

There should not be a second normal-path abort record for the same bare command.

After the bounded drain:

```text
FA1: done ...
FA2: result=ABORTED ... aborted=1 host_abort=1 ...
FATX: tag=aborted point_id=44 ... state=ABORTED abort_requested=1
      result_generation=... result_code=... result_crc=...
```

Query without motion:

```gcode
M878 J44
```

Repeat the query once. Both replies must carry the same nonzero `result_crc`.

## Idempotency and rejection rules

Repeated abort request while abort is pending:

```text
FATX: tag=abort_replay ...
```

Wrong point ID on the qualified path:

```gcode
M879 J45
```

```text
FATX: error=NOT_FOUND requested_point_id=45 retained_point_id=44
```

Abort after completion:

```text
FATX: error=NO_RUNNING_POINT ...
```

A request after the segmented engine has entered its natural terminal drain may be rejected because it can no longer reduce committed motion.

## Acceptance criteria

```text
no reset or watchdog event
transaction changes RUNNING -> ABORTING -> ABORTED
one bare M879 produces one abort action
no new segments are added after the request
only the bounded in-flight horizon drains
FA2 result is ABORTED
FATX retains abort_requested=1 and a stable nonzero result CRC
repeated M878 is side-effect-free
```
