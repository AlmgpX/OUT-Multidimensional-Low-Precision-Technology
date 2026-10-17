// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;



namespace OUT.Guard

{

    /// <summary>Four normalized features, eight bins per feature, sixteen risk profiles: 2048 packed bytes.</summary>

    public sealed class GuardBehaviorTable

    {

        public const int PackedBytes = 2048;

        private readonly byte[] packed;

        public GuardBehaviorTable(byte[] packed)

        {

            if (packed == null || packed.Length != PackedBytes) throw new ArgumentException("A behavior table requires exactly 2048 packed bytes.");

            this.packed = (byte[])packed.Clone();

        }

        public byte[] Export() => (byte[])packed.Clone();

        public static int Bin(double normalizedRatio)

        {

            if (!GuardRisk.Finite(normalizedRatio) || normalizedRatio < 0) throw new ArgumentOutOfRangeException(nameof(normalizedRatio));

            return normalizedRatio >= 1.75 ? 7 : (int)(normalizedRatio * 4);

        }

        public double Evaluate(double speedRatio, double accelerationRatio, double verticalRatio, double predictionRatio)

        {

            int index = Bin(speedRatio) | (Bin(accelerationRatio) << 3) | (Bin(verticalRatio) << 6) | (Bin(predictionRatio) << 9);

            return ((packed[index >> 1] >> ((index & 1) * 4)) & 15) / 15.0;

        }

        public static GuardBehaviorTable CreateDefault()

        {

            var bytes = new byte[PackedBytes];

            for (int index = 0; index < 4096; index++)

            {

                int a = index & 7, b = (index >> 3) & 7, c = (index >> 6) & 7, d = (index >> 9) & 7;

                int excess = Math.Max(0, Math.Max(Math.Max(a, b), Math.Max(c, d)) - 4);

                int corroboration = (a >= 5 ? 1 : 0) + (b >= 5 ? 1 : 0) + (c >= 5 ? 1 : 0) + (d >= 5 ? 1 : 0);

                int profile = Math.Min(15, excess * 3 + Math.Max(0, corroboration - 1) * 2);

                bytes[index >> 1] |= (byte)(profile << ((index & 1) * 4));

            }

            return new GuardBehaviorTable(bytes);

        }

    }



    [Serializable]

    public sealed class GuardMovementLimits

    {

        public double maximumSpeed = 8;

        public double maximumAcceleration = 60;

        public double maximumVerticalSpeed = 20;

        public double maximumPredictionError = 2;

        public double relativeTolerance = .1;

        public double maximumSampleGap = .5;

        public double minimumSampleInterval = .001;

        public double tableEvidenceThreshold = .8;

        public void Validate()

        {

            double[] positive = { maximumSpeed, maximumAcceleration, maximumVerticalSpeed, maximumPredictionError, maximumSampleGap, minimumSampleInterval };

            foreach (double x in positive) if (!GuardRisk.Finite(x) || x <= 0) throw new ArgumentOutOfRangeException("Movement limits must be finite and positive.");

            if (!GuardRisk.Finite(relativeTolerance) || relativeTolerance < 0 || relativeTolerance > 1 || maximumSampleGap < minimumSampleInterval)

                throw new ArgumentOutOfRangeException("Invalid movement tolerance or sample interval.");

            if (!GuardRisk.Finite(tableEvidenceThreshold) || tableEvidenceThreshold <= 0 || tableEvidenceThreshold > 1) throw new ArgumentOutOfRangeException(nameof(tableEvidenceThreshold));

        }

        internal GuardMovementLimits Copy() => (GuardMovementLimits)MemberwiseClone();

    }



    [Flags]

    public enum GuardMovementViolation { None = 0, Speed = 1, Acceleration = 2, Vertical = 4, Prediction = 8, Policy = 16, Gap = 32, Invalid = 64 }

    public struct GuardMovementResult

    {

        public bool accepted;

        public bool hasNormalizedFeatures;

        public double speedRatio, accelerationRatio, verticalRatio, predictionRatio;

        public bool skipped;

        public GuardMovementViolation violations;

        public bool hasEvidence;

        public bool rebased;

        public GuardSignal signal;

        public double observed, allowed, tableRisk;

        public GuardEvidence ToEvidence(string subject) => new GuardEvidence(signal,

            signal == GuardSignal.ObservationGap ? GuardSeverity.Notice : GuardSeverity.Suspicious, subject,

            signal == GuardSignal.ObservationGap ? "Observation gap: position held; trusted resynchronization required." :

            signal == GuardSignal.InvalidSample ? "Invalid movement observation rejected." : "Movement violations: " + violations + ".", observed, allowed);

    }



    /// <summary>One tracker per actor. Feed trusted timestamps and authoritative observations on the server.</summary>

    public sealed class GuardMovementTracker

    {

        private readonly GuardMovementLimits limits;

        private readonly GuardBehaviorTable table;

        private double lastX, lastY, lastZ, lastTime, lastObservationTime, vx, vy, vz;

        private bool initialized, hasVelocity, hasObservedTime;

        public GuardMovementTracker(GuardMovementLimits limits = null, GuardBehaviorTable table = null)

