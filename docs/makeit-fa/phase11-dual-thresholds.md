# Phase 11 — Separate printing accuracy from hard throughput

The Dyze Pro high-flow map showed that one threshold cannot answer both questions:

1. At what speed does extrusion stop matching the slicer accurately?
2. At what speed does the hotend / drive system reach its hard throughput limit?

Before Phase 11, `P` answered both questions and `M872` stopped at the first
whole-point result below `P`. Using `P97` therefore stopped a campaign at a mild
3–4% feed deficit, before the physical throughput plateau was reached. Using
`P85` allowed the campaign to continue but discarded an explicit 97% printing
accuracy bracket.

## Phase-11 semantics

```text
P = printing-accuracy threshold
R = hard throughput / rolling safety threshold
```

Recommended high-flow values:

```text
P97 R85
```

A measured point is classified as follows:

```text
efficiency >= P
    accurate point; update accuracy_last_pass and throughput last_pass

full-length point, R <= efficiency < P
    record accuracy_first_fail; report accuracy_limit_continue; continue ladder

efficiency < R, early rolling stop, pulse-gap stop, or incomplete point
    hard LIMIT_FOUND; record first_fail and stop ladder
```

Conditioning remains a hard safety check against `R`. If same-speed conditioning
is already below `R`, the longer measured point is not started.

## New FA7 fields

```text
accuracy_last_pass
accuracy_first_fail
point_efficiency
accuracy_threshold
throughput_threshold
```

Existing fields retain these meanings:

```text
last_pass  = highest point accepted for throughput
first_fail = first hard throughput failure
```

## New FA8ROW fields

```text
accuracy_last_pass_feed
accuracy_first_fail_feed
q_accurate_mm3_s
q_accuracy_fail_mm3_s
```

The existing `q_pass_mm3_s` / `q_fail_mm3_s` fields remain the hard-throughput
bracket.

## Dyze Pro PLA command

Run one temperature at a time:

```gcode
M881 P50
M109 S190
M872 J9100 F300 U600 V50 O10 L200 S0.35 B2 I250 \
     C0.685 P97 D3 A1 W50 R85 K2 G4 H500 X2
```

The campaign now continues after a full-length 96%, 93%, or 90% point, while
retaining the first sub-97% speed. It stops when efficiency falls below 85%, a
rolling/pulse-gap safety condition stops motion, temperature becomes invalid,
or the configured maximum feed is reached.

## July 20 high-flow map before Phase 11

The `P85 R85` discovery runs measured these hard limits / plateaus:

```text
180 C: F375 pass, F400 hard fail; delivered plateau about 12.8 mm3/s
190 C: F400 pass, F450 hard fail; delivered plateau about 15.0 mm3/s
200 C: F500 pass, F550 conditioning collapse; about 17.1 mm3/s
210 C: F550 pass, F600 hard fail; delivered plateau about 19.5–20.0 mm3/s
220 C: F600 delivered about 21.4 mm3/s; point invalid only because temperature
       reached 216.95 C, 0.05 C beyond D3. No hard flow limit was found.
```

At 220 C, repeat from F600 upward with `D5` if the goal is throughput discovery,
while preserving the measured minimum temperature as a separate heater-control
constraint.
