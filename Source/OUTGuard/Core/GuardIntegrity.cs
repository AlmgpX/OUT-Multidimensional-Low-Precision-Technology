// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace OUT.Guard
{
    public enum GuardFileStatus { Valid, Missing, Modified, InvalidPath, IOError, UnsupportedPlatform, UntrustedManifest }
    public static class GuardIntegrity
    {
        public static byte[] SignatureRecord(string relativePath, string expectedSha256, string releaseId)
        {
            GuardCodec.EncodeName(relativePath); GuardCodec.EncodeName(releaseId);
            if (expectedSha256 == null || expectedSha256.Length != 64) throw new ArgumentException("A SHA-256 hex digest is required.");
            foreach (char c in expectedSha256) if (!Uri.IsHexDigit(c)) throw new ArgumentException("Invalid SHA-256 hex digest.");
            using (var stream = new MemoryStream())
            {
                using (var writer = new BinaryWriter(stream, new UTF8Encoding(false, true), true)) { writer.Write("OUT.Guard/file/v1"); writer.Write(releaseId); writer.Write(relativePath); writer.Write(expectedSha256.ToLowerInvariant()); }
                return stream.ToArray();
            }
        }
        public static GuardFileStatus VerifySigned(string rootDirectory, string relativePath, string expectedSha256, string releaseId, byte[] signature, RSAParameters trustedServerPublicKey)
        {
            if (signature == null || signature.Length > 1024) return GuardFileStatus.UntrustedManifest;
            byte[] modulus = trustedServerPublicKey.Modulus;
            if (modulus == null || modulus.Length == 0 || modulus.Length > 1024) return GuardFileStatus.UntrustedManifest;
            int first = 0; while (first < modulus.Length && modulus[first] == 0) first++;
            if (first == modulus.Length) return GuardFileStatus.UntrustedManifest;
            int highBits = 0; for (int value = modulus[first]; value != 0; value >>= 1) highBits++;
            if ((modulus.Length - first - 1) * 8 + highBits < 2048) return GuardFileStatus.UntrustedManifest;
            try
            {
                using (var rsa = RSA.Create())
                {
                    rsa.ImportParameters(trustedServerPublicKey);
                    if (!rsa.VerifyData(SignatureRecord(relativePath, expectedSha256, releaseId), signature, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1)) return GuardFileStatus.UntrustedManifest;
                }
                return Verify(rootDirectory, relativePath, expectedSha256);
            }
            catch (CryptographicException) { return GuardFileStatus.UntrustedManifest; }
            catch (ArgumentException) { return GuardFileStatus.InvalidPath; }
        }
        public static string HashFile(string path)
        {
            using (var stream = File.OpenRead(path)) using (var sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
        }
        public static GuardFileStatus Verify(string rootDirectory, string relativePath, string expectedSha256)
        {
            if (Environment.OSVersion.Platform != PlatformID.Win32NT) return GuardFileStatus.UnsupportedPlatform;
            if (string.IsNullOrEmpty(rootDirectory) || string.IsNullOrEmpty(relativePath) || Path.IsPathRooted(relativePath) || expectedSha256 == null || expectedSha256.Length != 64)
                return GuardFileStatus.InvalidPath;
            try
            {
                string root = Path.GetFullPath(rootDirectory).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
                string full = Path.GetFullPath(Path.Combine(root, relativePath));
                var comparison = Path.DirectorySeparatorChar == '\\' ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
                if (!full.StartsWith(root, comparison)) return GuardFileStatus.InvalidPath;
                if (!File.Exists(full)) return GuardFileStatus.Missing;
                // Do not follow a file/directory reparse point out of the declared content root.
                string current = full;
                while (current != null && current.Length >= root.Length)
                {
                    if ((File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) return GuardFileStatus.InvalidPath;
                    current = Path.GetDirectoryName(current);
                }
                return string.Equals(HashFile(full), expectedSha256, StringComparison.OrdinalIgnoreCase) ? GuardFileStatus.Valid : GuardFileStatus.Modified;
            }
            catch (ArgumentException) { return GuardFileStatus.InvalidPath; }
            catch (NotSupportedException) { return GuardFileStatus.InvalidPath; }
            catch (IOException) { return GuardFileStatus.IOError; }
            catch (UnauthorizedAccessException) { return GuardFileStatus.IOError; }
        }
    }
}
