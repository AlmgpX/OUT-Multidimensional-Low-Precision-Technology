# Native port coverage and remaining engine adapters

This maps actual implementations. It does not assert drop-in source/API compatibility or complete
Unity simulation equivalence. C# reference files are intentionally retained, not disguised as C++.

| Reference source | Native implementation | Scope |
| --- | --- | --- |
| GuardBehavior, GuardClock, GuardEvidence | `guard_behavior.cpp`, `out_guard.hpp` | Packed policy, accepted movement baseline, clock/time-scale/risk and local notifications |
| GuardTemporalRisk | Same files | Fixed-format model reader and temporal hypotheses; shipped paid baseline excluded |
| ProtectedValues | `out_guard.hpp`, `guard_crypto.cpp` | Runtime scalar tamper evidence, binding, rekey and checked arithmetic; layout is not a cross-language storage format |
| GuardCodec | `guard_crypto.cpp` | Original authenticated envelope/key derivation; Windows atomic file saves/backups/revisions |
| GuardHandshake | Same file | Original binary authentication record, server session and token budgets; HTTP/JSON/TLS transport belongs to host |
| GuardIntegrity | Same file | File hashing, signed manifests, path containment and open-handle verification |
| GuardAuthoritativeWorld | `guard_server.cpp` | Original bounded static-box fixed-tick reference arena and ray combat |
| Existing Motor native solver | `OUTMotorNative.cpp/.h` | Existing solver retained; one shadowing local renamed, without algorithm change |
| OUTMotorData algorithm excerpt, MotionCache | `motor_motion.cpp` | Signed nibble / 3D field / 4D response addressing and all 128 procedural cache formulas; caller supplies bank |
| AnimationBank | `AuthoredClip` in same file | Validated float or int16 target frames, metadata/contact sampling; Unity asset/editor baking remains engine tooling |
| FootPlanner, FlowField | `motor_navigation.cpp` | Persistent foot targets/IK, shared grid fields, host-supplied obstacle query |
| EventQueue, World, InjuryScheduler | `out_motor.hpp`, `motor_world.cpp` | Bounded generation-safe events, world/state/targets/response, injury phases and support-loss handling |
| MeleeCombat, Projectiles | `motor_combat.cpp` | Physical swept contacts and delayed world events, one-hit swing rule, fixed projectiles; obstacle callback belongs to host |
| RTSSimulation | `motor_rts.cpp` | Orders, quantized headings/goals, bounded decisions/probes, finite resource ledger and recruitment |
| Crowd, Archetype, BloodPool | `motor_view.cpp`, `motor_combat.cpp` | Native orchestration, tuning, volleys, blood pool; no MonoBehaviour dependency |
| RigView | `RigBinding` in `motor_view.cpp` | Mapped bind-pose/world transforms and smoothing; host supplies joint mapping and applies output to its scene |
| InstancedRenderer, EquipmentRenderer | Capsule/equipment output helpers | Renderer-neutral CPU transforms only. Unity materials, generated Unity meshes, shaders, buffers, skinned objects and draw calls are not ported into a new graphics backend |

## Deliberate integration differences

- The complete native SDK is currently Windows x64 because its crypto backend uses CNG.
- C++ callers pass explicit data structures instead of Unity ScriptableObject assets. Bank/model
  payloads and all editor/corpus fitting tools remain excluded from this repository.
- Native crowd proxy contacts use a bounded 64-candidate search per actor every second solver tick.
  This bounds CPU but may miss dense contacts. It is not an exact copy of every Unity crowd contact.
- The state/target/controller port is verified through functional scenarios. Only the documented
  differential subsets are numerically compared with original C#; complete World/Crowd/render parity
  against Unity and paid authored animation has not been established.
- `Crowd` and `RTS` are alternative controllers with exclusive notification ownership. Host callbacks
  and simulations have explicit lifetimes/thread contracts. The C ABI exports a practical subset;
  the full class API is available to C++ consumers of the static library.
- Existing Unity C# adapters continue to use their existing implementation until a host explicitly
  connects the native API. No current Unity project, publisher-frozen model or installed asset changed.

The native SDK is an implementation delivery, not a security upgrade claim merely because its language changed.
