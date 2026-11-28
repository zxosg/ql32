# Grand Tour phase 2 results (2026-11-28)

Scenes 1-4 (palette, fonts, math twins, parallax) plus the end title, run by `showcase.bas` in test mode.

| Scene | CYD free / largest (running) | Tenstar free / largest |
| --- | --- | --- |
| baseline | 134.8K / 73.7K | 2.29M / 2.06M |
| 1 palette | 74.9K / 38.9K | 2.24M / 2.03M |
| 2 fonts | 73.7K / 30.7K | 2.22M / 2.00M |
| 3 math twins (2 tasks) | 61.0K / 27.6K | 2.18M / 2.00M |
| 4 parallax | 72.4K / 30.7K | 2.17M / 1.97M |

After each scene the heap returns to 135K free on the CYD but the largest block stays at 28-30K after scenes 2-3 (gap 17). Running scenes back to back without a reset made scene 4's 28.8K canvas fail, so the sequential tour on the CYD needs a mitigation in a later phase. Parallax runs at about 8 fps on the CYD.

Screenshots were captured with `@SCREEN` for both boards; the Tenstar leftovers visible at the bottom are the test script killing tasks, not the director.
