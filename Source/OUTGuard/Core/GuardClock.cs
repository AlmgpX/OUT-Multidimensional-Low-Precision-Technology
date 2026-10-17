// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

namespace OUT.Guard
{
    /// <summary>Compares accumulated engine time with an independent monotonic clock. A signal is not proof of cheating.</summary>
    public sealed class GuardClock
    {
        private double engineElapsed, lastReference, windowStart;
        private bool initialized;
        private bool suspended;
        private bool skipNextDelta;
        private int observedWindows;
        private double fastDebt, slowDebt, accumulatedEngine, accumulatedReference;
        public double WindowSeconds { get; }
        public double RelativeTolerance { get; }
        public int RequiredWindows { get; }
        public GuardClock(double windowSeconds = 5, double relativeTolerance = .15, int requiredWindows = 3)
        {
            if (!GuardRisk.Finite(windowSeconds) || windowSeconds < .1 || !GuardRisk.Finite(relativeTolerance) || relativeTolerance <= 0 || relativeTolerance >= 1 || requiredWindows < 1)
                throw new ArgumentOutOfRangeException(nameof(windowSeconds));
            WindowSeconds = windowSeconds; RelativeTolerance = relativeTolerance; RequiredWindows = requiredWindows;
        }
        public void Reset() { initialized = suspended = skipNextDelta = false; engineElapsed = 0; observedWindows = 0; fastDebt = slowDebt = accumulatedEngine = accumulatedReference = 0; }
        public void Start(double referenceSeconds)
        {
            if (!GuardRisk.Finite(referenceSeconds)) throw new ArgumentOutOfRangeException(nameof(referenceSeconds));
            Reset(); initialized = true; lastReference = windowStart = referenceSeconds;
        }
        public bool Suspend(double referenceSeconds, out double ratio)
        {
            ratio = 1;
            if (!initialized || suspended || !GuardRisk.Finite(referenceSeconds) || referenceSeconds < lastReference) { suspended = true; return false; }
            bool detected = !skipNextDelta && referenceSeconds > windowStart && CloseWindow(referenceSeconds, out ratio);
            suspended = true; lastReference = windowStart = referenceSeconds; engineElapsed = 0; return detected;
        }
        public void Resume(double referenceSeconds)
        {
            if (!GuardRisk.Finite(referenceSeconds)) throw new ArgumentOutOfRangeException(nameof(referenceSeconds));
            // Keep accumulated drift evidence across focus and pause transitions.
            initialized = true; suspended = false; skipNextDelta = true; lastReference = windowStart = referenceSeconds; engineElapsed = 0;
        }
        public bool Observe(double referenceSeconds, double engineDeltaSeconds, out double ratio)
        {
            ratio = 1;
            if (suspended) return false;
            if (!GuardRisk.Finite(referenceSeconds) || !GuardRisk.Finite(engineDeltaSeconds) || engineDeltaSeconds < 0) return false;
            if (skipNextDelta) { skipNextDelta = false; lastReference = windowStart = referenceSeconds; engineElapsed = 0; return false; }
            if (!initialized) { initialized = true; lastReference = windowStart = referenceSeconds; return false; }
            double gap = referenceSeconds - lastReference;
            if (gap < 0 || gap > Math.Max(2, WindowSeconds * 2)) { lastReference = windowStart = referenceSeconds; engineElapsed = 0; return false; }
            lastReference = referenceSeconds; engineElapsed += engineDeltaSeconds;
            double elapsed = referenceSeconds - windowStart;
            if (elapsed < WindowSeconds) return false;
            return CloseWindow(referenceSeconds, out ratio);
        }
        private bool CloseWindow(double referenceSeconds, out double ratio)
        {
            double elapsed = referenceSeconds - windowStart;
            if (elapsed <= 0) { ratio = 1; return false; }
            ratio = engineElapsed / elapsed;
            fastDebt = Math.Max(0, fastDebt + engineElapsed - elapsed * (1 + RelativeTolerance));
            slowDebt = Math.Max(0, slowDebt + elapsed * (1 - RelativeTolerance) - engineElapsed);
            accumulatedEngine += engineElapsed; accumulatedReference += elapsed;
            engineElapsed = 0; windowStart = referenceSeconds;
            observedWindows = Math.Min(RequiredWindows, observedWindows + 1);
            if (observedWindows < RequiredWindows || Math.Max(fastDebt, slowDebt) <= WindowSeconds * RelativeTolerance) return false;
            ratio = accumulatedEngine / accumulatedReference;
            observedWindows = 0; fastDebt = slowDebt = accumulatedEngine = accumulatedReference = 0;
            return true;
        }
    }

    public static class GuardTimeScale
    {
        /// <summary>Check a declared game scale. A hitch may clamp scaled delta downward, so only excess scaled time is compared.</summary>
        public static bool IsUnexpected(double expectedScale, double actualScale, double scaledDelta, double unscaledDelta, double tolerance = .05)
        {
            if (!GuardRisk.Finite(expectedScale) || expectedScale < 0 || !GuardRisk.Finite(actualScale) || actualScale < 0 ||
                !GuardRisk.Finite(scaledDelta) || scaledDelta < 0 || !GuardRisk.Finite(unscaledDelta) || unscaledDelta < 0 ||
                !GuardRisk.Finite(tolerance) || tolerance <= 0) return true;
            double allowance = tolerance * Math.Max(1, expectedScale);
            if (Math.Abs(actualScale - expectedScale) > allowance) return true;
            return unscaledDelta > 0 && scaledDelta / unscaledDelta > expectedScale + allowance;
        }
    }
}
