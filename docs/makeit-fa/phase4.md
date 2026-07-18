# Phase 4 — Pulse-gap feed-loss detection

Phase 4 adds a faster feed-loss path on top of the Phase-3 rolling-window monitor.

The clean Phase-3 bench runs showed:

```text
F500, L120:
  rolling windows: 93.49% to 100.68%
  no failed windows
  feed efficiency: 99.76%
  terminal result: INVALID_TEMP only because temp_min=237.62 C crossed D2

F100, L100:
  rolling windows: 93.49% to 100.68%
  no failed windows
  feed efficiency: 99.27%
  terminal result: PASS
```

So the rolling monitor did not false-trigger on clean feed. Phase 4 now adds a pulse-gap fast path to reduce the amount of filament commanded after motion stops.

## Command

```gcode
M109 S240
M873 L150 F500 S0.35 B2 I250 C0.685 P95 D2 A1 W20 R85 K2 G4 H500 X2
```

Additional Phase-4 parameters:

```text
G  pulse-gap factor. 0 disables the pulse-gap monitor.
   G4 means the timeout is four expected encoder-event intervals.

H  absolute minimum pulse-gap timeout in milliseconds.
   Default: 500 ms.

X  minimum expected missing encoder events before the stop can trigger.
   Default: 2 events.
```

`X` is used instead of `N` because `N` is reserved for G-code line numbers on host serial links.

## Threshold calculation

The expected encoder-event rate is:

```text
event_rate_hz = feed_mm_min / 60 * encoder_events_per_mm
```

The pulse-gap timeout is:

```text
threshold = max(G / event_rate_hz, H)
```

With the measured `C0.685` calibration:

```text
F500:
  expected event interval ~= 175 ms
  G4 threshold ~= 700 ms

F100:
  expected event interval ~= 876 ms
  G4 threshold ~= 3504 ms
```

The timeout alone is not enough. The firmware also requires that commanded travel since the last encoder event represents at least `X` missing expected events. This guards against false stops at startup and low flow.

## Stop behavior

When both the time and distance conditions are met, the firmware emits:

```text
FA4: stop_requested gen=...
     reason=pulse_gap
     cmd_mm=...
     gap_ms=...
     threshold_ms=...
     expected_missing=...
     enc=...
```

It then uses the same graceful stop as Phase 3:

```text
stop adding new segments
allow at most the already committed two blocks to drain
preserve known E position
record the point as aborted / LOW_FEED unless temperature invalidity takes precedence
```

The bounded travel after stop request remains tied to:

```text
S0.35 * B2 = 0.70 mm maximum committed filament distance
```

## Query

```gcode
M873 Q
```

This returns the stored `FA2` point result and an additional `FA4` line such as:

```text
FA4: enabled=1 active=0 triggered=1 factor=4.00 threshold_ms=700
     min_gap_ms=500 min_missing_events=2.00
     last_gap_ms=731 last_expected_missing=4.10 trigger_cmd_mm=43.75
```

## First validation

Clean run:

```gcode
M109 S240
M873 L100 F500 S0.35 B2 I250 C0.685 P95 D3 A1 W20 R85 K2 G4 H500 X2
```

`D3` is recommended for this monitor-only validation because the clean F500 run dipped to 237.62 C at a 240 C target. Keep `D2` for stricter thermal-characterization points if desired.

Expected clean behavior:

```text
FA4: armed ... threshold_ms about 700
no FA4: stop_requested
FA2: result=PASS or INVALID_TEMP depending on D
aborted=0
```

Supervised induced-loss run:

```gcode
M873 L100 F100 S0.35 B2 I250 C0.685 P95 D3 A1 W20 R85 K2 G4 H500 X2
```

After healthy encoder movement begins, release feeder grip so the motor turns without advancing filament. Expected behavior:

```text
FA4: stop_requested before two 20 mm rolling windows complete
bounded graceful drain
FA2: result=LOW_FEED
aborted=1
tested_mm less than requested_mm
```

Keep `M112` available during the induced-loss test.
