// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

namespace OUT.Guard
{
    public enum GuardSignal { None = 0, MemoryTamper, SavedDataTamper, MovementSpeed, MovementAcceleration, MovementVertical, MovementPattern, InvalidSample, ClockDrift, FileIntegrity, ObservationGap, TimeScaleMismatch, RiskThreshold, ClockObservationGap }
    public enum GuardSeverity { Notice, Suspicious, High }

    [Serializable]
    public struct GuardEvidence
    {
        public GuardSignal signal;
        public GuardSeverity severity;
        public string subject;
        public string detail;
        public double observed;
        public double allowed;
        public double utcSeconds;

        public GuardEvidence(GuardSignal signal, GuardSeverity severity, string subject, string detail, double observed = 0, double allowed = 0)
        {
            this.signal = signal; this.severity = severity; this.subject = subject ?? "";
            this.detail = detail ?? ""; this.observed = observed; this.allowed = allowed;
            utcSeconds = (DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds;
        }
    }

    /// <summary>Local notifications only. Subscribe on the thread on which protected values are read.</summary>
    public static class GuardEvents
    {
        public static event Action<GuardEvidence> Detected;
        public static event Action<Exception> SubscriberFailed;
        public static void Report(GuardEvidence evidence)
        {
            var handlers = Detected;
            if (handlers == null) return;
            foreach (Action<GuardEvidence> handler in handlers.GetInvocationList())
            {
                try { handler(evidence); }
                catch (Exception error) { try { SubscriberFailed?.Invoke(error); } catch { /* Reporting cannot disable a tamper rejection. */ } }
            }
        }
    }

    public sealed class GuardTamperException : InvalidOperationException
    {
        public GuardTamperException(string message) : base(message) { }
    }

    /// <summary>Accumulates evidence with time decay. It never bans, disconnects or changes game state.</summary>
    public sealed class GuardRisk
    {
        public double Score { get; private set; }
        private double lastTime;
        private bool initialized;
        public double HalfLifeSeconds { get; }
        public GuardRisk(double halfLifeSeconds = 30)
        {
            if (!Finite(halfLifeSeconds) || halfLifeSeconds <= 0) throw new ArgumentOutOfRangeException(nameof(halfLifeSeconds));
            HalfLifeSeconds = halfLifeSeconds;
        }
        public double Advance(double monotonicSeconds, double contribution = 0)
        {
            if (!Finite(monotonicSeconds) || !Finite(contribution) || contribution < 0 || (initialized && monotonicSeconds < lastTime))
                throw new ArgumentOutOfRangeException(nameof(monotonicSeconds));
            if (initialized) Score *= Math.Pow(0.5, (monotonicSeconds - lastTime) / HalfLifeSeconds);
            initialized = true; lastTime = monotonicSeconds; Score = Math.Min(100, Score + contribution);
            return Score;
        }
        public void Reset() { Score = 0; initialized = false; lastTime = 0; }
        internal static bool Finite(double x) => !double.IsNaN(x) && !double.IsInfinity(x);
    }
}
