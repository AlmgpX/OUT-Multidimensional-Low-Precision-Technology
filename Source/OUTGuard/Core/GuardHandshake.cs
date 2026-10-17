// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using System.IO;

using System.Security.Cryptography;

using System.Text;



namespace OUT.Guard

{

    [Serializable]

    public struct GuardChallenge

    {

        public long sequence;

        public string nonce;

        public double expiresAt;

    }

    [Serializable]

    public struct GuardReply

    {

        public long sequence;

        public string nonce;

        public byte[] stateDigest;

        public byte[] authentication;

    }

    public enum GuardReplyStatus { Accepted, ReplayOrUnknown, Expired, BadAuthentication, StateMismatch }



    /// <summary>JSON DTO shared by Unity JsonUtility and System.Text.Json, with explicit Base64 binary fields.</summary>

    [Serializable]

    public struct GuardWireReply

    {

        public long sequence;

        public string nonce;

        public string stateDigest;

        public string authentication;

        public static GuardWireReply FromReply(GuardReply reply) => new GuardWireReply

        {

            sequence = reply.sequence, nonce = reply.nonce,

            stateDigest = Convert.ToBase64String(reply.stateDigest), authentication = Convert.ToBase64String(reply.authentication)

        };

        public bool TryGetReply(out GuardReply reply)

        {

            reply = default;

            if (sequence <= 0 || nonce == null || nonce.Length != 44 || stateDigest == null || stateDigest.Length != 44 || authentication == null || authentication.Length != 44) return false;

            try

            {

                byte[] digest = Convert.FromBase64String(stateDigest), mac = Convert.FromBase64String(authentication);

                if (digest.Length != 32 || mac.Length != 32) return false;

                reply = new GuardReply { sequence = sequence, nonce = nonce, stateDigest = digest, authentication = mac }; return true;

            }

            catch (FormatException) { return false; }

        }

    }



    /// <summary>Application-level challenge authentication. Use TLS and an authenticated session; this is not remote attestation.</summary>

    public static class GuardHandshake

    {

        internal static bool Equal(byte[] a, byte[] b)

        {

            if (a == null || b == null || a.Length != b.Length) return false;

            int difference = 0; for (int i = 0; i < a.Length; i++) difference |= a[i] ^ b[i]; return difference == 0;

        }

        internal static byte[] Authenticate(byte[] key, GuardChallenge challenge, byte[] digest)

        {

            if (key == null || key.Length != 32) throw new ArgumentException("A random 32-byte per-session key is required.");

            using (var stream = new MemoryStream())

            {

                using (var writer = new BinaryWriter(stream, Encoding.UTF8, true))

                {

                    writer.Write("OUT.Guard/challenge/v1"); writer.Write(challenge.sequence);

                    writer.Write(challenge.nonce); writer.Write(challenge.expiresAt); writer.Write(digest);

                }

                using (var hmac = new HMACSHA256(key)) return hmac.ComputeHash(stream.ToArray());

            }

        }

        public static byte[] Digest(byte[] canonicalState)

        {

            if (canonicalState == null || canonicalState.Length > 65536) throw new ArgumentException("Canonical checkpoint state must be at most 64 KiB.");

            using (var sha = SHA256.Create()) return sha.ComputeHash(canonicalState);

        }

        public static GuardReply CreateReply(byte[] sessionKey, GuardChallenge challenge, byte[] observedCanonicalState)

        {

            if (challenge.sequence <= 0 || challenge.nonce == null || challenge.nonce.Length != 44 || !GuardRisk.Finite(challenge.expiresAt)) throw new ArgumentException("Malformed challenge.");

            byte[] digest = Digest(observedCanonicalState);

            return new GuardReply { sequence = challenge.sequence, nonce = challenge.nonce, stateDigest = digest, authentication = Authenticate(sessionKey, challenge, digest) };

        }

    }



    /// <summary>Keep one instance per authenticated connection, on the server only. Methods synchronize session state; game transactions need their own account lock.</summary>

    public sealed class GuardChallengeSession : IDisposable

    {

        private readonly byte[] key;

        private readonly object sync = new object();

        private long sequence;

        private GuardChallenge pending;

        private bool hasPending, disposed;

        private double lastTime;

        private bool hasTime;

        public GuardChallengeSession(byte[] sessionKey)

