# Actual local results — Tales 0.2.0

Final direct MSVC x64 build, **2026-10-18 Europe/Minsk**, Ryzen 7 3700X (8 cores / 16 threads),
Windows 11 Pro, `/O2 /fp:precise /MT /utf-8 /W4`, C++17. One final local run per row;
background load is uncontrolled. No screenshot capture ran concurrently in this recorded suite.
Seed 41917, 120 warmup ticks; measured lengths are in each linked JSON. Headless CPU only, **not FPS**.
The bigger world, homes, schedules and distance tiers differ from v0.1; do not compare rows as the same workload.
No player crafting/building is exercised by these timing runs; functional tests cover those actions separately.

| Actors | Policy | Workers | Schedule | Ticks | p50 ms | p95 ms | p99 ms | Decisions | Capped observations |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| [256](256-exploratory-1w-lod.json) | EXPLORATORY | 1 | Distance LOD | 1200 | 0.0415 | 0.0460 | 0.0481 | 9,067 | 893 |
| [4,096](4096-exploratory-1w-lod.json) | EXPLORATORY | 1 | Distance LOD | 1200 | 0.6066 | 0.6636 | 0.7725 | 116,291 | 107,583 |
| [4,096](4096-exploratory-8w-lod.json) | EXPLORATORY | 8 | Distance LOD | 1200 | 0.2608 | 0.2915 | 0.3474 | 116,291 | 107,583 |
| [4,096](4096-exploratory-8w-no-lod.json) | EXPLORATORY | 8 | No distance LOD | 1200 | 0.7558 | 0.9121 | 0.9789 | 813,444 | 714,673 |
| [4,096](4096-strict-8w-lod.json) | STRICT | 8 | Distance LOD | 1200 | 0.2589 | 0.3026 | 0.3663 | 116,133 | 108,011 |
| [4,096](4096-utility-8w-lod.json) | UTILITY | 8 | Distance LOD | 1200 | 0.2636 | 0.2970 | 0.3802 | 116,038 | 107,733 |
| [16,384](16384-exploratory-8w-lod.json) | EXPLORATORY | 8 | Distance LOD | 600 | 0.8780 | 1.0122 | 1.1223 | 240,390 | 237,392 |
| [65,536](65536-exploratory-8w-lod.json) | EXPLORATORY | 8 | Distance LOD | 300 | 4.8323 | 5.4581 | 5.7526 | 473,282 | 473,236 |
| [4,096](4096-exploratory-8w-full-ai.json) | EXPLORATORY | 8 | 60 Hz all | 600 | 1.7747 | 2.2407 | 2.3333 | 2,451,905 | 2,163,903 |
| [16,384](16384-exploratory-8w-full-ai.json) | EXPLORATORY | 8 | 60 Hz all | 300 | 6.5735 | 8.5611 | 8.9798 | 4,900,775 | 4,626,403 |
| [65,536](65536-exploratory-8w-full-ai.json) | EXPLORATORY | 8 | 60 Hz all | 180 | 37.4179 | 43.6134 | 45.8079 | 11,779,033 | 11,686,590 |

## What these numbers establish

All 11 reports end with `valid: true` and zero tracked ordinary C++ `new`/`new[]` during measured
steps. This excludes C/OS allocations. Owned bytes exclude stacks, allocator and OS/GUI resources.
The 4,096-actor exploratory LOD run has identical replay at 1 and 8 workers. Timing variation is
expected; portability of floating-point replay has not been established.

Distance tiers use near/mid/far cognition at 10 / 2 / 0.25 Hz and movement at 60 / 15 / 2 Hz.
Sleeping actors use 0.5 Hz cognition and 1 Hz movement. `--no-lod` retains sleep scheduling;
`--full-ai` forces 60 Hz everywhere. Thus an LOD row is not equivalent to 60 Hz decisions for every NPC.
Perception budgets are bounded at 192 / 64 / 16 candidates. Especially at high populations,
`cappedPerceptions` is substantial: part of the neighborhood is not observed. These are approximate
crowd workloads, not exact all-pairs social/physical simulation. The world has only 128 shared homes.

The inference bank is 23,168 bytes; Mind is 52 bytes, Actor is 216 bytes. Model checksum:
`14534943826093559650`. New synthetic reference fitting runs once at startup; policies stay read-only.

## Functional validation

[Selftest output](SELFTEST.txt): **2,282 assertions passed**. Includes action safety, seeded
ambiguity, actual worker-count replay, zero tracked step allocations, wall/finite state checks,
128 accessible homes, doorway navigation, sleep recovery, reduced far cognition, local conversation
partners and bonds, stress cues, original quest branches/rewards, inventory/trade, witnesses,
finite resource collection, tool recipes, invalid/overlapping construction without spending,
watered crop growth/one-time harvest, camp storage conservation, and full-state save/load with
corruption rejection. Blood uses a fixed 1,024-slot pool.

Synthetic held-out branch diagnostic: 3,472 matches / 4,000 total sampled queries. Safety-ineligible
teacher cases remain in the denominator and are not counted as matches. This is neither a calibrated
accuracy score for real play nor evidence of general intelligence. The original historical Lab 0.1
14-check gate was not reproduced.

Saves target the current build only. A checksum detects ordinary corruption; it is not authentication.
NPC memories and social links are short and bounded; dialogue is authored; camp objects are nonblocking.
Full economy, voxel excavation, exact crowd contacts, arbitrary terrain changes, streaming, multiplayer,
and other-platform validation remain outside this release.

The publication audit separately checks every source/script/build first line for the license identifier,
source and screenshot hashes, and forbidden paid data/binary paths. It is a scoped allowlist/payload
check, not legal certification or a universal secret scanner. [Hardware](HARDWARE.json).
