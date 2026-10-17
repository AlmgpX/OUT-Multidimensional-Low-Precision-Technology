// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using System.Collections.Generic;



namespace OUT.Guard.Diagnostics

{

    /// <summary>Executable reference-arena attacks and legitimate controls; no real game objects or accounts.</summary>

    public static class GuardAuthoritativeChecks

    {

        public static List<GuardCheckResult> Run()

        {

            var results = new List<GuardCheckResult>();

            Check(results, "40 / Server integrates commands using fixed time", () =>

            {

                var w = Arena(); var before = w.Snapshot("a", 0); Input(w, "a", 0, 1, 0); w.Advance(GuardAuthoritativeWorld.FixedDelta);

                var after = w.Snapshot("a", GuardAuthoritativeWorld.FixedDelta); return after.position.x > before.position.x && after.position.x - before.position.x < .02 && after.tick == 1;

            });

            Check(results, "41 / Input flooding cannot manufacture simulation time", () =>

            {

                var w = Arena(); for (int i = 0; i < 3; i++) if (Input(w, "a", 0, 1, 0) != GuardInputStatus.Queued) return false;

                return Input(w, "a", 0, 1, 0) == GuardInputStatus.QueueFull && w.Snapshot("a", 0).position.x == 0;

            });

            Check(results, "42 / Diagonal input respects the speed cap", () =>

            {

                var w = Arena(); GuardVector previous = default;

                for (int i = 0; i < 90; i++)

                {

                    // A legal turn must preserve both the speed circle and vector acceleration cap.

                    Step(w, i, 1, i < 30 ? 1 : i < 60 ? 0 : -1);

                    var velocity = w.Snapshot("a", (i + 1) * GuardAuthoritativeWorld.FixedDelta).velocity;

                    double speed = Math.Sqrt(velocity.x * velocity.x + velocity.z * velocity.z);

                    double dx = velocity.x - previous.x, dz = velocity.z - previous.z;

                    if (speed > GuardAuthoritativeWorld.Speed + 1e-8 || Math.Sqrt(dx * dx + dz * dz) > GuardAuthoritativeWorld.Acceleration * GuardAuthoritativeWorld.FixedDelta + 1e-8) return false;

                    previous = velocity;

                }

                return true;

            });

            Check(results, "43 / Nonfinite command rejected", () => Input(Arena(), "a", 0, double.NaN, 0) == GuardInputStatus.InvalidInput);

            Check(results, "44 / Oversized analog input rejected", () => Input(Arena(), "a", 0, 1000000, 0) == GuardInputStatus.InvalidInput);

            Check(results, "45 / Input replay rejected", () =>

            {

                var w = Arena(); var s = w.Snapshot("a", 0); var command = new GuardServerInput { sequence = 1, epoch = s.epoch, acknowledgedTick = s.tick, moveX = 1 };

                return w.SubmitInput("a", command, 0) == GuardInputStatus.Queued && w.SubmitInput("a", command, 0) == GuardInputStatus.SequenceMismatch;

            });

            Check(results, "46 / Future server acknowledgement rejected", () =>

            {

                var w = Arena(); return w.SubmitInput("a", new GuardServerInput { sequence = 1, epoch = 1, acknowledgedTick = 100000 }, 0) == GuardInputStatus.StaleAcknowledgement;

            });

            Check(results, "47 / Old synchronization epoch rejected", () =>

            {

                var w = Arena(); return w.SubmitInput("a", new GuardServerInput { sequence = 1, epoch = 0 }, 0) == GuardInputStatus.ResyncRequired;

            });

            Check(results, "48 / Swept body cannot tunnel through a thin wall", () =>

            {

                var w = Arena(true); for (int i = 0; i < 120; i++) Step(w, i, 1, 0, false, true);

                if (w.Snapshot("a", 2).position.x > 2 - .35 + 1e-8) return false;

                // Shallow penetration must be rejected too: a sweep starts outside its obstacle.

                var nearWall = new GuardAuthoritativeWorld(new[] { new GuardBox(new GuardVector(.35 - .0000005, 0, -10), new GuardVector(.36, 5, 10)) });

                bool rejected = false;

                try { nearWall.AddActor("overlap", new GuardVector(0, .9, 0), 0); } catch (ArgumentException) { rejected = true; }

                if (!rejected) return false;

                // Exactly body-width corridor: surface contact permits sliding and never applies

                // a skin offset into the opposite wall, even while steering into either side.

                var corridor = new GuardAuthoritativeWorld(new[] {

                    new GuardBox(new GuardVector(-100, -2, -100), new GuardVector(100, 0, 100)),

                    new GuardBox(new GuardVector(-2, 0, -20), new GuardVector(-.35, 5, 20)),

                    new GuardBox(new GuardVector(.35, 0, -20), new GuardVector(2, 5, 20)) });

                corridor.AddActor("a", new GuardVector(0, .9, 0), 0);

                for (int i = 0; i < 90; i++) Step(corridor, i, i < 45 ? 1 : -1, 1);

                var pose = corridor.Snapshot("a", 1.5).position;

                return pose.x == 0 && pose.z > 5;

            });

            Check(results, "49 / Collision permits legitimate sliding", () =>

            {

                var w = Arena(true); for (int i = 0; i < 90; i++) Step(w, i, 1, 1);

                var s = w.Snapshot("a", 1.5); return s.position.x <= 1.65 + 1e-8 && s.position.z > 3;

            });

            Check(results, "50 / Repeated jump requests cannot grant flight", () =>

            {

                var w = Arena(); double maximum = 0;

                for (int i = 0; i < 90; i++) { Step(w, i, 0, 0, true); maximum = Math.Max(maximum, w.Snapshot("a", (i + 1) * GuardAuthoritativeWorld.FixedDelta).position.y); }

                return maximum < 2.3 && maximum > 1.5;

            });

            Check(results, "51 / Server-authorized dash remains valid", () =>

            {

                var w = Arena(); for (int i = 0; i < 60; i++) Step(w, i, 1, 0);

                for (int i = 60; i < 65; i++) Step(w, i, 1, 0, false, i == 60);

                return w.Snapshot("a", 65 * GuardAuthoritativeWorld.FixedDelta).velocity.x > GuardAuthoritativeWorld.Speed;

            });

            Check(results, "52 / Dash spam cannot reset its server cooldown", () =>

            {

                var single = Arena(); var spam = Arena();

                for (int i = 0; i < 40; i++) { Step(single, i, 1, 0, false, i == 10); Step(spam, i, 1, 0, false, i >= 10); }

                return Math.Abs(single.Snapshot("a", 40 * GuardAuthoritativeWorld.FixedDelta).position.x - spam.Snapshot("a", 40 * GuardAuthoritativeWorld.FixedDelta).position.x) < 1e-8;

            });

            Check(results, "53 / Missing commands expire and request resynchronization", () =>

            {

                var w = Arena(); Input(w, "a", 0, 1, 0); for (int i = 1; i <= 40; i++) w.Advance(i * GuardAuthoritativeWorld.FixedDelta);

                var s = w.Snapshot("a", 40 * GuardAuthoritativeWorld.FixedDelta); return s.resyncRequired && s.epoch == 2 && s.position.x < 1 && s.velocity.x == 0;

            });

            Check(results, "54 / Resync cannot stop a fall or reload ammunition", () =>

            {

                var w = Arena(); Step(w, 0, 0, 0, true); var shot = w.Fire("a", 1, new GuardVector(0, 1, 0), GuardAuthoritativeWorld.FixedDelta);

                var before = w.Snapshot("a", GuardAuthoritativeWorld.FixedDelta);

                var after = w.ResynchronizeFromServer("a", GuardAuthoritativeWorld.FixedDelta);

                w.ResynchronizeFromServer("a", GuardAuthoritativeWorld.FixedDelta);

                return after.position.y == before.position.y && after.velocity.y == before.velocity.y && after.ammo == 29 && after.nextShotSequence == 2;

            });

            Check(results, "55 / Server stall has bounded catch-up and no teleport", () =>

            {

                var w = Arena(); Input(w, "a", 0, 1, 0); var s = w.Snapshot("a", 20);

                return s.resyncRequired && s.position.x == 0 && s.tick == 1200 && s.epoch == 2;

            });

            Check(results, "56 / Clear line of sight applies server damage", () =>

            {

                var w = Arena(); w.AddActor("b", new GuardVector(10, .9, 0), 0); var result = w.Fire("a", 1, new GuardVector(1, 0, 0), 0);

                return result.status == GuardShotStatus.Hit && result.damage == 25 && result.hitActor == "b" && result.remainingAmmo == 29 && w.Snapshot("b", 0).health == 75;

            });

            Check(results, "57 / Wall blocks a shot and preserves target health", () =>

            {

                var w = Arena(true); w.AddActor("b", new GuardVector(10, .9, 0), 0); var result = w.Fire("a", 1, new GuardVector(1, 0, 0), 0);

                return result.status == GuardShotStatus.Blocked && result.damage == 0 && result.hitActor == null && w.Snapshot("b", 0).health == 100;

            });

            Check(results, "58 / Replaying a shot cannot repeat damage", () =>

            {

                var w = Arena(); w.AddActor("b", new GuardVector(10, .9, 0), 0); w.Fire("a", 1, new GuardVector(1, 0, 0), 0);

                return w.Fire("a", 1, new GuardVector(1, 0, 0), 0).status == GuardShotStatus.SequenceMismatch && w.Snapshot("b", 0).health == 75;

            });

            Check(results, "59 / Server fire cooldown cannot be bypassed", () =>

            {

                var w = Arena(); w.Fire("a", 1, new GuardVector(0, 1, 0), 0);

                if (w.Fire("a", 2, new GuardVector(0, 1, 0), 0).status != GuardShotStatus.Cooldown || w.Snapshot("a", 0).ammo != 29) return false;

                w.Advance(.1); return w.Fire("a", 2, new GuardVector(0, 1, 0), .1).remainingAmmo == 28;

            });

            Check(results, "60 / Resynchronization cannot restore spent ammunition", () =>

            {

                var w = Arena();

                for (int i = 0; i < 30; i++) { double now = i * .1; w.ResynchronizeFromServer("a", now); if (w.Fire("a", i + 1, new GuardVector(0, 1, 0), now).remainingAmmo != 29 - i) return false; }

                w.ResynchronizeFromServer("a", 3); return w.Fire("a", 31, new GuardVector(0, 1, 0), 3).status == GuardShotStatus.NoAmmo;

            });

            Check(results, "61 / Rewind is bounded and configured only by the server", () =>

            {

                var w = Arena(); w.AddActor("b", new GuardVector(10, .9, 0), 0);

                for (int i = 0; i < 12; i++) { Input(w, "b", i * GuardAuthoritativeWorld.FixedDelta, 0, 1); w.Advance((i + 1) * GuardAuthoritativeWorld.FixedDelta); }

                bool bounded = false; try { w.SetServerLagCompensationTicks("a", 13); } catch (ArgumentOutOfRangeException) { bounded = true; }

                w.SetServerLagCompensationTicks("a", 12);

                return bounded && w.Fire("a", 1, new GuardVector(1, 0, 0), .2).status == GuardShotStatus.Hit;

            });

            Check(results, "62 / Invalid aim cannot consume ammunition", () =>

            {

                var w = Arena(); return w.Fire("a", 1, new GuardVector(double.NaN, 0, 0), 0).status == GuardShotStatus.InvalidAim && w.Fire("a", 1, default, 0).status == GuardShotStatus.InvalidAim && w.Snapshot("a", 0).ammo == 30;

            });

            Check(results, "63 / Returned snapshots cannot mutate server state", () =>

            {

                var w = Arena(); var copy = w.Snapshot("a", 0); copy.position = new GuardVector(999, 999, 999); copy.ammo = 999; copy.health = 999;

                var actual = w.Snapshot("a", 0); return actual.position.x == 0 && actual.ammo == 30 && actual.health == 100;

            });

            return results;

        }

