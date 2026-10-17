# Native Guard / Motor verification — 2026-10-18

Recorded build: Windows x64, MSVC 19.44, C++17, `/O2 /fp:precise /MT /utf-8 /W4`.
The final direct build completed without compiler warnings. The public bridge header was also compiled
as C11 with fixed-size ABI layout assertions. CMake was not installed and was not executed.

| Check | Recorded outcome | Meaning / limit |
| --- | --- | --- |
| Native checks | **8,445 assertions passed** | Includes exhaustive packed-policy cells and many cached-pose samples; this is not 8,445 independent security scenarios |
| Original C# differential output | **7,423 rows / 33,043 compared values passed** | Movement, clocks, temporal model, reference server, complete cache sample subset and foot planner |
| Cross-language cryptography | Passed both envelope directions, user-key / challenge / signature-record equality, RSA manifest verification and tampered-signature rejection | New synthetic state/key only; no production secret or paid model used |
| Actual DLL foreign calls | **50 assertions passed** | Python ctypes loaded the final DLL, exercising opaque handles, rejection paths, buffer negotiation, codec and native Motor world |
| Motor replay | Exact particle positions at 1 and 4 workers in the recorded fixture | One MSVC/platform, not cross-CPU/compiler determinism proof |
| Allocation instrumentation | 0 ordinary `new`/`new[]` in 10,000 protected reads and 120 warmed full-world ticks | 4-actor synthetic fixture; excludes startup, aligned/C/OS allocations and user callback allocations |
| DLL runtime imports | `bcrypt.dll`, `KERNEL32.dll` | No C#/.NET runtime import; direct native implementation |
| OUT FIBER regression | Existing 2,282 assertions passed | Existing FIBER binary was checked; native SDK has not been silently integrated into it |

## Differential coverage

See [PARITY.json](PARITY.json). The Guard movement, clock, temporal and reference-arena outputs have
zero recorded absolute numeric difference. The largest cached-motion component difference is about
4.95e-8; foot-planner positions/velocities differ by at most about 1.47e-5 in the test stream.
Permitted tolerances are documented in the JSON (double 1e-9, motion float 2e-5, feet 2e-4).
Binary authentication/derivation records are compared exactly, rather than by a numeric tolerance.

The C# runner compiles original repository source. Motor's Vector3/Quaternion/Mathf dependencies use a
small explicit test-only arithmetic shim. This is useful formula regression evidence, **not** a run of
Unity's complete runtime/physics, paid authored animations or the installed asset's NUnit suite.
Raw sampled rows remain transient; only aggregate evidence is committed.

## Functional boundaries exercised

Native checks cover rejected movement not poisoning a trusted baseline, simultaneous flags, gaps,
clock drift, context/unseen/unsupported temporal behavior, caller-independent model bytes, scalar
binding/tamper and checked overflow, subscriber exceptions, every envelope-byte mutation, invalid UTF-8,
slot/user derivation, challenge replay/expiry, concurrent budget spending, versioned atomic backup saves,
file root containment, fixed-tick input queues, acceleration, wall sweep, authoritative ray damage,
cooldowns, server stalls, bounded rewind and resynchronization state retention.

Motor checks cover all 128 procedural clips (16 families × 8 variants), finite targets, exact loop seams, int4
and trilinear addressing, bounded event FIFO, shared navigation, full-world stepping, generation-safe
respawn events, severing/crawling, phased injuries, physical swept projectiles/melee, single-hit swings,
obstacle callbacks, int16 authored interpolation/contact bits, stumble intent suppression, RTS resources/
recruitment/decision budgets, blood settling, reserved-slot rendering, archetypes, rig math and bounded
crowd catch-up. These are synthetic fixtures, not statistical evidence across real games.

## No stronger claim follows

No real-game detection rate, kernel/injection/wallhack detector, general terrain collision, full Unity
world/controller/render parity, universal physical stability, C++ speedup or production backend
certification is established. Dense crowd contacts have an explicit candidate cap; controller/world
behavior is reimplemented and only the documented subsets are compared numerically. Existing C# Unity
adapters are retained; the native SDK has not replaced any installed Unity asset.

[SELFTEST.txt](SELFTEST.txt) · [C_ABI.json](C_ABI.json) · [port map](../PORT_MAP.md) · [source hashes](../SNAPSHOT.json).
