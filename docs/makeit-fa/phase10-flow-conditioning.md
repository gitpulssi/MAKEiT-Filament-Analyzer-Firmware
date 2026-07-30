# Phase 10 — Same-speed flow conditioning before measurement

## Purpose

Stationary filament can heat-soak while a temperature changes or while a thermal
dwell runs. That material can be easier to push than continuously arriving
filament and can bias the beginning of a flow test.

Phase 10 uses:

```text
WAIT_TEMP -> CONDITIONING -> RUNNING_POINT
```

Conditioning runs at the same speed as the upcoming measured point. The encoder
and all measured-point statistics are reset after conditioning, and measurement
starts without a second thermal dwell.

## M881 configuration

```gcode
M881        ; report RAM-only setting
M881 P50    ; 50 mm high-flow conditioning
M881 P20    ; short development conditioning
M881 P0     ; disable conditioning
```

The reset default remains 20 mm. Non-zero values are constrained to 10–100 mm.
For Dyze Pro high-flow tests use 50 mm. That provides 10 seconds of flow at F300
and 5 seconds at F600 before measurement.

## Two different flow limits

Phase 11 separates the two questions:

```text
P = printing-accuracy threshold
R = hard throughput / rolling safety threshold
```

Use:

```text
P97 R85
```

A full-length point between 85% and 97% records the first printing-accuracy loss
but no longer ends the ladder. An early rolling/pulse-gap stop or whole-point
efficiency below 85% remains a hard `LIMIT_FOUND` result.

The delivered flow estimate is:

```text
Q_delivered = Q_commanded × efficiency_pct / 100
```

## High-flow PLA profile

For the 0.6 mm nozzle use:

```text
temperatures: 180, 190, 200, 210, 220 C
feed ladder:  F300, F350, F400, F450, F500, F550, F600
conditioning: 50 mm
measurement:  200 mm
accuracy:      P97
hard stop:     R85 K2
thermal:       D3 initially; D5 for 220 C throughput extension
rolling:       W50
```

For 1.75 mm filament:

```text
F300 = 12.03 mm3/s
F350 = 14.03 mm3/s
F400 = 16.04 mm3/s
F450 = 18.04 mm3/s
F500 = 20.04 mm3/s
F550 = 22.05 mm3/s
F600 = 24.05 mm3/s
```

A 200 mm measured point expects 137 encoder events at 0.685 events/mm. One event
changes whole-point efficiency by about 0.73 percentage points.

## Preferred: one temperature per campaign

```gcode
M881 P50
M109 S190
M872 J9100 F300 U600 V50 O10 L200 S0.35 B2 I250 \
     C0.685 P97 D3 A1 W50 R85 K2 G4 H500 X2
```

Repeat at 200, 210, and 220 C using different campaign IDs.

New Phase-11 output reports both brackets:

```text
accuracy_last_pass
accuracy_first_fail
last_pass
first_fail
```

`accuracy_*` is the 97% printing bracket. `last_pass` / `first_fail` is the hard
85% throughput bracket.

If a row ends in `LIMIT_FOUND`, set the next-row target and perform recovery:

```gcode
M109 S200
M880 J9190 T220 O10 L50 F150 V100 U150 S0.35 B2 I250 \
     C0.685 P97 D3 A1 W50 R85 K2 G4 H500 X2
```

Recovery validation remains P97 because recovery should establish clean, accurate
feed before another campaign begins.

## Full automatic envelope

Use only after disabling or extending the OctoPrint idle-heater timeout:

```gcode
M881 P50
M109 S190
M870 J9000 T220 E10 M220 Y1.75 F300 U600 V50 O10 L200 \
     S0.35 B2 I250 C0.685 P97 D3 A1 W50 R85 K2 G4 H500 X2
```

Seven speed points plus one recovery slot give a row stride of eight. Four rows
reserve IDs 9000–9031. Maximum normal filament use is 7000 mm.

## July 20 measured high-flow map

```text
180 C: F375 accepted at 85.40%; F400 hard fail at 75.21%
       delivered plateau about 12.8 mm3/s

190 C: F400 accepted at 93.43%; F450 hard fail at 82.55%
       delivered plateau about 15.0 mm3/s

200 C: F500 accepted at 85.40%; F550 conditioning collapsed to 64.23%
       delivered plateau about 17.1 mm3/s

210 C: F550 accepted at 88.32%; F600 hard fail at 82.81%
       delivered plateau about 19.5–20.0 mm3/s

220 C: F600 delivered about 21.4 mm3/s at 89.05%
       point was INVALID_TEMP only because minimum temperature reached 216.95 C,
       0.05 C beyond D3. No hard flow limit was found.
```

For 220 C throughput extension, retest from F600 upward with D5 while keeping the
measured minimum temperature as a separate heater-control constraint.

## Why the previous heater stopped

The July 19 serial log showed:

```text
17:12:28.669  M870 started
17:42:28.671  host sent M104 S0
```

The firmware did not decide to cool the hotend. OctoPrint explicitly changed the
target to zero almost exactly 30 minutes after the envelope started. A
HeaterTimeout or BetterHeaterTimeout plugin is the likely source because terminal
commands are not an OctoPrint print job.

Disable that plugin during testing or set its timeout to at least 60–90 minutes.
OctoPrint's temperature-history `cutoff: 30` only controls graph history; it does
not itself turn off the heater.

## Safety and acceptance

```text
conditioning speed equals upcoming test speed
measurement counters reset after conditioning
P97 records the printing-accuracy boundary
R85 K2 remains the hard throughput / rolling stop
measured temperature stays within the chosen D tolerance
conditioning efficiency stays above R85
pulse-gap monitoring remains enabled
cancel commits no more than S0.35 x B2 = 0.70 mm
```
