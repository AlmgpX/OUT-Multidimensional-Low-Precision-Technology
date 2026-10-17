// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using System.Collections.Generic;



namespace OUT.Guard

{

    [Serializable] public struct GuardVector

    {

        public double x, y, z;

        public GuardVector(double x, double y, double z) { this.x = x; this.y = y; this.z = z; }

        public static GuardVector operator +(GuardVector a, GuardVector b) => new GuardVector(a.x + b.x, a.y + b.y, a.z + b.z);

        public static GuardVector operator -(GuardVector a, GuardVector b) => new GuardVector(a.x - b.x, a.y - b.y, a.z - b.z);

        public static GuardVector operator *(GuardVector a, double b) => new GuardVector(a.x * b, a.y * b, a.z * b);

        public bool IsFinite => GuardRisk.Finite(x) && GuardRisk.Finite(y) && GuardRisk.Finite(z);

    }

    [Serializable] public struct GuardBox

    {

        public GuardVector min, max;

        public GuardBox(GuardVector min, GuardVector max)

        {

            if (!min.IsFinite || !max.IsFinite || min.x >= max.x || min.y >= max.y || min.z >= max.z ||

                Math.Abs(min.x) > 1000000 || Math.Abs(min.y) > 1000000 || Math.Abs(min.z) > 1000000 ||

                Math.Abs(max.x) > 1000000 || Math.Abs(max.y) > 1000000 || Math.Abs(max.z) > 1000000) throw new ArgumentException("Invalid server collision box.");

            this.min = min; this.max = max;

        }

    }

    [Serializable] public struct GuardServerInput

    {

        public long sequence, acknowledgedTick;

        public int epoch;

        public double moveX, moveZ;

        public bool jump, dash;

    }

    [Serializable] public sealed class GuardServerSnapshot

    {

        public long tick, acknowledgedInput, nextInputSequence, nextShotSequence;

        public int epoch, health, ammo;

        public bool grounded, resyncRequired;

        public GuardVector position, velocity;

    }

    public enum GuardInputStatus { Queued, InvalidInput, SequenceMismatch, StaleAcknowledgement, QueueFull, ResyncRequired, Dead }

    public enum GuardShotStatus { Hit, Miss, Blocked, InvalidAim, SequenceMismatch, Cooldown, NoAmmo, ResyncRequired, Dead }

    [Serializable] public struct GuardShotResult { public GuardShotStatus status; public string hitActor; public int damage, remainingAmmo; public long tick; }



    /// <summary>

    /// Bounded fixed-tick reference simulation. Only commands cross the trust boundary.

    /// Axis-aligned box bodies/obstacles, gravity, jumping, server-authorized dash, ray hits.

    /// This is not a replacement for a game's Unity physics, animation or production backend.

    /// </summary>

    public sealed class GuardAuthoritativeWorld

    {

        public const double FixedDelta = 1.0 / 60;

        public const double Speed = 8, Acceleration = 60, Gravity = 20, JumpSpeed = 7;

        public const int MaximumQueuedInputs = 3, MaximumRewindTicks = 12;

        private const double Skin = .000001;

        private static readonly GuardVector Half = new GuardVector(.35, .9, .35);

        private readonly object sync = new object();

        private readonly List<GuardBox> walls = new List<GuardBox>();

        private readonly Dictionary<string, Actor> actors = new Dictionary<string, Actor>(StringComparer.Ordinal);

        private double lastTime, accumulator;

        private long tick;

        private struct History { public long tick; public GuardVector position; public bool valid; }

        private sealed class Actor

        {

            public string id;

            public GuardVector position, velocity;

            public int health = 100, ammo = 30, epoch = 1, rewindTicks;

            public bool grounded, resync;

            public long queuedSequence, processedSequence, shotSequence, nextFireTick, dashUntilTick, nextDashTick;

            public double lastInputAt;

            public GuardServerInput held;

            public readonly Queue<GuardServerInput> pending = new Queue<GuardServerInput>();

            public readonly History[] history = new History[32];

        }

        public GuardAuthoritativeWorld(IEnumerable<GuardBox> staticObstacles, double serverNow = 0)

        {

            CheckTime(serverNow); lastTime = serverNow;

            if (staticObstacles == null) throw new ArgumentNullException(nameof(staticObstacles));

            foreach (var box in staticObstacles)

            {

                walls.Add(new GuardBox(box.min, box.max));

                if (walls.Count > 1024) throw new ArgumentException("Reference arena supports at most 1024 static boxes.");

            }

        }

        public void AddActor(string id, GuardVector trustedSpawn, double serverNow)

