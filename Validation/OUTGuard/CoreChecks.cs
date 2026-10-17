// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using System.IO;

using System.Linq;

using System.Reflection;

using System.Text;

using OUT.Guard;



static class CoreChecks

{

    static int checks;

    static void Check(bool condition, string label) { checks++; if (!condition) throw new Exception("FAILED: " + label); }

    static void Throws<T>(Action action, string label) where T : Exception { try { action(); } catch (T) { checks++; return; } throw new Exception("FAILED expected " + typeof(T).Name + ": " + label); }

    static void Main()

    {

        foreach (int value in new[] { 0, 1, -1, int.MinValue, int.MaxValue, 918234 }) Check(new ProtectedInt(value).Value == value, "int " + value);

        foreach (long value in new[] { 0L, -1L, long.MinValue, long.MaxValue }) Check(new ProtectedLong(value).Value == value, "long boundary");

        Check(default(ProtectedInt).Value == 0 && !default(ProtectedBool).Value, "default values");

        foreach (float f in new[] { 0f, -0f, float.Epsilon, float.MaxValue, float.MinValue, float.PositiveInfinity, float.NegativeInfinity, float.NaN })

            Check(BitConverter.SingleToInt32Bits(f) == BitConverter.SingleToInt32Bits(new ProtectedFloat(f).Value), "float bit preservation");

        var number = new ProtectedInt(41); number++; number += 9; number.Rekey(); Check(number.Value == 51, "arithmetic/rekey");

        Throws<OverflowException>(() => { var n = new ProtectedInt(int.MaxValue); n++; }, "checked overflow");

        object broken = new ProtectedLong(100);

        var encoded = typeof(ProtectedLong).GetField("encoded", BindingFlags.NonPublic | BindingFlags.Instance);

        encoded.SetValue(broken, (ulong)encoded.GetValue(broken) ^ 1UL);

        int evidence = 0; Action<GuardEvidence> count = e => evidence++; Action<GuardEvidence> failing = e => throw new Exception("Subscriber failed");

        GuardEvents.Detected += failing; GuardEvents.Detected += count;

        Throws<GuardTamperException>(() => { long x = ((ProtectedLong)broken).Value; }, "tamper still rejected with failing subscriber");

        GuardEvents.Detected -= failing; GuardEvents.Detected -= count; Check(evidence == 1, "independent evidence subscribers");

        var key = GuardCodec.CreateKey();

        using (var codec = new GuardCodec(key))

        {

            foreach (int length in new[] { 0, 1, 15, 16, 17, 4096, 65536 })

            {

                byte[] payload = Enumerable.Range(0, length).Select(i => (byte)(i * 23)).ToArray();

                byte[] packed = codec.Encode(payload, "slot/a"); Check(codec.TryDecode(packed, "slot/a", out var decoded) && decoded.SequenceEqual(payload), "encrypted roundtrip " + length);

            }

            byte[] original = codec.Encode(Encoding.UTF8.GetBytes("gold=120"), "slot/a");

            for (int i = 0; i < original.Length; i++) { var modified = (byte[])original.Clone(); modified[i] ^= 1; Check(!codec.TryDecode(modified, "slot/a", out _), "every envelope byte authenticated " + i); }

            for (int i = 0; i < original.Length; i++) Check(!codec.TryDecode(original.Take(i).ToArray(), "slot/a", out _), "truncated envelope " + i);

            Check(!codec.TryDecode(original, "slot/b", out _), "slot swapping");

            Check(!original.SequenceEqual(codec.Encode(Encoding.UTF8.GetBytes("gold=120"), "slot/a")), "fresh encryption randomness");

            using (var wrong = new GuardCodec(GuardCodec.CreateKey())) Check(!wrong.TryDecode(original, "slot/a", out _), "wrong key");

            var random = new Random(77); for (int i = 0; i < 200; i++) { var garbage = new byte[random.Next(0, 300)]; random.NextBytes(garbage); Check(!codec.TryDecode(garbage, "slot/a", out _), "malformed envelope " + i); }

        }

        var disposed = new GuardCodec(key); disposed.Dispose(); Throws<ObjectDisposedException>(() => disposed.Encode(new byte[0], "slot"), "disposed key");

        string temp = Path.Combine(Path.GetTempPath(), "OUTGuardChecks_" + Guid.NewGuid().ToString("N"));

        try

        {

            using (var store = new GuardSaveStore(temp, key))

            {

                Check(store.Load("a", out _) == GuardLoadStatus.Missing, "missing save");

                store.Save("a", Encoding.UTF8.GetBytes("one")); store.Save("a", Encoding.UTF8.GetBytes("two"));

                Check(store.Load("a", out var current) == GuardLoadStatus.Success && Encoding.UTF8.GetString(current) == "two", "atomic replacement");

                Check(store.Load("a", out var backup, true) == GuardLoadStatus.Success && Encoding.UTF8.GetString(backup) == "one", "previous generation backup");

                File.WriteAllText(store.GetSlotPath("a"), "tampered"); Check(store.Load("a", out _) == GuardLoadStatus.Invalid, "tampered save");

                Check(Path.GetDirectoryName(store.GetSlotPath("../../escape")) == temp, "slot path containment");

                store.Delete("a"); Check(store.Load("a", out _) == GuardLoadStatus.Missing && store.Load("a", out _, true) == GuardLoadStatus.Missing, "delete both generations");

            }

            string file = Path.Combine(temp, "content.txt"); Directory.CreateDirectory(temp); File.WriteAllText(file, "approved"); string hash = GuardIntegrity.HashFile(file);

            Check(GuardIntegrity.Verify(temp, "content.txt", hash) == GuardFileStatus.Valid, "content valid");

            File.WriteAllText(file, "changed"); Check(GuardIntegrity.Verify(temp, "content.txt", hash) == GuardFileStatus.Modified, "content modified");

            Check(GuardIntegrity.Verify(temp, "../elsewhere", hash) == GuardFileStatus.InvalidPath, "manifest traversal");

        }

        finally { if (Directory.Exists(temp)) Directory.Delete(temp, true); }

        var table = GuardBehaviorTable.CreateDefault(); Check(table.Export().Length == 2048, "packed storage");

        Check(table.Evaluate(.5, .5, .5, .5) == 0 && table.Evaluate(2, 2, 2, 2) == 1, "table risk boundaries");

        Throws<ArgumentOutOfRangeException>(() => table.Evaluate(double.NaN, 0, 0, 0), "invalid table feature");

        var tracker = new GuardMovementTracker(); tracker.Observe(0, 0, 0, 0);

        for (int i = 1; i <= 50; i++) Check(!tracker.Observe(i * .1, i * .4, 0, 0).hasEvidence, "legal constant motion " + i);

        Check(tracker.Observe(5.1, 25, 0, 0).hasEvidence, "speed violation");

        Check(!tracker.Observe(5.2, 900, 0, 0, true).hasEvidence, "authorized teleport");

        Check(tracker.Observe(5.2, 900, 0, 0).signal == GuardSignal.InvalidSample, "repeated timestamp");

        Check(tracker.Observe(5.3, double.NaN, 0, 0).signal == GuardSignal.InvalidSample, "invalid position");

        Check(tracker.Observe(5.3, double.MaxValue, 0, 0).signal == GuardSignal.InvalidSample, "overflowing intermediate");

        Check(tracker.Observe(100, 1000, 0, 0).signal == GuardSignal.ObservationGap, "observation gap requires trusted resynchronization");

        var clock = new GuardClock(.5, .15, 2); bool clockReported = false;

        for (int i = 0; i < 50; i++) clockReported |= clock.Observe(i * .1, .1, out _);

        Check(!clockReported, "normal clock"); clock.Reset();

        for (int i = 0; i < 30; i++) clockReported |= clock.Observe(i * .1, .2, out _);

        Check(clockReported, "accelerated clock"); clock.Reset(); Check(!clock.Observe(100, .1, out _), "clock resume baseline");

        var risk = new GuardRisk(10); risk.Advance(0, 80); Check(Math.Abs(risk.Advance(10) - 40) < 1e-9, "risk half life");

        Throws<ArgumentOutOfRangeException>(() => risk.Advance(9), "risk monotonic time");

        byte[] state = Encoding.UTF8.GetBytes("checkpoint:12;gold:120"), other = Encoding.UTF8.GetBytes("checkpoint:12;gold:999");

        using (var server = new GuardChallengeSession(key))

        {

            var challenge = server.Issue(0); var reply = GuardHandshake.CreateReply(key, challenge, state);

            var wire = GuardWireReply.FromReply(reply);

            Check(wire.TryGetReply(out var wireReply) && wireReply.stateDigest.SequenceEqual(reply.stateDigest), "wire Base64 DTO roundtrip");

            wire.authentication = "malformed"; Check(!wire.TryGetReply(out _), "malformed wire authentication");

            Check(server.Verify(reply, state, 1) == GuardReplyStatus.Accepted, "challenge accepted");

            Check(server.Verify(reply, state, 2) == GuardReplyStatus.ReplayOrUnknown, "replay rejected");

            challenge = server.Issue(3, 2); reply = GuardHandshake.CreateReply(key, challenge, state);

            Check(server.Verify(reply, state, 5) == GuardReplyStatus.Expired, "expiry boundary");

            challenge = server.Issue(6); reply = GuardHandshake.CreateReply(key, challenge, other);

            Check(server.Verify(reply, state, 7) == GuardReplyStatus.StateMismatch, "authenticated wrong state");

            challenge = server.Issue(8); reply = GuardHandshake.CreateReply(GuardCodec.CreateKey(), challenge, state);

            Check(server.Verify(reply, state, 9) == GuardReplyStatus.BadAuthentication, "wrong session key");

            var old = server.Issue(10); var latest = server.Issue(11);

            Check(server.Verify(GuardHandshake.CreateReply(key, old, state), state, 12) == GuardReplyStatus.ReplayOrUnknown, "superseded challenge");

            Check(server.Verify(GuardHandshake.CreateReply(key, latest, state), state, 12) == GuardReplyStatus.Accepted, "old replay cannot consume current challenge");

            Throws<ArgumentOutOfRangeException>(() => server.Issue(11), "server clock rollback");

        }

        var budget = new GuardDeltaBudget(10, 2);

        Check(budget.TrySpend(10, 0) && !budget.TrySpend(1, 0), "delta burst bounded");

        Check(budget.TrySpend(2, 1) && !budget.TrySpend(1, 1), "delta time refill");

        Check(!budget.TrySpend(-1, 2) && !budget.TrySpend(double.NaN, 2) && !budget.TrySpend(1, 0), "malformed delta rejected");

        number = 99; long warm = 0; for (int i = 0; i < 10000; i++) warm += number.Value;

        long before = GC.GetAllocatedBytesForCurrentThread(); for (int i = 0; i < 100000; i++) warm += number.Value;

        long allocation = GC.GetAllocatedBytesForCurrentThread() - before; Check(allocation == 0, "protected reads allocate zero after warmup");

        Console.WriteLine("PASS " + checks + " checks. Protected reads: " + allocation + " allocated bytes / 100000 reads. Sink=" + warm);

    }

}
