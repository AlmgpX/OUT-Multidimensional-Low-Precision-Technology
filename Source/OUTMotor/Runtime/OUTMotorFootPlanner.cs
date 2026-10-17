// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using UnityEngine;

namespace OUT.Motor

{

    /// <summary>Persistent world-space foot contacts, driven by the sampled clip's contact phase.

    /// Writes muscle targets only. It never moves physical particles or the actor root.</summary>

    public sealed class OUTMotorFootPlanner

    {

        private readonly Vector3[] planted, liftOff, landing, plantedForward, landingForward;

        private readonly float[] progress, duration;

        private readonly byte[] swinging, initialized;

        private readonly float upperLength, lowerLength, toeLength;

        public int Capacity { get { return initialized.Length; } }

        public bool HasUnfinishedStep(int actor)

        {

            if((uint)actor>=(uint)Capacity)throw new ArgumentOutOfRangeException(nameof(actor));

            return swinging[actor*2]!=0||swinging[actor*2+1]!=0;

        }

        public OUTMotorFootPlanner(int capacity)

        {

            if(capacity<1)throw new ArgumentOutOfRangeException(nameof(capacity));

            planted=new Vector3[capacity*2];liftOff=new Vector3[capacity*2];landing=new Vector3[capacity*2];

            plantedForward=new Vector3[capacity*2];landingForward=new Vector3[capacity*2];

            progress=new float[capacity*2];duration=new float[capacity*2];swinging=new byte[capacity*2];initialized=new byte[capacity];

            upperLength=Vector3.Distance(OUTMotorData.Pose(0,10),OUTMotorData.Pose(0,11));

            lowerLength=Vector3.Distance(OUTMotorData.Pose(0,11),OUTMotorData.Pose(0,12));

            toeLength=Vector3.Distance(OUTMotorData.Pose(0,12),OUTMotorData.Pose(0,13));

        }

        public void ResetActor(int actor,Vector3[] positions,int offset,Quaternion heading)

        {

            if((uint)actor>=(uint)Capacity)throw new ArgumentOutOfRangeException(nameof(actor));

            for(int foot=0;foot<2;foot++)

            {

                int slot=actor*2+foot,joint=foot==0?12:16;

                planted[slot]=positions[offset+joint];plantedForward[slot]=heading*Vector3.forward;

                liftOff[slot]=landing[slot]=planted[slot];progress[slot]=0;swinging[slot]=0;

            }

            initialized[actor]=1;

        }

        public void Apply(int actor,float dt,float normalizedTime,byte contactMask,float clipDuration,

            Vector3 worldVelocity,Quaternion heading,Vector3 anchor,Vector3[] goals,Vector3[] goalVelocity,

            byte[] detached,int offset,bool running,bool authoredSwing=false)