        {

            if (sessionKey == null || sessionKey.Length != 32) throw new ArgumentException("Expected 32-byte session key.");

            key = (byte[])sessionKey.Clone();

        }

        private void CheckTime(double now)

        {

            if (disposed) throw new ObjectDisposedException(nameof(GuardChallengeSession));

            if (!GuardRisk.Finite(now) || (hasTime && now < lastTime)) throw new ArgumentOutOfRangeException(nameof(now), "Use the server's monotonic clock.");

            hasTime = true; lastTime = now;

        }

        public GuardChallenge Issue(double serverMonotonicSeconds, double lifetimeSeconds = 10)

        {

            lock (sync)

            {

            CheckTime(serverMonotonicSeconds);

            if (!GuardRisk.Finite(lifetimeSeconds) || lifetimeSeconds <= 0 || lifetimeSeconds > 120) throw new ArgumentOutOfRangeException(nameof(lifetimeSeconds));

            double expires = serverMonotonicSeconds + lifetimeSeconds;

            if (!GuardRisk.Finite(expires)) throw new ArgumentOutOfRangeException(nameof(serverMonotonicSeconds));

            pending = new GuardChallenge { sequence = checked(++sequence), nonce = Convert.ToBase64String(GuardCodec.CreateKey()), expiresAt = expires };

            hasPending = true; return pending;

            }

        }

        public GuardReplyStatus Verify(GuardReply reply, byte[] authoritativeCanonicalState, double serverMonotonicSeconds)

        {

            lock (sync)

            {

            CheckTime(serverMonotonicSeconds);

            if (!hasPending || reply.sequence != pending.sequence || reply.nonce != pending.nonce) return GuardReplyStatus.ReplayOrUnknown;

            if (serverMonotonicSeconds >= pending.expiresAt) { hasPending = false; return GuardReplyStatus.Expired; }

            if (reply.stateDigest == null || reply.stateDigest.Length != 32 || reply.authentication == null || reply.authentication.Length != 32) return GuardReplyStatus.BadAuthentication;

            byte[] expected = GuardHandshake.Authenticate(key, pending, reply.stateDigest);

            if (!GuardHandshake.Equal(expected, reply.authentication)) return GuardReplyStatus.BadAuthentication;

            hasPending = false;

            return GuardHandshake.Equal(reply.stateDigest, GuardHandshake.Digest(authoritativeCanonicalState)) ? GuardReplyStatus.Accepted : GuardReplyStatus.StateMismatch;

            }

        }

        public void Dispose() { lock (sync) { if (disposed) return; Array.Clear(key, 0, key.Length); disposed = true; hasPending = false; } }

    }



    /// <summary>Server-side token budget for damage requests, rewards or action rates. Define costs from trusted game rules.</summary>

    public sealed class GuardDeltaBudget

    {

        private readonly double capacity, refillPerSecond;

        private readonly object sync = new object();

        private double tokens, lastTime;

        private bool initialized;

        public double Available { get { lock (sync) return tokens; } }

        public double Refresh(double serverMonotonicSeconds)

        {

            lock (sync)

            {

                if (!GuardRisk.Finite(serverMonotonicSeconds) || (initialized && serverMonotonicSeconds < lastTime)) throw new ArgumentOutOfRangeException(nameof(serverMonotonicSeconds));

                Refill(serverMonotonicSeconds); return tokens;

            }

        }

        private void Refill(double now)

        {

            if (initialized) tokens = Math.Min(capacity, tokens + (now - lastTime) * refillPerSecond);

            initialized = true; lastTime = now;

        }

        public GuardDeltaBudget(double capacity, double refillPerSecond)

        {

            if (!GuardRisk.Finite(capacity) || capacity <= 0 || !GuardRisk.Finite(refillPerSecond) || refillPerSecond < 0) throw new ArgumentOutOfRangeException(nameof(capacity));

            this.capacity = capacity; this.refillPerSecond = refillPerSecond; tokens = capacity;

        }

        public bool TrySpend(double cost, double serverMonotonicSeconds)

        {

            lock (sync)

            {

            if (!GuardRisk.Finite(cost) || cost <= 0 || !GuardRisk.Finite(serverMonotonicSeconds) || (initialized && serverMonotonicSeconds < lastTime)) return false;

            Refill(serverMonotonicSeconds);

            if (cost > tokens) return false;

            tokens -= cost; return true;

            }

        }

    }

}