        {

            GuardCodec.EncodeName(id, 256);

            if (!trustedSpawn.IsFinite || Math.Abs(trustedSpawn.x) > 10000 || Math.Abs(trustedSpawn.y) > 10000 || Math.Abs(trustedSpawn.z) > 10000) throw new ArgumentException("Invalid server spawn.");

            lock (sync)

            {

                AdvanceInternal(serverNow);

                if (actors.Count >= 64 || actors.ContainsKey(id)) throw new ArgumentException("Actor already exists or arena is full.");

                foreach (var wall in walls) if (Overlap(trustedSpawn, wall)) throw new ArgumentException("Server spawn intersects geometry.");

                foreach (var other in actors.Values) if (Overlap(trustedSpawn, BodyBox(other.position))) throw new ArgumentException("Server spawn intersects an actor.");

                var actor = new Actor { id = id, position = trustedSpawn, lastInputAt = lastTime };

                actors.Add(id, actor); Move(actor, new GuardVector(0, -.001, 0)); Record(actor);

            }

        }

        public void Advance(double serverNow) { lock (sync) AdvanceInternal(serverNow); }

        public GuardServerSnapshot Snapshot(string id, double serverNow) { lock (sync) { AdvanceInternal(serverNow); return SnapshotOf(Find(id)); } }

        /// <summary>Returns the SERVER pose and clears the input continuity gate. Never accepts a client pose or resets velocity/cooldowns.</summary>

        public GuardServerSnapshot ResynchronizeFromServer(string id, double serverNow)

        {

            lock (sync) { AdvanceInternal(serverNow); var actor = Find(id); actor.resync = false; actor.lastInputAt = lastTime; return SnapshotOf(actor); }

        }

        public GuardInputStatus SubmitInput(string id, GuardServerInput input, double serverNow)

        {

            lock (sync)

            {

                AdvanceInternal(serverNow); var actor = Find(id);

                if (!GuardRisk.Finite(input.moveX) || !GuardRisk.Finite(input.moveZ) || Math.Abs(input.moveX) > 1 || Math.Abs(input.moveZ) > 1) return GuardInputStatus.InvalidInput;

                if (actor.health <= 0) return GuardInputStatus.Dead;

                if (actor.resync || input.epoch != actor.epoch) return GuardInputStatus.ResyncRequired;

                if (input.sequence <= 0 || input.sequence != actor.queuedSequence + 1) return GuardInputStatus.SequenceMismatch;

                if (input.acknowledgedTick > tick || input.acknowledgedTick < tick - 30) return GuardInputStatus.StaleAcknowledgement;

                if (actor.pending.Count >= MaximumQueuedInputs) return GuardInputStatus.QueueFull;

                actor.pending.Enqueue(input); actor.queuedSequence = input.sequence; actor.lastInputAt = lastTime; return GuardInputStatus.Queued;

            }

        }

        /// <summary>Trusted network layer only. No endpoint accepts client rewind ticks/timestamps.</summary>

        public void SetServerLagCompensationTicks(string id, int measuredTicks)

        {

            if (measuredTicks < 0 || measuredTicks > MaximumRewindTicks) throw new ArgumentOutOfRangeException(nameof(measuredTicks));

            lock (sync) Find(id).rewindTicks = measuredTicks;

        }

        public GuardShotResult Fire(string id, long sequence, GuardVector aimDirection, double serverNow)

        {

            lock (sync)

            {

                AdvanceInternal(serverNow); var shooter = Find(id);

                var result = new GuardShotResult { tick = tick, remainingAmmo = shooter.ammo };

                double length = aimDirection.x * aimDirection.x + aimDirection.y * aimDirection.y + aimDirection.z * aimDirection.z;

                if (!aimDirection.IsFinite || !GuardRisk.Finite(length) || length < .000001) { result.status = GuardShotStatus.InvalidAim; return result; }

                if (shooter.health <= 0) { result.status = GuardShotStatus.Dead; return result; }

                if (shooter.resync) { result.status = GuardShotStatus.ResyncRequired; return result; }

                if (sequence <= 0 || sequence != shooter.shotSequence + 1) { result.status = GuardShotStatus.SequenceMismatch; return result; }

                if (tick < shooter.nextFireTick) { result.status = GuardShotStatus.Cooldown; return result; }

                if (shooter.ammo <= 0) { result.status = GuardShotStatus.NoAmmo; return result; }

                shooter.shotSequence = sequence; shooter.nextFireTick = tick + 6; shooter.ammo--; result.remainingAmmo = shooter.ammo;

                GuardVector origin = shooter.position + new GuardVector(0, .6, 0), ray = aimDirection * (100 / Math.Sqrt(length));

                double wallDistance = 1; bool blocked = false;

                foreach (var wall in walls) if (Intersect(origin, ray, wall, false, out double fraction, out _) && fraction <= wallDistance) { blocked = true; wallDistance = fraction; }

                Actor hit = null; double nearest = wallDistance;

                foreach (var target in actors.Values)

                {

                    if (target == shooter || target.health <= 0) continue;

                    GuardVector position = target.position;

                    if (shooter.rewindTicks > 0 && !TryHistory(target, tick - shooter.rewindTicks, out position)) continue;

                    if (Intersect(origin, ray, BodyBox(position), false, out double fraction, out _) && fraction < nearest - Skin) { nearest = fraction; hit = target; }

                }

                if (hit != null) { result.status = GuardShotStatus.Hit; result.hitActor = hit.id; result.damage = Math.Min(25, hit.health); hit.health -= result.damage; }

                else result.status = blocked ? GuardShotStatus.Blocked : GuardShotStatus.Miss;

                return result;

            }

        }

