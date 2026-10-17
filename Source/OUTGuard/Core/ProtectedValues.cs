// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;
using System.Runtime.InteropServices;
using System.Security.Cryptography;

namespace OUT.Guard
{
    internal static class MemorySeal
    {
        private static readonly byte[] Secret = GuardCodec.CreateKey();
        [ThreadStatic] private static HMACSHA256 authenticator;
        internal static ulong Random64()
        {
            var bytes = new byte[8];
            using (var rng = RandomNumberGenerator.Create()) rng.GetBytes(bytes);
            return BitConverter.ToUInt64(bytes, 0);
        }
        internal static ulong Tag(ulong value, ulong key, ulong binding = 0)
        {
            // Authenticate the ciphertext and per-value key separately. The process still owns its secret.
            if (authenticator == null) authenticator = new HMACSHA256(Secret);
            Span<byte> input = stackalloc byte[24];
            Span<byte> output = stackalloc byte[32];
            for (int i = 0; i < 8; i++) { input[i] = (byte)(value >> (i * 8)); input[8 + i] = (byte)(key >> (i * 8)); input[16 + i] = (byte)(binding >> (i * 8)); }
            if (!authenticator.TryComputeHash(input, output, out int written) || written != 32) throw new CryptographicException("Memory authentication failed.");
            ulong tag = 0; for (int i = 0; i < 8; i++) tag |= (ulong)output[i] << (i * 8);
            return tag;
        }
        internal static void Reject()
        {
            GuardEvents.Report(new GuardEvidence(GuardSignal.MemoryTamper, GuardSeverity.High, "Protected value", "Integrity check failed; value rejected."));
            throw new GuardTamperException("OUT Guard rejected a modified protected value.");
        }
    }

    /// <summary>Keep the expected binding at the call site, separately from the protected struct.</summary>
    public sealed class GuardBinding
    {
        internal ulong Id { get; }
        public GuardBinding(string fieldIdentity)
        {
            byte[] name = GuardCodec.EncodeName(fieldIdentity);
            using (var sha = SHA256.Create())
            {
                byte[] digest = sha.ComputeHash(name); ulong id = 0; for (int i = 0; i < 8; i++) id |= (ulong)digest[i] << (8 * i);
                Id = id == 0 ? 1UL : id;
            }
        }
    }

    /// <summary>Runtime-only value. Persist its decoded value with GuardPrefs or GuardSaveStore.</summary>
    public struct ProtectedLong : IEquatable<ProtectedLong>
    {
        private ulong encoded, key, tag;
        private ulong bindingId;
        private bool initialized;
        public ProtectedLong(long value)
        {
            bindingId = 0; key = MemorySeal.Random64(); encoded = unchecked((ulong)value) ^ key;
            tag = MemorySeal.Tag(encoded, key); initialized = true;
        }
        public ProtectedLong(long value, GuardBinding binding)
        {
            if (binding == null) throw new ArgumentNullException(nameof(binding));
            bindingId = binding.Id; key = MemorySeal.Random64(); encoded = unchecked((ulong)value) ^ key;
            tag = MemorySeal.Tag(encoded, key, bindingId); initialized = true;
        }
        public long Read(GuardBinding expectedBinding)
        {
            if (expectedBinding == null) throw new ArgumentNullException(nameof(expectedBinding));
            if (!initialized || bindingId != expectedBinding.Id || tag != MemorySeal.Tag(encoded, key, bindingId)) MemorySeal.Reject();
            return unchecked((long)(encoded ^ key));
        }
        public long Value
        {
            get
            {
                if (bindingId != 0) throw new InvalidOperationException("Read a bound value with Read(expectedBinding).");
                if (!initialized) { if (encoded != 0 || key != 0 || tag != 0) MemorySeal.Reject(); return 0; }
                if (tag != MemorySeal.Tag(encoded, key)) MemorySeal.Reject();
                return unchecked((long)(encoded ^ key));
            }
        }
        public void Rekey() { this = new ProtectedLong(Value); }
        public static implicit operator ProtectedLong(long value) => new ProtectedLong(value);
        public static implicit operator long(ProtectedLong value) => value.Value;
        public bool Equals(ProtectedLong other) => Value == other.Value;
        public override bool Equals(object other) => other is ProtectedLong v && Equals(v);
        public override int GetHashCode() => Value.GetHashCode();
        public override string ToString() => Value.ToString(System.Globalization.CultureInfo.InvariantCulture);
    }

