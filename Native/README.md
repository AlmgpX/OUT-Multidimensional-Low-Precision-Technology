# OUT Guard + OUT Motor — Native C++17 SDK 0.1

**Native computational cores, a C ABI DLL and static library. No C# runtime is needed to use them.**
Architecture, original product concept and direction: **Alex Merqury**.
Rights holder: **Алексей Савельев**. OpenAI Codex assisted with implementation and verification.
[Repository license](../LICENSE) applies; this source review publication is not a free project-use,
SDK redistribution or AI-training grant. [По-русски](README.ru.md).

This is a separate native SDK, **not an automatically converted/importable Unity asset**. Original C#
sources remain as reference implementations and Unity integration code. Inspector, GameObject,
prefab, humanoid discovery, material/shader and GPU submission still require an engine adapter.
The current Unity project and the paid asset deliveries have not been replaced by this SDK.

## OUT Guard in C++

- Protected int64/int32/float/bool values, field binding, randomized per-value masks and process-keyed
  HMAC tags. Checked integer arithmetic preserves binding and rejects overflow. Local tamper evidence
  still cannot prevent the process owner patching callers, keys or notifications.
- A real four-feature, 4,096-cell packed behavior table: **2,048 bytes**, with a shared immutable
  default policy. Per-actor movement trackers hold their own accepted baseline and velocity.
  Invalid/replayed samples, gaps and rejected positions never authorize a new trusted baseline.
- Independent-clock drift windows, pause/focus continuity, declared time-scale checking, decaying
  evidence and exception-isolated local notifications. Evidence does not automatically ban a player.
- Reader for the fixed five-table temporal model and per-actor temporal hypotheses, explicit unseen
  states, context checks and unsupported gaps. **The paid shipped baseline is not included**; load
  only a developer-authenticated model. Its SHA256 checksum alone is not authenticity verification.
- AES-256-CBC + HMAC-SHA256 encrypt-then-MAC, distinct derived encryption/authentication keys,
  slot/user binding, bounded UTF-8, atomic saves, backup reads and trusted revision floors.
  Envelopes, key derivation, challenge authentication and signed-file records interoperate with the
  original C# format. Protected in-memory structs are runtime-only and are not a persisted wire format.
- Per-session challenge authentication with replay/expiry checks, synchronized delta budgets,
  SHA256 file checks and RSA/SHA256 signed manifests. Native file verification hashes an open handle
  after checking its resolved path under the trusted root; reparse paths are rejected.
- A fixed 60 Hz reference server: bounded input queues/catch-up, gravity, jump/dash limits,
  swept axis-aligned bodies/walls, server ray damage, ammo/fire cooldowns and bounded server-owned rewind.
  Resynchronization returns server state, preserving velocity, health, ammunition and cooldowns.

