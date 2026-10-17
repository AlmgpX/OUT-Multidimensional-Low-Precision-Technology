// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using System.IO;

using System.Security.Cryptography;



namespace OUT.Guard

{

    public enum GuardTemporalStatus { NeedsHistory, Ready, UnseenState, ContextMismatch, InvalidObservation, UnsupportedInterval }

    public enum GuardTemporalHypothesis { Normal = 0, Lag = 1, Anomaly = 2, Uncertain = 3 }

    [Flags]

    public enum GuardTemporalReason { None = 0, StrictLimitExceeded = 1, ObservationJitter = 2, ConflictingHypotheses = 4, LowSupport = 8, UnsupportedContext = 16, Invalid = 32, Gap = 64 }



    /// <summary>Ratios must use trusted game-context limits. Time is a trusted server clock.

    /// This feature stream is additional telemetry, never a replacement for authoritative simulation.</summary>

    public struct GuardTemporalObservation

    {

        public double time, speedRatio, accelerationRatio, verticalRatio, predictionRatio;

        public GuardTemporalObservation(double time, double speedRatio, double accelerationRatio, double verticalRatio, double predictionRatio)

        {

            this.time = time; this.speedRatio = speedRatio; this.accelerationRatio = accelerationRatio;

            this.verticalRatio = verticalRatio; this.predictionRatio = predictionRatio;

        }

        internal bool Valid => Finite(time) && Finite(speedRatio) && speedRatio >= 0 && Finite(accelerationRatio) && accelerationRatio >= 0 &&

            Finite(verticalRatio) && verticalRatio >= 0 && Finite(predictionRatio) && predictionRatio >= 0;

        internal static bool Finite(double v) => !double.IsNaN(v) && !double.IsInfinity(v);

    }



    public struct GuardTemporalAssessment

    {

        public GuardTemporalStatus status;

        public GuardTemporalReason reasons;

        public double normal, lag, anomaly, uncertain;

        public byte support;

        /// <summary>Review hint only. Never use it as an automatic ban or movement acceptance result.</summary>

        public bool reviewSuggested;

        public Guid modelId;

        public bool HasSupportedScore => status == GuardTemporalStatus.Ready;

    }



    /// <summary>Immutable offline-baked model. SHA256 detects accidental corruption, not malicious

    /// replacement. Load ONLY from a trusted server/developer-controlled source, after your own

    /// authenticity verification. Use GuardBuiltInTemporalModel.CreateRisk for the publisher-sealed baseline.</summary>

    public sealed class GuardTemporalModel

    {

        public const int FormatVersion = 1, FeatureSchema = 1, Cells = 4096, TableBytes = 2048;

        public const int HeaderBytes = 80, PayloadBytes = TableBytes * 5, FileBytes = HeaderBytes + PayloadBytes + 32;

        private readonly byte[] serialized;

        public Guid ModelId { get; }

        public Guid ContextId { get; }

        public double NominalInterval { get; }

        public double MaximumInterval { get; }

        public double MemorySeconds { get; }

        public double ReviewThreshold { get; }

        public byte MinimumSupport { get; }

        public int PackedModelBytes => PayloadBytes;



        private GuardTemporalModel(byte[] bytes, Guid model, Guid context, double nominal, double maximum, double memory, ushort threshold, byte support)

        {

            serialized = bytes;

            ModelId = model; ContextId = context; NominalInterval = nominal; MaximumInterval = maximum;

            MemorySeconds = memory; ReviewThreshold = threshold / 65535.0; MinimumSupport = support;

        }

        public byte[] Export() => (byte[])serialized.Clone();

        public static GuardTemporalModel Load(byte[] bytes, Guid expectedContextId)

