// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;
using UnityEngine;

namespace OUT.Motor
{
    [Flags]
    public enum OUTMotorInjuryFlags : byte
    {
        None = 0, LeftArm = 1, RightArm = 2, LeftLeg = 4, RightLeg = 8, UnsupportedHuman = 16
    }

    /// <summary>
    /// Low-frequency injury policy audit. Twelve stable actor phases distribute 10Hz work
    /// over the 120Hz fixed solver without per-agent behaviours, timers or temporary lists.
    /// Immediate severing safety belongs to World; an audit is not the first line of defense.
    /// </summary>
    public sealed class OUTMotorInjuryScheduler
    {
        public const int PhaseCount = 12;
        private readonly byte[] flags;
        private readonly byte[] overstress;
        private static readonly int[] LimbJoints = {5, 8, 11, 15};
        private static readonly float[] RestLengthSquared = CreateRestLengths();
        private int lastFrame = -1;
        public event Action<int, int> LimbLost;
        public int LastVisited { get; private set; }
        public int LastChanged { get; private set; }
        public int Capacity { get { return flags.Length; } }

        public OUTMotorInjuryScheduler(int capacity)
        {
            if (capacity < 1) throw new ArgumentOutOfRangeException(nameof(capacity));
            flags = new byte[capacity];
            overstress = new byte[capacity * 4];
        }

        public OUTMotorInjuryFlags GetFlags(int actor)
        {
            if ((uint)actor >= flags.Length) throw new ArgumentOutOfRangeException(nameof(actor));
            return (OUTMotorInjuryFlags)flags[actor];
        }
        public void Reset()
        {
            Array.Clear(flags,0,flags.Length);
            Array.Clear(overstress,0,overstress.Length);
            lastFrame=-1;LastVisited=LastChanged=0;
        }

        public void Tick(OUTMotorWorld world, bool tearOverstressedLimbs = false, float tearLengthRatio = 1.8f)
        {
            if (world == null) throw new ArgumentNullException(nameof(world));
            if (world.Count > Capacity) throw new ArgumentException("Scheduler capacity is smaller than the world.", nameof(world));
            LastVisited = LastChanged = 0;
            if (world.Frame == lastFrame) return;
            lastFrame = world.Frame;
            int phase = world.Frame % PhaseCount;
            float ratio = float.IsNaN(tearLengthRatio) || float.IsInfinity(tearLengthRatio) ? 1.8f : Mathf.Max(1.5f, tearLengthRatio);
            float ratioSquared = ratio * ratio;
            for (int actor = phase; actor < world.Count; actor += PhaseCount)
            {
                LastVisited++;
                OUTMotorInjuryFlags injury = OUTMotorInjuryFlags.None;
                for (int limb = 0; limb < 4; limb++)
                {
                    int joint = LimbJoints[limb], streak = actor * 4 + limb;
                    if (world.IsDetached(actor, joint))
                    {
                        injury |= (OUTMotorInjuryFlags)(1 << limb);
                        overstress[streak] = 0;
                    }
                    else if (tearOverstressedLimbs && IsOverstressed(world, actor, joint, RestLengthSquared[limb] * ratioSquared))
                    {
                        overstress[streak] = (byte)Mathf.Min(3, overstress[streak] + 1);
                        if (overstress[streak] >= 3 && world.QueueSever(actor, joint)) overstress[streak] = 0;
                    }
                    else overstress[streak] = 0;
                }
                if ((injury & (OUTMotorInjuryFlags.LeftLeg | OUTMotorInjuryFlags.RightLeg)) != 0
                    && world.Anatomy[actor] == (byte)OUTMotorAnatomy.Human)
                {
                    injury |= OUTMotorInjuryFlags.UnsupportedHuman;
                    // World clamps AI intent and motor support to its low prone policy.
                    if (world.GetState(actor) != OUTMotorState.Dead) world.BeginCrawl(actor);
                }
                byte previous = flags[actor];
                if (previous != (byte)injury)
                {
                    flags[actor] = (byte)injury; LastChanged++;
                    int newLoss = (byte)injury & ~previous & 15;
                    for (int limb = 0; limb < 4; limb++)
                        if ((newLoss & (1 << limb)) != 0) LimbLost?.Invoke(actor, LimbJoints[limb]);
                }
            }
        }

        private static bool IsOverstressed(OUTMotorWorld world, int actor, int joint, float stretchLimitSquared)
        {
            Vector3 middle = world.GetJoint(actor, joint);
            Vector3 upper = middle - world.GetJoint(actor, OUTMotorData.Parents[joint]);
            float upperSquared = upper.sqrMagnitude;
            if (upperSquared > stretchLimitSquared) return true;
            Vector3 lower = world.GetJoint(actor, joint + 1) - middle;
            float product = upperSquared * lower.sqrMagnitude;
            float dot = Vector3.Dot(upper, lower);
            // cos < -0.95: sustained near-total fold; no normalization, sqrt or acos.
            // Degenerate zero-length segments cannot establish a reliable bend angle.
            return product > 1e-10f && dot < 0f && dot * dot > .9025f * product;
        }

        private static float[] CreateRestLengths()
        {
            var lengths = new float[4];
            for (int limb = 0; limb < 4; limb++)
            {
                int joint = LimbJoints[limb];
                lengths[limb] = (OUTMotorData.Pose(0, joint) - OUTMotorData.Pose(0, OUTMotorData.Parents[joint])).sqrMagnitude;
            }
            return lengths;
        }
    }
}
