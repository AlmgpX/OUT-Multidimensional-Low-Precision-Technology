// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;
using System.Collections.Generic;
using NUnit.Framework;
using UnityEngine;

namespace OUT.Motor.Tests
{
    public sealed class OUTMotorMotionCacheTests
    {
        [Test]
        public void CatalogHas128NamedFiniteMotionsAcross16Families()
        {
            var names = new HashSet<string>();
            Assert.AreEqual(128, OUTMotorMotionCache.Count);
            Assert.AreEqual(470016, OUTMotorMotionCache.CachedPoseBytes);
            for (int clip = 0; clip < OUTMotorMotionCache.Count; clip++)
            {
                var info = OUTMotorMotionCache.GetClip(clip);
                Assert.IsTrue(names.Add(info.Name), info.Name);
                Assert.AreEqual(clip / 8, (int)info.Family);
                Assert.Greater(info.Duration, 0f);
                Assert.Greater(info.MuscleScale, 0f);
                for (int frame = 0; frame <= 16; frame++)
                for (int joint = 0; joint < OUTMotorData.Joints; joint++)
                {
                    Vector3 p = OUTMotorMotionCache.SampleNormalized(clip, frame / 16f, joint);
                    Assert.IsFalse(float.IsNaN(p.x) || float.IsNaN(p.y) || float.IsNaN(p.z));
                    Assert.IsFalse(float.IsInfinity(p.x) || float.IsInfinity(p.y) || float.IsInfinity(p.z));
                    Assert.Less(p.sqrMagnitude, 16f, info.Name);
                }
            }
        }

        [Test]
        public void LoopSeamsAndClampedOneShotsRespectMetadata()
        {
            for (int clip = 0; clip < OUTMotorMotionCache.Count; clip++)
            for (int joint = 0; joint < OUTMotorData.Joints; joint++)
            {
                var info = OUTMotorMotionCache.GetClip(clip);
                Vector3 begin = OUTMotorMotionCache.SampleNormalized(clip, 0, joint);
                Vector3 end = OUTMotorMotionCache.SampleNormalized(clip, 1, joint);
                if (info.Looping)
                {
                    Assert.AreEqual(begin, end);
                    Assert.Less(Vector3.Distance(begin, OUTMotorMotionCache.SampleNormalized(clip, .99999f, joint)), .001f);
                }
                else
                {
                    Assert.AreEqual(begin, OUTMotorMotionCache.SampleNormalized(clip, -5, joint));
                    Assert.AreEqual(end, OUTMotorMotionCache.SampleNormalized(clip, 5, joint));
                }
            }
        }

        [Test]
        public void RecoveryFinishesAtStandingAndAttackActuallyExtendsHand()
        {
            int recovery = (int)OUTMotorMotionFamily.Recovery * 8;
            for (int joint = 0; joint < OUTMotorData.Joints; joint++)
                Assert.Less(Vector3.Distance(OUTMotorData.Pose(0, joint),
                    OUTMotorMotionCache.SampleNormalized(recovery, 1, joint)), .04f);
            int strike = (int)OUTMotorMotionFamily.Strike * 8;
            Assert.Greater(OUTMotorMotionCache.SampleNormalized(strike, .5f, 6).z,
                OUTMotorMotionCache.SampleNormalized(strike, 0, 6).z + .2f);
        }

        [Test]
        public void SelectionIsDeterministicAndCoversEveryVariant()
        {
            for (int family = 0; family < 16; family++)
            {
                int mask = 0;
                for (uint seed = 0; seed < 256; seed++)
                {
                    int clip = OUTMotorMotionCache.Select((OUTMotorMotionFamily)family, seed);
                    Assert.AreEqual(clip, OUTMotorMotionCache.Select((OUTMotorMotionFamily)family, seed));
                    Assert.AreEqual(family, clip / 8);
                    mask |= 1 << (clip % 8);
                }
                Assert.AreEqual(255, mask);
            }
        }