        {

            if((uint)actor>=(uint)Capacity)throw new ArgumentOutOfRangeException(nameof(actor));

            if(initialized[actor]==0)ResetActor(actor,goals,offset,heading);

            dt=Mathf.Clamp(dt,.001f,.1f);

            bool moving=worldVelocity.sqrMagnitude>.0001f;

            Vector3 forward=heading*Vector3.forward,right=heading*Vector3.right;

            for(int foot=0;foot<2;foot++)

            {

                int slot=actor*2+foot,ankle=foot==0?12:16,toe=ankle+1,knee=ankle-1,hip=ankle-2;

                if(detached[offset+ankle]!=0){swinging[slot]=0;continue;}

                bool wantsSwing=moving&&(contactMask&(1<<foot))==0;

                if(authoredSwing&&wantsSwing)

                {

                    // A clip may contain multiple steps. Its sampled swing and contact mask

                    // own the timing; normalized whole-clip phase is not a gait cycle.

                    swinging[slot]=2;landing[slot]=goals[offset+ankle];landingForward[slot]=forward;

                    continue;

                }

                if(swinging[slot]==2)

                {

                    if(moving)

                    {

                        planted[slot]=goals[offset+ankle];

                        // Retargeted skeleton proportions can leave the sampled ankle above

                        // the flat world floor. A contact transition must plant on that floor.

                        planted[slot].y=OUTMotorData.Radii[ankle];

                        plantedForward[slot]=forward;swinging[slot]=0;

                    }

                    else

                    {

                        liftOff[slot]=landing[slot];landing[slot].y=OUTMotorData.Radii[ankle];

                        progress[slot]=0;duration[slot]=.2f;swinging[slot]=3;

                    }

                }

                float phase=Mathf.Repeat(normalizedTime+foot*.5f,1f);

                // Mirrored variants swap contact bits. Select the matching half-cycle.

                if(wantsSwing&&phase<.6f)phase=Mathf.Repeat(phase+.5f,1f);

                float authoredProgress=Mathf.Clamp01((phase-.6f)/.4f);

                if(wantsSwing&&swinging[slot]==0)

                {

                    swinging[slot]=1;liftOff[slot]=planted[slot];

                    duration[slot]=Mathf.Max(.12f,clipDuration*.4f);

                    float reach=Mathf.Min(running?.5f:.35f,worldVelocity.magnitude*duration[slot]*.5f);

                    landing[slot]=anchor+right*(foot==0?-.15f:.15f)+forward*reach;

                    landing[slot].y=OUTMotorData.Radii[ankle];

                    landingForward[slot]=forward;progress[slot]=authoredProgress;

                }

                if(swinging[slot]!=0)

                {

                    if(wantsSwing)progress[slot]=Mathf.Max(progress[slot],authoredProgress);

                    else if(moving)progress[slot]=1f;

                    else progress[slot]=Mathf.Min(1f,progress[slot]+dt/duration[slot]);

                    if(progress[slot]>=.9999f)

                    {

                        planted[slot]=landing[slot];plantedForward[slot]=landingForward[slot];swinging[slot]=0;

                    }

                }

                float u=progress[slot];

                float lift=swinging[slot]!=0&&swinging[slot]!=3?Mathf.Sin(u*Mathf.PI)*(running?.18f:.1f):0;

                float blend=u*u*(3f-2f*u);

                Vector3 target=swinging[slot]!=0?Vector3.LerpUnclamped(liftOff[slot],landing[slot],blend)+Vector3.up*lift:planted[slot];

                Vector3 footForward=swinging[slot]!=0?landingForward[slot]:plantedForward[slot];

                SetGoal(offset+ankle,target,dt,goals,goalVelocity);

                float toeDeltaY=OUTMotorData.Radii[toe]-OUTMotorData.Radii[ankle];

                float horizontalToe=Mathf.Sqrt(Mathf.Max(0,toeLength*toeLength-toeDeltaY*toeDeltaY));

                SetGoal(offset+toe,target+footForward*horizontalToe+Vector3.up*toeDeltaY,dt,goals,goalVelocity);

                Vector3 hipGoal=goals[offset+hip],chord=target-hipGoal;

                float distance=chord.magnitude;

                if(distance>.0001f)

                {

                    Vector3 axis=chord/distance;

                    float reachable=Mathf.Clamp(distance,.05f,upperLength+lowerLength-.002f);

                    float along=(upperLength*upperLength-lowerLength*lowerLength+reachable*reachable)/(2f*reachable);

                    float bend=Mathf.Sqrt(Mathf.Max(.0016f,upperLength*upperLength-along*along));

                    Vector3 bendForward=Vector3.ProjectOnPlane(forward,axis).normalized;

                    if(bendForward.sqrMagnitude<.01f)bendForward=Vector3.ProjectOnPlane(Vector3.up,axis).normalized;

                    Vector3 kneeTarget=hipGoal+axis*along+bendForward*bend;

                    SetGoal(offset+knee,Vector3.LerpUnclamped(goals[offset+knee],kneeTarget,.65f),dt,goals,goalVelocity);

                }

            }

        }

        private static void SetGoal(int joint,Vector3 target,float dt,Vector3[] goals,Vector3[] velocities)

        {

            Vector3 previous=goals[joint]-velocities[joint]*dt;

            goals[joint]=target;velocities[joint]=(target-previous)/dt;

        }

    }

}
