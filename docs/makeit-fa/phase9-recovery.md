# Phase 9 — Recovery / re-prime and continued low-temperature envelopes

Phase 9 allows the `M870` temperature envelope to continue after a real
`LOW_FEED` row. This is required for useful PLA scans that start near 190 °C:
a cold row may find a low flow limit, but that result must not prevent testing
195, 200, 205, 210, 215, 220, 225, and 230 °C.

## Why recovery is required

A feed-limit event can leave three things uncertain:

- melt pressure in the hotend;
- filament engagement at the drive gear;
- whether the encoder still follows commanded feed.

Phase 8 therefore stopped after the first `LIMIT_FOUND`. Phase 9 adds a bounded,
measured recovery sequence before the next temperature row.

## Recovery sequence

When a non-final temperature row ends in `LIMIT_FOUND`, firmware:

1. stores the failed row and its `last_pass` / `first_fail` bracket;
2. raises the hotend to the configured recovery temperature;
3. requires a tight thermal dwell within `min(D, 1 °C)`;
4. purges 20 mm at the envelope's starting feed speed using the validated
   0.35 mm / two-block segmented engine;
5. resets the encoder and runs a 20–40 mm evaluated validation point;
6. accepts recovery only when that validation result is `PASS`;
7. restores the next row temperature and resumes the envelope.

If validation fails, the envelope terminates as `RECOVERY_FAILED`. This covers a
fouled hob, persistent clog, broken encoder contact, or a recovery temperature
that is still too low.

## `M880` standalone recovery command

`M880` makes the recovery sequence independently testable. `M869` is not used
because Marlin reserves `M860–M869` when `I2C_POSITION_ENCODERS` is enabled.

```gcode
M880 J9000 T230 O5 L20 F100 V30 U100 S0.35 B2 I250 \
     C0.685 P95 D5 A1 W20 R85 K2 G4 H500 X2
```

Parameters:

```text
J  unique recovery / validation point ID
T  recovery temperature, °C
O  required tight thermal dwell, seconds
L  unmeasured prime length, mm
F  prime feed speed, mm/min
V  evaluated validation length, mm
U  evaluated validation feed speed, mm/min
S/B/I/C/P/D/A/W/R/K/G/H/X  same validated point controls as M877
Q  side-effect-free query, optionally with J
Z  graceful cancel, optionally with J
```

Expected state sequence:

```text
WAIT_TEMP -> PRIMING -> VALIDATING -> COMPLETE
```

Other terminal states are `FAILED`, `ABORTED`, and `ERROR`.

## `M870` recovery parameter

Phase 9 adds one envelope parameter:

```text
M  recovery temperature, °C
```

When omitted, it defaults to the envelope maximum temperature. `M0` disables
recovery and preserves the conservative Phase-8 stop-on-limit behavior.

## PLA / 0.6 mm nozzle scan, 190–230 °C

The nozzle diameter affects the physical pressure and flow ceiling, but it does
not enter the filament-volume conversion. `Y1.75` is the **filament diameter**,
not the 0.6 mm nozzle diameter.

Set and stabilize the first row:

```gcode
M109 S190
```

Then run a 5 °C temperature ladder with a conservative 2–12 mm³/s speed ladder:

```gcode
M870 J2000 T230 E5 M230 Y1.75 F50 U300 V50 O10 \
     L40 S0.35 B2 I250 C0.685 P95 D5 A1 W20 R85 K2 G4 H500 X2
```

Temperature rows:

```text
190, 195, 200, 205, 210, 215, 220, 225, 230 °C
```

Feed speeds:

```text
50, 100, 150, 200, 250, 300 mm/min
```

For 1.75 mm filament these correspond approximately to:

```text
F50  =  2.00 mm³/s
F100 =  4.01 mm³/s
F150 =  6.01 mm³/s
F200 =  8.02 mm³/s
F250 = 10.02 mm³/s
F300 = 12.03 mm³/s
```

This first full-range scan uses at most 54 evaluated speed points and 2.16 m of
commanded test filament (`9 rows × 6 points × 40 mm`), plus recovery purge and
validation only after rows that actually find a limit. A later scan can extend
`U` to 400 or 500 mm/min after the recovery path and 190–230 °C map are proven.

## Point-ID allocation

With recovery enabled, every temperature row reserves one additional point ID.
For six speed points, each row has a stride of seven IDs:

```text
row campaign: base .. base+5
recovery validation: base+6
next row campaign: base+7
```

For the `J2000` / 9-row example, firmware reserves IDs 2000 through 2062.
Unused recovery IDs on clean rows remain reserved. This keeps every point ID
deterministic and prevents a later recovery from colliding with a speed point.

## Row output

`FA8ROW` includes:

```text
recovery_attempted
recovery_passed
recovery_id
recovery_crc
```

A cold row may therefore be retained as a valid limit measurement while the
envelope continues after a successful recovery.

## Safety boundary

Recovery cannot clean filament debris from the hobbed drive gear. If the prime
or validation does not re-establish clean encoder feed, firmware stops as
`RECOVERY_FAILED` and requires inspection. It never treats a failed recovery as
a valid state for the next temperature row.

The prime and cancellation paths use the same bounded segmented-motion horizon:

```text
0.35 mm × 2 blocks = 0.70 mm maximum committed distance
```

## Recommended validation order

Before launching the complete 190–230 °C map:

1. Build and flash Phase 9.
2. Run one standalone `M880` recovery at 230 °C.
3. Run a deliberately small two-row envelope that forces or supervises one
   recovery transition.
4. Run the full 190–230 °C, 5 °C-step scan above.

Queries remain side-effect-free:

```gcode
M880 Q J9000
M870 Q J2000
```
