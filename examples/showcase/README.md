# QL/32: The Grand Tour (work in progress)

A guided showcase of QL/32 (plan: `docs/plans/showcase-demo-plan.md`). It is opt-in: it is
not `startup.bas`.

## Files

| File | Role |
| --- | --- |
| `showcase.bas` | Director: runs foreground (`RUN`) because `TASKVIEW` is supervisor-only. |
| `gt-layout.bas` | Display size from `SYSINFO$(3)`, tile size helpers. INCLUDEd, so kept tiny. |
| `gt-title.bas` | Block-letter title proc used by the boot and finale screens. |
| `gt-s01-palette.bas` | Scene 1: concentric rectangles, `PALROT`, `PALFADE`. |
| `gt-s02-fonts.bas` | Scene 2: four fonts and a `GCOPY` scroller. |
| `gt-s03-double.bas`, `gt-s03-single.bas`, `gt-mathbody.bas`, `gt-optsingle.bas` | Scene 3: the same maths under binary64 and `OPTION KOptMathSingle`, side by side. |
| `gt-s06-parallax.bas` | Scene 4: three-layer procedural parallax with `GCOPY`. |

## Run

Copy every `gt-*.bas` and `showcase.bas` to `FFS:/`, then:

```
LOAD "FFS:/showcase.bas"
RUN
```

Keys: `SPACE` or `N` next, `P` previous, `Q` quit. Layout is computed at run time, so one
source serves every board; only the CYD and Tenstar are hardware-verified.

## Test mode

`autoSecs > 0` advances scenes without keys; `holdScene = N` ends the run with `STOP` while
scene N stays on screen for 6 seconds before `STOP`, so `SCREEN` can capture it. The host test
(`testShowcaseBootSceneIsCentredAtDefaultResolutions`) checks the boot scene is centred at six
resolutions.