        {

            if (bytes == null || bytes.Length != FileBytes) throw new ArgumentException("Temporal model has an invalid fixed file length.");

            // Clone once before validation; caller mutation cannot race parsing or affect loaded tables.

            var copy = (byte[])bytes.Clone();

            using (var sha = SHA256.Create())

            {

                var hash = sha.ComputeHash(copy, 0, HeaderBytes + PayloadBytes);

                int mismatch = 0; for (int i = 0; i < 32; i++) mismatch |= hash[i] ^ copy[HeaderBytes + PayloadBytes + i];

                if (mismatch != 0) throw new InvalidDataException("Temporal model checksum mismatch.");

            }

            using (var stream = new MemoryStream(copy, false))

            using (var reader = new BinaryReader(stream))

            {

                if (reader.ReadUInt32() != 0x5254474f || reader.ReadUInt16() != FormatVersion || reader.ReadUInt16() != FeatureSchema)

                    throw new InvalidDataException("Unsupported temporal model format or feature schema.");

                var model = new Guid(reader.ReadBytes(16)); var context = new Guid(reader.ReadBytes(16));

                if (model == Guid.Empty || context == Guid.Empty || context != expectedContextId) throw new InvalidDataException("Temporal model context/model identifier mismatch.");

                double nominal = reader.ReadDouble(), maximum = reader.ReadDouble(), memory = reader.ReadDouble();

                ushort threshold = reader.ReadUInt16(); byte support = reader.ReadByte();

                if (reader.ReadByte() != 0 || reader.ReadUInt16() != Cells || reader.ReadByte() != 4 || reader.ReadByte() != 0 || reader.ReadInt32() != PayloadBytes || reader.ReadUInt32() != 0)

                    throw new InvalidDataException("Invalid temporal model layout.");

                ValidateSettings(nominal, maximum, memory, support);

                return new GuardTemporalModel(copy, model, context, nominal, maximum, memory, threshold, support);

            }

        }

        internal static void ValidateSettings(double nominal, double maximum, double memory, byte support)

        {

            if (!GuardTemporalObservation.Finite(nominal) || nominal < .001 || nominal > 1 || !GuardTemporalObservation.Finite(maximum) || maximum < nominal || maximum > 10 ||

                !GuardTemporalObservation.Finite(memory) || memory < .01 || memory > 10 || support < 1 || support > 15)

                throw new InvalidDataException("Temporal model timing/support settings are outside the supported range.");

        }

        internal int Nibble(int table, int index) => (serialized[HeaderBytes + table * TableBytes + (index >> 1)] >> ((index & 1) * 4)) & 15;

        internal static byte[] Encode(Guid modelId, Guid contextId, double nominal, double maximum, double memory, ushort threshold, byte support, byte[] payload)

        {

            ValidateSettings(nominal, maximum, memory, support);

            if (modelId == Guid.Empty || contextId == Guid.Empty || payload == null || payload.Length != PayloadBytes) throw new ArgumentException("Invalid temporal model identity/payload.");

            var bytes = new byte[FileBytes];

            using (var stream = new MemoryStream(bytes, true))

            using (var writer = new BinaryWriter(stream))

            {

                writer.Write(0x5254474fU); writer.Write((ushort)FormatVersion); writer.Write((ushort)FeatureSchema);

                writer.Write(modelId.ToByteArray()); writer.Write(contextId.ToByteArray());

                writer.Write(nominal); writer.Write(maximum); writer.Write(memory); writer.Write(threshold); writer.Write(support);

                writer.Write((byte)0); writer.Write((ushort)Cells); writer.Write((byte)4); writer.Write((byte)0); writer.Write(PayloadBytes); writer.Write(0U); writer.Write(payload);

            }

            using (var sha = SHA256.Create())

            {

                var hash = sha.ComputeHash(bytes, 0, HeaderBytes + PayloadBytes);

                Buffer.BlockCopy(hash, 0, bytes, HeaderBytes + PayloadBytes, 32);

            }

            return bytes;

        }

    }



    /// <summary>Opt-in SHADOW risk only. Bounded O(1) work, short temporal memory, no online training,

    /// no position correction, no ban/acceptance API. One non-thread-safe instance per actor/context.</summary>

    public sealed class GuardTemporalRisk

    {

        private readonly GuardTemporalModel model;

        private static readonly double[] Emissions = CreateEmissions();

        private double normal = .88, lag = .07, anomaly = .02, uncertain = .03;

        private double lastTime, previousSpeed, change;

        private bool initialized, needsReset;

        public Guid ModelId => model.ModelId;

        public GuardTemporalRisk(GuardTemporalModel trustedModel) { model = trustedModel ?? throw new ArgumentNullException(nameof(trustedModel)); }

        private static double[] CreateEmissions()

        {

            var values = new double[16]; for (int i = 0; i < 16; i++) values[i] = Math.Exp((i - 15) / 2.0); return values;

        }

        /// <summary>Explicit trusted lifecycle/resynchronization only. Do not expose reset as a client command.</summary>

        public void Reset() { initialized = needsReset = false; normal = .88; lag = .07; anomaly = .02; uncertain = .03; lastTime = previousSpeed = change = 0; }

