# Actual local measurements

Final native C++ build, 2026-10-17. AMD Ryzen 7 3700X (8 cores / 16 logical processors),
MSVC x64 `/O2 /fp:precise /MT`, Windows. One local run per row; background load was not
controlled. All rows use seed 41917 and 120 warmup ticks. Different row lengths are
explicitly recorded in each JSON. These are headless simulation timings, not FPS.

| Actors | Mode | Workers | Cognition | p50 ms | p95 ms | p99 ms | Alive at end | Actual decisions | Capped perceptions |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 512 | EXPLORATORY | 1 | 10 Hz | 0.178 | 0.199 | 0.246 | 499 | 101,528 | 0 |
| 4,096 | EXPLORATORY | 1 | 10 Hz | 2.352 | 2.491 | 3.146 | 3,783 | 789,205 | 8,318 |
| 4,096 | EXPLORATORY | 8 | 10 Hz | 0.622 | 0.686 | 0.782 | 3,783 | 789,205 | 8,318 |
| 4,096 | OVERLAP+MEMORY | 8 | 10 Hz | 0.620 | 0.670 | 0.774 | 3,781 | 789,373 | 6,696 |
| 4,096 | STRICT | 8 | 10 Hz | 0.610 | 0.681 | 0.772 | 3,714 | 784,224 | 11,408 |
| 4,096 | UTILITY | 8 | 10 Hz | 0.599 | 0.675 | 0.735 | 3,828 | 795,719 | 0 |
| 4,096 | EXPLORATORY | 8 | 60 Hz | 1.672 | 1.775 | 1.837 | 3,939 | 2,424,108 | 0 |
| 16,384 | EXPLORATORY | 8 | 10 Hz | 3.306 | 3.486 | 3.625 | 15,267 | 1,599,626 | 1,355,640 |
| 16,384 | EXPLORATORY | 8 | 60 Hz | 10.257 | 10.526 | 10.681 | 16,085 | 4,889,748 | 4,213,292 |
| 65,536 | EXPLORATORY | 8 | 10 Hz | 15.084 | 16.337 | 16.913 | 65,525 | 3,276,702 | 3,275,960 |
| 65,536 | EXPLORATORY | 8 | 60 Hz | 42.240 | 44.902 | 47.585 | 65,533 | 11,796,372 | 11,794,393 |

All reports passed finite-state/static-wall checks and measured zero ordinary C++
`new`/`new[]` allocations during the measured steps. Inference storage is 11,392 bytes;
policy memory is 52 bytes per actor (additional physical/social state is separate).
The complete simulation replay digest matched between the 1-worker and 8-worker
4,096-actor runs. See SELFTEST.txt for the 4,473-assertion integrated suite.

At 65,536 actors with 60 Hz cognition, p95 is about 45 ms: this does **not** meet a
16.67 ms real-time budget on this CPU. Almost every perception reaches the 192-probe
cap at that density. The measured speed therefore comes with incomplete local sensing,
not omniscient behavior. At 10 Hz cognition the 65,536-actor p95 is about 16.3 ms;
rendering and other application work are additional.

Actors can die during combat. Alive counts and actual decision counts expose the
changing workload. Modes can produce different trajectories; their timing differences
do not establish decision-quality superiority. Heals, hits, rumor receptions and actions
are reported even when a particular run observes zero events of a type.

World and bank initialization, temporary calibration scratch, report formatting and
rendering are outside the measured tick. Owned bytes are an estimate from containers,
not process resident memory; stacks, allocator/OS overhead and GUI are excluded.

The procedural teacher match in SELFTEST.txt is a diagnostic, not an intelligence
benchmark, a real-game detection metric or a reproduction of the old Lab 0.1 report.