    public struct ProtectedInt : IEquatable<ProtectedInt>
    {
        private ProtectedLong storage;
        public ProtectedInt(int value) { storage = new ProtectedLong(value); }
        public ProtectedInt(int value, GuardBinding binding) { storage = new ProtectedLong(value, binding); }
        public int Read(GuardBinding binding) { long value = storage.Read(binding); if (value < int.MinValue || value > int.MaxValue) MemorySeal.Reject(); return (int)value; }
        public int Value { get { long value = storage.Value; if (value < int.MinValue || value > int.MaxValue) MemorySeal.Reject(); return (int)value; } }
        public void Rekey() { storage.Rekey(); }
        public static implicit operator ProtectedInt(int value) => new ProtectedInt(value);
        public static implicit operator int(ProtectedInt value) => value.Value;
        public static ProtectedInt operator ++(ProtectedInt value) => new ProtectedInt(checked(value.Value + 1));
        public static ProtectedInt operator --(ProtectedInt value) => new ProtectedInt(checked(value.Value - 1));
        public static ProtectedInt operator +(ProtectedInt a, int b) => new ProtectedInt(checked(a.Value + b));
        public static ProtectedInt operator -(ProtectedInt a, int b) => new ProtectedInt(checked(a.Value - b));
        public bool Equals(ProtectedInt other) => Value == other.Value;
        public override bool Equals(object other) => other is ProtectedInt v && Equals(v);
        public override int GetHashCode() => Value;
        public override string ToString() => Value.ToString(System.Globalization.CultureInfo.InvariantCulture);
    }

    public struct ProtectedFloat : IEquatable<ProtectedFloat>
    {
        [StructLayout(LayoutKind.Explicit)] private struct Bits { [FieldOffset(0)] public float number; [FieldOffset(0)] public int bits; }
        private ProtectedInt storage;
        public ProtectedFloat(float value) { storage = new ProtectedInt(new Bits { number = value }.bits); }
        public ProtectedFloat(float value, GuardBinding binding) { storage = new ProtectedInt(new Bits { number = value }.bits, binding); }
        public float Read(GuardBinding binding) => new Bits { bits = storage.Read(binding) }.number;
        public float Value => new Bits { bits = storage.Value }.number;
        public void Rekey() { storage.Rekey(); }
        public static implicit operator ProtectedFloat(float value) => new ProtectedFloat(value);
        public static implicit operator float(ProtectedFloat value) => value.Value;
        public static ProtectedFloat operator +(ProtectedFloat value, float other) => new ProtectedFloat(value.Value + other);
        public static ProtectedFloat operator -(ProtectedFloat value, float other) => new ProtectedFloat(value.Value - other);
        public static ProtectedFloat operator *(ProtectedFloat value, float other) => new ProtectedFloat(value.Value * other);
        public static ProtectedFloat operator /(ProtectedFloat value, float other) => new ProtectedFloat(value.Value / other);
        public bool Equals(ProtectedFloat other) => Value.Equals(other.Value);
        public override bool Equals(object other) => other is ProtectedFloat value && Equals(value);
        public override int GetHashCode() => Value.GetHashCode();
        public override string ToString() => Value.ToString(System.Globalization.CultureInfo.InvariantCulture);
    }

    public struct ProtectedBool
    {
        private ProtectedInt storage;
        public ProtectedBool(bool value) { storage = new ProtectedInt(value ? 1 : 0); }
        public ProtectedBool(bool value, GuardBinding binding) { storage = new ProtectedInt(value ? 1 : 0, binding); }
        public bool Read(GuardBinding binding) { int value = storage.Read(binding); if (value != 0 && value != 1) MemorySeal.Reject(); return value == 1; }
        public bool Value { get { int value = storage.Value; if (value != 0 && value != 1) MemorySeal.Reject(); return value == 1; } }
        public static implicit operator ProtectedBool(bool value) => new ProtectedBool(value);
        public static implicit operator bool(ProtectedBool value) => value.Value;
        public override string ToString() => Value.ToString();
    }
}