        private Actor Find(string id) => actors.TryGetValue(id, out var actor) ? actor : throw new KeyNotFoundException("Unknown server actor.");

        private static void CheckTime(double now) { if (!GuardRisk.Finite(now) || now < 0 || now > 1000000000000) throw new ArgumentOutOfRangeException(nameof(now)); }

        private void AdvanceInternal(double now)

        {

            CheckTime(now); now = Math.Max(now, lastTime); // Concurrent trusted callers can capture their clock just before another caller.

            accumulator += now - lastTime; lastTime = now;

            long steps = (long)Math.Floor((accumulator + 1e-10) / FixedDelta);

            if (steps > 8)

            {

                tick += steps; accumulator = Math.Max(0, accumulator - steps * FixedDelta);

                foreach (var actor in actors.Values) { Interrupt(actor); Record(actor); }

                return; // Bound catch-up CPU; hold server poses, never substitute a client teleport.

            }

            for (long i = 0; i < steps; i++)

            {

                double stepTime = now - accumulator + FixedDelta; accumulator = Math.Max(0, accumulator - FixedDelta); tick++;

                foreach (var actor in actors.Values)

                {

                    if (actor.health <= 0) { actor.velocity = default; Record(actor); continue; }

                    if (stepTime - actor.lastInputAt > .5 && !actor.resync) Interrupt(actor);

                    GuardServerInput input = default;

                    if (!actor.resync && actor.pending.Count > 0) { input = actor.pending.Dequeue(); actor.held = input; actor.processedSequence = input.sequence; }

                    else if (!actor.resync && stepTime - actor.lastInputAt <= .1) { input = actor.held; input.jump = input.dash = false; }

                    if (actor.health <= 0) input = default;

                    double length = Math.Sqrt(input.moveX * input.moveX + input.moveZ * input.moveZ);

                    double x = length > 1 ? input.moveX / length : input.moveX, z = length > 1 ? input.moveZ / length : input.moveZ;

                    if (input.dash && tick >= actor.nextDashTick) { actor.dashUntilTick = tick + 6; actor.nextDashTick = tick + 60; }

                    double speed = tick < actor.dashUntilTick ? 16 : Speed;

                    // Bound acceleration as a vector. Componentwise acceleration permits overspeed while turning.

                    double dx = x * speed - actor.velocity.x, dz = z * speed - actor.velocity.z;

                    double change = Math.Sqrt(dx * dx + dz * dz), maximumChange = Acceleration * FixedDelta;

                    double blend = change > maximumChange ? maximumChange / change : 1;

                    actor.velocity.x += dx * blend; actor.velocity.z += dz * blend;

                    if (input.jump && actor.grounded) { actor.velocity.y = JumpSpeed; actor.grounded = false; }

                    actor.velocity.y = Math.Max(-30, actor.velocity.y - Gravity * FixedDelta);

                    Move(actor, actor.velocity * FixedDelta);

                    if (actor.position.y < -100 || Math.Abs(actor.position.x) > 10000 || Math.Abs(actor.position.z) > 10000) { actor.health = 0; actor.velocity = default; }

                    Record(actor);

                }

            }

        }

        private static void Interrupt(Actor actor)

        {

            if (!actor.resync) actor.epoch++;

            actor.resync = true; actor.pending.Clear(); actor.held = default;

            // Preserve velocity, cooldowns, ammo and health. Requesting resync cannot stop a fall or reload a gun.

        }

        private GuardServerSnapshot SnapshotOf(Actor a) => new GuardServerSnapshot

        {

            tick = tick, position = a.position, velocity = a.velocity, epoch = a.epoch, grounded = a.grounded,

            health = a.health, ammo = a.ammo, acknowledgedInput = a.processedSequence,

            nextInputSequence = a.queuedSequence + 1, nextShotSequence = a.shotSequence + 1, resyncRequired = a.resync

        };

        private static GuardBox BodyBox(GuardVector position) => new GuardBox(position - Half, position + Half);

        // Use the same expanded boundaries as the sweep, without a penetration allowance.

        // Exact surface contact is valid; even a sub-skin overlap is not a valid spawn.