        private GuardTemporalAssessment Abstain(GuardTemporalStatus status, GuardTemporalReason reason) => new GuardTemporalAssessment

            { status = status, reasons = reason, normal = normal, lag = lag, anomaly = anomaly, uncertain = uncertain, modelId = model.ModelId };



        public GuardTemporalAssessment Observe(GuardTemporalObservation observation, Guid trustedContextId)

        {

            if (trustedContextId != model.ContextId) return Abstain(GuardTemporalStatus.ContextMismatch, GuardTemporalReason.UnsupportedContext);

            if (!observation.Valid) return Abstain(GuardTemporalStatus.InvalidObservation, GuardTemporalReason.Invalid);

            if (needsReset) return Abstain(GuardTemporalStatus.UnsupportedInterval, GuardTemporalReason.Gap);

            if (!initialized)

            {

                initialized = true; lastTime = observation.time; previousSpeed = observation.speedRatio;

                return Abstain(GuardTemporalStatus.NeedsHistory, GuardTemporalReason.None);

            }

            double dt = observation.time - lastTime;

            if (!GuardTemporalObservation.Finite(dt) || dt <= 0) return Abstain(GuardTemporalStatus.InvalidObservation, GuardTemporalReason.Invalid);

            if (dt < model.NominalInterval * .1 || dt > model.MaximumInterval)

            {

                needsReset = true;

                return Abstain(GuardTemporalStatus.UnsupportedInterval, GuardTemporalReason.Gap);

            }

            double persistence = Math.Exp(-dt / model.MemorySeconds);

            int index = Features(observation, dt, model.NominalInterval, persistence, ref previousSpeed, ref change);

            lastTime = observation.time;

            byte support = (byte)model.Nibble(4, index);

            if (support < model.MinimumSupport)

            {

                // Unknown space must abstain, never inherit an old suspicious decision.

                uncertain = 1; normal = lag = anomaly = 0;

                return new GuardTemporalAssessment { status = GuardTemporalStatus.UnseenState, reasons = GuardTemporalReason.LowSupport,

                    uncertain = 1, support = support, modelId = model.ModelId };

            }

            normal = (normal * persistence + (1 - persistence) * .88) * Emissions[model.Nibble(0, index)];

            lag = (lag * persistence + (1 - persistence) * .07) * Emissions[model.Nibble(1, index)];

            anomaly = (anomaly * persistence + (1 - persistence) * .02) * Emissions[model.Nibble(2, index)];

            uncertain = (uncertain * persistence + (1 - persistence) * .03) * Emissions[model.Nibble(3, index)];

            double sum = normal + lag + anomaly + uncertain;

            normal /= sum; lag /= sum; anomaly /= sum; uncertain /= sum;

            var reasons = GuardTemporalReason.None;

            if (Math.Max(Math.Max(observation.speedRatio, observation.accelerationRatio), Math.Max(observation.verticalRatio, observation.predictionRatio)) > 1) reasons |= GuardTemporalReason.StrictLimitExceeded;

            if (dt / model.NominalInterval > 1.5) reasons |= GuardTemporalReason.ObservationJitter;

            if (anomaly > .2 && (normal > .2 || lag > .2 || uncertain > .2)) reasons |= GuardTemporalReason.ConflictingHypotheses;

            return new GuardTemporalAssessment { status = GuardTemporalStatus.Ready, normal = normal, lag = lag, anomaly = anomaly, uncertain = uncertain,

                support = support, reasons = reasons, reviewSuggested = anomaly > model.ReviewThreshold, modelId = model.ModelId };

        }

        internal static int Features(GuardTemporalObservation observation, double dt, double nominal, double persistence, ref double previousSpeed, ref double change)

        {

            // Saturation avoids overflow even for extreme finite input; all features remain bounded.

            double delta = Math.Abs(Math.Min(16, observation.speedRatio) - Math.Min(16, previousSpeed));

            change = persistence * change + (1 - persistence) * Math.Min(16, delta * 4);

            previousSpeed = observation.speedRatio;

            return Bin(observation.speedRatio / 1.75) | (Bin(Math.Max(observation.accelerationRatio, Math.Max(observation.verticalRatio, observation.predictionRatio)) / 1.75) << 3) |

                (Bin((dt / nominal - .8) / 3.2) << 6) | (Bin(change) << 9);

        }

        private static int Bin(double value) => value <= 0 ? 0 : value >= .875 ? 7 : (int)(value * 8);

    }

}
