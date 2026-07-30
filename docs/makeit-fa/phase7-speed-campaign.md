# Phase 7 — Fixed-temperature automatic speed campaign (`M872`)

Phase 7 is the first automatic campaign layer. It keeps the current hotend
target fixed, waits for thermal stability, and runs a sequence of evaluated
feed points at increasing filament speeds.

It reuses the validated layers below it:

```text
M873  evaluated point
M877  idempotent point transaction
M878  repeatable result query
M879  graceful point abort
```

Each campaign point therefore retains the same encoder monitoring, rolling
feed-loss detection, pulse-gap detection, temperature validation, and terminal
CRC as an individually commanded point.

## Scope

Phase 7 answers one question at the current temperature:

> What is the highest tested filament feed speed that still passes?

It does not change the hotend target. Automatic temperature stepping is the
next campaign layer after this fixed-temperature ladder is validated.

## Command

Set and stabilize the hotend target first:

```gcode
M109 S240
```

Start a three-point campaign at 100, 200, and 300 mm/min:

```gcode
M872 J100 F100 U300 V100 O5 L50 S0.35 B2 I250 C0.685 P95 D5 A1 W20 R85 K2 G4 H500 X2
```

Campaign parameters:

```text
J  campaign ID and first point ID; required
F  starting filament feed speed in mm/min
U  maximum filament feed speed in mm/min
V  speed increment in mm/min
O  required in-band thermal settling time before each point, seconds
```

Point parameters are inherited from `M877`:

```text
L  filament length per speed point
S  segment length
B  maximum in-flight planner blocks
I  point telemetry interval
C  encoder events per physical filament mm
P  terminal passing efficiency percentage
D  temperature tolerance around the captured hotend target
A  rolling monitor enable
W  rolling-window length
R  rolling-window efficiency threshold
K  consecutive bad windows required
G  pulse-gap factor
H  pulse-gap minimum timeout
X  minimum expected missing encoder events
```

The example reserves point IDs:

```text
100, 101, 102
```

The host must choose a fresh contiguous ID range. Firmware rejects a campaign
whose requested range overlaps the currently retained transaction ID.

## Query

```gcode
M872 Q
M872 Q J100
```

Querying is side-effect-free. It never starts or repeats extrusion.

## Cancel

Cancel while waiting for temperature:

```gcode
M872 Z J100
```

Cancel while a point is running:

```gcode
M872 Z J100
```

A running point uses the same graceful bounded drain as `M879`. Bare `M879`
also aborts the current campaign point; the campaign then terminates as
`ABORTED`.

## State machine

```text
WAIT_TEMP
  |
  | target stays unchanged and temperature remains within D for O seconds
  v
RUNNING_POINT
  |
  +-- PASS ---------> next speed -> WAIT_TEMP
  |
  +-- LOW_FEED -----> LIMIT_FOUND
  |
  +-- INVALID_TEMP -> INVALID_TEMP
  |
  +-- ABORTED ------> ABORTED
  |
  +-- ERROR --------> ERROR

PASS at final configured speed -> COMPLETE
```

## Output

Typical campaign records:

```text
FA7: tag=started campaign_id=100 state=WAIT_TEMP ...
FA7: tag=wait_temp campaign_id=100 stable_ms=...
FA7: tag=point_started campaign_id=100 index=0 point_id=100 feed_mm_min=100 ...
FA7: tag=point_result campaign_id=100 point_result=1 point_crc=...
FA7: tag=next_speed campaign_id=100 index=1 point_id=100 feed_mm_min=200 ...
```

Successful completion through the configured maximum:

```text
FA7: tag=max_reached state=COMPLETE
     last_pass=300 first_fail=0
```

First failed speed:

```text
FA7: tag=limit_found state=LIMIT_FOUND
     last_pass=300 first_fail=400
```

Other terminal states:

```text
INVALID_TEMP
ABORTED
ERROR
```

## Idempotency

`M872` retains one RAM-only campaign record.

Repeating the same campaign ID and normalized parameters returns:

```text
FA7: tag=replay ...
```

and does not restart the campaign.

Reusing the same campaign ID with different parameters returns:

```text
FA7: error=PARAM_CONFLICT ...
```

Controller reset clears the campaign record because the physical feed and melt
state are no longer trustworthy.

## Integration

After pulling the branch, run the existing integration helper and the new
Phase-7 helper:

```powershell
python tools\makeit-fa\integrate_phase0.py
python tools\makeit-fa\integrate_phase7.py
```

Then build:

```powershell
py -3.12 -m platformio run -e BIGTREE_SKR_PRO
```

The Phase-7 helper:

```text
includes makeit_fa_campaign.h in MarlinCore.cpp
services transaction -> analyzer -> campaign in that order
declares GcodeSuite::M872()
dispatches case 872
```

## First validation

Use a deliberately small ladder that should pass:

```gcode
M109 S240
M872 J100 F100 U300 V100 O5 L50 S0.35 B2 I250 C0.685 P95 D5 A1 W20 R85 K2 G4 H500 X2
```

Expected:

```text
three automatic evaluated points
point IDs 100, 101, 102
no manual M877 between points
terminal FA7 state COMPLETE
last_pass=300
first_fail=0
```

Then query:

```gcode
M872 Q J100
```

The query must return the same terminal campaign record without movement.

## Current limitations

Phase 7 intentionally stops on the first non-PASS result. It does not yet:

```text
retry INVALID_TEMP points
perform recovery or re-prime after feed failure
change hotend temperature
build the full two-dimensional Qmax(T) envelope
store a multi-campaign result ring
classify failure with TMC load telemetry
```

Those are subsequent layers. The next functional step after Phase-7 validation
is an outer temperature ladder that runs one fixed-temperature speed campaign
at each temperature.
