// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace OUT.Guard
{
    /// <summary>AES-256-CBC + HMAC-SHA256, encrypt then MAC, with independent derived keys and slot binding.</summary>
    public sealed class GuardCodec : IDisposable
    {
        public const int MaximumPayloadBytes = 8 * 1024 * 1024;
        private byte[] master;
        private const int HeaderLength = 40;
        public GuardCodec(byte[] key)
        {
            if (key == null || key.Length != 32) throw new ArgumentException("A 32-byte random application key is required.", nameof(key));
            master = (byte[])key.Clone();
        }
        public static byte[] CreateKey()
        {
            var key = new byte[32]; using (var rng = RandomNumberGenerator.Create()) rng.GetBytes(key); return key;
        }
        public static string KeyToHex(byte[] key)
        {
            if (key == null || key.Length != 32) throw new ArgumentException("Expected 32 bytes.");
            return BitConverter.ToString(key).Replace("-", "").ToLowerInvariant();
        }
        public static byte[] KeyFromHex(string key)
        {
            if (key == null || key.Length != 64) throw new ArgumentException("Expected a 64-character hexadecimal key.");
            var result = new byte[32]; for (int i = 0; i < result.Length; i++) result[i] = Convert.ToByte(key.Substring(i * 2, 2), 16);
            return result;
        }
        public static byte[] EncodeName(string name, int maximumUtf8Bytes = 1024)
        {
            if (string.IsNullOrEmpty(name)) throw new ArgumentException("A nonempty name is required.", nameof(name));
            return EncodeUtf8Text(name, maximumUtf8Bytes);
        }
        public static byte[] EncodeUtf8Text(string value, int maximumUtf8Bytes)
        {
            if (value == null || maximumUtf8Bytes < 0 || value.Length > maximumUtf8Bytes) throw new ArgumentException("Text exceeds its UTF-8 byte limit.", nameof(value));
            // IL2CPP's encoder fallback behavior differs from Mono. Validate UTF-16 explicitly on every backend.
            for (int i = 0; i < value.Length; i++)
            {
                char c = value[i];
                if (c >= 0xD800 && c <= 0xDBFF)
                {
                    if (++i >= value.Length || value[i] < 0xDC00 || value[i] > 0xDFFF) throw new ArgumentException("Unpaired UTF-16 surrogate.", nameof(value));
                }
                else if (c >= 0xDC00 && c <= 0xDFFF) throw new ArgumentException("Unpaired UTF-16 surrogate.", nameof(value));
            }
            byte[] bytes = Encoding.UTF8.GetBytes(value);
            if (bytes.Length > maximumUtf8Bytes) throw new ArgumentException("Text exceeds its UTF-8 byte limit.", nameof(value));
            return bytes;
        }
        public static bool TryDecodeUtf8Text(byte[] bytes, int maximumUtf8Bytes, out string value)
        {
            value = null;
            if (bytes == null || bytes.Length > maximumUtf8Bytes) return false;
            try
            {
                string candidate = Encoding.UTF8.GetString(bytes);
                byte[] canonical = EncodeUtf8Text(candidate, maximumUtf8Bytes);
                if (canonical.Length != bytes.Length) return false;
                for (int i = 0; i < bytes.Length; i++) if (canonical[i] != bytes[i]) return false;
                value = candidate; return true;
            }
            catch (ArgumentException) { return false; }
        }
        public static byte[] DeriveUserKey(byte[] serverIssuedToken, string userId, string applicationId)
        {
            if (serverIssuedToken == null || serverIssuedToken.Length != 32) throw new ArgumentException("A high-entropy 32-byte server-issued token is required.");
            byte[] user = EncodeName(userId), application = EncodeName(applicationId);
            using (var stream = new MemoryStream())
            {
                using (var writer = new BinaryWriter(stream, Encoding.UTF8, true)) { writer.Write("OUT.Guard/user-key/v1"); writer.Write(application.Length); writer.Write(application); writer.Write(user.Length); writer.Write(user); }
                using (var mac = new HMACSHA256(serverIssuedToken)) return mac.ComputeHash(stream.ToArray());
            }
        }
        public static string UserScope(string userId)
        {
            using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(EncodeName(userId))).Replace("-", "").ToLowerInvariant();
        }
        private byte[] Derive(byte[] salt, string label)
        {
            if (master == null) throw new ObjectDisposedException(nameof(GuardCodec));
            byte[] domain = Encoding.UTF8.GetBytes("OUT.Guard/v1/" + label);
            var input = new byte[salt.Length + domain.Length];
            Buffer.BlockCopy(salt, 0, input, 0, salt.Length); Buffer.BlockCopy(domain, 0, input, salt.Length, domain.Length);
            using (var hmac = new HMACSHA256(master)) return hmac.ComputeHash(input);
        }
        private static byte[] Authenticate(byte[] key, string context, byte[] envelope, int length)
        {
            byte[] name = EncodeName(context, 4096);
            using (var hmac = new HMACSHA256(key))
            {
                byte[] prefix = { (byte)name.Length, (byte)(name.Length >> 8), (byte)(name.Length >> 16), (byte)(name.Length >> 24) };
                hmac.TransformBlock(prefix, 0, prefix.Length, prefix, 0);
                hmac.TransformBlock(name, 0, name.Length, name, 0);
                hmac.TransformFinalBlock(envelope, 0, length);
                return hmac.Hash;
            }
        }
        public byte[] Encode(byte[] payload, string context)
        {
            EncodeName(context, 4096);
            if (payload == null || payload.Length > MaximumPayloadBytes) throw new ArgumentException("Payload exceeds the 8 MiB limit.", nameof(payload));
            byte[] salt = new byte[16]; using (var rng = RandomNumberGenerator.Create()) rng.GetBytes(salt);
            byte[] encryptionKey = Derive(salt, "encryption"), macKey = Derive(salt, "authentication");
            try
            {
                using (var aes = Aes.Create())
                {
                    aes.Key = encryptionKey; aes.Mode = CipherMode.CBC; aes.Padding = PaddingMode.PKCS7; aes.GenerateIV();
                    byte[] ciphertext; using (var encryptor = aes.CreateEncryptor()) ciphertext = encryptor.TransformFinalBlock(payload, 0, payload.Length);
                    byte[] result = new byte[HeaderLength + ciphertext.Length + 32];
                    result[0] = 79; result[1] = 71; result[2] = 48; result[3] = 49;
                    Buffer.BlockCopy(salt, 0, result, 4, 16); Buffer.BlockCopy(aes.IV, 0, result, 20, 16);
                    int n = ciphertext.Length; for (int i = 0; i < 4; i++) result[36 + i] = (byte)(n >> (8 * i));
                    Buffer.BlockCopy(ciphertext, 0, result, HeaderLength, n);
                    byte[] tag = Authenticate(macKey, context, result, HeaderLength + n);
                    Buffer.BlockCopy(tag, 0, result, HeaderLength + n, 32); return result;
                }
            }
            finally { Array.Clear(encryptionKey, 0, encryptionKey.Length); Array.Clear(macKey, 0, macKey.Length); }
        }
        public bool TryDecode(byte[] envelope, string context, out byte[] payload)
        {
            payload = null;
            if (master == null) throw new ObjectDisposedException(nameof(GuardCodec));
            if (envelope == null || envelope.Length < HeaderLength + 16 + 32 || envelope.Length > MaximumPayloadBytes + HeaderLength + 16 + 32) return false;
            if (envelope[0] != 79 || envelope[1] != 71 || envelope[2] != 48 || envelope[3] != 49) return false;
            int n = envelope[36] | (envelope[37] << 8) | (envelope[38] << 16) | (envelope[39] << 24);
            if (n <= 0 || n % 16 != 0 || n != envelope.Length - HeaderLength - 32) return false;
            byte[] salt = new byte[16]; Buffer.BlockCopy(envelope, 4, salt, 0, 16);
            byte[] encryptionKey = Derive(salt, "encryption"), macKey = Derive(salt, "authentication");
            try
            {
                byte[] expected = Authenticate(macKey, context, envelope, HeaderLength + n);
                int difference = 0; for (int i = 0; i < 32; i++) difference |= expected[i] ^ envelope[HeaderLength + n + i];
                if (difference != 0) return false;
                using (var aes = Aes.Create())
                {
                    var iv = new byte[16]; Buffer.BlockCopy(envelope, 20, iv, 0, 16);
                    aes.Key = encryptionKey; aes.IV = iv; aes.Mode = CipherMode.CBC; aes.Padding = PaddingMode.PKCS7;
                    using (var decryptor = aes.CreateDecryptor()) payload = decryptor.TransformFinalBlock(envelope, HeaderLength, n);
                    if (payload.Length > MaximumPayloadBytes) { payload = null; return false; } return true;
                }
            }
            catch (CryptographicException) { payload = null; return false; }
            finally { Array.Clear(encryptionKey, 0, encryptionKey.Length); Array.Clear(macKey, 0, macKey.Length); }
        }
        public void Dispose() { if (master != null) { Array.Clear(master, 0, master.Length); master = null; } }
    }

    public enum GuardLoadStatus { Success, Missing, Invalid, IOError, StaleRevision, UnsupportedPlatform }

    /// <summary>Synchronous small-file storage. Run I/O outside hot frame loops. Slot names are hashed into filenames.</summary>
    public sealed class GuardSaveStore : IDisposable
    {
        private readonly string directory;
        private readonly GuardCodec codec;
        private readonly string scope;
        public GuardSaveStore(string directory, byte[] key, string userId = null)
        {
            if (string.IsNullOrWhiteSpace(directory)) throw new ArgumentException("A save directory is required.");
            this.directory = Path.GetFullPath(directory); codec = new GuardCodec(key);
            scope = userId == null ? "" : "user/" + GuardCodec.UserScope(userId) + "/";
        }
        public string GetSlotPath(string slot)
        {
            GuardCodec.EncodeName(slot);
            byte[] name = Encoding.UTF8.GetBytes(scope + slot);
            using (var sha = SHA256.Create()) return Path.Combine(directory, BitConverter.ToString(sha.ComputeHash(name)).Replace("-", "").ToLowerInvariant() + ".ogsave");
        }
        public void Save(string slot, byte[] payload)
        {
            string path = GetSlotPath(slot); byte[] envelope = codec.Encode(payload, "save/" + scope + slot);
            Directory.CreateDirectory(directory); string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try
            {
                using (var file = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None)) { file.Write(envelope, 0, envelope.Length); file.Flush(true); }
                if (File.Exists(path)) File.Replace(temporary, path, path + ".bak"); else File.Move(temporary, path);
            }
            finally { if (File.Exists(temporary)) File.Delete(temporary); }
        }
        public GuardLoadStatus Load(string slot, out byte[] payload, bool backup = false)
        {
            payload = null; string path = GetSlotPath(slot) + (backup ? ".bak" : "");
            try
            {
                if (!File.Exists(path)) return GuardLoadStatus.Missing;
                byte[] envelope;
                using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
                {
                    long length = stream.Length;
                    if (length > GuardCodec.MaximumPayloadBytes + 88 || length < 88) return Invalid(slot);
                    envelope = new byte[(int)length]; int offset = 0;
                    while (offset < envelope.Length) { int read = stream.Read(envelope, offset, envelope.Length - offset); if (read == 0) return Invalid(slot); offset += read; }
                }
                if (!codec.TryDecode(envelope, "save/" + scope + slot, out payload)) return Invalid(slot);
                return GuardLoadStatus.Success;
            }
            catch (IOException) { return GuardLoadStatus.IOError; }
            catch (UnauthorizedAccessException) { return GuardLoadStatus.IOError; }
            catch (PlatformNotSupportedException) { return GuardLoadStatus.UnsupportedPlatform; }
        }
        public void SaveVersioned(string slot, byte[] payload, long serverRevision)
        {
            if (scope.Length == 0) throw new InvalidOperationException("Versioned saves require a user-bound store.");
            if (serverRevision < 0 || payload == null || payload.Length > GuardCodec.MaximumPayloadBytes - 12) throw new ArgumentOutOfRangeException(nameof(serverRevision));
            byte[] record = new byte[payload.Length + 12]; record[0] = 79; record[1] = 71; record[2] = 83; record[3] = 50;
            for (int i = 0; i < 8; i++) record[4 + i] = (byte)((ulong)serverRevision >> (8 * i));
            Buffer.BlockCopy(payload, 0, record, 12, payload.Length); Save(slot, record);
        }
        public GuardLoadStatus LoadVersioned(string slot, long trustedMinimumServerRevision, out byte[] payload, out long revision, bool backup = false)
        {
            payload = null; revision = -1;
            if (scope.Length == 0 || trustedMinimumServerRevision < 0) throw new ArgumentException("Use a user-bound store and a trusted nonnegative server revision.");
            var status = Load(slot, out byte[] record, backup);
            if (status != GuardLoadStatus.Success) return status;
            if (record.Length < 12 || record[0] != 79 || record[1] != 71 || record[2] != 83 || record[3] != 50) return Invalid(slot);
            ulong raw = 0; for (int i = 0; i < 8; i++) raw |= (ulong)record[4 + i] << (8 * i);
            if (raw > long.MaxValue) return Invalid(slot); revision = (long)raw;
            if (revision < trustedMinimumServerRevision) return GuardLoadStatus.StaleRevision;
            payload = new byte[record.Length - 12]; Buffer.BlockCopy(record, 12, payload, 0, payload.Length); return GuardLoadStatus.Success;
        }
        public bool ProbeAtomicReplace(out string reason)
        {
            string slot = "OUTGuard.Capability." + Guid.NewGuid().ToString("N");
            try { Save(slot, new byte[] { 1 }); Save(slot, new byte[] { 2 }); reason = "Atomic replacement succeeded on this filesystem."; return true; }
            catch (Exception error) when (error is IOException || error is UnauthorizedAccessException || error is PlatformNotSupportedException) { reason = error.GetType().Name + ": " + error.Message; return false; }
            finally { try { Delete(slot); } catch (IOException) { } catch (UnauthorizedAccessException) { } }
        }
        private static GuardLoadStatus Invalid(string slot)
        {
            GuardEvents.Report(new GuardEvidence(GuardSignal.SavedDataTamper, GuardSeverity.High, slot, "Saved data could not be authenticated. No data was applied."));
            return GuardLoadStatus.Invalid;
        }
        public void Delete(string slot)
        {
            string path = GetSlotPath(slot); if (File.Exists(path)) File.Delete(path); if (File.Exists(path + ".bak")) File.Delete(path + ".bak");
        }
        public void Dispose() { codec.Dispose(); }
    }
}
