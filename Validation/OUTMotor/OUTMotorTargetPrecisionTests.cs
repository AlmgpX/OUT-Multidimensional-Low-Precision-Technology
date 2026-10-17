// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using NUnit.Framework;
using UnityEngine;

namespace OUT.Motor.Tests
{
    public sealed class OUTMotorTargetPrecisionTests
    {
        private static OUTMotorAnimationBank IdleBank(int chestExcursionMillimetres)
        {
            var bank=ScriptableObject.CreateInstance<OUTMotorAnimationBank>();
            bank.clips=new[]{new OUTMotorBakedClip{family=OUTMotorMotionFamily.Idle,
                name="Idle precision fixture",duration=.37f,frameCount=4,looping=true}};
            bank.samples=new short[4*OUTMotorAnimationBank.ChannelsPerFrame];
            bank.footContacts=new byte[]{3,3,3,3};
            for(int frame=0;frame<4;frame++)
            for(int joint=0;joint<OUTMotorData.Joints;joint++)
            {
                Vector3 p=OUTMotorData.Pose(0,joint);
                int offset=frame*OUTMotorAnimationBank.ChannelsPerFrame+joint*3;
                bank.samples[offset]=(short)Mathf.RoundToInt(p.x*1000f);
                bank.samples[offset+1]=(short)Mathf.RoundToInt(p.y*1000f);
                bank.samples[offset+2]=(short)Mathf.RoundToInt(p.z*1000f);
                if(joint==2)bank.samples[offset]+=(short)(frame==1?chestExcursionMillimetres:frame==2?-chestExcursionMillimetres:0);
            }
            bank.Warmup();return bank;
        }

        [TestCase(false)]
        [TestCase(true)]
        public void MillimetreIdleNoiseDoesNotCreateMotorTargetVelocity(bool native)
        {
            var bank=IdleBank(1);
            try
            {
                using var world=new OUTMotorWorld(null,1);
                if(native)
                {
#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN
                    Assert.IsTrue(world.NativeSolverAvailable,"Windows native plugin is required: "+world.NativeSolverLoadError);
#else
                    if(!world.NativeSolverAvailable)Assert.Ignore("Native plugin unavailable on this platform.");
#endif
                }
                world.NativeSolverEnabled=native;
                world.Spawn(0,Vector3.zero,0,70,0,OUTMotorRole.Zombie);
                world.AnimationBank=bank;
                Vector3 original=world.Goal[2];
                for(int tick=0;tick<120;tick++)
                {
                    world.Tick();
                    Assert.AreEqual(original,world.Goal[2],"Sub-resolution authored noise must not issue a new chest goal.");
                    Assert.AreEqual(Vector3.zero,world.GoalVelocity[2]);
                }
                Assert.AreEqual(OUTMotorState.Active,world.GetState(0));
                Assert.AreEqual(10,world.TargetPoseUpdateCount);
            }
            finally{Object.DestroyImmediate(bank);}
        }

        [Test]
        public void LargerAuthoredMotionStillPassesTheCoarseTargetBoundary()
        {
            var bank=IdleBank(18);
            try
            {
                using var world=new OUTMotorWorld(null,1);
                world.NativeSolverEnabled=false;
                world.Spawn(0,Vector3.zero,0,70,0,OUTMotorRole.Zombie);
                world.AnimationBank=bank;
                float excursion=0;
                for(int tick=0;tick<120;tick++)
                {world.Tick();excursion=Mathf.Max(excursion,Mathf.Abs(world.Goal[2].x));}
                Assert.Greater(excursion,.01f,"The deadband must not freeze intended centimetre-scale motion.");
            }
            finally{Object.DestroyImmediate(bank);}
        }

        [Test]
        public void SettledIdleRefreshesHalfAsOftenAsLocomotion()
        {
            using var idle=new OUTMotorWorld(null,1);
            using var walking=new OUTMotorWorld(null,1);
            idle.NativeSolverEnabled=walking.NativeSolverEnabled=false;
            idle.Spawn(0,Vector3.zero,0,70,0,OUTMotorRole.Zombie);
            walking.Spawn(0,Vector3.zero,0,70,0,OUTMotorRole.Zombie);
            walking.SetLocomotion(0,Vector3.forward,.7f);
            for(int tick=0;tick<120;tick++){idle.Tick();walking.Tick();}
            Assert.AreEqual(10,idle.TargetPoseUpdateCount);
            Assert.AreEqual(20,walking.TargetPoseUpdateCount);
            Assert.AreEqual(OUTMotorState.Active,idle.GetState(0));
            Assert.AreEqual(OUTMotorState.Active,walking.GetState(0));
            Assert.Greater(walking.GetAnchor(0).z,.6f);
        }
    }
}