        private static GuardAuthoritativeWorld Arena(bool wall = false)

        {

            var boxes = new List<GuardBox> { new GuardBox(new GuardVector(-100, -2, -100), new GuardVector(100, 0, 100)) };

            if (wall) boxes.Add(new GuardBox(new GuardVector(2, 0, -30), new GuardVector(2.01, 5, 30)));

            var world = new GuardAuthoritativeWorld(boxes); world.AddActor("a", new GuardVector(0, .9, 0), 0); return world;

        }

        private static GuardInputStatus Input(GuardAuthoritativeWorld world, string id, double now, double x, double z, bool jump = false, bool dash = false)

        {

            var s = world.Snapshot(id, now); return world.SubmitInput(id, new GuardServerInput { sequence = s.nextInputSequence, acknowledgedTick = s.tick, epoch = s.epoch, moveX = x, moveZ = z, jump = jump, dash = dash }, now);

        }

        private static void Step(GuardAuthoritativeWorld world, int frame, double x, double z, bool jump = false, bool dash = false)

        {

            double now = frame * GuardAuthoritativeWorld.FixedDelta;

            if (Input(world, "a", now, x, z, jump, dash) != GuardInputStatus.Queued) throw new Exception("Legitimate command rejected.");

            world.Advance((frame + 1) * GuardAuthoritativeWorld.FixedDelta);

        }

        private static void Check(List<GuardCheckResult> results, string name, Func<bool> test)

        {

            try { bool ok = test(); results.Add(new GuardCheckResult { name = name, passed = ok, detail = ok ? "Protection held." : "Unexpected acceptance or false signal." }); }

            catch (Exception error) { results.Add(new GuardCheckResult { name = name, passed = false, detail = error.GetType().Name + ": " + error.Message }); }

        }

    }

}
