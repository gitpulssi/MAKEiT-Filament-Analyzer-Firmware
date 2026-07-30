# OctoPrint controller 0.2.3: invalid-temperature row continuation

## Observation from serial (57)

A user-selected grid ran:

```text
temperature: 190..215 C, step 5 C
feed speed: F100..F500, step F50
conditioning: 50 mm
measurement: 200 mm
P97 / R85 / D3
```

The controller completed rows at 190, 195, 200, 205, and 210 C. Four recovery
cycles at 220 C completed successfully. At 215 C, the F400 point completed its
200 mm feed with 94.89% encoder efficiency but recorded a minimum temperature
of 212.00 C. The point therefore ended as `INVALID_TEMP` at the lower edge of
the selected 215 +/- 3 C qualification band.

Controller 0.2.2 treated any `INVALID_TEMP` row as terminal for the whole map.
That is too strict for a general temperature x speed experiment. The event is a
valid measured cell and a row boundary. It is not a Marlin thermal-protection
fault.

## Version 0.2.3 behavior

Default settings:

```text
continue_after_invalid_temp = true
recover_after_invalid_temp = true
```

When an `M872` row ends in `INVALID_TEMP`:

1. Retain the purple `INVALID_TEMP` point and row record.
2. Checkpoint JSON and CSV data.
3. Record the row in `invalid_temperature_rows`.
4. If another selected temperature remains and recovery is enabled, run `M880`.
5. Continue with the next temperature after recovery passes.
6. If recovery is disabled, continue directly to the next temperature row.
7. If the invalid point occurs in the final temperature row, finish as
   `COMPLETE_WITH_INVALID_TEMP` and turn the heater off.

Setting `continue_after_invalid_temp` to false restores strict stop-on-invalid
behavior. Setting `recover_after_invalid_temp` to false continues directly
without `M880`.

## Separation of failure classes

```text
INVALID_TEMP
  User-selected D band was exceeded. Preserve the cell and optionally continue.

Marlin thermal fault
  Existing Marlin thermal protection stops the controller. Never treated as a
  normal graph boundary.

LOW_FEED below R or incomplete point
  Hard flow boundary. Existing recovery behavior remains unchanged.
```

## Target-machine test

Use a small grid with a deliberately tight D value so one row produces an
`INVALID_TEMP` point before the final selected temperature:

```text
temperature: 210, 215, 220 C
speed: F350, F400
D: 2 C
recovery: enabled at 220 C
```

Acceptance sequence:

```text
FA7 state=INVALID_TEMP
OctoPrint log: action=recover
FA9 state=COMPLETE
next M109 / M872 row starts
final status COMPLETE or COMPLETE_WITH_INVALID_TEMP
heater target returns to 0 at terminal completion
```