        [Test]
        public void BadIndicesAreRejectedAndNonfiniteTimeIsSafe()
        {
            Assert.Throws<ArgumentOutOfRangeException>(() => OUTMotorMotionCache.GetClip(128));
            Assert.Throws<ArgumentOutOfRangeException>(() => OUTMotorMotionCache.Sample(-1, 0, 0));
            Assert.Throws<ArgumentOutOfRangeException>(() => OUTMotorMotionCache.Sample(0, 0, 18));
            Assert.Throws<ArgumentOutOfRangeException>(() => OUTMotorMotionCache.Select((OUTMotorMotionFamily)16, 1));
            Assert.AreEqual(OUTMotorMotionCache.Sample(0, 0, 0), OUTMotorMotionCache.Sample(0, float.NaN, 0));
            Assert.AreEqual(OUTMotorMotionCache.Sample(0, 0, 0), OUTMotorMotionCache.Sample(0, float.PositiveInfinity, 0));
        }

        [Test]
        public void SteadyStateSamplingAllocatesNoManagedBytes()
        {
            Vector3 checksum = Vector3.zero;
            for (int sample = 0; sample < 4096; sample++)
                checksum += OUTMotorMotionCache.Sample(sample & 127, sample * .013f, sample % 18);
            long before = GC.GetAllocatedBytesForCurrentThread();
            for (int sample = 0; sample < 32768; sample++)
                checksum += OUTMotorMotionCache.Sample(sample & 127, sample * .013f, sample % 18);
            long allocated = GC.GetAllocatedBytesForCurrentThread() - before;
            Assert.AreEqual(0, allocated);
            Assert.IsFalse(float.IsNaN(checksum.y));
        }

        [Test]
        public void GaitHasSwingClearanceSharedContactPhasesAndForwardKnees()
        {
            foreach (var family in new[] { OUTMotorMotionFamily.Walk, OUTMotorMotionFamily.Run,
                OUTMotorMotionFamily.Zombie, OUTMotorMotionFamily.Drunk, OUTMotorMotionFamily.Police,
                OUTMotorMotionFamily.Protest, OUTMotorMotionFamily.Sport })
            for (int variant = 0; variant < 8; variant++)
            {
                int clip = (int)family * 8 + variant;
                float maxLeft = 0, maxRight = 0;
                for (int frame = 0; frame < 16; frame++)
                {
                    float t = frame / 16f;
                    byte contact = OUTMotorMotionCache.GetFootContactMask(clip, t);
                    Assert.AreNotEqual(0, contact, "Walking gait must retain at least one support foot.");
                    Assert.AreEqual(contact, OUTMotorMotionCache.GetFootContactMask(clip, t + 1));
                    for (int leg = 0; leg < 2; leg++)
                    {
                        int hipJoint = leg == 0 ? 10 : 14;
                        Vector3 hip = OUTMotorMotionCache.SampleNormalized(clip, t, hipJoint);
                        Vector3 knee = OUTMotorMotionCache.SampleNormalized(clip, t, hipJoint + 1);
                        Vector3 ankle = OUTMotorMotionCache.SampleNormalized(clip, t, hipJoint + 2);
                        float thighLength = Vector3.Distance(OUTMotorData.Pose(0, hipJoint), OUTMotorData.Pose(0, hipJoint + 1));
                        float shinLength = Vector3.Distance(OUTMotorData.Pose(0, hipJoint + 1), OUTMotorData.Pose(0, hipJoint + 2));
                        Assert.That(Vector3.Distance(hip, knee), Is.EqualTo(thighLength).Within(.004f));
                        Assert.That(Vector3.Distance(knee, ankle), Is.EqualTo(shinLength).Within(.004f));
                        Vector3 forward = Vector3.ProjectOnPlane(Vector3.forward, (ankle - hip).normalized).normalized;
                        Assert.GreaterOrEqual(Vector3.Dot(knee - (hip + ankle) * .5f, forward), -.001f);
                        if (leg == 0) maxLeft = Mathf.Max(maxLeft, ankle.y); else maxRight = Mathf.Max(maxRight, ankle.y);
                    }
                }
                Assert.Greater(maxLeft, .17f, family.ToString());
                Assert.Greater(maxRight, .17f, family.ToString());
            }
        }
    }
}