        {

            this.limits = (limits ?? new GuardMovementLimits()).Copy(); this.limits.Validate();

            this.table = table ?? GuardBehaviorTable.CreateDefault();

        }

        public void Reset() { initialized = hasVelocity = hasObservedTime = false; }

        public GuardMovementResult Observe(double time, double x, double y, double z, bool authorizedTeleport = false)

        {

            if (!GuardRisk.Finite(time)) return new GuardMovementResult { hasEvidence = true, signal = GuardSignal.InvalidSample, violations = GuardMovementViolation.Invalid };

            if (hasObservedTime && time <= lastObservationTime)

                return new GuardMovementResult { hasEvidence = true, signal = GuardSignal.InvalidSample, violations = GuardMovementViolation.Invalid, observed = time, allowed = lastObservationTime };

            lastObservationTime = time; hasObservedTime = true;

            if (!GuardRisk.Finite(x) || !GuardRisk.Finite(y) || !GuardRisk.Finite(z))

                return new GuardMovementResult { hasEvidence = true, signal = GuardSignal.InvalidSample, violations = GuardMovementViolation.Invalid };

            double dt = initialized ? time - lastTime : 0;

            if (!initialized || authorizedTeleport)

            {

                initialized = true; hasVelocity = false; lastTime = time; lastX = x; lastY = y; lastZ = z;

                return new GuardMovementResult { accepted = true, rebased = true };

            }

            if (dt > limits.maximumSampleGap)

                return new GuardMovementResult { hasEvidence = true, signal = GuardSignal.ObservationGap, violations = GuardMovementViolation.Gap, observed = dt, allowed = limits.maximumSampleGap };

            if (dt < limits.minimumSampleInterval) return new GuardMovementResult { skipped = true };

            double dx = x - lastX, dy = y - lastY, dz = z - lastZ;

            double nextVx = dx / dt, nextVy = dy / dt, nextVz = dz / dt;

            double speed = Math.Sqrt(nextVx * nextVx + nextVz * nextVz);

            double ax = (nextVx - vx) / dt, az = (nextVz - vz) / dt;

            double acceleration = hasVelocity ? Math.Sqrt(ax * ax + az * az) : 0;

            double px = dx - vx * dt, py = dy - vy * dt, pz = dz - vz * dt;

            double prediction = hasVelocity ? Math.Sqrt(px * px + py * py + pz * pz) : 0;

            // Extreme finite inputs may overflow intermediate arithmetic. Reject before table indexing.

            if (!GuardRisk.Finite(speed) || !GuardRisk.Finite(acceleration) || !GuardRisk.Finite(prediction) || !GuardRisk.Finite(nextVy))

                return new GuardMovementResult { hasEvidence = true, signal = GuardSignal.InvalidSample, violations = GuardMovementViolation.Invalid };

            double tolerance = 1 + limits.relativeTolerance;

            double sr = speed / (limits.maximumSpeed * tolerance), ar = acceleration / (limits.maximumAcceleration * tolerance);

            double vr = Math.Abs(nextVy) / (limits.maximumVerticalSpeed * tolerance), pr = prediction / (limits.maximumPredictionError * tolerance);

            var result = new GuardMovementResult { hasNormalizedFeatures = true, speedRatio = sr, accelerationRatio = ar, verticalRatio = vr, predictionRatio = pr, tableRisk = table.Evaluate(sr, ar, vr, pr) };

            if (sr > 1) result.violations |= GuardMovementViolation.Speed;

            if (vr > 1) result.violations |= GuardMovementViolation.Vertical;

            if (ar > 1) result.violations |= GuardMovementViolation.Acceleration;

            if (pr > 1) result.violations |= GuardMovementViolation.Prediction;

            if (result.tableRisk >= limits.tableEvidenceThreshold) result.violations |= GuardMovementViolation.Policy;

            if (sr > 1) { result.hasEvidence = true; result.signal = GuardSignal.MovementSpeed; result.observed = speed; result.allowed = limits.maximumSpeed * tolerance; }

            else if (vr > 1) { result.hasEvidence = true; result.signal = GuardSignal.MovementVertical; result.observed = Math.Abs(nextVy); result.allowed = limits.maximumVerticalSpeed * tolerance; }

            else if (ar > 1) { result.hasEvidence = true; result.signal = GuardSignal.MovementAcceleration; result.observed = acceleration; result.allowed = limits.maximumAcceleration * tolerance; }

            else if (pr > 1) { result.hasEvidence = true; result.signal = GuardSignal.MovementPattern; result.observed = prediction; result.allowed = limits.maximumPredictionError * tolerance; }

            else if (result.tableRisk >= limits.tableEvidenceThreshold) { result.hasEvidence = true; result.signal = GuardSignal.MovementPattern; result.observed = result.tableRisk; result.allowed = limits.tableEvidenceThreshold; }

            // A rejected position never becomes the trusted baseline.

            if (!result.hasEvidence) { result.accepted = true; lastTime = time; lastX = x; lastY = y; lastZ = z; vx = nextVx; vy = nextVy; vz = nextVz; hasVelocity = true; }

            return result;

        }

    }

}
