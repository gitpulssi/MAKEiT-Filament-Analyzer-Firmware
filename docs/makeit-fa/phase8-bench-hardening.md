# Phase 8 bench hardening — thermal settling and command length

The first two-temperature `M870` bench run validated the envelope state machine but exposed two input / settling defects.

## What passed

The 230 °C row completed automatically:

```text
point 1000 / F100: PASS, 102.19%
point 1001 / F200: PASS, 97.32%
row 0: COMPLETE, last_pass_feed=200, q_pass_mm3_s=8.02
```

Point IDs, nested campaigns, row storage, volumetric conversion, and original-target restoration all behaved correctly.

## Defect 1 — the point started with thermal momentum

The point-validity tolerance was `D5`. The original Phase-7 gate also used that same ±5 °C band to start the five-second settling timer. On the 230 → 240 °C transition the timer therefore began at about 235.7 °C while the hotend was still climbing.

The first 240 °C point started at about 240.76 °C and then overshot to 247.52 °C:

```text
FA2: result=INVALID_TEMP ... temp_min=240.71 temp_max=247.52
```

This is not a filament-flow failure. It is a premature thermal-start decision.

### Fix

The speed campaign now derives a separate tight settling band:

```text
settle_band_c = min(point_temp_tolerance_D, 1.0 °C)
```

Within that band it tracks the observed temperature span. The `O`-second dwell restarts whenever the span exceeds the tight band. A point starts only when both conditions hold continuously:

```text
abs(temp - target) <= settle_band_c
temperature span during dwell <= settle_band_c
```

`FA7` wait reports now include:

```text
settle_band_c
stable_span_c
```

The wider `D` value remains the point-validity tolerance during extrusion; it no longer acts as the thermal-stability gate.

## Defect 2 — the M870 line exceeded Marlin's command buffer

The explicit test command was 100 characters long, while this Marlin configuration used:

```text
MAX_CMD_SIZE = 96
```

The line was silently truncated after:

```text
... G4 H50
```

Therefore the requested `H500` reached the pulse-gap monitor as `H50`. The omitted `X2` happened to equal the default, so only `H` visibly changed.

### Fix

The integration hardening raises:

```text
MAX_CMD_SIZE = 192
```

This comfortably holds explicit analyzer commands plus optional host line numbers and checksums on the STM32F4 target.

## Integration

```powershell
git pull --ff-only
python tools\makeit-fa\integrate_all.py
py -3.12 -m platformio run -e BIGTREE_SKR_PRO
```

`integrate_all.py` now runs `integrate_phase8_hardening.py` after the Phase-8 hook installer.

## Repeat validation

```gcode
M109 S230
M870 J1100 T240 E10 Y1.75 F100 U200 V100 O5 L30 S0.35 B2 I250 C0.685 P95 D5 A1 W20 R85 K2 G4 H500 X2
```

Use a new envelope / point-ID range because the old `J1000` record is retained until reset.

Acceptance criteria:

```text
FA7 wait reports settle_band_c=1.00
240 °C point does not start on the first target crossing
stable_span_c remains <=1.00 for five seconds before point start
FA4 reports min_gap_ms=500, not 50
both 230 °C and 240 °C rows complete
four unique point IDs are used: 1100..1103
final envelope state is COMPLETE
the original 230 °C target is restored
repeated M870 Q J1100 returns identical rows and envelope_crc
```
