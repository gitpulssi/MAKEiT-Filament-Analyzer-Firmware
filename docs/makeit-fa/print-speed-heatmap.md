# Recommended printing-speed heatmap

OctoPrint controller 0.2.4 adds a second measured-cell heatmap that converts
measured delivered volumetric flow into linear printing speed.

## Why nozzle diameter alone is not enough

Linear printing speed depends on the deposited bead cross-section:

```text
bead area = line width x layer height
```

The controller therefore uses three user-adjustable visualization inputs:

```text
line width, mm
layer height, mm
slicer safety factor, percent
```

The `Use nozzle defaults` button sets:

```text
line width  = nozzle diameter
layer height = nozzle diameter / 2
```

These are starting values only. The user can enter the actual slicer line width
and layer height for the intended print.

## Formula

```text
recommended print speed, mm/s
  = delivered flow, mm3/s
  x safety factor / 100
  / (line width, mm x layer height, mm)
```

A safety factor of 100 percent displays the direct measured-flow equivalent.
The default 90 percent leaves operating margin for normal printing transients.

## Cell colors

```text
Green  = measured efficiency met P
Amber  = below P but still above R
Red    = hard throughput failure
Purple = invalid temperature
Gray   = untested
```

The cell number is the calculated linear print speed in mm/s. A green cell is
the appropriate region for selecting a normal slicer speed. Amber cells show
physical throughput that did not meet the selected printing-accuracy threshold.

## Example

For a 0.6 mm line width, 0.3 mm layer height, 90 percent safety factor, and
20 mm3/s delivered flow:

```text
speed = 20 x 0.90 / (0.6 x 0.3)
      = 100 mm/s
```

The print-geometry values are stored in the run definition so a saved run can
recreate the same visualization. Older saved runs default to the stored nozzle
diameter for line width, half the nozzle diameter for layer height, and 90
percent for the safety factor.