Windows cryptographic primitives are provided by [CNG](https://learn.microsoft.com/en-us/windows/win32/seccng/cng-portal).
The native SDK currently targets **Windows x64**. No Linux/macOS/mobile crypto backend is supplied or
claimed tested. Use authenticated transport and server-owned game rules; a client-held secret is not
remote attestation. There are still no wallhack, injection, aimbot or kernel-level detectors.

## OUT Motor in C++

- The existing C++ batched 18-particle / 30-link solver is retained: continuous float positions,
  velocities, constraints, joint limits, self-contact and a flat-floor reference contact model.
- Native world ownership, actor generations, bounded ingress, damage/severing, human support-leg
  loss/crawling, falling/staged recovery, motor-response hysteresis and multirate target updates.
- Exact translated formulas for the **128-clip shared procedural cache**, including gait, family
  variants, loop seams and foot contact phases; persistent world-space foot planning and leg IK.
- Caller-supplied authored float or **int16 millimetre** target clips, contact bits, family lookup,
  hit/stumble overlays and named sword/stumble targets. No authored animation is shipped here.
- Signed-int4 decoding, trilinear 3D fields and packed 4D response indices. The data API requires the
  caller's licensed pose/field/response/gain bank; no replacement payload is hidden in the native source.
- Shared two-goal flowfields with a host overlap callback; phased injury audits; swept physical
  melee/projectiles, one melee hit per swing, generation-safe queued events and a fixed projectile pool.
- Native crowd orchestration, archetype tuning, bounded catch-up, volleys, blood particles, RTS
  move/gather/return/attack decisions, finite resources, deliveries and resource-funded recruitment.
- Renderer-neutral capsule/equipment packets and bind-pose retargeting/smoothing. These calculate
  transforms; an engine still draws meshes and materials. `Crowd` and `RTS` are alternative controllers
  that each own their world's combat notification slots; do not attach both simultaneously.

The native `World` copies/fixes its input bank at construction and shares motion storage among its actors.
It does not train or silently adapt policy weights. Targets, contacts and dynamics remain floats;
this is not an int4 physics engine. Crowd proxy contacts examine at most 64 candidates per actor and
expose `bodyProbes` / `bodyBudgetReached`; dense contacts may be missed. Projectile broadphase still
visits actors and only narrows nearby joints; it is not a proven unlimited-scale collision engine.
Static obstacle collision for projectiles is supplied through `obstacleFraction`; arbitrary terrain,
Unity colliders and host navigation/physics integration are not supplied automatically.

## Build and use

From the repository root, with MSVC x64 C++ tools and Windows SDK:

```bat
Native\tools\build_windows.cmd E:\OUT_NATIVE_BUILD
E:\OUT_NATIVE_BUILD\out_native_checks.exe
```

The script discovers VS2022 Community, or use an x64 Native Tools prompt. Recorded compilation uses
C++17, `/O2 /fp:precise /MT /utf-8 /W4`; the C header is separately compiled as C11.
Outputs are `out_native_core.lib` (C++ static SDK), `out_native.dll` and `out_native.lib` (C ABI import
library), plus the check runner. CMake source is also provided: `cmake -S Native -B build/native`, then
`cmake --build build/native --config Release`. **CMake was not executed on this host**; only the direct
MSVC path is recorded. No .NET, Python or Unity is required to run a consumer of the native DLL.

C++ consumers include [out_guard.hpp](../Source/OUTGuard/Native/out_guard.hpp) or
[out_motor.hpp](../Source/OUTMotor/Native/out_motor.hpp), and link the static SDK plus `bcrypt.lib`.
Use a compatible MSVC x64 `/MT` toolchain for this static build. A Motor world needs a complete valid
licensed `DataBank`; construction rejects an absent/degenerate bank. The small synthetic test fixture
is a regression fixture, not production motion data or a free replacement for the paid bank.

Foreign-language/engine hosts use [out_native_c.h](include/out_native_c.h). ABI version 1 exposes
movement tracking, authenticated envelopes, native Motor world creation, commands/events, stepping
and particle output. The fuller server/temporal/crowd/RTS APIs are C++ APIs; not every class is exported
through the C bridge. All handles are opaque, same-process host-owned lifetimes. Destroy them with the
matching exported destroy function; never pass arbitrary pointers or use a handle after destruction.
Do not concurrently destroy or mutate a simulation handle while it is being used.

ABI calls return 1 on success, 2 when an output buffer is too small (required length is returned),
and 0 on rejection/error. Exceptions stay inside the ABI. Error text is thread-local and borrowed until
the next call on that thread. Input/output buffers are caller-owned; the Motor bank is copied before
returning from creation. The bridge is not a network trust boundary or process sandbox.

## Verification and limits

```text
python Native/tools/verify_reference.py --executable E:/OUT_NATIVE_BUILD/out_native_checks.exe
python Native/tools/verify_c_api.py --dll E:/OUT_NATIVE_BUILD/out_native.dll
python Tools/audit_publication.py
```

The reference runner additionally requires .NET 10 to compile **original C# source** for differential
tests; the Python/C# tools are build-time verification only. Raw synthetic observations, encrypted test
files and RSA test keys remain transient outside Git. Only aggregate evidence is published.

Read [actual results](results/RESULTS.md), [coverage map](PORT_MAP.md) and [source hashes](SNAPSHOT.json).
The checks do not establish real-game cheat detection, full Unity behavior equivalence, physical
stability for every authored bank, a C++ speedup, production backend readiness or industry certification.
The original frozen OUT Guard model and all paid datasets remain unchanged and excluded.

Support and licensing: [OUT Discord](https://discord.gg/qpa2XdA6uT).
