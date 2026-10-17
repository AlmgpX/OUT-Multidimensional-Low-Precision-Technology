// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Security.Cryptography;

namespace OUT.Guard.Diagnostics
{
    [Serializable]
    public sealed class GuardCheckResult
    {
        public string name;
        public bool passed;
        public string detail;
    }
    /// <summary>Isolated diagnostic cases. They create their own values and never access game saves or settings.</summary>
    public static class GuardSecurityChecks
    {
        private static void Check(List<GuardCheckResult> results, string name, Func<bool> test)
        {
            try { bool passed = test(); results.Add(new GuardCheckResult { name = name, passed = passed, detail = passed ? "Protection held in this test." : "Unexpected acceptance or false signal." }); }
            catch (Exception error) { results.Add(new GuardCheckResult { name = name, passed = false, detail = error.GetType().Name + ": " + error.Message }); }
        }
        private static FieldInfo Field(string name) => typeof(ProtectedLong).GetField(name, BindingFlags.NonPublic | BindingFlags.Instance);
        private static ulong Read(object value, string name) => (ulong)Field(name).GetValue(value);
        private static ulong Inv(ulong a) { unchecked { ulong x = 1; for (int i = 0; i < 6; i++) x *= 2 - a * x; return x; } }
        private static ulong Undo(ulong v, int shift) { ulong x = v; for (int i = shift; i < 64; i += shift) x ^= v >> i; return x; }
        private static ulong Unmix(ulong x) { unchecked { x = Undo(x, 31) * Inv(0x94d049bb133111ebUL); x = Undo(x, 27) * Inv(0xbf58476d1ce4e5b9UL); return Undo(x, 30); } }
        private static ulong Mix(ulong x) { unchecked { x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9UL; x = (x ^ (x >> 27)) * 0x94d049bb133111ebUL; return x ^ (x >> 31); } }
        private static bool Rejects(Action read)
        {
            try { read(); return false; } catch (GuardTamperException) { return true; }
        }
        public static List<GuardCheckResult> Run()
        {
            var results = new List<GuardCheckResult>();
            Check(results, "01 / Protected integer roundtrip", () => new ProtectedInt(1200).Value == 1200 && default(ProtectedInt).Value == 0);
            Check(results, "02 / Single-field memory edit", () =>
            {
                object value = new ProtectedLong(1200); Field("encoded").SetValue(value, Read(value, "encoded") ^ 1);
                return Rejects(() => { long ignored = ((ProtectedLong)value).Value; });
            });
            Check(results, "03 / Old reversible-seal forgery", () =>
            {
                object sample = new ProtectedLong(1200), target = new ProtectedLong(31);
                ulong guessed = Unmix(Read(sample, "tag")) ^ Read(sample, "encoded") ^ Read(sample, "key");
                Field("encoded").SetValue(target, 999999UL ^ Read(target, "key")); Field("tag").SetValue(target, Mix(999999UL ^ guessed));
                return Rejects(() => { long ignored = ((ProtectedLong)target).Value; });
            });
            Check(results, "04 / Missing observations cannot authorize teleport", () =>
            {
                var tracker = new GuardMovementTracker(); tracker.Observe(0, 0, 0, 0);
                var r = tracker.Observe(.6, 1000, 0, 0); return r.hasEvidence && r.signal == GuardSignal.ObservationGap && !r.rebased;
            });
            Check(results, "05 / Rejected movement does not poison baseline", () =>
            {
                var tracker = new GuardMovementTracker(); tracker.Observe(0, 0, 0, 0);
                return tracker.Observe(.1, 100, 0, 0).hasEvidence && !tracker.Observe(.2, 0, 0, 0).hasEvidence;
            });
            Check(results, "06 / Rejected observations still enforce time order", () =>
            {
                var tracker = new GuardMovementTracker(); tracker.Observe(0, 0, 0, 0); tracker.Observe(.1, 100, 0, 0);
                return tracker.Observe(.05, 0, 0, 0).signal == GuardSignal.InvalidSample;
            });
            Check(results, "07 / Explicit authorized teleport", () =>
            {
                var tracker = new GuardMovementTracker(); tracker.Observe(0, 0, 0, 0); var r = tracker.Observe(10, 1000, 0, 0, true);
                return !r.hasEvidence && r.rebased;
            });
            Check(results, "08 / Painted high-risk policy participates", () =>
            {
                byte[] packed = new byte[2048]; for (int i = 0; i < packed.Length; i++) packed[i] = 255;
                var tracker = new GuardMovementTracker(table: new GuardBehaviorTable(packed)); tracker.Observe(0, 0, 0, 0);
                var r = tracker.Observe(.1, .4, 0, 0); return r.hasEvidence && r.signal == GuardSignal.MovementPattern;
            });
            Check(results, "09 / Ordinary motion remains accepted", () =>
            {
                var tracker = new GuardMovementTracker(); tracker.Observe(0, 0, 0, 0);
                for (int i = 1; i <= 100; i++) if (tracker.Observe(i * .1, i * .4, 0, 0).hasEvidence) return false;
                return true;
            });
            Check(results, "10 / Ordinary clock remains silent", () =>
            {
                var clock = new GuardClock(); clock.Observe(0, 0, out _);
                for (int i = 1; i <= 480; i++) if (clock.Observe(i * .125, .125, out _)) return false;
                return true;
            });
            Check(results, "11 / Alternating fast and normal clock windows", () =>
            {
                var clock = new GuardClock(); clock.Observe(0, 0, out _); bool detected = false;
                for (int i = 1; i <= 480; i++) detected |= clock.Observe(i * .125, .125 * (((i - 1) / 40) % 3 < 2 ? 2 : 1), out _);
                return detected;
            });
            Check(results, "12 / Slow clock detected", () =>
            {
                var clock = new GuardClock(); clock.Observe(0, 0, out _); bool detected = false;
                for (int i = 1; i <= 240; i++) detected |= clock.Observe(i * .125, .0625, out _);
                return detected;
            });
            Check(results, "13 / Invalid MAC cannot consume a valid challenge", () =>
            {
                byte[] key = GuardCodec.CreateKey(), state = { 1, 2, 3 };
                using (var server = new GuardChallengeSession(key))
                {
                    var challenge = server.Issue(0); var valid = GuardHandshake.CreateReply(key, challenge, state);
                    var fake = new GuardReply { sequence = challenge.sequence, nonce = challenge.nonce, stateDigest = new byte[32], authentication = new byte[32] };
                    return server.Verify(fake, state, 1) == GuardReplyStatus.BadAuthentication && server.Verify(valid, state, 2) == GuardReplyStatus.Accepted;
                }
            });
            Check(results, "14 / Consumed valid response cannot replay", () =>
            {
                byte[] key = GuardCodec.CreateKey(), state = { 1 };
                using (var server = new GuardChallengeSession(key))
                {
                    var reply = GuardHandshake.CreateReply(key, server.Issue(0), state);
                    return server.Verify(reply, state, 1) == GuardReplyStatus.Accepted && server.Verify(reply, state, 2) == GuardReplyStatus.ReplayOrUnknown;
                }
            });
            Check(results, "15 / Expired response rejected", () =>
            {
                byte[] key = GuardCodec.CreateKey(), state = { 1 };
                using (var server = new GuardChallengeSession(key)) return server.Verify(GuardHandshake.CreateReply(key, server.Issue(0, 1), state), state, 1) == GuardReplyStatus.Expired;
            });
            Check(results, "16 / Malformed Unicode names and text rejected", () =>
            {
                // Construct at runtime: IL2CPP normalizes invalid string literals in generated metadata.
                string invalid = "slot-" + new string((char)0xD800, 1);
                bool rejected = false; try { GuardCodec.EncodeName(invalid); } catch (ArgumentException) { rejected = true; }
                if (!rejected) throw new Exception("Unpaired code unit accepted: " + ((int)invalid[invalid.Length - 1]).ToString("X4"));
                return rejected && !GuardCodec.TryDecodeUtf8Text(new byte[] { 255 }, 100, out _) && GuardCodec.EncodeUtf8Text("", 0).Length == 0;
            });
            Check(results, "17 / Cross-slot encrypted data rejected", () =>
            {
                using (var codec = new GuardCodec(GuardCodec.CreateKey())) return !codec.TryDecode(codec.Encode(new byte[] { 1 }, "slot/a"), "slot/b", out _);
            });
            Check(results, "18 / Modified ciphertext rejected", () =>
            {
                using (var codec = new GuardCodec(GuardCodec.CreateKey())) { byte[] data = codec.Encode(new byte[] { 1 }, "slot"); data[45] ^= 1; return !codec.TryDecode(data, "slot", out _); }
            });
            Check(results, "19 / Negative and nonfinite delta costs rejected", () =>
            {
                var budget = new GuardDeltaBudget(10, 2); return !budget.TrySpend(-1, 0) && !budget.TrySpend(double.NaN, 0) && budget.Available == 10;
            });
            Check(results, "20 / Bound fields reject value substitution", () =>
            {
                GuardBinding health = new GuardBinding("test/player/health"), maximum = new GuardBinding("test/player/maxHealth");
                var copied = new ProtectedInt(1000, maximum); return Rejects(() => copied.Read(health));
            });
            Check(results, "21 / Bound field rejects a zeroed struct", () =>
            {
                var binding = new GuardBinding("test/player/cooldown"); var zeroed = default(ProtectedFloat);
                return Rejects(() => zeroed.Read(binding));
            });
            Check(results, "22 / Per-user derived keys differ", () =>
            {
                byte[] token = GuardCodec.CreateKey();
                byte[] a = GuardCodec.DeriveUserKey(token, "player-a", "game"), b = GuardCodec.DeriveUserKey(token, "player-b", "game");
                return !Convert.ToBase64String(a).Equals(Convert.ToBase64String(b), StringComparison.Ordinal);
            });
            Check(results, "23 / Old backup fails a trusted server revision floor", () =>
            {
                string folder = Path.Combine(Path.GetTempPath(), "OUTGuardVersionCheck_" + Guid.NewGuid().ToString("N"));
                using (var store = new GuardSaveStore(folder, GuardCodec.CreateKey(), "player-a"))
                {
                    try
                    {
                        store.SaveVersioned("slot", new byte[] { 1 }, 7); store.SaveVersioned("slot", new byte[] { 2 }, 8);
                        return store.LoadVersioned("slot", 8, out _, out long current) == GuardLoadStatus.Success && current == 8 &&
                            store.LoadVersioned("slot", 8, out _, out _, true) == GuardLoadStatus.StaleRevision;
                    }
                    finally { store.Delete("slot"); if (Directory.Exists(folder)) Directory.Delete(folder); }
                }
            });
            Check(results, "24 / Token budget is atomic under concurrent spend", () =>
            {
                var budget = new GuardDeltaBudget(10, 0); int accepted = 0;
                Parallel.For(0, 100, i => { if (budget.TrySpend(1, 0)) Interlocked.Increment(ref accepted); }); return accepted == 10 && budget.Available == 0;
            });
            Check(results, "25 / Token budget explicitly refreshes", () =>
            {
                var budget = new GuardDeltaBudget(10, 2); budget.TrySpend(10, 0); return budget.Refresh(3) == 6;
            });
            Check(results, "26 / Unauthorized fast game time scale", () => GuardTimeScale.IsUnexpected(1, 10, .16, .016));
            Check(results, "27 / Unauthorized slow game time scale", () => GuardTimeScale.IsUnexpected(1, .5, .008, .016));
            Check(results, "28 / Declared slow motion remains valid", () => !GuardTimeScale.IsUnexpected(.5, .5, .008, .016));
            Check(results, "29 / Downward scaled-delta hitch clamp is not a cheat verdict", () => !GuardTimeScale.IsUnexpected(1, 1, .333, 1));
            Check(results, "30 / Focus toggles preserve accumulated clock evidence", () =>
            {
                var clock = new GuardClock(); clock.Start(0); double now = 0; bool detected = false;
                for (int cycle = 0; cycle < 5; cycle++)
                {
                    for (int step = 0; step < 39; step++) { now += .125; detected |= clock.Observe(now, .25, out _); }
                    now += .025; detected |= clock.Suspend(now, out _); now += .1; clock.Resume(now);
                }
                if (!detected) return false;
                // A real pause can put the inactive interval into Unity's first resumed delta.
                var normal = new GuardClock(); normal.Start(0); now = 0;
                for (int cycle = 0; cycle < 5; cycle++)
                {
                    for (int step = 0; step < 40; step++) { now += .125; if (normal.Observe(now, .125, out _)) return false; }
                    now += .01; if (normal.Suspend(now, out _)) return false;
                    now += 10; normal.Resume(now); now += .125;
                    if (normal.Observe(now, 10.125, out _)) return false;
                }
                return true;
            });
            Check(results, "31 / Nonfinite-position spam cannot grant a rebase", () =>
            {
                var tracker = new GuardMovementTracker(); tracker.Observe(0, 0, 0, 0);
                for (int i = 1; i < 6; i++) if (tracker.Observe(i * .1, double.NaN, 0, 0).signal != GuardSignal.InvalidSample) return false;
                var r = tracker.Observe(.6, 1000, 0, 0); return !r.accepted && !r.rebased && r.signal == GuardSignal.ObservationGap;
            });
            Check(results, "32 / Default movement result is neutral and unaccepted", () => default(GuardMovementResult).signal == GuardSignal.None && !default(GuardMovementResult).accepted);
            Check(results, "33 / Simultaneous speed and vertical violations retained", () =>
            {
                var tracker = new GuardMovementTracker(); tracker.Observe(0, 0, 0, 0); var r = tracker.Observe(.1, 10, 10, 0);
                return (r.violations & GuardMovementViolation.Speed) != 0 && (r.violations & GuardMovementViolation.Vertical) != 0;
            });
            Check(results, "34 / Sub-interval observation is never accepted", () =>
            {
                var tracker = new GuardMovementTracker(); tracker.Observe(0, 0, 0, 0); var r = tracker.Observe(.0001, 1000, 0, 0); return r.skipped && !r.accepted;
            });
            Check(results, "35 / Per-user save contexts reject cross-user copies", () =>
            {
                string folder = Path.Combine(Path.GetTempPath(), "OUTGuardUserCheck_" + Guid.NewGuid().ToString("N")); byte[] key = GuardCodec.CreateKey();
                using (var a = new GuardSaveStore(folder, key, "a")) using (var b = new GuardSaveStore(folder, key, "b"))
                {
                    try { a.Save("slot", new byte[] { 1 }); File.Copy(a.GetSlotPath("slot"), b.GetSlotPath("slot")); return b.Load("slot", out _) == GuardLoadStatus.Invalid; }
                    finally { a.Delete("slot"); b.Delete("slot"); if (Directory.Exists(folder)) Directory.Delete(folder); }
                }
            });
            Check(results, "36 / Atomic file replacement probe", () =>
            {
                string folder = Path.Combine(Path.GetTempPath(), "OUTGuardAtomicCheck_" + Guid.NewGuid().ToString("N"));
                using (var store = new GuardSaveStore(folder, GuardCodec.CreateKey()))
                {
                    try { return store.ProbeAtomicReplace(out _); } finally { if (Directory.Exists(folder)) Directory.Delete(folder); }
                }
            });
            Check(results, "37 / Invalid context rejected before key use", () =>
            {
                var codec = new GuardCodec(GuardCodec.CreateKey()); codec.Dispose();
                try { codec.Encode(new byte[] { 1 }, new string((char)0xD800, 1)); return false; } catch (ArgumentException) { return true; }
            });
            Check(results, "38 / Concurrent challenge issuance stays serialized", () =>
            {
                var sequences = new System.Collections.Concurrent.ConcurrentDictionary<long, byte>();
                using (var session = new GuardChallengeSession(GuardCodec.CreateKey()))
                    Parallel.For(0, 50, i => sequences.TryAdd(session.Issue(0).sequence, 0));
                return sequences.Count == 50;
            });
            Check(results, "39 / Server-signed file digest rejects manifest editing", () =>
            {
                string folder = Path.Combine(Path.GetTempPath(), "OUTGuardSignedCheck_" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(folder);
                string file = Path.Combine(folder, "rules.txt");
                try
                {
                    File.WriteAllText(file, "rules"); string hash = GuardIntegrity.HashFile(file);
                    using (var serverKey = new RSACryptoServiceProvider(2048))
                    {
                        serverKey.PersistKeyInCsp = false;
                        byte[] signature = serverKey.SignData(GuardIntegrity.SignatureRecord("rules.txt", hash, "release-1"), HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
                        GuardFileStatus valid = GuardIntegrity.VerifySigned(folder, "rules.txt", hash, "release-1", signature, serverKey.ExportParameters(false));
                        string changed = new string('0', 64);
                        return (valid == GuardFileStatus.Valid || valid == GuardFileStatus.UnsupportedPlatform) &&
                            GuardIntegrity.VerifySigned(folder, "rules.txt", changed, "release-1", signature, serverKey.ExportParameters(false)) == GuardFileStatus.UntrustedManifest;
                    }
                }
                finally { if (File.Exists(file)) File.Delete(file); Directory.Delete(folder); }
            });
            results.AddRange(GuardAuthoritativeChecks.Run());
            return results;
        }
    }
}