        private static bool Overlap(GuardVector center, GuardBox box) => center.x > box.min.x - Half.x && center.x < box.max.x + Half.x && center.y > box.min.y - Half.y && center.y < box.max.y + Half.y && center.z > box.min.z - Half.z && center.z < box.max.z + Half.z;

        private void Move(Actor actor, GuardVector delta)

        {

            actor.grounded = false;

            // A sweep cannot resolve an already overlapping body. Fail closed rather than tunnel out.

            foreach (var wall in walls) if (Overlap(actor.position, wall)) { actor.velocity = default; return; }

            foreach (var other in actors.Values) if (other != actor && other.health > 0 && Overlap(actor.position, BodyBox(other.position))) { actor.velocity = default; return; }

            for (int iteration = 0; iteration < 4; iteration++)

            {

                double earliest = 1, contact = 0; GuardVector normal = default; bool collided = false;

                foreach (var wall in walls) Consider(actor.position, delta, wall, ref earliest, ref normal, ref contact, ref collided);

                foreach (var other in actors.Values) if (other != actor && other.health > 0) Consider(actor.position, delta, BodyBox(other.position), ref earliest, ref normal, ref contact, ref collided);

                if (!collided) { actor.position += delta; break; }

                actor.position += delta * Math.Max(0, earliest);

                // Clamp exactly to the swept plane. A skin push can penetrate the opposite

                // wall in an exactly body-width corridor, invalidating the next sweep.

                if (normal.x != 0) actor.position.x = contact;

                if (normal.y != 0) actor.position.y = contact;

                if (normal.z != 0) actor.position.z = contact;

                delta = delta * Math.Max(0, 1 - earliest);

                if (normal.x != 0) { actor.velocity.x = 0; delta.x = 0; }

                if (normal.y != 0) { actor.velocity.y = 0; delta.y = 0; if (normal.y > 0) actor.grounded = true; }

                if (normal.z != 0) { actor.velocity.z = 0; delta.z = 0; }

                if (Math.Abs(delta.x) + Math.Abs(delta.y) + Math.Abs(delta.z) < 1e-12) break;

            }

        }

        private static void Consider(GuardVector origin, GuardVector delta, GuardBox wall, ref double earliest, ref GuardVector normal, ref double contact, ref bool collided)

        {

            var expanded = new GuardBox { min = wall.min - Half, max = wall.max + Half };

            if (Intersect(origin, delta, expanded, true, out double fraction, out GuardVector hitNormal) && fraction <= earliest)

            {

                earliest = fraction; normal = hitNormal; collided = true;

                contact = normal.x != 0 ? (normal.x < 0 ? expanded.min.x : expanded.max.x) :

                    normal.y != 0 ? (normal.y < 0 ? expanded.min.y : expanded.max.y) :

                    (normal.z < 0 ? expanded.min.z : expanded.max.z);

            }

        }

        private static bool Intersect(GuardVector origin, GuardVector delta, GuardBox box, bool movingBody, out double fraction, out GuardVector normal)

        {

            double enter = double.NegativeInfinity, leave = double.PositiveInfinity; normal = default; fraction = 0;

            for (int axis = 0; axis < 3; axis++)

            {

                double p = axis == 0 ? origin.x : axis == 1 ? origin.y : origin.z;

                double d = axis == 0 ? delta.x : axis == 1 ? delta.y : delta.z;

                double lo = axis == 0 ? box.min.x : axis == 1 ? box.min.y : box.min.z;

                double hi = axis == 0 ? box.max.x : axis == 1 ? box.max.y : box.max.z;

                if (Math.Abs(d) < 1e-15)

                {

                    if (movingBody ? p <= lo || p >= hi : p < lo || p > hi) return false;

                    continue;

                }

                double a = (lo - p) / d, b = (hi - p) / d, sign = -1;

                if (a > b) { double temporary = a; a = b; b = temporary; sign = 1; }

                if (a > enter) { enter = a; normal = axis == 0 ? new GuardVector(sign, 0, 0) : axis == 1 ? new GuardVector(0, sign, 0) : new GuardVector(0, 0, sign); }

                leave = Math.Min(leave, b); if (enter > leave) return false;

            }

            if (leave < 0 || enter > 1 || (movingBody && enter < -1e-10)) return false;

            fraction = Math.Max(0, enter); return true;

        }

        private void Record(Actor actor) { actor.history[(int)(tick % actor.history.Length)] = new History { tick = tick, position = actor.position, valid = true }; }

        private static bool TryHistory(Actor actor, long requestedTick, out GuardVector position)

        {

            position = default; if (requestedTick < 0) return false;

            var sample = actor.history[(int)(requestedTick % actor.history.Length)];

            if (!sample.valid || sample.tick != requestedTick) return false; position = sample.position; return true;

        }

    }

}
