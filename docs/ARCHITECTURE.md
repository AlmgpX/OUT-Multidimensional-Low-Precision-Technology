# Architecture and implementation map

A common design direction is to keep reusable policy/storage compact, decode at a
controlled boundary, and retain the precision/state needed for stable continuous work.
The table dimensions and uses differ between the two products.

## OUT Motor

1. The licensed data bank packs 17 poses as signed bytes, a 3x3x3 vector field as signed
   nibbles, and a 4x4x4x4 response policy as 256 unsigned nibbles (128 bytes).
2. `Reference/OUTMotorData.algorithms.cs.txt` exposes signed-nibble decoding, packed
   response indexing and trilinear sampling, without the underlying arrays or gain bank.
3. `Runtime/OUTMotorMotionCache.cs` shares procedural motion targets rather than
   allocating a clip set per actor. Expanded samples use floats; not every cache is int4.
4. `Runtime/OUTMotorWorld.cs` drives active targets, contacts and continuous state.
   `OUTMotorFootPlanner.cs` handles foot targets; `OUTMotorInjuryScheduler.cs` and
   `OUTMotorEventQueue.cs` bound scheduled work and ingress.
5. `Runtime/OUTMotorRTSSimulation.cs` uses coarse command/heading vocabularies and bounded
   decision work. Quantized commands are not quantized particle integration.
6. `Native/OUTMotorNative.cpp` and `.h` provide the C++17 batched solver/C ABI. Position,
   velocity, constraints and contacts remain floating point. This is not an int4 solver.
7. The paid product can fit a finite profile vocabulary to a synthetic teacher.
   The fitting tool, teacher generator, outputs and corpora are withheld here.
   Fitting error is not proof of physical stability or reinforcement learning.

Paths above are relative to `Source/OUTMotor/`. Unity-facing code requires the licensed
product's withheld `OUTMotorData` and the Unity environment. This is deliberately not a
complete importable asset. No private Mixamo or third-party animation data is included.

## OUT Guard

1. `Core/GuardBehavior.cs`: four normalized features, eight bins each, 4096 cells;
   one four-bit profile per cell occupies 2048 bytes. The default static policy is
   constructed by a visible heuristic, not a learned model.
2. `Core/GuardTemporalRisk.cs`: a separate temporal feature schema and five packed
   tables for normal/lag/anomaly/uncertainty/support, a fixed model reader, per-actor
   history, bounded intervals and explicit unsupported/unseen states.
3. `GuardBuiltInTemporalModel.cs` is deliberately absent: it embeds the paid release's
   actual packed baseline. Model format code does not grant or deliver that payload.
4. `Core/GuardAuthoritativeWorld.cs`: fixed-tick reference command integration and
   static-box geometry. It is not arbitrary Unity collision simulation or a hosted service.
5. `Core/ProtectedValues.cs`, `GuardIntegrity.cs`, `GuardHandshake.cs`: local tamper
   evidence, integrity primitives and protocol checks with explicit trust boundaries.
   A client-held secret does not prove an uncompromised client.

Paths above are relative to `Source/OUTGuard/`. The core subset uses .NET APIs without
Unity. Validation fixtures do not reproduce the excluded publisher-trained artifact.

## Evidence over time, without magical guarantees

Temporal accumulation can distinguish a sequence from one noisy sample and expose
uncertainty. It does not guarantee that repeated subtle cheating will be found. Actor
history is mutable; the separately shipped baked model is fixed. Review hints must not
be treated as automatic bans, authoritative movement acceptance or proof of cheating.

Memory protection cannot stop a process owner patching callers. Speed/acceleration
limits alone cannot prove that movement did not pass through a wall. Motor's flat-floor
reference contacts are not a general terrain/collision engine. Integration and target
platform validation remain the developer's responsibility.
