# Phase 10 — Same-speed flow conditioning before measurement

## Why this layer exists

A temperature or speed campaign previously followed this sequence:

```text
wait with filament stationary -> start measured feed
```

That creates a residence-time bias. Filament already inside the hotend can sit in
the melt zone for many seconds while the temperature rises or while the thermal
dwell completes. It may become much easier to push than continuously arriving
filament in a real print. The first part of the next point can therefore look
better than the true steady-flow condition.

Phase 10 changes every campaign point to:

```text
WAIT_TEMP -> CONDITIONING -> RUNNING_POINT
```

After the tight thermal dwell, firmware feeds a configurable conditioning length
at the **same speed as the upcoming measured point**. This displaces the
stationary, heat-soaked melt column and establishes the upcoming flow condition.
The encoder and all evaluated-point statistics are then reset, and measurement
starts in the same main-loop iteration without another thermal dwell.

## Configuration command

`M881` controls a RAM-only default used by `M872` and `M870`:

```gcode
M881        ; report current setting
M881 Q      ; report current setting
M881 P20    ; condition with 20 mm before every measured point
M881 P0     ; disable conditioning
```

The default after reset is:

```text
20 mm
```

Non-zero values are constrained to 10–100 mm. Twenty millimetres of 1.75 mm
filament is about 48.1 mm³ of incoming polymer, which is a practical first flush
for this hotend. The value remains deliberately adjustable because melt-zone
volume and residence behaviour depend on hotend geometry.

Typical report:

```text
FA10: conditioning_mm=20.00 speed_mode=same_as_point
      counter_reset_before_measure=1 thermal_dwell_after_conditioning=0
```

## Campaign behaviour

For each speed in an `M872` or `M870` campaign, firmware now:

1. waits for the existing tight temperature-settle criterion;
2. resets the encoder;
3. feeds `conditioning_mm` through the bounded 0.35 mm segmented engine at the
   upcoming point speed;
4. records conditioning encoder movement as a safety observation;
5. starts the normal idempotent evaluated point immediately after the bounded
   conditioning feed drains;
6. resets encoder and point statistics again before measured data begins.

The conditioning feed is not included in `FA2` efficiency or temperature
statistics.

## Conditioning failure handling

When rolling monitoring is enabled (`A1`), conditioning efficiency is compared
with the configured rolling threshold `R`.

If the conditioning feed itself falls below that threshold, the campaign records
that speed as the first failure and stops before commanding the full measured
point. This reduces unnecessary grinding at a speed that already failed to move
filament during the flush.

A temperature excursion outside `D` during conditioning terminates the campaign
as `INVALID_TEMP`.

## Cancellation

`M872 Z`, `M870 Z`, or the applicable higher-level cancel path can stop a
conditioning feed. New segments stop being queued and only the bounded in-flight
horizon drains:

```text
S0.35 × B2 = 0.70 mm maximum committed filament distance
```

## Important limitation

The conditioning feed and measured point meet at a planner-empty boundary. The
measured point is started later in the same Marlin main-loop iteration, so there
is no intentional dwell and the interruption should be very short. It is not yet
a mathematically seamless, never-zero-velocity handoff.

A future trace-based refinement may move the encoder reset to an executed-step
boundary inside one uninterrupted segmented stream. Phase 10 addresses the
large and scientifically important bias — seconds or minutes of stationary
preheating — without requiring that more invasive step-domain transition.

## PLA / 0.6 mm nozzle validation

```gcode
M881 P20
M109 S190
M870 J3000 T230 E10 M230 Y1.75 F50 U300 V50 O10 L30 \
     S0.35 B2 I250 C0.685 P95 D5 A1 W10 R85 K2 G4 H500 X2
```

Expected progression at every speed:

```text
FA7: state=WAIT_TEMP
FA7: tag=conditioning_started state=CONDITIONING
FA1: done total_mm=20 ...
FA7: tag=conditioning_done conditioning_eff=...
FATX / FA2 measured point starts
```

Acceptance criteria:

```text
conditioning occurs before every measured point
conditioning speed equals the upcoming test speed
FA2 counters begin after conditioning, not before it
no additional thermal dwell occurs between conditioning and measurement
clean conditioning efficiency remains above R
M881 P0 reproduces legacy behaviour
cancel drains no more than the configured committed horizon
```

## Filament consumption

With the example above:

```text
5 temperatures × 6 speeds × (20 mm conditioning + 30 mm measurement)
= 1500 mm normal campaign feed
```

Recovery purge and validation add filament only after rows that actually find a
limit.
